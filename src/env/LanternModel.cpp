#include "LanternModel.hpp"

#include <algorithm>
#include <cmath>

namespace wxl_livingazeroth::lantern
{
    namespace
    {
        float Clamp01(float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }
        float Smooth(float a, float b, float v)
        {
            const float t = Clamp01((v - a) / (b - a));
            return t * t * (3.0f - 2.0f * t);
        }
    }

    float PlaceDarkness(float daylight, bool indoors, float stormDarkness)
    {
        if (indoors) return 1.0f;
        const float night = Clamp01((kDaylightLit - Clamp01(daylight)) / kDaylightLit);
        return std::max(night, Clamp01(stormDarkness));
    }

    float LitShare(float around) { return 1.0f - Smooth(kLitFrom, kLitFull, std::isfinite(around) ? around : 0.0f); }

    bool Press(State& s, const Settings& c)
    {
        (void)c;
        s.held = s.lit ? 0 : 1;
        s.lit = s.held == 1;
        return s.lit;
    }

    float Advance(State& s, const Settings& c, float daylight, bool indoors, float stormDarkness, float around, float dt)
    {
        if (!std::isfinite(dt) || dt < 0.0f) dt = 0.0f;
        dt = std::min(dt, 0.5f);
        const float a = std::isfinite(around) && around > 0.0f ? around : 0.0f;
        if (!s.seeded) { s.around = a; s.seeded = true; }
        else s.around += (a - s.around) * (1.0f - std::exp(-dt / kAroundSeconds));
        s.darkness = PlaceDarkness(daylight, indoors, stormDarkness) * LitShare(s.around);

        // The automatic choice, with hysteresis; when it changes, the key's choice gives way to it.
        const bool wants = c.automatic ? (s.autoWants ? s.darkness >= kAutoOff : s.darkness >= kAutoOn) : false;
        if (c.automatic && wants != s.autoWants) s.held = -1;
        s.autoWants = wants;
        s.lit = s.held >= 0 ? s.held == 1 : (c.automatic && s.autoWants);

        const float target = s.lit ? 1.0f : 0.0f;
        const float rate = dt / (s.lit ? kLightSeconds : kOutSeconds);
        s.level = target > s.level ? std::min(target, s.level + rate) : std::max(target, s.level - rate);
        const float strength = std::isfinite(c.strength) ? std::max(0.0f, c.strength) : 1.0f;
        return s.level * s.darkness * strength;
    }
}
