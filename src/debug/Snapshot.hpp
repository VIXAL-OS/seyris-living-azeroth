// The snapshot key (2026-10-22, Lumenveil's queue #10: "one key (and a panel button) that writes your exact situation to
// a file so bug reports don't need guessing"). One press writes Logs\la-snapshots\<date>-<time>.txt under the game folder
// and says so in chat:
//  - where: the map, the zone and sub-area and the area chain, the player's position and facing, the client's indoors;
//  - the camera: its world position, where it looks and its up, its field of view; in the nearest WMO's space too (the
//    building the camera is in, the client's s_viewerMapObjDef, or the nearest group the see-through walks visited last
//    frame), with the placement and the windows open there (debug/SnapshotView: the lines the harnesses read back);
//  - the time of day, the server's weather and Living Azeroth's (the climate's, the storm's phase, darker nights);
//  - the settings that matter (see-through, fog, storm, glass), the frame rate, and the see-through, fog and storm
//    panels' status lines as they read; the compare key's state first among the settings (2026-10-25: all effects off
//    for a comparison, and which were on), the lantern's and the distant lights' lines with the lights panel's.
//  - the memory readout (debug/Memory): the address space by owner, Direct3D's texture memory, the client's caches (its
//    largest textures named), Living Azeroth's own use by feature (2026-10-25). The memory warning asks for one too.
// The key: SnapshotKey, a Windows virtual-key code (F11 by default; 0 = none), read from the window's messages (OnInput)
// and polled while the client's window has the focus, as the core's F9; never swallowed (a binding of the client's on the
// same key still runs). The overview page and the see-through section have a button. Nothing here reaches addons.
#pragma once

#include "wxl/PluginApi.h"

namespace wxl_livingazeroth::snapshot
{
    struct Settings
    {
        int key = 0x7A;   // SnapshotKey: a Windows virtual-key code (0x7A F11); 0 = no key
    };
    Settings& Config();

    /// The key's messages (OnInput). From WXL_Load.
    void Install(const WXL_Api* api);
    /// Once per frame (OnUpdate, after seethrough::Update: last frame's walks are whole): the frame rate, the key's poll,
    /// and a snapshot written when one was asked for.
    void Update(float dt);
    /// Asks for one (the panels' buttons): written at the next Update.
    void Request(const char* by);
    /// The last one written (its path under the game folder), or why not.
    const char* Status();
    /// A button that asks for one, and the status under it. Inside a panel only.
    void DrawButton(const WXL_Api* api, const char* id);
}
