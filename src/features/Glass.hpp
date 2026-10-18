// Window glass, the runtime half (the rules: env/GlassModel; the shader blocks: features/GlassShader). The glass
// textures and their kinds come from GlassMaterials.cdbc, the panes from GlassPanes.cdbc (tools/gen_glass.py,
// docs/cdbc-tables.md).
//  - The look (b9): features/WmoIce's batch hooks ask, per batch, whether its material's texture is glass; this
//    sets the batch's constants (its kind's look, its mask, inside or outside) and switches the block on.
//  - The floor light (b10): each frame the sunlit panes nearest the camera (up to kFloorPanes) become projection
//    rows; the batches of a building's interior groups (not its glass, not a sand floor's rims) take them.
//  - The coloured shafts: features/GlassShafts marches the room's air with the panes whose room holds the camera.
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
    };
    Settings& Config();

    /// (Re)loads GlassMaterials.cdbc and GlassPanes.cdbc (the lights panel's reload button calls it too).
    void Load(const WXL_SeyrisCdbcApi* cdbc);
    const char* Status();
    uint32_t    Generation();   // changes on every load

    /// One glass batch's constants (c192..c201, GlassShader.hpp): its material's look, seen from inside by `inside`
    /// (InsideWeight), in this light (Evaluate), the sun (toward it, world) and its colour, the sky's colour, the settings.
    void BatchConstants(const Material& m, float inside, const Light& light, const float sun[3], const float sunColour[3], const float sky[3],
                        const Settings& s, float out[4 * kConstantCount]);

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
    /// Around a group's batch loop: its MOGP flags, and whether its root has glass materials.
    void BeginGroup(uint32_t groupFlags, bool rootHasGlass);
    /// Per batch: its material's glass row (0 none) and whether the rims hold c101.. for it.
    void Batch(uint32_t glassMaterial, bool rimsOn);
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

    /// The panel section (the lights panel) and one status line for the log.
    void DrawPanelSection(const WXL_Api* api);
    const char* StatusLine();
}
