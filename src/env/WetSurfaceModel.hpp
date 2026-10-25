// Wet surfaces, the rules (no client access here, so tests/wet runs them offline): how wet a zone's surfaces are (rain
// wets them, they dry after it stops), how much of that a spot of ground holds (open sky, snow, heat; its puddle potential:
// low and flat), how a ground layer or a building's texture takes it, and the constants the patched shaders read.
// features/WetSurfaces applies them in game (Lumenveil's queue #3, ideas only: docs/r&d/lumenveil-cross-pollination.md).
//
// Two quantities per zone, both 0..1, apart from the ground's moisture (env/Fields: the soil, over minutes to hours):
//  - the film: water on the surface. It rises while it rains (in about kWetUpSeconds in heavy rain, slower in light rain)
//    and dries linearly after it stops, over DryOutMinutes on a mild day (faster when warm, dry and windy).
//  - the pool: how full the puddles are. It rises more slowly (kPoolUpSeconds) and drains kPoolDryFactor times slower.
// A spot's film is its zone's (blended across zone borders as the rain is) times its open sky; a spot floods where its
// static puddle potential (lower than the ground round it, flat, plus a little noise so flat ground isn't all one sheet)
// is above a threshold the pool lowers.
#pragma once

#include <cmath>
#include <cstdint>

namespace wxl_livingazeroth::wetsurf
{
    struct Settings
    {
        int   enabled = 1;
        float intensity = 1.0f;       // 0..2: how glossy and dark wet surfaces get, and how bright what they reflect
        float puddles = 1.0f;         // 0..2: how much low, flat ground floods (0 = no puddles)
        float dryOutMinutes = 15.0f;  // minutes for soaked surfaces to dry after the rain stops, on a mild day
        int   buildings = 1;          // building exteriors' stone, wood and roofs too
    };

    // --- a zone's surfaces over time ---------------------------------------------------------------------------------
    struct ZoneWet
    {
        float film = 0.0f;  // 0 dry .. 1 wet through
        float pool = 0.0f;  // 0 no puddles .. 1 full
    };
    constexpr float kWetUpSeconds = 45.0f;    // heavy rain wets surfaces 63% of the way in this long
    constexpr float kPoolUpSeconds = 240.0f;  // ... and fills the puddles 63% of the way in this long
    constexpr float kLightRainShare = 0.3f;   // the lightest rain wets at this share of heavy rain's pace
    constexpr float kPoolDryFactor = 1.5f;    // puddles take this much longer than the surfaces to dry
    constexpr float kMinDryMinutes = 0.5f;

    /// One step of `dt` seconds. `raining`: rain falls (any grade); `rain` 0..1 how hard (StormModel RainLevel);
    /// `dryRate` the climate's drying relative to a mild day (DryRateFactor). Finite results in 0..1 whatever comes in.
    void Step(ZoneWet& z, float dt, bool raining, float rain, float dryRate, float dryOutMinutes);

    /// The drying pace relative to a mild day (15 degC, half humid, a light wind), from regional::DryingRate's values at
    /// the zone and at those, kept within 0.5 .. 2 (a hot desert dries twice as fast, a humid cold night half as fast).
    float DryRateFactor(float dryingRate, float mildDryingRate);

    // --- a spot of ground (the moisture grid's 2 yd cells) ---------------------------------------------------------------
    /// How much of the film this spot holds: the zone's film x its open sky, none under water, under snow (fallen or
    /// painted, yd: the cover lies on it) or on hot ground (lava streams: hotShare, the share painted with a hot material).
    inline float SpotFilm(float zoneFilm, float open, float snowYd, float hotShare, bool submerged)
    {
        if (submerged || !(zoneFilm > 0.0f) || !(open > 0.0f)) return 0.0f;
        const float snow = snowYd > 0.0f ? (snowYd >= 0.05f ? 1.0f : snowYd / 0.05f) : 0.0f;
        const float hot = hotShare > 0.0f ? (hotShare < 1.0f ? hotShare : 1.0f) : 0.0f;
        const float v = zoneFilm * (open < 1.0f ? open : 1.0f) * (1.0f - snow) * (1.0f - hot);
        return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f;
    }

    /// A cell's lowness: the mean height of the cells round it (two rings, 4 and 8 yd out) minus its own, in yd; > 0 in a
    /// hollow. Only the neighbours known count; fewer than kMinNeighbours known: 0 (unknown yet).
    constexpr int kMinNeighbours = 8;
    constexpr int kRingCells = 16;
    constexpr int kRing[kRingCells][2] = { { 2, 0 }, { -2, 0 }, { 0, 2 }, { 0, -2 }, { 2, 2 }, { 2, -2 }, { -2, 2 }, { -2, -2 },
                                           { 4, 0 }, { -4, 0 }, { 0, 4 }, { 0, -4 }, { 4, 4 }, { 4, -4 }, { -4, 4 }, { -4, -4 } };

    constexpr float kBasinFrom = 0.03f, kBasinFull = 0.35f;  // yd of lowness: no puddle .. full basin share
    constexpr float kFlatFrom = 0.06f, kFlatTo = 0.16f;      // the ground's slope (sin): flat .. too steep for a puddle
    constexpr float kBasinShare = 0.6f, kNoiseShare = 0.6f;  // the potential's two parts
    inline float SmoothStep(float a, float b, float x)
    {
        float t = (x - a) / (b - a);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return t * t * (3.0f - 2.0f * t);
    }
    /// A spot's puddle potential, 0..1 (static): flat ground (slope = the ground normal's horizontal length) in a hollow
    /// (`lowness` yd) holds water first; `noise` (CellNoise) breaks flat ground into patches.
    inline float PuddlePotential(float lowness, float slope, float noise)
    {
        if (!std::isfinite(lowness) || !std::isfinite(slope) || !std::isfinite(noise)) return 0.0f;
        const float flat = 1.0f - SmoothStep(kFlatFrom, kFlatTo, slope);
        const float v = flat * (kBasinShare * SmoothStep(kBasinFrom, kBasinFull, lowness) + kNoiseShare * noise * flat);
        return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f;
    }
    /// The potential where it counts: none under a roof, water, snow or on hot ground (as SpotFilm).
    inline float SpotPotential(float potential, float open, float snowYd, float hotShare, bool submerged)
    {
        return SpotFilm(potential, open, snowYd, hotShare, submerged);
    }
    /// Smooth value noise over the world's cells (about 3 cells, 6 yd, per feature), 0..1, the same for a cell every time.
    /// (Inline, as everything env/Fields uses from here: its offline tests link Fields.cpp on its own.)
    inline float CellNoise(int i, int j)
    {
        auto lattice = [](int a, int b)
        {
            uint32_t x = static_cast<uint32_t>(a) * 73856093u ^ static_cast<uint32_t>(b) * 19349663u ^ 0x5bd1e995u;
            x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
            return static_cast<float>(x & 0xFFFFFF) / 16777215.0f;
        };
        constexpr int kStep = 3;
        const int gi = i >= 0 ? i / kStep : -((-i + kStep - 1) / kStep), gj = j >= 0 ? j / kStep : -((-j + kStep - 1) / kStep);
        const float fx = (static_cast<float>(i - gi * kStep) + 0.5f) / kStep, fy = (static_cast<float>(j - gj * kStep) + 0.5f) / kStep;
        const float sx = fx * fx * (3.0f - 2.0f * fx), sy = fy * fy * (3.0f - 2.0f * fy);
        const float a = lattice(gi, gj), b = lattice(gi + 1, gj), c = lattice(gi, gj + 1), d = lattice(gi + 1, gj + 1);
        const float top = a + (b - a) * sx, bottom = c + (d - c) * sx;
        const float v = top + (bottom - top) * sy;
        return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f;
    }

    /// Where puddles are: potential > the threshold, softly. The pool (0..1) and the setting (0..2) lower it from 1 (no
    /// puddles) to 1 - kPoolReach x pool x puddles. PuddleAt is what the shader works out per pixel.
    constexpr float kPoolReach = 0.55f;
    constexpr float kPuddleSharpness = 6.0f;
    constexpr float kMinThreshold = 0.05f; // never below: beyond the grid (potential 0) nothing floods
    inline float PuddleThreshold(float pool, float puddles)
    {
        const float t = 1.0f - kPoolReach * pool * puddles;
        return t > kMinThreshold ? t : kMinThreshold;
    }
    inline float PuddleAt(float potential, float pool, float puddles)
    {
        const float v = (potential - PuddleThreshold(pool, puddles)) * kPuddleSharpness;
        return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f;
    }

    // --- what a surface is made of ----------------------------------------------------------------------------------------
    /// How much a terrain layer takes the wet look (0 none .. 1 stone), by its ground effect's TerrainType when no
    /// GroundMaterial in its chain sets WetShine: 0 Dirt 0.8, 1 Metallic 1, 2 Stone 1, 3 Snow 0, 4 Wood 0.6, 5 Grass 0,
    /// 6 Leaves 0, 7 Sand 0.5, 8 Soggy 0.9, 9 Dusty Grass 0.1, 10 None 0.6, 11 Water 1; no ground effect (-1) 0.6.
    float TerrainTypeShine(int terrainType);
    /// A layer's shine: the material's WetShine where its chain sets one (`materialSet`), else TerrainTypeShine; none on
    /// clear ice (its own look), snow or hot ground (a material with a Temperature of 60 degC or more).
    float LayerShine(bool materialSet, float materialShine, bool clearIce, bool snow, bool hot, int terrainType);

    /// A building texture's shine (its base name, any case or slashes): 0 for what never looks wet (glass, water, lava,
    /// fire and lights, the sky, foliage, cloth and banners, rope and chains, snow and ice, crystals, environment maps),
    /// 1 stone (walls, floors, cobbles, bricks, tiles, stairs, roads, columns, trim), 0.8 metal, 0.7 roof tiles and
    /// shingles, 0.5 wood, 0.15 thatch, else 0.5.
    float WmoTextureShine(const char* path);

    // --- the shaders' constants ---------------------------------------------------------------------------------------
    /// What a frame's constants come from.
    struct Frame
    {
        float film = 0.0f, pool = 0.0f;     // the player's zone's (beyond the moisture grid, every spot is open sky)
        float camera[3] = {};
        float sun[3] = { 0.0f, 0.0f, 1.0f };// toward the sun, world
        float sunColour[3] = { 1.0f, 1.0f, 1.0f };
        float daylight = 1.0f;              // 0 night .. 1 day
        float overcast = 0.0f;              // 0 clear .. 1 a heavy rain's sky (no sun glint)
    };

    /// The shader's wetness (r1.x) is the film x kSurfaceShare, or 1 in a puddle: a wet surface has this share of a
    /// puddle's darkening and highlights' base gain, and its square of the sky (Fresnel). (The grid's film bytes and c161.w
    /// carry it scaled.)
    constexpr float kSurfaceShare = 0.6f;
    /// The lobe of a highlight: saturate(dot(L, R) x a + b)^2, a = 1 / (1 - cos), b = -cos / (1 - cos): 1 along the
    /// mirror direction, 0 from `cos` off it. Wet stone a broad sheen, a puddle a mirror's sharp image.
    constexpr float kWetLobeCos = 0.90f, kPuddleLobeCos = 0.993f;
    /// The highlights' brightness (x our light's light there, x the wetness): wet ground kWetGain x kSurfaceShare (1.5),
    /// a puddle kPuddleGain. The lobes' peaks are 1 whatever their width, so the narrower lobe needs the larger gain to give
    /// what a mirror does: an image of the flame about as bright as the flame (a light's light on the ground falls off with
    /// distance and slant; its image in still water much less).
    constexpr float kWetGain = 2.5f, kPuddleGain = 25.0f;
    constexpr float kDarkening = 0.4f;                   // x the wetness: wet ground 24% darker, puddles 40% (on top of the soil's own)
    constexpr float kF0 = 0.02f;                         // water's reflectance face on
    constexpr float kSunGlint = 0.35f;                   // the sun's highlight, x its colour (x the gain: wet ground 0.5, a puddle white)

    /// Terrain pixel shader c161..c167 (7 registers, in order): c161 (camera xyz, the far film), c162 (the puddle
    /// threshold's offset, its sharpness, the darkening, 0), c163 (wet lobe a, b, puddle lobe a, b), c164 (toward the sun,
    /// 0), c165 ((1 - F0) x intensity, F0 x intensity, 0, 0), c166 (the sun's highlight colour, 0), c167 (puddle gain -
    /// wet gain, wet gain, 0, 0). All zeros: nothing changes (the film and puddles are 0, the highlights' gain is 0).
    constexpr int kTerrainConstant = 161, kTerrainRegisters = 7;
    void TerrainConstants(const Settings& s, const Frame& f, float c[4 * kTerrainRegisters]);

    /// The terrain's per-chunk layer mask, c160 = (layer 0..3's shine) x 0..1, set by the chunk draw.
    constexpr int kMaskConstant = 160;

    /// Buildings (features/ModelIce's block, Style::Wet): c77 = (the facing's share, its slope, 0, 0): the weight goes
    /// 1 - saturate(c77.x - N.z x c77.y): floors 1, walls 1 - c77.x, ceilings none. Zeros: 1 everywhere (the ice and the
    /// crystals, and anything that never set it).
    constexpr int kFacingConstant = 77;
    constexpr float kWallShare = 0.4f;
    void FacingConstant(bool wet, float c77[4]);
    /// The wet style's weight for a batch: the film x the texture's shine, 0..1 (0 while off or buildings are left out).
    float BuildingWeight(const Settings& s, float film, float shine);
    /// The wet style's darkening at intensity 1 (c179.z, as a brightness change: -0.22 = 22% darker), its Fresnel strength
    /// (c185.w) and its glint's exponent (c184.w: a broad sheen).
    constexpr float kBuildingDarkening = 0.22f, kBuildingFresnel = 0.8f, kBuildingGlintPower = 60.0f;
    /// How much of the sun's glint shows: by day, not under a heavy rain's sky. 0..1.
    float SunShare(const Frame& f);
    /// Intensity as the shaders take it, 0..2.
    float Intensity(const Settings& s);
}
