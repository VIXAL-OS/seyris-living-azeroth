// Window lights and window glow, the runtime half (the rules: env/WindowModel). The lit windows of inhabited
// buildings come from WindowLights.cdbc (tools/gen_window_lights.py, docs/cdbc-tables.md): each row a light in
// front of a facade's windows and the glass's rectangle. At night (the client's own window fade) env/Lights adds the
// nearest as spot lights facing out, and features/WindowGlow blooms the glass.
// The rooms behind the glass (features/Glass) go dark as the night goes on; WindowRooms.cdbc says which rooms' cells
// each light's glass covers, of which building, and how much of its glass each is. LitShare works out, with the
// shader's own hash, band and clock, how much of a light's glass has a lit room now; the light and the glow scale by
// it, so a row of windows dims as its rooms go dark, each fading over the same 1.5 s as the room.
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
        int   lightsFollowRooms = 1;    // the lights and the glow dim with the rooms behind the glass (WindowRooms.cdbc)
    };
    Settings& Config();

    /// How much of the window's glass is lit now, 0..1 (features/Glass's rooms and lit frosted glass, WindowRooms.cdbc):
    /// each room's, frosted or stained cell's lit value x (1 - the flicker's dip: a hearth behind frosted glass, a room's
    /// lamp with LampFlicker); 1 with LightsFollowRooms off, for cells whose glass doesn't hide its glow now (the glass,
    /// its rooms or lamp-lit rooms off; FrostedLightsOut off; StainedRooms off; titan glass lit from behind), and for glass
    /// with none (not glass, a dungeon's stained glass). The cells read are those of the stained glass's state now:
    /// with its rooms (glass::StainedRoomsOn) kCellStained and kCellWithStained, without them kCellUnstained (so with
    /// StainedRooms off every light dims as it did before stained glass had rooms). Once per window per frame.
    float LitShare(const Window& w);
    /// Whether the window light's flicker is in its LitShare, in time with the glass (a hearth's, a lamp's): env/Lights
    /// then leaves out the light's own.
    bool SyncedFlicker(const Window& w);
    /// WindowRooms.Flags: the cell is lit frosted glass (the glass itself goes dark and flickers; its room blurred);
    /// stained glass (its faint room: read only while stained glass has rooms); read only while it hasn't (the cell as it
    /// was before, the stained glass found among the other windows); a room or frosted cell read only while it has.
    constexpr uint32_t kCellFrosted = 0x1, kCellStained = 0x2, kCellUnstained = 0x4, kCellWithStained = 0x8;

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
        // The rooms (LitShare, last frame): windows near with rooms, of them dimmed (part dark) and dark, the mean lit.
        unsigned roomWindows = 0, roomDimmed = 0, roomDark = 0;
        float    roomMeanLit = 1.0f;
        // Lit frosted glass (LitShare, last frame, by its lights out alone): windows near with it, dimmed, dark, the mean lit.
        unsigned frostWindows = 0, frostDimmed = 0, frostDark = 0;
        float    frostMeanLit = 1.0f;
    };
    Stats& GetStats();

    /// The lights panel's section.
    void DrawPanelSection(const WXL_Api* api);
}
