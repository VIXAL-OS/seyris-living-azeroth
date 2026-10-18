// Coloured shafts through window glass: the pass's two pixel shaders (ps_3_0), apart from the pass so tests/glass can
// compile them, count their slots and run them. The pass: features/GlassShafts.
//
//  1. March (kGlassShaftMarchHlsl), at a quarter of the resolution: along each pixel's view ray, from the camera to
//     the surface (or kMarch yd), within the panes' rooms' box, a few steps (jittered per pixel); at each step, for each pane whose room holds the
//     camera, whether the ray from that point toward the sun leaves through the pane (the floor light's projection
//     rows: |a|, |b| < 1, 0 < t < 1, soft edges) and the point is in the pane's room; the light of the panes it
//     leaves through, x the step, x the air's density, x a forward-scattering phase (brighter looking toward the
//     sun). A sum of coloured light: the shafts are each window's own colour.
//  2. Composite (kGlassShaftCompositeHlsl): a small blur of the march (four taps), added over the frame, capped.
// Why a march and not a source blurred toward the sun (like the sun shafts): the sun is rarely on the screen
// indoors, and the shafts have to stand in the room where the light really goes, not radiate from the glass.
#pragma once

namespace wxl_livingazeroth::glassshafts
{
    constexpr int kMaxPanes = 16;
    constexpr int kPaneArrays = 6;          // rows a, b, t, light, room min, room max: c16 + 16 x array
    constexpr int kFirstPaneConstant = 16;

    inline const char* const kGlassShaftMarchHlsl = R"(
sampler2D depthTex : register(s0);
float4 cDepth : register(c0);  // x = viewport min z, y = 1 / (max z - min z), z = the world's deepest depth, w = depth decode (1 = 0..1)
float4 cProj  : register(c1);  // projection m00, m11, m20, m21
float4 cProj2 : register(c2);  // x = m22, y = m32, z = how far the march goes (yd), w = its steps (up to 32)
float4 cUv    : register(c3);  // depth texture uv = xy + viewport uv * zw
float4 cPix   : register(c4);  // xy = viewport uv per target pixel, z = the air's density per yd, w = panes (up to 16)
float4 cInv0  : register(c5);  // view to world (rows)
float4 cInv1  : register(c6);
float4 cInv2  : register(c7);
float4 cSun   : register(c8);  // xyz = toward the sun (world, unit), w = the forward-scattering share
float4 cRoomLo : register(c9); // the panes' rooms together: their box (camera-relative), so the steps stay in the room
float4 cRoomHi : register(c10);
// Per pane (six arrays of 16, so the loop's counter indexes each): the rows a, b, t (camera-relative, world axes;
// features/Glass's ProjectionRows), its light (rgb), its room's box (min xyz, w = the edges' softness; max xyz,
// w = the far end's softness), camera-relative.
float4 cRowA[16] : register(c16);
float4 cRowB[16] : register(c32);
float4 cRowT[16] : register(c48);
float4 cLight[16] : register(c64);
float4 cLo[16] : register(c80);
float4 cHi[16] : register(c96);

float ViewZ(float d)
{
    float d01 = saturate((d - cDepth.x) * cDepth.y);
    float ndcZ = cDepth.w > 0.5 ? d01 : d01 * 2 - 1;
    return abs(cProj2.y / min(ndcZ - cProj2.x, -1e-6));
}

float4 main(float2 vpos : VPOS) : COLOR
{
    float2 uv = (vpos + 0.5) * cPix.xy;
    float2 ndc = float2(uv.x * 2 - 1, 1 - uv.y * 2);
    float3 ray = float3((ndc.x - cProj.z) / cProj.x, (ndc.y - cProj.w) / cProj.y, 1);
    float d = tex2Dlod(depthTex, float4(cUv.xy + uv * cUv.zw, 0, 0)).r;
    float far = d > cDepth.z ? cProj2.z : min(ViewZ(d) * length(ray), cProj2.z);
    float3 dir = normalize(ray.x * cInv0.xyz + ray.y * cInv1.xyz + ray.z * cInv2.xyz);
    float3 h3 = frac(vpos.xyx * 0.1031);
    h3 += dot(h3, h3.yzx + 33.33);
    float j = frac((h3.x + h3.y) * h3.z);
    // Only the stretch of the ray inside the rooms' box: a house's room gets its steps as close as a cathedral's.
    float3 inv = 1 / (abs(dir) < 1e-5 ? (dir < 0 ? -1e-5 : 1e-5) : dir);
    float3 ta = cRoomLo.xyz * inv, tb = cRoomHi.xyz * inv;
    float3 tmin = min(ta, tb), tmax = max(ta, tb);
    float near = max(max(max(tmin.x, tmin.y), tmin.z), 0);
    far = min(far, min(min(tmax.x, tmax.y), tmax.z));
    float n = max(cProj2.w, 1);
    float stepLen = max(far - near, 0) / n;
    float3 sum = 0;
    [loop] for (int i = 0; i < 32; ++i)
    {
        if (i >= n) break;
        float4 p = float4(dir * (near + stepLen * (i + j)), 1);
        [loop] for (int k = 0; k < 16; ++k)
        {
            if (k >= cPix.w) break;
            float a = dot(p, cRowA[k]);
            float b = dot(p, cRowB[k]);
            float t = dot(p, cRowT[k]);
            float4 lo = cLo[k], hi = cHi[k];
            float w = saturate((1 - abs(a)) * lo.w) * saturate((1 - abs(b)) * lo.w);
            w *= t >= 0 ? saturate((1 - t) * hi.w) : 0;
            float3 inLo = p.xyz - lo.xyz, inHi = hi.xyz - p.xyz;
            w *= min(min(min(inLo.x, inLo.y), inLo.z), min(min(inHi.x, inHi.y), inHi.z)) >= 0 ? 1 : 0;
            sum += cLight[k].rgb * w;
        }
    }
    float mu = saturate(dot(dir, cSun.xyz) * 0.5 + 0.5);
    float phase = (1 - cSun.w) + cSun.w * 3.5 * pow(mu, 6);
    return float4(min(sum * stepLen * cPix.z * phase, 1), 1);
}
)";

    inline const char* const kGlassShaftCompositeHlsl = R"(
sampler2D shaftTex : register(s1);
float4 cShaft : register(c0);  // xy = the march's uv per viewport uv, zw = half a texel of it
float4 cGain  : register(c1);  // x = strength, y = the most added

float4 main(float2 uv : TEXCOORD0) : COLOR
{
    float2 su = uv * cShaft.xy;
    float3 s = tex2Dlod(shaftTex, float4(su - cShaft.zw, 0, 0)).rgb + tex2Dlod(shaftTex, float4(su + float2(cShaft.z, -cShaft.w), 0, 0)).rgb
             + tex2Dlod(shaftTex, float4(su + float2(-cShaft.z, cShaft.w), 0, 0)).rgb + tex2Dlod(shaftTex, float4(su + cShaft.zw, 0, 0)).rgb;
    return float4(min(s * 0.25 * cGain.x, cGain.y), 0);
}
)";
}
