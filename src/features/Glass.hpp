// Window glass, the runtime half (the rules: env/GlassModel; the shader blocks: features/GlassShader). The glass
// textures and their kinds come from GlassMaterials.cdbc, the panes from GlassPanes.cdbc (tools/gen_glass.py,
// docs/cdbc-tables.md).
//  - The look (b9): features/WmoIce's batch hooks ask, per batch, whether its material's texture is glass; this
//    sets the batch's constants (its kind's look, its mask, inside or outside) and switches the block on.
//  - The floor light (b10): each frame the sunlit panes nearest the camera (up to kFloorPanes) become projection
//    rows; the batches of a building's interior groups (not its glass, not a sand floor's rims) take them.
//  - The coloured shafts: features/GlassShafts marches the room's air with the panes whose room holds the camera.
//  - The rooms behind clear glass, per building (RoomStyles.cdbc, GlassBuildings.cdbc): features/WmoIce's group hook
//    (Wmo.RenderGroup: the placed instance and the group) names the building before its batches draw; each glass
//    batch then gets its style (tints, brightness, lamp, curtains, the layouts' weights, the dark share by the hour)
//    and its seed. A batch whose material the client lights at night (MOMT SIDN) gets a lamp-lit room.
#pragma once

#include "../env/GlassModel.hpp"
#include "GlassShader.hpp"

#include "wxl/PluginApi.h"

#include <cstdint>
#include <vector>

struct WXL_SeyrisCdbcApi;

namespace wxl_livingazeroth::glass
{
    struct Settings
    {
        int   enabled = 1;              // everything below
        int   insideGlow = 1;           // seen from inside: the light coming through
        int   outsideReflection = 1;    // seen from outside: darker glass, the sky in it, the sun's glint
        int   fakeInteriors = 1;        // seen from outside: a room behind clear and leaded glass
        int   colouredShafts = 1;       // features/GlassShafts
        int   floorLight = 1;           // light through sunlit panes on the room's floor and walls
        float insideStrength = 1.0f;    // x the light coming through
        float reflectionStrength = 1.0f;// x the reflection and the glint
        float interiorStrength = 1.0f;  // x the room's light
        float shaftStrength = 1.0f;     // x the coloured shafts
        float floorStrength = 1.0f;     // x the floor light
        float range = 70.0f;            // yd from the camera: the panes the floor light and the shafts use
        int   roomVariety = 1;          // the rooms differ by building: tint, brightness, a few dark (RoomStyles.cdbc)
        int   litRooms = 1;             // at night, lamp-lit rooms behind the windows the client lights
        int   lightsOut = 1;            // ... more of them dark as the night goes on (inns stay lit late)
        int   roomLayouts = 1;          // shelves, curtains half drawn, a dividing wall with a doorway
        float varietyStrength = 1.0f;   // x how far the buildings' tints, brightness and dark rooms spread
        float litRoomStrength = 1.0f;   // x the lit rooms' brightness (1: about the client's own lit window)
        float lightsOutStrength = 1.0f; // x the share of rooms dark at night
        float layoutStrength = 1.0f;    // x the layouts' detail (the things on the shelves, the folds, the curtains)
    };
    Settings& Config();

    /// (Re)loads GlassMaterials.cdbc, GlassPanes.cdbc, RoomStyles.cdbc and GlassBuildings.cdbc (the lights panel's
    /// reload button calls it too).
    void Load(const WXL_SeyrisCdbcApi* cdbc);
    const char* Status();
    uint32_t    Generation();   // changes on every load

    /// One glass batch's constants (c192..c205, GlassShader.hpp): its material's look, seen from inside by `inside`
    /// (InsideWeight), in this light (Evaluate), the sun (toward it, world) and its colour, the sky's colour, the settings.
    /// The room is the plain warm one (as before room variety); RoomBatchConstants makes it a building's.
    void BatchConstants(const Material& m, float inside, const Light& light, const float sun[3], const float sunColour[3], const float sky[3],
                        const Settings& s, float out[4 * kConstantCount]);

    /// The room for one glass batch: its building's style (a default house when `building` is null), the night
    /// (the client's window fade, 0..1), the hour (0..24), whether the client lights this material at night (SIDN).
    struct RoomInputs { float night = 0.0f, hour = 12.0f; bool selfLit = false; uint32_t fallbackSeed = 0; };
    /// Rewrites the room's part of `out` (c198.w, c199.rgb, c202..c205) and fills `seed` (c212).
    void RoomBatchConstants(const RoomStyle& style, const Building* building, const Light& light, const RoomInputs& in, const Settings& s,
                            float out[4 * kConstantCount], float seed[4]);
    /// The built-in style (a human house) for buildings the tables don't have.
    const RoomStyle& DefaultStyle();
    /// The rooms' tables as loaded (for the tests and the panel).
    const BuildingIndex& Buildings();
    const RoomStyle* StyleById(uint32_t id);
    /// The hour (0..24) the rooms' lights go by: the climate panel's override, else the clock.
    float HourNow();

    /// The glass table's row for a texture path (any case or slashes): its index + 1, 0 when it isn't glass.
    uint32_t MaterialOf(const char* texturePath);
    const Material* MaterialAt(uint32_t indexPlusOne);
    /// What the floor light and the shafts use (for the tests and GlassShafts).
    const PaneIndex& Panes();

    /// Once per frame (Extension OnUpdate, main thread): the light of the moment and the panes near the camera.
    void Update();

    // --- features/WmoIce's batch hooks ------------------------------------------------------------------------
    /// The variants that carry the blocks (counted by features/ModelIce's WMO edit).
    void NotePatchedVariant();
    unsigned PatchedVariants();
    /// Around a placed instance's group (Wmo.RenderGroup), before its batch loops: where the instance stands (its
    /// origin, world; null when unknown), its WMO's path (PathHash, 0 unknown), the group's index, and a seed for a
    /// building the tables don't have (the instance's address: steady while it's loaded).
    void BeginInstance(const float pos[3], uint32_t path, uint32_t group, uint32_t fallbackSeed);
    void EndInstance();
    /// Around a group's batch loop: its MOGP flags, and whether its root has glass materials.
    void BeginGroup(uint32_t groupFlags, bool rootHasGlass);
    /// Per batch: its material's glass row (0 none), whether the rims hold c101.. for it, and whether the client
    /// lights its material at night (MOMT SIDN: the lamp-lit room).
    void Batch(uint32_t glassMaterial, bool rimsOn, bool selfLit);
    void EndGroup();
    /// The rims (WmoIce) just wrote c101..c172 (and c180..c189): the panes go up again before the next floor-light batch.
    void PaneRangeTaken();
    /// The ice (WmoIce) just wrote c180..c190 with its own view: ours go up again before the next floor-light batch.
    void ViewTaken();

    /// The coloured shafts' panes this frame: sunlit, whose room holds the camera (up to `max`), and the sun's
    /// light through them (colour x strength, per pane); false when there are none.
    struct ShaftPane { const Pane* pane; float colour[3]; };
    bool ShaftPanes(std::vector<ShaftPane>& out, size_t max);
    /// The sun this frame (toward it, world) and its colour x gain through glass.
    void SunNow(float dir[3], float colour[3]);

    /// The panel section (the lights panel) and one status line for the log; the rooms' line (styles, buildings, how
    /// many instances drawn were found in the tables, the dark share at night now).
    void DrawPanelSection(const WXL_Api* api);
    const char* StatusLine();
    const char* RoomLine();
}
