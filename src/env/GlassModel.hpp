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
}
