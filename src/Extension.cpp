// wxl-seyris-living-azeroth -- client-side, purely cosmetic features that make the world feel more
// alive: weather and wind, surfaces, water, fog, clouds, lighting, characters reacting to the
// world. Standalone WXL v1.1 extension, sibling to wxl-seyris-tools/wxl-seyris-dehardcoding.
//
// See orchestration/docs/r&d/immersion/ for the planning (README.md is the index).
//
// This file only owns the WXL_Query/WXL_Load entry points and the event wiring. Hooks must be
// attached from WXL_Load (core arms the whole detour batch once, right after every extension's
// WXL_Load returns).

#include "wxl/PluginApi.h"

#include "engine/events/Event.hpp"

#include "Config.hpp"

#include "debug/ClimateView.hpp"
#include "debug/Compare.hpp"
#include "debug/DepthView.hpp"
#include "debug/LightEditor.hpp"
#include "debug/LightsView.hpp"
#include "debug/Memory.hpp"
#include "debug/MemoryTag.hpp"
#include "debug/Profiler.hpp"
#include "debug/StormView.hpp"
#include "debug/SurfaceView.hpp"
#include "debug/WindView.hpp"
#include "debug/WmoBindProbe.hpp"
#include "debug/FogProbe.hpp"
#include "debug/Overview.hpp"
#include "debug/Pages.hpp"
#include "debug/Snapshot.hpp"
#include "env/ClientWeather.hpp"
#include "env/Fog.hpp"
#include "env/FogSettings.hpp"
#include "features/FogWake.hpp"
#include "features/GroundFog.hpp"
#include "debug/FogView.hpp"
#include "env/Actors.hpp"
#include "env/Climate.hpp"
#include "env/Fields.hpp"
#include "env/Lights.hpp"
#include "env/Regional.hpp"
#include "env/Storm.hpp"
#include "env/ThunderDsp.hpp"
#include "env/Wind.hpp"
#include "env/WorldQuery.hpp"
#include "features/Crystal.hpp"
#include "features/DistantLights.hpp"
#include "features/DoodadFade.hpp"
#include "features/DoodadLightTable.hpp"
#include "features/GrassDensity.hpp"
#include "features/GrassDistanceCap.hpp"
#include "features/GrassDoodads.hpp"
#include "features/GrassInstanced.hpp"
#include "features/GrassMotion.hpp"
#include "features/GrassPerf.hpp"
#include "features/Lantern.hpp"
#include "features/ModelLights.hpp"
#include "features/SurfaceCover.hpp"
#include "features/ModelIce.hpp"
#include "features/PlayerDepth.hpp"
#include "features/SunShafts.hpp"
#include "features/TerrainIce.hpp"
#include "features/TerrainWetness.hpp"
#include "features/TreeShadows.hpp"
#include "features/Glass.hpp"
#include "features/GlassShafts.hpp"
#include "features/SeeThrough.hpp"
#include "features/SpellLights.hpp"
#include "features/WetSurfaces.hpp"
#include "features/WindowGlow.hpp"
#include "features/Windows.hpp"
#include "features/WmoIce.hpp"
#include "lua/EnvironmentLua.hpp"
#include "lua/LuaApi.hpp"
#include "render/M2Effects.hpp"
#include "render/PostAA.hpp"
#include "render/SceneDepth.hpp"
#include "render/ShaderPatch.hpp"
#include "wxl_seyris/CdbcApi.hpp"
#include "RainSignal.hpp"
#include <chrono>
#include <cmath>
#include <cstring>

namespace
{
    namespace ev = wxl::events;

    const WXL_PluginInfo kInfo = {
        sizeof(WXL_PluginInfo),
        WXL_API_VERSION,
        "wxl-seyris-living-azeroth",
        1, // pluginVersion: opaque to the core, bump manually as this module grows
        WXL_CLIENT_BUILD,
    };

    const WXL_Api* g_api = nullptr;

    float __cdecl RainIntensity()
    {
        const auto& s = wxl_livingazeroth::world::Current();
        if (!s.inWorld || !s.outdoors || s.weatherType != 1 || !std::isfinite(s.weatherIntensity)) return 0.0f;
        return s.weatherIntensity < 0.0f ? 0.0f : (s.weatherIntensity > 1.0f ? 1.0f : s.weatherIntensity);
    }
    WxlRainSignalV1 g_rainSignal{sizeof(WxlRainSignalV1), &RainIntensity};

    // Resolved on the first frame, never in WXL_Load: extensions load alphabetically on one thread,
    // so GetInterface from WXL_Load can see a false NULL for a module that loads later.
    void ResolveInterfacesOnce()
    {
        static bool done = false;
        if (done) return;
        done = true;

        // The panels: as pages of wxl-seyris-tools' control panel, or plain overlay panels without it.
        {
            wxl_livingazeroth::memuse::Scope memory(wxl_livingazeroth::memuse::kPanels);
            wxl_livingazeroth::pages::Publish(g_api);
        }

        // The tables (and what the features keep of them) are charged to the memory readout's "cdbc tables".
        {
            wxl_livingazeroth::memuse::Scope tables(wxl_livingazeroth::memuse::kTables);
            const auto* cdbc = static_cast<const WXL_SeyrisCdbcApi*>(
                g_api->GetInterface(WXL_SEYRIS_CDBC_INTERFACE_NAME, WXL_SEYRIS_CDBC_INTERFACE_VERSION));
            wxl_livingazeroth::debug::SetWindCdbc(cdbc);
            wxl_livingazeroth::wind::LoadProfiles(cdbc);
            wxl_livingazeroth::grassdoodads::LoadOverrides(cdbc);
            wxl_livingazeroth::grassdensity::Load(cdbc);
            wxl_livingazeroth::cover::LoadTable(cdbc);
            wxl_livingazeroth::climate::Load(cdbc);
            wxl_livingazeroth::fogsettings::Load(cdbc);
            wxl_livingazeroth::debug::SetClimateCdbc(cdbc);
            wxl_livingazeroth::lighttable::Load(cdbc);
            wxl_livingazeroth::windows::Load(cdbc);
            wxl_livingazeroth::glass::Load(cdbc);
            wxl_livingazeroth::glass::LogLoad(g_api);
            wxl_livingazeroth::crystal::Load(cdbc);
            wxl_livingazeroth::debug::SetLightsCdbc(cdbc);
            wxl_livingazeroth::debug::SetLightEditorCdbc(cdbc);
        }

        wxl_livingazeroth::config::Apply(g_api);
        wxl_livingazeroth::grassdistance::OnFirstFrame();
    }

    // Every 30 s in the world, one log line for diagnosing a report afterwards (the log is all a session leaves
    // behind): where the player is (the zone and sub-zone too, to line the external memory log up with), what the
    // client says they stand on, and what the ice features touched; then the memory readout's line (the core's log
    // lines hold 1,000 characters, so it goes on its own).
    void LogStatusWhenDue()
    {
        static auto next = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (now < next) return;
        next = now + std::chrono::seconds(30);
        const auto& s = wxl_livingazeroth::world::Current();
        if (!s.inWorld) return;
        wxl_livingazeroth::memuse::Scope memory(wxl_livingazeroth::memuse::kPanels);
        const char* zone = s.zoneText && s.zoneText[0] ? s.zoneText : "(no zone)";
        const bool sub = s.subZoneText && s.subZoneText[0] && std::strcmp(s.subZoneText, zone) != 0;
        g_api->Log(WXL_LOG_INFO, "wxl-seyris-living-azeroth", "status: in %s%s%s (area %u), map %d at %.1f, %.1f, %.1f, terrain type %d, %s; %s; %s; %s; %s; %s; %s; %s; %s",
                   zone, sub ? ", " : "", sub ? s.subZoneText : "", s.areaCount > 0 ? s.areaChain[0] : 0u,
                   s.mapId, s.playerPos[0], s.playerPos[1], s.playerPos[2], s.playerTerrainType, s.outdoors ? "outdoors" : "indoors",
                   wxl_livingazeroth::terrainice::StatusLine(), wxl_livingazeroth::modelice::StatusLine(),
                   wxl_livingazeroth::wmoice::StatusLine(), wxl_livingazeroth::glass::StatusLine(), wxl_livingazeroth::crystal::StatusLine(),
                   wxl_livingazeroth::seethrough::StatusLine(), wxl_livingazeroth::treeshadows::LogLine(), wxl_livingazeroth::spelllights::StatusLine());
        // Wet surfaces on a line of their own (the line above is near the core's 1,000 characters).
        g_api->Log(WXL_LOG_INFO, "wxl-seyris-living-azeroth", "status: %s", wxl_livingazeroth::wetsurfaces::StatusLine());
        g_api->Log(WXL_LOG_INFO, "wxl-seyris-living-azeroth", "status: in %s%s%s: %s", zone, sub ? ", " : "", sub ? s.subZoneText : "",
                   wxl_livingazeroth::memory::LogPart());
        // The lantern, the distant lights and the compare key (2026-10-25), on a line of their own (the core's lines hold 1,000
        // characters).
        g_api->Log(WXL_LOG_INFO, "wxl-seyris-living-azeroth", "status: %s", wxl_livingazeroth::compare::StatusLine());
        g_api->Log(WXL_LOG_INFO, "wxl-seyris-living-azeroth", "status: %s", wxl_livingazeroth::lantern::StatusLine());
        g_api->Log(WXL_LOG_INFO, "wxl-seyris-living-azeroth", "status: %s", wxl_livingazeroth::distantlights::StatusLine());
    }

    // Between frames: the safe moment to swap the depth surface the next world pass will bind.
    void __cdecl OnFrame(void* /*user*/, const void* /*args*/)
    {
        namespace mem = wxl_livingazeroth::memuse;
        ResolveInterfacesOnce();
        {
            mem::Scope memory(mem::kGrass);
            wxl_livingazeroth::grassperf::OnFrameEnd();
            wxl_livingazeroth::grassinst::OnFrameEnd();
        }
        {
            mem::Scope memory(mem::kRender);
            wxl_livingazeroth::depth::Update();
        }
        {
            mem::Scope memory(mem::kLights);
            wxl_livingazeroth::modellights::EndFrame();
        }
        LogStatusWhenDue();
    }

    // Once per frame, main thread: refresh the world snapshot, then everything that reads it.
    void __cdecl OnUpdate(void* /*user*/, const void* args)
    {
        const auto* a = static_cast<const ev::UpdateArgs*>(args);
        const float dt = a ? a->dt : 0.0f;
        namespace prof = wxl_livingazeroth::prof;
        namespace mem = wxl_livingazeroth::memuse;
        mem::Scope envMemory(mem::kEnv);   // what the world snapshot and the client's weather allocate
        {
            // The compare key first: the switches it flips then hold for this whole frame.
            mem::Scope memory(mem::kPanels);
            wxl_livingazeroth::compare::Update();
        }
        wxl_livingazeroth::clientweather::Apply();
        wxl_livingazeroth::doodadfade::Update();
        const auto& snap = wxl_livingazeroth::world::Refresh();
        {
            mem::Scope memory(mem::kRender);
            wxl_livingazeroth::playerdepth::Update(snap.inWorld);
        }
        {
            mem::Scope memory(mem::kTreeShadows);
            wxl_livingazeroth::treeshadows::Tick();
        }
        {
            prof::Scope scope(prof::kOursEnv);
            wxl_livingazeroth::climate::Update(snap);
            wxl_livingazeroth::storm::Update(dt, snap); // before the wind, the fog and the lights it feeds
            wxl_livingazeroth::thunderdsp::Update(dt);  // the thunder filters' chains: sounds counted, quiet ones off
            wxl_livingazeroth::regional::Update(dt, snap);
            wxl_livingazeroth::wind::Update(dt, snap);
            if (snap.inWorld)
                wxl_livingazeroth::actors::Refresh(snap.playerPos, wxl_livingazeroth::grass::kActorRange);
            wxl_livingazeroth::wetsurfaces::Update(dt, snap); // the zones' surface water, which the fields' step reads
            wxl_livingazeroth::fields::Update(dt, snap);
        }
        {
            prof::Scope scope(prof::kOursFogField);
            wxl_livingazeroth::fog::Update(dt, snap);
            wxl_livingazeroth::fogwake::Update(dt, snap);
        }
        {
            prof::Scope scope(prof::kOursLights);
            {
                mem::Scope memory(mem::kWindows);
                wxl_livingazeroth::windows::Update(); // the night factor the window lights and glow follow
            }
            {
                mem::Scope memory(mem::kGlass);
                wxl_livingazeroth::glass::Update();   // the light through the glass, the panes near the camera
            }
            {
                mem::Scope memory(mem::kSeeThrough);
                wxl_livingazeroth::seethrough::Update(); // last frame's windows opened and glass redrawn; its leftovers go
            }
            {
                mem::Scope memory(mem::kIce);
                wxl_livingazeroth::crystal::Update(); // the night the crystals' inner glow goes by
            }
            wxl_livingazeroth::lantern::Update(dt, snap);   // its key and rules: the light env/Lights takes next
            wxl_livingazeroth::lights::Update(dt, snap);
            wxl_livingazeroth::distantlights::Update(snap); // the far glows, from the windows and the light scan's far lamps
        }
        {
            prof::Scope scope(prof::kOursCoverFill);
            wxl_livingazeroth::cover::Update(dt, snap);
        }
        // The memory readout: a slice of the address space's walk, the client's caches when due, the warnings (before the
        // snapshot key: a warning's snapshot is written this frame).
        wxl_livingazeroth::memory::Update(snap.inWorld);
        // The snapshot key (after seethrough::Update: last frame's walks and counts are whole).
        {
            mem::Scope memory(mem::kPanels);
            wxl_livingazeroth::snapshot::Update(dt);
        }
    }

    // Main thread, before the client tears the world down for a loading screen: no background work
    // may outlive the world it was copied from. (Extensions are never unloaded; there is no unload
    // callback to stop threads in.)
    void __cdecl OnWorldLeave(void* /*user*/, const void* /*args*/)
    {
        wxl_livingazeroth::fields::OnWorldLeave();
        wxl_livingazeroth::storm::OnWorldLeave(); // its bolt models go while the world's scene is still there
        wxl_livingazeroth::spelllights::Reset();  // the scene's models go with the world
    }

    void __cdecl OnDeviceLost(void* /*user*/, const void* /*args*/)
    {
        wxl_livingazeroth::depth::OnDeviceLost();
        wxl_livingazeroth::grassinst::OnDeviceLost();
        wxl_livingazeroth::postaa::OnDeviceLost();
        wxl_livingazeroth::groundfog::OnDeviceLost();
        wxl_livingazeroth::windowglow::OnDeviceLost();
        wxl_livingazeroth::distantlights::OnDeviceLost();
        wxl_livingazeroth::sunshafts::OnDeviceLost();
        wxl_livingazeroth::glassshafts::OnDeviceLost();
        wxl_livingazeroth::seethrough::OnDeviceLost();
        wxl_livingazeroth::prof::OnDeviceLost();
        wxl_livingazeroth::cover::OnDeviceLost();
    }
}

const WXL_PluginInfo* __cdecl WXL_Query(void)
{
    return &kInfo;
}

int __cdecl WXL_Load(const WXL_Api* api)
{
    if (!api) return 0;
    g_api = api;
    if (api->PublishInterface) api->PublishInterface(kWxlRainSignalName, kWxlRainSignalVersion, &g_rainSignal);

    // The overview first among the pages (pages are listed in registration order).
    wxl_livingazeroth::debug::RegisterOverview(api);

    // The frame profiler first: its hooks go first in every chain, and its UI scope opens at the
    // world -> UI boundary before the anti-aliasing (subscribed below) runs there.
    wxl_livingazeroth::prof::Install(api);

    wxl_livingazeroth::depth::Init(api);
    api->Subscribe(static_cast<uint32_t>(ev::Event::OnFrame), &OnFrame, nullptr);
    api->Subscribe(static_cast<uint32_t>(ev::Event::OnDeviceLost), &OnDeviceLost, nullptr);

    wxl_livingazeroth::wind::Init(api);
    wxl_livingazeroth::fields::Init(api);
    api->Subscribe(static_cast<uint32_t>(ev::Event::OnUpdate), &OnUpdate, nullptr);
    api->Subscribe(static_cast<uint32_t>(ev::Event::OnWorldLeave), &OnWorldLeave, nullptr);

    // Raises the grass distance cap right before the world CVars (and Config.wtf's value) are
    // validated.
    wxl_livingazeroth::grassdistance::Install(api);

    // Features register their shader patches first; the single create hook goes in after.
    wxl_livingazeroth::grass::Install(api);
    wxl_livingazeroth::terrainwet::Register();
    wxl_livingazeroth::terrainice::Install(api); // the chunk draw: each chunk's ice mask
    wxl_livingazeroth::modelice::Install(api);   // the M2 batch draws: ice models switch their block on
    wxl_livingazeroth::playerdepth::Install(api); // the player's see-through batches (mount wings) write depth too
    wxl_livingazeroth::doodadfade::Install(api);  // the doodad fade and the M2 batch draws: fading doodads dissolve in
    wxl_livingazeroth::treeshadows::Install(api); // the shadow cache's band draw: the near trees' map drawn again, following the wind
    wxl_livingazeroth::modellights::Register();
    wxl_livingazeroth::modelice::Register();
    wxl_livingazeroth::shaderpatch::Install(api);
    wxl_livingazeroth::modellights::InstallProbe(api);
    wxl_livingazeroth::debug::InstallWmoBindProbe(api);
    wxl_livingazeroth::wmoice::Install(api);      // the WMO batch loops: ice textures switch the same block on
    wxl_livingazeroth::seethrough::Install(api);  // see-through windows: the portal walk, the device draw, the model scene
    wxl_livingazeroth::debug::InstallFogProbe(api);
    // Ground fog: drawn at the weather render's entry (after the water, before rain and see-through models).
    wxl_livingazeroth::groundfog::Install(api);
    // Window glow: at the same point, right after the fog (its link calls ours).
    wxl_livingazeroth::windowglow::Install(api);
    // Distant night lights: there too, after the window glow (its bright pass never sees them).
    wxl_livingazeroth::distantlights::Install(api);
    // Sun shafts: there too, between the fog and the window glow (the deck's result is this frame's).
    wxl_livingazeroth::sunshafts::Install(api);
    // Coloured shafts through sunlit window glass, in the room's air: right after the sun shafts.
    wxl_livingazeroth::glassshafts::Install(api);
    // Records the M2 shader effects as the client loads them (research: dumps; later M2 patches).
    wxl_livingazeroth::m2effects::Install(api);
    // Storms: the day-night colours, shaded right before the client's sky, light and fog take them.
    wxl_livingazeroth::storm::Install(api);
    // Thunder's distance filters (experimental): detours on the client's DSP chain callback and its chains' shutdown.
    wxl_livingazeroth::thunderdsp::Install(api);

    // Surface cover spike: draws at the end of the world scene (before anti-aliasing's pass).
    wxl_livingazeroth::cover::Install(api);

    // The snapshot key (debug/Snapshot), the compare key (debug/Compare) and the lantern's key: the window's messages.
    wxl_livingazeroth::snapshot::Install(api);
    wxl_livingazeroth::compare::Install(api);
    wxl_livingazeroth::lantern::Install(api);
    // The memory readout (debug/Memory): its guards against the client's code, read before any detour is armed.
    wxl_livingazeroth::memory::Install(api);

    // Read-only addon functions (docs/lua-api.md): registered with every script context the client builds.
    wxl_livingazeroth::envlua::Register();
    wxl_livingazeroth::luaapi::Install(api);

    // Anti-aliasing subscribes before the debug overlays, so it runs first and they stay sharp on top.
    wxl_livingazeroth::postaa::Install(api);
    wxl_livingazeroth::postaa::RegisterPanel(api);
    wxl_livingazeroth::debug::RegisterPanel(api);
    wxl_livingazeroth::debug::RegisterWindPanel(api);
    wxl_livingazeroth::debug::RegisterSurfacePanel(api);
    wxl_livingazeroth::debug::RegisterClimatePanel(api);
    wxl_livingazeroth::debug::RegisterStormPanel(api);
    wxl_livingazeroth::debug::RegisterFogPanel(api);
    wxl_livingazeroth::debug::RegisterLightsPanel(api);
    wxl_livingazeroth::debug::RegisterLightEditor(api);

    api->Log(WXL_LOG_INFO, "wxl-seyris-living-azeroth", "v1.1 loaded (WXL_Load reached).");
    return 1;
}
