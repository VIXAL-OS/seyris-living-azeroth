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

    /// The client's sound for a crack heard at this distance (SoundEntries names; Lua PlaySound takes
    /// no volume, so farther cracks use quieter entries of the same recordings: Netherstorm's crack at
    /// 0.5, 0.4 and 0.1 volume), or null beyond maxDistance (silent).
    const char* ThunderSound(float distanceYards, float maxDistance);

    // --- the day-night colours -------------------------------------------------------------------
    // The client's 18 interpolated band colours (0xAARRGGBB at 0xD38BD4, core Liquid.hpp kDnBandColors):
    // 0 direct light, 1 ambient, 2..7 the sky from the top to the horizon, 8 the fog (what the terrain
    // and the ground fog take, debug/FogProbe), 9..10 the sun, 11..13 the clouds, 14..17 the water.
    constexpr int kBandCount = 18;
    struct Shade
    {
        float dark = 0.0f;     // 0..1: the storm's darkening (sky fully, light and fog less)
        float desat = 0.0f;    // 0..1: toward grey
        float sky = 0.0f;      // a flash in the sky and the clouds
        float scene = 0.0f;    // a flash lighting the ground (the ambient and direct light)
        float tint[3] = { 0.80f, 0.86f, 1.0f }; // the flash's colour
    };
    constexpr float kFlashGain = 0.55f;  // a full flash adds this much white (of 255) to a fully weighted band
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
