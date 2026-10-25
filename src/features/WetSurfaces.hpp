// Wet surfaces in rain (Lumenveil's queue #3, docs/r&d/lumenveil-cross-pollination.md; their idea, none of their code):
// stone and soil darken and shine while it rains and for a while after, puddles gather in low, flat ground and mirror the
// sky and our lamps; grass, snow and ice don't. The rules: env/WetSurfaceModel.
//
// The terrain: inside the patched terrain pixel shaders (features/TerrainWetness, one rule per shader), sharing its
// moisture block, the clear ice's layer weights and the point lights' loop rather than drawing again:
//  - the surface texture (render/MoistureTexture BindSurface, s15: the moisture grid's layout; env/Fields fills it every
//    step) gives the film (its zone's x open sky: env/Shelter's roof checks, so nothing under roofs or indoors) and the
//    puddle potential; beyond the grid the film is the player's zone's (c161.w) and there are no puddles;
//  - each chunk's four layers' shine (c160, set at the chunk draw with the ice's mask: GroundMaterial WetShine along the
//    material's chain, else the layer's TerrainType: grass and leaves 0, stone 1), by the pixel's layer weights, scales
//    both (r25.z);
//  - the film and puddles darken the ground with the soil's own wetness; before the lights' loop the view's reflection
//    about the ground normal and the sun's glint along it; in the loop every light's highlight (a broad sheen on wet
//    ground, a sharp image in a puddle); after the lights the sky (the fog colour, c2) by Fresnel and the highlights.
//  Registers: r1..r6 (the stock's temps, dead by the fog ending), c160..c167, s15. 40 slots on the largest variant, with
//  the terrain wetness's debug views moved out of the shader (9 slots back): 475 -> 506 of 512.
//
// Buildings: features/ModelIce's block (b7) in a style of its own (ModelIce Style::Wet: the texture's own colour darker, the
// sky by Fresnel, a broad glint, no cracks), switched on by features/WmoIce for the stone, wood and roof batches of
// exterior groups, and of interior groups as far as the roof checks see the sky over their floor (Stormwind's streets
// and canals are interior groups, as its shops are); glass, ice, crystals, lights and foliage never. Weighted per pixel by
// its facing (c77: floors full, walls kWallShare, ceilings none: 2 slots, 2027 -> 2029 of 2048).
#pragma once

#include "../debug/MemoryUse.hpp"
#include "../env/TerrainHeight.hpp"
#include "../env/WetSurfaceModel.hpp"
#include "../env/WorldQuery.hpp"
#include "wxl/PluginApi.h"

#include <d3d9.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wxl_livingazeroth::wetsurfaces
{
    // --- the terrain shaders ---------------------------------------------------------------------------------------------
    /// The pieces features/TerrainWetness's edit puts in: `texel` after its moisture value (r30.x, before the ice's scale:
    /// r1.x = the wetness, r1.y = the puddle, their darkening onto r30.x), `beforeLoop` once the ground normal r24 is known
    /// (the reflection r2, the lobe and gain r3, the sun's glint into the highlights' sum r4), `inLoop` (each light's
    /// highlight into r4), `after` the lights (the sky, then the highlights onto `colour`), and `declarations` (s15).
    struct TerrainPieces
    {
        std::string texel, beforeLoop, inLoop, after, declarations;
    };
    TerrainPieces TerrainShader(const std::string& colour);
    /// Shader creation: a terrain variant took the pieces, or why not.
    void NoteVariant(bool withWet, const std::string& whyNot);
    unsigned PatchedVariants();
    /// Tests: the terrain edit without the pieces (and without the layer mask), to count what they cost. On by default.
    bool PiecesOn();
    void SetPiecesForTests(bool on);

    // --- the frame -------------------------------------------------------------------------------------------------------
    wetsurf::Settings& Config();

    /// Once per frame, after storm::Update and regional::Update, before fields::Update (whose step reads the zones' film).
    void Update(float dt, const world::Snapshot& snap);
    /// The zones' film, for env/Fields (fields::SetZoneFilm). 0 for a zone without a record.
    float ZoneFilm(int map, uint32_t zone);

    /// Right before the terrain stage (from features/TerrainWetness's, with whether its moisture texture is bound): the
    /// surface texture on s15 and c160..c167. Off, or everything dry: zeros (the ground exactly as before).
    void BeforeTerrainStage(IDirect3DDevice9* device, bool moistureBound);
    /// Whether this terrain stage wants each chunk's layers' shine (features/TerrainIce's chunk draw sets c160).
    bool StageOn();
    void NoteChunk(bool anyShine);
    /// A terrain layer's shine (0..1): its material's WetShine (along its chain), else its TerrainType's; none for clear
    /// ice, snow and hot ground. `material`: GroundMaterialSelector's for the layer (0 = none).
    float LayerShine(uint32_t material, const terrain::Surface& surface, int map);

    // --- buildings ---------------------------------------------------------------------------------------------------------
    /// Whether wet buildings are on this frame (on, buildings on, the player's zone wet, the WMO variants patched).
    bool BuildingsActive();
    /// A WMO material's shine (0..1): its first texture's name (wetsurf::WmoTextureShine); 0 for blended, additive,
    /// unlit or night-lit materials (MOMT blend > 1, flags 0x1 or 0x10).
    float BuildingShine(const char* texture, uint32_t blend, uint32_t flags);
    /// The block's constants for a wet batch of this shine: features/ModelIce's look (terrainice::FillModelConstants)
    /// adjusted in place, c179, c190 (ModelIce Style::Wet, then the intensity and the sun) and c77 (the facing).
    void BuildingConstants(float shine, float* look, float c179[4], float c190[4], float c77[4]);
    /// The block's detail texture for wet batches (1 x 1; the wet style reads none of it).
    IDirect3DTexture9* BuildingDetail(IDirect3DDevice9* device);
    void NoteBuildingBatch();

    // --- testing, the panel, the log --------------------------------------------------------------------------------------
    /// The player's zone soaked (film and puddles full) or dry at once; it goes on from there by the rules.
    void SoakNow();
    void DryNow();
    /// Tests: the player's zone (map, zone) with this film and pool, as Update would leave it.
    void SetZoneForTests(int map, uint32_t zone, float film, float pool);
    /// One line: the state, the shaders, what drew last frame (the 30 s log line, the panel).
    const char* StatusLine();
    /// Lines for the panel and the F11 snapshot: the zones, the spot under the player, the counts that prove it runs.
    void StatusLines(std::vector<std::string>& out);
    /// The climate panel's section: the status, the settings, the test buttons.
    void DrawPanelSection(const WXL_Api* api);

    /// Its Direct3D resources (the 1 x 1 detail), for the memory readout. The surface texture is render/MoistureTexture's.
    void GpuMemory(memuse::Gpu& out);
}
