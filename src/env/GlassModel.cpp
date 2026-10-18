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

    // --- who lives behind the glass ---------------------------------------------------------------------------------
    const char* ClassName(uint32_t cls)
    {
        static const char* const names[kClassCount] = { "other", "house", "farm", "inn", "shop", "temple", "keep", "tower", "city", "ship" };
        return cls < kClassCount ? names[cls] : "?";
    }

    uint32_t PathHash(const char* path)
    {
        uint32_t h = 2166136261u;
        for (const char* p = path ? path : ""; *p; ++p)
        {
            char c = *p == '/' ? '\\' : *p;
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
        }
        return path && *path ? h : 0;
    }

    namespace
    {
        uint64_t GridKey(uint32_t map, float x, float y)
        {
            const int64_t cx = static_cast<int64_t>(std::floor(x / 8.0f)), cy = static_cast<int64_t>(std::floor(y / 8.0f));
            return (static_cast<uint64_t>(map & 0xFFFF) << 48) | (static_cast<uint64_t>(cx & 0xFFFFFF) << 24) | static_cast<uint64_t>(cy & 0xFFFFFF);
        }
        constexpr uint32_t kAnyMap = 0xFFFFFFFFu;
        uint64_t PathKey(uint32_t map, uint32_t path) { return (static_cast<uint64_t>(map) << 32) | path; }
    }

    void BuildingIndex::Build(std::vector<Building> rows)
    {
        m_rows = std::move(rows);
        m_grid.clear(); m_groups.clear(); m_defaults.clear();
        m_placements = 0;
        // Placement rows first (the groups name theirs by unique ID and position), then a city's groups.
        std::unordered_map<uint64_t, uint32_t> byUid;   // (map, unique ID) -> its placement row
        for (uint32_t i = 0; i < m_rows.size(); ++i)
        {
            const Building& b = m_rows[i];
            if (!std::isfinite(b.pos[0]) || !std::isfinite(b.pos[1]) || !std::isfinite(b.pos[2])) continue;
            if (b.uniqueId == 0)
            {
                if (b.path) { m_defaults.emplace(PathKey(b.map, b.path), i); m_defaults.emplace(PathKey(kAnyMap, b.path), i); }
                continue;
            }
            if (b.group != kWholePlacement) continue;
            m_grid[GridKey(b.map, b.pos[0], b.pos[1])].push_back(i);
            byUid[(static_cast<uint64_t>(b.map) << 32) | b.uniqueId] = i;
            ++m_placements;
        }
        for (uint32_t i = 0; i < m_rows.size(); ++i)
        {
            const Building& b = m_rows[i];
            if (b.uniqueId == 0 || b.group == kWholePlacement) continue;
            auto it = byUid.find((static_cast<uint64_t>(b.map) << 32) | b.uniqueId);
            if (it != byUid.end()) m_groups[(static_cast<uint64_t>(it->second) << 32) | b.group] = i;
        }
    }

    const Building* BuildingIndex::Find(uint32_t map, const float pos[3], uint32_t path, uint32_t group) const
    {
        const Building* best = nullptr;
        uint32_t bestRow = 0;
        if (pos && std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2]))
        {
            float bestD = kNear * kNear;
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                {
                    auto it = m_grid.find(GridKey(map, pos[0] + dx * 8.0f, pos[1] + dy * 8.0f));
                    if (it == m_grid.end()) continue;
                    for (uint32_t i : it->second)
                    {
                        const Building& b = m_rows[i];
                        if (path && b.path && b.path != path) continue;   // another WMO at the same spot
                        const float ex = b.pos[0] - pos[0], ey = b.pos[1] - pos[1], ez = b.pos[2] - pos[2];
                        const float d = ex * ex + ey * ey + ez * ez;
                        if (d <= bestD) { bestD = d; best = &b; bestRow = i; }
                    }
                }
        }
        if (best)
        {
            if (group != kWholePlacement && !m_groups.empty())
            {
                auto it = m_groups.find((static_cast<uint64_t>(bestRow) << 32) | group);
                if (it != m_groups.end()) return &m_rows[it->second];
            }
            return best;
        }
        if (!path) return nullptr;
        auto it = m_defaults.find(PathKey(map, path));
        if (it == m_defaults.end()) it = m_defaults.find(PathKey(kAnyMap, path));
        return it == m_defaults.end() ? nullptr : &m_rows[it->second];
    }

    namespace
    {
        float Frac(float v) { return v - std::floor(v); }
        float Lerp(float f, float a, float b) { return f * a + (1.0f - f) * b; }   // the shader's lrp f, a, b
        float Ge(float v) { return v >= 0.0f ? 1.0f : 0.0f; }                     // cmp v, 1, 0
    }

    void Hash42(float x, float y, float h[4])
    {
        using namespace room;
        float p[4] = { Frac(x * kHashK[0]), Frac(y * kHashK[1]), Frac(x * kHashK[2]), Frac(y * kHashK[3]) };
        const float q[4] = { p[3] + kHashAdd, p[2] + kHashAdd, p[0] + kHashAdd, p[1] + kHashAdd };   // p.wzxy + 33.33
        const float d = p[0] * q[0] + p[1] * q[1] + p[2] * q[2] + p[3] * q[3];
        for (float& v : p) v += d;
        const float s[4] = { p[0] + p[1], p[0] + p[2], p[1] + p[2], p[2] + p[3] };   // xxyz + yzzw
        const float m[4] = { p[2], p[1], p[3], p[0] };                               // zywx
        for (int k = 0; k < 4; ++k) h[k] = Frac(s[k] * m[k]);
    }

    void LayoutThresholds(const float w[4], float t[3])
    {
        float sum = 0.0f;
        for (int k = 0; k < 4; ++k) sum += std::max(0.0f, w[k]);
        float run = 0.0f;
        for (int k = 0; k < 3; ++k)
        {
            run += sum > 0.0f ? std::max(0.0f, w[k]) / sum : 0.0f;
            t[k] = std::min(1.0f, run);
        }
    }

    Layout PickLayout(float h, const float t[3])
    {
        if (h < t[0]) return kLayoutShelves;
        if (h < t[1]) return kLayoutCurtains;
        if (h < t[2]) return kLayoutDivider;
        return kLayoutPlain;
    }

    float LightsOutShare(float hour, float lateHours, float cap)
    {
        // The evening runs on past midnight: 12..36 h.
        if (!std::isfinite(hour)) hour = 0.0f;
        hour = std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f);
        const float t = (hour < 12.0f ? hour + 24.0f : hour) - lateHours;
        struct Key { float t, share; };
        static constexpr Key keys[] = { { 21.0f, 0.08f }, { 24.0f, 0.5f }, { 26.0f, 0.72f }, { 28.5f, 0.78f }, { 30.0f, 0.6f } };
        float share = keys[0].share;
        if (t >= keys[4].t) share = keys[4].share;
        else
            for (int k = 0; k < 4; ++k)
                if (t >= keys[k].t && t < keys[k + 1].t)
                {
                    share = keys[k].share + (keys[k + 1].share - keys[k].share) * (t - keys[k].t) / (keys[k + 1].t - keys[k].t);
                    break;
                }
        return std::min(share, Clamp01(cap));
    }

    void RoomOutside(const float P[3], const float V[3], const float N[3], const RoomConstants& k, const float r12[3],
                     const float outside[3], float out[3], RoomSample* sample)
    {
        using namespace room;
        // The pane's frame: along (-N.y, N.x, 0), or x for a skylight; up = N x along.
        float a[3] = { -N[1], N[0], 0.0f };
        if (a[0] * a[0] + a[1] * a[1] - 0.0001f < 0.0f) { a[0] = 1; a[1] = 0; a[2] = 0; }
        const float al = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        for (float& v : a) v /= al;
        const float u[3] = { N[1] * a[2] - N[2] * a[1], N[2] * a[0] - N[0] * a[2], N[0] * a[1] - N[1] * a[0] };
        const float cosine = std::max(0.0f, std::min(1.0f, Dot(N, V)));
        // The ray (cell units), where it enters (cells), the cell, the walls it heads for.
        const float d[3] = { -Dot(V, a) * k.c198[0], -Dot(V, u) * k.c198[1], std::max(cosine, 0.0001f) * k.c198[2] };
        const float pos[2] = { Dot(P, a) * k.c198[0], Dot(P, u) * k.c198[1] };
        const float f[2] = { Frac(pos[0]), Frac(pos[1]) };
        const float cell[2] = { pos[0] - f[0], pos[1] - f[1] };
        // (the way to the wall over the step, both as sizes: a step of 0 or -0 never divides)
        const float tx = std::fabs(Ge(d[0]) - f[0]) * (1.0f / std::max(std::fabs(d[0]), 0.0001f));
        const float ty = std::fabs(Ge(d[1]) - f[1]) * (1.0f / std::max(std::fabs(d[1]), 0.0001f)), tz = 1.0f / d[2];
        const float t = std::min(std::min(tx, ty), tz);
        // The face it meets; farther is darker.
        float face = d[1] >= 0.0f ? kFace[2] : kFace[1];
        if (-(tx - t) >= 0.0f) face = kFace[0];
        const float back = -(tz - t) >= 0.0f ? 1.0f : 0.0f;
        if (back > 0.0f) face = kFace[3];
        float shade = face * (1.0f / (t * kFar + 1.0f));
        // The room's hash and the building's (in a city, the block's).
        float h[4], bh[4];
        Hash42(cell[0] + k.c212[0], cell[1] + k.c212[1], h);
        const float bx = P[0] * k.c212[2], by = P[1] * k.c212[2];
        Hash42((by - Frac(by)) + k.c212[0], (bx - Frac(bx)) + k.c212[1], bh);
        float tint[3];
        for (int i = 0; i < 3; ++i) tint[i] = (k.c202[i] - k.c199[i]) * bh[0] + k.c199[i];
        const float bright = (bh[1] * 2.0f + -1.0f) * k.c202[3] + 1.0f;
        // The layout; where the ray meets the room; lit or dark.
        float mask[3];
        for (int i = 0; i < 3; ++i) mask[i] = h[0] - k.c204[i] >= 0.0f ? 0.0f : 1.0f;
        const float shelves = mask[0], curtains = mask[1] - mask[0], divider = mask[2] - mask[1];
        float hit[3] = { d[0] * t + f[0], d[1] * t + f[1], d[2] * t };
        const float lit = h[1] - k.c203[3] >= 0.0f ? 1.0f : 0.0f;
        // The dividing wall with its doorway.
        const float td = kDividerDepth * tz;
        const float hd[2] = { d[0] * td + f[0], d[1] * td + f[1] };
        const float ahead = (t - td) >= 0.0f ? divider : 0.0f;
        float door = std::min(-std::fabs(hd[0] - (h[3] * kDoorSpan + kDoorBase)) + kDoorHalfWidth, -hd[1] + kDoorHeight);
        door = door >= 0.0f ? ahead : 0.0f;
        const float wall = ahead - door;
        const float wallShade = (1.0f / (td * kFar + 1.0f)) * kDividerShade;
        shade = Lerp(wall, wallShade, shade);
        shade = shade * (1.0f - door * kDoorDim);
        const float dividerPoint[3] = { hd[0], hd[1], kDividerDepth };
        for (int i = 0; i < 3; ++i) hit[i] = Lerp(wall, dividerPoint[i], hit[i]);
        // Curtains.
        const float hc = d[0] * (kCurtainDepth * tz) + f[0];
        const float gapHalf = Frac(h[3] * kGapHash) * kGapHalfSpan + kGapMinHalf;
        float cloth = std::fabs(hc - (h[3] * kGapSpan + kGapBase)) + -gapHalf;
        cloth = Clamp01(cloth * kClothEdge) * curtains;
        float fold = std::fabs(Frac(hc * kFolds) - 0.5f) * kFoldAmp + kFoldBase;
        // Shelves on the back wall.
        float region = std::min(-std::fabs(hit[0] - kShelfCentre[0]) + kShelfHalf[0], -std::fabs(hit[1] - kShelfCentre[1]) + kShelfHalf[1]);
        region = (region >= 0.0f ? back : 0.0f) * shelves;
        const float sx = hit[0] * kSlots, sy = hit[1] * kShelves;
        const float fx = Frac(sx), fy = Frac(sy);
        float q = (sy - fy) * kShelfHashShelf + (sx - fx);
        q = h[3] * kShelfHashRoom + q;
        q = Frac(q * kHashK[0]);
        q = q * (q + kHashAdd);
        q = Frac(q * q + q * q);
        const float above = fy + -kPlank;
        float thing = std::min(q * kThingMax + -above, -std::fabs(fx - 0.5f) + kThingGap);
        thing = above >= 0.0f ? thing : -1.0f;
        float shelf = thing >= 0.0f ? Frac(q * kThingHash) * kThingSpan + kThingBase : kShelfBack;
        shelf = above >= 0.0f ? shelf : kPlankLight;
        shelf = (shelf + -1.0f) * region;
        // The lamp.
        const float ld[3] = { hit[0] - (h[2] * kLamp[1] + kLamp[0]), hit[1] - kLamp[2], hit[2] - kLamp[3] };
        const float falloff = (1.0f / ((ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2]) + 1.0f / kLampK)) * (kLampGain / kLampK) + kLampBase;
        // The detail fades with distance and with the pixel's footprint.
        const float e[3] = { P[0] - k.cam[0], P[1] - k.cam[1], P[2] - k.cam[2] };
        const float fade = Clamp01((e[0] * e[0] + e[1] * e[1] + e[2] * e[2]) * kFadeB + kFadeA) * k.c204[3] *
                           Clamp01(k.footprint2 * kFootprintB + kFootprintA);
        shelf = shelf * fade + 1.0f;
        const float clothFar = curtains * kClothFar;
        cloth = (cloth - clothFar) * fade + clothFar;
        fold = (fold - kFoldMean) * fade + kFoldMean;
        // The light.
        const float lum = 0.2126f * r12[0] + 0.7152f * r12[1] + 0.0722f * r12[2];
        const float roomLight = (h[2] * kCellLight[0] + kCellLight[1]) * shade * shelf;
        const float day = bright * k.c198[3] * (lit * (1.0f - kDarkDay) + kDarkDay);
        const float lamp = lum * lit;
        float room[3], cloth3[3];
        for (int i = 0; i < 3; ++i)
        {
            room[i] = ((tint[i] * k.c203[i]) * (lamp * falloff) + tint[i] * day) * roomLight;
            cloth3[i] = (k.c203[i] * lamp + day * kCurtainDay) * k.c205[i] * fold * kCurtainGain;
            room[i] = Lerp(cloth, cloth3[i], room[i]);
        }
        // Dark rooms behind the client's lit windows at night hide most of its glow (from the whole window: the
        // shader takes it from the rest of the window too).
        const float hide = (1.0f - lit) * k.c205[3];
        for (int i = 0; i < 3; ++i) out[i] = Lerp(k.share, room[i], outside[i] * -hide + outside[i]);
        if (sample)
        {
            sample->hide = hide;
            sample->layout = shelves > 0 ? kLayoutShelves : curtains > 0 ? kLayoutCurtains : divider > 0 ? kLayoutDivider : kLayoutPlain;
            sample->lit = lit > 0; sample->door = door > 0; sample->wall = wall > 0;
            sample->cloth = cloth; sample->fold = fold; sample->shelf = shelf; sample->falloff = falloff; sample->fade = fade;
            sample->shade = shade; sample->building[0] = bh[0]; sample->building[1] = bh[1];
            for (int i = 0; i < 3; ++i) sample->hit[i] = hit[i];
        }
    }
}
