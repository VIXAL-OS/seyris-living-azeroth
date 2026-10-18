#include "GlassModel.hpp"

#include <algorithm>
#include <cmath>

namespace wxl_livingazeroth::glass
{
    namespace
    {
        float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : (v == v ? v : 0.0f)); }
        float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
        float Smooth(float e0, float e1, float x) { const float t = Clamp01((x - e0) / (e1 - e0)); return t * t * (3.0f - 2.0f * t); }
    }

    const char* KindName(uint32_t kind)
    {
        switch (kind)
        {
        case kClear: return "clear";
        case kStained: return "stained";
        case kFrosted: return "frosted";
        case kLeaded: return "leaded";
        default: return "none";
        }
    }

    uint64_t PaneIndex::Key(uint32_t map, int tx, int ty)
    {
        return (static_cast<uint64_t>(map & 0xFFFF) << 48) | (static_cast<uint64_t>(static_cast<uint32_t>(tx + 0x800000) & 0xFFFFFF) << 24) |
               static_cast<uint64_t>(static_cast<uint32_t>(ty + 0x800000) & 0xFFFFFF);
    }

    void PaneIndex::Build(std::vector<Pane> panes)
    {
        m_panes = std::move(panes);
        m_tiles.clear();
        for (uint32_t i = 0; i < m_panes.size(); ++i)
        {
            const Pane& p = m_panes[i];
            if (!std::isfinite(p.pos[0]) || !std::isfinite(p.pos[1]) || !std::isfinite(p.pos[2])) continue;
            m_tiles[Key(p.map, static_cast<int>(std::floor(p.pos[0] / kTile)), static_cast<int>(std::floor(p.pos[1] / kTile)))].push_back(i);
        }
    }

    void PaneIndex::Near(uint32_t map, const float c[3], float range, size_t max, std::vector<const Pane*>& out) const
    {
        out.clear();
        if (!(range > 0.0f) || !max || m_panes.empty()) return;
        const int x0 = static_cast<int>(std::floor((c[0] - range) / kTile)), x1 = static_cast<int>(std::floor((c[0] + range) / kTile));
        const int y0 = static_cast<int>(std::floor((c[1] - range) / kTile)), y1 = static_cast<int>(std::floor((c[1] + range) / kTile));
        static thread_local std::vector<std::pair<float, uint32_t>> found;
        found.clear();
        const float r2 = range * range;
        for (int tx = x0; tx <= x1; ++tx)
            for (int ty = y0; ty <= y1; ++ty)
            {
                auto it = m_tiles.find(Key(map, tx, ty));
                if (it == m_tiles.end()) continue;
                for (uint32_t i : it->second)
                {
                    const Pane& p = m_panes[i];
                    const float dx = p.pos[0] - c[0], dy = p.pos[1] - c[1], dz = p.pos[2] - c[2];
                    const float d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 <= r2) found.push_back({ d2, i });
                }
            }
        if (found.size() > max)
        {
            std::nth_element(found.begin(), found.begin() + max, found.end());
            found.resize(max);
        }
        std::sort(found.begin(), found.end());
        for (const auto& f : found) out.push_back(&m_panes[f.second]);
    }

    Light Evaluate(const Inputs& in)
    {
        Light l;
        // Day: the client's own window fade is the authority on night (its lit windows come on with it).
        l.day = Clamp01(1.0f - in.night) * Smooth(0.0f, 0.25f, in.daylight);
        // Cloud, rain and storms take the daylight down (a storm's darkness most of all), never quite to nothing.
        l.dim = std::max(0.15f, (1.0f - 0.55f * Clamp01(in.overcast)) * (1.0f - 0.5f * Clamp01(in.precipitation)) * (1.0f - 0.7f * Clamp01(in.storm)));
        // The direct sun: above the horizon, and much less of it through cloud or rain (none in a storm).
        const float up = Smooth(-0.02f, 0.15f, in.sun[2]);
        l.sunGain = up * l.day * (1.0f - 0.85f * Clamp01(in.overcast)) * (1.0f - 0.8f * Clamp01(in.precipitation)) * (1.0f - Clamp01(in.storm));
        l.skyGain = l.day * l.dim;
        for (int k = 0; k < 3; ++k) l.sky[k] = Clamp01(in.sky[k]) * l.skyGain;
        return l;
    }

    float Fresnel(float cosine, float f0)
    {
        const float c = Clamp01(cosine), f = 1.0f - c;
        const float f5 = f * f * f * f * f;
        return f0 + (1.0f - f0) * f5;
    }

    float SunThrough(const float sun[3], const float out[3], float diffusion)
    {
        // The pane's outside faces `out`: the sun shines through it when it's on that side.
        const float c = Dot(sun, out);
        const float direct = Clamp01(c);
        const float wrap = Clamp01(0.5f + 0.5f * c);
        return direct + (wrap - direct) * Clamp01(diffusion);
    }

    float InsideWeight(uint32_t materialFlags, uint32_t groupFlags, float cameraIndoors)
    {
        if (materialFlags & kInsideFace) return 1.0f;
        if (materialFlags & kOutsideFace) return 0.0f;
        const bool interior = (groupFlags & kGroupIndoor) && !(groupFlags & kGroupExterior);
        return interior ? Clamp01(cameraIndoors) : 0.0f;
    }

    void InsideColour(const float texel[3], const float base[3], float saturation, const float lightThrough[3], float out[3])
    {
        const float lum = 0.2126f * texel[0] + 0.7152f * texel[1] + 0.0722f * texel[2];
        for (int k = 0; k < 3; ++k)
        {
            const float s = std::max(0.0f, lum + (texel[k] - lum) * saturation);
            out[k] = std::max(s * lightThrough[k], base[k]);
        }
    }

    bool ProjectionRows(const Pane& p, const float sun[3], const float origin[3], float reach, float rows[12])
    {
        const float denom = Dot(sun, p.out);
        if (!(denom > 0.02f) || !(reach > 0.0f) || !(p.halfWidth > 0.0f) || !(p.halfHeight > 0.0f)) return false;
        const float c[3] = { p.pos[0] - origin[0], p.pos[1] - origin[1], p.pos[2] - origin[2] };
        // t(P) = -(P - c).out / (sun.out); the crossing q = P + sun t, so a = (P - c).along + t sun.along: linear in P.
        const float sa = Dot(sun, p.along) / denom, su = Dot(sun, p.up) / denom;
        for (int k = 0; k < 3; ++k)
        {
            rows[k] = (p.along[k] - p.out[k] * sa) / p.halfWidth;
            rows[4 + k] = (p.up[k] - p.out[k] * su) / p.halfHeight;
            rows[8 + k] = -p.out[k] / (denom * reach);
        }
        rows[3] = -(c[0] * rows[0] + c[1] * rows[1] + c[2] * rows[2]);
        rows[7] = -(c[0] * rows[4] + c[1] * rows[5] + c[2] * rows[6]);
        rows[11] = -(c[0] * rows[8] + c[1] * rows[9] + c[2] * rows[10]);
        return true;
    }

    float FloorWeight(const float rows[12], const float point[3], const float roomMin[3], const float roomMax[3], float soft, float farSoft)
    {
        const float a = rows[0] * point[0] + rows[1] * point[1] + rows[2] * point[2] + rows[3];
        const float b = rows[4] * point[0] + rows[5] * point[1] + rows[6] * point[2] + rows[7];
        const float t = rows[8] * point[0] + rows[9] * point[1] + rows[10] * point[2] + rows[11];
        float w = Clamp01((1.0f - std::fabs(a)) * soft) * Clamp01((1.0f - std::fabs(b)) * soft);
        w *= t >= 0.0f ? Clamp01((1.0f - t) * farSoft) : 0.0f;
        float inside = 1e30f;
        for (int k = 0; k < 3; ++k) inside = std::min(inside, std::min(point[k] - roomMin[k], roomMax[k] - point[k]));
        return inside >= 0.0f ? w : 0.0f;
    }

    float FloorWeightWorld(const Pane& p, const float sun[3], const float point[3], float reach, float soft, float farSoft)
    {
        const float origin[3] = { 0, 0, 0 };
        float rows[12];
        if (!ProjectionRows(p, sun, origin, reach, rows)) return 0.0f;
        return FloorWeight(rows, point, p.roomMin, p.roomMax, soft, farSoft);
    }

    RoomFace RoomHit(const float pos[2], const float dir[3], float cellW, float cellH, float depth, float& distance)
    {
        // In the cell (0..1 across, up and deep), as the shader works it: each wall the ray heads for, the back wall.
        const float f[2] = { pos[0] / cellW - std::floor(pos[0] / cellW), pos[1] / cellH - std::floor(pos[1] / cellH) };
        const float d[3] = { dir[0] / cellW, dir[1] / cellH, std::max(dir[2], 1e-4f) / depth };
        const float tx = ((d[0] >= 0.0f ? 1.0f : 0.0f) - f[0]) / d[0];
        const float ty = ((d[1] >= 0.0f ? 1.0f : 0.0f) - f[1]) / d[1];
        const float tz = 1.0f / d[2];
        distance = std::min(tx, std::min(ty, tz));
        if (distance == tz) return kBackWall;
        if (distance == tx) return kSideWall;
        return d[1] >= 0.0f ? kCeiling : kFloor;
    }
}
