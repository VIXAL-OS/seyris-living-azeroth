// Distant night lights, the rules (2026-10-25, Lumenveil's queue #6, the idea only: "towns visible from far away at night"),
// apart from the client so tests/lumen checks them offline. The runtime is features/DistantLights.
//
// Our point lights reach 256 yd (env/Lights' range, fading over its last third); past that a town at night was dark: its
// lamps' models aren't drawn that far and nothing lights its streets. Past two thirds of that range each lamp (the light
// scan's far list: the models with DoodadLightAssignment rows or lights of their own, env/Lights FarLights) and each lit
// window (WindowLights.cdbc, the share of its rooms lit now) is a small glow of its own colour on the screen instead,
// fading in as its point light fades out (NearFade), with the night (the client's own window fade), out with the distance
// fog (FogVisibility: lamps carry through the fog further than the land does, `FogPierce` fog depths) and before the far
// clip (FarClipFade). Glows close together at that distance merge, the farther the wider (Cluster: cells of 2 yd from 256
// yd, doubling with each doubling of the distance), keeping the square root of their summed light as the candle ring's
// rule does, a little bigger; the brightest, nearest MaxLights are drawn (Cap), all in one batched draw.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wxl_livingazeroth::distantlight
{
    struct Settings
    {
        int   enabled = 1;
        float strength = 1.0f;       // x the glows' brightness
        float size = 1.0f;           // x their size
        float maxDistance = 1500.0f; // yd: none farther (and none past the client's far clip)
        int   maxLights = 1024;      // drawn at most (after merging)
        float fogPierce = 1.5f;      // how far into the distance fog lamps carry, in fog depths
        int   windows = 1;           // lit windows glow
        int   lamps = 1;             // lamps, torches and fires glow (the light scan's far list)
    };
    constexpr int kMaxLightsCap = 4096;

    constexpr float kFadeFrom = 2.0f / 3.0f;   // of our lights' range: the glows fade in from here to the range
    constexpr float kMinCorePixels = 1.25f;    // a glow's core is never smaller on the screen than this (radius, px)
    constexpr float kMinHaloPixels = 4.0f;     // nor its halo
    constexpr float kHaloScale = 3.5f;         // the halo reaches this many core radii

    enum Kind : uint8_t { kWindow = 0, kLamp = 1 };

    /// One glow before merging: world position, colour (already x its strength, the night and its fade-in), its core's radius
    /// in yd.
    struct Source { float pos[3]; float color[3]; float size; Kind kind; };
    /// One glow to draw (merged).
    struct Sprite { float pos[3]; float color[3]; float size; float distance; unsigned count; Kind kind; };

    /// 0 at kFadeFrom x `nearRange`, 1 from `nearRange` on (smooth), as the point lights fade out over that band.
    float NearFade(float distance, float nearRange);
    /// How much of a glow's light gets through the client's distance fog: it starts at `start` yd and ends at `end` (the
    /// client's own fog runs linearly between them); a lamp's light falls off by e per `pierce` fog depths past its start.
    /// 1 without a fog (end <= start).
    float FogVisibility(float distance, float start, float end, float pierce);
    /// 1 to 88% of the far clip, 0 from 98% (the land behind the glow isn't drawn past the far clip). 1 without one.
    float FarClipFade(float distance, float farClip);

    /// A lamp's glow core (yd) for a light reaching `radius` yd (a candle 9.5 -> 0.7, a torch 20 -> 1.05).
    float LampSize(float radius);
    /// A window's glow core (yd) for its glass's half extent.
    float WindowSize(float halfWidth, float halfHeight);

    /// The glow on the screen: its core radius and the quad's half size in pixels, for a core of `worldSize` yd at
    /// `distance` yd, `pixelsPerYard` pixels across a yard at 1 yd (the viewport's half height x the projection's y scale).
    struct Screen { float corePixels, halfPixels; };
    Screen OnScreen(float worldSize, float distance, float pixelsPerYard);

    /// Merges `in` (as seen from `eye`) into `out`: glows in one cell (2 yd from 256 yd, twice that per doubling of the
    /// distance) become one at their light-weighted middle, with sqrt(N) of their summed colour, its core 1 + 0.25 log2 N
    /// times the largest.
    void Cluster(const std::vector<Source>& in, const float eye[3], std::vector<Sprite>& out);
    /// Keeps the `max` that count most (their light over 1 + distance / 400 yd), nearest first after.
    void Cap(std::vector<Sprite>& sprites, size_t max);
}
