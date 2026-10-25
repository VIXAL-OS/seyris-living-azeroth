#include "WindowGlow.hpp"
#include "../debug/MemoryTag.hpp"

#include "../env/WorldQuery.hpp"
#include "../render/SceneDepth.hpp"
#include "GrassPerf.hpp"
#include "GroundFog.hpp"
#include "Windows.hpp"

#include "game/Camera.hpp"
#include "game/Gfx.hpp"
#include "game/Gx.hpp"

#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace wxl_livingazeroth::windowglow
{
    namespace
    {
        namespace gx = wxl::game::gx;
        constexpr const char* kTag = "wxl-seyris-living-azeroth";

        // Client globals (as GroundFog reads them).
        constexpr uintptr_t kCameraInLiquid = 0x00CD8794;
        constexpr uintptr_t kInteriorBlend = 0x00D38B9C;

        constexpr size_t kMaxWindows = 512;   // in range at once (nearest first)
        constexpr float  kMargin = 0.25f;     // yd around the glass's rectangle
        constexpr float  kNearest = 0.3f;     // a window this close to the camera plane (view depth, yd) is skipped
        constexpr float  kBlurPixels = 48.0f; // how far past the glass the halo reaches on screen (the composite's scissor)

        using WeatherFn = void(__fastcall*)(void* self, void* edx);
        WeatherFn      g_origWeather = nullptr;
        const WXL_Api* g_api = nullptr;

        IDirect3DDevice9*      g_device = nullptr;
        IDirect3DPixelShader9* g_psBright = nullptr;
        IDirect3DPixelShader9* g_psGlow = nullptr;
        bool                   g_shaderFailed = false;
        // [0] full size (a multisampled frame's resolve only), [1] the frame at half size, [2] the source (half),
        // [3] 1/4, [4] 1/8.
        IDirect3DTexture9*     g_tex[5] = {};
        UINT                   g_w = 0, g_h = 0;
        D3DMULTISAMPLE_TYPE    g_ms = D3DMULTISAMPLE_NONE;
        bool                   g_texFailed = false;

        // The source: the frame's pixels inside a window's rectangle that are its lit glass. The rectangle's own view
        // depth comes per vertex (perspective-correct); a pixel counts while the scene there is that deep (within
        // cTest.x yd + cTest.y of the distance), bright (luminance from cTest.z) and coloured (saturation from
        // cTest.w): warm lamplight, the elves' gold, the draenei's rose -- not snow, not a moonlit wall.
        const char* const kBrightHlsl = R"(
sampler2D depthTex : register(s0);
sampler2D colorTex : register(s11);
float4 cDepth : register(c0);  // x = viewport min z, y = 1 / (max z - min z), w = depth decode (1 = 0..1)
float4 cProj2 : register(c1);  // x = projection m22, y = m32, zw = 1 / the half-size target's size
float4 cTest  : register(c2);  // x = depth tolerance (yd), y = + share of the distance, z = luminance, w = saturation

struct In { float2 vpos : VPOS; float4 t : TEXCOORD0; }; // t.x = the glass's view depth, t.y = its strength
float4 main(In i) : COLOR
{
    float2 uv = (i.vpos + 0.5) * cProj2.zw;
    float3 c = tex2Dlod(colorTex, float4(uv, 0, 0)).rgb;
    float hi = max(c.r, max(c.g, c.b)), lo = min(c.r, min(c.g, c.b));
    float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
    float sat = (hi - lo) / max(hi, 1e-3);
    float lit = smoothstep(cTest.z, cTest.z + 0.25, lum) * smoothstep(cTest.w, cTest.w + 0.15, sat);
    [branch] if (lit <= 0) return float4(0, 0, 0, 0);
    float d = tex2Dlod(depthTex, float4(uv, 0, 0)).r;
    float d01 = saturate((d - cDepth.x) * cDepth.y);
    float ndcZ = cDepth.w > 0.5 ? d01 : d01 * 2 - 1;
    float viewZ = abs(cProj2.y / min(ndcZ - cProj2.x, -1e-6));
    float tol = cTest.x + cTest.y * i.t.x;
    float glass = 1 - smoothstep(tol, tol * 2, abs(viewZ - i.t.x));
    return float4(max(c - cTest.z * 0.5, 0) * lit * glass * i.t.y, 1);
}
)";

        // The halo: the source at 1/4 (a tight ring) and 1/8 (a wide one), added over the frame, never more than cGlow2.y.
        const char* const kGlowHlsl = R"(
sampler2D quarterTex : register(s10);
sampler2D eighthTex  : register(s12);
float4 cGlow  : register(c4); // xy = one texel of the 1/4 copy, zw = one of the 1/8 copy (uv)
float4 cGlow2 : register(c5); // x = strength, y = the most it adds
float4 cUv    : register(c6); // the copies' uv = xy + screen uv * zw

static const float2 kRing[8] = { float2(1, 0), float2(0.7071, 0.7071), float2(0, 1), float2(-0.7071, 0.7071),
                                 float2(-1, 0), float2(-0.7071, -0.7071), float2(0, -1), float2(0.7071, -0.7071) };

float4 main(float2 uv : TEXCOORD0) : COLOR
{
    float2 suv = cUv.xy + uv * cUv.zw;
    float3 g = tex2Dlod(quarterTex, float4(suv, 0, 0)).rgb * 0.16 + tex2Dlod(eighthTex, float4(suv, 0, 0)).rgb * 0.12;
    [unroll] for (int k = 0; k < 8; ++k)
    {
        g += tex2Dlod(quarterTex, float4(suv + kRing[k] * 1.5 * cGlow.xy, 0, 0)).rgb * 0.06;
        g += tex2Dlod(eighthTex, float4(suv + kRing[k].yx * float2(1, -1) * 2.0 * cGlow.zw, 0, 0)).rgb * 0.05;
    }
    return float4(min(g * cGlow2.x, cGlow2.y), 0);
}
)";

        IDirect3DPixelShader9* Compile(IDirect3DDevice9* dev, const char* source)
        {
            HMODULE m = GetModuleHandleA("d3dcompiler_47.dll");
            if (!m) m = LoadLibraryA("d3dcompiler_47.dll");
            const auto compile = m ? reinterpret_cast<pD3DCompile>(GetProcAddress(m, "D3DCompile")) : nullptr;
            if (!compile) return nullptr;
            ID3DBlob* code = nullptr;
            ID3DBlob* err = nullptr;
            const HRESULT hr = compile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", "ps_3_0", 0, 0, &code, &err);
            if (err)
            {
                g_api->Log(WXL_LOG_WARN, kTag, "window glow: shader compile: %s", static_cast<const char*>(err->GetBufferPointer()));
                err->Release();
            }
            if (FAILED(hr) || !code) return nullptr;
            IDirect3DPixelShader9* ps = nullptr;
            if (FAILED(dev->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()), &ps))) ps = nullptr;
            code->Release();
            return ps;
        }

        void ReleaseTextures()
        {
            for (auto*& t : g_tex) if (t) { t->Release(); t = nullptr; }
            g_w = g_h = 0;
        }

        void ReleaseAll()
        {
            ReleaseTextures();
            if (g_psBright) { g_psBright->Release(); g_psBright = nullptr; }
            if (g_psGlow) { g_psGlow->Release(); g_psGlow = nullptr; }
            g_shaderFailed = false;
            g_texFailed = false;
            g_device = nullptr;
        }

        bool EnsureTextures(IDirect3DDevice9* dev, const D3DSURFACE_DESC& sd)
        {
            if (g_texFailed) return false;
            if (sd.Width == g_w && sd.Height == g_h && sd.MultiSampleType == g_ms && g_tex[1]) return true;
            ReleaseTextures();
            g_w = sd.Width; g_h = sd.Height; g_ms = sd.MultiSampleType;
            const UINT sizes[5][2] = { { sd.Width, sd.Height }, { sd.Width / 2, sd.Height / 2 }, { sd.Width / 2, sd.Height / 2 },
                                       { sd.Width / 4, sd.Height / 4 }, { sd.Width / 8, sd.Height / 8 } };
            for (int k = 0; k < 5; ++k)
            {
                if (k == 0 && sd.MultiSampleType == D3DMULTISAMPLE_NONE) continue; // only to resolve a multisampled frame
                if (FAILED(dev->CreateTexture(std::max(1u, sizes[k][0]), std::max(1u, sizes[k][1]), 1, D3DUSAGE_RENDERTARGET, sd.Format,
                                              D3DPOOL_DEFAULT, &g_tex[k], nullptr)))
                {
                    g_tex[k] = nullptr;
                    g_texFailed = true;
                    ReleaseTextures();
                    g_api->Log(WXL_LOG_WARN, kTag, "window glow: couldn't create its %ux%u target; the glow stays off.", sizes[k][0], sizes[k][1]);
                    return false;
                }
            }
            return true;
        }

        struct Vtx { float x, y, z, rhw, depth, strength; };

        /// Whether the pass runs this frame, and why not (for the panel).
        const char* WhyNot(float& strength)
        {
            const windows::Settings& s = windows::Config();
            const world::Snapshot& snap = world::Current();
            const float night = windows::Night();
            strength = s.glowStrength * night;
            if (!s.glow) return "switched off";
            if (!snap.inWorld || snap.mapId < 0) return "not in the world";
            if (night <= 0.01f) return "daytime: the client's windows are dark";
            if (s.glowStrength <= 0.0f) return "strength 0";
            if (*reinterpret_cast<const uint32_t*>(kCameraInLiquid)) return "camera under water";
            if (*reinterpret_cast<const float*>(kInteriorBlend) >= 0.999f) return "indoors";
            if (windows::ClientGlow() == 1)
            {
                if (s.withClientGlow == 2) return "the client's Full Screen Glow is on (set to give way to it)";
                if (s.withClientGlow == 1) strength *= 0.5f;
            }
            if (!depth::Texture()) return "no readable scene depth (see the scene depth panel)";
            if (g_shaderFailed) return "its shaders failed to compile (see the log)";
            if (g_texFailed) return "couldn't create its targets (see the log)";
            return nullptr;
        }

        void Draw(IDirect3DDevice9* dev, float strength)
        {
            windows::Stats& st = windows::GetStats();
            const double t0 = grassperf::Now();
            if (dev != g_device) { ReleaseAll(); g_device = dev; }
            if (!g_psBright && !g_shaderFailed)
            {
                g_psBright = Compile(dev, kBrightHlsl);
                g_psGlow = g_psBright ? Compile(dev, kGlowHlsl) : nullptr;
                g_shaderFailed = !g_psBright || !g_psGlow;
            }
            if (g_shaderFailed) { st.glowInactive = "its shaders failed to compile (see the log)"; return; }

            float view[16], proj[16], cam[3];
            if (!wxl::game::gfx::SceneMatrices(view, proj)) { st.glowInactive = "no scene matrices"; return; }
            wxl::game::camera::GetPosition(cam);
            // The scene view is camera-relative when the eye sits at its origin (GroundFog: the eye is added back).
            const bool relative = std::fabs(view[12]) + std::fabs(view[13]) + std::fabs(view[14]) < 1.0f;

            IDirect3DSurface9* screen = nullptr;
            if (FAILED(dev->GetRenderTarget(0, &screen)) || !screen) return;
            D3DSURFACE_DESC sd{};
            screen->GetDesc(&sd);
            D3DVIEWPORT9 vp{};
            dev->GetViewport(&vp);
            if (!vp.Width || !vp.Height || vp.MaxZ <= vp.MinZ) { screen->Release(); st.glowInactive = "unexpected viewport"; return; }

            // The windows in range, as quads in the half-size source.
            const windows::Settings& s = windows::Config();
            static std::vector<const windows::Window*> nearby;
            windows::Near(static_cast<uint32_t>(world::Current().mapId), cam, s.glowRange, true, kMaxWindows, nearby);
            st.glowWindows = static_cast<unsigned>(nearby.size());
            static std::vector<Vtx> quads;
            quads.clear();
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f; // full-size pixels the glass covers
            const float fadeFrom = s.glowRange * 0.7f;
            for (const windows::Window* w : nearby)
            {
                const float mid[3] = { w->pos[0] - w->facing[0] * w->back, w->pos[1] - w->facing[1] * w->back, w->pos[2] - w->facing[2] * w->back };
                const float e[3] = { cam[0] - mid[0], cam[1] - mid[1], cam[2] - mid[2] };
                const float dist = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
                if (e[0] * w->facing[0] + e[1] * w->facing[1] + e[2] * w->facing[2] <= 0.05f * dist) continue; // seen from behind or edge-on
                const float fade = dist <= fadeFrom ? 1.0f : std::max(0.0f, 1.0f - (dist - fadeFrom) / (s.glowRange - fadeFrom));
                if (fade <= 0.0f) continue;
                // The share of its glass whose room is lit (the rooms go dark as the night goes on).
                const float lit = windows::LitShare(*w);
                if (lit <= 0.002f) continue;
                float corner[4][3];
                windows::GlassCorners(*w, kMargin, corner);
                Vtx v[4];
                bool ok = true;
                for (int c = 0; c < 4 && ok; ++c)
                {
                    const float p[3] = { corner[c][0] - (relative ? cam[0] : 0.0f), corner[c][1] - (relative ? cam[1] : 0.0f),
                                         corner[c][2] - (relative ? cam[2] : 0.0f) };
                    float vv[4], clip[4];
                    for (int j = 0; j < 4; ++j) vv[j] = p[0] * view[j] + p[1] * view[4 + j] + p[2] * view[8 + j] + view[12 + j];
                    for (int j = 0; j < 4; ++j) clip[j] = vv[0] * proj[j] + vv[1] * proj[4 + j] + vv[2] * proj[8 + j] + vv[3] * proj[12 + j];
                    if (!(clip[3] > kNearest)) { ok = false; break; }
                    const float nx = clip[0] / clip[3], ny = clip[1] / clip[3];
                    const float px = vp.X + (nx * 0.5f + 0.5f) * vp.Width, py = vp.Y + (0.5f - ny * 0.5f) * vp.Height;
                    x0 = std::min(x0, px); x1 = std::max(x1, px); y0 = std::min(y0, py); y1 = std::max(y1, py);
                    v[c] = { px * 0.5f - 0.5f, py * 0.5f - 0.5f, 0.0f, 1.0f / clip[3], clip[3], fade * lit * std::max(0.5f, std::min(1.5f, w->strength)) };
                }
                if (!ok) continue; // (a window past the screen's edge still glows onto it: the rasterizer clips its quad)
                quads.push_back(v[0]); quads.push_back(v[1]); quads.push_back(v[2]);
                quads.push_back(v[2]); quads.push_back(v[1]); quads.push_back(v[3]);
            }
            st.glowDrawn = static_cast<unsigned>(quads.size() / 6);
            if (quads.empty()) { screen->Release(); st.glowInactive = nullptr; st.glowMs = grassperf::Now() - t0; return; }
            const float sx0 = std::max(static_cast<float>(vp.X), x0 - kBlurPixels), sy0 = std::max(static_cast<float>(vp.Y), y0 - kBlurPixels);
            const float sx1 = std::min(static_cast<float>(vp.X + vp.Width), x1 + kBlurPixels), sy1 = std::min(static_cast<float>(vp.Y + vp.Height), y1 + kBlurPixels);
            if (sx1 <= sx0 || sy1 <= sy0) { screen->Release(); st.glowInactive = nullptr; st.glowMs = grassperf::Now() - t0; return; }

            if (!EnsureTextures(dev, sd)) { screen->Release(); st.glowInactive = "couldn't create its targets (see the log)"; return; }
            if (!depth::Refresh(dev) || !depth::Texture()) { screen->Release(); st.glowInactive = "the depth copy failed"; return; }
            auto* depthTex = static_cast<IDirect3DTexture9*>(depth::Texture());

            // The frame at half size (a full-size resolve first when it's multisampled).
            bool ok = true;
            {
                IDirect3DSurface9* from = screen;
                from->AddRef();
                for (int k = 0; k < 2 && ok; ++k)
                {
                    if (!g_tex[k]) continue;
                    IDirect3DSurface9* to = nullptr;
                    ok = SUCCEEDED(g_tex[k]->GetSurfaceLevel(0, &to)) && to &&
                         SUCCEEDED(dev->StretchRect(from, nullptr, to, nullptr, k == 0 ? D3DTEXF_NONE : D3DTEXF_LINEAR));
                    from->Release();
                    from = to;
                }
                if (from) from->Release();
            }
            if (!ok) { screen->Release(); st.glowInactive = "the frame copy failed"; return; }

            IDirect3DStateBlock9* saved = nullptr;
            if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) { screen->Release(); return; }
            IDirect3DSurface9* depthNow = nullptr;
            dev->GetDepthStencilSurface(&depthNow);

            dev->SetVertexShader(nullptr);
            dev->SetRenderState(D3DRS_ZENABLE, FALSE);
            dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
            dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
            dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
            for (DWORD smp : { 0u, 11u })
            {
                dev->SetSamplerState(smp, D3DSAMP_MINFILTER, D3DTEXF_POINT);
                dev->SetSamplerState(smp, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
                dev->SetSamplerState(smp, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
                dev->SetSamplerState(smp, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
                dev->SetSamplerState(smp, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
                dev->SetSamplerState(smp, D3DSAMP_SRGBTEXTURE, FALSE);
            }

            // The source: cleared, then each window's glass at its brightest (max: overlapping rectangles don't add up).
            const UINT hw = std::max(1u, g_w / 2), hh = std::max(1u, g_h / 2);
            IDirect3DSurface9* bright = nullptr;
            ok = SUCCEEDED(g_tex[2]->GetSurfaceLevel(0, &bright)) && bright;
            if (ok)
            {
                dev->SetRenderTarget(0, bright);
                dev->SetDepthStencilSurface(nullptr); // the depth is read, not tested
                dev->ColorFill(bright, nullptr, D3DCOLOR_ARGB(0, 0, 0, 0));
                const D3DVIEWPORT9 hv = { 0, 0, hw, hh, 0.0f, 1.0f };
                dev->SetViewport(&hv);
                dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
                dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x07);
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_MAX);
                dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
                dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
                const float c[12] = {
                    vp.MinZ, 1.0f / (vp.MaxZ - vp.MinZ), 0.0f, groundfog::Config().depthDecode ? 1.0f : 0.0f,
                    proj[10], proj[14], 1.0f / hw, 1.0f / hh,
                    0.6f, 0.004f, 0.30f, 0.20f,
                };
                dev->SetPixelShaderConstantF(0, c, 3);
                dev->SetPixelShader(g_psBright);
                dev->SetTexture(0, depthTex);
                dev->SetTexture(11, g_tex[1]);
                dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
                dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(quads.size() / 3), quads.data(), sizeof(Vtx));
                dev->SetTexture(0, nullptr);
                dev->SetTexture(11, nullptr);
            }
            // Down to 1/4 and 1/8 (each a box filter of the one above).
            for (int k = 3; k < 5 && ok; ++k)
            {
                IDirect3DSurface9* from = nullptr;
                IDirect3DSurface9* to = nullptr;
                ok = SUCCEEDED(g_tex[k - 1]->GetSurfaceLevel(0, &from)) && from && SUCCEEDED(g_tex[k]->GetSurfaceLevel(0, &to)) && to &&
                     SUCCEEDED(dev->StretchRect(from, nullptr, to, nullptr, D3DTEXF_LINEAR));
                if (from) from->Release();
                if (to) to->Release();
            }
            if (bright) bright->Release();
            dev->SetRenderTarget(0, screen); // (resets the viewport: the world's goes back below)
            dev->SetDepthStencilSurface(depthNow);
            dev->SetViewport(&vp);
            if (ok)
            {
                // The halo over the frame, only around the glass (scissored), added.
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
                dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
                dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
                dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x07);
                const RECT scissor = { static_cast<LONG>(sx0), static_cast<LONG>(sy0), static_cast<LONG>(std::ceil(sx1)), static_cast<LONG>(std::ceil(sy1)) };
                dev->SetScissorRect(&scissor);
                dev->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
                const UINT qw = std::max(1u, g_w / 4), qh = std::max(1u, g_h / 4), ew = std::max(1u, g_w / 8), eh = std::max(1u, g_h / 8);
                const float g[12] = {
                    1.0f / qw, 1.0f / qh, 1.0f / ew, 1.0f / eh,
                    1.4f * strength, 0.35f, 0.0f, 0.0f,
                    static_cast<float>(vp.X) / g_w, static_cast<float>(vp.Y) / g_h, static_cast<float>(vp.Width) / g_w, static_cast<float>(vp.Height) / g_h,
                };
                dev->SetPixelShaderConstantF(4, g, 3);
                dev->SetPixelShader(g_psGlow);
                for (DWORD smp : { 10u, 12u })
                {
                    dev->SetSamplerState(smp, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                    dev->SetSamplerState(smp, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
                    dev->SetSamplerState(smp, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
                    dev->SetSamplerState(smp, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
                    dev->SetSamplerState(smp, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
                    dev->SetSamplerState(smp, D3DSAMP_SRGBTEXTURE, FALSE);
                }
                dev->SetTexture(10, g_tex[3]);
                dev->SetTexture(12, g_tex[4]);
                struct Quad { float x, y, z, rhw, u, v; };
                const float qx0 = vp.X - 0.5f, qy0 = vp.Y - 0.5f, qx1 = qx0 + vp.Width, qy1 = qy0 + vp.Height;
                const Quad quad[4] = { { qx0, qy0, 0, 1, 0, 0 }, { qx1, qy0, 0, 1, 1, 0 }, { qx0, qy1, 0, 1, 0, 1 }, { qx1, qy1, 0, 1, 1, 1 } };
                dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
                dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Quad));
                dev->SetTexture(10, nullptr);
                dev->SetTexture(12, nullptr);
            }
            if (depthNow) depthNow->Release();
            screen->Release();
            saved->Apply();
            saved->Release();
            st.glowInactive = ok ? nullptr : "a copy failed";
            st.glowMs = grassperf::Now() - t0;
        }

        void __fastcall hkWeather(void* self, void* edx)
        {
            memuse::Scope memory(memuse::kWindows);
            windows::Stats& st = windows::GetStats();
            float strength = 0.0f;
            st.glowInactive = WhyNot(strength);
            if (!st.glowInactive)
            {
                if (auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice())) Draw(dev, strength);
            }
            else { st.glowDrawn = 0; st.glowMs = 0.0; }
            g_origWeather(self, edx);
        }
    }

    void Install(const WXL_Api* api)
    {
        g_api = api;
        // Behind the ground fog's link (-1000, which draws the fog and then calls on): the glow goes over the fog.
        const int ok = api->HookAttachByName("Weather.Render", reinterpret_cast<void*>(&hkWeather), reinterpret_cast<void**>(&g_origWeather), -900) ||
                       api->HookAttachByName("Weather.Render", reinterpret_cast<void*>(&hkWeather), reinterpret_cast<void**>(&g_origWeather), 1000);
        api->Log(ok ? WXL_LOG_INFO : WXL_LOG_WARN, kTag, "window glow: weather render hook %s", ok ? "attached" : "missing (the glow stays off)");
        if (!ok) windows::GetStats().glowInactive = "no weather render hook";
    }

    void OnDeviceLost() { ReleaseAll(); }
}

namespace wxl_livingazeroth::windowglow
{
    void GpuMemory(memuse::Gpu& out)
    {
        for (IDirect3DTexture9* t : g_tex) out.Add(t);
    }
}
