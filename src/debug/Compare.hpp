// The compare key (2026-10-25, Lumenveil's queue #10, its second half, the idea only: "one key for all effects off/on").
// One press switches every Living Azeroth visual effect off, so a scene can be seen as the client draws it; the next press
// switches them back on, and a chat line says which it is. For A/B screenshots and bug reports.
//
// Each effect goes off through its own switch, the one its panel checkbox sets (Switches(): the ground fog, storms, darker
// nights, our point lights with the window and spell lights and the lantern on them, the window glow, sun shafts, glass,
// see-through windows, crystals, clear ice, wet ground and wet surfaces, the surface cover, tree shadows, grass motion and density, the
// doodad fade, anti-aliasing, mount wings' depth, distant lights), so each feature runs its own off path (clears its
// lights, gives the client its own lights back, stops drawing) and nothing is left half on. The values it found are kept
// and put back by the second press; a switch someone changed in a panel meanwhile keeps that change. Nothing here reads or
// writes WarcraftXL.ini: the saved settings stay as they are, and a restart starts with them.
//
// The key: CompareKey, a Windows virtual-key code (F8 by default: the client binds nothing to it, the core's panel is F9,
// the snapshot F11), read like the snapshot key (debug/KeyWatch). The overview page has a button; the snapshot (F11) and
// the 30-second log line say which state it's in.
#pragma once

#include "wxl/PluginApi.h"

#include <cstddef>
#include <string>
#include <vector>

namespace wxl_livingazeroth::compare
{
    struct Settings
    {
        int key = 0x77;   // CompareKey: F8; 0 = no key
    };
    Settings& Config();

    /// The key's messages (OnInput). From WXL_Load.
    void Install(const WXL_Api* api);
    /// Once per frame, first in OnUpdate (the switches then hold for the whole frame): the key's poll and the toggle.
    void Update();
    /// Off when on, on when off; `by` for the chat line and the log ("the key", "the panel's button").
    void Toggle(const char* by);
    /// Every effect is off for the comparison now.
    bool EffectsOff();

    /// One effect's switch: its value now, setting it, and the value that turns its effect off.
    struct Switch
    {
        const char* name;
        float (*get)();
        void (*set)(float);
        float off;
    };
    const Switch* Switches(size_t& count);

    const char* StatusLine();
    void SnapshotLines(std::vector<std::string>& out);
    /// A button that toggles, and the state under it. Inside a panel only.
    void DrawButton(const WXL_Api* api, const char* id);

    /// Tests: forget the comparison (as if never pressed), without touching any switch.
    void ResetForTests();
}
