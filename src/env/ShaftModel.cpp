#include "ShaftModel.hpp"

#include <algorithm>
#include <cmath>

namespace wxl_livingazeroth::shafts
{
    namespace
    {
        float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : (v == v ? v : 0.0f)); }
        float Smooth(float a, float b, float v) { const float t = Clamp01((v - a) / (b - a)); return t * t * (3.0f - 2.0f * t); }

        void Normalize(float v[3])
        {
            const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (len > 1.0e-12f) for (int i = 0; i < 3; ++i) v[i] /= len;
        }
    }

    float AirAmount(float fogginess, float mist, float areaFog, float haze, float waterFog)
    {
        // A place that is foggy by nature counts before its fog has built up (arriving in Duskwood), its area fog
        // after; mist and haze a little less (a thinner, lower layer), water fog least (it hangs over the water).
        const float veil[4] = {
            Clamp01(1.2f * std::max(Clamp01(fogginess), Clamp01(areaFog))),
            0.8f * Clamp01(mist),
            0.6f * Clamp01(haze),
            0.4f * Clamp01(waterFog),
        };
        float through = 1.0f;
        for (float v : veil) through *= 1.0f - v;
        return Clamp01(1.0f - through);
    }

    float GoldenHour(float hour)
    {
        const float h = hour - 24.0f * std::floor(hour / 24.0f);
        const float morning = (h - 7.0f) / 1.6f, evening = (h - 19.0f) / 1.8f;
        return std::max(std::exp(-morning * morning), std::exp(-evening * evening));
    }

    Strength Evaluate(const Inputs& in)
    {
        Strength s;
        s.air = AirAmount(in.fogginess, in.mist, in.areaFog, in.haze, in.waterFog);
        s.airFactor = 0.3f + 0.7f * s.air;
        s.airPerYard = 0.002f + 0.010f * s.air;
        s.golden = GoldenHour(in.hour);
        // Above the horizon (the client's light never goes below it: env/Climate turns it up; this is for a sun on it).
        const float up = Smooth(0.0f, 0.06f, in.sunHeight);
        const float night = Clamp01(in.night);
        s.sun = (1.0f - night) * (0.5f + 0.5f * s.golden) * up;
        s.moon = 0.3f * Clamp01(in.moon) * night * up;
        s.weather = (1.0f - Smooth(0.15f, 0.6f, in.precipitation)) * (1.0f - Smooth(0.2f, 0.7f, in.storminess));
        s.indoor = 1.0f - Clamp01(in.interior);
        s.total = std::min(s.sun + s.moon, 1.0f) * s.airFactor * s.weather * s.indoor;
        if (in.interior >= 0.999f) s.why = "indoors";
        else if (s.weather < 0.01f) s.why = in.storminess > 0.5f ? "a storm" : "heavy rain or snow";
        else if (s.sun + s.moon < 0.01f) s.why = night > 0.5f ? (in.moon > 0.0f ? "night (and the light is below the horizon)" : "night (Moon is off)") : "the sun is on the horizon";
        else if (s.total < 0.003f) s.why = "too faint here and now";
        return s;
    }

    float ScreenFade(float u, float v, float margin)
    {
        const float dx = std::max(0.0f, std::max(-u, u - 1.0f)), dy = std::max(0.0f, std::max(-v, v - 1.0f));
        const float off = std::sqrt(dx * dx + dy * dy);
        return 1.0f - Smooth(0.0f, std::max(margin, 1.0e-3f), off);
    }

    SunScreen ProjectSun(const float view[16], const float proj[16], const float dir[3], float margin)
    {
        SunScreen s;
        // A direction (w = 0): the view's translation doesn't move it.
        for (int j = 0; j < 3; ++j) s.view[j] = dir[0] * view[j] + dir[1] * view[4 + j] + dir[2] * view[8 + j];
        Normalize(s.view);
        float clip[4];
        for (int j = 0; j < 4; ++j) clip[j] = s.view[0] * proj[j] + s.view[1] * proj[4 + j] + s.view[2] * proj[8 + j];
        if (!(clip[3] > 1.0e-4f)) { s.inFront = false; s.fade = 0.0f; return s; }
        s.inFront = true;
        s.u = 0.5f + 0.5f * clip[0] / clip[3];
        s.v = 0.5f - 0.5f * clip[1] / clip[3];
        s.fade = ScreenFade(s.u, s.v, margin);
        return s;
    }

    void ViewRay(const float proj[16], float u, float v, float out[3])
    {
        // As the shaders (and the ground fog's): x = (ndc.x - m20) / m00 per unit of view depth (w = the view depth).
        const float nx = u * 2.0f - 1.0f, ny = 1.0f - v * 2.0f;
        out[0] = (nx - proj[8]) / proj[0];
        out[1] = (ny - proj[9]) / proj[5];
        out[2] = 1.0f;
        Normalize(out);
    }

    void ViewToWorld(const float view[16], const float in[3], float out[3])
    {
        // view = p x R + t (rows): world = v x R^-1. R is a rotation (perhaps mirrored), but invert it properly anyway.
        const float r[3][3] = { { view[0], view[1], view[2] }, { view[4], view[5], view[6] }, { view[8], view[9], view[10] } };
        const float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                          r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
        if (std::fabs(det) < 1.0e-12f) { out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; return; }
        float inv[3][3];
        inv[0][0] = (r[1][1] * r[2][2] - r[1][2] * r[2][1]) / det;
        inv[0][1] = (r[0][2] * r[2][1] - r[0][1] * r[2][2]) / det;
        inv[0][2] = (r[0][1] * r[1][2] - r[0][2] * r[1][1]) / det;
        inv[1][0] = (r[1][2] * r[2][0] - r[1][0] * r[2][2]) / det;
        inv[1][1] = (r[0][0] * r[2][2] - r[0][2] * r[2][0]) / det;
        inv[1][2] = (r[0][2] * r[1][0] - r[0][0] * r[1][2]) / det;
        inv[2][0] = (r[1][0] * r[2][1] - r[1][1] * r[2][0]) / det;
        inv[2][1] = (r[0][1] * r[2][0] - r[0][0] * r[2][1]) / det;
        inv[2][2] = (r[0][0] * r[1][1] - r[0][1] * r[1][0]) / det;
        for (int k = 0; k < 3; ++k) out[k] = in[0] * inv[0][k] + in[1] * inv[1][k] + in[2] * inv[2][k];
        Normalize(out);
    }

    float UnderDeck(float camZ, float base, float top)
    {
        const float edge = std::max(std::min((top - base) * 0.25f, 100.0f), 1.0f);
        return 1.0f - Smooth(base, base + edge, camZ);
    }

    float DeckMeanExtinction(float density, float cover)
    {
        return std::max(density, 0.0f) * 0.55f * (0.3f + 0.7f * Clamp01(cover));
    }

    float DeckSlabLength(float camZ, float dirZ, float base, float top, float range)
    {
        if (std::fabs(dirZ) > 1.0e-4f)
        {
            const float ea = (base - camZ) / dirZ, eb = (top - camZ) / dirZ;
            return std::max(std::min(std::max(ea, eb), range) - std::max(std::min(ea, eb), 0.0f), 0.0f);
        }
        return camZ > base && camZ < top ? range : 0.0f;
    }
}
