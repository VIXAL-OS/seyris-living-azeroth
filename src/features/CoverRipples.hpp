// Still rings in sand and mud around the client's footprints (features/SurfaceCover.cpp, and on buildings' sand floors
// features/WmoIce), kept apart so the rules can be checked offline (tests/cover).
//
// "Simon is of the opinion that sand shouldn't have trenches but should rather have unmoving 'ripples' centered on
// each footprint & i'm inclined to agree with him" (the user, 2026-10-11). On covers whose row sets kFlagRipplePrints
// (sand, mud) units carve no trench. Each print the client puts down there gets one rim of displaced sand around it
// ("we probably only need 1 ring for these"), doesn't move or spread, and fades out slowly ("footprint sand rings fade
// too fast imo"). On the terrain's cover it follows the print's own outline -- the boot, bare foot, hoof, claw or paw
// the client drew there, pointed and mirrored the way it drew it (Shape) --; on buildings' floors, an oval the print's
// length and width pointed along the stride. The pixel shaders draw it (shading and a little brightness, no geometry):
// the cover's 0.25 yd grid is too coarse for a rim a few inches wide.
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

#include "FootprintOutlines.hpp"

namespace wxl_livingazeroth::cover::ripples
{
    /// Rims drawn at once on the terrain's cover (pixel shader constants c42.., kRegistersPerRim each), the youngest kept.
    constexpr int kMax = 48;
    /// Buildings' floors: the rim's centre line lies this far out on the oval through the print's outline (1 = the outline).
    constexpr float kRimAt = 1.12f;
    /// Its width, as a share of that oval.
    constexpr float kRimWidth = 0.55f;
    /// How far a print's rim reaches, as a share of its larger semi-axis (the shaders' quick reject).
    constexpr float kReach = kRimAt + 0.5f * kRimWidth + 0.1f;

    /// The rim on the terrain's cover follows the print's own outline ("would it be possible to make the rings into sand
    /// outlines around the prints themselves?", the user, 2026-10-12). A Shape is how far that outline lies from its
    /// centre, by the direction on the ground, as a Fourier series of kShapeTerms harmonics in units of its mean distance
    /// r0 (eight keep the claw's toes and the paw's pads). The rim's centre line lies kRimOffset r0 outside the outline
    /// (along the direction from the centre), and it is kRimBand r0 wide: nothing at either edge, full on that line.
    constexpr int kShapeTerms = 8;
    constexpr int kShapeRays = 64;
    constexpr float kRimOffset = 0.2f;
    constexpr float kRimBand = 0.45f;

    struct Shape
    {
        float cx = 0.0f, cy = 0.0f;   // the outline's centre (world yd)
        float r0 = 0.0f;              // its mean distance from there (yd); 0: none
        float a[kShapeTerms] = {};    // distance / r0 = 1 + the sum over k of a[k-1] cos k t + b[k-1] sin k t, t the
        float b[kShapeTerms] = {};    //   direction on the ground (from world +x toward +y)
        float reach = 0.0f;           // how far from the centre the rim reaches in x or y (yd; the shaders' quick reject)
    };

    /// The outline's distance from the centre toward direction t, in units of r0.
    inline float ShapeRadius(const Shape& s, float t)
    {
        float r = 1.0f;
        for (int k = 1; k <= kShapeTerms; ++k) r += s.a[k - 1] * std::cos(k * t) + s.b[k - 1] * std::sin(k * t);
        return r;
    }

    /// A shape from a closed outline (n world points in order) about a centre it surrounds: its farthest crossing along
    /// kShapeRays directions, then the series. False when a direction crosses nothing or the outline is tiny.
    inline bool ShapeFromOutline(const float* xy, int n, float cx, float cy, Shape& out)
    {
        if (n < 3) return false;
        double dist[kShapeRays];
        for (int j = 0; j < kShapeRays; ++j)
        {
            const double t = 6.283185307179586 * j / kShapeRays;
            const double dx = std::cos(t), dy = std::sin(t);
            double farthest = -1.0;
            for (int i = 0; i < n; ++i)
            {
                // The ray (centre + along d) against the edge p + s e: along = p x e / d x e, s = p x d / d x e.
                const int k = (i + 1) % n;
                const double px = xy[2 * i] - cx, py = xy[2 * i + 1] - cy;
                const double ex = xy[2 * k] - xy[2 * i], ey = xy[2 * k + 1] - xy[2 * i + 1];
                const double den = dx * ey - dy * ex;
                if (std::fabs(den) < 1e-12) continue;
                const double along = (px * ey - py * ex) / den, s = (px * dy - py * dx) / den;
                if (s >= 0.0 && s <= 1.0 && along > farthest) farthest = along;
            }
            if (!(farthest > 0.0)) return false;
            dist[j] = farthest;
        }
        double mean = 0.0;
        for (double d : dist) mean += d;
        mean /= kShapeRays;
        if (!(mean > 1e-3)) return false;
        Shape s;
        s.cx = cx;
        s.cy = cy;
        s.r0 = static_cast<float>(mean);
        for (int k = 1; k <= kShapeTerms; ++k)
        {
            double ca = 0.0, sb = 0.0;
            for (int j = 0; j < kShapeRays; ++j)
            {
                const double t = 6.283185307179586 * j / kShapeRays;
                ca += dist[j] * std::cos(k * t);
                sb += dist[j] * std::sin(k * t);
            }
            s.a[k - 1] = static_cast<float>(2.0 * ca / kShapeRays / mean);
            s.b[k - 1] = static_cast<float>(2.0 * sb / kShapeRays / mean);
        }
        float most = 0.0f;
        for (int i = 0; i < 256; ++i) most = std::fmax(most, ShapeRadius(s, 6.28318531f * i / 256));
        s.reach = s.r0 * (most + kRimOffset + 0.5f * kRimBand + 0.05f);
        out = s;
        return true;
    }

    /// The oval through a print's outline (semi-axes along and across `axis`, yd) as a shape: for a print whose texture
    /// isn't known or whose placement couldn't be read.
    inline bool ShapeFromOval(float x, float y, const float axis[2], float along, float across, Shape& out)
    {
        float xy[2 * kShapeRays];
        for (int i = 0; i < kShapeRays; ++i)
        {
            const float t = 6.28318531f * i / kShapeRays, c = std::cos(t) * along, s = std::sin(t) * across;
            xy[2 * i] = x + axis[0] * c - axis[1] * s;
            xy[2 * i + 1] = y + axis[1] * c + axis[0] * s;
        }
        return ShapeFromOutline(xy, kShapeRays, x, y, out);
    }

    /// The shape of a print the client drew with footprint texture `texture` (FootprintTextures.dbc), whose texture
    /// coordinates over the ground are u = map[0] dx + map[1] dy + map[2], v = map[3] dx + map[4] dy + map[5] with
    /// (dx, dy) = (x - px, y - py) (coverprints::FitPrintMapping): the texture's baked outline (FootprintOutlines.hpp)
    /// taken back onto the ground, so it points the way the client pointed the print, and is mirrored for the other foot
    /// as the client mirrored it. False for a texture it doesn't know or a mapping that can't be undone.
    inline bool ShapeFromPrint(int texture, const float map[6], float px, float py, Shape& out)
    {
        const BakedOutline* o = FindOutline(texture);
        const float det = map[0] * map[4] - map[1] * map[3];
        if (!o || !(std::fabs(det) > 1e-6f)) return false;
        const auto ground = [&](float u, float v, float& x, float& y)
        {
            const float du = u - map[2], dv = v - map[5];
            x = px + (map[4] * du - map[1] * dv) / det;
            y = py + (map[0] * dv - map[3] * du) / det;
        };
        float xy[2 * kOutlineRays];
        for (int i = 0; i < kOutlineRays; ++i)
        {
            const float t = 6.28318531f * i / kOutlineRays;
            ground(o->cu + o->radius[i] * std::cos(t), o->cv + o->radius[i] * std::sin(t), xy[2 * i], xy[2 * i + 1]);
        }
        float cx = 0.0f, cy = 0.0f;
        ground(o->cu, o->cv, cx, cy);
        return ShapeFromOutline(xy, kOutlineRays, cx, cy, out);
    }

    /// One print's rim: where the print lies (world yd), the direction it points (unit, world x, y) and its outline's
    /// semi-axes along and across that (yd) for the oval, its own outline's shape, when it was put down and how long it
    /// lasts (seconds of the cover's clock).
    struct Ripple
    {
        float x = 0.0f, y = 0.0f;
        float axis[2] = { 1.0f, 0.0f };
        float along = 0.15f, across = 0.15f;
        Shape shape;
        float born = 0.0f, life = 25.0f;
    };

    /// Two of a shape's coefficients (each clamped to -1..1, 10 bits) in one float, exactly: lo + 1024 hi. The shaders
    /// take them apart with floor.
    inline float PackPair(float lo, float hi)
    {
        const auto q = [](float c) { return static_cast<float>(std::lround((std::fmin(std::fmax(c, -1.0f), 1.0f) + 1.0f) * 511.5f)); };
        return q(lo) + 1024.0f * q(hi);
    }
    inline float UnpackLo(float v) { return (v - 1024.0f * std::floor(v / 1024.0f)) / 511.5f - 1.0f; }
    inline float UnpackHi(float v) { return std::floor(v / 1024.0f) / 511.5f - 1.0f; }

    /// A rim in the terrain cover's pixel shader (SurfaceCover's RippleRings): (its centre relative to the camera,
    /// 1 / r0, its reach in 1/1024 yd plus 0.99 x its strength now), then the coefficients in pairs (a_k, b_k), k = 1..8.
    constexpr int kRegistersPerRim = 3;
    inline void PackRim(const Shape& s, float eyeX, float eyeY, float strength, float out[4 * kRegistersPerRim])
    {
        out[0] = s.cx - eyeX;
        out[1] = s.cy - eyeY;
        out[2] = 1.0f / s.r0;
        out[3] = std::ceil(s.reach * 1024.0f) + 0.99f * std::fmin(std::fmax(strength, 0.0f), 1.0f);
        for (int k = 0; k < kShapeTerms; ++k) out[4 + k] = PackPair(s.a[k], s.b[k]);
    }

    /// The shape as the shaders get it (its coefficients through PackPair).
    inline Shape AsPacked(const Shape& s)
    {
        Shape q = s;
        for (int k = 0; k < kShapeTerms; ++k)
        {
            const float v = PackPair(s.a[k], s.b[k]);
            q.a[k] = UnpackLo(v);
            q.b[k] = UnpackHi(v);
        }
        return q;
    }

    /// One rim at world (x, y) the way RippleRings works it out: its height, added to `height`, and its tilt of the
    /// normal, added to grad (world x, y). `px` is the pixel's size (yd): where it spans more than a quarter of the
    /// rim's width the rim fades out instead of shimmering. Returns the band coordinate (0 on the outline, in r0).
    inline float RimShade(const Shape& s, float strength, float x, float y, float px, float& height, float grad[2])
    {
        const float nx = (x - s.cx) / s.r0, ny = (y - s.cy) / s.r0;
        const float q = std::fmax(std::sqrt(nx * nx + ny * ny), 1e-3f);
        const float t = std::atan2(ny, nx);
        float dr = 0.0f;
        for (int k = 1; k <= kShapeTerms; ++k) dr += k * (s.b[k - 1] * std::cos(k * t) - s.a[k - 1] * std::sin(k * t));
        const float band = q - ShapeRadius(s, t);
        const float ph = 6.28318531f * (band - kRimOffset) / kRimBand;
        if (!(std::fabs(ph) < 3.14159265f)) return band;
        const float c = nx / q, sn = ny / q;
        const float g[2] = { (c + dr / q * sn) / s.r0, (sn - dr / q * c) / s.r0 };
        const float gl = std::fmax(std::sqrt(g[0] * g[0] + g[1] * g[1]), 1e-4f);
        const float env = strength * std::fmin(std::fmax(kRimBand / gl / (4.0f * px) - 0.5f, 0.0f), 1.0f);
        grad[0] += -std::sin(ph) * env / gl * g[0];
        grad[1] += -std::sin(ph) * env / gl * g[1];
        height += (0.5f + 0.5f * std::cos(ph)) * env;
        return band;
    }

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
