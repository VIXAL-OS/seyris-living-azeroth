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
// (the look), s14. Stock M2 pixel shaders use r0..r11, c0..c14, s0..s7 and no b/i registers.
//
// Frozen waterfalls get their own style: brighter, glassier ice whose detail is streaked along the
// flow, at a scale that still reads from a distance (Bor's Fall in the Storm Peaks is 366 yd tall).
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

    enum class Style : int { None = 0, Ice = 1, FrozenFall = 2 };
    /// Which look a model path (stem or file name, any case or slashes) gets: none, ice, or frozen fall.
    Style StyleOf(const char* path);
    /// Whether a model path is one of the ice models (any style).
    bool IsIceModel(const char* path);
    /// One batch's constants for a style: the shared look (terrainice::FillModelConstants, c180..c189)
    /// adjusted in place, and c179 = (weight, vertical stretch, extra brightness, extra reflection), where
    /// zeros leave the shared look exactly as it is.
    void StyleConstants(Style style, float weight, float* look, float c179[4]);

    constexpr int kWeightConstant = 179;
    constexpr int kLookConstant = 180;
    constexpr int kSwitch = 7;   // PS bool register b7

    void SetEnabled(bool enabled);
    bool Enabled();
    const char* StatusLine();
}
