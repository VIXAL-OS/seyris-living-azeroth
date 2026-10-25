#include "DistantLights.hpp"

#include "../debug/MemoryTag.hpp"
#include "../debug/UiText.hpp"
#include "../env/StormModel.hpp"
#include "DoodadLightTable.hpp"
#include "GrassPerf.hpp"
#include "Windows.hpp"

#include "game/Camera.hpp"
#include "game/Gfx.hpp"
#include "game/Gx.hpp"

#include <windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace wxl_livingazeroth::distantlights
{
    namespace
    {
        namespace gx = wxl::game::gx;
        namespace dl = distantlight;
        constexpr const char* kTag = "wxl-seyris-living-azeroth";

        // Client globals (as the window glow reads them).
        constexpr uintptr_t kCameraInLiquid = 0x00CD8794;
        constexpr uintptr_t kInteriorBlend = 0x00D38B9C;
        constexpr size_t    kMaxWindowsNear = 4096;
        constexpr float     kNearest = 1.0f;      // yd of view depth: nearer isn't drawn
        constexpr float     kPullYards = 0.8f, kPullShare = 0.006f;   // toward the camera: so its own wall doesn't hide a glow

        const WXL_Api*      g_api = nullptr;
        dl::Settings        g_settings;
        Stats               g_stats;
        std::string         g_status;
        std::vector<dl::Source> g_sources;
        std::vector<dl::Sprite> g_sprites;
        // What the last build went by (Update builds again only when one of these changed).
        struct Built
        {
            float eye[3] = { 1e9f, 1e9f, 1e9f };
            float night = -1.0f, range = -1.0f;
            double at = -1e12;
            dl::Settings settings;
            uint32_t windows = 0;
            unsigned farPasses = ~0u;
            int map = -1;
        } g_built;

        using WeatherFn = void(__fastcall*)(void* self, void* edx);
        WeatherFn g_origWeather = nullptr;
        IDirect3DDevice9*      g_device = nullptr;
        IDirect3DPixelShader9* g_ps = nullptr;
        bool                   g_shaderFailed = false;

        // A glow: a bright core and a faint halo round it, in its colour, added over the frame. uv in -1..1 over the quad;
        // c.a = the core's radius in uv.
        const char* const kPsHlsl = R"(
struct In { float2 uv : TEXCOORD0; float4 c : TEXCOORD1; };
float4 main(In i) : COLOR
{
    float r2 = dot(i.uv, i.uv);
    float core = exp(-2.0 * r2 / (i.c.a * i.c.a));
    float h = saturate(1.0 - r2);
    return float4(i.c.rgb * (core + 0.2 * h * h), 0.0);
}
)";

        float Clamp(float v, float lo, float hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : lo; }
        float MaxDistance() { return Clamp(g_settings.maxDistance, 300.0f, 5000.0f); }

        void Build(const float eye[3], uint32_t map, float night, const std::vector<lights::FarLight>& lamps)
        {
            const double t0 = grassperf::Now();
            Stats& st = g_stats;
            st.windowsNear = st.windows = st.lamps = 0;
            g_sources.clear();
            const float nearRange = lights::Config().range > 0.0f ? lights::Config().range : 256.0f;
            const float fadeFrom = nearRange * dl::kFadeFrom;
            const float maxDist = MaxDistance();
            const float strength = Clamp(g_settings.strength, 0.0f, 10.0f);
            const float size = Clamp(g_settings.size, 0.1f, 10.0f);
            auto add = [&](const float pos[3], const float rgb[3], float k, float core, dl::Kind kind)
            {
                const float dx = pos[0] - eye[0], dy = pos[1] - eye[1], dz = pos[2] - eye[2];
                const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (!(d > fadeFrom) || d > maxDist) return false;
                const float w = k * night * strength * dl::NearFade(d, nearRange);
                if (!(w > 1e-3f)) return false;
                dl::Source s{};
                std::memcpy(s.pos, pos, sizeof(s.pos));
                for (int c = 0; c < 3; ++c) s.color[c] = rgb[c] * w;
                s.size = core * size;
                s.kind = kind;
                g_sources.push_back(s);
                return true;
            };
            if (g_settings.windows)
            {
                static std::vector<const windows::Window*> nearby;
                windows::Near(map, eye, maxDist, true, kMaxWindowsNear, nearby);
                st.windowsNear = static_cast<unsigned>(nearby.size());
                for (const windows::Window* w : nearby)
                {
                    // The glass's middle, seen from in front (dimmer at a slant, none from behind).
                    const float mid[3] = { w->pos[0] - w->facing[0] * w->back, w->pos[1] - w->facing[1] * w->back, w->pos[2] - w->facing[2] * w->back };
                    const float e[3] = { eye[0] - mid[0], eye[1] - mid[1], eye[2] - mid[2] };
                    const float d = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
                    if (!(d > fadeFrom)) continue;
                    const float facing = (e[0] * w->facing[0] + e[1] * w->facing[1] + e[2] * w->facing[2]) / d;
                    if (facing <= 0.02f) continue;
                    const float lit = windows::LitShare(*w);
                    if (lit <= 0.002f) continue;
                    const lighttable::Properties* p = lighttable::FindProperties(w->lightId);
                    const uint32_t colour = p ? p->color : 0xFFA757u;
                    const float inten = p ? p->intensity : 0.9f;
                    const float rgb[3] = { ((colour >> 16) & 0xFF) / 255.0f, ((colour >> 8) & 0xFF) / 255.0f, (colour & 0xFF) / 255.0f };
                    const float k = inten * std::clamp(w->strength, 0.25f, 2.0f) * lit * std::min(1.0f, facing * 3.0f);
                    if (add(mid, rgb, k, dl::WindowSize(w->halfWidth, w->halfHeight), dl::kWindow)) ++st.windows;
                }
            }
            const double tWindows = grassperf::Now();
            if (g_settings.lamps)
                for (const lights::FarLight& l : lamps)
                {
                    const float rgb[3] = { l.color[0], l.color[1], l.color[2] };
                    if (add(l.pos, rgb, 1.0f, dl::LampSize(l.radius), dl::kLamp)) ++st.lamps;
                }
            const double tLamps = grassperf::Now();
            st.msWindows = tWindows - t0;
            st.msLamps = tLamps - tWindows;
            dl::Cluster(g_sources, eye, g_sprites);
            st.merged = static_cast<unsigned>(g_sources.size() - g_sprites.size());
            const size_t cap = static_cast<size_t>(std::clamp(g_settings.maxLights, 0, dl::kMaxLightsCap));
            st.capped = g_sprites.size() > cap ? static_cast<unsigned>(g_sprites.size() - cap) : 0u;
            dl::Cap(g_sprites, cap);
            st.sprites = static_cast<unsigned>(g_sprites.size());
            st.msBuild = grassperf::Now() - t0;
            st.msMerge = st.msBuild - st.msWindows - st.msLamps;
        }

        IDirect3DPixelShader9* Compile(IDirect3DDevice9* dev)
        {
            HMODULE m = GetModuleHandleA("d3dcompiler_47.dll");
            if (!m) m = LoadLibraryA("d3dcompiler_47.dll");
            const auto compile = m ? reinterpret_cast<pD3DCompile>(GetProcAddress(m, "D3DCompile")) : nullptr;
            if (!compile) return nullptr;
            ID3DBlob* code = nullptr;
            ID3DBlob* err = nullptr;
            const HRESULT hr = compile(kPsHlsl, std::strlen(kPsHlsl), nullptr, nullptr, nullptr, "main", "ps_2_0", 0, 0, &code, &err);
            if (err)
            {
                if (g_api) g_api->Log(WXL_LOG_WARN, kTag, "distant lights: shader compile: %s", static_cast<const char*>(err->GetBufferPointer()));
                err->Release();
            }
            if (FAILED(hr) || !code) return nullptr;
            IDirect3DPixelShader9* ps = nullptr;
            if (FAILED(dev->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()), &ps))) ps = nullptr;
            code->Release();
            return ps;
        }

        // The client's distance fog this frame: where it ends and where it starts (its end x the start's share), as the
        // colour resolve left them (env/StormModel kFogEnd, kFogStartMul; Sky.FogUpdate reads them after).
        bool ReadFog(float& start, float& end)
        {
            __try
            {
                const float e = *reinterpret_cast<const float*>(storm::kFogEnd);
                const float m = *reinterpret_cast<const float*>(storm::kFogStartMul);
                if (!std::isfinite(e) || !std::isfinite(m) || e < 10.0f || e > 50000.0f || m < -4.0f || m > 1.0f) return false;
                end = e;
                start = e * m;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        struct Vtx { float x, y, z, rhw; float u, v; float r, g, b, core; };
        constexpr DWORD kFvf = D3DFVF_XYZRHW | D3DFVF_TEX2 | D3DFVF_TEXCOORDSIZE2(0) | D3DFVF_TEXCOORDSIZE4(1);

        // The glows on `dev`'s bound target and depth: projected here (the scene's own view and projection), each a quad
        // in pixels round its centre, at its centre's depth pulled toward the camera, faded by the fog and the far clip.
        unsigned DrawSprites(IDirect3DDevice9* dev, const float view[16], const float proj[16], const float cam[3], bool relative, float fogStart, float fogEnd)
        {
            const double t0 = grassperf::Now();
            Stats& st = g_stats;
            st.drawn = 0;
            st.farthest = 0;
            if (dev != g_device) { if (g_ps) g_ps->Release(); g_ps = nullptr; g_shaderFailed = false; g_device = dev; }
            if (!g_ps && !g_shaderFailed) { g_ps = Compile(dev); g_shaderFailed = !g_ps; }
            if (!g_ps) { st.drawWhy = "its shader failed to compile (see the log)"; return 0; }
            D3DVIEWPORT9 vp{};
            dev->GetViewport(&vp);
            if (!vp.Width || !vp.Height || vp.MaxZ <= vp.MinZ) { st.drawWhy = "unexpected viewport"; return 0; }
            // The far clip from the projection (z = m22 + m32 / w: far = m32 / (1 - m22)).
            const float farClip = std::fabs(1.0f - proj[10]) > 1e-6f ? proj[14] / (1.0f - proj[10]) : 0.0f;
            st.farClip = farClip > 10.0f && farClip < 100000.0f ? farClip : 0.0f;
            const float end = st.farClip > 0.0f && fogEnd > 0.0f ? std::min(fogEnd, st.farClip) : fogEnd;
            st.fogStart = fogStart;
            st.fogEnd = end;
            const float pxPerYard = proj[5] * vp.Height * 0.5f;
            const float pierce = Clamp(g_settings.fogPierce, 0.1f, 10.0f);

            static std::vector<Vtx> verts;
            static std::vector<uint16_t> idx;
            verts.clear();
            idx.clear();
            for (const dl::Sprite& s : g_sprites)
            {
                if (verts.size() + 4 > 65535) break;
                const float e[3] = { cam[0] - s.pos[0], cam[1] - s.pos[1], cam[2] - s.pos[2] };
                const float d = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
                const float vis = dl::FogVisibility(d, fogStart, end, pierce) * dl::FarClipFade(d, st.farClip);
                if (!(vis > 0.003f) || !(d > 1.0f)) continue;
                const float pull = std::min(kPullYards + kPullShare * d, d * 0.5f) / d;
                const float p[3] = { s.pos[0] + e[0] * pull - (relative ? cam[0] : 0.0f), s.pos[1] + e[1] * pull - (relative ? cam[1] : 0.0f),
                                     s.pos[2] + e[2] * pull - (relative ? cam[2] : 0.0f) };
                float vv[4], clip[4];
                for (int j = 0; j < 4; ++j) vv[j] = p[0] * view[j] + p[1] * view[4 + j] + p[2] * view[8 + j] + view[12 + j];
                for (int j = 0; j < 4; ++j) clip[j] = vv[0] * proj[j] + vv[1] * proj[4 + j] + vv[2] * proj[8 + j] + vv[3] * proj[12 + j];
                if (!(clip[3] > kNearest)) continue;
                const float z = clip[2] / clip[3];
                if (!(z >= 0.0f && z < 1.0f)) continue;
                const dl::Screen sc = dl::OnScreen(s.size, d, pxPerYard);
                const float px = vp.X + (clip[0] / clip[3] * 0.5f + 0.5f) * vp.Width, py = vp.Y + (0.5f - clip[1] / clip[3] * 0.5f) * vp.Height;
                const float h = sc.halfPixels;
                if (px + h < vp.X || py + h < vp.Y || px - h > vp.X + vp.Width || py - h > vp.Y + vp.Height) continue;
                const float zz = vp.MinZ + z * (vp.MaxZ - vp.MinZ), rhw = 1.0f / clip[3], core = sc.corePixels / h;
                const uint16_t base = static_cast<uint16_t>(verts.size());
                const float cx[4] = { -1.0f, 1.0f, -1.0f, 1.0f }, cy[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
                for (int c = 0; c < 4; ++c)
                    verts.push_back({ px + cx[c] * h - 0.5f, py + cy[c] * h - 0.5f, zz, rhw, cx[c], cy[c], s.color[0] * vis, s.color[1] * vis, s.color[2] * vis, core });
                idx.insert(idx.end(), { base, static_cast<uint16_t>(base + 1), static_cast<uint16_t>(base + 2), static_cast<uint16_t>(base + 2),
                                        static_cast<uint16_t>(base + 1), static_cast<uint16_t>(base + 3) });
                st.farthest = std::max(st.farthest, static_cast<unsigned>(d));
            }
            if (verts.empty()) { st.drawWhy = nullptr; st.msDraw = grassperf::Now() - t0; return 0; }

            IDirect3DStateBlock9* saved = nullptr;
            if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) { st.drawWhy = "no state block"; return 0; }
            dev->SetVertexShader(nullptr);
            dev->SetPixelShader(g_ps);
            dev->SetFVF(kFvf);
            dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
            dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
            dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
            dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
            dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
            dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
            dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x07);
            dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, static_cast<UINT>(verts.size()), static_cast<UINT>(idx.size() / 3), idx.data(), D3DFMT_INDEX16,
                                        verts.data(), sizeof(Vtx));
            saved->Apply();
            saved->Release();
            st.drawn = static_cast<unsigned>(verts.size() / 4);
            st.drawWhy = nullptr;
            st.msDraw = grassperf::Now() - t0;
            return st.drawn;
        }

        const char* DrawWhyNot()
        {
            if (!g_settings.enabled) return "switched off";
            if (g_sprites.empty()) return g_stats.why ? g_stats.why : "nothing to draw";
            if (*reinterpret_cast<const uint32_t*>(kCameraInLiquid)) return "camera under water";
            if (*reinterpret_cast<const float*>(kInteriorBlend) >= 0.999f) return "the camera is indoors";
            if (g_shaderFailed) return "its shader failed to compile (see the log)";
            return nullptr;
        }

        void Draw(IDirect3DDevice9* dev)
        {
            float view[16], proj[16], cam[3];
            if (!wxl::game::gfx::SceneMatrices(view, proj)) { g_stats.drawWhy = "no scene matrices"; return; }
            wxl::game::camera::GetPosition(cam);
            // The scene view is camera-relative when the eye sits at its origin (as the window glow finds it).
            const bool relative = std::fabs(view[12]) + std::fabs(view[13]) + std::fabs(view[14]) < 1.0f;
            float start = 0.0f, end = 0.0f;
            if (!ReadFog(start, end)) { start = 0.0f; end = 0.0f; }
            DrawSprites(dev, view, proj, cam, relative, start, end);
        }

        void __fastcall hkWeather(void* self, void* edx)
        {
            memuse::Scope memory(memuse::kLights);
            const char* why = DrawWhyNot();
            if (!why)
            {
                if (auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice())) Draw(dev);
                else g_stats.drawWhy = "no device";
            }
            else { g_stats.drawWhy = why; g_stats.drawn = 0; g_stats.msDraw = 0.0; }
            g_origWeather(self, edx);
        }
    }

    dl::Settings& Config() { return g_settings; }
    const Stats& GetStats() { return g_stats; }
    const char* PixelShaderSource() { return kPsHlsl; }
    const std::vector<dl::Sprite>& Sprites() { return g_sprites; }

    void Install(const WXL_Api* api)
    {
        g_api = api;
        // After the window glow (-900): its bright pass never sees these.
        const int ok = api->HookAttachByName("Weather.Render", reinterpret_cast<void*>(&hkWeather), reinterpret_cast<void**>(&g_origWeather), -880) ||
                       api->HookAttachByName("Weather.Render", reinterpret_cast<void*>(&hkWeather), reinterpret_cast<void**>(&g_origWeather), 1000);
        api->Log(ok ? WXL_LOG_INFO : WXL_LOG_WARN, kTag, "distant lights: weather render hook %s", ok ? "attached" : "missing (they stay off)");
        if (!ok) g_stats.drawWhy = "no weather render hook";
    }

    void OnDeviceLost()
    {
        if (g_ps) g_ps->Release();
        g_ps = nullptr;
        g_shaderFailed = false;
        g_device = nullptr;
    }

    void Update(const world::Snapshot& snap)
    {
        Stats& st = g_stats;
        auto none = [&](const char* why)
        {
            g_sources.clear();
            g_sprites.clear();
            st.windowsNear = st.windows = st.lamps = st.sprites = st.merged = st.capped = 0;
            st.msBuild = 0.0;
            st.why = why;
        };
        if (!g_settings.enabled) { lights::SetFarRange(0.0f); none("switched off"); return; }
        if (!snap.inWorld || snap.mapId < 0) { lights::SetFarRange(0.0f); none("not in the world"); g_built.at = -1e12; return; }
        lights::SetFarRange(g_settings.lamps ? MaxDistance() : 0.0f);
        st.night = windows::Night();
        if (st.night <= 0.01f) { none("daytime: the client's windows are dark"); g_built.at = -1e12; return; }
        float eye[3] = { snap.playerPos[0], snap.playerPos[1], snap.playerPos[2] };
        wxl::game::camera::GetPosition(eye);
        // Built again only when something it depends on changed (the glows don't move): the camera by 4 yd, the night by 2%,
        // the settings, our lights' range, the window table, the far lamps' pass, the map, or a quarter of a second (the rooms
        // behind the glass going dark); the fog and the far clip are the draw's, every frame.
        const double now = grassperf::Now();
        const float dx = eye[0] - g_built.eye[0], dy = eye[1] - g_built.eye[1], dz = eye[2] - g_built.eye[2];
        const bool same = dx * dx + dy * dy + dz * dz < 16.0f && std::fabs(st.night - g_built.night) < 0.02f && now - g_built.at < 250.0 &&
                          std::memcmp(&g_settings, &g_built.settings, sizeof(g_settings)) == 0 && lights::Config().range == g_built.range &&
                          windows::Generation() == g_built.windows && lights::GetFarStats().passes == g_built.farPasses && snap.mapId == g_built.map;
        if (!same)
        {
            Build(eye, static_cast<uint32_t>(snap.mapId), st.night, lights::FarLights());
            std::memcpy(g_built.eye, eye, sizeof(eye));
            g_built.night = st.night;
            g_built.at = now;
            g_built.settings = g_settings;
            g_built.range = lights::Config().range;
            g_built.windows = windows::Generation();
            g_built.farPasses = lights::GetFarStats().passes;
            g_built.map = snap.mapId;
            ++st.builds;
        }
        const double spent = same ? 0.0 : st.msBuild;
        st.msAverage += (spent - st.msAverage) * (1.0 / 60.0);
        st.why = g_sprites.empty() ? "no lamps or lit windows in the band" : nullptr;
    }

    void BuildForTests(const float eye[3], uint32_t map, float night, const std::vector<lights::FarLight>& lamps)
    {
        g_stats.night = night;
        if (!g_settings.enabled) { g_sources.clear(); g_sprites.clear(); g_stats.sprites = 0; g_stats.why = "switched off"; return; }
        Build(eye, map, night, lamps);
        g_stats.why = g_sprites.empty() ? "no lamps or lit windows in the band" : nullptr;
    }

    unsigned DrawForTests(IDirect3DDevice9* dev, const float view[16], const float proj[16], const float cam[3], float fogStart, float fogEnd)
    {
        if (!g_settings.enabled || g_sprites.empty()) { g_stats.drawn = 0; return 0; }
        const bool relative = std::fabs(view[12]) + std::fabs(view[13]) + std::fabs(view[14]) < 1.0f;
        return DrawSprites(dev, view, proj, cam, relative, fogStart, fogEnd);
    }

    const char* StatusLine()
    {
        const Stats& s = g_stats;
        const lights::FarStats& f = lights::GetFarStats();
        char line[900];
        if (s.why)
            std::snprintf(line, sizeof(line), "distant lights: %s%s (far lamps from the light scan: %u lights from %u of %u models out to %.0f yd)",
                          g_settings.enabled ? "nothing now: " : "", s.why, f.lights, f.withLights, f.models, f.range);
        else
            std::snprintf(line, sizeof(line),
                          "distant lights: on; %u glows (%u windows of %u looked at, %u lamps; %u merged into others, %u over the cap), %u drawn%s%s, the farthest %u yd; "
                          "night %.2f; the client's fog from %.0f to %.0f yd, far clip %.0f yd; far lamps from the light scan: %u lights from %u of %u models out to %.0f yd "
                          "(%u files looked up; that pass %.2f ms in all, %u far passes); cost: building %.3f ms a frame on average (built %u times, the last %.3f ms), "
                          "drawing %.3f ms",
                          s.sprites, s.windows, s.windowsNear, s.lamps, s.merged, s.capped, s.drawn, s.drawWhy ? ", not drawn: " : "", s.drawWhy ? s.drawWhy : "",
                          s.farthest, s.night, s.fogStart, s.fogEnd, s.farClip, f.lights, f.withLights, f.models, f.range, f.files, f.ms, f.passes, s.msAverage, s.builds,
                          s.msBuild, s.msDraw);
        g_status = line;
        return g_status.c_str();
    }

    void SnapshotLines(std::vector<std::string>& out)
    {
        const dl::Settings& c = g_settings;
        char line[400];
        std::snprintf(line, sizeof(line), "distant lights settings: Enabled %d, Strength %.2f, Size %.2f, MaxDistance %.0f yd, MaxLights %d, FogPierce %.2f, Windows %d, Lamps %d",
                      c.enabled, c.strength, c.size, c.maxDistance, c.maxLights, c.fogPierce, c.windows, c.lamps);
        out.emplace_back(line);
        out.emplace_back(StatusLine());
        int n = 0;
        for (const dl::Sprite& s : g_sprites)
        {
            if (n++ >= 6) break;
            std::snprintf(line, sizeof(line), "  %s glow %.0f yd away at %.1f %.1f %.1f: colour %.2f %.2f %.2f, core %.2f yd%s", s.kind == dl::kLamp ? "a lamp's" : "a window's",
                          s.distance, s.pos[0], s.pos[1], s.pos[2], s.color[0], s.color[1], s.color[2], s.size,
                          s.count > 1 ? (" (" + std::to_string(s.count) + " merged)").c_str() : "");
            out.emplace_back(line);
        }
    }

    void DrawPanelSection(const WXL_Api* api)
    {
        if (!api->UiCollapsingHeader("Distant lights: towns' lamps and lit windows glow from afar at night##distant")) return;
        dl::Settings& s = g_settings;
        api->UiCheckbox("distant lights: past our lights' reach, lamps and lit windows glow at night##dl", &s.enabled);
        api->UiCheckbox("distant lights: lit windows##dlw", &s.windows);
        api->UiCheckbox("distant lights: lamps, torches and fires (the light scan's far list)##dll", &s.lamps);
        api->UiSliderFloat("distant lights: strength##dls", &s.strength, 0.0f, 3.0f);
        api->UiSliderFloat("distant lights: size##dlz", &s.size, 0.25f, 3.0f);
        api->UiSliderFloat("distant lights: as far as (yd; never past the far clip)##dld", &s.maxDistance, 300.0f, 3000.0f);
        api->UiSliderInt("distant lights: at most this many glows##dlm", &s.maxLights, 0, dl::kMaxLightsCap);
        api->UiSliderFloat("distant lights: how far into the fog they carry (fog depths)##dlf", &s.fogPierce, 0.25f, 4.0f);
        ui::Text(api, StatusLine());
    }
}
