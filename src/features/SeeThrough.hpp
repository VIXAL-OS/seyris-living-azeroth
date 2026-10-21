// See-through windows (experimental, SeeThroughWindows, off by default): from outside, the real modelled room behind a
// window, its furniture with it, seen through the glass. The rules: env/SeeThroughModel; the research:
// docs/r&d/see-through-windows.md.
//  - The room: the client's portal walk (Wmo.RenderThroughPortals) reaches an exterior group of a building in the
//    whitelist from outside (s_interiorPass 0, the camera in no building of it); after it, each of the group's windows
//    with an interior group behind it, facing the camera, on the screen and within kRange of the group, is opened: the
//    client's own walk runs on into the room through the window's screen rectangle (FrustumPush, FrustumSet, the walk at
//    depth 1, as through a doorway), so the room joins the frame's sort table clipped to the window, its doodads (the
//    furniture) culled to the same frustum, and the rooms beyond its own doors. Nothing in the WMO changes.
//  - The glass: features/WmoIce's batch hooks tell this which group of which instance draws (Wmo.RenderGroup) and each
//    batch that passes the cull test (Wmo.CullBatch). A glass batch of an opened group with at least one of its windows
//    open is deferred: its device draw (Gx.DeviceDraw) runs with the client's colour and depth writes off (the client's
//    own state cache: no pixel, no depth), and the device's whole state at it is captured. After the model scene's first
//    pass (M2.SceneDraw, World::s_m2Scene, pass 0: the opaque models, the room's furniture among them) each is drawn again
//    from that state, two ways (SeeThroughRedraw):
//     0 (the default; as on 2026-10-20, the way seen working in game): the whole batch at SeeThroughOpacity (BLENDFACTOR),
//       its look kept but its fake room off, no depth; a batch goes see-through only with none of its windows shut.
//     1 (per pixel): with the glass's look (LA's b9 block) and its redraw (b11, c219: features/GlassShader): each pixel
//       finds its window; an open one has no fake room (the real room is behind it) and its glass texels' alpha is
//       1 - the mask x (1 - SeeThroughOpacity), so its lead, frame and tracery stay opaque ("the tracery also becomes more
//       transparent", the user, 2026-10-20); a shut or unseen window of the batch stays as the client draws it, opaque
//       with its fake room. Two draws split by that alpha in the shader (c28.y, a texkill; not the device's alpha test):
//       the opaque with its depth, then the glass over the room, blended (SRCALPHA), no depth. A shader without the
//       redraw (a variant without the blocks): drawn opaque, as the client would have. Its debug view (the panel's, not
//       saved; c28.x) tints each pixel by what the shader decided, and the panel reads the device's state back at the
//       redraw once a second: "see-through windows don't seem to work anymore now" (the user, 2026-10-21) with every
//       count saying the glass was drawn.
//  - With SeeThroughRedraw 1 the room is opened only where it's seen through the window (env/SeeThroughModel's
//    SeesRoom: rays from the eye through the window meet the room's own surfaces from inside first, its glass aside),
//    its rendered triangles copied out of its group once (MOVT, MOVI, MOBA: RoomMesh): "certain attic windows shouldn't
//    be able to see through the roof" (the user, 2026-10-20).
//  - Guards: the client's code it relies on (kGuards) checked once at install against the running image; every hook body
//    under __try/__except; any fault switches it off for the session (the panel says where).
#pragma once

#include "wxl/PluginApi.h"

namespace wxl_livingazeroth::seethrough
{
    struct Settings
    {
        int   enabled = 0;          // SeeThroughWindows: experimental, off by default
        float opacity = 0.35f;      // SeeThroughOpacity: the glass's share over the room behind it (0..1)
        int   redraw = 0;           // SeeThroughRedraw: 0 the whole glass at the opacity, every window with a room open (as on
                                    // 2026-10-20, seen working in game); 1 per pixel (the lead opaque, only where the room is seen)
    };
    Settings& Config();

    /// The walk's, the device draw's and the model scene's hooks, the guards (from WXL_Load).
    void Install(const WXL_Api* api);
    /// Once per frame (OnUpdate): last frame's counts for the panel; drops whatever a frame left behind.
    void Update();
    /// Before IDirect3DDevice9::Reset: the captured states go.
    void OnDeviceLost();

    // --- features/WmoIce's hooks ------------------------------------------------------------------------------------
    /// Around one placed instance's group (Wmo.RenderGroup): its root, the group's index, the instance's +0xB0.
    void BeginGroupDraw(const void* root, int group, const void* instanceArg);
    void EndGroupDraw();
    /// A batch that passed the loop's cull test (Wmo.CullBatch): the group object, its MOBA record, whether its material
    /// is glass.
    void Batch(const void* group, const void* record, bool glass);
    /// The batch loop ended.
    void EndLoop();

    /// The panel's section (the lights panel, under the glass) and one status line for the log.
    void DrawPanelSection(const WXL_Api* api);
    const char* StatusLine();
}
