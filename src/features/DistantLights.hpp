// Distant night lights, the runtime half (the rules: env/DistantLightModel). At night the lamps and lit windows of towns
// past our point lights' reach are small glows of their own colour, so a town reads from a hill a few hundred yards off.
//
//  - Each frame (OnUpdate, after env/Lights): the lit windows of WindowLights.cdbc out to MaxDistance (the share of their
//    rooms lit now, seen from in front) and the light scan's far lamps (env/Lights FarLights: the lights of the models
//    from two thirds of our lights' range out, noted every couple of seconds), x the night (the client's own window fade),
//    fading in as the point lights fade out; merged and capped (MaxLights).
//  - Drawn at the weather render's entry (Weather.Render, after the fog, the shafts and the window glow), in one batched
//    draw: a quad per glow, already on the screen (XYZRHW), additive, tested against the scene's depth (hills and walls in
//    front hide them; each glow is pulled a little toward the camera so its own wall doesn't), faded by the client's
//    distance fog (its end and start: the colour resolve's CurrentLight +0x48, +0x4C, as env/StormModel reads them) and
//    before the far clip. Its own pixel shader (ps_2_0, a dozen slots); nothing in the world's shaders changes. Not drawn
//    with the camera indoors or under water.
#pragma once

#include "../env/DistantLightModel.hpp"
#include "../env/Lights.hpp"
#include "../env/WorldQuery.hpp"
#include "wxl/PluginApi.h"

#include <d3d9.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wxl_livingazeroth::distantlights
{
    distantlight::Settings& Config();

    /// The weather render hook. From WXL_Load.
    void Install(const WXL_Api* api);
    /// Once per frame, after env/Lights::Update: the far list asked for, this frame's glows.
    void Update(const world::Snapshot& snap);
    void OnDeviceLost();

    struct Stats
    {
        const char* why = "not in the world yet";   // why nothing is drawn now (nullptr: drawn)
        const char* drawWhy = "not drawn yet";      // the draw's own reason (nullptr: drawn)
        float    night = 0.0f, fogStart = 0.0f, fogEnd = 0.0f, farClip = 0.0f;
        unsigned windowsNear = 0, windows = 0, lamps = 0;          // windows looked at; glows from windows, from lamps (before merging)
        unsigned sprites = 0, merged = 0, capped = 0, drawn = 0;   // after merging; sources merged into others; left out by the cap; drawn
        unsigned farthest = 0;                                     // yd: the farthest drawn
        unsigned builds = 0;                                       // the list built (again) since the start: only when something changed
        double   msBuild = 0.0, msDraw = 0.0;                      // the last build's; this frame's draw
        double   msAverage = 0.0;                                  // the building's cost a frame, averaged over about a second
        double   msWindows = 0.0, msLamps = 0.0, msMerge = 0.0;    // the last build's parts: the windows, the lamps, merging and the cap
    };
    const Stats& GetStats();

    const char* StatusLine();
    void SnapshotLines(std::vector<std::string>& out);
    /// The lights panel's section.
    void DrawPanelSection(const WXL_Api* api);

    /// The pixel shader's source (ps_2_0), for the tests' slot count.
    const char* PixelShaderSource();

    // --- tests ---------------------------------------------------------------------------------------------------------
    /// This frame's glows as Update builds them, from `eye` on `map` at night `night`, with the windows features/Windows
    /// loaded and `lamps` as the light scan's far list.
    void BuildForTests(const float eye[3], uint32_t map, float night, const std::vector<lights::FarLight>& lamps);
    const std::vector<distantlight::Sprite>& Sprites();
    /// The draw on `dev` into whatever it has bound: the scene's view (row vectors; camera-relative when its translation is
    /// ~0) and projection, the camera, the fog (start, end yd; 0, 0 = none). Returns the glows drawn.
    unsigned DrawForTests(IDirect3DDevice9* dev, const float view[16], const float proj[16], const float cam[3], float fogStart, float fogEnd);
}
