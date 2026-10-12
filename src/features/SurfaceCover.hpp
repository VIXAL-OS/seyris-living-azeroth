// Deformable surface cover, first spike: a dense mesh around the player that sits ON TOP of the
// terrain (snow/sand/mud as a real volume), lifted per vertex by vertex texture fetch.
//
// Two toroidal R32F textures, one texel per mesh vertex (0.25 yd), indexed by world cell:
//   - base height: the drawn terrain surface (terrain::HeightAt), holes marked;
//   - cover depth: a test depth (everywhere, or only on TerrainTypes with the footprint flag),
//     pressed down by the player and nearby units into trenches with a rim, relaxing over time.
// Drawn at the end of the world scene against the scene depth, plain-lit, to judge the look, the
// seams with the terrain and the cost. See orchestration docs/r&d/immersion/deformable-surfaces.md.
#pragma once

#include <string>

#include "wxl/PluginApi.h"

#include "../env/WorldQuery.hpp"

namespace wxl_livingazeroth::cover
{
    void Install(const WXL_Api* api);

    /// Start-up settings (WarcraftXL.ini); the panel can change them at runtime.
    void Configure(bool enabled, int levels, bool trenches, float depthMultiplier, float fillBudgetMs);
    bool  Enabled();
    int   Levels();
    bool  Trenches();
    float DepthMultiplier();
    float FillBudgetMs();

    /// Loads SurfaceCover.cdbc (which ground gets cover, how deep) through the cdbc reader.
    void LoadTable(const void* cdbcApi);

    /// Once per frame on the main thread, after the world snapshot and the actors are refreshed.
    void Update(float dt, const world::Snapshot& snap);

    /// Last drawn frame: the most point lights any patch had, and how many patches had more than
    /// lights::kMaxLights (the nearest to the camera kept).
    void PatchLightStats(unsigned& most, unsigned& full);

    /// Whether the client's footprints go on top of the cover now (features/CoverPrints): the setting is
    /// on and the near level was drawn in this scene's terrain stage, before the prints.
    bool PrintsOnCover();
    /// Rims around prints on building floors (features/WmoIce draws them on sand and snow floors): up to `max`, the
    /// newest first, kWmoRingRegisters registers each (CoverRipples.hpp's PackRim, in world coordinates). Returns how
    /// many.
    constexpr int kWmoRingRegisters = 3;
    unsigned WmoRings(float* out, unsigned max);
    /// The uploads of only the changed cells against a full upload of the same data, read back from the GPU (tests,
    /// on a device of their own): a level of random data, then scattered cells, a row strip and a column strip
    /// (across the wrap) changed. Empty when what the GPU holds agrees bit for bit; else what differed.
    std::string PartialUploadSelfTest(void* device);
    /// The rims around prints in sand drawn by the GPU (the cover's own RippleRings, on a device of their own, tests)
    /// against CoverRipples' RimShade: a boot, a mirrored paw and an oval, two of them overlapping. A line saying how
    /// close they came, starting with "FAIL " when they don't agree or a rim reaches past where the shader looks.
    std::string RimShaderSelfTest(void* device);

    /// How far the cover drawn in this scene rises above the terrain at world (x, y), where the terrain
    /// is at terrainZ (yd): its top there as the near level draws it (trenches included), 0 where none
    /// is drawn (no cover, thinner than drawn, outside the near level, or PrintsOnCover() is false).
    float DrawnCoverAbove(float x, float y, float terrainZ);
}
