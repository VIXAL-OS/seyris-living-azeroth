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
    /// Effects at units' feet (Base attachment: auras) lifted onto the cover.
    void SetLiftBaseEffects(bool on);
    bool LiftBaseEffects();
    /// The trench edge filter: no trench wall or rim narrower than this many 0.25 yd cells (0..4; 0 = the original).
    void  SetTrenchSoftness(float cells);
    float TrenchSoftness();
    /// How much faster mud ruts fill in in heavy rain (0..1: 0.75 = in a quarter of their relax time).
    void  SetRainFillsRuts(float share);
    float RainFillsRuts();

    /// Loads SurfaceCover.cdbc (which ground gets cover, how deep) through the cdbc reader.
    void LoadTable(const void* cdbcApi);

    /// Once per frame on the main thread, after the world snapshot and the actors are refreshed.
    void Update(float dt, const world::Snapshot& snap);

    /// The top of the cover as drawn at a world position (base + depth, level 0 with its trenches,
    /// bilinear over the finest drawn level holding it). False where no cover is drawn: off, outside
    /// the grid, a hole, or no depth there (z is then the terrain the cover sits on, if known).
    bool TopAt(float x, float y, float& z);

    /// Before IDirect3DDevice9::Reset: releases what has to go first (its textures are managed).
    void OnDeviceLost();

    /// Last drawn frame: the most point lights any patch had, and how many patches had more than
    /// lights::kMaxLights (the nearest to the camera kept).
    void PatchLightStats(unsigned& most, unsigned& full);
    /// The terrain's ambient and sun/moon (diffuse) light as the cover last read them from the terrain's shader (this
    /// frame or the last one it drew in, up to ten minutes ago); false when not seen. The cloud deck is lit with them.
    bool TerrainLight(float ambient[3], float diffuse[3]);

    /// Whether the client's footprints go on top of the cover now (features/CoverPrints): the setting is
    /// on and the near level was drawn in this scene's terrain stage, before the prints.
    bool PrintsOnCover();
    /// Rims around prints on building floors (features/WmoIce draws them on sand and snow floors): up to `max`, the
    /// newest first, kWmoRingRegisters registers each (CoverRipples.hpp's PackRim, in world coordinates), for a snow
    /// floor (kWmoSnowRelaxSeconds) or a sand one (sand's 10 s). Returns how many.
    constexpr int kWmoRingRegisters = 3;
    /// Snow fills a print back in over this long (x the relax setting) on building floors: its rims last 2.5 x that,
    /// 90 s, where sand's last 25 s ("snow rims work but fade too fast imo", the user, 2026-10-12).
    constexpr float kWmoSnowRelaxSeconds = 36.0f;
    unsigned WmoRings(float* out, unsigned max, bool snow);
    /// The rims' shading as the panel sets it (the terrain's cover uses them as they are): the steepest slope's tilt of
    /// the normal and the crest's brightening.
    void RingShading(float& slope, float& brightness);
    /// The rims around prints in sand drawn by the GPU (the cover's own RippleRings, on a device of their own, tests)
    /// against CoverRipples' RimShade: a boot, a mirrored paw and an oval, two of them overlapping. A line saying how
    /// close they came, starting with "FAIL " when they don't agree or a rim reaches past where the shader looks.
    std::string RimShaderSelfTest(void* device);

    /// How far the cover drawn in this scene rises above the terrain at world (x, y), where the terrain
    /// is at terrainZ (yd): its top there as the near level draws it (trenches included), 0 where none
    /// is drawn (no cover, thinner than drawn, outside the near level, or PrintsOnCover() is false).
    float DrawnCoverAbove(float x, float y, float terrainZ);
    /// The stock fog the terrain stage left on the device, as last read there (only while the cover
    /// draws in that pass with scene lighting on): VS c12 = (x, y, exponent, 0) with fog =
    /// min(pow(max(viewZ * x + y, 0), exponent), 1), and PS c2 = the fog colour. False if never read.
    bool SeenSceneFog(float vsFog[4], float psFogColor[4]);
}
