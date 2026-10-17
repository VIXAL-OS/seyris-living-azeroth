// Storms: rain that builds up into thunder (the rules: env/StormModel).
//
//  - Light and medium rain: the sky and the light darken, the wind gusts (env/Wind), more haze
//    (env/Fog), and now and then a flash in the clouds -- the sky brightens, no bolt. All of it grows
//    with the rain's grade.
//  - Heavy rain: strikes. Each lights the sky (brighter and lighting the ground the nearer it is), its
//    crack follows after the sound's travel time (about 3 s per km) at a loudness by distance, a point
//    light shines at it for its strokes (env/Lights), and optionally a bolt model stands there and a
//    scorch mark stays behind on the terrain (features/TerrainWetness).
//
// The darkening and the flashes change the client's own day-night colours, every frame, right before
// it hands them to its sky, light and fog (a pre-hook of the band copy, StormModel kBandsToLight): the
// whole picture follows them, and nothing builds up -- the client recomputes the colours each frame.
// Bolts are client models of our own in the world's M2 scene (StormModel kModelGuards); both hooks and
// calls are checked against the 12340 client's bytes first and stay off, saying why, if anything differs.
#pragma once

#include "WorldQuery.hpp"

#include "wxl/PluginApi.h"

namespace wxl_livingazeroth::storm
{
    struct Settings
    {
        int   enabled = 1;
        float darkness = 0.35f;      // how much darker the sky and the light get in full storm (0..1)
        int   flashes = 1;           // flashes in the clouds (light and medium rain, and between strikes)
        float flashStrength = 1.0f;  // x every flash's brightness
        float frequency = 1.0f;      // x how often flashes and strikes come
        int   thunder = 1;           // heavy rain: strikes
        int   sounds = 1;            // their cracks
        float soundDistance = 1600.0f; // yd: farther strikes are silent
        float strikeLight = 1.0f;    // x the point light at a strike (0 = none)
        float wind = 1.0f;           // x the storm's extra gusts (0 = none)
        float haze = 0.3f;           // extra haze in full storm (0..1, 0 = none)
        int   bolts = 0;             // a bolt model at strikes (experimental: our own models in the client's scene)
        float boltSize = 6.0f;       // its scale (1 = Call Lightning's impact, about 11.5 yd tall)
        float boltRange = 900.0f;    // yd: strikes farther away get no bolt
        int   scorch = 0;            // a scorch mark on the terrain where a near strike lands (experimental)
        float scorchSeconds = 45.0f; // how long it takes to fade
    };
    Settings& Config();

    /// The band hook and the bolt checks. From WXL_Load.
    void Install(const WXL_Api* api);

    /// Once per frame, after climate::Update and before the wind, the fog and the lights.
    void Update(float dt, const world::Snapshot& snap);

    /// Before a loading screen: the bolt models go, pending sounds and the scorch mark too.
    void OnWorldLeave();

    // --- panel ---
    /// A flash in the clouds now, or a strike at a distance (yd; < 0 = a random one).
    void TestFlash();
    void TestStrike(float distance);
    /// Plays the crack heard at this distance now.
    void TestSound(float distance);

    struct Status
    {
        int   phase = 0;          // StormModel Phase
        float grade = 0.0f;       // the weather's grade the phase comes from
        bool  overridden = false; // the climate panel's weather override
        float storminess = 0.0f;  // smoothed, 0..1
        float darkness = 0.0f, skyFlash = 0.0f, sceneFlash = 0.0f; // this frame's shade
        float nextFlash = -1.0f;  // seconds, < 0 = none coming
        unsigned flashes = 0, strikes = 0, sounds = 0, bolts = 0, lights = 0; // this session
        float lastStrikeDistance = -1.0f;
        int   pendingSounds = 0, liveBolts = 0;
        float scorchLeft = 0.0f;  // seconds
        unsigned bandFrames = 0;  // frames the band hook shaded
    };
    const Status& GetStatus();
    const char* BandStatus();   // the band hook: on, or why not
    const char* BoltStatus();   // the bolt models: available, or why not
}
