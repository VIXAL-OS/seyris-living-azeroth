// Window glass: the rules, apart from the client (tests/glass checks them offline). The runtime is features/Glass
// (the tables, the per-batch switch, the floor light's panes) with features/GlassShader (the WMO pixel-shader blocks)
// and features/GlassShafts (the coloured shafts).
//
// What glass does here (all of it on the glass texels of a window texture, tools/gen_glass.py's mask):
//  - Seen from inside, the light coming through: the texel's own colour (more saturated) x the sky's light, x the
//    sun's when the sun is beyond the pane (frosted glass spreads it: less direction), dimmed by cloud, rain, storms
//    and night. Never darker than the client's own colour, so its lit windows at night stay as they are.
//  - Seen from outside by day, darker glass with the sky in it (Fresnel) and the sun's glint; behind clear and
//    leaded glass a room (interior mapping in the pane's own frame); at night the client's own lit windows.
//  - Light through sunlit panes onto the floors and walls of the room behind them, and coloured shafts in the room's
//    air (the panes' rectangles, GlassPanes.cdbc), kept inside that room.
//  - The rooms behind the glass differ by building (RoomStyles.cdbc, GlassBuildings.cdbc): tint, brightness, a few
//    dark; per window a layout (shelves, curtains half drawn, a dividing wall with a doorway, plain) weighted by the
//    building's class and its place; at night lamp-lit behind the client's own lit windows, more of them dark as the
//    night goes on.
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace wxl_livingazeroth::glass
{
    // --- the tables (tools/gen_glass.py, docs/cdbc-tables.md) ----------------------------------------------------
    enum Kind : uint32_t { kNone = 0, kClear = 1, kStained = 2, kFrosted = 3, kLeaded = 4 };
    enum MaterialFlag : uint32_t { kInsideFace = 0x1, kOutsideFace = 0x2, kMatte = 0x4, kPainted = 0x8 };

    struct Material
    {
        uint32_t kind = kNone, flags = 0;
        float colour[3] = { 1, 1, 1 };   // the glass texels' average colour
        float light[3] = { 1, 1, 1 };    // the light it lets in (full value 1)
        float mask[4] = { 0, 0, 0, 1 };  // glass = saturate(dot(texel.rgb, mask.xyz) + mask.w)
    };
    const char* KindName(uint32_t kind);

    struct Pane
    {
        uint32_t id = 0, map = 0, wmo = 0;     // wmo: the placement's unique ID
        float    pos[3] = {};                  // the glass's middle
        float    out[3] = {};                  // out of the building (unit)
        float    along[3] = {};                // along the pane (unit; horizontal for an upright window)
        float    up[3] = {};                   // up the pane: out x along
        float    halfWidth = 1.0f, halfHeight = 1.0f;
        float    light[3] = { 1, 1, 1 };       // the colour of the light it lets in
        uint32_t kind = kNone;
        float    roomMin[3] = {}, roomMax[3] = {}; // the room it lights (world box)
    };

    /// The panes of every map, bucketed in kTile-yd squares.
    class PaneIndex
    {
    public:
        static constexpr float kTile = 64.0f;
        void Build(std::vector<Pane> panes);
        /// The panes of `map` within `range` yd of `center`, nearest first, at most `max`.
        void Near(uint32_t map, const float center[3], float range, size_t max, std::vector<const Pane*>& out) const;
        size_t Size() const { return m_panes.size(); }
        const std::vector<Pane>& All() const { return m_panes; }

    private:
        static uint64_t Key(uint32_t map, int tx, int ty);
        std::vector<Pane> m_panes;
        std::unordered_map<uint64_t, std::vector<uint32_t>> m_tiles;
    };

    // --- the light of the moment --------------------------------------------------------------------------------
    struct Inputs
    {
        float sun[3] = { 0, 0, 1 };          // toward the sun (world, unit)
        float daylight = 1.0f;               // the sun's day, 0..1 (climate)
        float night = 0.0f;                  // the client's window fade, 0 day .. 1 night (its lit windows)
        float overcast = 0.0f;               // cloud and fog, 0..1
        float precipitation = 0.0f;          // rain or snow, 0..1
        float storm = 0.0f;                  // a storm's darkness, 0..1
        float sky[3] = { 0.6f, 0.7f, 0.8f }; // the sky's (fog's) colour
        float sunColour[3] = { 1.0f, 0.95f, 0.85f };
    };

    struct Light
    {
        float sunGain = 0.0f;     // the direct sun through a pane, 0..1 (x its colour)
        float skyGain = 0.0f;     // the sky's light through a pane, any side
        float sky[3] = {};        // ... its colour x that
        float day = 0.0f;         // 1 by day .. 0 at night (the outside look and the room behind the glass)
        float dim = 1.0f;         // what cloud, rain and storms leave of the daylight
    };
    Light Evaluate(const Inputs& in);

    /// Schlick's Fresnel: the share of light the glass reflects at this cosine to its normal.
    float Fresnel(float cosine, float f0);

    /// How much of the sun comes straight through a pane whose outside faces `out`: none with the sun behind
    /// the building, the cosine beyond it; `diffusion` (frosted glass) spreads it toward a half everywhere.
    float SunThrough(const float sun[3], const float out[3], float diffusion);

    /// Whether a glass face is seen from inside (1) or outside (0): its texture says so (_int_, _ext_), else an
    /// interior group's face counts as inside as far as the camera is indoors (the client's interior blend).
    float InsideWeight(uint32_t materialFlags, uint32_t groupFlags, float cameraIndoors);
    constexpr uint32_t kGroupExterior = 0x8, kGroupIndoor = 0x2000;

    /// The colour seen from inside: the texel (saturated by `saturation`) x the light through, never below `base`.
    void InsideColour(const float texel[3], const float base[3], float saturation, const float lightThrough[3], float out[3]);

    // --- the floor light: one pane's projection along the sun --------------------------------------------------
    /// Three rows (12 floats) taking a point relative to `origin` (the camera) to (a, b, t): a and b across the
    /// pane (|a|, |b| < 1 inside it) where the ray from the point toward the sun crosses the pane's plane, and
    /// t = that ray's length / `reach` (0 < t < 1: behind the pane and near enough). False when the sun is
    /// on the pane's inside (or along it): the pane lets in no sun.
    bool ProjectionRows(const Pane& p, const float sun[3], const float origin[3], float reach, float rows[12]);

    /// One pane's light at a point (relative to `origin`) as the shader works it out: the rectangle with soft
    /// edges (`soft`: 1 / the share of the half sizes they take), along the ray (soft over the last 1/`farSoft`),
    /// and only inside the room box (relative to `origin` too). 0..1.
    float FloorWeight(const float rows[12], const float point[3], const float roomMin[3], const float roomMax[3], float soft, float farSoft);

    /// The same from scratch, for the tests: the pane, the sun and a world point.
    float FloorWeightWorld(const Pane& p, const float sun[3], const float point[3], float reach, float soft, float farSoft);

    // --- the fake room behind clear glass (interior mapping) ------------------------------------------------------
    enum RoomFace : int { kBackWall = 0, kSideWall = 1, kFloor = 2, kCeiling = 3 };
    /// A ray entering the glass at `pos` (in the pane's frame: along, up, in yd) going `dir` (along, up, into the
    /// room; dir[2] > 0) meets the room's cell (cellW x cellH, `depth` deep): which face, after how many yd.
    RoomFace RoomHit(const float pos[2], const float dir[3], float cellW, float cellH, float depth, float& distance);

    // --- who lives behind the glass (RoomStyles.cdbc, GlassBuildings.cdbc: tools/gen_glass.py) --------------------
    // A building's class (from its WMO's path and, in a city, its group's name or district) and its place's palette
    // (culture and region) make a style: its rooms' tints, brightness, lamp, curtains, how many are dark, the layouts'
    // weights and how late its lights stay on. Each placement (and each group of a city's one big WMO) has a style
    // and a seed. Per window (cell) a hash of the cell and the seed picks the layout, whether the room is dark, its
    // light, the lamp's place, the curtains' gap, the things on the shelves.
    enum RoomClass : uint32_t
    {
        kClassOther = 0, kClassHouse = 1, kClassFarm = 2, kClassInn = 3, kClassShop = 4, kClassTemple = 5,
        kClassKeep = 6, kClassTower = 7, kClassCity = 8, kClassShip = 9, kClassCount = 10
    };
    const char* ClassName(uint32_t cls);
    enum Layout : int { kLayoutShelves = 0, kLayoutCurtains = 1, kLayoutDivider = 2, kLayoutPlain = 3 };

    struct RoomStyle
    {
        uint32_t id = 0, cls = kClassHouse, palette = 0;
        float tint[3] = { 0.85f, 0.72f, 0.55f };     // the rooms' colour by day (one end of the building's range)
        float tint2[3] = { 0.85f, 0.72f, 0.55f };    // ... and the other end
        float lamp[3] = { 1.0f, 0.66f, 0.36f };      // the lamp at night
        float curtain[3] = { 0.55f, 0.2f, 0.16f };   // the curtains' cloth
        float brightness = 1.0f;                     // x the rooms' light by day
        float spread = 0.15f;                        // buildings brighter or dimmer by up to this share
        float darkShare = 0.1f;                      // rooms dark by day
        float weights[4] = { 0.25f, 0.35f, 0.15f, 0.25f };  // shelves, curtains, divider, plain (Layout order)
        float lateHours = 0.0f;                      // its lights go out this much later (inns) or earlier (shops)
        float nightCap = 1.0f;                       // at most this share of its rooms dark at night
    };

    enum BuildingFlag : uint32_t
    {
        kBuildingCity = 0x1,       // a city's one big WMO: the building is the block (kBlock yd) within its group
        kBuildingDarkPlace = 0x2,  // ruined, abandoned, haunted (gen_window_lights' DARK_AREAS / DARK_PATHS): no lit rooms
        kBuildingEmpty = 0x4,      // nobody lives here: every room dark
        kBuildingSelfLit = 0x8,    // its WMO has windows the client lights at night (for the editor; not read)
    };
    constexpr uint32_t kWholePlacement = 0xFFFFFFFFu;

    struct Building
    {
        uint32_t map = 0, uniqueId = 0, group = kWholePlacement, style = 0, seed = 0, flags = 0, area = 0;
        float    pos[3] = {};          // the placement's origin (its WMO's 0, 0, 0 in the world)
        uint32_t path = 0;             // PathHash of its WMO's path (0: any)
    };
    /// FNV-1a of a path, lower case with backslashes (so "World/wmo/X.wmo" and "world\wmo\x.wmo" are the same).
    uint32_t PathHash(const char* path);

    /// The buildings by where they stand (a placement's origin within kNear yd, and its WMO's path), a city's groups
    /// under its placement, and a default per WMO (its path, on a map or any map) for instances not in the table
    /// (transports, a placement the scan missed).
    class BuildingIndex
    {
    public:
        static constexpr float kNear = 1.5f;
        void Build(std::vector<Building> rows);
        /// The building drawn by an instance at `pos` (its origin) of the WMO with `path` (0: unknown), group `group`.
        const Building* Find(uint32_t map, const float pos[3], uint32_t path, uint32_t group) const;
        size_t Placements() const { return m_placements; }
        size_t Size() const { return m_rows.size(); }

    private:
        std::vector<Building> m_rows;
        std::unordered_map<uint64_t, std::vector<uint32_t>> m_grid;   // (map, 8-yd cell) -> placement rows
        std::unordered_map<uint64_t, uint32_t> m_groups;              // (placement row, group) -> row
        std::unordered_map<uint64_t, uint32_t> m_defaults;            // (map, path) -> row; (any map, path) too
        size_t m_placements = 0;
    };

    // The shader's room (features/GlassShader), its numbers. The cell runs 0..1 across, up and deep.
    namespace room
    {
        constexpr float kHashK[4] = { 0.1031f, 0.1030f, 0.0973f, 0.1099f };  // the hash (Dave Hoskins' hash42)
        constexpr float kHashAdd = 33.33f;
        constexpr float kDividerDepth = 0.5f;                     // the dividing wall: half way back
        constexpr float kDoorSpan = 0.4f, kDoorBase = 0.3f;       // the doorway's middle: 0.3 .. 0.7 across
        constexpr float kDoorHalfWidth = 0.13f, kDoorHeight = 0.72f, kDoorDim = 0.45f;  // ... the room beyond 45% dimmer
        constexpr float kDividerShade = 0.9f;
        constexpr float kCurtainDepth = 0.08f;                    // the cloth just behind the glass
        constexpr float kGapSpan = 0.3f, kGapBase = 0.35f;        // the gap's middle: 0.35 .. 0.65 across
        constexpr float kGapMinHalf = 0.08f, kGapHalfSpan = 0.2f, kGapHash = 7.13f;  // its half width 0.08 .. 0.28
        constexpr float kClothEdge = 25.0f;                       // the cloth's edge: soft over 1/25 of the cell
        constexpr float kFolds = 9.0f, kFoldAmp = 0.76f, kFoldBase = 0.62f, kFoldMean = 0.81f;
        constexpr float kSlots = 9.0f, kShelves = 4.0f;           // things per shelf, shelves per wall
        constexpr float kPlank = 0.12f, kThingMax = 0.85f, kThingGap = 0.4f;
        constexpr float kShelfCentre[2] = { 0.5f, 0.52f }, kShelfHalf[2] = { 0.38f, 0.33f };
        constexpr float kShelfHashShelf = 7.0f, kShelfHashRoom = 31.0f, kThingHash = 7.7f;
        constexpr float kThingSpan = 0.9f, kThingBase = 0.45f, kShelfBack = 0.8f, kPlankLight = 1.35f;
        constexpr float kLamp[4] = { 0.25f, 0.5f, 0.42f, 0.72f }; // across 0.25 .. 0.75 (hashed), up, deep
        constexpr float kLampK = 6.0f, kLampGain = 1.2f, kLampBase = 0.4f;  // base + gain / (1 + k d^2)
        constexpr float kFadeA = 1.225f, kFadeB = -1.0f / 4000.0f;  // detail: 1 within 30 yd .. 0 from 70 yd (d^2)
        /// ... and by the pixel's footprint on the glass (yd, squared): 1 up to 0.05 yd .. 0 from 0.15 yd, where the
        /// planks (0.1 yd), the things and the folds (0.4 yd) would shimmer
        constexpr float kFootprintA = 1.125f, kFootprintB = -50.0f;
        constexpr float kClothFar = 0.6f;                         // the curtains' cover far away
        constexpr float kDarkDay = 0.12f;                         // a dark room by day keeps this much light
        constexpr float kCurtainDay = 1.5f, kCurtainGain = 0.8f;  // cloth: 1.2 x the day in front, 0.8 x the lamp behind
        constexpr float kBlock = 14.0f;                           // yd: a city's blocks (one building each)
        constexpr float kCellLight[2] = { 0.6f, 0.55f };          // each room's light: 0.55 .. 1.15 (hashed)
        constexpr float kFace[4] = { 0.7f, 0.55f, 0.4f, 1.0f };   // side wall, floor, ceiling, back wall
        constexpr float kFar = 0.12f;                             // farther is darker: 1 / (1 + 0.12 t)
        /// The lamp-lit room's mean light (cell light x face x falloff over many rooms and views), measured with
        /// RoomOutside: the lamp's colour is divided by it so a lit room is about as bright as the client's window.
        constexpr float kNightMean = 0.32f;
    }

    /// Dave Hoskins' hash42 as the shader works it: four values in 0..1 from two.
    void Hash42(float x, float y, float out[4]);

    /// The layouts' cumulative thresholds from their weights (shelves, curtains, divider, plain; normalised): a
    /// room's hash below t[0] has shelves, below t[1] curtains, below t[2] a dividing wall, else it's plain.
    void LayoutThresholds(const float weights[4], float t[3]);
    Layout PickLayout(float hash, const float t[3]);

    /// The share of rooms dark at night by the hour (0..24, the client's clock): 8% in the evening, rising from 21:00
    /// to half at midnight, 72% at 02:00 and 78% at 04:30; early risers bring it back to 60% by 06:00. `lateHours`
    /// shifts the evening rise later (an inn) or earlier (a shop), `cap` caps it (an inn stays mostly lit).
    float LightsOutShare(float hour, float lateHours, float cap);

    /// The room's constants for one glass batch (c193.z, c198, c199, c202..c205, c212: GlassShader.hpp) and the camera
    /// (c180), as RoomOutside reads them.
    struct RoomConstants
    {
        float share = 0.65f;                          // c193.z
        float c198[4] = { 1 / 3.5f, 1 / 3.2f, 0.25f, 0.32f };
        float c199[4] = { 0.85f, 0.72f, 0.55f, 1.0f };
        float c202[4] = { 0.85f, 0.72f, 0.55f, 0.0f };
        float c203[4] = { 0, 0, 0, 0 };
        float c204[4] = { 0, 0, 0, 1 };
        float c205[4] = { 0.55f, 0.2f, 0.16f, 0 };
        float c212[4] = { 0, 0, 0, 0 };
        float cam[3] = { 0, 0, 0 };
        float footprint2 = 0.0f;                      // the pixel's footprint on the glass, squared (yd^2; the shader's)
    };

    /// What RoomOutside found (for the tests).
    struct RoomSample
    {
        int layout = kLayoutPlain;
        bool lit = true, door = false, wall = false;  // through the doorway; the dividing wall
        float cloth = 0, fold = 0, shelf = 1, falloff = 0, fade = 1, shade = 0, building[2] = {};
        float hit[3] = {};                            // where the ray meets the room (cell units)
        float hide = 0;                               // the share of the client's glow a dark room hides (c205.w or 0)
    };

    /// The room seen through clear glass from outside as the shader works it (GlassShader's room part, op for op):
    /// the point P (world), toward the eye V, the normal N facing the eye (world, unit), the client's colour r12 and
    /// the outside colour so far (r23): the outside colour after the room. `sample` may be null. A dark room behind
    /// the client's lit window hides `sample->hide` of its glow from the whole window: the glass here, and the shader
    /// takes it from the rest of the window too (the lead, the frame: (1 - hide) x r12; seen from outside).
    void RoomOutside(const float P[3], const float V[3], const float N[3], const RoomConstants& k, const float r12[3],
                     const float outside[3], float out[3], RoomSample* sample);
}
