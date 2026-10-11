// Still ripples in sand around the client's footprints (features/SurfaceCover.cpp), kept apart so the rules
// can be checked offline (tests/cover).
//
// "Simon is of the opinion that sand shouldn't have trenches but should rather have unmoving 'ripples' centered
// on each footprint & i'm inclined to agree with him" (the user, 2026-10-11). On covers whose row sets
// kFlagRipplePrints (sand) units carve no trench. Each print the client puts down there gets rings of sand
// around it instead: they don't move or spread, and fade out over the cover's relax time. The pixel shader draws
// them (shading and a little brightness, no geometry): the cover's 0.25 yd grid is too coarse for rings a few
// inches apart.
#pragma once

#include <cstddef>
#include <vector>

namespace wxl_livingazeroth::cover::ripples
{
    /// Rings drawn at once (pixel shader constants c42..c73), the youngest kept.
    constexpr int kMax = 32;
    /// How far a print's rings reach, in its radius: they start at 0.6 of it and run 3 spacings of 0.75 of it.
    constexpr float kReach = 0.6f + 3.0f * 0.75f;

    /// One print's rings: centre (world yd), the print's radius (yd), when it was put down and how long it lasts
    /// (seconds of the cover's clock).
    struct Ripple
    {
        float x = 0.0f, y = 0.0f, radius = 0.2f, born = 0.0f, life = 10.0f;
    };

    /// The rings' radius from the print of the unit that put it down: its longer side (yd, x the unit's scale; 0 =
    /// unknown, 0.33 yd: a human's). The first ring lies at the print's edge, the next ones 0.75 of that apart:
    /// a human's 0.33 yd print rings out to about 0.55 yd, a large mount's more.
    inline float RadiusFor(float footprint)
    {
        const float r = (footprint > 0.0f ? footprint : 0.33f) * 0.6f;
        return r < 0.08f ? 0.08f : (r > 1.2f ? 1.2f : r);
    }

    /// How strongly a print's rings show `age` seconds after it was put down: fully for the first 40% of its life,
    /// then easing out to nothing at its end (the wind smoothing the sand, as it fills trenches in elsewhere).
    inline float Strength(float age, float life)
    {
        if (!(life > 0.0f) || age >= life) return 0.0f;
        if (age <= 0.4f * life) return 1.0f;
        const float t = (life - age) / (0.6f * life); // 1 .. 0
        return t * t * (3.0f - 2.0f * t);
    }

    /// The live rings: added as prints are put down, the oldest dropped when kMax are already there or when their
    /// life is over.
    class Set
    {
    public:
        void Add(const Ripple& r)
        {
            if (m_list.size() >= static_cast<size_t>(kMax))
            {
                size_t oldest = 0;
                for (size_t k = 1; k < m_list.size(); ++k)
                    if (m_list[k].born < m_list[oldest].born) oldest = k;
                m_list[oldest] = r;
                return;
            }
            m_list.push_back(r);
        }
        void Expire(float now)
        {
            for (size_t k = 0; k < m_list.size();)
            {
                if (now - m_list[k].born >= m_list[k].life) { m_list[k] = m_list.back(); m_list.pop_back(); }
                else ++k;
            }
        }
        void Clear() { m_list.clear(); }
        const std::vector<Ripple>& List() const { return m_list; }

    private:
        std::vector<Ripple> m_list;
    };
}
