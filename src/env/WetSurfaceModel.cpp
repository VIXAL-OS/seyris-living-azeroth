#include "WetSurfaceModel.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace wxl_livingazeroth::wetsurf
{
    namespace
    {
        float Clamp01(float v) { return std::isfinite(v) ? (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v)) : 0.0f; }

        bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
        bool HasAny(const std::string& s, std::initializer_list<const char*> parts)
        {
            for (const char* p : parts) if (Has(s, p)) return true;
            return false;
        }
    }

    void Step(ZoneWet& z, float dt, bool raining, float rain, float dryRate, float dryOutMinutes)
    {
        z.film = Clamp01(z.film);
        z.pool = Clamp01(z.pool);
        if (!std::isfinite(dt) || dt <= 0.0f) return;
        dt = std::min(dt, 3600.0f);
        if (raining)
        {
            const float pace = kLightRainShare + (1.0f - kLightRainShare) * Clamp01(rain);
            z.film += (1.0f - z.film) * (1.0f - std::exp(-dt * pace / kWetUpSeconds));
            z.pool += (1.0f - z.pool) * (1.0f - std::exp(-dt * pace / kPoolUpSeconds));
        }
        else
        {
            const float minutes = std::isfinite(dryOutMinutes) ? std::max(kMinDryMinutes, dryOutMinutes) : 15.0f;
            const float pace = std::isfinite(dryRate) ? std::clamp(dryRate, 0.5f, 2.0f) : 1.0f;
            const float perSecond = pace / (minutes * 60.0f);
            z.film = std::max(0.0f, z.film - dt * perSecond);
            z.pool = std::max(0.0f, z.pool - dt * perSecond / kPoolDryFactor);
        }
        z.film = Clamp01(z.film);
        z.pool = Clamp01(z.pool);
    }

    float DryRateFactor(float dryingRate, float mildDryingRate)
    {
        if (!std::isfinite(dryingRate) || !std::isfinite(mildDryingRate) || !(mildDryingRate > 0.0f)) return 1.0f;
        return std::clamp(dryingRate / mildDryingRate, 0.5f, 2.0f);
    }

    float TerrainTypeShine(int terrainType)
    {
        switch (terrainType)
        {
            case 0: return 0.8f;   // Dirt
            case 1: return 1.0f;   // Metallic
            case 2: return 1.0f;   // Stone
            case 3: return 0.0f;   // Snow
            case 4: return 0.6f;   // Wood
            case 5: return 0.0f;   // Grass
            case 6: return 0.0f;   // Leaves
            case 7: return 0.5f;   // Sand
            case 8: return 0.9f;   // Soggy
            case 9: return 0.1f;   // Dusty Grass
            case 10: return 0.6f;  // None
            case 11: return 1.0f;  // Water
            default: return 0.6f;  // no ground effect: mostly rock and bare ground
        }
    }

    float LayerShine(bool materialSet, float materialShine, bool clearIce, bool snow, bool hot, int terrainType)
    {
        if (clearIce || snow || hot) return 0.0f;
        return materialSet ? Clamp01(materialShine) : TerrainTypeShine(terrainType);
    }

    float WmoTextureShine(const char* path)
    {
        if (!path || !*path) return 0.0f;
        std::string p(path);
        for (char& c : p) c = c == '/' ? '\\' : static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        const size_t slash = p.rfind('\\');
        const std::string base = slash == std::string::npos ? p : p.substr(slash + 1);
        if (Has(base, "flagstone")) return 1.0f;   // (before "flag": banners)
        // Never wet-looking: light and its sources, the sky, water and lava, foliage and cloth, ropes and chains, snow and
        // ice (their own looks), crystals and magic, environment maps.
        if (HasAny(base, { "glass", "window", "_wnd", "water", "lava", "magma", "fire", "flame", "glow", "light", "lamp", "candle",
                           "torch", "lantern", "sky", "cloud", "envmap", "_env", "env_", "fx", "leaf", "leaves", "foliage", "grass", "vine",
                           "ivy", "hay", "straw", "bush", "tree", "branch", "flower", "plant", "cloth", "banner", "tapestry", "rug",
                           "carpet", "curtain", "fabric", "web", "rope", "chain", "flag", "sail", "tent", "hide", "fur", "canvas",
                           "snow", "ice", "frost", "crystal", "gem", "portal", "rune", "energy", "blood", "slime", "goo" }))
            return 0.0f;
        if (Has(base, "thatch")) return 0.15f;
        if (HasAny(base, { "roof", "shingle" })) return 0.7f;
        if (HasAny(base, { "stone", "cobble", "brick", "floor", "tile", "marble", "rock", "road", "street", "pave", "slate", "cement",
                           "granite", "flag", "stair", "step", "curb", "plaza", "bridge", "wall", "column", "pillar", "arch", "rubble",
                           "boulder", "trim", "path", "ground", "dirt", "mud", "foundation", "block", "statue", "tomb", "cliff" }))
            return 1.0f;
        if (HasAny(base, { "metal", "iron", "steel", "bronze", "copper", "gold", "silver", "pipe", "rail" })) return 0.8f;
        if (HasAny(base, { "wood", "plank", "beam", "timber", "board", "log", "dock", "fence", "door", "barrel", "crate" })) return 0.5f;
        return 0.5f;
    }

    float Intensity(const Settings& s) { return std::isfinite(s.intensity) ? std::clamp(s.intensity, 0.0f, 2.0f) : 1.0f; }

    float SunShare(const Frame& f) { return Clamp01(f.daylight) * (1.0f - Clamp01(f.overcast)); }

    void TerrainConstants(const Settings& s, const Frame& f, float c[4 * kTerrainRegisters])
    {
        std::memset(c, 0, sizeof(float) * 4 * kTerrainRegisters);
        const bool on = s.enabled != 0;
        const float I = on ? Intensity(s) : 0.0f;
        const float puddles = std::isfinite(s.puddles) ? std::clamp(s.puddles, 0.0f, 2.0f) : 1.0f;
        // c161: the camera, the film beyond the moisture grid.
        for (int k = 0; k < 3; ++k) c[k] = std::isfinite(f.camera[k]) ? f.camera[k] : 0.0f;
        c[3] = on ? Clamp01(f.film) * kSurfaceShare : 0.0f;
        // c162: -threshold, sharpness, darkening.
        c[4] = on ? -PuddleThreshold(Clamp01(f.pool), puddles) : -1.0f;
        c[5] = kPuddleSharpness;
        c[6] = kDarkening * I;
        // c163: the lobes.
        auto lobe = [](float cosine, float& a, float& b) { a = 1.0f / (1.0f - cosine); b = -cosine * a; };
        lobe(kWetLobeCos, c[8], c[9]);
        lobe(kPuddleLobeCos, c[10], c[11]);
        // c164: toward the sun (unit; straight up if unusable).
        float sun[3] = { f.sun[0], f.sun[1], f.sun[2] };
        const float len = std::sqrt(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
        if (!(len > 1e-4f) || !std::isfinite(len)) { sun[0] = 0.0f; sun[1] = 0.0f; sun[2] = 1.0f; }
        else for (float& v : sun) v /= len;
        for (int k = 0; k < 3; ++k) c[12 + k] = sun[k];
        // c165: Fresnel, x intensity.
        c[16] = (1.0f - kF0) * I;
        c[17] = kF0 * I;
        // c166: the sun's highlight.
        const float share = SunShare(f) * kSunGlint;
        for (int k = 0; k < 3; ++k) c[20 + k] = Clamp01(f.sunColour[k] * 0.5f) * 2.0f * share;
        // c167: the highlights' gains (the puddle's on top of the wet one), x intensity.
        c[24] = (kPuddleGain - kWetGain) * I;
        c[25] = kWetGain * I;
    }

    void FacingConstant(bool wet, float c77[4])
    {
        c77[0] = wet ? 1.0f - kWallShare : 0.0f;
        c77[1] = wet ? 1.0f - kWallShare : 0.0f;
        c77[2] = 0.0f;
        c77[3] = 0.0f;
    }

    float BuildingWeight(const Settings& s, float film, float shine)
    {
        if (!s.enabled || !s.buildings) return 0.0f;
        return Clamp01(Clamp01(film) * Clamp01(shine));
    }
}
