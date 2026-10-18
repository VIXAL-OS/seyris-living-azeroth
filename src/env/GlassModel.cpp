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
        // The room behind the glass keeps the day's light until the client lights its windows (dusk: the sun's day is
        // over at 18:00, the windows light from 20:30): no black rooms between.
        l.rooms = Clamp01(1.0f - in.night);
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
        static const char* const names[kClassCount] = { "other", "house", "farm", "inn", "shop", "temple", "keep", "tower", "city", "ship",
                                                        "forge", "commons", "quarter", "cavern" };
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
        m_grid.clear(); m_groups.clear(); m_defaults.clear(); m_byId.clear();
        m_placements = 0;
        for (uint32_t i = 0; i < m_rows.size(); ++i)
            if (m_rows[i].id) m_byId.emplace(m_rows[i].id, i);
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

    const Building* BuildingIndex::ById(uint32_t id) const
    {
        auto it = m_byId.find(id);
        return it == m_byId.end() ? nullptr : &m_rows[it->second];
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

    // --- lit or dark, shared by the shader and the window lights --------------------------------------------------
    namespace
    {
        // The point about the building's origin, then about its anchor (the nearest kAnchor-yd step from the origin:
        // within kAnchor / 2 on each axis), and the anchor's offset to the cell (whole numbers), as the shader.
        void Anchored(const float P[3], const float origin[3], float R[3], float offset[2])
        {
            using namespace room;
            float k[3];
            for (int i = 0; i < 3; ++i)
            {
                R[i] = P[i] - origin[i];
                const float q = R[i] * (1.0f / kAnchor) + 0.5f;
                k[i] = q - (q - std::floor(q));
                R[i] = k[i] * -kAnchor + R[i];
            }
            offset[0] = k[0] * kAnchorKeyX[0] + k[1] * kAnchorKeyX[1] + k[2] * kAnchorKeyX[2];
            offset[1] = k[0] * kAnchorKeyY[0] + k[1] * kAnchorKeyY[1] + k[2] * kAnchorKeyY[2];
        }
    }

    void RoomCell(const float P[3], const float origin[3], const float N[3], const float inv[2], float cell[2])
    {
        // As the shader: along (-N.y, N.x, 0) (x for a skylight), normalised; up N x along; the point about the
        // origin's anchor; the cell, plus the anchor's offset.
        float a[3] = { -N[1], N[0], 0.0f };
        if (a[0] * a[0] + a[1] * a[1] - 0.0001f < 0.0f) { a[0] = 1; a[1] = 0; a[2] = 0; }
        const float al = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        for (float& v : a) v /= al;
        const float u[3] = { N[1] * a[2] - N[2] * a[1], N[2] * a[0] - N[0] * a[2], N[0] * a[1] - N[1] * a[0] };
        float R[3], offset[2];
        Anchored(P, origin, R, offset);
        const float pos[2] = { Dot(R, a) * inv[0], Dot(R, u) * inv[1] };
        for (int k = 0; k < 2; ++k) cell[k] = pos[k] - (pos[k] - std::floor(pos[k])) + offset[k];
    }

    namespace
    {
        // Mod 289 of a whole number below 2^22: floor((v + 0.5) / 289) is never within the product's rounding of a
        // whole number (it's at least 0.5 / 289 away), so every step is exact.
        float LitMod(float v)
        {
            const float q = (v + 0.5f) * room::kLitInv;
            const float f = q - std::floor(q);
            return (q - f) * -room::kLitMod + v;
        }
        // The permutation (34 v + 1) v mod 289 of 0..288 (webgl-noise's), as the shader: 34 v, then x v + v.
        float LitPermute(float v)
        {
            const float t = v * room::kLitPoly;
            return LitMod(t * v + v);
        }
    }

    float LitHash(float cellX, float cellY, float seedX, float seedY)
    {
        const float x = LitMod(cellX + seedX), y = LitMod(cellY + seedY);
        const float s = LitMod(LitPermute(x) + y);
        return (LitPermute(s) + 0.5f) * room::kLitInv;
    }

    uint32_t LitHashIndex(int64_t x, int64_t y)
    {
        auto mod = [](int64_t v) { const int64_t r = v % 289; return r < 0 ? r + 289 : r; };
        auto permute = [&](int64_t v) { return mod((34 * v + 1) * v); };
        return static_cast<uint32_t>(permute(mod(permute(mod(x)) + mod(y))));
    }

    float RoomLit(float hash, const DarkBand& b)
    {
        // As the shader: add r20.w; mul; add_sat; cmp (at or below lo: dark); add; cmp (above hi: lit).
        const float d = hash - b.lo;
        float v = d * b.slope;
        v = Clamp01(v + b.base);
        if (-d >= 0.0f) v = 0.0f;
        const float e = hash - b.hi;
        return -e >= 0.0f ? v : 1.0f;
    }

    DarkBand Band(float shareNow, float sharePast)
    {
        DarkBand b;
        if (!std::isfinite(shareNow)) shareNow = 0.0f;
        if (!std::isfinite(sharePast)) sharePast = shareNow;
        b.lo = std::min(shareNow, sharePast);
        b.hi = std::max(shareNow, sharePast);
        b.base = 0.0f;
        b.slope = 1.0f / std::max(b.hi - b.lo, room::kBandMin);
        return b;
    }

    DarkBand Crossfade(float shareFrom, float shareTo, float progress)
    {
        DarkBand b;
        if (!std::isfinite(shareTo)) shareTo = 0.0f;
        if (!std::isfinite(shareFrom)) shareFrom = shareTo;
        const float p = Clamp01(progress);
        b.lo = std::min(shareFrom, shareTo);
        b.hi = std::max(shareFrom, shareTo);
        b.base = shareTo > shareFrom ? 1.0f - p : p;   // going dark: lit fading out; lit again: fading in
        b.slope = 0.0f;
        return b;
    }

    float DarkShare(const RoomStyle& style, uint32_t flags, const RoomToggles& t, float night, float hour)
    {
        const float v = t.variety ? std::max(0.0f, t.varietyStrength) : 0.0f;
        const bool empty = t.variety && (flags & kBuildingEmpty);
        const bool darkPlace = (flags & kBuildingDarkPlace) != 0;
        // By day the style's few; at night by the hour (inns late, shops early) -- the night's schedule is the rooms'
        // variety too: with it off every room is the same lit one -- every room in an empty building, and at night in
        // a dark place.
        const float dayDark = empty ? 1.0f : (t.variety ? Clamp01(style.darkShare * v) : 0.0f);
        float nightDark = t.variety && t.lightsOut
                              ? Clamp01(LightsOutShare(hour, style.lateHours, style.nightCap) * std::max(0.0f, t.lightsOutStrength))
                              : 0.0f;
        if (darkPlace || empty) nightDark = 1.0f;
        return dayDark + (nightDark - dayDark) * Clamp01(night);
    }

    void RoomClock::Restart(double seconds)
    {
        m_first = 0; m_count = 1;
        m_ring[0] = { seconds, static_cast<double>(m_hour), m_night };
    }

    void RoomClock::Advance(double seconds, float hour, float night)
    {
        if (!std::isfinite(hour)) hour = 12.0f;
        hour = std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f);
        night = Clamp01(night);
        const Sample* last = m_count ? &m_ring[(m_first + m_count - 1) % kSamples] : nullptr;
        const double dt = last ? seconds - last->t : 0.0;
        if (!last || !(dt >= 0.0) || dt > kSnapSeconds)
        {
            m_hour = hour; m_night = night;
            m_fading = false; m_progress = 1.0f;
            Restart(seconds);
            return;
        }
        if (dt == 0.0) return;
        // How far the moment moved since the last step, the shorter way round; faster than kJumpRate x the running
        // clock is a jump (a hair over for the float hour's own steps).
        double dh = static_cast<double>(hour) - m_hour;
        dh -= 24.0 * std::floor((dh + 12.0) / 24.0);
        const float dn = night - m_night;
        const double most = kJumpRate * dt / 3600.0;
        if (std::fabs(dh) > most + 1e-5 || std::fabs(dn) > most + 1e-4)
        {
            // A jump: cross-fade from the time shown (past half way through a cross-fade, from where it was going;
            // short of that, from where it came: a slider dragged keeps the rooms as they were until it settles).
            if (!m_fading || m_progress >= 0.5f) { m_fromHour = m_hour; m_fromNight = m_night; }
            m_fading = true;
            m_progress = 0.0f;
        }
        else if (m_fading)
        {
            m_progress = static_cast<float>(m_progress + dt / kFadeSeconds);
            if (m_progress >= 1.0f) { m_fading = false; m_progress = 1.0f; }
        }
        const double unwrapped = last->hour + dh;
        m_hour = hour; m_night = night;
        if (m_fading) { Restart(seconds); return; }   // running again from rest once the cross-fade is done
        if (m_count == kSamples) { m_first = (m_first + 1) % kSamples; --m_count; }
        m_ring[(m_first + m_count) % kSamples] = { seconds, unwrapped, night };
        ++m_count;
        // Keep one sample at or before kFadeSeconds ago (for Past), none older.
        while (m_count > 2 && m_ring[(m_first + 1) % kSamples].t <= seconds - kFadeSeconds) { m_first = (m_first + 1) % kSamples; --m_count; }
    }

    void RoomClock::Past(double& hour, float& night) const
    {
        if (!m_count) { hour = 12.0; night = 0.0f; return; }
        const Sample& newest = m_ring[(m_first + m_count - 1) % kSamples];
        const double at = newest.t - kFadeSeconds;
        const Sample& oldest = m_ring[m_first];
        if (m_count == 1 || at <= oldest.t) { hour = oldest.hour; night = oldest.night; return; }
        for (int i = 0; i + 1 < m_count; ++i)
        {
            const Sample& a = m_ring[(m_first + i) % kSamples];
            const Sample& b = m_ring[(m_first + i + 1) % kSamples];
            if (at <= b.t)
            {
                const double f = b.t > a.t ? (at - a.t) / (b.t - a.t) : 1.0;
                hour = a.hour + (b.hour - a.hour) * f;
                night = static_cast<float>(a.night + (b.night - a.night) * f);
                return;
            }
        }
        hour = newest.hour; night = newest.night;
    }

    namespace
    {
        float Wrap24(double h) { return static_cast<float>(h - 24.0 * std::floor(h / 24.0)); }
    }

    float RoomClock::HourPast() const { double h; float n; Past(h, n); return Wrap24(h); }
    float RoomClock::NightPast() const { double h; float n; Past(h, n); return n; }

    DarkBand BandOf(const RoomClock& clock, const RoomStyle& style, uint32_t flags, const RoomToggles& t)
    {
        const float now = DarkShare(style, flags, t, clock.Night(), clock.Hour());
        if (clock.Fading()) return Crossfade(DarkShare(style, flags, t, clock.FromNight(), clock.FromHour()), now, clock.Progress());
        return Band(now, DarkShare(style, flags, t, clock.NightPast(), clock.HourPast()));
    }

    float LitShare(const LightCell* cells, size_t count)
    {
        float rooms = 0.0f, lit = 0.0f;
        for (size_t i = 0; i < count; ++i)
        {
            const float s = std::max(0.0f, cells[i].share);
            rooms += s;
            lit += s * RoomLit(cells[i].hash, cells[i].band) * (1.0f - Clamp01(cells[i].dip));
        }
        return Clamp01(1.0f - std::min(1.0f, rooms) + lit);
    }

    float HearthDip(float hash, float clock, float amplitude)
    {
        // As the shader: t = hash x spread + clock (mad); x = t x rate; q = 2 frac(x) - 1 (mad); q (1 - |q|);
        // f = sum w q (1 - |q|) + 0.5 (dp3, add); x the amplitude.
        using namespace hearth;
        const float t = hash * kSpread + clock;
        float f = 0.0f;
        for (int k = 0; k < 3; ++k)
        {
            const float x = t * kRate[k];
            const float q = (x - std::floor(x)) * 2.0f + -1.0f;
            f += q * (1.0f - std::fabs(q)) * kWeight[k];
        }
        return (f + 0.5f) * amplitude;
    }

    void HearthClock::Advance(double seconds, float speed)
    {
        // A step of at most a quarter second (a hitch, a loading screen: the fire doesn't race to catch up).
        const double dt = m_have ? std::min(0.25, std::max(0.0, seconds - m_last)) : 0.0;
        m_last = seconds;
        m_have = true;
        const double s = std::isfinite(speed) ? std::max(0.0f, speed) : 1.0;
        m_clock += dt * s;
        m_clock -= hearth::kWrap * std::floor(m_clock / hearth::kWrap);
    }

    void RoomLook(float day, float out[8])
    {
        using namespace room;
        const float d = Clamp01(day);
        out[0] = kWallSoftDay * d;
        out[1] = kLayoutSoftDay * d;
        out[2] = kPixelSoft;
        out[3] = 1.0f + (kThingContrastDay - 1.0f) * d;
        const float n = kDetailNear[0] + (kDetailNear[1] - kDetailNear[0]) * d, f = kDetailFar[0] + (kDetailFar[1] - kDetailFar[0]) * d;
        const float span = std::max(1.0f, f * f - n * n);
        out[4] = f * f / span;
        out[5] = -1.0f / span;
        out[6] = kEdgeEase;
        out[7] = 1.0f - d;
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
        // The ray (cell units), where it enters (cells, about the building's origin), the cell, the walls it heads for.
        const float d[3] = { -Dot(V, a) * k.c198[0], -Dot(V, u) * k.c198[1], std::max(cosine, 0.0001f) * k.c198[2] };
        float R[3], offset[2];
        Anchored(P, k.c213, R, offset);
        const float pos[2] = { Dot(R, a) * k.c198[0], Dot(R, u) * k.c198[1] };
        const float f[2] = { Frac(pos[0]), Frac(pos[1]) };
        const float cell[2] = { pos[0] - f[0] + offset[0], pos[1] - f[1] + offset[1] };
        // (the way to the wall over the step, both as sizes: a step of 0 or -0 never divides)
        const float tx = std::fabs(Ge(d[0]) - f[0]) * (1.0f / std::max(std::fabs(d[0]), 0.0001f));
        const float ty = std::fabs(Ge(d[1]) - f[1]) * (1.0f / std::max(std::fabs(d[1]), 0.0001f)), tz = 1.0f / d[2];
        const float t = std::min(std::min(tx, ty), tz);
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
        // (lit or dark: the exact hash against the dark band, part lit while it fades)
        const float litHash = LitHash(cell[0], cell[1], k.c212[0], k.c212[1]);
        DarkBand band;
        band.lo = k.c203[3]; band.hi = k.c200w; band.base = k.c213[3]; band.slope = k.c212[3];
        const float lit = RoomLit(litHash, band);
        // (the flicker's dip: the hearth clock c201.y, its amplitude c201.z)
        const float dip = HearthDip(litHash, k.c201[1], k.c201[2]);
        // The soft widths at the point the ray meets (cells): the walls' and the layouts' plus the pixel's footprint
        // there (on the glass x (1 + the ray's depth over the eye's distance)), never below 1e-4.
        const float e[3] = { P[0] - k.cam[0], P[1] - k.cam[1], P[2] - k.cam[2] };
        const float eye2 = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
        const float foot = 1.0f / (1.0f / std::sqrt(k.footprint2 + 1e-12f));
        const float px = (t * (1.0f / std::sqrt(eye2)) + 1.0f) * foot * k.c198[0];
        const float invWall = 1.0f / std::max(px * k.c218[2] + k.c218[0], 0.0001f);
        const float invLayout = 1.0f / std::max(px * k.c218[2] + k.c218[1], 0.0001f);
        // Near the cell's edge on the glass, or its anchor's (where the cells are cut: how far along the glass the
        // nearest anchor plane crossing it is, each axis's over sqrt(|1 - N_i^2|)), the room eases to its mean (by day;
        // 1 at night).
        float anchorEdge[3];
        for (int i = 0; i < 3; ++i)
            anchorEdge[i] = (-std::fabs(R[i]) + 0.5f * kAnchor) * (1.0f / std::sqrt(std::fabs(1.0f - N[i] * N[i]) + 1e-12f));
        const float toAnchor = std::min(std::min(anchorEdge[0], anchorEdge[1]), anchorEdge[2]) * k.c198[0];
        const float toEdge = std::min(std::min(std::min(1.0f - f[0], f[0]), std::min(1.0f - f[1], f[1])), toAnchor);
        const float edge = Clamp01(toEdge * (1.0f / (px * kEdgePixels + k.c219[2])) + k.c219[3]);
        // The dividing wall (where the ray would go deeper, soft) with its doorway (its edge soft): the point and the way
        // to it the wall's where it's in the way.
        const float td = kDividerDepth * tz;
        const float hd[2] = { d[0] * td + f[0], d[1] * td + f[1] };
        const float ahead = Clamp01((hit[2] - kDividerDepth) * invLayout + 0.5f) * divider;
        float door = std::min(-std::fabs(hd[0] - (h[3] * kDoorSpan + kDoorBase)) + kDoorHalfWidth, -hd[1] + kDoorHeight);
        door = Clamp01(door * invLayout + 0.5f) * ahead;
        const float wall = ahead - door;
        const float depth = Lerp(wall, td, t);
        const float dividerPoint[3] = { hd[0], hd[1], kDividerDepth };
        for (int i = 0; i < 3; ++i) hit[i] = Lerp(wall, dividerPoint[i], hit[i]);
        // The walls' shade, soft: how near the point is to each wall (across the nearer side, up the floor and the
        // ceiling), smoothstepped over the walls' width, each wall's shade toward 1 away from it, x each other; the
        // dividing wall a little darker; farther darker; beyond the doorway dimmer.
        const float near[3] = { std::min(1.0f - hit[0], hit[0]), hit[1], 1.0f - hit[1] };
        float g[3];
        for (int i = 0; i < 3; ++i)
        {
            const float w = Clamp01(near[i] * invWall);
            const float smooth = (w * w) * (3.0f - (w + w));
            g[i] = Lerp(smooth, 1.0f, kFace[i]);
        }
        float shade = g[0] * g[1] * g[2];
        shade = shade * (1.0f / ((depth * k.c86[1] + depth * kFar) + 1.0f));
        shade = shade * ((wall * kDividerShade - wall) + 1.0f);
        shade = shade * (1.0f - door * kDoorDim);
        // Curtains (their edges soft over the layouts' width, at least 1 / kClothEdge of the cell).
        const float hc = d[0] * (kCurtainDepth * tz) + f[0];
        const float gapHalf = Frac(h[3] * kGapHash) * kGapHalfSpan + kGapMinHalf;
        float cloth = std::fabs(hc - (h[3] * kGapSpan + kGapBase)) + -gapHalf;
        cloth = Clamp01(cloth * std::min(invLayout, kClothEdge)) * curtains;
        float fold = std::fabs(Frac(hc * kFolds) - 0.5f) * kFoldAmp + kFoldBase;
        // Shelves on the back wall (every edge soft over the layouts' width).
        float region = std::min(-std::fabs(hit[0] - kShelfCentre[0]) + kShelfHalf[0], -std::fabs(hit[1] - kShelfCentre[1]) + kShelfHalf[1]);
        region = Clamp01(region * invLayout + 0.5f) * shelves;
        const float sx = hit[0] * kSlots, sy = hit[1] * kShelves;
        const float fx = Frac(sx), fy = Frac(sy);
        float q = (sy - fy) * kShelfHashShelf + (sx - fx);
        q = h[3] * kShelfHashRoom + q;
        q = Frac(q * kHashK[0]);
        q = q * (q + kHashAdd);
        q = Frac(q * q + q * q);
        const float above = fy + -kPlank;
        const float thing = std::min(q * kThingMax + -above, -std::fabs(fx - 0.5f) + kThingGap);
        const float slotSoft = invLayout * kSlotSoft;
        // (the plank soft about its middle, so one shelf's row meets the next without a step; a thing soft inward from
        // its outline, so one place meets the next without one)
        const float plankAt = Frac(fy + kPlankMid) + -0.5f;
        const float inPlank = Clamp01((-std::fabs(plankAt) + kPlankHalf) * slotSoft + 0.5f);
        const float thingIn = Clamp01(thing * slotSoft);
        float shelf = Lerp(thingIn, Frac(q * kThingHash) * kThingSpan + kThingBase, kShelfBack);
        shelf = Lerp(inPlank, kPlankLight, shelf);
        shelf = (shelf + -1.0f) * region;
        // By day the sun falling in: a patch on the floor by the window (across the middle, not deep, low; soft), x its
        // cosine to the face, x c86.x (none at night).
        float patch = Clamp01(Dot(N, k.sun));
        patch = patch * Clamp01((-std::fabs(hit[0] + -0.5f) + kPatchAcross) * (1.0f / kPatchSoft));
        patch = patch * Clamp01((-hit[2] + kPatchDeep) * (1.0f / kPatchDeepSoft));
        patch = patch * Clamp01(-(hit[1] * (1.0f / kPatchHigh)) + 1.0f);
        patch = patch * k.c86[0];
        // The lamp.
        const float ld[3] = { hit[0] - (h[2] * kLamp[1] + kLamp[0]), hit[1] - kLamp[2], hit[2] - kLamp[3] };
        const float falloff = (1.0f / ((ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2]) + 1.0f / kLampK)) * (kLampGain / kLampK) + kLampBase;
        // The detail fades with distance (c219.xy), with the pixel's footprint and near the cell's edge; the things at
        // c218.w of their contrast.
        const float fade = Clamp01(eye2 * k.c219[1] + k.c219[0]) * k.c204[3] * Clamp01(k.footprint2 * kFootprintB + kFootprintA) * edge;
        shelf = shelf * (fade * k.c218[3]) + 1.0f;
        const float clothFar = curtains * kClothFar * edge;
        cloth = (cloth - clothFar) * fade + clothFar;
        fold = (fold - kFoldMean) * fade + kFoldMean;
        // The light: by day its tint x the client's colour's luminance (the room seen through the glass; c198.w makes it
        // about kDayRoom of the glass's brightness; the curtains in front too), the sun's patch on its floor; near the
        // cell's edge the room's light and its dark rooms' ease to their means.
        const float lum = 0.2126f * r12[0] + 0.7152f * r12[1] + 0.0722f * r12[2];
        float roomLight = (h[2] * kCellLight[0] + kCellLight[1]) * shade * shelf;
        roomLight = roomLight * ((-hit[1] + 0.5f) * k.c86[2]) + roomLight;   // by day lighter low, darker high
        roomLight = roomLight + patch;
        roomLight = Lerp(edge, roomLight, kEdgeMean);
        const float darkDay = Lerp(edge, lit * (1.0f - kDarkDay) + kDarkDay, kDarkDayMean);
        const float day = bright * k.c198[3] * darkDay;
        const float dayLum = day * lum;
        float lamp = lum * lit;
        lamp = lamp * -dip + lamp;
        float room[3], cloth3[3];
        for (int i = 0; i < 3; ++i)
        {
            room[i] = ((tint[i] * k.c203[i]) * (lamp * falloff) + tint[i] * dayLum) * roomLight;
            cloth3[i] = (k.c203[i] * lamp + dayLum * kCurtainDay) * k.c205[i] * fold * kCurtainGain;
            room[i] = Lerp(cloth, cloth3[i], room[i]);
        }
        // Dark rooms behind the client's lit windows at night hide most of its glow (from the whole window: the
        // shader takes it from the rest of the window too); lit ones the flicker's dip.
        float hide = (1.0f - lit) * k.c205[3];
        hide = lit * dip + hide;
        for (int i = 0; i < 3; ++i) out[i] = Lerp(k.share, room[i], outside[i] * -hide + outside[i]);
        if (sample)
        {
            sample->hide = hide; sample->dip = dip;
            sample->layout = shelves > 0 ? kLayoutShelves : curtains > 0 ? kLayoutCurtains : divider > 0 ? kLayoutDivider : kLayoutPlain;
            sample->lit = lit >= 0.5f; sample->litValue = lit; sample->litHash = litHash;
            sample->cell[0] = cell[0]; sample->cell[1] = cell[1];
            sample->door = door > 0; sample->wall = wall > 0;
            sample->cloth = cloth; sample->fold = fold; sample->shelf = shelf; sample->falloff = falloff; sample->fade = fade;
            sample->shade = shade; sample->building[0] = bh[0]; sample->building[1] = bh[1];
            sample->edge = edge; sample->light = roomLight;
            for (int i = 0; i < 3; ++i) { sample->hit[i] = hit[i]; sample->room[i] = room[i]; }
        }
    }

    float SkyAlong(const float N[3], const float V[3], const float reflected[3], float out[3])
    {
        const float cosine = Clamp01(Dot(N, V));
        const float height = Clamp01((cosine + cosine) * N[2] + -V[2]);
        for (int i = 0; i < 3; ++i) out[i] = ((room::kZenith[i] + -1.0f) * height + 1.0f) * reflected[i];
        return height;
    }

    void DayCool(const float outside[3], const float sky[3], float height, float cosine, const float c85[4], float out[3])
    {
        using namespace room;
        const float skyLum = std::max(0.2126f * sky[0] + 0.7152f * sky[1] + 0.0722f * sky[2], 0.05f);
        const float inv = 1.0f / skyLum;
        const float lum = 0.2126f * outside[0] + 0.7152f * outside[1] + 0.0722f * outside[2];
        float amount = height * c85[1];
        amount = (1.0f + -cosine) * c85[2] + amount;
        amount = Clamp01(amount * c85[0] + c85[0]);
        for (int i = 0; i < 3; ++i)
        {
            float hue = std::min(std::max(sky[i] * inv, kSkyHue[0]), kSkyHue[1]);
            hue = ((hue + -1.0f) * c85[3] + 1.0f) * lum;
            out[i] = Lerp(amount, hue, outside[i]);
        }
    }
}
