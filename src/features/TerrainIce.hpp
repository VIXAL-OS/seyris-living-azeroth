// Clear ice: ground textures whose material has the ClearIce flag (features/GroundMaterialTable,
// e.g. Storm Peaks' SP_Ice, Winterspring's glacier, Icecrown's frozen bones) are drawn as deep,
// clear ice. The terrain is opaque and drawn before the water, so the depth is faked:
//  - cracks, bubbles and cloudiness from a small detail texture, sampled twice at two depths below
//    the surface along the view (parallax), so they seem to sit inside the ice;
//  - a deep blue body lit like the surface, which shows through by the "clarity";
//  - the sky's colour reflected with a Fresnel term, and a sharp glint of the sun.
//
// The client's terrain pixel shaders are patched inside features/TerrainWetness's edit (one patch rule
// per shader): the variant's combined alpha sample (x/y/z = layers 1..3) is copied to r25 right after
// it's read, and before the wet block the ice weight is worked out from it, the stock way that variant
// blends its layers (in sequence or summed), with the chunk's per-layer ice mask (c215). The chunk
// draw (Adt.SurfaceChunkDrawShader) sets that mask from the draw node's layer textures.
// Registers: r25 (kept), r10..r22 (free again for the point lights after), c190..c193 (defined),
// c215..c222 (ours), s14 (detail texture). Stock terrain shaders use r0..r9, c0..c13, s0..s8.
#pragma once

#include "../debug/MemoryUse.hpp"
#include "wxl/PluginApi.h"

#include <d3d9.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wxl_livingazeroth::terrainice
{
    /// How a stock terrain pixel shader variant blends its layers.
    enum class Blend { Single, Chain, Sum };
    struct Variant
    {
        int         layers = 0;   // 1..4
        Blend       blend = Blend::Single;
        std::string alpha;        // the register its combined alpha map is read into ("" for one layer)
    };

    /// Shader edits (called by terrainwet's pixel edit, before it inserts anything else). Prepare reads
    /// the variant (layer samplers s0..s(n-1), the combined alpha map at s(n), its blend form) and
    /// inserts the copy of the alpha sample into r25 right after it. False: this variant gets no ice
    /// (the wet terrain and lights still go in).
    bool        Prepare(std::string& src, Variant& variant, std::string& why);
    /// r25.x = ice weight (x c216.w), r25.y = 1 - it. Goes before the wet block (temps r10..r12).
    std::string WeightBlock(const Variant& variant);
    /// The wet block's darkening, times r25.y (no wet look on ice).
    std::string WetScale();
    /// The ice look on colour register r. After the wet block, before the point lights (r10..r22).
    std::string LookBlock(const std::string& r);
    /// def constants and the detail sampler's declaration.
    std::string Definitions();
    /// For the panel: a variant prepared, or why not.
    void        NoteVariant(bool withIce, const std::string& why);

    /// The detail texture's texels (A8R8G8B8, size x size, tiling): r = cracks, g = bubbles, b = cloud.
    void BuildDetail(std::vector<uint32_t>& texels, int size);

    /// The look as the ice models use it (features/ModelIce), worked out at this frame's terrain stage:
    /// PS c180..c189 (camera; the view's rows; the sun in view space; glint; deep body and clarity;
    /// depths and detail scales; highlight and sky reflection; debug tint).
    constexpr int kModelConstantCount = 10;
    void FillModelConstants(float constants[4 * kModelConstantCount]);
    /// The shared detail texture (created on first use for this device), or null.
    IDirect3DTexture9* DetailTexture(IDirect3DDevice9* device);

    /// From WXL_Load: the chunk-draw hook.
    void Install(const WXL_Api* api);
    /// Right before the terrain stage (the cover's terrain-stage hook, after terrainwet's).
    void BeforeTerrainStage(IDirect3DDevice9* device);

    void  SetEnabled(bool enabled);
    bool  Enabled();
    /// How strongly icy ground turns to clear ice, 0..1.
    void  SetStrength(float strength);
    float Strength();
    /// How much of the deep body shows through the surface texture, 0..1.
    void  SetClarity(float clarity);
    float Clarity();
    /// Tints the ground it treats as ice magenta.
    void  SetDebug(bool on);
    bool  Debug();

    const char* StatusLine();

    /// Its Direct3D resources, for the memory readout (debug/Memory).
    void GpuMemory(memuse::Gpu& out);
}
