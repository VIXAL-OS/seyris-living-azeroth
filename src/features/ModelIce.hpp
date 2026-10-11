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
// (the look), c190 (more of the style), s14. Stock M2 pixel shaders use r0..r11, c0..c14, s0..s7 and no
// b/i registers.
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

    /// The pixel edit on its own (offline tests).
    bool EditPixelForTests(std::string& src, std::string& why);

    enum class Style : int { None = 0, Ice = 1, FrozenFall = 2, Light = 3, Floe = 4, Glacier = 5 };
    /// Which look a model path (stem or file name, any case or slashes) gets: none, light (the ice models),
    /// glacier (Dragonblight's ice rocks, shards, cliffs and icebergs), floe (the flat shoreline floes), or
    /// frozen fall.
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

    void SetEnabled(bool enabled);
    bool Enabled();
    /// Pixel shader variants (M2 and WMO: both are recorded effects) that carry the ice block.
    unsigned PatchedVariants();
    const char* StatusLine();
}
