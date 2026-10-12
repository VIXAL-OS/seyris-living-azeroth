// Still rings in sand and mud around the client's footprints (features/SurfaceCover.cpp, and on buildings' sand floors
// features/WmoIce), kept apart so the rules can be checked offline (tests/cover).
//
// "Simon is of the opinion that sand shouldn't have trenches but should rather have unmoving 'ripples' centered on
// each footprint & i'm inclined to agree with him" (the user, 2026-10-11). On covers whose row sets kFlagRipplePrints
// (sand, mud) units carve no trench. Each print the client puts down there gets one rim of displaced sand around it
// ("we probably only need 1 ring for these"): it follows the print's outline -- an oval the print's length and width,
// pointed along the stride ("would it be possible for them to match the outlines of the footprints themselves?") --,
// doesn't move or spread, and fades out slowly ("footprint sand rings fade too fast imo"). The pixel shaders draw it
// (shading and a little brightness, no geometry): the cover's 0.25 yd grid is too coarse for a rim a few inches wide.
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace wxl_livingazeroth::cover::ripples
{
    /// Rims drawn at once on the terrain's cover (pixel shader constants c42.., two registers each), the youngest kept.
    constexpr int kMax = 48;
    /// The rim's centre line lies this far out on the oval through the print's outline (1 = the outline).
    constexpr float kRimAt = 1.12f;
    /// Its width, as a share of that oval.
    constexpr float kRimWidth = 0.55f;
    /// How far a print's rim reaches, as a share of its larger semi-axis (the shaders' quick reject).
    constexpr float kReach = kRimAt + 0.5f * kRimWidth + 0.1f;

    /// One print's rim: centre (world yd), the direction the print points (unit, world x, y), its outline's semi-axes
    /// along and across that (yd), when it was put down and how long it lasts (seconds of the cover's clock).
    struct Ripple
    {
        float x = 0.0f, y = 0.0f;
        float axis[2] = { 1.0f, 0.0f };
        float along = 0.15f, across = 0.15f;
        float born = 0.0f, life = 25.0f;
    };

    /// The print's outline from the unit's print size (CreatureModelData x its scale, yd; 0 = unknown): semi-axes along
    /// and across the stride. Unknown: a human's (0.33 x 0.15). Without a direction (the unit's first print) it is a
    /// circle as wide as the print is long, so it can't point the wrong way.
    inline void Outline(float length, float width, bool directed, float& along, float& across)
    {
        if (!(length > 0.0f)) length = 0.33f;
        if (!(width > 0.0f)) width = length * 0.45f;
        along = std::fmin(std::fmax(0.5f * length, 0.05f), 1.2f);
        across = std::fmin(std::fmax(0.5f * width, 0.04f), 1.2f);
        if (!directed) across = along;
    }

    /// The direction a print points: from the same foot's previous print (two prints back, a whole stride), else the
    /// other foot's; false when neither is a plausible step (0.1..3 yd) away. `prints` holds the unit's last ones,
    /// newest first, as x, y pairs.
    inline bool StrideDirection(const float* prints, int count, float x, float y, float axis[2])
    {
        for (int back = 1; back >= 0; --back)
        {
            if (back >= count) continue;
            const float dx = x - prints[2 * back], dy = y - prints[2 * back + 1];
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d >= 0.1f && d <= 3.0f) { axis[0] = dx / d; axis[1] = dy / d; return true; }
        }
        return false;
    }

    /// How long a print's rim lasts on ground whose cover fills back in over `relaxSeconds`: 2.5 times that, at least
    /// 20 s (sand 25 s, mud 50 s).
    inline float LifeFor(float relaxSeconds) { return std::fmax(20.0f, 2.5f * relaxSeconds); }

    /// How strongly a print's rim shows `age` seconds after it was put down: fully for the first half of its life,
    /// then easing out to nothing at its end (the wind smoothing the sand).
    inline float Strength(float age, float life)
    {
        if (!(life > 0.0f) || age >= life) return 0.0f;
        if (age <= 0.5f * life) return 1.0f;
        const float t = (life - age) / (0.5f * life); // 1 .. 0
        return t * t * (3.0f - 2.0f * t);
    }

    /// The live rims: added as prints are put down, the oldest dropped when `capacity` are already there or when their
    /// life is over.
    class Set
    {
    public:
        explicit Set(int capacity = kMax) : m_capacity(capacity) {}
        void Add(const Ripple& r)
        {
            if (m_list.size() >= static_cast<size_t>(m_capacity))
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
        int m_capacity;
        std::vector<Ripple> m_list;
    };
}
