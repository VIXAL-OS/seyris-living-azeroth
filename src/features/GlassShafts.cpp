#include "GlassShafts.hpp"
#include "GlassShaftShaders.hpp"

#include "../debug/Profiler.hpp"
#include "../env/GlassModel.hpp"
#include "../env/ShaftModel.hpp"
#include "../env/WorldQuery.hpp"
#include "../render/SceneDepth.hpp"
#include "Glass.hpp"
#include "GrassPerf.hpp"
#include "GroundFog.hpp"

#include "game/Camera.hpp"
#include "game/Gfx.hpp"
#include "game/Gx.hpp"

#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wxl_livingazeroth::glassshafts
{
    namespace
    {
        namespace gx = wxl::game::gx;
        constexpr const char* kTag = "wxl-seyris-living-azeroth";

        constexpr uintptr_t kCameraInLiquid = 0x00CD8794;

        constexpr float kMarch = 45.0f;       // yd along the view ray at most
        constexpr int   kSteps = 20;          // steps along it (jittered per pixel)
        constexpr float kDensity = 0.03f;     // the room's dusty air, per yd, at strength 1
        constexpr float kForward = 0.6f;      // the forward-scattering share of the phase
        constexpr float kReach = 30.0f, kSoft = 5.0f, kFarSoft = 4.0f; // as the floor light's (features/Glass)
        constexpr float kMostAdded = 0.35f;

        const WXL_Api* g_api = nullptr;
        using WeatherFn = void(__fastcall*)(void* self, void* edx);
        WeatherFn g_origWeather = nullptr;

        IDirect3DDevice9*      g_device = nullptr;
        IDirect3DPixelShader9* g_psMarch = nullptr;
        IDirect3DPixelShader9* g_psComposite = nullptr;
        bool                   g_shaderFailed = false;
        IDirect3DTexture9*     g_tex = nullptr;
        UINT                   g_texW = 0, g_texH = 0;
        bool                   g_texFailed = false;

        const char* g_inactive = "not drawn yet";
        unsigned    g_panes = 0, g_draws = 0;
        double      g_ms = 0.0;
        std::string g_line;

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
                g_api->Log(WXL_LOG_WARN, kTag, "glass shafts: shader compile: %s", static_cast<const char*>(err->GetBufferPointer()));
                err->Release();
            }
            if (FAILED(hr) || !code) return nullptr;
            IDirect3DPixelShader9* ps = nullptr;
            if (FAILED(dev->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()), &ps))) ps = nullptr;
            code->Release();
            return ps;
        }

        void ReleaseAll()
        {
            if (g_tex) { g_tex->Release(); g_tex = nullptr; }
            g_texW = g_texH = 0;
            for (auto** ps : { &g_psMarch, &g_psComposite })
                if (*ps) { (*ps)->Release(); *ps = nullptr; }
            g_shaderFailed = false;
            g_texFailed = false;
            g_device = nullptr;
        }

        bool EnsureTexture(IDirect3DDevice9* dev, UINT w, UINT h)
        {
            if (g_texFailed) return false;
            if (g_tex && w == g_texW && h == g_texH) return true;
            if (g_tex) { g_tex->Release(); g_tex = nullptr; }
            if (FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_tex, nullptr)))
            {
                g_tex = nullptr;
                g_texFailed = true;
                g_api->Log(WXL_LOG_WARN, kTag, "glass shafts: couldn't create their %ux%u target; they stay off.", w, h);
                return false;
            }
            g_texW = w; g_texH = h;
            return true;
        }

        void Sampler(IDirect3DDevice9* dev, DWORD s, D3DTEXTUREFILTERTYPE filter)
        {
            dev->SetSamplerState(s, D3DSAMP_MINFILTER, filter);
            dev->SetSamplerState(s, D3DSAMP_MAGFILTER, filter);
            dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            dev->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
            dev->SetSamplerState(s, D3DSAMP_MAXMIPLEVEL, 0);
        }

        struct Vtx { float x, y, z, rhw, u, v; };
        void Quad(IDirect3DDevice9* dev, float x, float y, float w, float h)
        {
            const float x0 = x - 0.5f, y0 = y - 0.5f, x1 = x0 + w, y1 = y0 + h;
            const Vtx q[4] = { { x0, y0, 0, 1, 0, 0 }, { x1, y0, 0, 1, 1, 0 }, { x0, y1, 0, 1, 0, 1 }, { x1, y1, 0, 1, 1, 1 } };
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));
        }

        std::vector<glass::ShaftPane> g_shaftPanes;

        const char* WhyNot()
        {
            const glass::Settings& s = glass::Config();
            if (!s.enabled || !s.colouredShafts) return "switched off";
            const world::Snapshot& snap = world::Current();
            if (!snap.inWorld || snap.mapId < 0) return "not in the world";
            if (s.shaftStrength <= 0.0f) return "strength 0";
            if (*reinterpret_cast<const uint32_t*>(kCameraInLiquid)) return "camera under water";
            if (!glass::ShaftPanes(g_shaftPanes, kMaxPanes)) return "no sunlit glass in the room you're in";
            if (!depth::Texture()) return "no readable scene depth (see the scene depth panel)";
            if (g_shaderFailed) return "their shaders failed to compile (see the log)";
            if (g_texFailed) return "couldn't create their target (see the log)";
            return nullptr;
        }

        void Draw(IDirect3DDevice9* dev)
        {
            prof::Scope scope(prof::kOursGlassShafts);
            const double t0 = grassperf::Now();
            if (dev != g_device) { ReleaseAll(); g_device = dev; }
            if (!g_psMarch && !g_shaderFailed)
            {
                g_psMarch = Compile(dev, kGlassShaftMarchHlsl);
                g_psComposite = g_psMarch ? Compile(dev, kGlassShaftCompositeHlsl) : nullptr;
                g_shaderFailed = !g_psMarch || !g_psComposite;
            }
            if (g_shaderFailed) { g_inactive = "their shaders failed to compile (see the log)"; return; }

            float view[16], proj[16], cam[3];
            if (!wxl::game::gfx::SceneMatrices(view, proj)) { g_inactive = "no scene matrices"; return; }
            wxl::game::camera::GetPosition(cam);
            IDirect3DSurface9* screen = nullptr;
            if (FAILED(dev->GetRenderTarget(0, &screen)) || !screen) return;
            D3DSURFACE_DESC sd{};
            screen->GetDesc(&sd);
            D3DVIEWPORT9 vp{};
            dev->GetViewport(&vp);
            if (!vp.Width || !vp.Height || vp.MaxZ <= vp.MinZ) { screen->Release(); g_inactive = "unexpected viewport"; return; }

            // The panes as projection rows about the camera, their light x the air's density.
            float sun[3], sunColour[3];
            glass::SunNow(sun, sunColour);
            const float strength = glass::Config().shaftStrength;
            // Six arrays of kMaxPanes registers (GlassShaftShaders.hpp): rows a, b, t, light, room min, room max.
            float panes[4 * kPaneArrays * kMaxPanes] = {};
            auto reg = [&panes](int array, int pane) { return panes + 4 * (array * kMaxPanes + pane); };
            int count = 0;
            for (const glass::ShaftPane& sp : g_shaftPanes)
            {
                if (count >= kMaxPanes) break;
                float rows[12];
                if (!glass::ProjectionRows(*sp.pane, sun, cam, kReach, rows)) continue;
                for (int r = 0; r < 3; ++r) std::memcpy(reg(r, count), rows + 4 * r, sizeof(float) * 4);
                for (int k = 0; k < 3; ++k)
                {
                    reg(3, count)[k] = sp.colour[k] * strength;
                    reg(4, count)[k] = sp.pane->roomMin[k] - cam[k];
                    reg(5, count)[k] = sp.pane->roomMax[k] - cam[k];
                }
                reg(3, count)[3] = 1.0f;
                reg(4, count)[3] = kSoft;
                reg(5, count)[3] = kFarSoft;
                ++count;
            }
            g_panes = static_cast<unsigned>(count);
            // The rooms' box together (the march stays inside it).
            float roomLo[4] = { 1e9f, 1e9f, 1e9f, 0 }, roomHi[4] = { -1e9f, -1e9f, -1e9f, 0 };
            for (int i = 0; i < count; ++i)
                for (int k = 0; k < 3; ++k) { roomLo[k] = std::min(roomLo[k], reg(4, i)[k]); roomHi[k] = std::max(roomHi[k], reg(5, i)[k]); }
            if (!count) { screen->Release(); g_inactive = "no pane lets the sun in"; g_ms = grassperf::Now() - t0; return; }

            const UINT tw = (vp.Width + 3) / 4, th = (vp.Height + 3) / 4;
            if (!EnsureTexture(dev, tw, th)) { screen->Release(); g_inactive = "couldn't create their target (see the log)"; return; }
            if (!depth::Refresh(dev) || !depth::Texture()) { screen->Release(); g_inactive = "the depth copy failed"; return; }
            auto* depthTex = static_cast<IDirect3DTexture9*>(depth::Texture());

            IDirect3DStateBlock9* saved = nullptr;
            if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) { screen->Release(); return; }
            IDirect3DSurface9* depthNow = nullptr;
            dev->GetDepthStencilSurface(&depthNow);

            dev->SetVertexShader(nullptr);
            dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
            dev->SetRenderState(D3DRS_ZENABLE, FALSE);
            dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
            dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
            dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
            dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);
            dev->SetDepthStencilSurface(nullptr);

            // 1. The march.
            bool ok = false;
            {
                IDirect3DSurface9* s = nullptr;
                ok = SUCCEEDED(g_tex->GetSurfaceLevel(0, &s)) && s && SUCCEEDED(dev->SetRenderTarget(0, s));
                if (s) s->Release();
            }
            if (ok)
            {
                const D3DVIEWPORT9 tv = { 0, 0, tw, th, 0.0f, 1.0f };
                dev->SetViewport(&tv);
                float inv[3][3];
                for (int j = 0; j < 3; ++j)
                {
                    const float e[3] = { j == 0 ? 1.0f : 0.0f, j == 1 ? 1.0f : 0.0f, j == 2 ? 1.0f : 0.0f };
                    shafts::ViewToWorld(view, e, inv[j]);
                }
                const float c[11 * 4] = {
                    vp.MinZ, 1.0f / (vp.MaxZ - vp.MinZ), vp.MaxZ + 1.0e-4f, groundfog::Config().depthDecode ? 1.0f : 0.0f,
                    proj[0], proj[5], proj[8], proj[9],
                    proj[10], proj[14], kMarch, static_cast<float>(kSteps),
                    static_cast<float>(vp.X) / sd.Width, static_cast<float>(vp.Y) / sd.Height,
                    static_cast<float>(vp.Width) / sd.Width, static_cast<float>(vp.Height) / sd.Height,
                    4.0f / vp.Width, 4.0f / vp.Height, kDensity, static_cast<float>(count),
                    inv[0][0], inv[0][1], inv[0][2], 0.0f,
                    inv[1][0], inv[1][1], inv[1][2], 0.0f,
                    inv[2][0], inv[2][1], inv[2][2], 0.0f,
                    sun[0], sun[1], sun[2], kForward,
                    roomLo[0], roomLo[1], roomLo[2], 0.0f,
                    roomHi[0], roomHi[1], roomHi[2], 0.0f,
                };
                dev->SetPixelShaderConstantF(0, c, 11);
                dev->SetPixelShaderConstantF(kFirstPaneConstant, panes, kPaneArrays * kMaxPanes);
                dev->SetPixelShader(g_psMarch);
                dev->SetTexture(0, depthTex);
                Sampler(dev, 0, D3DTEXF_POINT);
                Quad(dev, 0.0f, 0.0f, static_cast<float>(tw), static_cast<float>(th));
                dev->SetTexture(0, nullptr);
            }

            // 2. Over the frame, added.
            dev->SetRenderTarget(0, screen);
            dev->SetViewport(&vp);
            if (ok)
            {
                const float c[8] = {
                    vp.Width / (4.0f * tw), vp.Height / (4.0f * th), 0.5f / tw, 0.5f / th,
                    1.0f, kMostAdded, 0.0f, 0.0f,
                };
                dev->SetPixelShaderConstantF(0, c, 2);
                dev->SetPixelShader(g_psComposite);
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
                dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
                dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
                dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x07);
                dev->SetTexture(1, g_tex);
                Sampler(dev, 1, D3DTEXF_LINEAR);
                Quad(dev, static_cast<float>(vp.X), static_cast<float>(vp.Y), static_cast<float>(vp.Width), static_cast<float>(vp.Height));
                dev->SetTexture(1, nullptr);
            }
            dev->SetDepthStencilSurface(depthNow);
            if (depthNow) depthNow->Release();
            screen->Release();
            saved->Apply();
            saved->Release();
            dev->SetViewport(&vp);
            g_inactive = ok ? nullptr : "its target couldn't be bound";
            if (ok) ++g_draws;
            g_ms = grassperf::Now() - t0;
        }

        void __fastcall hkWeather(void* self, void* edx)
        {
            g_inactive = WhyNot();
            if (!g_inactive)
            {
                if (auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice())) Draw(dev);
            }
            else { g_panes = 0; g_ms = 0.0; }
            g_origWeather(self, edx);
        }
    }

    void Install(const WXL_Api* api)
    {
        g_api = api;
        // Behind the sun shafts' link (-950) and in front of the window glow's (-900).
        const int ok = api->HookAttachByName("Weather.Render", reinterpret_cast<void*>(&hkWeather), reinterpret_cast<void**>(&g_origWeather), -940) ||
                       api->HookAttachByName("Weather.Render", reinterpret_cast<void*>(&hkWeather), reinterpret_cast<void**>(&g_origWeather), 1000);
        api->Log(ok ? WXL_LOG_INFO : WXL_LOG_WARN, kTag, "glass shafts: weather render hook %s", ok ? "attached" : "missing (they stay off)");
        if (!ok) g_inactive = "no weather render hook";
    }

    void OnDeviceLost() { ReleaseAll(); }

    const char* StatusLine()
    {
        char line[256];
        if (g_inactive) std::snprintf(line, sizeof(line), "coloured shafts: not drawing: %s", g_inactive);
        else std::snprintf(line, sizeof(line), "coloured shafts: %u pane(s) at %ux%u, %.2f ms CPU (GPU: the profiler's 'ours: glass shafts'); %u frames",
                           g_panes, g_texW, g_texH, g_ms, g_draws);
        g_line = line;
        return g_line.c_str();
    }
}
