// Window lights and window glow, the runtime half (the rules: env/WindowModel). The lit windows of inhabited
// buildings come from WindowLights.cdbc (tools/gen_window_lights.py, docs/cdbc-tables.md): each row a light in
// front of a facade's windows and the glass's rectangle. At night (the client's own window fade) env/Lights adds the
// nearest as spot lights facing out, and features/WindowGlow blooms the glass.
#pragma once

#include "../env/WindowModel.hpp"

#include "wxl/PluginApi.h"

#include <cstdint>
#include <vector>

struct WXL_SeyrisCdbcApi;

namespace wxl_livingazeroth::windows
{
    struct Settings
    {
        int   lights = 1;               // window lights (env/Lights)
        float lightStrength = 1.0f;     // x their brightness
        float lightRange = 160.0f;      // yd from the camera
        int   maxLights = 96;           // the nearest this many at once
        int   perCell = 4;              // at most this many window lights in one cell of the lights' grid
        int   glow = 1;                 // the window glow (features/WindowGlow)
        float glowStrength = 1.0f;      // x its brightness
        float glowRange = 220.0f;       // yd from the camera
        int   withClientGlow = 1;       // while the client's Full Screen Glow (ffxGlow) is on: 0 full, 1 half, 2 off
        float nightTest = -1.0f;        // the panel's test: this night factor (0..1) instead of the client's; < 0 off
    };
    Settings& Config();

    /// (Re)loads WindowLights.cdbc from DBFilesClient (the lights panel's reload button calls it too).
    void Load(const WXL_SeyrisCdbcApi* cdbc);
    const char* Status();
    uint32_t    Generation();   // changes on every load

    /// The windows of `map` near `center`, nearest first (Index::Near).
    void Near(uint32_t map, const float center[3], float range, bool glowOnly, size_t max, std::vector<const Window*>& out);

    /// The night factor this frame, 0 (day) .. 1: the client's own window fade (the guards hold), else the same curve
    /// from the clock (the climate panel's time override, if set, wins), else the panel's test value.
    float Night();
    const char* NightSource();

    /// The client's Full Screen Glow: 1 on, 0 off, -1 unknown (its guards don't hold).
    int ClientGlow();

    /// Once per frame, before env/Lights (Extension OnUpdate).
    void Update();

    /// Counters for the panel (filled by env/Lights and features/WindowGlow).
    struct Stats
    {
        unsigned inRange = 0, lights = 0, leftOutOfCells = 0; // lights: in range, drawn, kept out of full cells
        unsigned glowWindows = 0, glowDrawn = 0;             // glow: windows in range, drawn this frame
        double   glowMs = 0.0;
        const char* glowInactive = "not drawn yet";
    };
    Stats& GetStats();

    /// The lights panel's section.
    void DrawPanelSection(const WXL_Api* api);
}
