// Window glass, the runtime half (the rules: env/GlassModel; the shader blocks: features/GlassShader). The glass
// textures and their kinds come from GlassMaterials.cdbc, the panes from GlassPanes.cdbc (tools/gen_glass.py,
// docs/cdbc-tables.md).
//  - The look (b9): features/WmoIce's batch hooks ask, per batch, whether its material's texture is glass; this
//    sets the batch's constants (its kind's look, its mask, inside or outside) and switches the block on.
//  - The floor light (b10): each frame the sunlit panes nearest the camera (up to kFloorPanes) become projection
//    rows; the batches of a building's interior groups (not its glass, not a sand floor's rims) take them.
//  - The coloured shafts: features/GlassShafts marches the room's air with the panes whose room holds the camera.
//  - The rooms behind clear glass, one per window (GlassWindows.cdbc: env/GlassRooms' WindowTable, a texture the
//    shader looks the pixel's window up in), per building (RoomStyles.cdbc, GlassBuildings.cdbc): features/WmoIce's
//    group hook (Wmo.RenderGroup: the placed instance, its placement matrix and the group) names the building and its
//    frame before its batches draw; each glass batch then gets its style (tints, brightness, lamp, curtains, the dark
//    share by the hour), its seed, the group's grid in the window table, the frame that takes the point into its WMO's
//    space, its class's room depth and furniture, and its palette's interior textures (client BLPs, loaded the first
//    time a palette is seen; s8..s11) with the furniture atlas (s12) and the table (s13). A batch whose material the
//    client lights at night (MOMT SIDN) gets a lamp-lit room.
//    The dark share goes by the rooms' clock (RoomClock: never jumping, so a room fades over 1.5 s); features/Windows
//    asks for the same band (BandNow) so the window lights dim with the rooms behind them.
//  - Frosted glass the client lights (SIDN, or unlit: at full day and night) goes dark the same way without a room
//    (FrostedBand: its building's hours; Ironforge's parts their own), and a lit one flickers like a hearth on the hearth
//    clock (HearthDip); features/Windows dims and flickers the window lights in front with it (FrostedBandNow,
//    HearthClockNow, HearthAmplitudeNow). A dungeon's frosted glass keeps the client's look.
//  - Underground (GlassBuildings' kBuildingUnderground: Ironforge's interior groups) the glass has the street's look
//    with no sky, sun or glint in it, and its panes (GlassPanes' kPaneNoSky) light no floor and make no shafts.
//  - Behind frosted glass a heavily blurred room (FrostedRooms; not a dungeon's), behind stained glass a faint one tinted
//    by its colours (StainedRooms; its windows in grids of their own, not a dungeon's, not titan glass lit from behind):
//    "the rooms behind the windows look gorgeous but when lights in the windows are turned off I can't see the rooms at
//    all. It also seems like rooms behind Dalaran and Ironforge windows don't work?" (the user, 2026-10-20). A dark room at
//    night keeps DarkRoomLight of a lit one's light: the moon's and the night's, at its window (Light::moon); underground
//    the city's warm ambient, and a frosted room there lamp-lit at any hour (no daylight).
//  - Titan glass with no sky behind it (GlassBuildings' kBuildingBacklit, GlassMaterials' Backlit: Ulduar, the Halls of
//    Stone and of Lightning) is lit from behind: its batches take the inside look with a steady light behind them (its
//    texture's strength, General Vezax's with a slow pulse: BacklitConstants; no sky, sun or glint), the same at any hour,
//    and its panes (GlassPanes' kPaneBacklit) light the floor and the air from a light behind each (LightOfPane).
#pragma once

#include "../env/GlassModel.hpp"
#include "../env/GlassRooms.hpp"
#include "GlassShader.hpp"

#include "wxl/PluginApi.h"

#include <cstdint>
#include <vector>

struct WXL_SeyrisCdbcApi;

namespace wxl_livingazeroth::glass
{
    /// Leaded glass's share of the room (x RoomShareDay, RoomShareNight): its lead hides a little of it.
    constexpr float kLeadedRoom = 0.85f;
    /// Frosted and stained glass's (with FrostedRooms, StainedRooms): the frosted glass in front dominates; stained glass
    /// shows a faint room, mostly its own colours.
    constexpr float kFrostedRoom = 0.75f, kStainedRoom = 0.45f;
    /// Behind frosted glass the room's footprint at least kFrostedBlur yd (its textures at a low mip, its furniture's and
    /// curtains' edges soft), its faces' shades kFrostedFlatten of the way to kFaceMean (no crisp corner); behind stained
    /// glass the room x lerp(1, the glass's hue, kStainedTint).
    constexpr float kFrostedBlur = 0.3f, kFrostedFlatten = 0.5f, kFaceMean = 0.75f, kStainedTint = 0.75f;

    struct Settings
    {
        int   enabled = 1;              // everything below
        int   insideGlow = 1;           // seen from inside: the light coming through
        int   outsideReflection = 1;    // seen from outside: the glass a little darker by day, the sky in it, the sun's glint
        int   fakeInteriors = 1;        // seen from outside: a room behind clear and leaded glass
        int   colouredShafts = 1;       // features/GlassShafts
        int   floorLight = 1;           // light through sunlit panes on the room's floor and walls
        float insideStrength = 1.0f;    // x the light coming through
        float reflectionStrength = 1.0f;// x the reflection and the glint
        float interiorStrength = 1.0f;  // x the room's light (1: by day about room::kDayRoom of the glass)
        float shaftStrength = 1.0f;     // x the coloured shafts
        float floorStrength = 1.0f;     // x the floor light
        float range = 70.0f;            // yd from the camera: the panes the floor light and the shafts use
        int   roomVariety = 1;          // the rooms differ by building: tint, brightness, a few dark (RoomStyles.cdbc)
        int   litRooms = 1;             // at night, lamp-lit rooms behind the windows the client lights
        int   lightsOut = 1;            // ... more of them dark as the night goes on (inns stay lit late)
        int   roomLayouts = 1;          // curtains half drawn in some rooms (the style's share)
        float varietyStrength = 1.0f;   // x how far the buildings' tints, brightness and dark rooms spread
        float litRoomStrength = 1.0f;   // x the lit rooms' brightness (1: about the client's own lit window)
        float lightsOutStrength = 1.0f; // x the share of rooms dark at night
        float layoutStrength = 1.0f;    // x the furniture's and the curtains' detail (their folds)
        int   roomDetail = 1;           // furniture in the rooms (tables, beds, shelves, hearths...), at its depth
        int   roomTextures = 1;         // the rooms' floors, walls and ceilings in the palette's client interior textures
        // Seen from outside (the kinds' shares of these: features/Glass's Look).
        float outsideDarkening = 0.45f; // by day clear glass this much darker than the client's own (leaded 0.6 x it, stained and frosted 0.2 x)
        float reflectionF0 = 0.2f;      // by day the sky's share in the glass head on (clear 1.2 x it, leaded 1 x, stained and frosted 0.5 x; to 1 at grazing; at night 0.1 x the kind's)
        float dayCoolness = 0.25f;      // by day the painted glass this far toward cool glass (its luminance in the sky's hue; more up the pane and at a slant; frosted 0.6 x, stained 0.25 x)
        float roomShareDay = 0.65f;     // by day (and at dusk) the room behind clear glass shows this much (leaded kLeadedRoom x it)
        float roomShareNight = 0.75f;   // at night (the lamp-lit rooms) this much (leaded kLeadedRoom x)
        // Frosted glass (no room behind it): the windows the client lights.
        int   frostedLightsOut = 1;     // they go dark through the night as rooms do (the building's hours; Ironforge's parts their own)
        float hearthFlicker = 0.08f;    // a lit one's glow dips by up to this (a hearth behind it), x the night; 0 = steady
        float flickerSpeed = 1.0f;      // x the hearth's pace (~0.6 .. 1.8 Hz; the lamps' too)
        float lampFlicker = 0.0f;       // the same for the lamp-lit rooms behind clear and leaded glass (0 = steady lamps)
        int   ironforgeAlwaysLit = 0;   // Ironforge's windows lit all night (they still flicker)
        // Titan glass with no sky behind it (Ulduar, the Halls of Stone and of Lightning).
        int   titanBacklit = 1;         // lit from behind: it glows at any hour, its colours on the floor and in the air (0: the sun's, as other glass)
        float backlitStrength = 1.0f;   // x its glow and the light it casts
        float vezaxPulse = 1.0f;        // x the slow pulse of General Vezax's glass (0: steady)
        // The rooms behind frosted and stained glass; the dark rooms at night.
        int   frostedRooms = 1;         // behind frosted glass a heavily blurred room, its lamp a soft glow (not a dungeon's)
        int   stainedRooms = 1;         // behind stained glass a faint room tinted by its colours (not a dungeon's, not titan glass lit from behind)
        float darkRoomLight = 0.12f;    // a dark room at night keeps this share of a lit one's light: the moon's and the night's at its window
    };
    Settings& Config();

    /// (Re)loads GlassMaterials.cdbc, GlassPanes.cdbc, RoomStyles.cdbc and GlassBuildings.cdbc (the lights panel's
    /// reload button calls it too).
    void Load(const WXL_SeyrisCdbcApi* cdbc);
    const char* Status();
    /// Status() to the log (wxl-core.log): each table's rows and what Load kept of them; a warning when a table is
    /// missing or no rooms came of them (no windows kept, no placements). After each Load.
    void LogLoad(const WXL_Api* api);
    /// Whether the last Load warned (LogLoad's level).
    bool LoadProblem();
    uint32_t    Generation();   // changes on every load

    /// One glass batch's constants (c192..c205, GlassShader.hpp): its material's look, seen from inside by `inside`
    /// (InsideWeight), in this light (Evaluate), the sun (toward it, world) and its colour, the sky's colour, the settings.
    /// The room is the plain warm one (as before room variety); RoomBatchConstants makes it a building's.
    void BatchConstants(const Material& m, float inside, const Light& light, const float sun[3], const float sunColour[3], const float sky[3],
                        const Settings& s, float out[4 * kConstantCount]);
    /// The room's detail by the distance for a batch (c218.xy: env/GlassModel's RoomLook) and its light lit and dark as by
    /// day (c218.zw: 1 - room::kDarkDay, room::kDarkDay; RoomBatchConstants sets a batch's own with the moon's at night).
    void LookConstants(const Light& light, float out[4 * kLookRegisters]);
    /// The room's registers for a batch (c15..c27: GlassShader.hpp): the instance's frame (`axes`: its WMO's x, y and z in
    /// the world, three rows; `origin` where its 0, 0, 0 stands) about the camera; the group's grid in the window table
    /// (null: no rooms) and the table's height; the building's class (its rooms' depth and furniture: RoomKindOf); the
    /// texture set's scales (yd a repeat) and gains (to a mean of 1); the settings (RoomDetail, LayoutStrength); the glass's
    /// kind (c26, c27: frosted glass's blur and its windows' rooms with FrostedRooms, stained glass's tint with
    /// StainedRooms; any other kind, or either off, (0, 0, 0, kFaceMean) and (1, 0, 0, 0): the room as it was); c27.zw the
    /// texture's corner that isn't glass (`notGlass`: Material::notGlass, null or 1 none) as 1 - where it starts (0, 0: none).
    void RoomRegisters(const float axes[9], const float origin[3], const float camera[3], const WindowTable::Grid* grid, int tableHeight,
                       uint32_t cls, const float scales[4], const float gains[4], const Settings& s, float out[4 * kRoomRegisters],
                       uint32_t kind = Kind::kClear, const float notGlass[2] = nullptr);
    /// Whether a placement matrix's rotation (three rows) is a rotation: unit rows at right angles (else no rooms).
    bool IsRotation(const float axes[9]);
    /// The windows as loaded (GlassWindows.cdbc; for the tests and the panel).
    const WindowTable& Windows();
    /// The day's look for a batch (c85, c86): the glass cooler (DayCoolness x the kind's x the day; none matte, underground
    /// or without OutsideReflection) and the room by day (the sun's patch x the sun through, farther darker x the light's
    /// rooms). c85.x and c86 are 0 at night (the night's look).
    void DayConstants(const Material& m, const Light& light, const Settings& s, uint32_t buildingFlags, float out[4 * kDayRegisters]);

    /// The room for one glass batch: its building's style (a default house when `building` is null), the night
    /// (the client's window fade, 0..1), the hour (0..24), whether the client lights this material at night (SIDN).
    struct RoomInputs
    {
        float night = 0.0f, hour = 12.0f;
        bool selfLit = false;
        uint32_t fallbackSeed = 0;
        /// The rooms' clock (the dark band's fades); null: `hour` and `night`, at rest.
        const RoomClock* clock = nullptr;
        /// The instance's origin (world; the building's when it's a placement in GlassBuildings), null: unknown.
        const float* origin = nullptr;
        /// The batch's glass kind (GlassMaterials), whether the client draws it unlit (MOMT 0x1: at full day and night),
        /// and the hearth clock now (HearthClock).
        uint32_t kind = kNone;
        bool unlit = false;
        float hearthClock = 0.0f;
    };
    /// The settings the dark share depends on.
    RoomToggles Toggles(const Settings& s);
    /// A building's dark band (BandOf the clock; without one, the share at `hour` and `night`, at rest).
    DarkBand RoomBand(const RoomStyle& style, uint32_t buildingFlags, const RoomInputs& in, const Settings& s);
    /// Lit frosted glass: frosted, lit by the client at night (SIDN) or unlit, not a dungeon's, and FrostedLightsOut or
    /// HearthFlicker on.
    bool FrostedLit(uint32_t kind, bool selfLit, bool unlit, uint32_t buildingFlags, const Settings& s);
    /// The toggles frosted glass's dark share goes by: FrostedLightsOut, LightsOutStrength (not the rooms' variety).
    RoomToggles FrostedToggles(const Settings& s);
    /// Whether a building is Ironforge (its WMO's path).
    bool IsIronforge(const Building* b);
    /// The dark band of lit frosted glass in a building (null: the default house): every window lit with
    /// FrostedLightsOut off, and Ironforge's with IronforgeAlwaysLit; else as a room's, with FrostedToggles.
    DarkBand FrostedBand(const RoomStyle& style, const Building* building, const RoomInputs& in, const Settings& s);
    /// The flicker's amplitude at this night factor: `amount` (HearthFlicker, LampFlicker) x the night, 0..1.
    float FlickerAmplitude(float amount, float night);
    /// Rewrites the room's part of `out` (c193.z for a dungeon's frosted or stained glass, c198, c199.rgb, c200.w,
    /// c201.yzw, c202..c205; `out` as BatchConstants left it: the day's room light goes by its darkening, c197.x) and fills
    /// `seed` (c212: the seed, the block, the dark band's slope; c213: the building's origin, else the instance's, and the
    /// band's base). The band: c203.w lo, c200.w hi. Lit frosted glass (FrostedLit) gets its band, c201.w 1, the glow a
    /// dark one hides at night (c205.w) and the hearth's flicker (c201.y the clock, c201.z its amplitude); a lamp-lit room
    /// the lamps' (LampFlicker); a frosted room (FrostedRooms) the frosted band and the lamp behind lit frosted glass,
    /// underground lamp-lit at any hour with no daylight. Underground (kBuildingUnderground) the street's look without
    /// sky: c193.x, c196.w, c197.xy 0. A dark room at night (DarkRoomLight): c198.w has the moon's light too (the moon's
    /// part, MoonRoomLight) and c198.rgb its colour mixed in by its share; `look` (c218, when given) its .zw so only dark
    /// rooms take it (lit x .z + .w; with no lamp, every room): exactly the day's (1 - kDarkDay, kDarkDay) without it.
    void RoomBatchConstants(const RoomStyle& style, const Building* building, const Light& light, const RoomInputs& in, const Settings& s,
                            float out[4 * kConstantCount], float seed[4 * kSeedRegisters], float look[4 * kLookRegisters] = nullptr);
    /// The moon's light in a dark room for a batch (c198.w's part): DarkRoomLight x `night` / (room::kMoonMean x the
    /// building's tints' mean luminance), so a dark room is about DarkRoomLight of a lit one (whose lamp is the client's
    /// window's light / room::kNightMean). 0 with DarkRoomLight 0 or not finite.
    float MoonRoomLight(float night, const float tintA[3], const float tintB[3], const Settings& s);
    /// Whether a batch's glass has a room behind it now by kind: clear and leaded, frosted with FrostedRooms (not a
    /// dungeon's), stained with StainedRooms (not a dungeon's).
    bool KindHasRoom(uint32_t kind, uint32_t buildingFlags, const Settings& s);
    /// The fallback seed of an instance the tables don't have: its WMO's PathHash when known (the same for every copy,
    /// and features/Windows can know it), else the hook's.
    uint32_t FallbackSeed(uint32_t path, uint32_t hookSeed);

    // --- titan glass lit from behind --------------------------------------------------------------------------------
    /// A batch lit from behind: TitanBacklit on, its material backlit glass (GlassMaterials' Backlit) and its building's
    /// with no sky behind its glass (kBuildingBacklit).
    bool BacklitBatch(const Material& m, uint32_t buildingFlags, const Settings& s);
    /// The pulse's scale for a material: VezaxPulse for General Vezax's glass (kBacklitPulse), else 1 (the others' shimmer).
    float PulseScale(const Material& m, const Settings& s);
    /// Rewrites a backlit batch's look over what BatchConstants and RoomBatchConstants made: c193.x 1 (the inside look,
    /// whatever the camera's indoors blend), c193.z 0 (no room); c194.rgb its glow at `seconds` on the pulse's clock
    /// (BacklitGlow), c194.w 0 (no sun through it); c196.w, c197.xy 0 (no sky in it, no darkening, no glint); c199.w 1;
    /// c201.w 0; and its day look's c85.x, c86.x 0. No input depends on the hour: the same at noon and at midnight.
    void BacklitConstants(const Material& m, double seconds, const Settings& s, float out[4 * kConstantCount], float day[4 * kDayRegisters]);
    /// A pane's light this frame, for the floor light and the coloured shafts: toward its light (world, unit) and its
    /// colour (the floor block x kFloorGain). A backlit pane (kPaneBacklit, TitanBacklit on, its material backlit glass):
    /// the light behind it (BacklitDirection; BacklitGlow x backlit::kFloorShare x its kind's through x its light), at any
    /// hour. Else the sun through it (the sun's direction; its light x the sun's colour x the kind's through x the sun's
    /// gain): false with the sun down (gain 0.01 or less), behind the pane (out . sun 0.05 or less), or the pane underground
    /// (kPaneNoSky).
    struct PaneLight { const Pane* pane = nullptr; float dir[3] = { 0, 0, 1 }; float colour[3] = {}; bool backlit = false; };
    bool LightOfPane(const Pane& p, const Material* m, const Light& light, const float sun[3], const float sunColour[3], double seconds,
                     const Settings& s, PaneLight& out);
    /// The direction the floor light's facing (c195 in its batches) and the shafts' forward scattering use for a set of
    /// panes: with a backlit pane among them, their lights' directions weighted by their colours' luminance (MeanDirection;
    /// from overhead if they cancel); sunlit panes alone (or none), the sun's, exactly as before.
    void LightFacing(const PaneLight* lights, size_t n, const float sun[3], float out[3]);

    // --- for the window lights (features/Windows) -------------------------------------------------------------------
    /// Whether a dark room hides the client's lit window at night now: the glass on, its blocks in the shaders, the
    /// rooms (FakeInteriors with OutsideReflection) and lamp-lit rooms on.
    bool RoomsHideGlow();
    /// Whether dark frosted glass hides its glow now (the glass on, its blocks in the shaders, FrostedLightsOut).
    bool FrostedHideGlow();
    /// Whether stained glass has rooms now (the glass on, its blocks in the shaders, FakeInteriors with OutsideReflection,
    /// StainedRooms): the window lights read WindowRooms' stained cells, else its cells as they were before.
    bool StainedRoomsOn();
    /// Whether a dark stained room hides its glass's glow now (StainedRoomsOn and RoomsHideGlow).
    bool StainedHideGlow();
    /// This frame's dark band for lit frosted glass in a building (null: the default house).
    DarkBand FrostedBandNow(const RoomStyle& style, const Building* building);
    /// This frame's hearth clock and the flicker's amplitudes (the glass's, as its batches get them): frosted glass's
    /// (HearthFlicker x the night; 0 with the glass off), and the lamp-lit rooms' (LampFlicker x the night, while dark
    /// rooms hide the glow).
    float HearthClockNow();
    float HearthAmplitudeNow();
    float LampAmplitudeNow();
    /// This frame's dark band for a building (its style, its flags), as this frame's glass batches get it.
    DarkBand BandNow(const RoomStyle& style, uint32_t buildingFlags);
    /// The rooms' clock (for the panel and the tests).
    const RoomClock& Clock();
    /// The built-in style (a human house) for buildings the tables don't have.
    const RoomStyle& DefaultStyle();
    /// The rooms' tables as loaded (for the tests and the panel).
    const BuildingIndex& Buildings();
    const RoomStyle* StyleById(uint32_t id);
    /// The hour (0..24) the rooms' lights go by: the climate panel's override, else the clock.
    float HourNow();

    /// The glass table's row for a texture path (any case or slashes): its index + 1, 0 when it isn't glass.
    uint32_t MaterialOf(const char* texturePath);
    const Material* MaterialAt(uint32_t indexPlusOne);
    /// The material with this GlassMaterials ID (a pane's Material), null: none.
    const Material* MaterialById(uint32_t id);
    /// What the floor light and the shafts use (for the tests and GlassShafts).
    const PaneIndex& Panes();

    /// Once per frame (Extension OnUpdate, main thread): the light of the moment and the panes near the camera.
    void Update();

    // --- features/WmoIce's batch hooks ------------------------------------------------------------------------
    /// The variants that carry the blocks (counted by features/ModelIce's WMO edit).
    void NotePatchedVariant();
    unsigned PatchedVariants();
    /// Around a placed instance's group (Wmo.RenderGroup), before its batch loops: where the instance stands (its
    /// origin, world; null when unknown), its placement's rotation (its WMO's x, y and z in the world, three rows; null
    /// when unknown or not a rotation: no rooms), its WMO's path (PathHash, 0 unknown), the group's index, and a seed for
    /// a building the tables don't have (the instance's address: steady while it's loaded; FallbackSeed prefers the path).
    void BeginInstance(const float pos[3], const float axes[9], uint32_t path, uint32_t group, uint32_t fallbackSeed);
    void EndInstance();
    /// Around a group's batch loop: its MOGP flags, and whether its root has glass materials.
    void BeginGroup(uint32_t groupFlags, bool rootHasGlass);
    /// Per batch: its material's glass row (0 none), whether the rims hold c101.. for it, whether the client lights its
    /// material at night (MOMT SIDN: the lamp-lit room) and whether it draws it unlit (MOMT 0x1: at full day and night;
    /// frosted glass so is lit).
    void Batch(uint32_t glassMaterial, bool rimsOn, bool selfLit, bool unlit);
    void EndGroup();
    /// The rims (WmoIce) just wrote c101..c172 (and c180..c189): the panes go up again before the next floor-light batch.
    void PaneRangeTaken();
    /// The ice (WmoIce) just wrote c180..c190 with its own view: ours go up again before the next floor-light batch.
    void ViewTaken();

    /// The coloured shafts' panes this frame: sunlit or backlit, whose room holds the camera (up to `max`), each with
    /// its light (toward it, its colour: LightOfPane); false when there are none.
    bool ShaftPanes(std::vector<PaneLight>& out, size_t max);
    /// The sun this frame (toward it, world) and its colour x gain through glass.
    void SunNow(float dir[3], float colour[3]);

    /// The panel section (the lights panel) and one status line for the log; the rooms' line (styles, buildings, how
    /// many instances drawn were found in the tables, the dark share at night now).
    void DrawPanelSection(const WXL_Api* api);
    const char* StatusLine();
    const char* RoomLine();
    /// The windows' line: GlassWindows.cdbc, the table and atlas on the device, the texture sets loaded, last frame's glass
    /// batches with rooms and those without (no placement frame, no windows in their group).
    const char* WindowLine();
    /// Frosted glass's line: lights out, the flicker now, the lit frosted and underground batches drawn last frame, and
    /// Ironforge's parts' dark shares at this hour.
    const char* FrostLine();
    /// Titan glass's line: TitanBacklit, the backlit panes loaded and near the camera; last frame the backlit panes
    /// lighting the floor and the backlit batches drawn; Vezax's pulse now.
    const char* BacklitLine();
}
