#include "Compare.hpp"

#include "ChatLine.hpp"
#include "KeyWatch.hpp"
#include "UiText.hpp"
#include "../env/Lights.hpp"
#include "../env/Storm.hpp"
#include "../features/Crystal.hpp"
#include "../features/DistantLights.hpp"
#include "../features/DoodadFade.hpp"
#include "../features/FogWake.hpp"
#include "../features/Glass.hpp"
#include "../features/GrassInstanced.hpp"
#include "../features/GrassMotion.hpp"
#include "../features/GroundFog.hpp"
#include "../features/Lantern.hpp"
#include "../features/ModelIce.hpp"
#include "../features/PlayerDepth.hpp"
#include "../features/SeeThrough.hpp"
#include "../features/SpellLights.hpp"
#include "../features/SunShafts.hpp"
#include "../features/SurfaceCover.hpp"
#include "../features/TerrainIce.hpp"
#include "../features/TerrainWetness.hpp"
#include "../features/TreeShadows.hpp"
#include "../features/WetSurfaces.hpp"
#include "../features/Windows.hpp"
#include "../features/WmoIce.hpp"
#include "../render/PostAA.hpp"

#include "engine/events/Event.hpp"

#include <windows.h>

#include <cstdio>
#include <string>

namespace wxl_livingazeroth::compare
{
    namespace
    {
        namespace ev = wxl::events;
        constexpr const char* kTag = "wxl-seyris-living-azeroth";

        float I(int v) { return static_cast<float>(v); }
        int   N(float v) { return static_cast<int>(v < 0.0f ? v - 0.5f : v + 0.5f); }

        // Every visual effect's own switch (the one its panel sets), and the value that turns it off. Our point lights carry
        // the window lights, spell lights, the lantern, strike lights, the lights in the fog and on models: off, env/Lights
        // clears its lists and grid and the client's own lights come back on the terrain and models. The instanced grass
        // renderer stays (it draws the client's plants alike); its denser grass and thinning go. Darker nights act with
        // storms off, so they're a switch of their own; the waterfall hats' fade too (not gated by the model ice).
        const Switch kSwitches[] = {
            { "ground fog", [] { return I(groundfog::Config().enabled); }, [](float v) { groundfog::Config().enabled = N(v); }, 0.0f },
            { "fog wake", [] { return I(fogwake::Config().enabled); }, [](float v) { fogwake::Config().enabled = N(v); }, 0.0f },
            { "storms", [] { return I(storm::Config().enabled); }, [](float v) { storm::Config().enabled = N(v); }, 0.0f },
            { "darker nights", [] { return storm::Config().nightDarkness; }, [](float v) { storm::Config().nightDarkness = v; }, 0.0f },
            { "our point lights", [] { return I(lights::Config().enabled); }, [](float v) { lights::Config().enabled = N(v); }, 0.0f },
            { "window lights", [] { return I(windows::Config().lights); }, [](float v) { windows::Config().lights = N(v); }, 0.0f },
            { "window glow", [] { return I(windows::Config().glow); }, [](float v) { windows::Config().glow = N(v); }, 0.0f },
            { "spell lights", [] { return I(spelllights::Config().enabled); }, [](float v) { spelllights::Config().enabled = N(v); }, 0.0f },
            { "lantern", [] { return I(lantern::Config().enabled); }, [](float v) { lantern::Config().enabled = N(v); }, 0.0f },
            { "distant lights", [] { return I(distantlights::Config().enabled); }, [](float v) { distantlights::Config().enabled = N(v); }, 0.0f },
            { "sun shafts", [] { return I(sunshafts::Config().enabled); }, [](float v) { sunshafts::Config().enabled = N(v); }, 0.0f },
            { "glass", [] { return I(glass::Config().enabled); }, [](float v) { glass::Config().enabled = N(v); }, 0.0f },
            { "see-through windows", [] { return I(seethrough::Config().enabled); }, [](float v) { seethrough::Config().enabled = N(v); }, 0.0f },
            { "see-through from inside", [] { return I(seethrough::Config().fromInside); }, [](float v) { seethrough::Config().fromInside = N(v); }, 0.0f },
            { "crystals", [] { return I(crystal::Config().enabled); }, [](float v) { crystal::Config().enabled = N(v); }, 0.0f },
            { "clear ice on the ground", [] { return I(terrainice::Enabled()); }, [](float v) { terrainice::SetEnabled(N(v) != 0); }, 0.0f },
            { "clear ice on models", [] { return I(modelice::Enabled()); }, [](float v) { modelice::SetEnabled(N(v) != 0); }, 0.0f },
            { "clear ice in buildings", [] { return I(wmoice::Enabled()); }, [](float v) { wmoice::SetEnabled(N(v) != 0); }, 0.0f },
            { "waterfall hats fading in", [] { return I(modelice::FadeHats()); }, [](float v) { modelice::SetFadeHats(N(v) != 0); }, 0.0f },
            { "waterfall hats hidden", [] { return I(modelice::HideHats()); }, [](float v) { modelice::SetHideHats(N(v) != 0); }, 0.0f },
            { "wet ground", [] { return I(terrainwet::Enabled()); }, [](float v) { terrainwet::SetEnabled(N(v) != 0); }, 0.0f },
            { "wet surfaces", [] { return I(wetsurfaces::Config().enabled); }, [](float v) { wetsurfaces::Config().enabled = N(v); }, 0.0f },
            { "surface cover", [] { return I(cover::Enabled()); },
              [](float v) { cover::Configure(N(v) != 0, cover::Levels(), cover::Trenches(), cover::DepthMultiplier(), cover::FillBudgetMs()); }, 0.0f },
            { "tree shadows", [] { return I(treeshadows::Config().enabled); }, [](float v) { treeshadows::Config().enabled = N(v); }, 0.0f },
            { "grass motion", [] { return I(grass::Tunables().enabled); }, [](float v) { grass::Tunables().enabled = N(v) != 0; }, 0.0f },
            { "denser grass", [] { return grassinst::Tunables().maxMultiplier; }, [](float v) { grassinst::Tunables().maxMultiplier = v; }, 1.0f },
            { "far grass thinned", [] { return I(grassinst::Tunables().thinEnabled); }, [](float v) { grassinst::Tunables().thinEnabled = N(v) != 0; }, 0.0f },
            { "small doodads' view distance", [] { return I(doodadfade::Config().smallUseNext); }, [](float v) { doodadfade::Config().smallUseNext = N(v); }, 0.0f },
            { "doodads popping in under the fog", [] { return I(doodadfade::Config().fadeFrom); }, [](float v) { doodadfade::Config().fadeFrom = N(v); }, 0.0f },
            { "doodads dissolving in", [] { return I(doodadfade::Config().dissolve); }, [](float v) { doodadfade::Config().dissolve = N(v); }, 0.0f },
            { "anti-aliasing", [] { return I(static_cast<int>(postaa::Tunables().mode)); }, [](float v) { postaa::Tunables().mode = static_cast<postaa::Mode>(N(v)); }, 0.0f },
            { "mount wings writing depth", [] { return I(playerdepth::Config().enabled); }, [](float v) { playerdepth::Config().enabled = N(v); }, 0.0f },
        };
        constexpr size_t kCount = sizeof(kSwitches) / sizeof(kSwitches[0]);

        const WXL_Api* g_api = nullptr;
        Settings       g_settings;
        keys::KeyWatch g_watch;
        bool           g_off = false;
        float          g_saved[kCount] = {};
        unsigned       g_switched = 0, g_restored = 0, g_kept = 0, g_presses = 0;
        char           g_since[16] = "";
        std::string    g_by = "", g_status;

        void __cdecl OnInput(void* /*user*/, const void* args)
        {
            const auto* a = static_cast<const ev::InputArgs*>(args);
            if (a) g_watch.Message(g_settings.key, a->message, a->wparam);
        }

        std::string KeyName() { return keys::KeyName(g_settings.key, "no key: CompareKey 0"); }
    }

    Settings& Config() { return g_settings; }
    bool EffectsOff() { return g_off; }

    const Switch* Switches(size_t& count)
    {
        count = kCount;
        return kSwitches;
    }

    void Install(const WXL_Api* api)
    {
        g_api = api;
        if (api) api->Subscribe(static_cast<uint32_t>(ev::Event::OnInput), &OnInput, nullptr);
    }

    void Toggle(const char* by)
    {
        ++g_presses;
        g_by = by ? by : "?";
        if (!g_off)
        {
            g_switched = 0;
            for (size_t i = 0; i < kCount; ++i)
            {
                g_saved[i] = kSwitches[i].get();
                if (g_saved[i] != kSwitches[i].off) ++g_switched;
                kSwitches[i].set(kSwitches[i].off);
            }
            g_off = true;
            SYSTEMTIME now{};
            GetLocalTime(&now);
            std::snprintf(g_since, sizeof(g_since), "%02u:%02u:%02u", now.wHour, now.wMinute, now.wSecond);
            chat::Say("all effects OFF: the client's own look (" + KeyName() + " turns them back on).");
            if (g_api) g_api->Log(WXL_LOG_INFO, kTag, "compare: all effects off by %s (%u of %zu switches were on)", g_by.c_str(), g_switched, kCount);
            return;
        }
        // Back as they were, except a switch someone changed in a panel meanwhile (it no longer holds the off value).
        g_restored = g_kept = 0;
        for (size_t i = 0; i < kCount; ++i)
        {
            if (kSwitches[i].get() != kSwitches[i].off) { ++g_kept; continue; }
            kSwitches[i].set(g_saved[i]);
            ++g_restored;
        }
        g_off = false;
        chat::Say("all effects back ON" + (g_kept ? std::string(" (") + std::to_string(g_kept) + " changed in a panel meanwhile kept as set)." : std::string(".")));
        if (g_api) g_api->Log(WXL_LOG_INFO, kTag, "compare: all effects back on by %s (%u put back, %u changed in a panel meanwhile kept)", g_by.c_str(), g_restored, g_kept);
    }

    void Update()
    {
        g_watch.Poll(g_settings.key);
        if (g_watch.Take()) Toggle("the key");
    }

    const char* StatusLine()
    {
        char line[400];
        if (g_off)
            std::snprintf(line, sizeof(line), "compare: all effects OFF since %s (by %s; %u of %zu switches were on); %s turns them back on", g_since, g_by.c_str(), g_switched,
                          kCount, KeyName().c_str());
        else if (g_presses)
            std::snprintf(line, sizeof(line), "compare: effects ON (back on by %s: %u put back, %u changed in a panel meanwhile kept); %s switches them all off", g_by.c_str(),
                          g_restored, g_kept, KeyName().c_str());
        else
            std::snprintf(line, sizeof(line), "compare: effects ON (as set); %s switches them all off for a comparison", KeyName().c_str());
        g_status = line;
        return g_status.c_str();
    }

    void SnapshotLines(std::vector<std::string>& out)
    {
        out.emplace_back(StatusLine());
        if (!g_off) return;
        std::string on = "  off for the comparison (were on): ";
        bool any = false;
        for (size_t i = 0; i < kCount; ++i)
            if (g_saved[i] != kSwitches[i].off) { on += (any ? ", " : "") + std::string(kSwitches[i].name); any = true; }
        out.emplace_back(any ? on : on + "none");
    }

    void DrawButton(const WXL_Api* api, const char* id)
    {
        char label[160];
        std::snprintf(label, sizeof(label), "%s (the key: %s)%s", g_off ? "All effects back on" : "All effects off, for a comparison", KeyName().c_str(), id ? id : "");
        if (api->UiButton(label)) Toggle("the panel's button");
        ui::Text(api, StatusLine());
    }

    void ResetForTests()
    {
        g_off = false;
        g_switched = g_restored = g_kept = g_presses = 0;
        g_since[0] = 0;
        g_by.clear();
        g_watch = keys::KeyWatch{};
    }
}
