// Clear ice on ice models (icebergs, shoreline ice, ice cliffs and shards, frozen waterfalls, Ulduar's
// ice walls): the same look as the icy ground (features/TerrainIce, whose per-frame values and
// detail texture it shares), worked out in the M2 pixel shaders.
//
// The client's M2 pixel shaders (the Combiners_* sets, recognized through render/M2Effects) are
// patched: right before the stock fog ending, an ice block behind static branch b7 works out the ice
// colour from the variant's own view-space position and normal inputs (the dynamic-shadow variants
// have them: texcoord1/2 for one texture, texcoord2/3 for two; the shadow-less variants don't and stay
// stock) and leaves the change in r31 (zero when b7 is off), added after the fog subtraction. The M2
// batch draws (M2.DrawBatch, M2.DrawBatchDoodad) switch b7 on for ice models only, so every other
// model draw skips the block and comes out exactly as before.
// Registers: r12..r31, b7, c174..c178 (defined), c179 (the batch's ice weight and style), c180..c189
// (the look), c190 (more of the style), c78..c81 (the crystals': features/Crystal; zeros for the ice), c77 (the weight's
// facing: features/WetSurfaces' wet buildings; zeros everywhere else, the weight as before), s14. Stock M2
// pixel shaders use r0..r11, c0..c14, s0..s7 and no b/i registers.
//
// The same block draws the crystals (features/Crystal): the batch-draw hooks switch it on for a crystal batch of any other
// model, with the crystal's constants and detail texture.
//
// Behind b8, after the ice: still rims around footprints on the sand and snow floors of buildings (features/WmoIce
// switches it on for those batches; the rims come from features/SurfaceCover's prints). The Caverns of Time are a
// building: "caverns of time sand still does not work for footprints sadly" (the user, 2026-10-11); the snowy caves
// (Ice Heart Cavern) are too. Each rim follows its print's own outline, as on the terrain's cover (CoverRipples.hpp's
// Shape; "yes can you do this?", the user, 2026-10-12). Up to kWmoRings rims in c101..c172 (three registers each,
// PackRim's with world coordinates; a rim not in use is zeros: the stock WMO and M2 pixel shaders read only c0..c14),
// c173, c222, c223 (defined: the band's phase, the coefficients' unpacking, the harmonics' orders), and c191 = the
// sun's world direction (x, y), the brightness gains of the rim's slope toward it and of its height. Ten rims were a
// walking trail of about four seconds: "snow rims work but fade too fast imo" (the user, 2026-10-12).
//
// Behind b9 and b10, after the rims, on the buildings' variants only: the window glass and the light through sunlit
// panes on a room's floor (features/GlassShader has their register map: c85, c86, c192..c205, c212, c213, c218 per
// batch, c83, c84, c87..c100, c206..c211 defined, and the rims' c101..c172 for the panes; features/Glass switches them per
// batch, from features/WmoIce's hooks). Taken in the buildings' variants so far: c83..c223 (c220, c221 defined: the terrain's ice
// uploads its own there, which the buildings' shaders don't see; c219 read only under b11, see-through windows' redraw);
// c0..c14 stock. With the glass the variant's alpha write goes to r12.w and oC0.w is written from it last, after the
// redraw's change (GlassShader's ReplayAlpha).
//
// Frozen waterfalls get their own style: brighter, glassier ice whose detail is streaked along the
// flow, at a scale that still reads from a distance (Bor's Fall in the Storm Peaks is 366 yd tall).
// The ice models and buildings (features/WmoIce) get a lighter one: their textures are already bright,
// saturated ice, so the texture's own colour stays the body and only the cracks, the sheen and the glint
// go on top.
#pragma once

#include "wxl/PluginApi.h"

#include <string>

namespace wxl_livingazeroth::modelice
{
    /// Registers the M2 pixel shader patch. From WXL_Load, before shaderpatch::Install().
    void Register();
    /// The batch-draw hooks. From WXL_Load.
    void Install(const WXL_Api* api);

    /// The pixel edit on its own (offline tests): the models' (the ice), the buildings' (the ice and the footprint rims).
    bool EditPixelForTests(std::string& src, std::string& why);
    bool EditPixelWmoForTests(std::string& src, std::string& why);
    /// The buildings' variants with the rims and the glass blocks (features/GlassShader: b9 the glass, b10 the floor
    /// light), as the runtime gives them to a device with kGlassMinSlots.
    bool EditPixelWmoGlassForTests(std::string& src, std::string& why);
    /// The buildings' variants carry the rims only on a device with this many pixel shader slots (they come to ~830;
    /// ps_3_0 promises 512): without them the client would get no shader at all.
    constexpr unsigned kRingsMinSlots = 1024;
    /// ... and the glass blocks as well only with this many (tests/glass reports the largest variant's slots).
    constexpr unsigned kGlassMinSlots = 2048;

    /// Wet (features/WetSurfaces): not ice at all but a building's wet stone, wood and roofs in rain, through the same block:
    /// the texture's own colour a little darker (c179.z), the sky by Fresnel and a broad glint, no detail (c190.z -1: the
    /// cracks and bubbles nothing, whatever the detail texture holds), weighted per pixel by its facing (c77).
    enum class Style : int { None = 0, Ice = 1, FrozenFall = 2, Light = 3, Floe = 4, Glacier = 5, SeaIce = 6, Wet = 7 };
    /// Which look a model path (stem or file name, any case or slashes) gets: none, light (the ice models),
    /// glacier (Dragonblight's ice rocks, shards, cliffs and icebergs), sea ice (the floating shoreline floes), or
    /// frozen fall. (Floe is a building texture's: features/WmoIce.)
    Style StyleOf(const char* path);
    /// Whether a model path is one of the ice models (any style).
    bool IsIceModel(const char* path);
    /// Whether a model path is a waterfall "hat" (the sheet on the river above a fall: "waterfall" and "_hat" in
    /// the file name). The batch draws skip them while SetHideHats(true) (a test switch); otherwise, with
    /// SetFadeHats(true), the hats' upstream ends fade in over the river instead of ending in a jagged edge.
    bool IsWaterfallHat(const char* path);
    void SetHideHats(bool hide);
    bool HideHats();
    void SetFadeHats(bool fade);
    bool FadeHats();
    /// The hats' fade in an M2 vertex shader (behind VS b15, c250, using r28.w), right before its colour write;
    /// `stock` is the client's own text (the clash check). True when it went in. The model lights' vertex rule
    /// applies it (ShaderPatch applies one table rule per shader).
    bool EditVertexHatFade(std::string& src, const std::string& stock);
    /// One batch's constants for a style: the shared look (terrainice::FillModelConstants, c180..c189)
    /// adjusted in place, c179 = (weight, vertical stretch, extra brightness, extra reflection) and
    /// c190 = (the texture's own colour as the body, how much of the cloudy depth stays on it, extra
    /// crack strength, 0), where zeros leave the shared look exactly as it is.
    void StyleConstants(Style style, float weight, float* look, float c179[4], float c190[4]);

    constexpr int kWeightConstant = 179;
    constexpr int kLookConstant = 180;
    constexpr int kStyleConstant = 190;
    constexpr int kSwitch = 7;   // PS bool register b7
    constexpr int kRingSwitch = 8;     // PS bool register b8: the footprint rims
    constexpr int kRingControl = 191;  // sun x, y (world), slope gain, height gain
    constexpr int kRingConstant = 101; // per rim, three registers: CoverRipples.hpp's PackRim, about the world's origin
    constexpr int kWmoRings = 24;      // c101..c172

    /// The rims block drawn by the GPU (a shader of just the block, on a device of its own, tests) against CoverRipples'
    /// RimShade for a boot, a mirrored paw and an oval, two of them overlapping: height and slope. A line saying how
    /// close they came, starting with "FAIL " when they don't agree.
    std::string RingBlockSelfTest(void* device);

    void SetEnabled(bool enabled);
    bool Enabled();
    /// Pixel shader variants (M2 and WMO: both are recorded effects) that carry the ice block.
    unsigned PatchedVariants();
    const char* StatusLine();
}
