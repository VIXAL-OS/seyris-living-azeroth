// Sun shafts: the pass's three pixel shaders (ps_3_0), apart from the pass so tests/shafts can compile them, count
// their instruction slots and run them. The pass: features/SunShafts.
//
//  1. Source (kShaftSourceHlsl), at a quarter (or half) of the resolution: r = how much sunlit air each pixel shows
//     toward the sun -- the sky full, a surface by the lit air in front of it (none for the trunk next to you, most
//     for a hillside in the fog), x a forward-scattering lobe round the sun, x what the cloud deck lets through there
//     (its own result's transmittance, so its gaps let light through); g = the light scattered in under the cloud
//     deck along the view ray, each step lit by the deck's transmittance toward the sun (a short march through LA's
//     own density field: shafts below its gaps, with the sun off the screen too).
//     b = the source before that lobe (how open the pixel is toward the sun).
//  2. Radial blur (kShaftBlurHlsl): r gathered along the line toward the sun's place on the screen, each tap weighed
//     less than the one before (the shafts), and a little more where that line is broken into gaps (b open and shut
//     along it: a canopy); a second, short pass smooths the taps' steps. g a small blur.
//  3. Composite (kShaftCompositeHlsl): added over the frame in the light's colour; less over near surfaces (little
//     air in front of them) and over the sky; never more than a cap.
#pragma once

namespace wxl_livingazeroth::sunshafts
{
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

static const float2 kTaps[4] = { float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1) };

float ViewZ(float d)
{
    float d01 = saturate((d - cDepth.x) * cDepth.y);
    float ndcZ = cDepth.w > 0.5 ? d01 : d01 * 2 - 1;
    return abs(cProj2.y / min(ndcZ - cProj2.x, -1e-6));
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
    // The source over the pixel's block (four taps: thin branches count): the sky full, a surface by the lit air in
    // front of it, the far end of the world's depth nearly full.
    float src = 0;
    [unroll] for (int k = 0; k < 4; ++k)
    {
        float d = tex2Dlod(depthTex, float4(cUv.xy + (uv + kTaps[k] * cPix.zw) * cUv.zw, 0, 0)).r;
        float z = ViewZ(d);
        float s = max(1 - exp(-cProj2.w * z), 0.85 * smoothstep(0.6 * cProj2.z, cProj2.z, z));
        src += (d > cDepth.z ? 1 : s) * 0.25;
    }
    // Forward scattering: most of it close round the sun (half at ~8 degrees), a little wider. The blur's taps nearest
    // the sun then carry the shafts: what stands in front of the sun's glow shades the line behind it.
    float c = max(dot(normalize(ray), cSunV.xyz), 1e-4);
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
    // reach), each step lit by what the deck lets through toward the sun (three samples across its slab).
    float lit = 0;
    [branch] if (cCam.w > 0)
    {
        float dc = tex2Dlod(depthTex, float4(cUv.xy + uv * cUv.zw, 0, 0)).r;
        float far = dc > cDepth.z ? cDeck2.y : min(ViewZ(dc) * length(ray), cDeck2.y);
        if (dir.z > 1e-3) far = min(far, max((cDeck.x - cCam.z) / dir.z, 0));
        float3 h3 = frac(vpos.xyx * 0.1031);
        h3 += dot(h3, h3.yzx + 33.33);
        float j = frac((h3.x + h3.y) * h3.z);
        float edge = min(max(cDeck.y - cDeck.x, 1) * 0.25, 100);
        float sz = max(cSunW.z, 0.05);
        float n = max(cSunW.w, 1);
        float sum = 0;
        [loop] for (int i = 0; i < 24; ++i)
        {
            if (i >= n) break;
            float3 p = cCam.xyz + dir * (far * (i + j) / n);
            float s0 = max((cDeck.x - p.z) / sz, 0);
            float s1 = max((cDeck.y - p.z) / sz, s0);
            float ds = (s1 - s0) / 3;
            float lod = min(log2(max(ds * 16 * cDeck.w, 1)), 3); // a long step reads the noise's blurrier levels
            float tau = 0;
            [unroll] for (int m = 0; m < 3; ++m) tau += DeckSigma(p + cSunW.xyz * (s0 + ds * (m + 0.5)), edge, lod) * ds;
            sum += exp(-tau * cDeck2.z);
        }
        float ph = 0.35 + 0.65 * pow(saturate(dot(dir, cSunW.xyz) * 0.5 + 0.5), 6);
        lit = sum / n * (1 - exp(-cNoise.w * far)) * ph * cCam.w;
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
    // The in-scatter (g): a small blur (its steps were jittered per pixel).
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
