#include "GlassModel.hpp"
#include "GlassRooms.hpp"

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

    bool LeadMask(const float* rgb, uint32_t w, uint32_t h, const Material& m, const LeadRule& rule, std::vector<uint8_t>& out)
    {
        out.clear();
        if (!rgb || !w || !h || rule.rects.empty()) return false;
        const size_t n = static_cast<size_t>(w) * h;
        std::vector<float> lum(n);
        std::vector<uint8_t> inside(n, 0);
        for (size_t i = 0; i < n; ++i) lum[i] = 0.2126f * rgb[3 * i] + 0.7152f * rgb[3 * i + 1] + 0.0722f * rgb[3 * i + 2];
        auto texel = [](float u, uint32_t size) { return static_cast<long>(std::lround(std::clamp(u, 0.0f, 1.0f) * static_cast<float>(size))); };
        for (const LeadRect& r : rule.rects)
            for (long y = texel(r.v0, h); y < texel(r.v1, h); ++y)
                for (long x = texel(r.u0, w); x < texel(r.u1, w); ++x) inside[static_cast<size_t>(y) * w + x] = 1;
        // the lit glass's median luminance in them (else all of theirs); over the whole texture, its texels lit or coloured
        const bool whole = rule.Whole();
        std::vector<uint8_t> coloured(whole ? n : 0, 0);
        std::vector<float> lit, all;
        for (size_t i = 0; i < n; ++i)
        {
            if (!inside[i]) continue;
            all.push_back(lum[i]);
            const float* c = rgb + 3 * i;
            const float g = m.mask[0] * c[0] + m.mask[1] * c[1] + m.mask[2] * c[2] + m.mask[3];
            if (g > 0.5f) lit.push_back(lum[i]);
            if (whole)
            {
                const float hi = std::max(std::max(c[0], c[1]), c[2]), lo = std::min(std::min(c[0], c[1]), c[2]);
                coloured[i] = g > 0.5f || (hi > 0.0f && 1.0f - lo / hi >= kLeadColour);
            }
        }
        if (all.empty()) return false;
        std::vector<float>& from = lit.empty() ? all : lit;
        std::nth_element(from.begin(), from.begin() + from.size() / 2, from.end());
        const float ref = from[from.size() / 2];
        // a thin dark line: the luminance closed (the largest over a square, then the smallest of those; the edges
        // clamped) over kLeadClose texels, minus it
        const long half = kLeadClose / 2;
        auto pass = [&](const std::vector<float>& src, std::vector<float>& dst, bool largest, bool across) {
            dst.resize(n);
            for (long y = 0; y < static_cast<long>(h); ++y)
                for (long x = 0; x < static_cast<long>(w); ++x)
                {
                    float v = largest ? -1e30f : 1e30f;
                    for (long d = -half; d <= half; ++d)
                    {
                        const long xx = across ? std::clamp(x + d, 0L, static_cast<long>(w) - 1) : x;
                        const long yy = across ? y : std::clamp(y + d, 0L, static_cast<long>(h) - 1);
                        const float s = src[static_cast<size_t>(yy) * w + xx];
                        v = largest ? std::max(v, s) : std::min(v, s);
                    }
                    dst[static_cast<size_t>(y) * w + x] = v;
                }
        };
        std::vector<float> a, b;
        pass(lum, a, true, true);
        pass(a, b, true, false);
        pass(b, a, false, true);
        pass(a, b, false, false);
        out.assign(n, 0);
        for (size_t i = 0; i < n; ++i)
            if (inside[i] && lum[i] >= rule.dark * ref && b[i] - lum[i] < rule.line * ref && (!whole || coloured[i])) out[i] = 255;
        return true;
    }

    void LeadMip(const std::vector<uint8_t>& level, uint32_t& w, uint32_t& h, std::vector<uint8_t>& out)
    {
        const uint32_t nw = std::max(w / 2, 1u), nh = std::max(h / 2, 1u);
        out.assign(static_cast<size_t>(nw) * nh, 0);
        for (uint32_t y = 0; y < nh; ++y)
            for (uint32_t x = 0; x < nw; ++x)
            {
                unsigned sum = 0;
                for (uint32_t dy = 0; dy < 2; ++dy)
                    for (uint32_t dx = 0; dx < 2; ++dx)
                        sum += level[static_cast<size_t>(std::min(2 * y + dy, h - 1)) * w + std::min(2 * x + dx, w - 1)];
                out[static_cast<size_t>(y) * nw + x] = static_cast<uint8_t>((sum + 2) / 4);
            }
        w = nw;
        h = nh;
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
        MoonHue(in.ambient, l.moon);
        return l;
    }

    void MoonHue(const float clientLight[3], float out[3])
    {
        auto lum = [](const float c[3]) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; };
        float hue[3];
        const float ml = lum(room::kMoonHue);
        for (int k = 0; k < 3; ++k) hue[k] = room::kMoonHue[k] / ml;
        const float cl = clientLight ? lum(clientLight) : 0.0f;
        if (cl > 1e-3f && std::isfinite(cl))
            for (int k = 0; k < 3; ++k)
            {
                const float c = std::min(room::kSkyHue[1], std::max(room::kSkyHue[0], std::max(0.0f, clientLight[k]) / cl));
                hue[k] += (c - hue[k]) * room::kMoonClient;
            }
        const float l = std::max(1e-3f, lum(hue));
        for (int k = 0; k < 3; ++k) out[k] = hue[k] / l;
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

    // --- titan glass lit from behind ----------------------------------------------------------------------------------
    void BacklitDirection(const Pane& p, float out[3])
    {
        // out + tan(tilt) z: the way back along a ray that falls `tilt` below the inward normal (a skylight's: up).
        const float lift = std::tan(backlit::kTiltDegrees * 3.14159265f / 180.0f);
        float d[3] = { p.out[0], p.out[1], p.out[2] + lift };
        const float n = std::sqrt(Dot(d, d));
        bool ok = std::isfinite(n) && n > 1e-6f;
        if (ok)
        {
            for (float& v : d) v /= n;
            ok = Dot(d, p.out) >= backlit::kMinOut;
        }
        for (int k = 0; k < 3; ++k) out[k] = ok ? d[k] : p.out[k];
    }

    float BacklitWave(double seconds, float period)
    {
        if (!std::isfinite(seconds) || !std::isfinite(period) || !(period > 0.0f)) return 0.0f;
        const double x = seconds / static_cast<double>(period);
        const double f = x - std::floor(x);
        return static_cast<float>(0.5 - 0.5 * std::cos(6.283185307179586 * f));
    }

    void BacklitGlow(const Material& m, double seconds, float strength, float pulseScale, float out[3])
    {
        const bool ok = m.backlit > 0.0f && std::isfinite(m.backlit) && std::isfinite(strength) && std::isfinite(pulseScale) &&
                        std::isfinite(m.pulse);
        const float base = ok ? m.backlit * std::max(0.0f, strength) : 0.0f;
        const float wave = ok ? BacklitWave(seconds, m.pulseSeconds) * std::max(0.0f, m.pulse) * std::max(0.0f, pulseScale) : 0.0f;
        for (int k = 0; k < 3; ++k)
        {
            const float tint = std::isfinite(m.pulseColour[k]) ? std::max(0.0f, m.pulseColour[k]) : 1.0f;
            const float v = base * (1.0f + wave * tint);
            out[k] = std::isfinite(v) ? std::max(0.0f, v) : 0.0f;
        }
    }

    void MeanDirection(const float* dirs, const float* weights, size_t n, const float fallback[3], float out[3])
    {
        double sum[3] = { 0, 0, 0 }, total = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const float w = std::isfinite(weights[i]) ? std::max(0.0f, weights[i]) : 0.0f;
            if (!(w > 0.0f)) continue;
            bool finite = true;
            for (int k = 0; k < 3; ++k) finite = finite && std::isfinite(dirs[3 * i + k]);
            if (!finite) continue;
            for (int k = 0; k < 3; ++k) sum[k] += static_cast<double>(w) * dirs[3 * i + k];
            total += w;
        }
        const double len = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
        if (!(total > 0.0) || !(len > 0.2 * total))
        {
            for (int k = 0; k < 3; ++k) out[k] = fallback[k];
            return;
        }
        for (int k = 0; k < 3; ++k) out[k] = static_cast<float>(sum[k] / len);
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

    void RoomLook(float out[4])
    {
        // saturate(d^2 x slope + base): 1 within kDetailNear, 0 from kDetailFar (over the distance squared)
        const float n = rooms::kDetailNear, f = rooms::kDetailFar;
        const float span = std::max(1.0f, f * f - n * n);
        out[0] = f * f / span;
        out[1] = -1.0f / span;
        out[2] = 0.0f;
        out[3] = 0.0f;
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
