// The belt lantern's rules (2026-10-25, Lumenveil's queue #4, the idea only: "a lantern on the player (key)"), apart from
// the client so tests/lumen checks them offline. The runtime is features/Lantern: one warm point light at the player's hip,
// through our lights (env/Lights), following the player every frame.
//
// How dark the place is (Darkness, 0..1): outdoors, the day's light (climate's daylight: none from 0.35 up, all at 0) or a
// storm's darkening, whichever is darker; indoors (the client's verdict: a cave, a crypt, a mine, a house), dark. Then how
// lit the spot already is by our lights (LitShare of the light round the player, env/Lights LightAround: a hearth and
// candles near, a lamp-lit street): a lit inn or a torch-lit square leaves little for a lantern. Strength = that, so by
// day outdoors the lantern gives nothing even when lit by the key.
//
// Lit or out (Advance): with Automatic, it lights itself when the place is dark enough (kAutoOn, out again below kAutoOff)
// after the light round the player has settled (smoothed over kAroundSeconds, so passing a torch in a mine doesn't put it
// out). The key (Press) puts it out when it shows and lights it when it doesn't; that holds until the automatic choice
// changes (dusk or dawn, into or out of a dark place), then automatic again. Without Automatic the key alone lights it and
// puts it out. Lighting it fades in over kLightSeconds, putting it out over kOutSeconds.
#pragma once

#include <cstdint>

namespace wxl_livingazeroth::lantern
{
    struct Settings
    {
        int   enabled = 1;      // the lantern at all (needs our point lights)
        int   key = 0x76;       // F7: a Windows virtual-key code, 0 = no key
        int   automatic = 1;    // lights itself in the dark (night, caves, indoors without light)
        float strength = 1.0f;  // x its light
        float radius = 8.0f;    // yd: its light is exactly 0 here
    };

    // Its light: a candle-lantern's warm 2400 K, a little under a torch's (DoodadLightProperties' torches 1.0 .. 1.4 x
    // FFA050), flickering gently in its colour only (no dip reported: the WMO interiors' baked light doesn't pulse with it).
    constexpr uint32_t kColor = 0xFFB25C;
    constexpr float    kIntensity = 0.75f;
    constexpr float    kFlicker = 0.05f, kFlickerSpeed = 0.6f;

    // Where it hangs: at the hip, on the right, a little forward (yd from the feet along the player's facing); higher on a mount.
    constexpr float kHipHeight = 1.0f, kMountedHeight = 2.3f, kSide = 0.45f, kForward = 0.15f;

    constexpr float kDaylightLit = 0.35f;   // daylight from which outdoors needs no lantern
    constexpr float kAroundReach = 15.0f;   // yd: the lights round the player that count (LightAround)
    constexpr float kLitFrom = 0.3f, kLitFull = 1.2f; // light round the player: from here the lantern gives way, all of it at kLitFull
    constexpr float kAutoOn = 0.5f, kAutoOff = 0.3f;  // Darkness at which Automatic lights it, and puts it out again
    constexpr float kAroundSeconds = 1.5f;  // the light round the player smoothed over this long
    constexpr float kLightSeconds = 0.6f, kOutSeconds = 0.4f;

    /// How dark the place itself is, 0..1: outdoors by the daylight (0..1) or a storm's darkening (0..1), the darker;
    /// indoors 1.
    float PlaceDarkness(float daylight, bool indoors, float stormDarkness);
    /// The share of the lantern's light a spot keeps with `around` of our light round it (LightAround): 1 in the dark,
    /// 0 from kLitFull up, smooth between.
    float LitShare(float around);

    struct State
    {
        int   held = -1;         // the key's choice: -1 none (automatic), 0 out, 1 lit
        bool  autoWants = false; // the automatic choice (hysteresis: kAutoOn, kAutoOff)
        bool  lit = false;       // what it is now
        float level = 0.0f;      // 0..1, fading in and out
        float around = 0.0f;     // the light round the player, smoothed
        bool  seeded = false;    // `around` has a first value
        float darkness = 0.0f;   // this frame's (PlaceDarkness x LitShare)
    };

    /// The key: out if it shows (lit), lit if it doesn't. Returns whether it's lit now.
    bool Press(State& s, const Settings& c);

    /// One frame: the automatic choice, the fade, and the strength (0..1, x Settings::strength) its light has now.
    float Advance(State& s, const Settings& c, float daylight, bool indoors, float stormDarkness, float around, float dt);
}
