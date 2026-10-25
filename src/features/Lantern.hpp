// The belt lantern, the runtime half (the rules: env/LanternModel). Every frame, before our lights (Extension OnUpdate):
// the key (the window's messages and a poll, debug/KeyWatch), how dark the place is, lit or out, and this frame's light at
// the player's hip, which env/Lights takes (Light) as the player's own light: never merged with a torch, never left out of
// the pool or of a grid cell (it ranks first in every cell it reaches, in the scene's places, not the spell lights'),
// indoor as the client says the player is, through the same knee as every light on every surface. Read only: the player's
// position (the world snapshot), facing (core World: the active player's object) and whether they're mounted
// (env/Actors).
#pragma once

#include "../env/LanternModel.hpp"
#include "../env/WorldQuery.hpp"
#include "wxl/PluginApi.h"

#include <string>
#include <vector>

namespace wxl_livingazeroth::lantern
{
    Settings& Config();

    /// The key's messages (OnInput). From WXL_Load.
    void Install(const WXL_Api* api);

    /// Once per frame (OnUpdate, after the actors and the storm, before env/Lights): the key, the rules, this frame's light.
    void Update(float dt, const world::Snapshot& snap);

    /// This frame's light, for env/Lights (false: none now).
    struct Light
    {
        float pos[3];
        float color[3];   // already x its strength now
        float radius;
        bool  indoors;
    };
    bool Current(Light& out);

    struct Stats
    {
        const char* why = "not in the world yet"; // why there's no light now ("" while it shines)
        float strength = 0.0f, darkness = 0.0f, around = 0.0f, daylight = 0.0f;
        bool  lit = false, autoWants = false, indoors = false, mounted = false, facingRead = false;
        int   held = -1;
        float pos[3] = {};
        unsigned presses = 0;
    };
    const Stats& GetStats();

    const char* StatusLine();
    void SnapshotLines(std::vector<std::string>& out);
    /// The lights panel's section.
    void DrawPanelSection(const WXL_Api* api);

    // --- tests ---------------------------------------------------------------------------------------------------------
    /// One frame of the rules and the light with these inputs instead of the client's (no key poll): the player at `feet`
    /// facing `facing` (radians), `around` the light round them. Returns the strength.
    float StepForTests(float dt, const float feet[3], float facing, float daylight, bool indoors, float stormDarkness, float around);
    /// The key, as a press would (its chat line included).
    void PressForTests();
    void ResetForTests();
}
