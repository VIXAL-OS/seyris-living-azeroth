#include "SunShafts.hpp"
#include "SunShaftShaders.hpp"

#include "../debug/Profiler.hpp"
#include "../env/Climate.hpp"
#include "../env/Fog.hpp"
#include "../env/ShaftModel.hpp"
#include "../env/Storm.hpp"
#include "../env/WorldQuery.hpp"
#include "../render/SceneDepth.hpp"
#include "GrassPerf.hpp"
#include "GroundFog.hpp"
#include "SurfaceCover.hpp"

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

namespace wxl_livingazeroth::sunshafts
{
    namespace
    {
        namespace gx = wxl::game::gx;
        constexpr const char* kTag = "wxl-seyris-living-azeroth";

        // Client globals (as GroundFog reads them).
        constexpr uintptr_t kCameraInLiquid = 0x00CD8794;
        constexpr uintptr_t kInteriorBlend = 0x00D38B9C;
        constexpr uintptr_t kFogColor = 0x00D38BF4;

        constexpr float kScreenMargin = 0.35f; // uv off the screen at which the screen-space shafts are gone
        constexpr float kScreenGain = 0.6f;    // the screen-space shafts' brightness at strength 1 and a full model
        constexpr float kSkyShare = 0.6f;      // ... over the sky itself (round the sun they'd be a glow, not shafts)
        constexpr float kVolumeGain = 0.5f;    // the in-scatter under the deck at strength 1 and a full model
        constexpr float kMostAdded = 0.6f;     // never more than this added to a pixel's colour
        constexpr float kGapBoost = 0.5f;      // up to this much more where branches break the line to the sun into gaps
        constexpr float kNearYards = 20.0f;    // a surface needs about this much air in front of it for the full shafts
        constexpr float kDecay = 0.985f;       // each tap's weight after the one before (the 32nd: 0.63)
        constexpr float kVolumeReach = 600.0f; // yd the in-scatter is marched
        constexpr float kDeckShadow = 2.0f;    // the deck's shadow: its optical depth toward the sun x this (its look is
                                               // tuned to see a few hundred yd inside it; real cloud shades more). The
                                               // thick decks (Storm Peaks: 910 yd) shade by their banks, thin ones
                                               // (Alterac: 190 yd) by their puffs.
        constexpr int   kVolumeSteps = 12;

        Settings       g_settings;
        const WXL_Api* g_api = nullptr;

        using WeatherFn = void(__fastcall*)(void* self, void* edx);
        WeatherFn g_origWeather = nullptr;

        IDirect3DDevice9*      g_device = nullptr;
        IDirect3DPixelShader9* g_psSource = nullptr;
        IDirect3DPixelShader9* g_psBlur = nullptr;
        IDirect3DPixelShader9* g_psComposite = nullptr;
        bool                   g_shaderFailed = false;
        IDirect3DTexture9*     g_tex[2] = {};   // the source, then the blur's ping-pong (A8R8G8B8: r the shafts, g the in-scatter)
        UINT                   g_texW = 0, g_texH = 0;
        bool                   g_texFailed = false;

        struct Stats
        {
            const char*       inactive = "not drawn yet";
            shafts::Strength  model;
            shafts::SunScreen sun;              // the disc the shafts radiate from (the sky's sun, or the moon)
            bool              moonLeads = false;
            float             horizon = 1.0f;   // the disc's horizon fade
            shafts::SunScreen skySun, light;     // the sky's sun and the light's direction on the screen (the panel)
            float             screen = 0.0f, volume = 0.0f; // this frame's two parts, after the strength setting
            const char*       deck = "";
            float             colour[3] = {};
            UINT              w = 0, h = 0;
            double            ms = 0.0;
            unsigned          draws = 0;
        };
        Stats g_stats;

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
                g_api->Log(WXL_LOG_WARN, kTag, "sun shafts: shader compile: %s", static_cast<const char*>(err->GetBufferPointer()));
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
            g_texW = g_texH = 0;
        }

        void ReleaseAll()
        {
            ReleaseTextures();
            for (auto** ps : { &g_psSource, &g_psBlur, &g_psComposite })
                if (*ps) { (*ps)->Release(); *ps = nullptr; }
            g_shaderFailed = false;
            g_texFailed = false;
            g_device = nullptr;
        }

        bool EnsureTextures(IDirect3DDevice9* dev, UINT w, UINT h)
        {
            if (g_texFailed) return false;
            if (w == g_texW && h == g_texH && g_tex[0] && g_tex[1]) return true;
            ReleaseTextures();
            for (auto*& t : g_tex)
            {
                if (FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t, nullptr)))
                {
                    t = nullptr;
                    ReleaseTextures();
                    g_texFailed = true;
                    g_api->Log(WXL_LOG_WARN, kTag, "sun shafts: couldn't create their %ux%u target; they stay off.", w, h);
                    return false;
                }
            }
            g_texW = w; g_texH = h;
            return true;
        }

        float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : (v == v ? v : 0.0f)); }

        /// The model's inputs this frame.
        shafts::Inputs GatherInputs()
        {
            shafts::Inputs in;
            const climate::Sun& sun = climate::SunNow();
            in.sunHeight = sun.dir[2];
            in.hour = climate::Now().dayFraction * 24.0f;
            const fog::RulesInputs& fi = fog::Inputs();
            in.night = fi.valid ? fi.night : 1.0f - sun.daylight;
            const world::Snapshot& s = world::Current();
            if (g_settings.testAir >= 0.0f) in.fogginess = Clamp01(g_settings.testAir) / 1.2f; // the air as the panel says
            else
            {
                in.fogginess = fi.valid ? fi.fogginess : climate::Here().fogginess;
                in.mist = fog::AmountAt(fog::kMist, s.playerPos[0], s.playerPos[1]);
                in.areaFog = fog::AmountAt(fog::kAreaFog, s.playerPos[0], s.playerPos[1]);
                in.haze = fog::AmountAt(fog::kHaze, s.playerPos[0], s.playerPos[1]);
                in.waterFog = fog::AmountAt(fog::kWaterFog, s.playerPos[0], s.playerPos[1]);
            }
            const climate::Weather& w = climate::CurrentWeather();
            in.precipitation = (w.type == 1 || w.type == 2) ? w.intensity : (w.type == 3 ? 0.6f * w.intensity : 0.0f);
            in.storminess = storm::GetStatus().storminess;
            in.interior = Clamp01(*reinterpret_cast<const float*>(kInteriorBlend));
            in.moon = g_settings.moon;
            return in;
        }

        /// Whether the pass runs this frame, and why not (for the panel).
        const char* WhyNot()
        {
            g_stats.model = shafts::Strength{};
            if (!g_settings.enabled) return "switched off";
            const world::Snapshot& snap = world::Current();
            if (!snap.inWorld || snap.mapId < 0) return "not in the world";
            if (g_settings.strength <= 0.0f) return "strength 0";
            if (*reinterpret_cast<const uint32_t*>(kCameraInLiquid)) return "camera under water";
            g_stats.model = shafts::Evaluate(GatherInputs());
            if (g_stats.model.why) return g_stats.model.why;
            if (!depth::Texture()) return "no readable scene depth (see the scene depth panel)";
            if (g_shaderFailed) return "their shaders failed to compile (see the log)";
            if (g_texFailed) return "couldn't create their targets (see the log)";
            return nullptr;
        }

        /// The light's colour: the client's sun (or moon) light as the terrain takes it, a little toward the fog's
        /// colour, at its hue's full value x a level by how bright it is.
        void LightColour(float out[3])
        {
            const uint32_t argb = *reinterpret_cast<const uint32_t*>(kFogColor);
            const float fog[3] = { ((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f };
            float ambient[3], diffuse[3], c[3];
            float level = 0.7f;
            if (cover::TerrainLight(ambient, diffuse))
            {
                for (int i = 0; i < 3; ++i) c[i] = 0.75f * diffuse[i] + 0.25f * fog[i];
                const float lum = 0.2126f * (diffuse[0] + 0.5f * ambient[0]) + 0.7152f * (diffuse[1] + 0.5f * ambient[1]) + 0.0722f * (diffuse[2] + 0.5f * ambient[2]);
                level = std::clamp(0.45f + 0.9f * lum, 0.45f, 1.0f);
            }
            else for (int i = 0; i < 3; ++i) c[i] = fog[i];
            const float peak = std::max(c[0], std::max(c[1], c[2]));
            const float warm[3] = { 1.0f, 0.95f, 0.85f };
            for (int i = 0; i < 3; ++i) out[i] = (peak > 0.02f ? c[i] / peak : warm[i]) * level;
        }

        struct Vtx { float x, y, z, rhw, u, v; };
        void Quad(IDirect3DDevice9* dev, float x, float y, float w, float h)
        {
            const float x0 = x - 0.5f, y0 = y - 0.5f, x1 = x0 + w, y1 = y0 + h;
            const Vtx q[4] = { { x0, y0, 0, 1, 0, 0 }, { x1, y0, 0, 1, 1, 0 }, { x0, y1, 0, 1, 0, 1 }, { x1, y1, 0, 1, 1, 1 } };
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));
        }

        void Sampler(IDirect3DDevice9* dev, DWORD s, D3DTEXTUREFILTERTYPE filter, D3DTEXTUREADDRESS address, D3DTEXTUREFILTERTYPE mip = D3DTEXF_NONE)
        {
            dev->SetSamplerState(s, D3DSAMP_MINFILTER, filter);
            dev->SetSamplerState(s, D3DSAMP_MAGFILTER, filter);
            dev->SetSamplerState(s, D3DSAMP_MIPFILTER, mip);
            dev->SetSamplerState(s, D3DSAMP_ADDRESSU, address);
            dev->SetSamplerState(s, D3DSAMP_ADDRESSV, address);
            dev->SetSamplerState(s, D3DSAMP_ADDRESSW, address);
            dev->SetSamplerState(s, D3DSAMP_BORDERCOLOR, 0);
            dev->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
            dev->SetSamplerState(s, D3DSAMP_MAXMIPLEVEL, 0);    // (the deck's noise: its long steps read its smaller levels)
            dev->SetSamplerState(s, D3DSAMP_MIPMAPLODBIAS, 0);
        }

        bool Target(IDirect3DDevice9* dev, IDirect3DTexture9* tex)
        {
            IDirect3DSurface9* s = nullptr;
            if (FAILED(tex->GetSurfaceLevel(0, &s)) || !s) return false;
            const bool ok = SUCCEEDED(dev->SetRenderTarget(0, s));
            s->Release();
            return ok;
        }

        void Draw(IDirect3DDevice9* dev)
        {
            prof::Scope scope(prof::kOursShafts);
            const double t0 = grassperf::Now();
            if (dev != g_device) { ReleaseAll(); g_device = dev; }
            if (!g_psSource && !g_shaderFailed)
            {
                g_psSource = Compile(dev, kShaftSourceHlsl);
                g_psBlur = g_psSource ? Compile(dev, kShaftBlurHlsl) : nullptr;
                g_psComposite = g_psBlur ? Compile(dev, kShaftCompositeHlsl) : nullptr;
                g_shaderFailed = !g_psSource || !g_psBlur || !g_psComposite;
            }
            if (g_shaderFailed) { g_stats.inactive = "their shaders failed to compile (see the log)"; return; }

            float view[16], proj[16], cam[3];
            if (!wxl::game::gfx::SceneMatrices(view, proj)) { g_stats.inactive = "no scene matrices"; return; }
            wxl::game::camera::GetPosition(cam);
            IDirect3DSurface9* screen = nullptr;
            if (FAILED(dev->GetRenderTarget(0, &screen)) || !screen) return;
            D3DSURFACE_DESC sd{};
            screen->GetDesc(&sd);
            D3DVIEWPORT9 vp{};
            dev->GetViewport(&vp);
            if (!vp.Width || !vp.Height || vp.MaxZ <= vp.MinZ) { screen->Release(); g_stats.inactive = "unexpected viewport"; return; }

            // Where they radiate from, and the two parts. The screen-space shafts radiate from the disc the player
            // sees -- the sky's sun, or the moon when the moon's part leads -- and fade as it leaves the screen or
            // sinks below the horizon; the light's direction (the client sets it lower and apart: env/SkyPath) would
            // put their centre half a screen off the sun. The in-scatter under the deck is a march through the air
            // lit along the light's direction, as the scene is shaded (its shadows fall with the world's), with the
            // light anywhere above the horizon.
            const climate::Sun& sunNow = climate::SunNow();
            g_stats.moonLeads = g_stats.model.moon > g_stats.model.sun;
            const float* disc = g_stats.moonLeads ? sunNow.moon : sunNow.sky;
            g_stats.skySun = shafts::ProjectSun(view, proj, sunNow.sky, kScreenMargin);
            g_stats.light = shafts::ProjectSun(view, proj, sunNow.dir, kScreenMargin);
            g_stats.sun = g_stats.moonLeads ? shafts::ProjectSun(view, proj, disc, kScreenMargin) : g_stats.skySun;
            g_stats.horizon = shafts::HorizonFade(disc[2]);
            const float total = g_stats.model.total * g_settings.strength;
            const float screenPart = total * g_stats.sun.fade * g_stats.horizon;
            const groundfog::DeckFrame& deck = groundfog::DeckThisFrame();
            const bool deckOn = g_settings.throughDeck && deck.drawn && deck.density > 0.0f;
            float volume = 0.0f;
            if (deckOn && deck.noise && sunNow.dir[2] > 0.05f)
                volume = total * shafts::UnderDeck(cam[2], deck.base, deck.top) * (0.25f + 0.75f * Clamp01(deck.cover));
            g_stats.screen = screenPart;
            g_stats.volume = volume;
            g_stats.deck = !g_settings.throughDeck ? "left out (ThroughCloudDeck 0)"
                         : !deckOn ? "none here"
                         : deck.result ? (volume > 0.003f ? "its gaps, and shafts under it" : "its gaps (you're not under it)")
                                       : "its slab, evenly (CloudDeckSmooth is off: no gaps)";
            if (screenPart < 0.003f && volume < 0.003f)
            {
                screen->Release();
                const bool moon = g_stats.moonLeads;
                g_stats.inactive = g_stats.horizon < 0.01f ? (moon ? "the moon is below the horizon" : "the sun is below the horizon")
                                 : !g_stats.sun.inFront ? (moon ? "the moon is behind you" : "the sun is behind you")
                                                        : (moon ? "the moon is off the screen" : "the sun is off the screen");
                g_stats.ms = grassperf::Now() - t0;
                return;
            }

            const int div = g_settings.quality >= 2 ? 2 : 4;
            const UINT tw = (vp.Width + div - 1) / div, th = (vp.Height + div - 1) / div;
            if (!EnsureTextures(dev, tw, th)) { screen->Release(); g_stats.inactive = "couldn't create their targets (see the log)"; return; }
            if (!depth::Refresh(dev) || !depth::Texture()) { screen->Release(); g_stats.inactive = "the depth copy failed"; return; }
            auto* depthTex = static_cast<IDirect3DTexture9*>(depth::Texture());
            g_stats.w = tw; g_stats.h = th;

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
            dev->SetDepthStencilSurface(nullptr); // the depth is read, never tested (and can't be bound while it's read)

            // Constants the passes share.
            const float m22 = proj[10], m32 = proj[14];
            float farClip = m32 / (1.0f - m22);
            if (!(farClip > 10.0f && farClip < 100000.0f)) farClip = 1000.0f;
            const float depthC[4] = { vp.MinZ, 1.0f / (vp.MaxZ - vp.MinZ), vp.MaxZ + 1.0e-4f, groundfog::Config().depthDecode ? 1.0f : 0.0f };
            const float uvC[4] = { static_cast<float>(vp.X) / sd.Width, static_cast<float>(vp.Y) / sd.Height,
                                   static_cast<float>(vp.Width) / sd.Width, static_cast<float>(vp.Height) / sd.Height };
            const D3DVIEWPORT9 tv = { 0, 0, tw, th, 0.0f, 1.0f };

            // 1. The source.
            bool ok = Target(dev, g_tex[0]);
            if (ok)
            {
                dev->SetViewport(&tv);
                float inv[3][3];
                for (int j = 0; j < 3; ++j)
                {
                    const float e[3] = { j == 0 ? 1.0f : 0.0f, j == 1 ? 1.0f : 0.0f, j == 2 ? 1.0f : 0.0f };
                    shafts::ViewToWorld(view, e, inv[j]);
                }
                const float taps = div / 4.0f; // full pixels: the centres of the block's quarters
                const int deckMode = deckOn ? (deck.result ? 1 : 2) : 0;
                const float c[15 * 4] = {
                    depthC[0], depthC[1], depthC[2], depthC[3],
                    proj[0], proj[5], proj[8], proj[9],
                    m22, m32, farClip, g_stats.model.airPerYard,
                    uvC[0], uvC[1], uvC[2], uvC[3],
                    static_cast<float>(div) / vp.Width, static_cast<float>(div) / vp.Height, taps / vp.Width, taps / vp.Height,
                    g_stats.sun.view[0], g_stats.sun.view[1], g_stats.sun.view[2], screenPart >= 0.003f ? 1.0f : 0.0f,
                    static_cast<float>(deckMode), shafts::DeckMeanExtinction(deck.density, deck.cover),
                    deck.resultW ? vp.Width / (2.0f * deck.resultW) : 1.0f, deck.resultH ? vp.Height / (2.0f * deck.resultH) : 1.0f,
                    inv[0][0], inv[0][1], inv[0][2], 0.0f,
                    inv[1][0], inv[1][1], inv[1][2], 0.0f,
                    inv[2][0], inv[2][1], inv[2][2], 0.0f,
                    cam[0], cam[1], cam[2], volume >= 0.003f ? 1.0f : 0.0f,
                    deck.base, deck.top, deck.density, deck.invTile,
                    deck.banks, kVolumeReach, kDeckShadow, deck.range,
                    deck.drift[0], deck.drift[1], deck.drift[2], g_stats.model.airPerYard,
                    sunNow.dir[0], sunNow.dir[1], sunNow.dir[2], static_cast<float>(kVolumeSteps),
                };
                dev->SetPixelShaderConstantF(0, c, 15);
                dev->SetPixelShader(g_psSource);
                dev->SetTexture(0, depthTex);
                Sampler(dev, 0, D3DTEXF_POINT, D3DTADDRESS_CLAMP);
                dev->SetTexture(1, deckMode == 1 ? static_cast<IDirect3DTexture9*>(deck.result) : nullptr);
                Sampler(dev, 1, D3DTEXF_LINEAR, D3DTADDRESS_CLAMP);
                dev->SetTexture(2, volume >= 0.003f ? static_cast<IDirect3DVolumeTexture9*>(deck.noise) : nullptr);
                Sampler(dev, 2, D3DTEXF_LINEAR, D3DTADDRESS_WRAP, D3DTEXF_LINEAR);
                Quad(dev, 0.0f, 0.0f, static_cast<float>(tw), static_cast<float>(th));
                dev->SetTexture(0, nullptr);
                dev->SetTexture(1, nullptr);
                dev->SetTexture(2, nullptr);
            }

            // 2. The radial blur toward the sun (the sun in the target's uv), then a short smoothing pass.
            IDirect3DTexture9* result = g_tex[0];
            if (ok)
            {
                const bool front = g_stats.sun.inFront;
                const float su = front ? g_stats.sun.u * vp.Width / (div * static_cast<float>(tw)) : 0.5f;
                const float sv = front ? g_stats.sun.v * vp.Height / (div * static_cast<float>(th)) : 0.5f;
                const int n = std::clamp(g_settings.samples, 8, 64);
                const float reach = std::clamp(g_settings.length, 0.05f, 1.0f);
                dev->SetPixelShader(g_psBlur);
                Sampler(dev, 0, D3DTEXF_LINEAR, D3DTADDRESS_BORDER); // off the screen: no light
                const int passes = g_settings.quality >= 1 ? 2 : 1;
                for (int p = 0; p < passes && ok; ++p)
                {
                    IDirect3DTexture9* from = g_tex[p & 1];
                    IDirect3DTexture9* to = g_tex[(p + 1) & 1];
                    ok = Target(dev, to);
                    if (!ok) break;
                    dev->SetViewport(&tv);
                    const float c[12] = {
                        su, sv, p == 0 ? reach : 2.5f * reach / n, p == 0 ? static_cast<float>(n) : 8.0f,
                        p == 0 ? kDecay : 1.0f, p == 0 ? 1.0f : 0.0f, 1.0f / tw, 1.0f / th,
                        p == 0 ? kGapBoost : 0.0f, 0.0f, 0.0f, 0.0f,
                    };
                    dev->SetPixelShaderConstantF(0, c, 3);
                    dev->SetTexture(0, from);
                    Quad(dev, 0.0f, 0.0f, static_cast<float>(tw), static_cast<float>(th));
                    dev->SetTexture(0, nullptr);
                    result = to;
                }
            }

            // 3. Over the frame, added.
            dev->SetRenderTarget(0, screen); // (resets the viewport: the world's goes back below)
            dev->SetViewport(&vp);
            if (ok)
            {
                float colour[3];
                LightColour(colour);
                for (int i = 0; i < 3; ++i) g_stats.colour[i] = colour[i];
                const float s = kScreenGain * screenPart, v = kVolumeGain * volume;
                const float c[6 * 4] = {
                    depthC[0], depthC[1], depthC[2], depthC[3],
                    m22, m32, 1.0f / kNearYards, 0.0f,
                    uvC[0], uvC[1], uvC[2], uvC[3],
                    vp.Width / (div * static_cast<float>(tw)), vp.Height / (div * static_cast<float>(th)), 0.5f / tw, 0.5f / th,
                    colour[0] * s, colour[1] * s, colour[2] * s, kMostAdded,
                    colour[0] * v, colour[1] * v, colour[2] * v, kSkyShare,
                };
                dev->SetPixelShaderConstantF(0, c, 6);
                dev->SetPixelShader(g_psComposite);
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
                dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
                dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
                dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x07);
                dev->SetTexture(0, depthTex);
                Sampler(dev, 0, D3DTEXF_POINT, D3DTADDRESS_CLAMP);
                dev->SetTexture(1, result);
                Sampler(dev, 1, D3DTEXF_LINEAR, D3DTADDRESS_CLAMP);
                Quad(dev, static_cast<float>(vp.X), static_cast<float>(vp.Y), static_cast<float>(vp.Width), static_cast<float>(vp.Height));
                dev->SetTexture(0, nullptr);
                dev->SetTexture(1, nullptr);
            }
            dev->SetDepthStencilSurface(depthNow);
            if (depthNow) depthNow->Release();
            screen->Release();
            saved->Apply();
            saved->Release();
            dev->SetViewport(&vp);
            g_stats.inactive = ok ? nullptr : "a target couldn't be bound";
            if (ok) ++g_stats.draws;
            g_stats.ms = grassperf::Now() - t0;
        }

        void __fastcall hkWeather(void* self, void* edx)
        {
            g_stats.inactive = WhyNot();
            if (!g_stats.inactive)
            {
                if (auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice())) Draw(dev);
            }
            else { g_stats.screen = g_stats.volume = 0.0f; g_stats.ms = 0.0; }
            g_origWeather(self, edx);
        }
    }

    Settings& Config() { return g_settings; }

    void Install(const WXL_Api* api)
    {
        g_api = api;
        // Behind the ground fog's link (-1000: it draws the fog, then calls on) and in front of the window glow's (-900).
        const int ok = api->HookAttachByName("Weather.Render", reinterpret_cast<void*>(&hkWeather), reinterpret_cast<void**>(&g_origWeather), -950) ||
                       api->HookAttachByName("Weather.Render", reinterpret_cast<void*>(&hkWeather), reinterpret_cast<void**>(&g_origWeather), 1000);
        api->Log(ok ? WXL_LOG_INFO : WXL_LOG_WARN, kTag, "sun shafts: weather render hook %s", ok ? "attached" : "missing (the shafts stay off)");
        if (!ok) g_stats.inactive = "no weather render hook";
    }

    void OnDeviceLost() { ReleaseAll(); }

    void DrawPanelSection(const WXL_Api* api)
    {
        if (!api->UiCollapsingHeader("Sun shafts: light through trees, fog and the cloud deck##shafts")) return;
        Settings& s = g_settings;
        api->UiCheckbox("sun shafts##shon", &s.enabled);
        api->UiSliderFloat("strength##shstr", &s.strength, 0.0f, 3.0f);
        static const char* const kQuality[] = { "low (quarter resolution, one pass)", "medium (quarter resolution, smoothed)", "high (half resolution, smoothed)" };
        api->UiCombo("quality##shq", &s.quality, kQuality, 3);
        api->UiSliderInt("samples per pixel toward the sun##shn", &s.samples, 8, 64);
        api->UiSliderFloat("length (share of the way to the sun)##shlen", &s.length, 0.2f, 1.0f);
        api->UiCheckbox("through the cloud deck: its gaps, and shafts under it##shdeck", &s.throughDeck);
        api->UiSliderFloat("moon: faint shafts at night (0 = none)##shmoon", &s.moon, 0.0f, 1.0f);
        int test = s.testAir >= 0.0f ? 1 : 0;
        if (api->UiCheckbox("test: set what's in the air here (instead of the fog)##shta", &test)) s.testAir = test ? 0.8f : -1.0f;
        if (s.testAir >= 0.0f) api->UiSliderFloat("test: in the air (0 clear .. 1 thick fog)##shtav", &s.testAir, 0.0f, 1.0f);

        char line[320];
        if (g_stats.inactive) std::snprintf(line, sizeof(line), "not drawing: %s", g_stats.inactive);
        else std::snprintf(line, sizeof(line), "drawing at %ux%u, %.2f ms CPU (GPU: the profiler's 'ours: sun shafts'); %u frames", g_stats.w, g_stats.h, g_stats.ms, g_stats.draws);
        api->UiText(line);
        const shafts::Strength& m = g_stats.model;
        std::snprintf(line, sizeof(line), "strength %.2f = (sun %.2f [low sun %.2f] + moon %.2f) x air %.2f (in the air %.2f) x weather %.2f x outdoors %.2f",
                      m.total, m.sun, m.golden, m.moon, m.airFactor, m.air, m.weather, m.indoor);
        api->UiText(line);
        // Both directions: where the sky draws the sun (and the moon), and where the light comes from.
        const climate::Sun& now = climate::SunNow();
        auto bearing = [](const float d[3]) { // compass degrees (+x north, +y west)
            const float b = std::atan2(-d[1], d[0]) * 57.29578f;
            return b < 0.0f ? b + 360.0f : b;
        };
        auto up = [](const float d[3]) { return std::asin(std::clamp(d[2], -1.0f, 1.0f)) * 57.29578f; };
        auto at = [](char* out, size_t n, const shafts::SunScreen& s) {
            if (s.inFront) std::snprintf(out, n, "(%.2f, %.2f)", s.u, s.v);
            else std::snprintf(out, n, "behind you");
        };
        char skyAt[32], lightAt[32];
        at(skyAt, sizeof(skyAt), g_stats.skySun);
        at(lightAt, sizeof(lightAt), g_stats.light);
        std::snprintf(line, sizeof(line), "visible sun at %s, %.0f deg up, bearing %.0f; moon %.0f deg up (from %s)",
                      skyAt, up(now.sky), bearing(now.sky), up(now.moon), now.skyFrom);
        api->UiText(line);
        std::snprintf(line, sizeof(line), "light from bearing %.0f, %.0f deg up, at %s on the screen (%s): shades the world, the glass, the deck's in-scatter",
                      bearing(now.dir), up(now.dir), lightAt, now.fromClient ? "the client's" : "the client's rule at the hour");
        api->UiText(line);
        const shafts::SunScreen& sun = g_stats.sun;
        char discAt[32];
        at(discAt, sizeof(discAt), sun);
        const char* centre = now.skyRead ? (g_stats.moonLeads ? "the visible moon" : "the visible sun")
                           : climate::TimeOverride() >= 0.0f ? (g_stats.moonLeads ? "the moon at the override's hour" : "the sun at the override's hour")
                                                             : "the light's direction (standing in for the sky's)";
        std::snprintf(line, sizeof(line), "the centre: %s at %s, fade %.2f x horizon %.2f; screen-space part %.2f, under the deck %.2f",
                      centre, discAt, sun.fade, g_stats.horizon, g_stats.screen, g_stats.volume);
        api->UiText(line);
        std::snprintf(line, sizeof(line), "the cloud deck: %s; light colour %.2f %.2f %.2f", g_stats.deck,
                      g_stats.colour[0], g_stats.colour[1], g_stats.colour[2]);
        api->UiText(line);
    }
}
