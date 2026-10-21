// Window glass: the rules, apart from the client (tests/glass checks them offline). The runtime is features/Glass
// (the tables, the per-batch switch, the floor light's panes) with features/GlassShader (the WMO pixel-shader blocks)
// and features/GlassShafts (the coloured shafts).
//
// What glass does here (all of it on the glass texels of a window texture, tools/gen_glass.py's mask):
//  - Seen from inside, the light coming through: the texel's own colour (more saturated) x the sky's light, x the
//    sun's when the sun is beyond the pane (frosted glass spreads it: less direction), dimmed by cloud, rain, storms
//    and night. Never darker than the client's own colour, so its lit windows at night stay as they are.
//  - Seen from outside by day, the client's own painted glass a little darker (clear most, stained barely) and cooler
//    (DayCool: less saturated, toward the sky's hue, more toward the top of the pane and at a slant; stained barely)
//    with the sky in it (Fresnel, more at a slant) and the sun's glint; behind clear and leaded glass one room per
//    window (env/GlassRooms: interior mapping into a box behind the window's opening, a few yards deep by the
//    building's class; its floor, walls and ceiling in the client's interior textures by its palette, the corners
//    darker, the light falling in at the window, a patch of sun on the floor; furniture against the back wall and in
//    the middle, at their depths); at dusk the painted glass's warmth again with the room still seen by the day's
//    light; at night the client's own lit windows, lamp-lit rooms behind them.
//  - Light through sunlit panes onto the floors and walls of the room behind them, and coloured shafts in the room's
//    air (the panes' rectangles, GlassPanes.cdbc), kept inside that room.
//  - The rooms behind the glass differ by building (RoomStyles.cdbc, GlassBuildings.cdbc): tint, brightness, a few
//    dark; per window its furniture by the building's class and its hash, curtains half drawn in some (weighted by the
//    class and its place); at night lamp-lit behind the client's own lit windows, more of them dark as the night goes
//    on (each fading over 1.5 s), the window lights in front of them with them (features/Windows works out the same
//    rooms: the window's key in WindowRooms.cdbc, LitHash, the dark band, the rooms' clock, below). A dark room at night
//    isn't a black void: the moon's cool light and the night's ambient come in at its window (DarkRoomLight, about an
//    eighth of a lit room; Light::moon), its furniture dim shapes, its corners darker.
//  - Frosted glass: the windows the client lights (self-lit, SIDN, or unlit: always at full) go dark the same way: the
//    same windows, hash, schedule and fades, the glass's own glow hidden when dark; and a lit one has a hearth's gentle
//    flicker (HearthDip), its window light in time with it. Ironforge's parts each keep their own hours (the forge never
//    sleeps), and underground no sky comes through its glass. Behind it a heavily blurred room (FrostedRooms): its lamp a
//    soft glow, its furniture soft dark shapes, the frosted glass in front of it; underground lamp-lit at any hour, no
//    daylight in it.
//  - Stained glass (not a dungeon's; StainedRooms): a faint room behind it, tinted by the glass's colours: a hint of depth
//    by day, mostly the glass's own glow at night, its lights out with the rooms'.
//  - Titan glass with no sky behind it (Ulduar, the Halls of Stone and of Lightning: tools/gen_glass.py TITAN_MAPS) is
//    lit from behind: it glows in its own colours at any hour, General Vezax's with a slow faint pulse, and a light
//    behind each pane puts its colours on the floor and in the room's air (BacklitDirection, BacklitGlow).
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace wxl_livingazeroth::glass
{
    // --- the tables (tools/gen_glass.py, docs/cdbc-tables.md) ----------------------------------------------------
    enum Kind : uint32_t { kNone = 0, kClear = 1, kStained = 2, kFrosted = 3, kLeaded = 4 };
    enum MaterialFlag : uint32_t
    {
        kInsideFace = 0x1, kOutsideFace = 0x2, kMatte = 0x4, kPainted = 0x8,
        kBacklitPulse = 0x10,   // its backlit pulse is a heartbeat (General Vezax's glass): VezaxPulse scales it (the others' is a shimmer)
    };

    struct Material
    {
        uint32_t kind = kNone, flags = 0;
        float colour[3] = { 1, 1, 1 };   // the glass texels' average colour
        float light[3] = { 1, 1, 1 };    // the light it lets in (full value 1)
        float mask[4] = { 0, 0, 0, 1 };  // glass = saturate(dot(texel.rgb, mask.xyz) + mask.w)
        // Titan glass (GlassMaterials' Backlit columns, tools/gen_glass.py BACKLIT): where its building has no sky behind its
        // glass (kBuildingBacklit) lit from behind: the glow's strength (x the texel; 0: not backlit glass), its pulse's
        // amplitude (a share of the glow), period (s) and the colour the pulse adds (luminance 1; white: a brightening).
        float backlit = 0.0f, pulse = 0.0f, pulseSeconds = 0.0f;
        float pulseColour[3] = { 1, 1, 1 };
        // A corner of the texture that isn't glass (GlassMaterials' NotGlassU, NotGlassV; tools/gen_glass.py NOT_GLASS): its
        // texels from (u, v) to the far corner of its tile, drawn as the client draws them. 1: none. The human window
        // atlases' fourth quadrant (0.5, 0.5): a smithy's forge mouth, "blacksmith windows look very weird lol" (the
        // user, 2026-10-20).
        float notGlass[2] = { 1.0f, 1.0f };
    };
    const char* KindName(uint32_t kind);

    // The see-through redraw's own mask (features/SeeThrough; "yeah build the lead mask pls :D", the user, 2026-10-21).
    // The light's mask above counts the lit glass only: the shaded lower half of a pane, painted darker, reads as frame,
    // and through the inn's windows the per-pixel redraw showed ~4% of the room. This one is glass for every pane's
    // inside, lit and shaded alike, and opaque for the lead, the frame with its mullions and tracery, and the shutters:
    // where the texture's panes are (GlassLead.cdbc: rectangles in its uv, by hand from its layout; tools/gen_glass.py
    // LEAD_LAYOUTS), its texels in them that aren't lead: not dark (luminance under `dark` x the median luminance of the
    // lit glass in them: the mask above over half) and not on a thin dark line (the luminance closed over kLeadClose x
    // kLeadClose texels, minus it, under `line` x that median). Built at run time from the client's texture, never stored.
    struct LeadRect { float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f; };   // in the texture's uv (0..1), half-open
    constexpr float kLeadDark = 0.18f, kLeadLine = 0.15f;
    constexpr int kLeadClose = 5;
    struct LeadRule
    {
        std::vector<LeadRect> rects;
        float dark = kLeadDark, line = kLeadLine;
    };
    /// The lead mask of a texture's level (`rgb`: w x h texels, three floats each, 0..1 as stored, rows top down) by
    /// its material's light mask (the lit glass) and its rule: w x h bytes, 255 glass, 0 not. False (and `out` empty)
    /// when no texel is in its rectangles.
    bool LeadMask(const float* rgb, uint32_t w, uint32_t h, const Material& m, const LeadRule& rule, std::vector<uint8_t>& out);
    /// A mask's next mip level: each texel the mean of its 2 x 2 (a size of 1 stays 1); w and h become the new size.
    void LeadMip(const std::vector<uint8_t>& level, uint32_t& w, uint32_t& h, std::vector<uint8_t>& out);

    enum PaneFlag : uint32_t
    {
        kPaneNoSky = 0x1,     // GlassPanes.Flags: no sky behind it (underground): no floor light, no shafts
        kPaneBacklit = 0x2,   // titan glass with no sky behind it: its light from behind at any hour (BacklitDirection), not the sun's
    };

    struct Pane
    {
        uint32_t id = 0, map = 0, wmo = 0;     // wmo: the placement's unique ID
        uint32_t flags = 0;                    // PaneFlag
        uint32_t material = 0;                 // its texture's GlassMaterials ID (0: unknown)
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
        float ambient[3] = { 0, 0, 0 };      // the client's light on the land (ambient + diffuse; 0: unknown): the night's hue
    };

    struct Light
    {
        float sunGain = 0.0f;     // the direct sun through a pane, 0..1 (x its colour)
        float skyGain = 0.0f;     // the sky's light through a pane, any side
        float sky[3] = {};        // ... its colour x that
        float day = 0.0f;         // 1 by day .. 0 at night (the outside look: darker, cooler, the sky in it)
        float dim = 1.0f;         // what cloud, rain and storms leave of the daylight
        /// The room behind the glass: 1 while the client's windows are unlit (by day, and at dusk until they light up,
        /// 20:30 .. 21:30) .. 0 at night: seen by the day's light, soft, its share by day; at night the lamp-lit rooms.
        float rooms = 0.0f;
        /// The dark rooms' light at night (the moon's and the night's ambient through the window), luminance 1: room::kMoonHue
        /// moved room::kMoonClient of the way to the client's own night light's hue (each channel within kSkyHue).
        float moon[3] = { 1.0f, 1.0f, 1.0f };
    };
    Light Evaluate(const Inputs& in);
    /// The dark rooms' light colour at night (Light::moon): room::kMoonHue moved room::kMoonClient of the way to the hue
    /// of `clientLight` (the client's ambient + diffuse; its colour over its luminance, each channel within kSkyHue; none
    /// when its luminance is under 0.001 or not finite), at luminance 1.
    void MoonHue(const float clientLight[3], float out[3]);

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

    // --- titan glass lit from behind (Ulduar, the Halls of Stone and of Lightning) ------------------------------------
    // "Their stained glass should glow on its own and cast coloured light from behind, not depend on the sun" (the user,
    // 2026-10-18). A backlit pane's light is a virtual one behind it, its rays along the pane's inward normal tilted
    // kTiltDegrees down: the floor light's projection (ProjectionRows) and the shafts' with that direction instead of the
    // sun's, at any hour.
    namespace backlit
    {
        constexpr float kTiltDegrees = 30.0f;   // the light from behind falls this far below the pane's inward normal
        constexpr float kFloorShare = 0.45f;    // its light on the floor and in the air: x the glow x the pane's light (the sun's: 1 x)
        constexpr float kMinOut = 0.25f;        // toward the light at least this along the pane's way out (else straight out)
    }
    /// Toward the virtual light behind a backlit pane (world, unit): its way out tilted kTiltDegrees up (the light falls that
    /// far below the inward normal), normalize(out + tan(tilt) z); a skylight's straight up. Straight out when that would
    /// lie within kMinOut of the pane's plane (a pane facing steeply down).
    void BacklitDirection(const Pane& p, float out[3]);
    /// The pulse's wave at `seconds` (any steady clock): 0.5 - 0.5 cos(2 pi frac(seconds / period)), 0..1, 0 at whole
    /// periods; 0 without a period (0, negative, not finite) or when the time isn't finite. Worked in doubles: the same
    /// value for the same time, periodic.
    float BacklitWave(double seconds, float period);
    /// A backlit material's glow now (the light behind it, rgb, x the texel): `strength` (BacklitStrength) x its Backlit x
    /// (1 + its pulse x `pulseScale` x the wave x its pulse colour). Never below 0; 0 where it isn't backlit glass or a
    /// number isn't finite.
    void BacklitGlow(const Material& m, double seconds, float strength, float pulseScale, float out[3]);
    /// The mean of `n` unit directions (three floats each) weighted by `weights` (negative ones count 0), normalised;
    /// `fallback` when the weights are all 0 or the directions cancel (the mean shorter than 0.2).
    void MeanDirection(const float* dirs, const float* weights, size_t n, const float fallback[3], float out[3]);

    // --- the fake room behind clear glass (one room per window: env/GlassRooms) ---------------------------------------
    enum RoomFace : int { kBackWall = 0, kSideWall = 1, kFloor = 2, kCeiling = 3 };

    // --- who lives behind the glass (RoomStyles.cdbc, GlassBuildings.cdbc: tools/gen_glass.py) --------------------
    // A building's class (from its WMO's path and, in a city, its group's name or district) and its place's palette
    // (culture and region) make a style: its rooms' tints, brightness, lamp, curtains, how many are dark, the layouts'
    // weights and how late its lights stay on. Each placement (and each group of a city's one big WMO) has a style
    // and a seed. Per window a hash of its key (its ordinal and group in its WMO: GlassWindows.cdbc) and the seed picks
    // whether the room is dark, its light, its depth, the lamp's place, the furniture, the curtains and their gap.
    enum RoomClass : uint32_t
    {
        kClassOther = 0, kClassHouse = 1, kClassFarm = 2, kClassInn = 3, kClassShop = 4, kClassTemple = 5,
        kClassKeep = 6, kClassTower = 7, kClassCity = 8, kClassShip = 9,
        // Ironforge's parts (a city that never fully sleeps; tools/gen_glass.py IRONFORGE): the Great Forge and the
        // smithies, the Commons (shops, the bank, the inn, the halls), the homes and quarters of the wards, the Forlorn
        // Cavern.
        kClassForge = 10, kClassCommons = 11, kClassQuarter = 12, kClassCavern = 13, kClassCount = 14
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
        kBuildingUnderground = 0x10, // no sky through its glass (Ironforge's interior groups): the street's look, no sky or sun in it
        kBuildingDungeon = 0x20,   // a dungeon's WMO (its path): its frosted windows keep the client's look (no lights out, no flicker)
        kBuildingBacklit = 0x40,   // its titan glass has no sky behind it (Ulduar, the Halls of Stone and of Lightning): lit from behind
    };
    constexpr uint32_t kWholePlacement = 0xFFFFFFFFu;

    struct Building
    {
        uint32_t map = 0, uniqueId = 0, group = kWholePlacement, style = 0, seed = 0, flags = 0, area = 0;
        float    pos[3] = {};          // the placement's origin (its WMO's 0, 0, 0 in the world)
        uint32_t path = 0;             // PathHash of its WMO's path (0: any)
        uint32_t id = 0;               // its GlassBuildings row ID (WindowRooms.cdbc names it)
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
        /// The row with this GlassBuildings ID (null: none).
        const Building* ById(uint32_t id) const;
        size_t Placements() const { return m_placements; }
        /// A city's group rows found under a placement.
        size_t Groups() const { return m_groups.size(); }
        size_t Size() const { return m_rows.size(); }

    private:
        std::vector<Building> m_rows;
        std::unordered_map<uint32_t, uint32_t> m_byId;
        std::unordered_map<uint64_t, std::vector<uint32_t>> m_grid;   // (map, 8-yd cell) -> placement rows
        std::unordered_map<uint64_t, uint32_t> m_groups;              // (placement row, group) -> row
        std::unordered_map<uint64_t, uint32_t> m_defaults;            // (map, path) -> row; (any map, path) too
        size_t m_placements = 0;
    };

    // The shader's room (features/GlassShader), its numbers that aren't env/GlassRooms' (the box, the furniture, the
    // textures): the hash, the building's variety, the light by day and night, the glass's day look, lit or dark.
    namespace room
    {
        constexpr float kHashK[4] = { 0.1031f, 0.1030f, 0.0973f, 0.1099f };  // the hash (Dave Hoskins' hash42)
        constexpr float kHashAdd = 33.33f;
        /// A dark room by day keeps this much light (no lamp, the curtains drawn: dimmer, not a black hole).
        constexpr float kDarkDay = 0.6f;
        constexpr float kCurtainDay = 1.3f, kCurtainGain = 0.8f;  // cloth: 1.3 x the day in front, 0.8 x the lamp behind
        constexpr float kBlock = 14.0f;                           // yd: a city's blocks (one building each)
        /// By day the light comes in at the window: farther from it darker still (1 / (1 + (rooms::kFar + kFarDay x the
        /// rooms' day) deep), deep in yd: c86.y), and the sun falling in makes a patch on the floor (kPatch x the sun through
        /// x its cosine to the face: c86.x) by the window: across kPatchAcross of the room's half width (soft over kPatchSoft
        /// yd), up to kPatchDeep yd in (soft over the last kPatchDeepSoft); c83, c84.zw.
        constexpr float kFarDay = 0.22f;
        constexpr float kPatch = 0.8f;
        /// ... and the light falls downward from the window: the lower walls lighter, the upper darker, x (1 + kFallDay (0.5 -
        /// up)) (c86.z), and the curtains kCurtainDayGrey of the way to their grey (seen through the cooler glass, a red
        /// cloth reads pink otherwise).
        constexpr float kFallDay = 0.6f, kCurtainDayGrey = 0.5f;
        constexpr float kPatchAcross = 0.7f, kPatchSoft = 0.35f, kPatchDeep = 1.8f, kPatchDeepSoft = 0.9f;
        /// The lamp-lit room's mean light (the lamp's falloff x the faces' shades x the corners, over many rooms and views,
        /// measured on WARP by tests/glass): the lamp's colour is divided by it so a lit room is about as bright as the
        /// client's window.
        constexpr float kNightMean = 0.62f;
        /// By day the room is seen through the glass: its own tint x the glass's luminance (after the darkening), its
        /// light (the window's falloff x the faces' shades x the corners, plus the sun's patch, as bright as a dark room
        /// keeps, over many rooms and views, measured on WARP by tests/glass) over kDayMean, x kDayRoom, so the room is about
        /// kDayRoom of the glass's brightness: darker than the glass's sunlit face, which makes it read as depth.
        /// (Calibrated: tests/glass's rooms by day, "the room alone", measured 1.12 of its reference with 0.42 and
        /// kDayRoom 0.85.) kDayRoom is over the client's light x kGlassAlbedo, a dim glass's: on the leaded and clear
        /// windows' brighter painted glass the room is about 0.9 of it, and the painted glass in front and its cooler hue
        /// subdue it further (by day the leaded windows keep 70% of the client's light, at dusk 75%).
        constexpr float kDayMean = 0.553f;
        constexpr float kDayRoom = 1.15f;
        /// The day's light in the room is coloured: the sky's and the sun's through a pane at luminance 1 (orange at dusk,
        /// a little blue in the shade), kDayHue of the way from white.
        constexpr float kDayHue = 0.75f;
        /// By day seen from outside the painted glass is cooler (DayCool, c85; the room behind it keeps its colours, which
        /// makes it stand out): the share of the way to the sky's hue at its
        /// luminance, DayCoolness x the kind's (Look.cool) x the day, x (1 + kCoolUp x the reflected ray's height + kCoolGraze
        /// x (1 - cos)): more toward the top of the pane and at a slant; kCoolTint of it the sky's hue, the rest grey (less
        /// saturated). The sky's hue: its colour over its luminance, each channel within kSkyHue (a green or red fog
        /// doesn't paint the glass).
        constexpr float kCoolUp = 0.8f, kCoolGraze = 0.6f, kCoolTint = 0.6f;
        constexpr float kSkyHue[2] = { 0.6f, 1.5f };
        constexpr float kZenith[3] = { 0.7f, 0.85f, 1.25f };      // the reflected sky toward the zenith: x this (c211)
        /// At night the glass reflects the dark sky with F0 kNightF0 x the kind's, as before glass-day2 (ReflectionF0 is the
        /// day's; between them by the day).
        constexpr float kNightF0 = 0.1f;
        constexpr float kDayVariety = 0.25f;   // by day the buildings' tints and brightness spread this share of the night's
        /// Lit or dark: an exact hash (LitHash) of the window's key (its ordinal in its WMO, its group) and the seed, mod 289
        /// (every step a whole number below 2^22, so any GPU, WARP and the CPU give the same bits), against the dark band
        /// (DarkBand).
        constexpr float kLitMod = 289.0f, kLitInv = 1.0f / 289.0f, kLitPoly = 34.0f;
        /// The narrowest band a moving threshold makes (1 / this: the steepest slope).
        constexpr float kBandMin = 1e-6f;
        /// A dark room at night (its lamp out): the moon's and the night's light come in at its window, DarkRoomLight of a lit
        /// room's brightness (its furniture dim shapes, its corners darker), in the moon's cool colour: kMoonHue (luminance 1
        /// after normalising) kMoonClient of the way to the hue of the client's own light on the land. Underground (Ironforge)
        /// no moon: the city's warm ambient, kUndergroundHue. kMoonMean: that light's mean in a room at night (the window's
        /// falloff x the faces' shades x the corners, over many rooms and views, measured on WARP by tests/glass), so
        /// DarkRoomLight is the share of a lit room's light.
        constexpr float kMoonHue[3] = { 0.72f, 0.88f, 1.35f };
        constexpr float kMoonClient = 0.5f;
        constexpr float kUndergroundHue[3] = { 1.12f, 0.97f, 0.8f };
        constexpr float kMoonMean = 0.7f;
    }

    // --- lit or dark, shared by the shader and the window lights (features/Windows) ---------------------------------

    /// The room's lit hash (0..1, never 0 or 1: (k + 0.5) / 289) for the window's key (cellX its ordinal, cellY its
    /// group: whole numbers) and the seed (two whole numbers 0..1023), as the shader works it, op for op.
    float LitHash(float cellX, float cellY, float seedX, float seedY);
    /// The same with whole numbers (the reference: k in 0..288).
    uint32_t LitHashIndex(int64_t x, int64_t y);

    /// Which rooms are lit: a room is dark when its hash is at or below `lo`, lit above `hi`, and in between (the
    /// rooms the threshold crossed in the last kFadeSeconds) part lit, saturate((hash - lo) x slope + base). At rest
    /// lo = hi: a plain threshold.
    struct DarkBand { float lo = 0.0f, hi = 0.0f, base = 0.0f, slope = 0.0f; };
    /// The clock running: the share of rooms dark now and kFadeSeconds ago. A room the threshold crossed in between
    /// fades by when it did (base 0, slope 1 / (hi - lo)): going dark at night, lit again at dawn, each over the
    /// same real seconds.
    DarkBand Band(float shareNow, float sharePast);
    /// After a jump (the time override set, the night test): every room between the share it jumped from and the
    /// one it jumped to fades together, `progress` 0..1 of the way (base 1 - progress going dark, progress lit again).
    DarkBand Crossfade(float shareFrom, float shareTo, float progress);
    /// How lit a room is in a band, as the shader works it (add, mul, add_sat, cmp, add, cmp).
    float RoomLit(float hash, const DarkBand& band);

    /// The settings the dark share depends on (features/Glass's Settings).
    struct RoomToggles
    {
        bool  variety = true, lightsOut = true;
        float varietyStrength = 1.0f, lightsOutStrength = 1.0f;
    };
    /// The share of a building's rooms dark (the shader's c203.w, before the band): by day the style's few (x the
    /// variety's strength), at night by the hour (LightsOutShare, x its strength; with room variety and lights out
    /// on), blended by the night factor; every room in an empty building (with variety), and at night in a dark place.
    float DarkShare(const RoomStyle& style, uint32_t buildingFlags, const RoomToggles& t, float night, float hour);

    /// The rooms' clock for lights out: the hour (0..24) and the night factor as the rooms show them. Running (a game
    /// second a real second; the night factor fading with the client's windows over a game hour) it keeps the last
    /// kFadeSeconds, so each room the threshold crosses fades over them (Band). A step faster than kJumpRate times that
    /// (the climate panel's time set or dragged, the night test) is a jump: the rooms cross-fade from the time shown to
    /// the new one over kFadeSeconds (Crossfade); while the jumps keep coming (a slider dragged) they stay as they were
    /// until it settles. After kSnapSeconds without a step (a loading screen), or at the first, it snaps (no fade).
    class RoomClock
    {
    public:
        static constexpr double kFadeSeconds = 1.5;   // a room going dark or lit fades over this
        static constexpr double kJumpRate = 20.0;     // faster than this many game seconds a real second is a jump
        static constexpr double kSnapSeconds = 2.0;
        /// One step at `seconds` (real, steady) to the hour and night factor of the moment.
        void Advance(double seconds, float hour, float night);
        void Reset() { m_count = 0; m_fading = false; }
        float Hour() const { return m_hour; }      // now (cross-fading: where it's going)
        float Night() const { return m_night; }
        float HourPast() const;                    // kFadeSeconds ago (running)
        float NightPast() const;
        bool  Fading() const { return m_fading; }  // cross-fading after a jump
        float Progress() const { return m_progress; }
        float FromHour() const { return m_fromHour; }
        float FromNight() const { return m_fromNight; }

    private:
        struct Sample { double t, hour; float night; };   // hour unwrapped (keeps counting past 24)
        void Past(double& hour, float& night) const;
        void Restart(double seconds);
        static constexpr int kSamples = 512;
        Sample m_ring[kSamples] = {};
        int m_first = 0, m_count = 0;
        float m_hour = 12.0f, m_night = 0.0f, m_fromHour = 12.0f, m_fromNight = 0.0f, m_progress = 1.0f;
        bool m_fading = false;
    };

    /// A building's dark band now (its style, its flags): Band of the share now and kFadeSeconds ago, or Crossfade
    /// from the share before a jump.
    DarkBand BandOf(const RoomClock& clock, const RoomStyle& style, uint32_t buildingFlags, const RoomToggles& t);

    /// One cell of a window light's glass (WindowRooms.cdbc: a room's, or lit frosted glass's): its lit hash, its share
    /// of the light's glass, its building's dark band now, and the flicker's dip now (HearthDip).
    struct LightCell { float hash = 0.5f, share = 0.0f; DarkBand band; float dip = 0.0f; };
    /// How much of a window light's glass is lit: the glass with no cell (stained, not in GlassMaterials: 1 - the cells'
    /// shares) always, plus each cell's share x RoomLit x (1 - its dip). 0..1.
    float LitShare(const LightCell* cells, size_t count);

    // --- the hearth's flicker (lit frosted glass; rooms' lamps with LampFlicker) ----------------------------------------
    // env/Flame's three rates and weights (env/Lights' flame flicker) at the hearth's pace, each rounded to a whole
    // number of cycles in kWrap seconds, so the clock (HearthClock) wraps without a seam and the shader only ever sees
    // small numbers. Each wave a parabola through a cycle (4 q (1 - |q|), q = 2 frac(x) - 1: a sine within 6%), the
    // same ops on the CPU and in the shader (c201.y the clock, c201.z the amplitude; c220, c221 defined).
    namespace hearth
    {
        constexpr float kWrap = 1024.0f;              // s
        constexpr double kPace = 0.5;                 // x env/Flame's rates: a hearth through frosted glass, ~0.6 .. 1.8 Hz
        constexpr float Cycles(double radPerSecond)
        {
            return static_cast<float>(static_cast<int>(radPerSecond / 6.283185307179586 * kPace * 1024.0 + 0.5)) / 1024.0f;
        }
        constexpr float kRate[3] = { Cycles(7.13), Cycles(13.71), Cycles(23.17) };   // cycles a second (env/Flame kRates)
        constexpr float kWeight[3] = { 1.0f, 0.6f, 0.4f };   // 2 x env/Flame's 0.5, 0.3, 0.2: f = 0.5 + sum w q (1 - |q|), 0..1
        constexpr float kSpread = 640.0f;             // s: a window's clock runs its lit hash x this ahead (its own phase)
        constexpr float kFrostedHidden = 0.85f;       // at night a dark frosted window keeps 15% of its glow (a banked fire)
    }
    /// The flicker's dip at a window (its lit hash) on the hearth clock: amplitude x f, f 0..1 (mean 0.5), as the
    /// shader works it. The glass's glow and its window light go down by it.
    float HearthDip(float hash, float clock, float amplitude);
    /// The hearth clock: seconds x the flicker's speed, steady (a speed change doesn't jump it), wrapping at kWrap.
    class HearthClock
    {
    public:
        void Advance(double seconds, float speed);
        void Reset() { m_have = false; }
        float Now() const { return static_cast<float>(m_clock); }
    private:
        double m_clock = 0.0, m_last = 0.0;
        bool m_have = false;
    };

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

    /// The room's detail by the distance (c218: GlassShader.hpp): base and slope over the distance squared, all of it
    /// within rooms::kDetailNear yd, none from rooms::kDetailFar (the textures, the furniture and the corners to their mean).
    void RoomLook(float out[4]);

    /// The sky along the ray reflected off the glass as the shader has it: `reflected` (c196) x lerp(1, kZenith, its
    /// height), the height saturate(2 cos N.z - V.z) (N facing the eye V, world, unit). Returns the height.
    float SkyAlong(const float N[3], const float V[3], const float reflected[3], float out[3]);
    /// By day seen from outside, the painted glass cooler (GlassShader, op for op, after its darkening and before the room
    /// and the sky's reflection: the room keeps its own colours): the outside colour toward its own luminance x lerp(1,
    /// the sky's hue, c85.w), the sky's hue `sky`'s
    /// colour over its luminance (at least 0.05) within kSkyHue; by saturate(c85.x (1 + c85.y height + c85.z (1 - cos)))
    /// (c85.x 0: none, at night).
    void DayCool(const float outside[3], const float sky[3], float height, float cosine, const float c85[4], float out[3]);
}
