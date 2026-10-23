// Storms, the rules (no client access here, so tests/storm can run them offline): which phase a rain
// grade is, how stormy it is, when flashes come, what one flash looks like over time, where strikes
// land, when and how loud their thunder is, and how a flash and the storm's darkness change the
// client's day-night colours. env/Storm applies them to the client.
//
// The server leads, we follow (no guessing ahead): AzerothCore rolls each zone's weather every 10
// minutes, starts new rain light 85% of the time and moves it one step per roll. So light rain is the
// build-up (a darker sky, gusts, haze, rare distant flashes), medium more of the same, and only heavy
// rain brings strikes with thunder.
#pragma once

#include <cstddef>
#include <cstdint>

namespace wxl_livingazeroth::storm
{
    // --- grades and phases ---------------------------------------------------------------------
    // The grade is the weather intensity the server sends (and the client walks toward): AzerothCore's
    // Weather::GetWeatherState sends fine below 0.27, light rain below 0.40, medium below 0.70, heavy
    // from there. Its re-roll grades (light < 0.33, medium < 0.67) differ a little; these are the ones
    // that decide what rain the client shows.
    enum class Phase : int { None = 0, Light, Medium, Heavy };
    constexpr float kRainFrom   = 0.27f;
    constexpr float kMediumFrom = 0.40f;
    constexpr float kHeavyFrom  = 0.70f;
    constexpr float kFullStorm  = 0.90f; // storminess reaches 1 here

    /// The phase of a weather type (0 fine, 1 rain, 2 snow, 3 sand) at a grade. Only rain storms.
    Phase PhaseFor(int weatherType, float grade);
    const char* PhaseName(Phase phase);

    /// 0..1, how stormy: 0 below kRainFrom, rising through light and medium rain, 1 from kFullStorm.
    float Storminess(int weatherType, float grade);

    /// 0..1, how hard it rains: 0 below kRainFrom (and in any weather but rain), 1 from kHeavyFrom. Unsmoothed:
    /// the client walks the grade toward the server's slowly by itself. What the fog wake and mud ruts follow.
    float RainLevel(int weatherType, float grade);

    // --- random numbers (deterministic, so the tests can replay them) ----------------------------
    struct Rng
    {
        uint32_t state = 0x9E3779B9u;
        uint32_t Next();
        float    Uniform();                 // [0, 1)
        float    Range(float a, float b);   // [a, b)
    };

    // --- when flashes come -----------------------------------------------------------------------
    /// Mean seconds between flashes at frequency 1: light rain 80 -> 55 s across its grades, medium
    /// 45 -> 25 s, heavy 20 -> 9 s. 0 = none (fine weather).
    float MeanInterval(Phase phase, float grade);
    constexpr float kMinGap = 3.0f; // never two flashes closer than this

    /// The wait for the next flash: kMinGap plus an exponential share of the rest of the mean.
    float NextWait(Rng& rng, float mean);

    /// Counts down to the next flash. Step() is true on the frame one is due. The first wait after
    /// the weather turns is a random part of the mean (no flash the moment rain starts); a change to a
    /// stormier phase shortens a long wait.
    struct Scheduler
    {
        float wait = -1.0f;   // seconds to the next flash, < 0 = not counting
        Phase phase = Phase::None;
        bool  Step(float dt, Phase now, float grade, float frequency, Rng& rng);
    };

    /// In heavy rain, the share of flashes that are strikes (the rest are flashes in the clouds).
    constexpr float kStrikeShare = 0.65f;

    // --- one flash -------------------------------------------------------------------------------
    constexpr int   kMaxPulses = 4;
    constexpr float kPulseRise = 0.015f;   // seconds to a pulse's peak
    struct Flash
    {
        float start = 0.0f;               // on the storm clock (seconds)
        int   pulses = 0;
        float at[kMaxPulses] = {};        // each pulse's onset after start
        float amp[kMaxPulses] = {};       // its peak, 0..1
        float decay = 0.1f;               // the pulses' decay time constant (seconds)
        float sky = 0.0f;                 // how much it lights the sky and the clouds, 0..1
        float scene = 0.0f;               // how much it lights the ground and all on it, 0..1
        bool  strike = false;
        float distance = 0.0f;            // yd from the player (strikes)
        float pos[3] = {};                // where it struck (strikes)
    };

    /// A flash in the clouds: no bolt, the sky brightens a little (more as the rain gets heavier).
    Flash MakeDistantFlash(Rng& rng, Phase phase, float now);

    /// A strike at a distance (yd): two to four strokes, brighter and lighting the ground the nearer.
    Flash MakeStrike(Rng& rng, float distance, float now);

    /// 0..1: the flash's brightness at a time (each pulse rises in kPulseRise and decays exponentially;
    /// overlapping pulses take the brighter).
    float Envelope(const Flash& flash, float now);

    /// When it has died away (below 0.1% of its peak).
    float EndTime(const Flash& flash);

    // --- where strikes land ----------------------------------------------------------------------
    constexpr float kNearestStrike = 60.0f;  // yd
    constexpr float kFarthestStrike = 2500.0f;
    /// Most far away (700 .. 2500 yd: 70%), some in between (250 .. 700: 23%), few near (60 .. 250: 7%).
    float StrikeDistance(Rng& rng);

    // --- thunder ---------------------------------------------------------------------------------
    constexpr float kYard = 0.9144f;         // metres
    constexpr float kSoundSpeed = 343.0f;    // m/s: about 3 s per km
    /// Seconds from a strike's flash to its sound reaching the player.
    float ThunderDelay(float distanceYards);

    // The recordings thunder plays (SoundEntries.dbc rows of the 12340 client; a row's ID is its sound kit ID). The
    // user's picks from the candidates, 2026-10-17: "The Blasted Lands one in particular could be good for distant
    // lightning and Machine Lightning for close lightning"; never Saurfang's bolt (17203), never Netherstorm's electrical
    // crack ("wrong for thunder": NetherstormLightningStrikes, and TK_Lightning_Slow and UL_lightning, its recordings
    // at lower volumes). Zul'Gurub's bolt row plays the Blasted Lands takes plus one more, and unlike the Blasted Lands
    // row (Flags 0x20, a "suppressor": one at a time) it can overlap itself in a busy storm.
    enum class Kit : int { Machine = 0, ShamanThunder, CallLightning, Bolt, BlastedLandsBolt, Rumble, Count };
    struct KitInfo
    {
        const char* label;        // for the panel
        int32_t     soundKitId;   // SoundEntries ID = the client's sound kit ID
        const char* name;         // its Name as the DBC stores it (cut to 31 characters): what Lua PlaySound looks up
        float       volume;       // its Volume (the native path scales this)
        float       minDistance;  // its MinDistance: nearer than this the client plays it at full volume (ClientRolloff)
        float       cutoff;       // its DistanceCutoff
        bool        positional;   // played at a point toward the strike (it pans); else straight (a deep rumble barely
                                  // has a direction, and lasts long enough for a running player to leave its MinDistance)
    };
    inline constexpr KitInfo kKits[static_cast<int>(Kit::Count)] = {
        { "Machine lightning (Goblin weather machine, 12321)", 12321, "Goblin_Weather_Machine_Lightnin", 0.8f, 18.0f, 45.0f, true },
        { "Shaman thunder (13006)",                            13006, "Shaman_Thunder",                  0.8f,  8.0f, 45.0f, true },
        { "Call Lightning impact (30)",                        30,    "Call Lightning Impact",           0.8f,  8.0f, 45.0f, true },
        { "Blasted Lands bolt, Zul'Gurub's row (8439)",        8439,  "LightningBolt - ZulGurub",        1.0f, 50.0f, 100.0f, true },
        { "Blasted Lands bolt, its own row (4936)",            4936,  "BlastedLandsLightningbolt01Stan", 1.0f, 50.0f, 100.0f, true },
        { "Rolling rumble (EarthquakeCameraShake, 12549)",     12549, "EarthquakeCameraShake (High Vol", 1.0f,  8.0f, 45.0f, false },
    };
    const KitInfo& Info(Kit kit);

    /// The Lua fallback: Lua PlaySound has no volume, so each kit lists the rows that play its recordings at other
    /// volumes, and the one nearest the wanted volume plays.
    struct LuaRow { Kit kit; const char* name; float volume; int32_t soundEntryId; };
    inline constexpr LuaRow kLuaRows[] = {
        { Kit::Machine,          "Goblin_Weather_Machine_Lightnin", 0.8f, 12321 },
        { Kit::ShamanThunder,    "Shaman_Thunder",                  0.8f, 13006 },
        { Kit::CallLightning,    "Call Lightning Impact",           0.8f, 30 },
        { Kit::Bolt,             "LightningBolt - ZulGurub",        1.0f, 8439 },
        { Kit::Bolt,             "ChainHeal",                       0.8f, 5635 },  // Sound\Doodad: the same four takes
        { Kit::BlastedLandsBolt, "BlastedLandsLightningbolt01Stan", 1.0f, 4936 },
        { Kit::BlastedLandsBolt, "ChainHeal",                       0.8f, 5635 },
        { Kit::Rumble,           "EarthquakeCameraShake (High Vol", 1.0f, 12549 },
        { Kit::Rumble,           "EarthquakeCameraShake",           0.4f, 1485 },
        { Kit::Rumble,           "EarthquakeCameraShake low volum", 0.2f, 9947 },
    };
    /// A row further than this above the wanted volume isn't played: that layer is left out (a distant bolt with only
    /// loud rows to play it is heard as its rumble alone).
    constexpr float kLuaTooLoud = 2.5f;
    /// The Lua row for a kit at a volume (absolute, 0..1): the nearest on a log scale, or null when even the quietest
    /// is more than kLuaTooLoud x the volume (or the volume is 0).
    const char* LuaRowFor(Kit kit, float volume);

    // The loudness by distance: (kThunderRef / d)^kThunderFalloff, 1 up to kThunderRef -- a near strike at full
    // volume, 0.90 at 250 yd, 0.65 at 600, 0.53 at 1000, 0.45 at 1600 -- fading to nothing over the last 15% before the
    // silent distance. A gain on the kit's own volume. "can we make thunder noises louder :D" (the user, 2026-10-18):
    // it was (60 / d)^0.6, 0.42 at 250 yd and 0.19 at 1000.
    constexpr float kThunderRef = 190.0f;
    constexpr float kThunderFalloff = 0.38f;
    constexpr float kThunderFadeShare = 0.15f;
    float ThunderGain(float distanceYards, float maxDistance);

    // What a strike sounds like by its distance:
    //  - nearer than kNearCrack: a crack (the machine's lightning half the time, Shaman thunder or Call Lightning's
    //    impact a quarter each), then the rumble rolling after it at kRumbleNear of its gain; nearer than kPunch a
    //    second crack at the same time under it (Shaman thunder under the machine's and Call Lightning's, the machine's
    //    under Shaman thunder): one channel stops at full volume (LayerVolume), two together are louder;
    //  - nearer than kRumbleOnly: the Blasted Lands bolt, the rumble after it at 0.7;
    //  - farther: the rumble alone (x1.3), or the bolt when the rumble is off.
    constexpr float kNearCrack = 250.0f;
    constexpr float kPunch = 150.0f;
    constexpr float kRumbleOnly = 1200.0f;
    constexpr float kRumbleNear = 0.8f, kRumbleMid = 0.7f, kRumbleAlone = 1.3f;
    constexpr float kRumbleAfterNear = 0.25f, kRumbleAfterMid = 0.5f; // seconds after the crack
    struct ThunderLayer { Kit kit = Kit::Rumble; float gain = 0.0f; float after = 0.0f; }; // after: s after the crack's arrival
    struct ThunderPlan  { int count = 0; ThunderLayer layer[3]; };
    /// pick: 0..1 (random), which crack a near strike gets. Nothing (count 0) beyond maxDistance.
    ThunderPlan PlanThunder(float distanceYards, float maxDistance, float pick, bool rumble);

    /// A layer's volume as the client gets it: the kit's own x the layer's gain x the volume setting, never above 1
    /// (the most a sound kit plays at: SE2SoundKitProperties' volume is used only within 0..1, and FMOD's channel
    /// volume stops at 1), never below 0.
    float LayerVolume(Kit kit, float gain, float volumeSetting);
    /// The volume setting's default ([LivingAzeroth.Storm] ThunderVolume; it was 1).
    constexpr float kThunderVolume = 1.5f;

    /// The native path's point for a positional kit: this far from the listener toward the strike -- a quarter of its
    /// MinDistance, at most 6 yd -- so the client's rolloff leaves the volume as we set it, with room for the
    /// listener to move during the sound. 0 = not positional.
    float PlacementDistance(Kit kit);

    /// The client's 3D rolloff (its sound engine's ComputeRolloff, 0x8782B0, set as FMOD's rolloff callback): 1 up to
    /// minDistance, then min / ((d - min) x 4 + min), fading to 0 over the last tenth before maxDistance.
    float ClientRolloff(float minDistance, float maxDistance, float distance);

    // --- the day-night colours -------------------------------------------------------------------
    // AreaTable's Flags (WorldQuery's snapshot: every area of the chain): a capital city and its sub-areas (AzerothCore
    // DBCEnums AREA_FLAG_CAPITAL 0x100, AREA_FLAG_SLAVE_CAPITAL 0x8).
    constexpr uint32_t kAreaCapital = 0x100, kAreaSlaveCapital = 0x8;
    // The client's 18 interpolated band colours (0xAARRGGBB at 0xD38BD4, core Liquid.hpp kDnBandColors):
    // 0 direct light, 1 ambient, 2..7 the sky from the top to the horizon, 8 the fog (what the terrain
    // and the ground fog take, debug/FogProbe), 9..10 the sun, 11..13 the clouds, 14..17 the water.
    // (The fill at 0x7EBFF0 puts LightIntBand's fog, its 8th colour, in band 8, which Sky.FogUpdate reads at 0x7F171A;
    // band 2 holds LightIntBand's 9th, a grey, not the sky.)
    constexpr int kBandCount = 18;
    constexpr int kFogBand = 8;
    struct Shade
    {
        float dark = 0.0f;     // 0..1: the storm's darkening (sky fully, light less, the fog least)
        float desat = 0.0f;    // 0..1: toward grey (not the fog: it cools instead)
        float sky = 0.0f;      // a flash in the sky and the clouds
        float scene = 0.0f;    // a flash lighting the ground (the ambient and direct light)
        float tint[3] = { 0.80f, 0.86f, 1.0f }; // the flash's colour
        // The fog band, its own storm: rain and mist scatter light, so the near air in a storm is lighter and cooler
        // than the dark sky above. The client's distance fog takes this band, and our ground fog and haze its colour.
        float fog = 0.0f;       // 0..1: how stormy, for the fog
        float fogBright = 1.0f; // its luminance in full storm by day, x the client's own fog colour's; at night
                                // kFogNightShare of that. A floor: the storm's darkening never takes it lower.
        float fogCool = 0.0f;   // 0..1: how much of its hue gives way to kFogCool in full storm (instead of grey)
        float day = 1.0f;       // 0 night .. 1 day, the sky's (env/Fog SkyNight)
        // Falling snow. The client shows its weather light set (Light.dbc's LightParams[2]) in snow as in rain, all of it
        // from a grade of 0.25 (kStormBlend). Where no zone light reaches, that is the continent default's set,
        // LightParams 10: one flat grey for every hour, the fog #525452, the sky #6F6F6F to #525352, and a fog that ends
        // at 278 yd and starts 139 yd behind the camera (SnowFogDistance). Over snow by day it read as a dark grey wall
        // (Winterspring at Frostfire Hot Springs, 15:20, outside Winterspring's lilac light: "WINTERSPRING TOO DARK AND
        // FOGGY AHHHHH", the user, 2026-10-18). Snow lifts and whitens those greys: the fog, the sky and the clouds
        // (SnowWeight), so the fogged distance meets a sky as bright. A zone's own coloured weather light (Winterspring's
        // lilac, Dun Morogh's sky blue) keeps its colour: the lift fades out with the band's saturation (kSnowGreyFull ..
        // kSnowGreyNone).
        float snow = 0.0f;       // 0..1: the weather set's share in this frame's colours while it snows (the client's
                                 // storm blend; 0 in rain, with the camera in a liquid, or snow off)
        float snowBright = 1.0f; // a grey band's luminance in snow by day, x the client's own (at night x 1), up to
                                 // kSnowCeiling. A floor: snow never darkens a band.
        float snowWhite = 0.0f;  // 0..1: how much of a grey band's hue gives way to kSnowWhite in snow
        // Darker nights (2026-10-22, Lumenveil's queue #1: "darker night ambient and sky so lamps and lit windows matter"):
        // this frame's night darkening (NightShade), each band x 1 - night x its night weight (NightWeight): the ambient most,
        // the sky and clouds, the direct light (the moon's) and the water less, the fog least (a black fog wall reads worse
        // than a dark one), the sun's bands not at all. 0: the bands as they were.
        float night = 0.0f;
    };
    /// A band's share of the night's darkening (0..1).
    float NightWeight(int band);
    /// The night's darkening this frame (Shade::night): `setting` (Storm NightDarkness, 0..1) x how much night it is (1 -
    /// `day`, the sky's) x the place's share: in a city (`city`: its area or a parent flagged a capital) kNightCityShare
    /// (lamps everywhere already), where it's freezing (`temperature` degC, climate: snow on the ground lights the night)
    /// down to kNightSnowShare from kNightWarm to kNightCold, and indoors (`outdoors` 0, smoothed) kNightIndoorShare (a
    /// room's own light; the bands still light what's seen out of it). Not finite or out of range: clamped, NaN 0.
    struct NightShare { float night = 0.0f, city = 1.0f, snow = 1.0f, indoor = 1.0f, total = 0.0f; };
    NightShare NightShade(float setting, float day, bool city, float temperature, float outdoors);
    constexpr float kNightCityShare = 0.5f, kNightSnowShare = 0.5f, kNightIndoorShare = 0.3f;
    constexpr float kNightWarm = 2.0f, kNightCold = -4.0f;
    constexpr float kFlashGain = 0.55f;  // a full flash adds this much white (of 255) to a fully weighted band
    constexpr float kFogNightShare = 0.75f;
    constexpr float kFogCeiling = 0.70f; // luminance (0..1): fogBright lifts the fog no higher (a brighter fog keeps its own)
    inline constexpr float kFogCool[3] = { 0.76f, 0.87f, 1.0f }; // a pale blue-grey, at the fog's own luminance
    constexpr float kSnowCeiling = 0.75f; // luminance (0..1): snowBright lifts a band no higher (a brighter one keeps its own)
    inline constexpr float kSnowWhite[3] = { 0.86f, 0.92f, 1.0f }; // falling snow's pale blue-white, at the band's luminance
    // A band's share of the snow treatment by its saturation ((max - min) / max): all of it up to kSnowGreyFull (the weather
    // sets' greys: #525452 is 0.02, Winterspring's eastern #4F4F57 0.09), none from kSnowGreyNone (Winterspring's lilac
    // storm fog #784E9A is 0.49 and its sky's top #FB9FFF 0.38, Dun Morogh's sky-blue fog #74A7C4 0.41).
    constexpr float kSnowGreyFull = 0.12f, kSnowGreyNone = 0.35f;
    /// How much snow lifts a band (0..1): the sky (3..7, LightIntBand 2..6), the fog (8) and the clouds (11..13); not the
    /// light, the sun or the water.
    float    SnowWeight(int band);
    bool     Identity(const Shade& s);
    uint32_t ShadeBand(uint32_t argb, int band, const Shade& s);

    /// Shades the bands in place. `last` remembers what was there and what was written, so a band the
    /// client did not recompute since (it always does, but) is shaded from its original again instead
    /// of darkening a little more every frame.
    struct BandMemory
    {
        bool     valid = false;
        uint32_t in[kBandCount] = {};
        uint32_t out[kBandCount] = {};
    };
    void ShadeBands(uint32_t bands[kBandCount], const Shade& s, BandMemory& last);

    // --- the zone fog's distance in snow ---------------------------------------------------------
    // The two floats after the bands (CurrentLight +0x48 and +0x4C, kFogEnd and kFogStartMul): where the zone fog ends (yd)
    // and where it starts, x the end (-1..1). Sky.FogUpdate (0x7F16F0) reads both after the band copy: the fog runs
    // linearly from end x startMul to min(far clip, end) in fog mode 0, the client's mode below map 530 (Kalimdor and the
    // Eastern Kingdoms); from 530 on the end is already the far clip and the start never negative, so this changes nothing
    // there. The weather set's fog ends at 278 yd and starts at x -0.5: a third fogged at the camera, half at 70 yd. The
    // clear default's ends at 490 and starts at x 0.25 (122 yd).
    struct FogDistance { float end = 0.0f, startMul = 0.0f; };
    /// In falling snow (`share`, as Shade::snow): the fog ends no nearer than `floorYards` and starts no nearer than the
    /// camera, as much as it snows. A zone's own longer or clearer fog stays as it is. Share or floor 0 (or NaN): as it is.
    FogDistance SnowFogDistance(FogDistance in, float share, float floorYards);
    /// SnowFogDistance in place, remembering what it wrote like ShadeBands: values we wrote that the client didn't
    /// recompute are lifted from their originals again, not lifted further every frame.
    struct FogDistanceMemory
    {
        bool        valid = false;
        FogDistance in, out;
    };
    void LiftFogDistance(float& end, float& startMul, float share, float floorYards, FogDistanceMemory& last);

    // --- the client code this relies on (checked byte for byte before any hook or call) ------------
    struct Guard
    {
        const char* what;
        uintptr_t   address;
        uint32_t    size;
        uint8_t     bytes[16];
    };

    // The band copy into the light block (0x7EE750, __cdecl, no arguments, reads only globals): the day-night
    // colour resolve (Sky.ColorsResolve 0x7F3230) calls it once, at 0x7F333B, after every one of its paths has
    // written the bands and before the sky (0x7F0530), the light block and the fog (Sky.FogUpdate) read them.
    constexpr uintptr_t kBandsToLight = 0x007EE750;
    constexpr uintptr_t kBands = 0x00D38BD4;
    inline constexpr Guard kBandGuards[] = {
        {"band copy: entry, and its read of the ambient band", 0x007EE750, 12,
         {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x28, 0x8B, 0x15, 0xD8, 0x8B, 0xD3, 0x00}},
        {"colour resolve: its one call of the band copy", 0x007F333B, 5, {0xE8, 0x10, 0xB4, 0xFF, 0xFF}},
    };

    // Snow (Shade::snow, FogDistance), read and written inside the band hook, where the client has just resolved this
    // frame's colours and distances:
    //  - the weather set's share, DNInfo m_stormBlend: the day-night update (0x7F3920) stores min(1, 4 x the weather's fog
    //    intensity) there at 0x7F3995, then calls the colour resolve, which blends LightParams[2] in by it;
    //  - the camera in a liquid: the colour resolve asks 0x780620 (at 0x7F3245), which returns [0xCD8794]; in a liquid
    //    the colours are the liquid's or the underwater set's, not the snowy air's;
    //  - the zone fog's end and start (FogDistance), at the bands + 0x48 and + 0x4C, read by Sky.FogUpdate.
    constexpr uintptr_t kStormBlend = 0x00D38B88;
    constexpr uintptr_t kCameraLiquid = 0x00780620;
    constexpr uintptr_t kCameraInLiquid = 0x00CD8794;
    constexpr uintptr_t kFogEnd = kBands + 0x48;      // 0xD38C1C
    constexpr uintptr_t kFogStartMul = kBands + 0x4C; // 0xD38C20
    inline constexpr Guard kSnowGuards[] = {
        {"day-night update: it stores the weather set's share", 0x007F3995, 6, {0xD9, 0x1D, 0x88, 0x8B, 0xD3, 0x00}},
        {"colour resolve: it asks whether the camera is in a liquid", 0x007F3245, 5, {0xE8, 0xD6, 0xD3, 0xF8, 0xFF}},
        {"camera liquid: the value it returns", 0x00780632, 5, {0xA1, 0x94, 0x87, 0xCD, 0x00}},
        {"fog update: the zone fog's end", 0x007F1705, 6, {0xD9, 0x05, 0x1C, 0x8C, 0xD3, 0x00}},
        {"fog update: its start, x the end", 0x007F1725, 6, {0xD8, 0x0D, 0x20, 0x8C, 0xD3, 0x00}},
    };

    // A model of our own in the world's M2 scene (bolts). The world scene is [0xCD754C] (env/Lights).
    //  - CreateModel 0x81F8F0, __thiscall(scene; path, flags), ret 8: always a new model, linked into the scene's
    //    model list at once; a failed load falls back to the placeholder. The doodad spawn calls it at 0x7BEECE.
    //  - SetBoneSequence 0x832AB0 (core M2.hpp): what the doodad spawn calls right after, (-1, 0, -1, 0, 1.0, 1, 1).
    //  - Its world matrix at +0xB4 (row-major, translation at +0xE4), read every frame (core Doodad.hpp).
    //  - 0x823F10, __thiscall(model; bool): adds it to (1) or takes it off (0) this frame's animate list (scene +0x28,
    //    links at model +0x40/+0x44); the scene animates, culls and draws what's on it and empties the list every
    //    frame (0x821C09), so an owner adds its models every frame -- the world's doodad culling at 0x798926, the
    //    world effect spawner at 0x7FB375.
    //  - 0x824ED0, __thiscall(model): drops a reference; at 0 the model is destroyed (unlinked from the lists).
    constexpr uintptr_t kWorldScene = 0x00CD754C;
    constexpr uintptr_t kCreateModel = 0x0081F8F0;
    constexpr uintptr_t kSetAnimating = 0x00823F10;
    constexpr uintptr_t kReleaseModel = 0x00824ED0;
    constexpr uintptr_t kSetBoneSequence = 0x00832AB0;
    constexpr size_t    kModelMatrix = 0xB4;
    constexpr size_t    kModelAnimLink = 0x40;
    inline constexpr Guard kModelGuards[] = {
        {"scene: create a model", 0x0081F8F0, 16,
         {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x85, 0xC0, 0x53, 0x8B, 0xD9, 0x75, 0x05, 0x5B, 0x5D, 0xC2}},
        {"model: on or off this frame's animate list", 0x00823F10, 14,
         {0x55, 0x8B, 0xEC, 0x56, 0x8B, 0xF1, 0xF6, 0x46, 0x04, 0x20, 0x57, 0x8B, 0x7D, 0x08}},
        {"model: release", 0x00824ED0, 12, {0x56, 0x8B, 0xF1, 0x83, 0x06, 0xFF, 0x8B, 0x06, 0x75, 0x21, 0x57, 0x8B}},
        {"model: set its sequence", 0x00832AB0, 8, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20, 0x53, 0x8B}},
        {"doodad spawn: the world scene and its create call", 0x007BEEC8, 11,
         {0x8B, 0x0D, 0x4C, 0x75, 0xCD, 0x00, 0xE8, 0x1D, 0x0A, 0x06, 0x00}},
        {"world effects: a model put on the animate list each frame", 0x007FB370, 10,
         {0x8B, 0x4B, 0x14, 0x6A, 0x01, 0xE8, 0x96, 0x8B, 0x02, 0x00}},
    };

    // Thunder through the client's own sound kits (the native path; Lua PlaySound is the fallback), called on the main
    // thread like the client's own object sounds (GameObject_C's one-shot positional sounds call it the same way):
    //  - SI2::PlaySoundKit 0x4C6A40, __cdecl int(int kitId, const C3Vector* position (null: not positional),
    //    SE2SoundKitObject* object (null: a one-shot), SE2SoundKitProperties* props, uint32 advancedId = 0,
    //    uint32* soundId = null, int checkDistance = 1, int keepPlaying = 0). 0 = playing, else why not (SoundOutcome).
    //  - SE2SoundKitProperties::ResetToDefaults 0x4C5990, __thiscall: 0xE8 bytes; m_volume (+4) = -1 means "the kit's
    //    Volume x m_volumeScale (+8)" (PlaySoundKit at 0x4C6FD1 / 0x4C6FEE), m_volumeScale 1, group 0 (SFX).
    //  - SE2::GetListenerPosition 0x879350, __cdecl bool(C3Vector*): where FMOD's listener is (the character, or the
    //    camera with Sound_ListenerAtCharacter 0), in world coordinates like every sound's position.
    //  - Positional sounds roll off with ComputeRolloff 0x8782B0 (ClientRolloff above).
    constexpr uintptr_t kPlaySoundKit = 0x004C6A40;
    constexpr uintptr_t kResetSoundProps = 0x004C5990;
    constexpr uintptr_t kListenerPosition = 0x00879350;
    constexpr uintptr_t kSoundRolloff = 0x008782B0;
    constexpr size_t    kSoundPropsSize = 0xE8;
    constexpr size_t    kSoundPropsVolume = 0x04, kSoundPropsVolumeScale = 0x08;
    inline constexpr Guard kSoundGuards[] = {
        {"sound kits: play one (SI2::PlaySoundKit)", 0x004C6A40, 16,
         {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00, 0x85, 0xC0, 0x7F, 0x09}},
        {"sound kits: a set volume (+4) is used only within 0..1", 0x004C6FD1, 16,
         {0xD9, 0xEE, 0xD8, 0x5B, 0x04, 0xDF, 0xE0, 0xF6, 0xC4, 0x41, 0x7A, 0x11, 0xD9, 0xE8, 0xD8, 0x5B}},
        {"sound kits: else the kit's volume x the scale (+8)", 0x004C6FEE, 13,
         {0xF7, 0x46, 0x3C, 0x00, 0x08, 0x00, 0x00, 0xD9, 0x46, 0x34, 0xD8, 0x4B, 0x08}},
        {"sound kit properties: their defaults (volume -1 at +4)", 0x004C5990, 16,
         {0xD9, 0x05, 0xF4, 0x2E, 0x9E, 0x00, 0x53, 0x56, 0x8B, 0xF1, 0xD9, 0x56, 0x04, 0x33, 0xDB, 0xD9}},
        {"sound kit properties: the scale 1 at +8", 0x004C599F, 8, {0xD9, 0xE8, 0x83, 0xC8, 0xFF, 0xD9, 0x56, 0x08}},
        {"sound engine: the listener's position", 0x00879350, 16,
         {0x55, 0x8B, 0xEC, 0x83, 0x3D, 0x14, 0x38, 0xD4, 0x00, 0x00, 0x75, 0x04, 0x32, 0xC0, 0x5D, 0xC3}},
        {"sound engine: the 3D rolloff", 0x008782B0, 16,
         {0x55, 0x8B, 0xEC, 0xD9, 0x45, 0x0C, 0xD9, 0x45, 0x10, 0xD8, 0xD1, 0xDF, 0xE0, 0xF6, 0xC4, 0x01}},
    };

    /// What PlaySoundKit's result means for the fallback: Played (0); Declined by the client on purpose (9 SFX off,
    /// 10 music off, 11 ambience off, 14 too many sounds at once, 15 a suppressor kit already playing, 17 sound off or
    /// in a movie: Lua would be declined the same way, so nothing else plays); else a Problem with the kit (5 no such
    /// kit, 6 no files, 8 bad group, 16 its file failed, 20 not downloaded yet, anything else): Lua's row is tried.
    enum class SoundOutcome { Played, Declined, Problem };
    SoundOutcome Outcome(int playSoundKitResult);

    /// The first guard of `guards` whose bytes differ (read through `read(address, size)`, which returns a
    /// pointer to that many bytes or null), or null when all match.
    template <class Read, size_t N> const Guard* FirstMismatch(const Guard (&guards)[N], Read read)
    {
        for (const Guard& g : guards)
        {
            const uint8_t* at = static_cast<const uint8_t*>(read(g.address, g.size));
            if (!at) return &g;
            for (uint32_t i = 0; i < g.size; ++i)
                if (at[i] != g.bytes[i]) return &g;
        }
        return nullptr;
    }

    /// A bolt's world matrix (row-major, row vectors, as the client keeps it): turned `yaw` radians
    /// about the vertical, scaled by `scale`, standing at `pos`.
    void BoltMatrix(const float pos[3], float yaw, float scale, float out[16]);

    // A one-shot bolt: Call Lightning's impact (Spells\CallLightning_Impact.m2, 2.1 s, about 11.5 yd tall at
    // scale 1). Its strokes show between 134 and 867 ms into its sequence (colour alpha keys, sequence time, not a
    // global loop), so a bolt shown from its creation flashes at once; the storm's flash starts kBoltFlashDelay
    // after it. Storm Peaks' own lightning doodads are larger but flash on a 33 s global loop, out of our control.
    constexpr const char* kBoltModel = "Spells\\CallLightning_Impact.m2";
    constexpr float kBoltFlashDelay = 0.134f;
    constexpr float kBoltLife = 1.0f;          // seconds it stays on the animate list
}
