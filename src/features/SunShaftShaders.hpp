// Sun shafts: the pass's three pixel shaders (ps_3_0), apart from the pass so tests/shafts can compile them, count
// their instruction slots and run them. The pass: features/SunShafts.
//
//  1. Source (kShaftSourceHlsl), at a quarter (or half) of the resolution: r = how much sunlit air each pixel shows
//     toward the sun -- the sky full, the far end of the world's depth (fogged out) nearly so, a surface only a little,
//     for the lit air in front of it (none for the trunk next to you, at most kSurfaceAir for a hillside in the fog;
//     none where it stands in front of the sun's glow, and only as much of that air as is out of the shadow of what
//     stands toward the sun: a mountain in front of the sun is an occluder, not lit air) -- x a forward-scattering lobe
//     round the sun, x what the cloud deck lets through there (its own result's transmittance, so its gaps let light
//     through); g = the light scattered in under the cloud deck along the view ray, each step lit by the deck's
//     transmittance toward the light (a short march through LA's own density field: shafts below its gaps, with the
//     sun off the screen too) and shaded by what stands in the light's way on the screen; b = the source before that
//     lobe (how open the pixel is toward the sun).
//     What stands in the way: a screen-space shadow test. The shadow rays of all the points on one view ray cross the
//     screen along one line (toward the light's place, or away from it with the light behind); twelve depth taps along
//     it, and each point's own depths there, say which points are behind a surface (and no deeper than kThickness x
//     its depth behind it). Where the line leaves the screen over a surface the test can't see the rest: there the
//     in-scatter keeps kUnsure.
//  2. Radial blur (kShaftBlurHlsl): r gathered along the line toward the sun's place on the screen, each tap weighed
//     less than the one before (the shafts), and a little more where that line is broken into gaps (b open and shut
//     along it: a canopy); a second, short pass smooths the taps' steps. g a small blur.
//  3. Composite (kShaftCompositeHlsl): added over the frame in the light's colour; less over near surfaces (little
//     air in front of them) and over the sky; never more than a cap.
#pragma once

namespace wxl_livingazeroth::sunshafts
{
    // What stands in the sun's way (the source's c15 and c16; features/SunShafts and tests/shafts pass the same).
    constexpr float kSurfaceAir = 0.3f;     // the most a surface counts for: the lit air in front of it (the sky counts 1)
    constexpr float kSunFadeFrom = 0.8192f; // cos 35 deg: nearer the sun than this a surface counts for less ...
    constexpr float kSunFadeTo = 0.9903f;   // cos 8 deg: ... and from here for nothing (it stands in front of the glow)
    constexpr float kThickness = 1.0f;      // an occluder reaches this x its depth behind its surface (terrain: deep)
    constexpr float kUnsure = 0.5f;         // what the in-scatter keeps where its shadow test lost sight of the occluders

    inline const char* const kShaftSourceHlsl = R"(
sampler2D depthTex : register(s0);
sampler2D deckTex  : register(s1);
sampler3D noiseTex : register(s2);
float4 cDepth : register(c0);  // x = viewport min z, y = 1 / (max z - min z), z = the world's deepest depth (beyond: sky, far terrain), w = depth decode (1 = 0..1)
float4 cProj  : register(c1);  // projection m00, m11, m20, m21
float4 cProj2 : register(c2);  // x = m22, y = m32, z = the far clip (yd), w = how fast the air in front of a surface lights up (per yd)
float4 cUv    : register(c3);  // depth texture uv = xy + viewport uv * zw
float4 cPix   : register(c4);  // xy = viewport uv per target pixel, zw = the depth taps' offset (viewport uv)
float4 cSunV  : register(c5);  // xyz = toward the sun, view space (unit), w = the screen-space source (0 off)
float4 cDeckT : register(c6);  // x = the deck in the way: 0 none, 1 its own result (deckTex alpha), 2 its slab at extinction y; zw = deckTex uv per viewport uv
float4 cInv0  : register(c7);  // view to world (rows)
float4 cInv1  : register(c8);
float4 cInv2  : register(c9);
float4 cCam   : register(c10); // xyz = the camera (world), w = the in-scatter under the deck (0 off)
float4 cDeck  : register(c11); // x = its base (world z), y = its top, z = density per yd, w = 1 / its noise tile (yd)
float4 cDeck2 : register(c12); // x = the banks' threshold, y = how far the in-scatter is marched (yd), z = its shadow's darkness, w = the deck's range (yd)
float4 cNoise : register(c13); // xyz = the fog's noise drift (tiles), w = the air's density for the in-scatter (per yd)
float4 cSunW  : register(c14); // xyz = toward the sun, world (unit), w = the in-scatter's steps (up to 24)
float4 cLightV : register(c15); // xyz = toward the light (cSunW), view space (unit); w = 1 + how far behind its surface an occluder reaches (x its depth), 0 = nothing blocks
float4 cOcc   : register(c16); // x = the most a surface counts for, y..z = the cosines toward the sun over which that fades to none, w = what an unsure in-scatter keeps

static const float2 kTaps[4] = { float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1) };
// How far the shadow test's taps reach along a shadow ray, in yd per yd of the point's view depth: each 1.94 x the last,
// so a surface's shadow (its depth .. 2 x it) never falls between two of them.
static const float kReach[12] = { 0.02, 0.039, 0.075, 0.146, 0.28, 0.55, 1.07, 2.07, 4.0, 7.8, 15.2, 29.4 };

float ViewZ(float d)
{
    float d01 = saturate((d - cDepth.x) * cDepth.y);
    float ndcZ = cDepth.w > 0.5 ? d01 : d01 * 2 - 1;
    return abs(cProj2.y / min(ndcZ - cProj2.x, -1e-6));
}

// The 2 x 2 ordered dither, 0 .. 0.75 (a pixel's position, whole).
float Bayer2(float2 a) { a = floor(a); return frac(a.x * 0.5 + a.y * a.y * 0.75); }

// A view-space direction per unit of view depth (x/z, y/z) to the viewport's uv.
float2 RayUv(float2 q) { return float2(q.x * cProj.x + cProj.z, -(q.y * cProj.y + cProj.w)) * 0.5 + 0.5; }

// What stands in the light's way, for the points along one view ray.
struct Shadows
{
    float4 perA, perB, perC; // each tap: the shadow ray's view depth there over the scene's, per unit of the point's view
                             // depth (the ray's depth / the point's, / the scene's); 0 for the sky or off the screen
    float  tNear;            // points nearer than this (view depth) are shaded by what covers the light's own place
    float  sure;             // 1: the taps saw the whole way, or left the screen through the sky; 0: they lost it over a surface
};

Shadows NoShadows()
{
    Shadows o;
    o.perA = 0; o.perB = 0; o.perC = 0; o.tNear = 0; o.sure = 1;
    return o;
}

// The shadow rays toward L (view space, unit) of the points on the view ray through p (x/z, y/z). The point at view depth
// t, k t yd along its shadow ray, is at view depth (1 + k L.z) t over the screen point (p + k L.xy) / (1 + k L.z): one
// line over the screen for all the ray's points (toward L's place, or away from it with L behind), each point's shadow
// ray along it at its own depths. Twelve taps along it (k 0.02 .. 29), each pushed on by `jit` (1 .. 1.94: up to the
// next one's reach) so the steps between them, which would show as bands echoing the occluder's outline, become a fine
// pattern the blur pass smooths.
Shadows March(float2 p, float3 L, float jit)
{
    Shadows o;
    float per[12];
    float rho = 1;
    float lastSky = 1, allOn = 1;
    [unroll] for (int k = 0; k < 12; ++k)
    {
        float s = kReach[k] * jit;
        rho = 1 + s * L.z;
        float2 uv = RayUv((p + s * L.xy) / max(rho, 1e-3));
        float on = (rho > 0.02 && all(abs(uv - 0.5) <= 0.5)) ? 1 : 0;
        float d = tex2Dlod(depthTex, float4(cUv.xy + saturate(uv) * cUv.zw, 0, 0)).r;
        float sky = d > cDepth.z ? 1 : 0;
        per[k] = on * (1 - sky) * rho / ViewZ(d);
        lastSky = on > 0 ? sky : lastSky;
        allOn *= on;
    }
    o.perA = float4(per[0], per[1], per[2], per[3]);
    o.perB = float4(per[4], per[5], per[6], per[7]);
    o.perC = float4(per[8], per[9], per[10], per[11]);
    // L ahead: a surface over its own place shades the points too near for the last tap (their rays all end behind it).
    o.tNear = 0;
    if (L.z > 1e-3)
    {
        float2 uv = RayUv(L.xy / L.z);
        if (all(abs(uv - 0.5) <= 0.5))
        {
            float d = tex2Dlod(depthTex, float4(cUv.xy + uv * cUv.zw, 0, 0)).r;
            o.tNear = d > cDepth.z ? 0 : ViewZ(d) / rho;
        }
        else allOn = 0;
    }
    o.sure = max(allOn, lastSky);
    return o;
}

// 1 where the point at view depth t sees the light past what's on the screen; 0 in an occluder's shadow (its shadow ray
// passes behind a surface, no deeper than cLightV.w x the surface's depth), or nearer than tNear.
float4 Blocked(float4 x, float k) { return saturate(x * 10 - 9.5) * saturate(10 - x * (10 / k)); }
float Lit(Shadows o, float t)
{
    float k = max(cLightV.w, 1.01);
    // x: the shadow ray's depth over the scene's at each tap (0: nothing there).
    float4 c = (1 - Blocked(o.perA * t, k)) * (1 - Blocked(o.perB * t, k)) * (1 - Blocked(o.perC * t, k));
    return c.x * c.y * c.z * c.w * (t >= o.tNear ? 1 : 0);
}

// The deck's extinction at a world point, shaped as the deck pass shapes it (its base's wobble left out, its puffs'
// second octave at its mean): its soft slab x its puffs x its banks.
float DeckSigma(float3 q, float edge, float lod)
{
    float shape = saturate((q.z - cDeck.x) / edge) * saturate((cDeck.y - q.z) / edge);
    float n = tex3Dlod(noiseTex, float4(float3(q.xy, q.z * 1.6) * cDeck.w + cNoise.xyz * 0.32, lod)).r * 0.6 + 0.2;
    float m = tex3Dlod(noiseTex, float4(q.xy * (cDeck.w * 0.25) + cNoise.xy * 0.08, 0.37, 0)).r;
    return cDeck.z * shape * smoothstep(0.32, 0.56, n) * smoothstep(cDeck2.x - 0.12, cDeck2.x + 0.12, m);
}

float4 main(float2 vpos : VPOS) : COLOR
{
    float2 uv = (vpos + 0.5) * cPix.xy;
    float2 ndc = float2(uv.x * 2 - 1, 1 - uv.y * 2);
    float3 ray = float3((ndc.x - cProj.z) / cProj.x, (ndc.y - cProj.w) / cProj.y, 1); // per unit of view depth
    float3 h3 = frac(vpos.xyx * 0.1031);
    h3 += dot(h3, h3.yzx + 33.33);
    float j = frac((h3.x + h3.y) * h3.z); // a jitter per pixel (the in-scatter's steps)
    // The shadow test's: a 4 x 4 ordered dither (every offset in each 4 x 4 block), over one step of its taps' reach.
    float jit = exp2(0.956 * (Bayer2(vpos * 0.5) * 0.25 + Bayer2(vpos) + 1.0 / 32));
    float sigma = max(cProj2.w, 1e-5);
    float c = max(dot(normalize(ray), cSunV.xyz), 1e-4);
    bool blocks = cLightV.w > 0;
    // The source over the pixel's block (four taps: thin branches count): the sky full; a surface for the lit air in
    // front of it, at most cOcc.x, and none where it stands in front of the sun's glow (it's an occluder there); the far
    // end of the world's depth (fogged out) nearly as the sky. `surf`: the surfaces' share of it.
    float open = cOcc.x * (1 - smoothstep(cOcc.y, cOcc.z, c));
    float src = 0, surf = 0;
    [unroll] for (int k = 0; k < 4; ++k)
    {
        float d = tex2Dlod(depthTex, float4(cUv.xy + (uv + kTaps[k] * cPix.zw) * cUv.zw, 0, 0)).r;
        float z = ViewZ(d);
        float s = max(open * (1 - exp(-sigma * z)), 0.85 * smoothstep(0.6 * cProj2.z, cProj2.z, z));
        src += (d > cDepth.z ? 1 : s) * 0.25;
        surf += (d > cDepth.z ? 0 : s) * 0.25;
    }
    // A surface only as far as the air in front of it is out of the shadow of what stands toward the sun: four points
    // in front of it, one in each quarter of the light that air scatters, each by its shadow ray (only where a surface
    // counts for something here).
    [branch] if (blocks && cSunV.w > 0 && surf > 0.002)
    {
        float z = ViewZ(tex2Dlod(depthTex, float4(cUv.xy + uv * cUv.zw, 0, 0)).r);
        Shadows o = March(ray.xy, cSunV.xyz, jit);
        float e = 1 - exp(-sigma * z);
        float front = 0;
        [unroll] for (int q = 0; q < 4; ++q) front += Lit(o, -log(1 - (q + 0.5) * 0.25 * e) / sigma) * 0.25;
        src -= surf * (1 - front);
    }
    // Forward scattering: most of it close round the sun (half at ~8 degrees), a little wider. The blur's taps nearest
    // the sun then carry the shafts: what stands in front of the sun's glow shades the line behind it.
    float phase = 0.85 * pow(c, 64) + 0.15 * pow(c, 8);
    float3 dir = normalize(ray.x * cInv0.xyz + ray.y * cInv1.xyz + ray.z * cInv2.xyz);
    // What the cloud deck lets through here: its own result (its gaps), or its slab at its mean extinction.
    float T = 1;
    [branch] if (cDeckT.x > 1.5)
    {
        float L = 0;
        if (abs(dir.z) > 1e-4)
        {
            float ea = (cDeck.x - cCam.z) / dir.z, eb = (cDeck.y - cCam.z) / dir.z;
            L = max(min(max(ea, eb), cDeck2.w) - max(min(ea, eb), 0), 0);
        }
        else if (cCam.z > cDeck.x && cCam.z < cDeck.y) L = cDeck2.w;
        T = exp(-cDeckT.y * L);
    }
    else if (cDeckT.x > 0.5) T = tex2Dlod(deckTex, float4(uv * cDeckT.zw, 0, 0)).a;
    float shaft = src * phase * T * cSunV.w;
    // Under the deck: the light scattered in along the view ray (to the surface, the deck's base or the march's
    // reach), each step lit by what the deck lets through toward the light (three samples across its slab) and shaded
    // by what stands in the light's way on the screen.
    float lit = 0;
    [branch] if (cCam.w > 0)
    {
        float dc = tex2Dlod(depthTex, float4(cUv.xy + uv * cUv.zw, 0, 0)).r;
        float far = dc > cDepth.z ? cDeck2.y : min(ViewZ(dc) * length(ray), cDeck2.y);
        if (dir.z > 1e-3) far = min(far, max((cDeck.x - cCam.z) / dir.z, 0));
        float invLen = 1 / length(ray); // view depth per yd along the ray
        Shadows o = NoShadows();
        [branch] if (blocks) o = March(ray.xy, cLightV.xyz, jit);
        float edge = min(max(cDeck.y - cDeck.x, 1) * 0.25, 100);
        float sz = max(cSunW.z, 0.05);
        float n = max(cSunW.w, 1);
        float sum = 0;
        [loop] for (int i = 0; i < 24; ++i)
        {
            if (i >= n) break;
            float dist = far * (i + j) / n;
            float3 p = cCam.xyz + dir * dist;
            float s0 = max((cDeck.x - p.z) / sz, 0);
            float s1 = max((cDeck.y - p.z) / sz, s0);
            float ds = (s1 - s0) / 3;
            float lod = min(log2(max(ds * 16 * cDeck.w, 1)), 3); // a long step reads the noise's blurrier levels
            float tau = 0;
            [unroll] for (int m = 0; m < 3; ++m) tau += DeckSigma(p + cSunW.xyz * (s0 + ds * (m + 0.5)), edge, lod) * ds;
            sum += exp(-tau * cDeck2.z) * Lit(o, dist * invLen);
        }
        float ph = 0.35 + 0.65 * pow(saturate(dot(dir, cSunW.xyz) * 0.5 + 0.5), 6);
        lit = sum / n * (1 - exp(-cNoise.w * far)) * ph * cCam.w * lerp(cOcc.w, 1, o.sure);
    }
    // b: the source before the lobe round the sun (how open the pixel is toward it), for the blur's gaps.
    return float4(saturate(shaft), saturate(lit), saturate(src * T * cSunV.w), 1);
}
)";

    inline const char* const kShaftBlurHlsl = R"(
sampler2D srcTex : register(s0);
float4 cSun  : register(c0); // xy = the sun in this target's uv, z = how far toward it the taps reach (share of the way), w = taps (up to 64)
float4 cBlur : register(c1); // x = each tap's weight after the one before, y = 1: the taps start at a random offset per pixel, zw = 1 / the target's size
float4 cGap  : register(c2); // x = how much more where the line toward the sun is broken into gaps (a canopy), 0 = none

float4 main(float2 vpos : VPOS) : COLOR
{
    float2 uv = (vpos + 0.5) * cBlur.zw;
    float n = max(cSun.w, 1);
    float2 dstep = (cSun.xy - uv) * (cSun.z / n);
    float j = 0.5;
    if (cBlur.y > 0.5)
    {
        float3 h3 = frac(vpos.xyx * 0.1031);
        h3 += dot(h3, h3.yzx + 33.33);
        j = frac((h3.x + h3.y) * h3.z);
    }
    float2 p = uv + dstep * j;
    float sum = 0, wsum = 0, w = 1;
    // How the line flickers between open and shut from tap to tap (b; on the screen only): branches breaking the sun
    // into gaps, not a slope's smooth change nor a lone trunk's one edge.
    float flick = 0, wflick = 0, prev = 0, prevOn = 0;
    [loop] for (int i = 0; i < 64; ++i)
    {
        if (i >= n) break;
        float2 t = tex2Dlod(srcTex, float4(p, 0, 0)).rb;
        sum += t.x * w;
        wsum += w;
        float on = all(abs(p - 0.5) <= 0.5) ? 1 : 0;
        float both = on * prevOn * w;
        float d = t.y - prev;
        flick += d * d * both;
        wflick += both;
        prev = t.y;
        prevOn = on;
        w *= cBlur.x;
        p += dstep;
    }
    // 0 steady .. 1 open and shut at random, half and half.
    float broken = saturate(2 * flick / max(wflick, 1e-4));
    sum *= 1 + cGap.x * broken;
    // The in-scatter (g): a small blur (its steps were jittered per pixel, its shadow taps dithered).
    float g = tex2Dlod(srcTex, float4(uv, 0, 0)).g * 2
            + tex2Dlod(srcTex, float4(uv + float2(cBlur.z, 0), 0, 0)).g + tex2Dlod(srcTex, float4(uv - float2(cBlur.z, 0), 0, 0)).g
            + tex2Dlod(srcTex, float4(uv + float2(0, cBlur.w), 0, 0)).g + tex2Dlod(srcTex, float4(uv - float2(0, cBlur.w), 0, 0)).g;
    return float4(sum / max(wsum, 1e-4), g / 6, 0, 1);
}
)";

    inline const char* const kShaftCompositeHlsl = R"(
sampler2D depthTex : register(s0);
sampler2D shaftTex : register(s1);
float4 cDepth  : register(c0);  // x = viewport min z, y = 1 / (max z - min z), z = the world's deepest depth, w = depth decode
float4 cProj2  : register(c1);  // x = m22, y = m32, z = 1 / the yards of air a surface needs in front of it for the full shafts
float4 cUv     : register(c2);  // depth texture uv = xy + viewport uv * zw
float4 cShaft  : register(c3);  // xy = the shafts' uv per viewport uv, zw = half a texel of theirs
float4 cColor  : register(c4);  // rgb = the screen-space shafts' colour x strength, w = the most added
float4 cColor2 : register(c5);  // rgb = the in-scatter's colour x strength, w = the screen-space shafts over the sky, x this

float4 main(float2 uv : TEXCOORD0) : COLOR
{
    float2 su = uv * cShaft.xy;
    float2 s = tex2Dlod(shaftTex, float4(su - cShaft.zw, 0, 0)).rg + tex2Dlod(shaftTex, float4(su + float2(cShaft.z, -cShaft.w), 0, 0)).rg
             + tex2Dlod(shaftTex, float4(su + float2(-cShaft.z, cShaft.w), 0, 0)).rg + tex2Dlod(shaftTex, float4(su + cShaft.zw, 0, 0)).rg;
    s *= 0.25;
    float d = tex2Dlod(depthTex, float4(cUv.xy + uv * cUv.zw, 0, 0)).r;
    float d01 = saturate((d - cDepth.x) * cDepth.y);
    float ndcZ = cDepth.w > 0.5 ? d01 : d01 * 2 - 1;
    float z = abs(cProj2.y / min(ndcZ - cProj2.x, -1e-6));
    float air = 1 - exp(-z * cProj2.z);
    float sky = d > cDepth.z ? 1 : 0;
    float3 add = s.r * cColor.rgb * lerp(air, cColor2.w, sky) + s.g * cColor2.rgb * lerp(air, 1, sky);
    return float4(min(add, cColor.w), 0);
}
)";
}
