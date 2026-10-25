// Tree shadows that follow the wind. See TreeShadows.hpp and env/TreeShadowModel.hpp.
#include "TreeShadows.hpp"
#include "../debug/MemoryTag.hpp"

#include "../debug/Pages.hpp"
#include "../debug/Profiler.hpp"
#include "../debug/UiText.hpp"

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace wxl_livingazeroth::treeshadows
{
    namespace
    {
        namespace ts = treeshadow;
        constexpr const char* kTag = "wxl-seyris-living-azeroth";
        constexpr const char* kPanelTitle = "wxl-seyris-living-azeroth: tree shadows";

        using RenderBandsFn = void(__cdecl*)(void* data);
        using FrustumFn = void(__cdecl*)(void* frustum, void* matrix, const void* direction, void* data, int band);
        using QueryFn = int(__cdecl*)(void* data, uint32_t frame);
        using ExtentsFn = void(__thiscall*)(void* frustum, void* box);

        const WXL_Api* g_api = nullptr;
        RenderBandsFn g_origRenderBands = nullptr;
        ts::Settings g_settings;
        ts::State g_state;
        ts::FoliageCache g_foliage;
        bool g_hooked = false;
        int g_treeWind = -1; // -1 not looked yet
        double g_treeWindChecked = 0.0;
        char g_install[220] = "tree shadows: not started";
        char g_logLine[400] = "tree shadows: not started";
        LARGE_INTEGER g_freq = {};

        double NowMs()
        {
            LARGE_INTEGER t;
            QueryPerformanceCounter(&t);
            return g_freq.QuadPart ? static_cast<double>(t.QuadPart) * 1000.0 / static_cast<double>(g_freq.QuadPart) : 0.0;
        }

        // --- reads and calls into the client, each guarded ----------------------------------------------------------
        bool ReadBytes(uintptr_t at, void* out, size_t size)
        {
            __try { std::memcpy(out, reinterpret_cast<const void*>(at), size); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        template <class T> bool Read(uintptr_t at, T& out) { return ReadBytes(at, &out, sizeof(T)); }

        int Quality()
        {
            int32_t recreate = 0, mode = 0;
            if (!Read(ts::kRecreateTextures, recreate) || !Read(ts::kShadowMode, mode)) return -1;
            return recreate ? -1 : mode;
        }

        bool TreeWind() { return g_treeWind == 1; }

        bool StockCallbacks()
        {
            uintptr_t f = 0, q = 0, r = 0;
            return Read(ts::kFrustumCallbackPtr, f) && Read(ts::kQueryCallbackPtr, q) && Read(ts::kRenderCallbackPtr, r) &&
                   f == ts::kFrustumCallback && q == ts::kQueryCallback && r == ts::kRenderCallback;
        }

        bool Viewer(float out[3]) { return ReadBytes(ts::kViewer, out, 12); }

        bool Band(int index, ts::BandView& v)
        {
            const uintptr_t base = ts::kBands + static_cast<uintptr_t>(index) * ts::kBandStride;
            uintptr_t t0 = 0, t1 = 0;
            uint32_t current = 0;
            if (!Read(base + ts::kBandTextures, t0) || !Read(base + ts::kBandTextures + 4, t1) || !Read(base + ts::kBandRadius, v.radius) ||
                !ReadBytes(base + ts::kBandLastPos, v.lastPos, 12) || !Read(base + ts::kBandCurrent, current))
                return false;
            v.textures = t0 && t1;
            v.current = current;
            return true;
        }

        bool Frustum(uint8_t* data, int band)
        {
            __try
            {
                const FrustumFn frustum = *reinterpret_cast<const FrustumFn*>(ts::kFrustumCallbackPtr);
                uint8_t* f = data + ts::kQdFrustums + static_cast<size_t>(band) * ts::kFrustumSize;
                frustum(f, data + ts::kQdMatrices + static_cast<size_t>(band) * ts::kMatrixSize,
                        reinterpret_cast<const void*>(ts::kBands + static_cast<uintptr_t>(band) * ts::kBandStride + ts::kBandDir), data, band);
                reinterpret_cast<ExtentsFn>(ts::kFrustumExtents)(f, data + ts::kQdExtents + static_cast<size_t>(band) * ts::kAaBoxSize);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool Query(uint8_t* data)
        {
            __try
            {
                const QueryFn query = *reinterpret_cast<const QueryFn*>(ts::kQueryCallbackPtr);
                query(data, 0);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        const char* PathOf(uintptr_t model)
        {
            uintptr_t shared = 0;
            if (!Read(model + ts::kModelShared, shared) || !shared) return nullptr;
            char first = 0;
            return Read(shared + ts::kSharedPath, first) && first ? reinterpret_cast<const char*>(shared + ts::kSharedPath) : nullptr;
        }

        bool CountLists(uintptr_t untextured, uint32_t untexturedCount, uintptr_t textured, uint32_t texturedCount, ts::Casters& out)
        {
            __try
            {
                if (untextured) ts::CountBatches(reinterpret_cast<const uint8_t*>(untextured), untexturedCount, &PathOf, g_foliage, out);
                if (textured) ts::CountBatches(reinterpret_cast<const uint8_t*>(textured), texturedCount, &PathOf, g_foliage, out);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool Casters(int band, ts::Casters& out)
        {
            const uintptr_t base = ts::kQueryBuffers + static_cast<uintptr_t>(band) * ts::kBufStride;
            uint32_t groups = 0, untexturedCount = 0, texturedCount = 0;
            uintptr_t untextured = 0, textured = 0;
            if (!Read(base + ts::kBufGroups, groups) || !Read(base + ts::kBufUntexturedBatches, untextured) ||
                !Read(base + ts::kBufUntexturedBatches + 4, untexturedCount) || !Read(base + ts::kBufTexturedBatches, textured) ||
                !Read(base + ts::kBufTexturedBatches + 4, texturedCount))
                return false;
            out.groups = groups;
            out.untextured = untexturedCount;
            out.textured = texturedCount;
            return CountLists(untextured, untexturedCount, textured, texturedCount, out);
        }

        bool Render(uint8_t* data)
        {
            __try { g_origRenderBands(data); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        const ts::Client g_client = {&Quality, &TreeWind, &StockCallbacks, &Viewer, &Band, &Frustum, &Query, &Casters, &Render, &NowMs};

        // The client's own bands just drawn: ours after them, still inside CShadowCache::Update's PCF state.
        void __cdecl hkRenderBands(void* data)
        {
            memuse::Scope memory(memuse::kTreeShadows);
            g_origRenderBands(data);
            if (!data) return;
            prof::Scope scope(prof::kOursTreeShadows);
            ts::RunFrame(g_client, static_cast<uint8_t*>(data), g_settings, g_state);
        }

        // --- text ------------------------------------------------------------------------------------------------------
        const char* QualityText(int q)
        {
            switch (q)
            {
            case 0: return "0: no shadows from models at all";
            case 1:
            case 2: return "1-2: only units' shadows, in one map round you drawn every frame; no tree shadows to follow the wind";
            case 3:
            case 4: return "3-4: the client caches the trees' shadows and draws them again only as you walk on (every 2 yards near you, "
                           "4 farther out); the near map is drawn again here so they follow the wind";
            case 5: return "5: the client draws every shadow map again every frame itself, trees included (the dearest setting): "
                           "nothing to do";
            default: return q < 0 ? "the client hasn't made its shadow maps yet" : "unknown";
            }
        }

        std::string BandLine(int b)
        {
            const ts::BandStats& s = g_state.second[b];
            char line[420];
            const char* name = b ? "Far trees (40 to 160 yards)" : "Near trees (40 yards round you)";
            if (b && !g_settings.farTrees)
            {
                std::snprintf(line, sizeof(line), "%s: off", name);
                return line;
            }
            const unsigned passes = s.drawn + s.looked;
            const double each = passes ? s.ms / static_cast<double>(passes) : 0.0;
            std::snprintf(line, sizeof(line), "%s: %s. Last second: %u redraws in %u frames, %.2f ms each (CPU, the client's draws included; "
                                              "at most %.2f), %u looks found no tree or bush",
                          name, ts::OutcomeText(g_state.outcome[b]), s.drawn, g_state.secondFrames, each, s.maxMs, s.looked);
            return line;
        }

        std::string CastersLine(int b)
        {
            const ts::Casters& c = g_state.second[b].last;
            char line[300];
            std::snprintf(line, sizeof(line), "  last look: %u model batches of %u models, %u of them trees or bushes%s%s%s; %u building groups",
                          c.untextured + c.textured, c.models, c.foliage, c.foliage ? " (" : "", c.foliage ? c.firstFoliage : "",
                          c.foliage ? ", ...)" : "", c.groups);
            return line;
        }

        void __cdecl Panel(void* /*user*/)
        {
            if (!g_api) return;
            g_api->UiCheckbox("Tree shadows follow the wind (the trees within 40 yards)", &g_settings.enabled);
            g_api->UiSliderFloat("Their redraws per second, at most (0 = every frame)##treeshadows", &g_settings.rate, 0.0f, 60.0f);
            g_api->UiCheckbox("The trees 40 to 160 yards away too (costs more)", &g_settings.farTrees);
            g_api->UiSliderFloat("Their redraws per second, at most (0 = every frame)##treeshadowsfar", &g_settings.farRate, 0.0f, 30.0f);
            std::vector<std::string> lines;
            StatusLines(lines);
            for (const std::string& l : lines) ui::Text(g_api, l.c_str());
        }
    }

    treeshadow::Settings& Config() { return g_settings; }

    void Install(const WXL_Api* api)
    {
        g_api = api;
        QueryPerformanceFrequency(&g_freq);
        pages::AddPanel(kPanelTitle, &Panel);
        if (!api || !api->HookAttach)
        {
            std::snprintf(g_install, sizeof(g_install), "tree shadows: off (no hook interface)");
            return;
        }
        // Before any detour is armed (the core arms them after every WXL_Load): the code as the 12340 client has it.
        const ts::Guard* bad = ts::FirstMismatch([](uintptr_t address, uint32_t size) -> const void* {
            uint8_t probe = 0;
            return size && ReadBytes(address, &probe, 1) && ReadBytes(address + size - 1, &probe, 1) ? reinterpret_cast<const void*>(address) : nullptr;
        });
        if (bad)
        {
            std::snprintf(g_install, sizeof(g_install), "tree shadows: off (the client's code differs: %s at 0x%08X)", bad->what,
                          static_cast<unsigned>(bad->address));
            api->Log(WXL_LOG_WARN, kTag, "%s", g_install);
            return;
        }
        g_hooked = api->HookAttach("living-azeroth: tree shadows (CShadowCache::RenderBands)", ts::kRenderBands,
                                   reinterpret_cast<void*>(&hkRenderBands), reinterpret_cast<void**>(&g_origRenderBands),
                                   WXL_HOOK_DEFAULT_PRIORITY) != 0;
        std::snprintf(g_install, sizeof(g_install), g_hooked ? "tree shadows: hooked CShadowCache::RenderBands; the client's code as expected (%u checks)"
                                                             : "tree shadows: off (the hook on CShadowCache::RenderBands failed; %u checks passed)",
                      static_cast<unsigned>(sizeof(ts::kGuards) / sizeof(ts::kGuards[0])));
        api->Log(g_hooked ? WXL_LOG_INFO : WXL_LOG_WARN, kTag, "%s", g_install);
    }

    void Tick()
    {
        const double now = NowMs();
        // wxl-treewind loads after this module (name order): looked for once the world runs, again each few seconds while absent.
        if (g_treeWind != 1 && now - g_treeWindChecked > 3000.0)
        {
            g_treeWindChecked = now;
            g_treeWind = GetModuleHandleA("wxl-treewind.dll") ? 1 : 0;
        }
        ts::Tick(g_state, now);
    }

    const char* LogLine()
    {
        ts::BandStats (&s)[2] = g_state.sinceLog;
        char farText[160] = "far off";
        if (g_settings.farTrees)
            std::snprintf(farText, sizeof(farText), "far %u redraws (%.2f ms avg), %u looks without trees", s[1].drawn,
                          s[1].drawn + s[1].looked ? s[1].ms / (s[1].drawn + s[1].looked) : 0.0, s[1].looked);
        std::snprintf(g_logLine, sizeof(g_logLine), "tree shadows: %s, quality %d, TreeWind %s; near %u redraws (%.2f ms avg, %.2f max), "
                                                    "%u looks without trees, now %s; %s",
                      !g_hooked ? "not hooked" : g_settings.enabled ? "on" : "off", Quality(),
                      g_treeWind == 1 ? "loaded" : g_treeWind == 0 ? "absent" : "not looked for", s[0].drawn,
                      s[0].drawn + s[0].looked ? s[0].ms / (s[0].drawn + s[0].looked) : 0.0, s[0].maxMs, s[0].looked,
                      ts::OutcomeText(g_state.outcome[0]), farText);
        s[0] = ts::BandStats{};
        s[1] = ts::BandStats{};
        return g_logLine;
    }

    void StatusLines(std::vector<std::string>& out)
    {
        out.push_back(g_install);
        if (!g_hooked) return;
        const int q = Quality();
        out.push_back(std::string("Shadow quality (extShadowQuality) ") + QualityText(q));
        out.push_back(g_treeWind == 1 ? "wxl-treewind: loaded" : g_treeWind == 0 ? "wxl-treewind: not loaded (nothing sways; nothing to do)"
                                                                                  : "wxl-treewind: not looked for yet");
        if (q < 3 || q >= 5) return;
        out.push_back(BandLine(0));
        out.push_back(CastersLine(0));
        out.push_back(BandLine(1));
        if (g_settings.farTrees) out.push_back(CastersLine(1));
    }
}
