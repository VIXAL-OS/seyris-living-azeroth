#include "CoverPrints.hpp"
#include "../debug/MemoryTag.hpp"

#include "SurfaceCover.hpp"
#include "../env/TerrainHeight.hpp"
#include "wxl/PluginApi.h"

#include <windows.h>

#include <cstdio>

namespace wxl_livingazeroth::coverprints
{
    namespace
    {
        constexpr const char* kTag = "wxl-seyris-living-azeroth";

        using PrintDrawFn = void(__cdecl*)();
        PrintDrawFn g_origPrintDraw = nullptr;
        const WXL_Api* g_api = nullptr;

        LiftRun     g_run;
        bool        g_broken = false;          // a fault while lifting: left alone for the session
        unsigned    g_lastLive = 0, g_lastOnCover = 0;
        char        g_status[192] = "not installed";

        // New prints for the sand's rings (features/SurfaceCover), found at every print draw.
        constexpr size_t       kMaxNew = 64;
        std::vector<PrintSeen> g_seen;
        std::vector<NewPrint>  g_new;
        unsigned               g_found = 0;
        bool                   g_findBroken = false; // a fault while reading the pool: no rings for the session

        void FindUnsafe()
        {
            FindNewPrints(reinterpret_cast<const uint8_t*>(kPool), g_seen, [](const NewPrint& p)
            {
                ++g_found;
                if (g_new.size() >= kMaxNew) g_new.erase(g_new.begin()); // nobody took them: keep the latest
                g_new.push_back(p);
            });
        }
        bool Find()
        {
            __try { FindUnsafe(); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool ImageIs12340Unsafe()
        {
            const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
            if (reinterpret_cast<uintptr_t>(base) != kClientImageBase) return false;
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 0x1000) return false;
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.TimeDateStamp == kClientTimestamp &&
                   nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC;
        }

        const Guard* MismatchUnsafe()
        {
            return FirstMismatch([](uintptr_t address, uint32_t) { return reinterpret_cast<const void*>(address); });
        }

        // Which check failed: 0 none, 1 not the 12340 image, 2 a guard (in `guard`), 3 a fault reading.
        int Verify(const Guard*& guard)
        {
            __try
            {
                if (!ImageIs12340Unsafe()) return 1;
                guard = MismatchUnsafe();
                return guard ? 2 : 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return 3;
            }
        }

        void LiftUnsafe()
        {
            LiftPrints(reinterpret_cast<uint8_t*>(kPool),
                       [](float x, float y, float terrainZ) { return cover::DrawnCoverAbove(x, y, terrainZ); },
                       [](float x, float y, float& z) { return terrain::HeightAt(x, y, z); }, g_run);
        }
        bool Lift()
        {
            __try { LiftUnsafe(); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool Restore()
        {
            __try { RestorePrints(reinterpret_cast<uint8_t*>(kPool), g_run); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // Prints on sand the client counts as dirt (CoverPrints.hpp): the footstep's print thunk.
        using StepPrintFn = void(__cdecl*)(uint32_t texture, const float* size, const float* position, float facing,
                                           uint32_t foot, int32_t terrainType, uint32_t activePlayer);
        StepPrintFn g_origStepPrint = nullptr;
        int32_t     g_printingType = -1;      // the TerrainType given on sand (Sand), found at the first need
        unsigned    g_steps = 0, g_stepsOnSand = 0;
        bool        g_stepBroken = false;      // a fault while deciding: the client's own TerrainType from then on
        bool        g_stepFaultLogged = false;
        char        g_stepStatus[192] = "not installed";

        int32_t StepTypeUnsafe(int32_t terrainType, const float* position)
        {
            const auto read = [](uintptr_t address, uint32_t) { return reinterpret_cast<const void*>(address); };
            if (TerrainTypePrints(terrainType, read)) return terrainType; // the client prints there already
            const bool sand = position && cover::SandPrintsAt(position[0], position[1], position[2]);
            if (sand && g_printingType < 0) g_printingType = PrintingTerrainType(read);
            return StepTerrainType(terrainType, false, sand, g_printingType);
        }
        int32_t StepType(int32_t terrainType, const float* position)
        {
            __try { return StepTypeUnsafe(terrainType, position); }
            __except (EXCEPTION_EXECUTE_HANDLER) { g_stepBroken = true; return terrainType; }
        }

        void __cdecl hkStepPrint(uint32_t texture, const float* size, const float* position, float facing, uint32_t foot,
                                 int32_t terrainType, uint32_t activePlayer)
        {
            memuse::Scope memory(memuse::kCover);
            ++g_steps;
            int32_t type = terrainType;
            if (!g_stepBroken && cover::SandPrints()) type = StepType(terrainType, position);
            if (type != terrainType) ++g_stepsOnSand;
            if (g_stepBroken && !g_stepFaultLogged)
            {
                g_stepFaultLogged = true;
                std::snprintf(g_stepStatus, sizeof(g_stepStatus), "stopped: a fault while reading the ground's TerrainType");
                if (g_api) g_api->Log(WXL_LOG_WARN, kTag, "cover prints: a fault while reading the client's TerrainType; sand the client counts as dirt gets no prints from now on.");
            }
            g_origStepPrint(texture, size, position, facing, foot, type, activePlayer);
        }

        const Guard* StepMismatchUnsafe()
        {
            return FirstMismatchIn(kStepGuards, [](uintptr_t address, uint32_t) { return reinterpret_cast<const void*>(address); });
        }
        // 0 all match, 2 a guard differs (in `guard`), 3 a fault reading.
        int VerifyStep(const Guard*& guard)
        {
            __try { guard = StepMismatchUnsafe(); return guard ? 2 : 0; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 3; }
        }

        // The footstep's print thunk, on its own guards: the lift and the rims don't need it.
        void InstallStep(const WXL_Api* api)
        {
            const Guard* guard = nullptr;
            switch (VerifyStep(guard))
            {
                case 2:
                    std::snprintf(g_stepStatus, sizeof(g_stepStatus), "off: the client's %s at 0x%08X differs", guard->what, static_cast<unsigned>(guard->address));
                    api->Log(WXL_LOG_WARN, kTag, "cover prints: the client's %s at 0x%08X differs from the 12340 client; no prints on sand it counts as dirt.",
                             guard->what, static_cast<unsigned>(guard->address));
                    return;
                case 3:
                    std::snprintf(g_stepStatus, sizeof(g_stepStatus), "off: the client's code could not be read");
                    return;
                default: break;
            }
            if (!api->HookAttach("LivingAzeroth.CoverPrints.Step", kStepPrint, reinterpret_cast<void*>(&hkStepPrint),
                                 reinterpret_cast<void**>(&g_origStepPrint), WXL_HOOK_DEFAULT_PRIORITY))
            {
                std::snprintf(g_stepStatus, sizeof(g_stepStatus), "off: the footstep's print hook failed");
                api->Log(WXL_LOG_WARN, kTag, "cover prints: the footstep's print hook failed; no prints on sand the client counts as dirt.");
                return;
            }
            std::snprintf(g_stepStatus, sizeof(g_stepStatus), "on");
        }

        // The client's print draw: lifted onto the cover around it, put back right after.
        void __cdecl hkPrintDraw()
        {
            memuse::Scope memory(memuse::kCover);
            if (!g_findBroken && !Find())
            {
                g_findBroken = true;
                if (g_api) g_api->Log(WXL_LOG_WARN, kTag, "cover prints: a fault while reading the client's footprints; no rings around them in sand from now on.");
            }
            if (g_broken || !cover::PrintsOnCover())
            {
                g_lastLive = g_lastOnCover = 0;
                g_origPrintDraw();
                return;
            }
            const bool lifted = Lift();
            g_origPrintDraw();
            const bool restored = Restore();
            g_lastLive = g_run.live;
            g_lastOnCover = g_run.onCover;
            if (!lifted || !restored)
            {
                g_broken = true;
                std::snprintf(g_status, sizeof(g_status), "stopped: a fault while %s the prints", lifted ? "putting back" : "lifting");
                if (g_api) g_api->Log(WXL_LOG_WARN, kTag, "cover prints: a fault while %s the client's footprints; they stay as the client draws them from now on.",
                                      lifted ? "putting back" : "lifting");
            }
        }
    }

    void Install(const WXL_Api* api)
    {
        g_api = api;
        const Guard* guard = nullptr;
        const int verdict = Verify(guard);
        if (verdict == 1) std::snprintf(g_stepStatus, sizeof(g_stepStatus), "off: not the 3.3.5a 12340 client");
        else if (verdict != 3) InstallStep(api);
        switch (verdict)
        {
            case 1:
                std::snprintf(g_status, sizeof(g_status), "off: not the 3.3.5a 12340 client");
                api->Log(WXL_LOG_WARN, kTag, "cover prints: not the 12340 client; footprints stay under the cover.");
                return;
            case 2:
                std::snprintf(g_status, sizeof(g_status), "off: the client's %s at 0x%08X differs", guard->what, static_cast<unsigned>(guard->address));
                api->Log(WXL_LOG_WARN, kTag, "cover prints: the client's %s at 0x%08X differs from the 12340 client; footprints stay under the cover.",
                         guard->what, static_cast<unsigned>(guard->address));
                return;
            case 3:
                std::snprintf(g_status, sizeof(g_status), "off: the client's code could not be read");
                api->Log(WXL_LOG_WARN, kTag, "cover prints: the client's code could not be read; footprints stay under the cover.");
                return;
            default: break;
        }
        if (!api->HookAttach("LivingAzeroth.CoverPrints", kPrintDraw, reinterpret_cast<void*>(&hkPrintDraw),
                             reinterpret_cast<void**>(&g_origPrintDraw), WXL_HOOK_DEFAULT_PRIORITY))
        {
            std::snprintf(g_status, sizeof(g_status), "off: the footprint draw hook failed");
            api->Log(WXL_LOG_WARN, kTag, "cover prints: the footprint draw hook failed; footprints stay under the cover.");
            return;
        }
        std::snprintf(g_status, sizeof(g_status), "on");
    }

    const char* Status() { return g_status; }
    unsigned    LastLive() { return g_lastLive; }
    unsigned    LastOnCover() { return g_lastOnCover; }
    unsigned    PrintsFound() { return g_found; }
    const char* StepStatus() { return g_stepStatus; }
    unsigned    StepsSeen() { return g_steps; }
    unsigned    StepsOnSand() { return g_stepsOnSand; }

    void TakeNewPrints(std::vector<NewPrint>& out)
    {
        out.insert(out.end(), g_new.begin(), g_new.end());
        g_new.clear();
    }
}
