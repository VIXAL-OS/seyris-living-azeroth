// Internals of the surface cover (features/SurfaceCover.hpp), shared by its files:
//   SurfaceCover.cpp          public API and the per-frame update
//   SurfaceCoverFill.cpp      what the table says per spot, sampling, fill jobs
//   SurfaceCoverTrenches.cpp  trenches under units
//   SurfaceCoverUpload.cpp    cover texture slots, folding and texture uploads
//   SurfaceCoverDraw.cpp      GPU objects, the draw and its hooks
//   SurfaceCoverShaders.cpp   the cover's HLSL
//   SurfaceCoverCamera.cpp    the 3rd-person camera kept above the cover
//   SurfaceCoverLift.cpp      models at units' feet (Base attachment) lifted onto the cover
//   SurfaceCoverPanel.cpp     the debug panel
//   SurfaceCoverRooms.cpp     no cover inside buildings (features/CoverRooms.hpp): the buildings drawn, their rooms
// Design and history: orchestration docs/r&d/immersion/deformable-surfaces.md.
#pragma once

#include "SurfaceCover.hpp"
#include "SurfaceCoverTable.hpp"
#include "CoverBlend.hpp"
#include "CoverPrints.hpp"
#include "CoverRipples.hpp"
#include "TerrainIce.hpp"

#include "../env/TerrainHeight.hpp"

#include "wxl/PluginApi.h"

#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace wxl_livingazeroth::cover::detail
{
    constexpr const char* kTag = "wxl-seyris-living-azeroth";
    constexpr const char* kPanelTitle = "wxl-seyris-living-azeroth: surface cover (spike)";

    // --- grid ------------------------------------------------------------------------------
    // A clipmap: every level is the same 320x320-cell grid (4 patches of 160x160 cells, 161x161
    // vertices, 16-bit indices), level k with cells of 0.25 * 2^k yd, so the levels reach 40, 80,
    // 160, 320 and 640 yd from the player. Only level 0 deforms (trenches); the coarser ones carry the
    // static cover. Each level has its own toroidal textures, one texel per world cell of that
    // level; 384 > 321 so a slot is never shared by two vertices of the grid at once.
    //
    // Seams: every level's origin snaps to an even cell, so its border lies on the next coarser
    // level's vertex lines. In a band along the border the odd vertices morph onto their even
    // neighbours, which turns the edge into exactly the coarser level's triangles; the coarser
    // level discards its pixels inside the finer level's box. In the band the normals switch to the
    // coarser level's spacing too, or the two levels shade the same surface differently.
    constexpr float kCell0 = 0.25f;
    constexpr int   kMaxLevels = 5;
    constexpr int   kPatchCells = 160, kPatchVerts = kPatchCells + 1, kPatches = 2;
    constexpr int   kGridCells = kPatchCells * kPatches, kGridVerts = kGridCells + 1, kHalfCells = kGridCells / 2;
    constexpr int   kTex = 384;
    constexpr int   kApron = 2;                // cells filled beyond the grid on every side (border normals)
    constexpr int   kFilled = kGridVerts + 2 * kApron;
    static_assert(kFilled <= kTex, "the filled area must fit the toroidal texture");
    constexpr int   kRecentreCells = 16;       // recentre after the player moved 16 cells of the level
    constexpr int   kMorphCells = 16;          // morph band along a level's border
    constexpr int   kDeformFullCells = 120;    // level 0: trenches at full strength inside this (Chebyshev, cells)...
    constexpr int   kDeformZeroCells = 140;    // ...gone by this, before the morph band starts
    constexpr float kOuterFadeStart = 0.80f, kOuterFadeEnd = 0.97f; // outermost level: cover fades out (share of its half)
    constexpr float kHole = -100000.0f;        // base height of a hole / unloaded spot

    // "Everywhere" (test): no painted cover, but fallen snow may lie anywhere (with the plain look);
    // the panel's deposit button puts some down.
    enum class Coverage : int { Everywhere = 0, Table = 1 };

    struct Settings
    {
        int   enabled = 1;
        int   coverage = static_cast<int>(Coverage::Table);
        int   levels = 5;               // 1..5: 40, 80, 160, 320, 640 yd
        // Player/testing multipliers on what SurfaceCover.cdbc says; 1 = exactly the table.
        float depthMul = 1.0f, driftMul = 1.0f, breakupMul = 1.0f, rimMul = 1.0f, relaxMul = 1.0f;
        int   stamp = 1;
        float stampScale = 1.0f;        // trench radius x the unit's size
        float farLift = 0.75f;          // yd added far away, against terrain drawn simpler than its heights
        int   sceneLight = 1;           // light and fog the cover like the terrain just drawn (in-pass only)
        int   shadows = 1;              // the terrain's dynamic shadows on the cover (in-pass, lit like the scene)
        float brightness = 0.93f;       // cover albedo
        float fillBudgetMs = 2.0f;      // sampling work per frame for fills and recentres
        int   materialSource = 0;       // 0 the painted strength of every layer (alpha maps), 1 the dominant layer per cell
        int   matchGround = 1;          // sand, ash and mud over mixed ground take the other layers' colour (blend::MatchGround)
        int   alphaSwap = 0;            // debug: read the alpha maps with rows and columns swapped
        int   wireframe = 0;
        int   drawPoint = 0;            // 0 right after the terrain (inside the world pass), 1 end of the scene
        int   debugView = 0;            // 0 off, 1 cover texture share, 2 specular only, 3 lighting only
        int   decals = 1;               // the client's projected textures (selection circles, spell ground effects) on the cover too
        int   camera = 1;               // the 3rd-person camera can't sink into the cover
        float cameraClearance = 0.4f;   // yd the camera's boom keeps above the cover (its near plane)
        int   liftBase = 1;             // models on units' Base attachment (auras at the feet) sit on the cover
        // ChromieCraft fork:
        int   printsOnCover = 1;        // the client's footprints drawn on top of the cover (features/CoverPrints)
        int   ripples = 1;              // covers with kFlagRipplePrints (sand): rings around prints, no trenches
        // The rings' steepest slope (shading) and how much their crest lightens: 0.5 and 0.1 read only up close ("can
        // we make the rings slightly more intense", the user, 2026-10-12, on Azuremyst's sand).
        float rippleSlope = 0.9f;
        float rippleBright = 0.18f;
        int   slither = 1;              // bodies without legs (snakes, naga, worms) leave a weaving groove, not prints
        // The client's prints on sand it counts as dirt (features/CoverPrints: Silithus's dunes are TerrainType Dirt,
        // where the client puts no print down, so the sand's rims never formed there).
        int   sandPrints = 1;
        // The trench edge filter (blend::TrenchProfile): no trench wall or rim narrower than this many level-0 cells
        // (0.25 yd), or the grid draws a walked trench's edge as a staircase. 0 = the original widths.
        float trenchSoftness = 1.5f;
        // Mud ruts (moisture-gated covers) fill in this much faster in heavy rain: 0.75 = in a quarter of their relax
        // time (blend::RainRelaxShare). 0 = rain changes nothing.
        float rainFill = 0.75f;
        // No cover where the terrain lies inside a building's room (features/CoverRooms.hpp): drawn before the WMOs, the
        // cover poked through floors lying within its depth over the terrain, seen only while a way out was on screen.
        int   keepOutOfRooms = 1;
    };

    struct Level
    {
        float cell = kCell0;
        std::vector<float>   base = std::vector<float>(kTex * kTex, kHole);
        // Coverage parts per slot, from the table rows of the terrain cells around (blended),
        // combined at upload (so the panel's multipliers need no refill): how covered (0..255,
        // soft across terrain cells), the table's depth (yd), the slope factor (0..255, from the
        // terrain's steepness and the rows' MaxSlope/SlopeFade), the rows' drift and breakup
        // amounts (0..255 = 0..1), the drift and edge noise (0..255), and the tint (ARGB, A =
        // strength).
        std::vector<uint8_t>  material = std::vector<uint8_t>(kTex * kTex, 0);
        std::vector<float>    depth = std::vector<float>(kTex * kTex, 0.0f);
        std::vector<uint8_t>  slope = std::vector<uint8_t>(kTex * kTex, 255);
        std::vector<uint8_t>  driftAmp = std::vector<uint8_t>(kTex * kTex, 0);
        std::vector<uint8_t>  breakup = std::vector<uint8_t>(kTex * kTex, 0);
        std::vector<uint8_t>  stiff = std::vector<uint8_t>(kTex * kTex, 0); // cover material's stiffness (level 0's trenches)
        // 1 = the cover's base sits below the terrain (ZOffset < 0, e.g. mud whose rims show through):
        // written as a negative depth, so the VS lowers it far away instead of lifting it.
        std::vector<uint8_t>  sink = std::vector<uint8_t>(kTex * kTex, 0);
        // Moisture-gated covers (MoistureMin, e.g. mud): the share of the painted cover that needs
        // moisture (0..255), the moisture it needs and the ground's rest moisture there (0..255 =
        // 0..1; the rest stands in for the moisture beyond the near grid). Applied at fold, which
        // re-runs every few seconds while the moisture data changes (wetCells: slots with a share).
        std::vector<uint8_t>  wetGate = std::vector<uint8_t>(kTex * kTex, 0);
        std::vector<uint8_t>  wetMin = std::vector<uint8_t>(kTex * kTex, 0);
        std::vector<uint8_t>  wetRest = std::vector<uint8_t>(kTex * kTex, 0);
        int                   wetCells = 0;
        // Fallen snow (regional-layer-and-snow.md): how much of the spot may hold it (0..255, the
        // share of bare and snow-covered layers, none on or beside other covers, none under
        // liquids), and the share of the painted depth that is snow (it melts by the near grid's
        // kept share). Both static; the fallen depth and kept share are folded in at upload.
        std::vector<uint8_t>  snowShare = std::vector<uint8_t>(kTex * kTex, 0);
        std::vector<uint8_t>  paintedSnow = std::vector<uint8_t>(kTex * kTex, 0);
        uint32_t snowVersion = 0;       // the snow values last folded in (fields::SnowVersion)
        bool     snowFolded = false;    // whether that fold had any snow in it
        double   foldMs = 0;            // the last full depth pass (upload)
        std::vector<uint8_t>  drift = std::vector<uint8_t>(kTex * kTex, 128);
        std::vector<uint8_t>  edgeNoise = std::vector<uint8_t>(kTex * kTex, 128);
        std::vector<uint32_t> tint = std::vector<uint32_t>(kTex * kTex, 0);
        // The cell's two strongest cover textures, by table ID (lo/hi 16 bits) and share (lo/hi
        // byte). Turned into per-slot weights at upload, with whatever IDs hold the slots then.
        std::vector<uint32_t> texIds = std::vector<uint32_t>(kTex * kTex, 0);
        std::vector<uint16_t> texShares = std::vector<uint16_t>(kTex * kTex, 0);
        // A = wetness, RGB = the terrain's vertex colour where rows use it (0x80 = neutral).
        std::vector<uint32_t> props = std::vector<uint32_t>(kTex * kTex, 0x00808080u);
        uint32_t slotsUploaded = 0;     // the slot generation the weight textures were built with
        bool lookDirty = true;
        int boostRows = 0;              // rows left to re-sample at full speed after a setting change
        bool  haveGrid = false;         // drawn: its grid at gridI/gridJ is filled
        // A fill in progress, worked through within the per-frame budget. The level keeps drawing
        // its current grid meanwhile (unless `hidden`: a new place, nothing valid to show), and
        // switches to the job's origin when it's done. The texture has 59 spare rows/columns, so
        // the strips being filled never overwrite cells still on screen.
        struct Segment { int i0, i1, j; };
        std::vector<Segment> job;
        size_t jobPos = 0;
        int    jobI = 0, jobJ = 0;
        bool   jobActive = false, jobHidden = false, jobFull = false;
        double jobMs = 0;
        int   gridI = 0, gridJ = 0;     // world cell index (of this level) of the grid's first vertex
        int   refreshRow = 0;
        // Whole-texture passes, for changes that touch every slot (a refill, a setting, snow,
        // the cover texture slots) ...
        bool  baseDirty = true, coverDirty = true;
        // ... and changed regions, per texture row and column (kDirty* bits): a recentre's new
        // strips (whole rows, or columns for strips along y) and re-sampled rows. Folded and uploaded
        // on their own once no fill job is running (the slots then map to the grid that draws them).
        std::vector<uint8_t> dirtyRow = std::vector<uint8_t>(kTex, 0);
        std::vector<uint8_t> dirtyCol = std::vector<uint8_t>(kTex, 0);
        bool  anyCover = true;          // its last cover pass found cover somewhere (else it isn't drawn)
        int   coverCells = 0;           // cells with cover of their own (depth or painted snow), kept as they're sampled
        int   snowCells = 0;            // cells where fallen snow could settle (snow share)
        unsigned holes = 0;
        // Cells inside a building's room (SurfaceCoverRooms.cpp): sampled without cover. And the rows to re-sample where a
        // building came, moved or the setting changed (world cells of this level; clamped to the grid when worked).
        std::vector<uint8_t> inRoom = std::vector<uint8_t>(kTex * kTex, 0);
        int    roomCells = 0;
        std::vector<Segment> redo;
        size_t redoPos = 0;
        double   fillMs = 0, firstFillMs = 0;
        IDirect3DTexture9* baseTex = nullptr;
        IDirect3DTexture9* coverTex = nullptr;
        IDirect3DTexture9* lookTex = nullptr;  // A8R8G8B8 tint per slot (when the GPU can fetch it in the VS)
        IDirect3DTexture9* texWTex[2] = {};    // A8R8G8B8 cover texture weights (pixel shader, always available)
        IDirect3DTexture9* propsTex = nullptr; // A8R8G8B8 props (pixel shader)
    };

    // --- shared state (SurfaceCover.cpp) ---------------------------------------------------------
    extern const WXL_Api* g_api;
    extern Settings       g_settings;
    extern Level          g_levels[kMaxLevels];
    extern int            g_mapNow;           // the map being filled
    extern const void*    g_cdbcApi;
    extern double         g_simMs;
    extern unsigned       g_frame;            // counts updates (in world, cover on)
    extern bool           g_resting;          // the finer levels rest: nothing within the outermost's reach

    // What the table sees where the player is (for the panel, so rows can be authored on the spot).
    extern terrain::Surface   g_hereSurface;
    extern covertable::Values g_hereValues;
    extern std::string        g_hereTexture;
    extern bool               g_hereValid;
    extern float              g_hereMccv[3], g_hereLiquid, g_hereLiquidDepth;
    extern bool               g_hereMccvValid, g_hereLiquidValid;
    // Under the player, for the panel: the layer weights both ways round and the client's own
    // dominant layer (the check that the alpha maps are read the right way round).
    extern terrain::LayerWeights g_hereLayers, g_hereLayersSwapped;
    extern std::string g_hereLayerTexture[4]; // own copies for the panel (the cache can drop the originals)
    extern bool g_hereLayersValid;

    inline int Slot(int i) { const int m = i % kTex; return m < 0 ? m + kTex : m; }
    inline int SlotOf(int i, int j) { return Slot(j) * kTex + Slot(i); } // row = y (j), column = x (i)
    inline int FloorDiv2(int v) { return v >= 0 ? v / 2 : -((-v + 1) / 2); }

    inline int ActiveLevels()
    {
        const int n = g_settings.levels;
        return n < 1 ? 1 : (n > kMaxLevels ? kMaxLevels : n);
    }

    // --- fill (SurfaceCoverFill.cpp) ------------------------------------------------------------
    extern unsigned g_cellMisses; // terrain cells looked up in the table (panel)
    void ClearCellCache();
    void ClearLayerValues();
    void SampleRect(Level& L, bool isLevel0, int i0, int i1, int j0, int j1, bool fresh); // inclusive
    /// The ground at a world position, as the fill reads it (panel): the share painted with sealed ground
    /// (GroundMaterial kFlagSealed) and how much of a mud cover may form there (blend::MudAllowed). False: no terrain.
    bool SealedAt(float x, float y, float& sealedShare, float& mudAllowed);

    // What a sample changed, for Level::dirtyRow/dirtyCol.
    constexpr uint8_t kDirtyBase = 1, kDirtyCover = 2, kDirtyLook = 4;

    // Plans a level's next grid around the player (origin on an even cell). The filled area is
    // the grid plus an apron of kApron cells: the normals of the border vertices read up to 2
    // cells beyond the grid (morph band normals at the coarser spacing), and without the apron
    // those slots hold stale heights or holes -- a bright or dark line along every level border.
    // Modes: a new place (nothing valid on screen: filled while hidden), a refill in place (the
    // table or the "where" changed: refilled while the old cover stays visible), or a recentre
    // (only the strips that came into the grid).
    enum class Plan { Recentre, RefillInPlace, NewPlace };
    void PlanFill(Level& L, const float pos[3], Plan plan);
    // Works one level's job until the deadline; true when the job is done.
    bool WorkFill(Level& L, bool isLevel0, double deadline);

    // --- trenches (SurfaceCoverTrenches.cpp) ----------------------------------------------------
    // Level 0's trench state, slot = (world cell mod kTex): how far pressed down (0..1) and the
    // rim's strength (0..1; its height is a share of the local cover depth). Both relax over time.
    extern std::vector<float> g_press, g_rim;
    extern std::vector<int>   g_changed;          // slots to re-upload this frame
    extern bool               g_changedPending;   // changes not uploaded yet
    extern std::vector<float> g_static0;          // level 0 cover without trenches
    // Level 0's rim share and relax time per slot, from the rows of the cell's materials.
    extern std::vector<float> g_rimCell, g_relaxCell;
    extern unsigned           g_stamps;           // units stamping this frame
    extern float              g_rainNow;          // the rain the ruts last filled in with (StormModel RainLevel)
    // Trench radius per yard of footprint (its longer side): a human's 0.33 yd print gives 0.4 yd.
    constexpr float kFootprintToTrench = 1.2f;
    /// rain: StormModel RainLevel (0..1), mud ruts fill in faster with it.
    void Simulate(float dt, float rain);

    // --- cover textures and uploads (SurfaceCoverUpload.cpp) -----------------------------------
    extern IDirect3DTexture9* g_coverTextures[covertable::kMaxCoverTextures]; // per slot (owned by the cache)
    extern bool               g_slotSpecular[covertable::kMaxCoverTextures];  // per slot: its alpha is a specular mask
    extern int                g_slotId[covertable::kMaxCoverTextures];        // the ID each slot holds (0 = free)
    extern unsigned           g_texturesInView;
    extern std::unordered_map<int, std::string> g_textureStatus;
    extern std::unordered_map<int, IDirect3DTexture9*> g_textureCache; // the cover textures by ID (SurfaceCoverUpload)
    constexpr float kCoverTextureTile = 33.3333333f / 8.0f; // one repeat per terrain cell, like the terrain's layers
    void EnsureCoverTextures(IDirect3DDevice9* dev);
    // Recount which cover textures are around at the next draw (instead of within a second): a level
    // just filled a new place, whose cover would draw plain until then.
    void RecountCoverTextures();
    bool UploadLevel(Level& L, bool isLevel0);
    // A slot's depth as the cover texture draws it (level 0 with its trenches, or without), unsigned.
    float DrawnDepth(const Level& L, bool isLevel0, int row, int col, bool trenches);
    // What the uploads did in the last drawn frame (all levels), for the panel.
    struct UploadStats
    {
        unsigned fullFolds = 0, fullLooks = 0;   // whole-texture passes
        unsigned regions = 0, regionSlots = 0;   // changed regions, and the slots in them
        double   foldMs = 0, lookMs = 0;
    };
    extern UploadStats g_uploadStats;
    void ReleaseCoverTextures();

    // --- GPU and drawing (SurfaceCoverDraw.cpp, SurfaceCoverShaders.cpp) ------------------------
    extern const char* kVsHlsl;
    extern const char* kPsHlsl;
    /// In front of the pixel shader's source: the knee on our summed light (lights::kLampKnee*, as every shader has it).
    std::string LampKneeDefines();
    extern const char* kDecalPsHlsl;
    extern bool  g_tintSupported;
    // The terrain's lighting and fog as last read (for the panel): VS c12 fog, c24 sun direction
    // (view space), c25 ambient, c26 diffuse, PS c2 fog colour.
    extern float g_seenFog[4], g_seenLight[16], g_seenFogColor[4]; // light: c24..c27 (c27 = specular colour, exponent)
    // The terrain's dynamic shadows as found at our draw point: which shadow maps are still bound
    // on its samplers s5..s8, and whether they were used (all four bound and switched on).
    extern bool  g_shadowBound[4];
    extern bool  g_shadowUsed;
    extern bool  g_seenScene;
    extern float g_lastLight[6];        // ambient, diffuse as last read, kept when the cover stops drawing
    extern unsigned long long g_lastLightTick; // GetTickCount64 when (0: never)
    extern unsigned g_patchLightsMost, g_patchLightsFull; // last frame: the busiest patch; patches over kMaxLights
    extern double g_uploadMs, g_drawMs;
    extern const char* g_inactive;
    // The client's projected textures redrawn on the cover: what the calls came to since the last
    // reset (panel).
    struct DecalStats
    {
        unsigned calls = 0, modelCalls = 0, drawn = 0; // modelCalls: from models' projected batches (0x7E3AA0)
        unsigned noCover = 0, noPieces = 0, noBox = 0; // skipped: cover not drawn this frame, nothing drawn on the terrain, not overhead
        uintptr_t lastCaller = 0;
        uint32_t  lastFlags = 0;
        // The last replayed decal: the setup the client left, and the cover patches it went onto.
        unsigned  patches = 0, patchesBefore = 0, lastPatches = 0;
        bool      lastFromModel = false;
        uint32_t  lastColor = 0;
        DWORD     texCoordIndex = 0, transformFlags = 0, texCoordIndex1 = 0, transformFlags1 = 0;
        DWORD     srcBlend = 0, dstBlend = 0, alphaTest = 0, alphaRef = 0, zFunc = 0;
        unsigned  shaders = 0;        // bit 0: a vertex shader was bound, bit 1: a pixel shader
        float     matrix[16] = {};    // stage 0 texture matrix
    };
    extern DecalStats g_decalStats;
    // The draw's hooks (terrain stage, end of the scene). From Install.
    void InstallDraw(const WXL_Api* api);
    // Before IDirect3DDevice9::Reset: the draw's state block (a live one makes the Reset fail).
    void ReleaseDrawDeviceState();

    // The cover's top and depth at a world position over the first `levels` levels (cover::TopAt).
    // Level 0 alone only reads fixed-size arrays: safe from the scene's animate threads; the
    // coarser levels' folds read snow and moisture state the main thread changes.
    // trenches = false: level 0 as if nobody had walked there (the surface around a unit, not its footprint).
    bool SampleTop(float x, float y, int levels, float& z, float& depth, bool trenches = true);

    // --- camera (SurfaceCoverCamera.cpp) -------------------------------------------------------
    // Keeps the 3rd-person camera above the cover like the client keeps it out of the terrain. From Install.
    void InstallCamera(const WXL_Api* api);
    struct CameraStats { unsigned calls = 0, pulled = 0; float lastPull = 0.0f; };
    extern CameraStats g_cameraStats;

    // --- lift (SurfaceCoverLift.cpp) -------------------------------------------------------------
    // Models on a unit's Base attachment (auras and other spell effects at the feet) lifted onto the
    // cover. From Install.
    void InstallLift(const WXL_Api* api);
    unsigned LiftedCount(); // model builds lifted so far (panel; counted from the animate threads)
    float    LastLift();    // yd, the last lift

    // --- ChromieCraft fork: names shared across the files above ----------------------------------
    constexpr float kEdgeRadiusShare = 0.85f, kEdgeFullShare = 0.65f;
    constexpr float kMinDrawnDepth = 0.004f;   // thinner cover isn't drawn (no z-fighting with the terrain)
    // Trenches (SurfaceCoverTrenches.cpp): cells with something on them this frame, the sand's still rings
    // around prints (features/CoverRipples), and the last draw's counts for the panel.
    extern std::vector<uint8_t> g_isActive;
    extern std::vector<float>   g_rippleCell;
    // A print in sand at level 0's slot `s` gets a rim (AddRipple), and one the client wouldn't put down there goes down
    // anyway (cover::SandPrintsAt): at least half the cover's look there is from kFlagRipplePrints rows and the cover is
    // drawn. Silithus's dunes, with its rock highlight painted over the sand, come to 0.5..1.
    constexpr float kRippleShareMin = 0.5f;
    inline bool RippleCell(int s) { return g_rippleCell[s] >= kRippleShareMin && g_static0[s] >= kMinDrawnDepth; }
    // How far from the cover's base a foot may be for SandPrintsAt (yd): the cover's few centimetres and the foot's
    // height at the step; a building's floor (the Caverns of Time) lies further from the terrain.
    constexpr float kSandPrintReach = 0.6f;
    extern ripples::Set         g_ripples, g_wmoRipples;
    extern unsigned             g_ripplesMade, g_ripplesShaped, g_wmoRipplesMade;
    extern float                g_rippleClock;
    extern unsigned             g_drawPatches, g_emptyLevels, g_snowLookups;
    extern unsigned             g_slithering;   // bodies carving a slither trail this frame
    // Uploads (SurfaceCoverUpload.cpp): cover texture ID -> slot (-1 = none).
    extern std::vector<int> g_slotOfId;
    // Level 0's trench strength at a slot from the grid as it is now (upstream's, SurfaceCoverUpload.cpp): the
    // prints' lift (features/CoverPrints) reads the drawn depth with it.
    float DeformK(const Level& L, int s);
    // Draw (SurfaceCoverDraw.cpp): the terrain's shadow mode as found, where prints lift the cover, the compiler.
    struct PrintLift { bool valid = false; bool outermost = false; float cx = 0.0f, cy = 0.0f, radius = 0.0f; };
    extern PrintLift g_printLift;
    extern int       g_shadowMode;
    void* Compile(IDirect3DDevice9* dev, const char* hlsl, const char* target, bool pixel);
    // Shaders (SurfaceCoverShaders.cpp): the pixel shader comes in three parts, kPsHlsl + these.
    extern const char* kRippleHlsl;
    extern const char* kPsMainHlsl;

    // --- rooms (SurfaceCoverRooms.cpp): no cover inside buildings ---------------------------------------------------
    /// Once per frame before the fills: the buildings the scene drew since the last frame (cover::NoteBuilding), read
    /// from the client; where one came, moved or went, its rooms' rows are queued for re-sampling. refill: every level
    /// is about to be sampled afresh (the queues are dropped).
    void UpdateRooms(const world::Snapshot& snap, bool refill);
    /// Leaving the world: every building is forgotten.
    void ClearRooms();
    /// Works the levels' re-sampling queues until the deadline (levels without a fill job running).
    void WorkRooms(double deadline);
    /// Before each row SampleRect samples: the buildings it can meet. Then whether the terrain at (x, y, z) lies in one
    /// of their rooms (margin: yd around the boxes).
    void RowRooms(const Level& L, int i0, int i1, int j);
    bool InRoom(float x, float y, float z, float margin);
    /// The status lines for the panel and the snapshot.
    void RoomLines(std::vector<std::string>& out);

    // --- panel (SurfaceCoverPanel.cpp) ----------------------------------------------------------
    void __cdecl Panel(void* user);
    /// The panel's status lines (the snapshot writes them too).
    void Status(std::vector<std::string>& lines);
}
