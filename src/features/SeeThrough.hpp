// See-through windows (experimental, SeeThroughWindows, off by default): from outside, the real modelled room behind a
// window, its furniture with it, seen through the glass. The rules: env/SeeThroughModel; the research:
// docs/r&d/see-through-windows.md.
//  - The buildings: SeeThroughBuildings.cdbc's (features/Glass loads it; 2026-10-22, before the inn alone in the code):
//    a WMO's path and its Flags, from outside and from inside.
//  - The room: the client's portal walk (Wmo.RenderThroughPortals) reaches an exterior group of a building the table
//    names from outside (s_interiorPass 0, the camera in no building of it); after it, each of the group's windows
//    with an interior group behind it, facing the camera, on the screen and within kRange of the group, is opened: the
//    client's own walk runs on into the room through the window's screen rectangle (FrustumPush, FrustumSet, the walk at
//    depth 1, as through a doorway), so the room joins the frame's sort table clipped to the window, its doodads (the
//    furniture) culled to the same frustum, and the rooms beyond its own doors. Nothing in the WMO changes.
//  - The glass: features/WmoIce's batch hooks tell this which group of which instance draws (Wmo.RenderGroup) and each
//    batch that passes the cull test (Wmo.CullBatch). A glass batch of an opened group with at least one of its windows
//    open is deferred: its device draw (Gx.DeviceDraw) runs with the client's colour and depth writes off (the client's
//    own state cache: no pixel, no depth; a transition batch, which the interior loop draws twice, lit as the outside and
//    then as the inside: its first draw silenced, its second deferred, 2026-10-23), and the device's whole state at it is
//    captured, every shader constant apart
//    (features/SeeThroughDraw: a state block alone puts back the pixel shader's c0..c127 only, and with the glass's view
//    rows from c180 lost its window lookup found no window: "every see-through window pixel is solid YELLOW", the debug
//    view, 2026-10-21). After the model scene's first
//    pass (M2.SceneDraw, World::s_m2Scene, pass 0: the opaque models, the room's furniture among them) each is drawn again
//    from that state, two ways (SeeThroughRedraw):
//     1 (the default, per pixel): with the glass's look (LA's b9 block) and its redraw (b11, c219: features/GlassShader):
//       each pixel finds its window; an open one has no fake room (the real room is behind it) and its glass texels'
//       alpha is 1 - the mask x (1 - SeeThroughOpacity), so its lead, frame and tracery stay opaque ("the tracery also
//       becomes more transparent", the user, 2026-10-20); a shut or unseen window of the batch stays as the client draws
//       it, opaque with its fake room. The mask is the texture's lead mask (s15, built here from the client's own file:
//       env/GlassModel LeadMask over GlassLead.cdbc's panes; "yeah build the lead mask pls :D", the user, 2026-10-21):
//       every pane's inside glass, lit and shaded alike, where the light's mask left the shaded half and so most of the
//       room out (~4% of the room through the inn's ground-floor window); a texture without panes keeps the light's. Two draws split by that alpha in the shader (c28.y, a texkill; not the device's alpha test):
//       the opaque with its depth, then the glass over the room, blended (SRCALPHA), no depth. A shader without the
//       redraw (a variant without the blocks): drawn opaque, as the client would have. Its debug view (the panel's, not
//       saved; c28.x) tints each pixel by what the shader decided, and the panel reads the device's state back at the
//       redraw once a second: "see-through windows don't seem to work anymore now" (the user, 2026-10-21) with every
//       count saying the glass was drawn.
//     0 (as on 2026-10-20): the whole batch at SeeThroughOpacity (BLENDFACTOR), its look kept but its fake room off, no
//       depth; a batch goes see-through only with none of its windows shut.
//  - With SeeThroughRedraw 1 the room is opened only where it's seen through the window (env/SeeThroughModel's
//    LookThrough, SightOpen, Hold: rays from the eye through points on the window's own glass meet the room's own surfaces,
//    its glass aside, from inside rather than from outside; meeting nothing of it, on through a doorway, neither), its
//    rendered triangles copied out of its group once (MOVT, MOVI, MOBA: RoomMesh) and the windows' glass points out of the
//    walked group's glass batches (GlassSightPoints); the verdict held per window (its own thresholds to open and to stay,
//    a change after 0.35 s): "certain attic windows shouldn't be able to see through the roof" (the user, 2026-10-20);
//    "it works at some angles and goes back to the fake room at others lol" (2026-10-21). Since 2026-10-22 ("dormers
//    still switch between the fake rooms and the real ones") a ray that meets nothing of the room is followed on through
//    every group of the building (LookIn over each group's surfaces, its glass tagged: BuildingOf): the next room through
//    a doorway sees in, the roof or a wall from inside, or the void, is against; five of nine must see in to open.
//  - From inside (SeeThroughFromInside, off by default; "windows should be see-through from the inside as well", the user,
//    2026-10-21, in the inn's ground floor): the client draws the outdoors from inside only through its ways out, the
//    exterior portal views its walk from the camera's groups records for each portal into an exterior group (the outdoors
//    culled through the rectangle round them, the sky drawn in it), so the street never shows behind a window. In that walk
//    (s_interiorPass 1, the camera's building one the table names), each window of a room it reaches (GlassWindows' rows of
//    interior groups) that faces the camera from inside, lies on the screen within the walk's rectangle and within 40 yd
//    becomes a way out of its own: the view the client would make of a portal there (its screen rectangle, its farthest
//    depth) pushed and merged through the client's own PushExteriorPortalView, MergeIntoWorldRect and
//    MergeIntoFrustumRect, where the street is really seen through it (LookOut: rays through its glass leave by the
//    building's own window glass, not through a wall or the roof from inside, whose faces are culled). The room's glass
//    batch is then deferred and drawn again over the street as from outside (SeeThroughRedraw, SeeThroughOpacity). Nothing
//    else of the walk changes; the outdoors, the sky and the weather come as through an open door.
//  - Stained glass (2026-10-22: "the coloured glass should stay coloured over the room (tint, not just blend); and from
//    inside, the street through the coloured glass"): with SeeThroughRedraw 1 what's behind an open window's stained glass
//    is multiplied by the glass's colour before the glass is blended over it (SeeThroughStainedTint; SeeThroughDraw's tint
//    pass, its own pixel shader, not the stock variants': after the opaque draw, depth tested LESS, so only where the glass
//    is in front of what's behind it: split, over the open windows' triangles; whole, over the batch's). Its lead mask is
//    found in the texture (GlassLead.cdbc's rows over its designs or the whole texture: env/GlassModel LeadMask), and the
//    gate passes the inside of a thick wall round a window's opening (env/SeeThroughModel kReveal) and, from inside, the
//    lining of a deep opening up to the building's outer glass behind it (OuterBehind, kLiningMargin).
//  - Frosted glass (2026-10-22, Phase B): a frosted window opens with SeeThroughFrosted (2026-10-23; before, with the blur
//    over 0); behind its glass the frame's blurred copy of what's there (SeeThroughDraw's FrostCapture, once a frame before
//    the first frosted batch, then FrostDraw where the glass is in front), the glass over it at the opacity; with
//    SeeThroughFrostBlur 0 no copy, the room as through clear glass (env/SeeThroughModel PlanFrost).
//  - Between rooms (2026-10-23, Phase C; SeeThroughBetweenRooms, the table's 0x4 for a WMO or one group of it): from
//    inside, a window of an interior group looking into another interior group (env/SeeThroughModel RoomBeyond: Ironforge's
//    shop windows from its streets, and back) opens onto it as a window onto a room does from outside: where the room is
//    seen through it (LookIn), the client's walk goes on into that room through the window's rectangle (OpenWindow, with
//    the walk's own interior argument and its lighting global put back after), and the glass is drawn again over it.
//  - Guards: the client's code it relies on (kGuards) checked once at install against the running image; every hook body
//    under __try/__except; any fault switches it off for the session (the panel says where).
#pragma once

#include "wxl/PluginApi.h"

#include <cstdint>
#include <string>
#include <vector>

namespace wxl_livingazeroth::seethrough
{
    struct Settings
    {
        int   enabled = 0;          // SeeThroughWindows: experimental, off by default
        float opacity = 0.35f;      // SeeThroughOpacity: the glass's share over the room behind it (0..1)
        int   redraw = 1;           // SeeThroughRedraw: 1 per pixel (the lead mask: the panes see-through, the lead, frame and
                                    // shutters opaque; only where the room is seen); 0 the whole glass at the opacity (2026-10-20)
        int   fromInside = 0;       // SeeThroughFromInside: from inside, the street through the room's own windows (a way out
                                    // each, as a door is); off by default, not tied to `enabled`
        float stainedTint = 0.8f;   // SeeThroughStainedTint: with SeeThroughRedraw 1, how much stained glass colours what's seen
                                    // through it (0..1; SeeThroughDraw's tint pass; 0 none: the glass only blended over it)
        int   betweenRooms = 1;     // SeeThroughBetweenRooms (2026-10-23, Phase C): with SeeThroughFromInside, a window from one
                                    // room or indoor street into another (Ironforge's shops) opens onto it (the table's 0x4)
        int   frosted = 1;          // SeeThroughFrosted: with SeeThroughRedraw 1, frosted glass goes see-through (2026-10-23); 0:
                                    // its windows stay shut, the fake rooms as before
        float frostBlur = 0.5f;     // SeeThroughFrostBlur: how soft frosted glass makes what's seen through it (0..1;
                                    // SeeThroughDraw's FrostCapture; 0: no blur, as through clear glass, 2026-10-23)
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
    /// A batch that passed the loop's cull test (Wmo.CullBatch): the group object, its MOBA record, its glass material
    /// (features/Glass: index + 1; 0 not glass).
    void Batch(const void* group, const void* record, uint32_t glassMaterial);
    /// The batch loop ended.
    void EndLoop();

    /// The panel's section (the lights panel, under the glass) and one status line for the log.
    void DrawPanelSection(const WXL_Api* api);
    const char* StatusLine();
    /// The section's status lines (everything under its controls), as the panel draws them (debug/Snapshot writes them).
    void StatusLines(std::vector<std::string>& out);

    // --- a snapshot's (debug/Snapshot, 2026-10-22) ----------------------------------------------------------------
    /// A group the walks visited last frame (from outside or inside) and its windows' states.
    struct WalkedGroup
    {
        std::vector<int> rooms;     // between rooms (2026-10-23): the room each open window opened onto (-1: a way out), as `open`
        std::string wmo;            // its path
        uint32_t group = 0;
        bool  inside = false;       // walked from inside (its room's own windows)
        float distance = 0.0f;      // the camera to the group's box, yd
        float cam[3] = {};          // the camera in the WMO's space (s_camPosLocal)
        float model[16] = {};       // its placement (s_mapObjToWorld: row vectors, row-major)
        std::vector<uint32_t> open, shut;   // its windows' ordinals
    };
    /// Last frame's walked groups, nearest the camera first.
    void LastWalked(std::vector<WalkedGroup>& out);
    /// Every placed WMO's group the client drew last frame (Wmo.RenderGroup, any building, in the table or not; 2026-10-24:
    /// a snapshot at Valkyrion named Dalaran, the see-through walk's last group, 1,372 yd off, not the vrykul tower in
    /// front of the camera): its path, group, MOGI flags and box, the instance's m_invMat (world -> its space, row vectors),
    /// the group's name (MOGN, when loaded), its glass batches (GlassMaterials' textures) and whether the table names its WMO.
    struct DrawnGroup
    {
        std::string wmo, name;
        uint32_t group = 0, flags = 0, glassBatches = 0, batches = 0;
        float lo[3] = {}, hi[3] = {};
        float invMat[16] = {};
        bool inTable = false;
    };
    void LastDrawn(std::vector<DrawnGroup>& out);
    /// The blurred copy (frosted glass's) read back: its mean colour over the frame and within each open frosted window's
    /// screen rectangle last frame, and when it was taken (after how many of the frame's WMO batch draws, how many of them
    /// the opened rooms'); and the panel's diagnostics. A device read: a snapshot's, not every frame's.
    void CopyLines(std::vector<std::string>& out);
    /// The building the camera is in (the client's s_viewerMapObjDef: any WMO, in the table or not): its path, the
    /// placed instance's m_invMat (world -> its space, row vectors) and its groups' flags and boxes (MOGI). False when
    /// the camera is in none or it can't be read.
    bool ViewerBuilding(std::string& wmo, float invMat[16], std::vector<uint32_t>& flags, std::vector<float>& boxes);
}
