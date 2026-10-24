#include "SurfaceCoverInternal.hpp"

#include "../env/Lights.hpp"

#include <cstdio>

namespace wxl_livingazeroth::cover::detail
{
    // VS: c0..c3 view-projection columns; c4 = (patch first cell i, j, cell size, 1 / texture
    // size); c5 = (level centre cell i, j, half extent, morph band) in cells; c6 = the camera
    // position (the scene is drawn about the camera); c7 = (fade start, fade end, fade on) in cells;
    // c8 = (far lift start, end, height) in yd from the camera; c9..c11 = the view rotation rows
    // (identity when not lit like the scene); c12 = the terrain's fog parameters; c13.x = tint on.
    const char* kVsHlsl = R"(
float4 vp0 : register(c0); float4 vp1 : register(c1); float4 vp2 : register(c2); float4 vp3 : register(c3);
float4 grid : register(c4);
float4 lod : register(c5);
float4 eye : register(c6);
float4 fade : register(c7);
float4 lift : register(c8);
float4 vr0 : register(c9); float4 vr1 : register(c10); float4 vr2 : register(c11);
float4 fogp : register(c12);
float4 look : register(c13);
float4 farMap : register(c14);   // 1 / chunk size, map size, 1 / map size, on
float4 farBox : register(c15);   // the far map's box, world yd (min x, min y, max x, max y)
float4 farMisc : register(c16);  // x = the value outside that box
float4 pointInfo : register(c17);   // x = point lights in use (env/Lights)
float4 points[96] : register(c20);  // per light: (view-space position, 1 / radius), (colour, falloff shape),
                                    // (view-space spot direction, cos outer angle), (1 / (cos inner - cos outer))
sampler2D baseTex : register(s0);
sampler2D coverTex : register(s1);
sampler2D lookTex : register(s2);
sampler2D farTex : register(s3);  // ground moisture beyond the near grid, per terrain chunk (R32F)

// The far map at a world position, filtered here (vertex textures aren't).
float FarAt(float2 w)
{
    float2 t = w * farMap.x - 0.5;
    float2 f = frac(t);
    float4 uv = float4((t - f + 0.5) * farMap.z, 0, 0);
    float a = tex2Dlod(farTex, uv).r;
    float b = tex2Dlod(farTex, uv + float4(farMap.z, 0, 0, 0)).r;
    float c = tex2Dlod(farTex, uv + float4(0, farMap.z, 0, 0)).r;
    float d = tex2Dlod(farTex, uv + float4(farMap.z, farMap.z, 0, 0)).r;
    float v = lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
    float inside = (w.x >= farBox.x && w.y >= farBox.y && w.x <= farBox.z && w.y <= farBox.w) ? farMap.w : 0;
    return lerp(farMisc.x, v, inside);
}

struct VOut { float4 pos : POSITION; float3 n : TEXCOORD0; float2 d : TEXCOORD1; float2 rel : TEXCOORD2; float fog : TEXCOORD3; float4 tint : TEXCOORD4; float2 wuv : TEXCOORD5; float3 vpos : TEXCOORD6; float far : TEXCOORD7; float3 lamps : TEXCOORD8; };

// Point lights (torches; env/Lights) at a view-space position with a view-space unit normal:
// colour x a falloff like the client's 1 / (0.7 d + 0.03 d^2), windowed to exactly 0 at the
// light's radius. The light has a size (~1 yd, like a flame): the falloff uses sqrt(d^2 + 1), so
// right under it it tops out at the client's value at 1 yd instead of a hot spot with a hard edge
// (a torch flame ends up just above deep snow). Wrapped Lambert ((N.L + 0.5) / 1.5), so a low light
// still lights the surface around it, not just at grazing angles. Falloff shape: the window to the
// power of the light's falloff (1 = smooth, higher = tighter). Spot lights: x saturate((cos to the
// axis - cos outer) / (cos inner - cos outer)); point lights have cos outer -2 (always inside). Per
// vertex: the cover's mesh is dense (0.25 yd near the player), and pixel shaders can't index constants.
float3 Lamps(float3 vp, float3 nv)
{
    float3 sum = 0;
    [loop] for (int k = 0; k < 24; ++k)
    {
        if (k >= pointInfo.x) break;
        float4 p = points[k * 4], col = points[k * 4 + 1], spot = points[k * 4 + 2];
        float spotScale = points[k * 4 + 3].x;
        float3 dv = p.xyz - vp;
        float d2 = dot(dv, dv);
        float d = sqrt(d2);
        float3 l = dv / max(d, 0.001);
        float t = saturate(d * p.w);
        float win = 1 - t * t;
        win = pow(win * win, col.w);
        float de = sqrt(d2 + 1);
        float wrap = saturate((dot(nv, l) + 0.5) / 1.5);
        float cone = saturate((dot(-l, spot.xyz) - spot.w) * spotScale);
        sum += col.rgb * win * wrap * cone / (de * (0.7 + 0.03 * de));
    }
    return sum;
}

float4 Tint(float2 idx) { return look.x > 0.5 ? tex2Dlod(lookTex, float4((idx + 0.5) * grid.w, 0, 0)) : 0; }


float Cheb(float2 idx) { float2 a = abs(idx - lod.xy); return max(a.x, a.y); }
float Fade(float2 idx) { return fade.z > 0.5 ? saturate((fade.y - Cheb(idx)) / (fade.y - fade.x)) : 1; }

// The cover texture holds the depth, negative where the cover sits below the terrain (ZOffset < 0):
// such a cover is lowered far away instead of lifted.
// x = base height, y = cover depth
float2 Sample(float2 idx)
{
    float2 uv = (idx + 0.5) * grid.w;
    return float2(tex2Dlod(baseTex, float4(uv, 0, 0)).r, abs(tex2Dlod(coverTex, float4(uv, 0, 0)).r) * Fade(idx));
}

float B(float2 idx) { return tex2Dlod(baseTex, float4((idx + 0.5) * grid.w, 0, 0)).r; }
float C(float2 idx) { return abs(tex2Dlod(coverTex, float4((idx + 0.5) * grid.w, 0, 0)).r) * Fade(idx); }
float LiftDir(float2 idx) { return tex2Dlod(coverTex, float4((idx + 0.5) * grid.w, 0, 0)).r < 0 ? -1 : 1; }
// Inside the level's filled area (its grid plus the 2-cell apron): beyond it the slots hold stale heights.
float2 Inside(float2 idx) { return clamp(idx, lod.xy - lod.z - 2, lod.xy + lod.z + 2); }

// step = neighbour distance for the normal in cells (2 = the coarser level's spacing).
// The slope has two parts. The terrain's: the terrain is flat triangles (4 per 4.17 yd cell) and
// shades itself with smooth vertex normals, so measured over 0.25 yd its slope jumps at every crease
// and the lit cover shows the triangles -- it's measured over ~2.5 yd instead (the short step near
// holes). The cover's own (drifts, trenches, rims): the fine step.
void Vertex(float2 idx, float step, out float3 p, out float3 n, out float2 s)
{
    s = Sample(idx);
    p = float3(idx * grid.z, s.x + s.y);
    float wide = max(step, floor(2.5 / grid.z));
    float2 xa = Inside(idx - float2(wide, 0)), xb = Inside(idx + float2(wide, 0));
    float2 ya = Inside(idx - float2(0, wide)), yb = Inside(idx + float2(0, wide));
    float bxa = B(xa), bxb = B(xb), bya = B(ya), byb = B(yb);
    float2 terrain;
    [branch] if (min(min(bxa, bxb), min(bya, byb)) < -10000)
        terrain = float2(B(idx - float2(step, 0)) - B(idx + float2(step, 0)), B(idx - float2(0, step)) - B(idx + float2(0, step))) / (2 * step * grid.z);
    else
        terrain = float2((bxa - bxb) / max(xb.x - xa.x, 1), (bya - byb) / max(yb.y - ya.y, 1)) / grid.z;
    float2 cover = float2(C(idx - float2(step, 0)) - C(idx + float2(step, 0)), C(idx - float2(0, step)) - C(idx + float2(0, step))) / (2 * step * grid.z);
    n = float3(terrain + cover, 1);
}

VOut main(float2 ij : POSITION)
{
    VOut o;
    float2 idx = grid.xy + ij;
    float3 p, n; float2 s;
    Vertex(idx, 1, p, n, s);
    o.tint = Tint(idx);
    o.wuv = (idx + 0.5) * grid.w;
    float dir = LiftDir(idx);

    // Border band: odd vertices slide onto their even neighbours, so the edge becomes the coarser
    // level's triangles.
    float m = lod.w > 1 ? saturate((Cheb(idx) - (lod.z - lod.w)) / (lod.w - 1)) : 0;
    [branch] if (m > 0)
    {
        float2 odd = frac(idx * 0.5) * 2;
        float3 p1, n1; float2 s1;
        Vertex(idx - odd, 2, p1, n1, s1);
        // Fully morphed takes the coarser vertex exactly: lerp(p, p1, 1) = p + (p1 - p) rounds off p1
        // at world-sized coordinates, and the two levels' edges no longer meet (hairline cracks).
        float4 t1 = Tint(idx - odd);
        float2 w1 = (idx - odd + 0.5) * grid.w;
        if (m >= 1) { p = p1; n = n1; s = s1; o.tint = t1; o.wuv = w1; dir = LiftDir(idx - odd); }
        else        { p = lerp(p, p1, m); n = lerp(normalize(n), normalize(n1), m); s = lerp(s, s1, m); o.tint = lerp(o.tint, t1, m); o.wuv = lerp(o.wuv, w1, m); }
    }

    o.far = FarAt(p.xy);

    // Far away the client draws simpler terrain than its heights, which pokes through a thin
    // cover; a lift that only depends on the distance stays seamless across the levels. A cover
    // below the terrain (mud whose rims show through) is lowered instead, so the simpler far terrain
    // hides it rather than the lift pushing it into view.
    float dist = length(float3(p.xy - eye.xy, p.z - eye.z));
    p.z += dir * lift.z * saturate((dist - lift.x) / (lift.y - lift.x)) * saturate(s.y * 10);

    // Normal and position into view space, where the terrain shader lights and fogs (its sun
    // direction is view space; its fog runs on the view depth).
    float3 nw = normalize(n);
    o.n = nw.x * vr0.xyz + nw.y * vr1.xyz + nw.z * vr2.xyz;
    o.rel = p.xy - eye.xy;
    float4 q = float4(o.rel, p.z - eye.z, 1);
    // The depth tested is pulled toward the camera along the vertex's own ray, so the cover stays where it is on the
    // screen: about 1 cm at 30 yd and 10 cm at 100 yd (as the depth buffer's precision falls off, with the square of the
    // distance), at most 30 cm. Where the cover thins out to the terrain -- drifting sand, its edges, a print filling
    // in -- the two fought for the far pixels ("lil bit of possibly actual z-fighting on this sand here", the user,
    // 2026-10-12). Shading keeps the real position (vpos).
    float reach = length(q.xyz);
    float4 qd = float4(q.xyz * (1 - min(1e-5 * reach * reach, 0.3) / max(reach, 1)), 1);
    o.pos = float4(dot(qd, vp0), dot(qd, vp1), dot(qd, vp2), dot(qd, vp3));
    o.vpos = q.x * vr0.xyz + q.y * vr1.xyz + q.z * vr2.xyz;
    float viewZ = o.vpos.z;
    o.fog = min(pow(max(viewZ * fogp.x + fogp.y, 0), fogp.z), 1);
    o.lamps = pointInfo.x > 0 ? Lamps(o.vpos, normalize(o.n)) : 0;
    o.d = float2(s.y, s.x < -10000 ? 1 : 0);
    return o;
}
)";

    // PS: c0 = light direction, c1 = colour, c2.x = smallest drawn depth, c3 = the next finer
    // level's box (camera-relative min x, min y, max x, max y): this level isn't drawn there;
    // c4 = ambient, c5 = diffuse, c6 = fog colour; c8/c9 = per cover texture slot 1 when its
    // alpha is a specular mask; c10 = specular colour (rgb) and exponent (w). The tint (rgb, a =
    // strength) comes from the VS, wetness and vertex colour from the props texture.
    // Lit the way the terrain shader lights: min(ambient + diffuse * saturate(N.L), 1), plus
    // Blinn-Phong specular in view space (terrain VS: pow(N.normalize(L + normalize(-pos)), c27.w)
    // x c27.rgb, times the layer texture's alpha in its PS), then fogged towards the fog colour.
    const char* kPsHlsl = R"(
float4 light : register(c0);
float4 albedo : register(c1);
float4 opts : register(c2);
float4 inner : register(c3);
float4 ambient : register(c4);
float4 diffuse : register(c5);
float4 fogColor : register(c6);
float4 texMap : register(c7);   // 1 / tile size (yd), uv offset (x, y): uv = rel * x + yz
float4 specOn0 : register(c8);
float4 specOn1 : register(c9);
float4 specular : register(c10);
float4 wetMap : register(c11);  // 1 / grid extent (yd), uv offset (x, y), on
float4 wetBox : register(c12);  // the grid's valid box, camera-relative (min x, min y, max x, max y)
float4 debugView : register(c186); // x = 0 off, 1 cover texture share, 2 specular only, 3 lighting only (upstream's c14 holds the fork's edge fade)
float4 misc : register(c13);     // x = the terrain's dynamic shadows: 0 none, 1 its one map, 2 its near box and its cascades,
                                 // 3 its cascades; y = (unused), z = 1 / the near grid's edge fade (yd),
                                 // w = how much darker soaked ground gets (the wet terrain's strength, so both match)
float4 edgeFade : register(c14); // player centre relative to camera (xy), fully covered radius, zero coverage radius (yd)
float4 rippleInfo : register(c15);  // rings around prints in sand: x = how many, y = their slope, z = brightness change
float4 rippleAxisX : register(c16); // the world's x and y axes in the space the normal is lit in
float4 rippleAxisY : register(c17);
float4 rippleBox : register(c18);   // where any ring reaches, camera-relative (min x, min y, max x, max y)
float4 dissolve : register(c19);    // the camera's world xy mod kDissolvePeriod, 1 / the dissolve's patch size (yd)
float4 pcf[8] : register(c20);   // the terrain's PCF offsets (its PS c3..c10)
float4 shadowM[12] : register(c30); // the terrain's shadow matrices (its VS c37..c48), from view space
float4 ripples[144] : register(c42); // per print, three registers: its outline's shape (CoverRipples.hpp PackRim)
sampler2D cover0 : register(s0);
sampler2D cover1 : register(s1);
sampler2D cover2 : register(s2);
sampler2D cover3 : register(s3);
sampler2D cover4 : register(s4);
sampler2D cover5 : register(s5);
sampler2D cover6 : register(s6);
sampler2D cover7 : register(s7);
sampler2D weights0 : register(s8); // slots 1..4 (r..a)
sampler2D weights1 : register(s9); // slots 5..8
sampler2D props : register(s10);   // a = wetness, rgb = vertex colour (0.5 = neutral)
sampler2D wetGrid : register(s11); // a = ground moisture above the material's rest (env/Fields)
sampler2D shadow0 : register(s12); // the terrain's shadow maps (its s5..s8): three cascades, then far
sampler2D shadow1 : register(s13);
sampler2D shadow2 : register(s14);
sampler2D shadow3 : register(s15);

float Tap(sampler2D s, float2 uv, float z) { return tex2Dlod(s, float4(uv, z, 0)).x; }

// 1 = lit, 0 = in shadow. The second and third cascades, then the far map fading out: 5 taps each.
float ShadowOuter(float3 vp)
{
    float4 p = float4(vp, 1);
    float4 a = float4(0, 0, 0, dot(p, shadowM[9]));
    float4 b = float4(dot(p, shadowM[3]), dot(p, shadowM[4]), dot(p, shadowM[5]), dot(p, shadowM[10]));
    float4 c = float4(dot(p, shadowM[6]), dot(p, shadowM[7]), dot(p, shadowM[8]), dot(p, shadowM[11]));
    [branch] if (max(abs(b.x), abs(b.y)) < 1)
    {
        float2 uv = b.xy * 0.5 + 0.5;
        float s = Tap(shadow1, uv, b.z);
        [unroll] for (int k = 0; k < 8; k += 2) s += Tap(shadow1, uv + pcf[k].xy, b.z);
        return s * 0.2;
    }
    [branch] if (max(abs(c.x), abs(c.y)) < 1)
    {
        float2 uv = c.xy * 0.5 + 0.5;
        float s = Tap(shadow2, uv, c.z);
        [unroll] for (int k = 0; k < 8; k += 2) s += Tap(shadow2, uv + pcf[k].xy, c.z);
        return s * 0.2;
    }
    float2 uv = float2(a.w, b.w) * 0.5 + 0.5;
    float s = Tap(shadow3, uv, c.w);
    [unroll] for (int k = 0; k < 8; k += 2) s += Tap(shadow3, uv + pcf[k].xy, c.w);
    float edge = saturate(max(abs(a.w), abs(b.w)) * -11.1111107 + 11);
    return edge * (s * 0.2 - 1) + 1;
}

// Shadow quality 5-6 (the client's terrain shadow mode 3, Terrain3_pcf_18, dumped 2026-10-02): inside the nearest
// cascade 9 taps of it and nothing else; outside it, the other cascades.
float ShadowCascades(float3 vp)
{
    float4 p = float4(vp, 1);
    float3 a = float3(dot(p, shadowM[0]), dot(p, shadowM[1]), dot(p, shadowM[2]));
    [branch] if (saturate(max(abs(a.x), abs(a.y)) * -3.4482758 + 3.41379309) > 0)
    {
        float2 uv = a.xy * 0.5 + 0.5;
        float s = Tap(shadow0, uv, a.z);
        [unroll] for (int k = 0; k < 8; ++k) s += Tap(shadow0, uv + pcf[k].xy, a.z);
        return s / 9;
    }
    return ShadowOuter(vp);
}

// The near box: the first map (VS c37..c39, on s5), 5 PCF taps, faded out over the box's outer edge. Alone it is
// shadow quality 1-2 (the client's terrain shadow mode 1). Quality 3-4 (mode 2, Terrain3_pcf_10) takes the darker
// of it and the other cascades everywhere; the cover used the 5-6 shader there, which inside the near box reads s5
// alone, so nothing near the player shadowed the snow (the user, 2026-10-10: "still snow shadow box problems").
float ShadowSingle(float3 vp)
{
    float4 p = float4(vp, 1);
    float3 a = float3(dot(p, shadowM[0]), dot(p, shadowM[1]), dot(p, shadowM[2]));
    float2 uv = a.xy * 0.5 + 0.5;
    float s = Tap(shadow0, uv, a.z);
    [unroll] for (int k = 0; k < 8; k += 2) s += Tap(shadow0, uv + pcf[k].xy, a.z);
    float fade = saturate(max(abs(a.x), abs(a.y)) * -3.4482758 + 3.41379309);
    return fade * (s * 0.2 - 1) + 1;
}

// Coverage, rather than only height, must fade or thin snow still ends as an
// opaque square. Every clipmap level uses the same circle to avoid LOD seams.
float CoverVisibility(float2 rel)
{
    float t = saturate((edgeFade.w - length(rel - edgeFade.xy)) / max(edgeFade.w - edgeFade.z, 0.001));
    return t * t * (3 - 2 * t);
}
// The cover's outer ring dissolves in patches of the ground, not of the screen: a fixed 8x8 screen pattern stayed put
// while the ground moved under it, and crawled like z-fighting whenever the camera moved ("some z-fighting with the
// snow cover mod ... near the edges specifically", the user, 2026-10-12). Smooth value noise in world space, about
// 1 / dissolve.z yd across, two octaves; its lattice repeats every 512 cells, as the camera's position is passed mod
// a whole number of them, so it never jumps. Discarded pixels keep the terrain's depth (alpha-blending while writing
// depth would leave an invisible occluding sheet).
float DissolveHash(float2 c)
{
    c -= 512 * floor(c / 512);
    return frac(sin(dot(c, float2(127.1, 311.7))) * 43758.5453);
}
float DissolveNoise(float2 w)
{
    float2 i = floor(w), f = w - i;
    f = f * f * (3 - 2 * f);
    float a = DissolveHash(i), b = DissolveHash(i + float2(1, 0)), c = DissolveHash(i + float2(0, 1)), d = DissolveHash(i + 1);
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}
float CoverThreshold(float2 rel)
{
    float2 w = (rel + dissolve.xy) * dissolve.z;
    float n = 0.65 * DissolveNoise(w) + 0.35 * DissolveNoise(w * 2.73 + 17.3);
    // Never 0 or 1: past the ring every pixel goes, inside it every pixel stays.
    return saturate((n - 0.5) * 1.8 + 0.5) * 0.98 + 0.01;
}
)";
        // The rims around prints in sand, kept apart so RimShaderSelfTest can draw them on their own.
    const char* kRippleHlsl = R"(
// A still rim of sand around each of the client's footprints (features/CoverRipples.hpp), following the print's own
// outline: the boot, bare foot, hoof, claw or paw the client drew there, pointed and mirrored the way it drew it ("would
// it be possible to make the rings into sand outlines around the prints themselves?", the user, 2026-10-12). The
// outline's distance from its centre is 1 + sum a_k cos k t + b_k sin k t (k = 1..8, in units of its mean distance r0,
// t the direction on the ground); the rim's centre line lies kRimOffset r0 outside it, kRimBand r0 wide. Three
// registers a print: (centre relative to the camera, 1 / r0, its reach in 1/1024 yd + 0.99 x its strength), then the
// coefficients two to a float (a_k + 1024 b_k, each 10 bits for -1..1); a print not in use has all zeros. Shading
// only: the height comes back as a little brightness, and `grad` (world x, y) tilts the normal down the rim's slopes.
// Where a pixel spans more than a quarter of the rim's width it fades out instead of shimmering. px = the pixel's size
// in yd.
// A pixel shader can't index constants by a counter, so the loop over the prints is unrolled; working out an outline
// in every pass came to some 5,400 instruction slots, past what the GPU takes. So the loop only picks the (at most)
// two prints whose rims can reach this pixel -- a few instructions each --, and only those two are worked out.
static const float kRimOffset = 0.325; // = ripples::kRimOffset, kRimBand (-0.025 .. 0.675 r0); the self-test compares
static const float kRimBand = 0.7;
float2 CMul(float2 p, float2 q) { return float2(p.x * q.x - p.y * q.y, p.x * q.y + p.y * q.x); }
float RimOne(float2 rel, float px, float4 a, float4 p1, float4 p2, inout float2 grad)
{
    float2 n = (rel - a.xy) * a.z;
    float q = max(length(n), 1e-3);
    float2 z1 = n / q;
    float2 z2 = CMul(z1, z1), z3 = CMul(z2, z1), z4 = CMul(z2, z2);
    float2 z5 = CMul(z4, z1), z6 = CMul(z4, z2), z7 = CMul(z4, z3), z8 = CMul(z4, z4);
    float4 hi1 = floor(p1 * (1.0 / 1024)), hi2 = floor(p2 * (1.0 / 1024));
    float4 ca1 = (p1 - hi1 * 1024) * (1.0 / 511.5) - 1, sb1 = hi1 * (1.0 / 511.5) - 1;
    float4 ca2 = (p2 - hi2 * 1024) * (1.0 / 511.5) - 1, sb2 = hi2 * (1.0 / 511.5) - 1;
    float4 c1 = float4(z1.x, z2.x, z3.x, z4.x), s1 = float4(z1.y, z2.y, z3.y, z4.y);
    float4 c2 = float4(z5.x, z6.x, z7.x, z8.x), s2 = float4(z5.y, z6.y, z7.y, z8.y);
    float r = 1 + dot(ca1, c1) + dot(sb1, s1) + dot(ca2, c2) + dot(sb2, s2);
    float dr = dot(sb1 * c1 - ca1 * s1, float4(1, 2, 3, 4)) + dot(sb2 * c2 - ca2 * s2, float4(5, 6, 7, 8));
    float ph = 6.28318531 * (q - r - kRimOffset) / kRimBand;
    float height = 0;
    [branch] if (abs(ph) < 3.14159265)
    {
        // The band's slope: (cos t, sin t) - dr / q (-sin t, cos t), per r0; on the ground x 1 / r0.
        float2 g = (z1 + (dr / q) * float2(z1.y, -z1.x)) * a.z;
        float gl = max(length(g), 1e-4);
        float env = frac(a.w) * (1.0 / 0.99) * saturate(kRimBand / gl / (4 * px) - 0.5);
        grad += (-sin(ph) * env / gl) * g;
        height = (0.5 + 0.5 * cos(ph)) * env;
    }
    return height;
}
float RippleRings(float2 rel, float px, out float2 grad)
{
    grad = 0;
    float height = 0;
    // In a branch of its own (px is never negative): without one the compiler hoists the 48 reach tests ahead of the
    // picking and runs out of temporary registers.
    [branch] if (px >= 0)
    {
        float4 a0 = 0, b0 = 0, c0 = 0, a1 = 0, b1 = 0, c1 = 0;
        float taken0 = 0, taken1 = 0;
        [unroll] for (int k = 0; k < 48; ++k)
        {
            float4 a = ripples[3 * k];
            float2 dv = abs(rel - a.xy);
            // Within its reach (a.w's whole part, 1/1024 yd; never for a print not in use): the first two found.
            float inside = max(dv.x, dv.y) * 1024 + 1 < a.w ? 1 : 0;
            float second = inside * taken0, first = inside - second;
            a1 = second > 0 ? a : a1; b1 = second > 0 ? ripples[3 * k + 1] : b1; c1 = second > 0 ? ripples[3 * k + 2] : c1;
            a0 = first > 0 ? a : a0; b0 = first > 0 ? ripples[3 * k + 1] : b0; c0 = first > 0 ? ripples[3 * k + 2] : c0;
            taken1 = max(taken1, second);
            taken0 = max(taken0, inside);
        }
        [branch] if (taken0 > 0) height += RimOne(rel, px, a0, b0, c0, grad);
        [branch] if (taken1 > 0) height += RimOne(rel, px, a1, b1, c1, grad);
    }
    return height;
}
)";
    const char* kPsMainHlsl = R"(
float4 main(float3 n : TEXCOORD0, float2 d : TEXCOORD1, float2 rel : TEXCOORD2, float fog : TEXCOORD3, float4 tint : TEXCOORD4, float2 wuv : TEXCOORD5, float3 vpos : TEXCOORD6, float far : TEXCOORD7, float3 lamps : TEXCOORD8, float2 pixel : VPOS) : COLOR
{
    clip(CoverVisibility(rel) - CoverThreshold(rel));
    clip(d.x - opts.x);
    // Not on a triangle reaching a hole (or unloaded ground): its vertex there sits 100,000 yd down, and drawn up
    // to its middle it hung from the hole's edge like a curtain (across the Caverns of Time's tunnel, where the
    // hillside over its mouth is cut away; the user, 2026-10-11). The cover now stops a cell short of a hole.
    clip(-d.y);
    float2 a = step(inner.xy, rel) * step(rel, inner.zw);
    clip(0.5 - a.x * a.y);
    float3 nn = normalize(n), l = normalize(light.xyz);
    float ripplePx = length(fwidth(rel));
    float rippleShade = 0;
    [branch] if (rippleInfo.x > 0.5 && all(rel >= rippleBox.xy) && all(rel <= rippleBox.zw))
    {
        float2 rippleGrad;
        rippleShade = RippleRings(rel, ripplePx, rippleGrad);
        nn = normalize(nn - rippleInfo.y * (rippleGrad.x * rippleAxisX.xyz + rippleGrad.y * rippleAxisY.xyz));
    }
    float ndl = saturate(dot(nn, l));
    float3 lit = min(ambient.rgb + diffuse.rgb * ndl, 1);
    // Cover textures, tiled in world space like the terrain's layers, mixed by their share here
    // (two filtered weight textures, slots 1..8); the plain cover colour fills the rest. Their
    // alpha is the specular mask (as on the terrain's layers, or the _s texture's).
    float2 uv = rel * texMap.x + texMap.yz;
    float4 w = tex2D(weights0, wuv), v = tex2D(weights1, wuv);
    float4 t0 = tex2D(cover0, uv), t1 = tex2D(cover1, uv), t2 = tex2D(cover2, uv), t3 = tex2D(cover3, uv);
    float4 t4 = tex2D(cover4, uv), t5 = tex2D(cover5, uv), t6 = tex2D(cover6, uv), t7 = tex2D(cover7, uv);
    float3 textured = t0.rgb * w.r + t1.rgb * w.g + t2.rgb * w.b + t3.rgb * w.a + t4.rgb * v.r + t5.rgb * v.g + t6.rgb * v.b + t7.rgb * v.a;
    float mask = dot(float4(t0.a, t1.a, t2.a, t3.a) * w, specOn0) + dot(float4(t4.a, t5.a, t6.a, t7.a) * v, specOn1);
    float share = saturate(dot(w, 1) + dot(v, 1));
    // Where no cover texture is drawn (its slot not filled yet: a place just loaded in, or flown into), a cover
    // with a tint (ash, sand: its texture's average colour x 1.68) fills with that colour back at its average, not
    // the plain snow white ("ash still has white when loading in especially", the user, 2026-10-11). Snow's rows
    // have no tint: white, as before.
    float3 fill = tint.a > 0.01 ? tint.rgb * 0.6 : albedo.rgb;
    float3 base = fill * (1 - share) + textured * albedo.w;
    float3 colour = lerp(base, tint.rgb, saturate(tint.a));
    colour *= 1 + rippleInfo.z * rippleShade; // the rim a little lighter
    // Vertex colour (the terrain multiplies by 2 x it) and wetness: darker, and a stronger, tighter shine.
    float4 pr = tex2D(props, wuv);
    // The row's own wet look, plus how much wetter than its rest the ground is now (rain, water nearby).
    // Inside its last yards the grid fades into the one regional value used beyond it.
    float2 fromMin = rel - wetBox.xy, toMax = wetBox.zw - rel;
    float inner = saturate(min(min(fromMin.x, fromMin.y), min(toMax.x, toMax.y)) * misc.z);
    float wet = saturate(pr.a + wetMap.w * lerp(far, tex2D(wetGrid, rel * wetMap.x + wetMap.yz).a, inner));
    colour *= pr.rgb * 2 * (1 - misc.w * wet);
    float3 h = normalize(l + normalize(-vpos));
    float sp = pow(saturate(dot(nn, h)), max(specular.w, 1) * (1 + 3 * wet)) * saturate(mask * (1 + 2 * wet));
    // Dynamic shadows like the terrain's: down to 70% in full shadow, no shine there.
    float shade = 1;
    [branch] if (misc.x > 2.5) shade = ShadowCascades(vpos);
    else if (misc.x > 1.5) shade = min(ShadowSingle(vpos), ShadowOuter(vpos));
    else if (misc.x > 0.5) shade = ShadowSingle(vpos);
    // Point lights (torches, from the VS): not shadowed by the sun's shadow maps. Added to the sun's
    // light like the client's own (the same amount whichever way the ground faces the sun), then
    // compressed smoothly above a knee instead of clamped (a hard clamp left a flat white plateau with
    // a ring), on the brightest channel with all three scaled alike so the flame's hue stays. The knee
    // starts at the sun's own level (at least 0.8): without lamps this is the sun's lighting as before.
    float3 sunLit = lit * (0.7 + 0.3 * shade);
    // Our summed light through the knee every shader applies first (lights::LampKnee: many small flames together, a
    // ring of candles, a warm glow and not a blowout; LAMP_KNEE_* put in front of this source from env/Lights).
    float lampPeak = max(lamps.r, max(lamps.g, lamps.b));
    float lampOver = max(lampPeak - LAMP_KNEE_START, 0);
    lamps *= (lampPeak - lampOver + LAMP_KNEE_ROOM * (1 - exp(-lampOver / LAMP_KNEE_ROOM))) / max(lampPeak, 0.001);
    float3 total = sunLit + lamps;
    float peak = max(total.r, max(total.g, total.b));
    float knee = max(0.8, max(sunLit.r, max(sunLit.g, sunLit.b)));
    float over = max(peak - knee, 0);
    float room = max(1 - knee, 0.001);
    float shaped = peak - over + room * (1 - exp(-over / room));
    total *= shaped / max(peak, 0.001);
    float3 c = colour * total + specular.rgb * sp * shade;
    // Debug views (panel): which part makes a colour.
    [branch] if (debugView.x > 0.5)
    {
        if (debugView.x < 1.5) return float4(1 - share, share, 0, 1);       // cover texture share: red = plain colour
        if (debugView.x < 2.5) return float4(specular.rgb * sp * shade, 1); // the shine alone
        return float4(total * 0.5, 1);                                     // the light alone (half, so bright stays readable)
    }
    return float4(lerp(fogColor.rgb, c, fog), 1);
}
)";

    // Projected textures on the cover (selection circles, spell ground effects, targeting reticles):
    // the client draws them after the scene by re-drawing the terrain under an overhead projector, in
    // fixed function -- stage 0 the decal texture x the decal colour, stage 1 a falloff texture, each
    // with texture coordinates = the camera-space position x a texture matrix (3 coordinates, no
    // divide). Models' projected batches (Consecration) do the same in shaders: their VS dots the
    // view-space position with c6/c7 (decal) and c8/c9 (falloff), their PS fogs (VS c30, PS c2).
    // The cover lies on top and hid them; this draws the same onto the cover, with the client's
    // blend, alpha test and depth bias still set. Same VS as the cover (same depth). PS: c2/c3 as the
    // cover's (smallest depth, the finer level's box), c40..c43 / c44..c47 the decal / falloff
    // texture coordinates as dot vectors over (view position, 1), c48 the colour, c49 the fog
    // (viewZ * x + y, power z; x = 0, y = 1 = none), c50 the fog colour; s0/s1 the client's textures.
    // c51..c58 the two stages' combiners as the client set them (fixed function; the decals don't all
    // combine alike -- the blob shadow fades its texture to white and ADDS its falloff, for a
    // modulate blend): per stage colour then alpha, each (D3DTOP, arg1 select, complement, alpha
    // replicate), (arg2 select, complement, alpha replicate, 0); select 0 diffuse, 1 current,
    // 2 texture, 3 texture factor (c59). A model's projected batch: modulate, modulate.
    const char* kDecalPsHlsl = R"(
float4 opts : register(c2);
float4 inner : register(c3);
float4 m0[4] : register(c40);
float4 m1[4] : register(c44);
float4 decalColor : register(c48);
float4 decalFog : register(c49);
float4 decalFogColor : register(c50);
float4 stages[8] : register(c51);
float4 texFactor : register(c59);
sampler2D decalTex : register(s0);
sampler2D fadeTex : register(s1);

float4 Arg(float sel, float comp, float rep, float4 dif, float4 cur, float4 tex)
{
    float4 v = sel < 0.5 ? dif : (sel < 1.5 ? cur : (sel < 2.5 ? tex : texFactor));
    v = rep > 0.5 ? v.aaaa : v;
    return comp > 0.5 ? 1 - v : v;
}

// D3DTOP_*; anything not listed modulates.
float4 Op(float op, float4 a, float4 b, float4 dif, float4 cur, float4 tex)
{
    float4 r = a * b;
    r = op == 2 ? a : r;
    r = op == 3 ? b : r;
    r = op == 5 ? a * b * 2 : r;
    r = op == 6 ? a * b * 4 : r;
    r = op == 7 ? a + b : r;
    r = op == 8 ? a + b - 0.5 : r;
    r = op == 9 ? (a + b - 0.5) * 2 : r;
    r = op == 10 ? a - b : r;
    r = op == 11 ? a + b - a * b : r;
    r = op == 12 ? lerp(b, a, dif.a) : r;
    r = op == 13 ? lerp(b, a, tex.a) : r;
    r = op == 14 ? lerp(b, a, texFactor.a) : r;
    r = op == 15 ? a + b * (1 - tex.a) : r;
    r = op == 16 ? lerp(b, a, cur.a) : r;
    return saturate(r);
}

float4 Stage(float4 cA, float4 cB, float4 aA, float4 aB, float4 dif, float4 cur, float4 tex)
{
    float4 c = Op(cA.x, Arg(cA.y, cA.z, cA.w, dif, cur, tex), Arg(cB.x, cB.y, cB.z, dif, cur, tex), dif, cur, tex);
    float4 l = Op(aA.x, Arg(aA.y, aA.z, aA.w, dif, cur, tex), Arg(aB.x, aB.y, aB.z, dif, cur, tex), dif, cur, tex);
    float4 r = float4(c.rgb, aA.x == 1 ? cur.a : l.a);
    return cA.x == 1 ? cur : r; // D3DTOP_DISABLE
}

float4 main(float2 d : TEXCOORD1, float2 rel : TEXCOORD2, float3 vpos : TEXCOORD6) : COLOR
{
    clip(d.x - opts.x);
    clip(0.5 - d.y);
    float2 a = step(inner.xy, rel) * step(rel, inner.zw);
    clip(0.5 - a.x * a.y);
    float4 p = float4(vpos, 1);
    float2 t0 = float2(dot(p, m0[0]), dot(p, m0[1]));
    float2 t1 = float2(dot(p, m1[0]), dot(p, m1[1]));
    float4 c = Stage(stages[0], stages[1], stages[2], stages[3], decalColor, decalColor, tex2D(decalTex, t0));
    c = Stage(stages[4], stages[5], stages[6], stages[7], decalColor, c, tex2D(fadeTex, t1));
    float fog = min(pow(max(vpos.z * decalFog.x + decalFog.y, 0), decalFog.z), 1);
    c.rgb = lerp(decalFogColor.rgb, c.rgb, fog);
    return c;
}
)";

    std::string LampKneeDefines()
    {
        char d[160];
        std::snprintf(d, sizeof(d), "#define LAMP_KNEE_START %.9g\n#define LAMP_KNEE_ROOM %.9g\n", lights::kLampKneeStart, lights::kLampKneeRoom);
        return d;
    }
}
