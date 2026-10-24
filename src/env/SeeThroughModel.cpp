#include "SeeThroughModel.hpp"

#include "GlassModel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace wxl_livingazeroth::seethrough
{
    namespace
    {
        float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

        bool Finite3(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

        // Row vectors: out = (v, w) M, M row-major (the client's C44Matrix).
        void Transform(const float v[3], float w, const float m[16], float out[4])
        {
            for (int j = 0; j < 4; ++j) out[j] = v[0] * m[j] + v[1] * m[4 + j] + v[2] * m[8 + j] + w * m[12 + j];
        }

        struct Clip { float v[4]; };

        // Sutherland-Hodgman against a * x + b * y + c * w >= eps (clip space), `in` -> `out`.
        void ClipAgainst(const std::vector<Clip>& in, float a, float b, float c, float eps, std::vector<Clip>& out)
        {
            out.clear();
            const size_t n = in.size();
            for (size_t i = 0; i < n; ++i)
            {
                const Clip& p = in[i];
                const Clip& q = in[(i + 1) % n];
                const float dp = a * p.v[0] + b * p.v[1] + c * p.v[3] - eps;
                const float dq = a * q.v[0] + b * q.v[1] + c * q.v[3] - eps;
                if (dp >= 0.0f) out.push_back(p);
                if ((dp >= 0.0f) != (dq >= 0.0f))
                {
                    const float t = dp / (dp - dq);
                    Clip r;
                    for (int k = 0; k < 4; ++k) r.v[k] = p.v[k] + (q.v[k] - p.v[k]) * t;
                    out.push_back(r);
                }
            }
        }
    }

    int InteriorBehind(const float center[3], const float normal[3], const GroupInfo* groups, size_t count)
    {
        if (!groups || !Finite3(center) || !Finite3(normal)) return -1;
        for (const float depth : kProbes)
        {
            const float p[3] = { center[0] - normal[0] * depth, center[1] - normal[1] * depth, center[2] - normal[2] * depth };
            int best = -1;
            float volume = 0.0f;
            for (size_t i = 0; i < count; ++i)
            {
                const GroupInfo& g = groups[i];
                if (g.flags & client::kSkippedGroups) continue;
                bool inside = true;
                for (int k = 0; k < 3 && inside; ++k)
                    inside = p[k] >= g.lo[k] - kBoxMargin && p[k] <= g.hi[k] + kBoxMargin;
                if (!inside) continue;
                const float v = (g.hi[0] - g.lo[0]) * (g.hi[1] - g.lo[1]) * (g.hi[2] - g.lo[2]);
                if (best < 0 || v < volume) { best = static_cast<int>(i); volume = v; }
            }
            if (best >= 0) return best;
        }
        return -1;
    }

    const SeeWindow* RootWindows::Group(uint32_t group, uint32_t& count) const
    {
        count = 0;
        if (group >= byGroup.size() || !byGroup[group].second) return nullptr;
        count = byGroup[group].second;
        return windows.data() + byGroup[group].first;
    }

    namespace
    {
        // A WMO's exterior windows (each with the room behind it) or its interior groups' (`inside`: none behind them, but a
        // group `between` flags: the room beyond each window).
        RootWindows Build(uint32_t path, const std::vector<glass::Window>& all, const GroupInfo* groups, size_t count, bool inside,
                          const std::vector<uint8_t>* between = nullptr)
        {
            RootWindows r;
            r.path = path;
            r.byGroup.assign(count, { 0u, 0u });
            for (const glass::Window& w : all)
            {
                if (w.path != path || ((w.flags & kWindowExterior) != 0) == inside || w.group >= count) continue;
                if (!Finite3(w.center) || !Finite3(w.normal) || !(w.halfWidth > 0.0f) || !(w.halfHeight > 0.0f)) continue;
                SeeWindow s;
                s.ordinal = w.ordinal;
                s.group = w.group;
                s.kind = w.kind;
                for (int k = 0; k < 3; ++k) { s.center[k] = w.center[k]; s.normal[k] = w.normal[k]; }
                glass::WindowFrame(s.normal, s.along, s.up);
                s.halfWidth = w.halfWidth;
                s.halfHeight = w.halfHeight;
                s.thickness = w.thickness;
                const bool beyond = inside && between && groups && w.group < between->size() && (*between)[w.group];
                const int partner = beyond ? GlazedPartner(s, path, all, groups, count) : -1;
                s.interior = beyond ? (partner >= 0 ? partner : RoomBeyond(s, groups, count)) : inside ? -1 : InteriorBehind(s.center, s.normal, groups, count);
                s.outer = inside ? OuterBehind(s, path, all) : 0.0f;
                r.windows.push_back(s);
            }
            std::stable_sort(r.windows.begin(), r.windows.end(), [](const SeeWindow& a, const SeeWindow& b) {
                return a.group != b.group ? a.group < b.group : a.ordinal < b.ordinal;
            });
            for (uint32_t i = 0; i < r.windows.size(); ++i)
            {
                auto& slot = r.byGroup[r.windows[i].group];
                if (!slot.second) slot.first = i;
                ++slot.second;
                const bool beyond = inside && between && r.windows[i].group < between->size() && (*between)[r.windows[i].group];
                if (!inside || beyond) (r.windows[i].interior >= 0 ? r.real : r.fake) += 1;
            }
            return r;
        }
    }

    float OuterBehind(const SeeWindow& w, uint32_t path, const std::vector<glass::Window>& all)
    {
        float best = 0.0f;
        for (const glass::Window& o : all)
        {
            if (o.path != path || !(o.flags & kWindowExterior) || !Finite3(o.center) || !Finite3(o.normal)) continue;
            if (std::fabs(Dot(o.normal, w.normal)) < kOuterFacing) continue;
            const float d[3] = { o.center[0] - w.center[0], o.center[1] - w.center[1], o.center[2] - w.center[2] };
            const float depth = Dot(d, w.normal);
            if (!(depth > 0.0f && depth <= kOuterReach)) continue;
            if (std::fabs(Dot(d, w.along)) > w.halfWidth + o.halfWidth || std::fabs(Dot(d, w.up)) > w.halfHeight + o.halfHeight) continue;
            if (best == 0.0f || depth < best) best = depth;
        }
        return best;
    }

    RootWindows BuildRoot(uint32_t path, const std::vector<glass::Window>& all, const GroupInfo* groups, size_t count)
    {
        return Build(path, all, groups, count, false);
    }

    unsigned ListOutside(RootWindows& outside, const std::vector<uint32_t>& groupFlags)
    {
        unsigned listed = 0;
        for (SeeWindow& w : outside.windows)
        {
            const uint32_t own = w.group < groupFlags.size() ? groupFlags[w.group] : 0u;
            const uint32_t room = w.interior >= 0 && static_cast<size_t>(w.interior) < groupFlags.size() ? groupFlags[static_cast<size_t>(w.interior)] : 0u;
            w.unlisted = ((own | room) & kSeeFromOutside) == 0;
            listed += w.unlisted ? 0 : 1;
        }
        return listed;
    }

    RootWindows BuildInside(uint32_t path, const std::vector<glass::Window>& all, size_t groupCount)
    {
        return Build(path, all, nullptr, groupCount, true);
    }

    RootWindows BuildInside(uint32_t path, const std::vector<glass::Window>& all, const GroupInfo* groups, size_t count,
                            const std::vector<uint8_t>& between)
    {
        return Build(path, all, groups, count, true, &between);
    }

    int GlazedPartner(const SeeWindow& w, uint32_t path, const std::vector<glass::Window>& all, const GroupInfo* groups, size_t count)
    {
        if (!groups || !Finite3(w.center) || !Finite3(w.normal)) return -1;
        int best = -1;
        float bestDistance = 0.0f;
        for (const glass::Window& o : all)
        {
            if (o.path != path || (o.flags & kWindowExterior) || o.group == w.group || o.group >= count || !Finite3(o.center) || !Finite3(o.normal))
                continue;
            if (groups[o.group].flags & client::kSkippedGroups) continue;
            if (Dot(o.normal, w.normal) > -kPartnerFacing) continue;   // not facing back at it
            const float d[3] = { o.center[0] - w.center[0], o.center[1] - w.center[1], o.center[2] - w.center[2] };
            if (Dot(d, w.normal) < -kPartnerBehind) continue;           // behind it: not the other side of its glass
            const float distance = std::sqrt(Dot(d, d));
            if (distance > kPartnerReach || (best >= 0 && distance >= bestDistance)) continue;
            best = static_cast<int>(o.group);
            bestDistance = distance;
        }
        return best;
    }

    int RoomBeyond(const SeeWindow& w, const GroupInfo* groups, size_t count)
    {
        if (!groups || !Finite3(w.center) || !Finite3(w.normal)) return -1;
        for (const float depth : kProbes)
        {
            const float p[3] = { w.center[0] + w.normal[0] * depth, w.center[1] + w.normal[1] * depth, w.center[2] + w.normal[2] * depth };
            int best = -1;
            float volume = 0.0f;
            for (size_t i = 0; i < count; ++i)
            {
                const GroupInfo& g = groups[i];
                if (i == w.group || (g.flags & client::kSkippedGroups)) continue;
                bool inside = true;
                for (int k = 0; k < 3 && inside; ++k)
                    inside = p[k] >= g.lo[k] - kBoxMargin && p[k] <= g.hi[k] + kBoxMargin;
                if (!inside) continue;
                const float v = (g.hi[0] - g.lo[0]) * (g.hi[1] - g.lo[1]) * (g.hi[2] - g.lo[2]);
                if (best < 0 || v < volume) { best = static_cast<int>(i); volume = v; }
            }
            if (best >= 0) return best;
        }
        return -1;
    }

    namespace
    {
        // Moller-Trumbore: t along o + t d to the triangle (a, e1, e2 from it), and det (> 0: its front faces the ray's
        // origin, cross(e1, e2) . d < 0); false when it misses or the ray runs along it.
        bool Intersect(const float* tri, const float o[3], const float d[3], float& t, float& det)
        {
            const float e1[3] = { tri[3] - tri[0], tri[4] - tri[1], tri[5] - tri[2] };
            const float e2[3] = { tri[6] - tri[0], tri[7] - tri[1], tri[8] - tri[2] };
            const float p[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
            det = Dot(e1, p);
            if (!(std::fabs(det) > 1e-12f)) return false;
            const float inv = 1.0f / det;
            const float s[3] = { o[0] - tri[0], o[1] - tri[1], o[2] - tri[2] };
            const float u = Dot(s, p) * inv;
            if (u < 0.0f || u > 1.0f) return false;
            const float q[3] = { s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0] };
            const float v = Dot(d, q) * inv;
            if (v < 0.0f || u + v > 1.0f) return false;
            t = Dot(e2, q) * inv;
            return std::isfinite(t);
        }
    }

    bool RoomMesh::Build(const float* vertices, uint32_t vertexCount, const uint16_t* indices, uint32_t indexCount,
                         const std::vector<std::pair<uint32_t, uint32_t>>& ranges)
    {
        return Build(vertices, vertexCount, indices, indexCount, ranges, {});
    }

    bool RoomMesh::Build(const float* vertices, uint32_t vertexCount, const uint16_t* indices, uint32_t indexCount,
                         const std::vector<std::pair<uint32_t, uint32_t>>& ranges, const std::vector<uint8_t>& tags)
    {
        *this = RoomMesh{};
        if (!vertices || !indices) return false;
        for (size_t ri = 0; ri < ranges.size(); ++ri)
        {
            const auto& r = ranges[ri];
            if (r.first > indexCount || r.second > indexCount - r.first || r.second % 3) { *this = RoomMesh{}; return false; }
            for (uint32_t k = r.first; k < r.first + r.second; ++k)
            {
                const uint32_t i = indices[k];
                if (i >= vertexCount) { *this = RoomMesh{}; return false; }
                for (int c = 0; c < 3; ++c) m_tris.push_back(vertices[3 * static_cast<size_t>(i) + c]);
            }
            m_tags.insert(m_tags.end(), r.second / 3, ri < tags.size() ? tags[ri] : uint8_t{ 0 });
        }
        for (const float f : m_tris)
            if (!std::isfinite(f)) { *this = RoomMesh{}; return false; }
        if (m_tris.empty()) return true;
        float hi[3];
        for (int k = 0; k < 3; ++k) { m_lo[k] = m_tris[k]; hi[k] = m_tris[k]; }
        for (size_t i = 0; i < m_tris.size(); i += 3)
            for (int k = 0; k < 3; ++k) { m_lo[k] = std::min(m_lo[k], m_tris[i + k]); hi[k] = std::max(hi[k], m_tris[i + k]); }
        float longest = 0.0f;
        for (int k = 0; k < 3; ++k) { m_lo[k] -= 0.01f; hi[k] += 0.01f; longest = std::max(longest, hi[k] - m_lo[k]); }
        m_cell = std::max(kGridLeast, longest / static_cast<float>(kGridCells));
        for (int k = 0; k < 3; ++k) m_n[k] = std::clamp(static_cast<int>(std::ceil((hi[k] - m_lo[k]) / m_cell)), 1, 2 * kGridCells);
        const size_t cells = static_cast<size_t>(m_n[0]) * m_n[1] * m_n[2];
        // each triangle in every cell its box (a hair grown) touches: counted, then placed
        auto range = [&](size_t t, int lo[3], int hiCell[3]) {
            const float* v = &m_tris[9 * t];
            for (int k = 0; k < 3; ++k)
            {
                const float a = std::min({ v[k], v[3 + k], v[6 + k] }) - 1e-3f, b = std::max({ v[k], v[3 + k], v[6 + k] }) + 1e-3f;
                lo[k] = std::clamp(static_cast<int>(std::floor((a - m_lo[k]) / m_cell)), 0, m_n[k] - 1);
                hiCell[k] = std::clamp(static_cast<int>(std::floor((b - m_lo[k]) / m_cell)), 0, m_n[k] - 1);
            }
        };
        m_cellStart.assign(cells + 1, 0);
        const size_t n = Triangles();
        for (int pass = 0; pass < 2; ++pass)
        {
            std::vector<uint32_t> fill;
            if (pass) { for (size_t c = 0; c < cells; ++c) m_cellStart[c + 1] += m_cellStart[c]; m_refs.resize(m_cellStart[cells]); fill.assign(m_cellStart.begin(), m_cellStart.end() - 1); }
            for (size_t t = 0; t < n; ++t)
            {
                int lo[3], hiCell[3];
                range(t, lo, hiCell);
                for (int z = lo[2]; z <= hiCell[2]; ++z)
                    for (int y = lo[1]; y <= hiCell[1]; ++y)
                        for (int x = lo[0]; x <= hiCell[0]; ++x)
                        {
                            const size_t c = (static_cast<size_t>(z) * m_n[1] + y) * m_n[0] + x;
                            if (pass) m_refs[fill[c]++] = static_cast<uint32_t>(t);
                            else ++m_cellStart[c + 1];
                        }
            }
        }
        return true;
    }

    bool RoomMesh::TestCell(int cell, const float o[3], const float d[3], float tmin, float& best, int& bestTri) const
    {
        bool any = false;
        for (uint32_t r = m_cellStart[static_cast<size_t>(cell)]; r < m_cellStart[static_cast<size_t>(cell) + 1]; ++r)
        {
            float t, det;
            if (Intersect(&m_tris[9 * static_cast<size_t>(m_refs[r])], o, d, t, det) && t > tmin && t < best)
            {
                best = t;
                bestTri = static_cast<int>(m_refs[r]);
                any = true;
            }
        }
        return any;
    }

    RoomMesh::Hit RoomMesh::First(const float o[3], const float d[3], float tmin, float tmax, float* tOut, uint8_t* tagOut,
                                  float* normalOut) const
    {
        if (m_tris.empty() || !Finite3(o) || !Finite3(d) || !(tmax > tmin)) return kNone;
        // the ray within the grid's box
        float t0 = tmin, t1 = tmax;
        for (int k = 0; k < 3; ++k)
        {
            const float hi = m_lo[k] + m_cell * static_cast<float>(m_n[k]);
            if (std::fabs(d[k]) < 1e-12f)
            {
                if (o[k] < m_lo[k] || o[k] > hi) return kNone;
                continue;
            }
            float a = (m_lo[k] - o[k]) / d[k], b = (hi - o[k]) / d[k];
            if (a > b) std::swap(a, b);
            t0 = std::max(t0, a);
            t1 = std::min(t1, b);
            if (t0 > t1) return kNone;
        }
        int c[3], step[3];
        float next[3], delta[3];
        for (int k = 0; k < 3; ++k)
        {
            const float p = o[k] + d[k] * t0;
            c[k] = std::clamp(static_cast<int>(std::floor((p - m_lo[k]) / m_cell)), 0, m_n[k] - 1);
            if (d[k] > 1e-12f) { step[k] = 1; next[k] = (m_lo[k] + m_cell * static_cast<float>(c[k] + 1) - o[k]) / d[k]; delta[k] = m_cell / d[k]; }
            else if (d[k] < -1e-12f) { step[k] = -1; next[k] = (m_lo[k] + m_cell * static_cast<float>(c[k]) - o[k]) / d[k]; delta[k] = -m_cell / d[k]; }
            else { step[k] = 0; next[k] = std::numeric_limits<float>::infinity(); delta[k] = next[k]; }
        }
        float best = tmax;
        int bestTri = -1;
        for (int guard = 0; guard < 4 * (m_n[0] + m_n[1] + m_n[2]) + 8; ++guard)
        {
            TestCell((c[2] * m_n[1] + c[1]) * m_n[0] + c[0], o, d, tmin, best, bestTri);
            const int k = next[0] < next[1] ? (next[0] < next[2] ? 0 : 2) : (next[1] < next[2] ? 1 : 2);
            const float exit = next[k];
            if ((bestTri >= 0 && best <= exit) || exit > t1) break;
            c[k] += step[k];
            if (c[k] < 0 || c[k] >= m_n[k]) break;
            next[k] += delta[k];
        }
        if (bestTri < 0) return kNone;
        float t, det;
        if (!Intersect(&m_tris[9 * static_cast<size_t>(bestTri)], o, d, t, det)) return kNone;
        if (tOut) *tOut = best;
        if (tagOut) *tagOut = static_cast<size_t>(bestTri) < m_tags.size() ? m_tags[static_cast<size_t>(bestTri)] : uint8_t{ 0 };
        if (normalOut)
        {
            const float* tri = &m_tris[9 * static_cast<size_t>(bestTri)];
            const float e1[3] = { tri[3] - tri[0], tri[4] - tri[1], tri[5] - tri[2] };
            const float e2[3] = { tri[6] - tri[0], tri[7] - tri[1], tri[8] - tri[2] };
            const float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
            const float l = std::sqrt(Dot(n, n));
            for (int k = 0; k < 3; ++k) normalOut[k] = l > 0.0f ? n[k] / l : 0.0f;
        }
        return det > 0.0f ? kFront : kBack;
    }

    RoomMesh::Hit RoomMesh::FirstBrute(const float o[3], const float d[3], float tmin, float tmax, float* tOut) const
    {
        float best = tmax, bestDet = 0.0f;
        bool any = false;
        for (size_t i = 0; i < Triangles(); ++i)
        {
            float t, det;
            if (Intersect(&m_tris[9 * i], o, d, t, det) && t > tmin && t < best) { best = t; bestDet = det; any = true; }
        }
        if (!any) return kNone;
        if (tOut) *tOut = best;
        return bestDet > 0.0f ? kFront : kBack;
    }

    bool SeesRoom(const SeeWindow& w, const float eye[3], const RoomMesh& room, Sight* out)
    {
        Sight sight;
        if (room.Empty() || !Finite3(eye))
        {
            if (out) *out = sight;
            return false;
        }
        for (int i = 0; i < static_cast<int>(sizeof(kSight) / sizeof(kSight[0])); ++i)
        {
            float d[3];
            for (int k = 0; k < 3; ++k) d[k] = w.center[k] + w.along[k] * kSight[i][0] * w.halfWidth + w.up[k] * kSight[i][1] * w.halfHeight - eye[k];
            const float len = std::sqrt(Dot(d, d));
            float t = 0.0f;
            RoomMesh::Hit hit = RoomMesh::kNone;
            if (len > 1e-3f && std::isfinite(len))
            {
                for (float& v : d) v /= len;
                hit = room.First(eye, d, len, len + kSightReach, &t);
            }
            if (i == 0) sight.middle = hit;
            if (hit == RoomMesh::kFront) { ++sight.front; continue; }
            ++(hit == RoomMesh::kBack ? sight.back : sight.none);
            if (sight.firstMiss < 0)
            {
                sight.firstMiss = i;
                sight.missHit = hit;
                sight.missDepth = hit == RoomMesh::kBack ? t - len : 0.0f;
            }
        }
        if (out) *out = sight;
        return sight.middle == RoomMesh::kFront && sight.front >= kSightFront;
    }

    void GlassSightPoints(const float* vertices, uint32_t vertexCount, const uint16_t* indices, uint32_t indexCount,
                          const std::vector<std::pair<uint32_t, uint32_t>>& glassRanges, const SeeWindow* windows, uint32_t count,
                          std::vector<SightPoints>& out)
    {
        out.assign(count, SightPoints{});
        if (!vertices || !indices || !windows || !count) return;
        std::vector<std::vector<std::array<float, 3>>> candidates(count);
        for (const auto& [start, n] : glassRanges)
        {
            if (start > indexCount || n > indexCount - start) continue;
            for (uint32_t t = start; t + 2 < start + n; t += 3)
            {
                const float* v[3];
                bool ok = true;
                for (int k = 0; k < 3; ++k)
                {
                    ok = ok && indices[t + k] < vertexCount;
                    v[k] = ok ? vertices + 3 * static_cast<size_t>(indices[t + k]) : vertices;
                }
                if (!ok) continue;
                const float mid[3] = { (v[0][0] + v[1][0] + v[2][0]) / 3.0f, (v[0][1] + v[1][1] + v[2][1]) / 3.0f, (v[0][2] + v[1][2] + v[2][2]) / 3.0f };
                const int w = WindowOf(mid, windows, count);
                if (w < 0) continue;
                // the triangle cut in sixteen (each side in four): the pieces' middles in quarter steps along a -> b (u) and
                // a -> c (v): the ten upright pieces' at (i + 1/3, j + 1/3) / 4, the six upside down's at (i + 2/3, j + 2/3) / 4
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; i + j < 4; ++j)
                        for (int down = 0; down < 2; ++down)
                        {
                            if (down && i + j > 2) continue;
                            const float third = down ? 2.0f / 3.0f : 1.0f / 3.0f;
                            const float u = (i + third) / 4.0f, vv = (j + third) / 4.0f;
                            std::array<float, 3> p;
                            for (int k = 0; k < 3; ++k) p[k] = v[0][k] + (v[1][k] - v[0][k]) * u + (v[2][k] - v[0][k]) * vv;
                            candidates[static_cast<size_t>(w)].push_back(p);
                        }
            }
        }
        for (uint32_t w = 0; w < count; ++w)
        {
            // the glass inside the window's middle kSightInner of its rectangle (an outer window's glass reaches past the
            // room's opening behind it: its edge is mostly lead and frame, and behind it the wall); all of it if none is
            std::vector<std::array<float, 3>> c;
            for (const auto& p : candidates[w])
            {
                const float d[3] = { p[0] - windows[w].center[0], p[1] - windows[w].center[1], p[2] - windows[w].center[2] };
                if (std::fabs(Dot(d, windows[w].along)) <= kSightInner * windows[w].halfWidth && std::fabs(Dot(d, windows[w].up)) <= kSightInner * windows[w].halfHeight)
                    c.push_back(p);
            }
            if (c.empty()) c = candidates[w];
            if (c.empty()) continue;
            SightPoints& s = out[w];
            std::vector<float> nearest(c.size(), 1e30f);
            auto d2 = [](const std::array<float, 3>& a, const float* b) { const float d[3] = { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; return Dot(d, d); };
            // the middle's nearest first, then the farthest from those taken, until kSightPoints or none left apart
            size_t pick = 0;
            for (size_t i = 1; i < c.size(); ++i)
                if (d2(c[i], windows[w].center) < d2(c[pick], windows[w].center)) pick = i;
            while (s.n < kSightPoints)
            {
                for (int k = 0; k < 3; ++k) s.p[s.n][k] = c[pick][k];
                ++s.n;
                size_t far = 0;
                float best = -1.0f;
                for (size_t i = 0; i < c.size(); ++i)
                {
                    nearest[i] = std::min(nearest[i], d2(c[i], s.p[s.n - 1]));
                    if (nearest[i] > best) { best = nearest[i]; far = i; }
                }
                if (best < 1e-4f) break;   // (every candidate taken or on one already)
                pick = far;
            }
        }
    }

    void LookThrough(const SeeWindow& w, const float eye[3], const RoomMesh& room, const SightPoints* points, Sight& out)
    {
        out = Sight{};
        if (room.Empty() || !Finite3(eye)) return;
        out.onGlass = points && points->n > 0;
        const int rays = out.onGlass ? points->n : static_cast<int>(sizeof(kSight) / sizeof(kSight[0]));
        for (int i = 0; i < rays; ++i)
        {
            float d[3];
            for (int k = 0; k < 3; ++k)
                d[k] = (out.onGlass ? points->p[i][k] : w.center[k] + w.along[k] * kSight[i][0] * w.halfWidth + w.up[k] * kSight[i][1] * w.halfHeight) - eye[k];
            const float len = std::sqrt(Dot(d, d));
            float t = 0.0f;
            RoomMesh::Hit hit = RoomMesh::kNone;
            if (len > 1e-3f && std::isfinite(len))
            {
                for (float& v : d) v /= len;
                hit = room.First(eye, d, len, len + kSightReach, &t);
            }
            ++out.rays;
            if (i == 0) out.middle = hit;
            if (hit == RoomMesh::kFront) { ++out.front; continue; }
            ++(hit == RoomMesh::kBack ? out.back : out.none);
            if (out.firstMiss < 0)
            {
                out.firstMiss = i;
                out.missHit = hit;
                out.missDepth = hit == RoomMesh::kBack ? t - len : 0.0f;
            }
        }
    }

    bool SightOpen(const Sight& s, bool wasOpen)
    {
        if (!(s.front > s.back)) return false;
        if (wasOpen) return s.front >= kStayFront && s.back <= kStayBack && s.front + kStayNone >= s.none;
        return s.front >= kOpenFront && s.back <= kOpenBack && s.front > s.none;
    }

    bool Hold(Gate& g, bool want, float dt)
    {
        if (!g.seen) { g = Gate{}; g.seen = true; g.open = want; return g.open; }
        if (want == g.open) { g.pending = false; g.held = 0.0f; return g.open; }
        if (!g.pending) { g.pending = true; g.held = 0.0f; }
        g.held += std::isfinite(dt) && dt > 0.0f ? std::min(dt, 1.0f) : 0.0f;
        if (g.held >= kGateHold) { g.open = want; g.pending = false; g.held = 0.0f; }
        return g.open;
    }

    void Corners(const SeeWindow& w, float grow, float out[4][3])
    {
        const float a = w.halfWidth + grow, b = w.halfHeight + grow;
        const float sa[4] = { -1.0f, 1.0f, 1.0f, -1.0f }, sb[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
        for (int i = 0; i < 4; ++i)
            for (int k = 0; k < 3; ++k) out[i][k] = w.center[k] + w.along[k] * a * sa[i] + w.up[k] * b * sb[i];
    }

    bool ScreenRect(const float (*corners)[3], int count, const float model[16], const float eye[3], const float viewProj[16], Rect& out)
    {
        if (!corners || count < 3 || count > 16) return false;
        std::vector<Clip> poly(static_cast<size_t>(count)), next;
        for (int i = 0; i < count; ++i)
        {
            float world[4];
            Transform(corners[i], 1.0f, model, world);
            const float rel[3] = { world[0] - eye[0], world[1] - eye[1], world[2] - eye[2] };
            Transform(rel, 1.0f, viewProj, poly[static_cast<size_t>(i)].v);
            for (float c : poly[static_cast<size_t>(i)].v)
                if (!std::isfinite(c)) return false;
        }
        // In front of the eye, then the four sides: x <= w, -x <= w, y <= w, -y <= w.
        const float planes[5][4] = { { 0, 0, 1, 1e-4f }, { -1, 0, 1, 0 }, { 1, 0, 1, 0 }, { 0, -1, 1, 0 }, { 0, 1, 1, 0 } };
        for (const auto& p : planes)
        {
            ClipAgainst(poly, p[0], p[1], p[2], p[3], next);
            poly.swap(next);
            if (poly.size() < 3) return false;
        }
        Rect r{ 1e30f, 1e30f, -1e30f, -1e30f };
        for (const Clip& c : poly)
        {
            const float w = c.v[3] > 1e-4f ? c.v[3] : 1e-4f;
            const float x = c.v[0] / w, y = c.v[1] / w;
            r.minX = std::min(r.minX, x); r.maxX = std::max(r.maxX, x);
            r.minY = std::min(r.minY, y); r.maxY = std::max(r.maxY, y);
        }
        r.minX = std::max(r.minX, -1.0f); r.maxX = std::min(r.maxX, 1.0f);
        r.minY = std::max(r.minY, -1.0f); r.maxY = std::min(r.maxY, 1.0f);
        if (!(r.maxX > r.minX) || !(r.maxY > r.minY)) return false;
        out = r;
        return true;
    }

    bool Overlap(const Rect& a, const Rect& b, Rect& out)
    {
        Rect r{ std::max(a.minX, b.minX), std::max(a.minY, b.minY), std::min(a.maxX, b.maxX), std::min(a.maxY, b.maxY) };
        if (!(r.maxX - r.minX > 0.001f) || !(r.maxY - r.minY > 0.001f)) return false;
        out = r;
        return true;
    }

    bool Facing(const SeeWindow& w, const float camLocal[3])
    {
        const float d[3] = { camLocal[0] - w.center[0], camLocal[1] - w.center[1], camLocal[2] - w.center[2] };
        return Dot(d, w.normal) > kFacing;
    }

    float DistanceToBox(const float p[3], const float lo[3], const float hi[3])
    {
        float s = 0.0f;
        for (int k = 0; k < 3; ++k)
        {
            const float d = p[k] < lo[k] ? lo[k] - p[k] : (p[k] > hi[k] ? p[k] - hi[k] : 0.0f);
            s += d * d;
        }
        return std::sqrt(s);
    }

    void PlanWindows(const SeeWindow* windows, uint32_t count, const float camLocal[3], const float boxLo[3], const float boxHi[3],
                     const Rect& walk, const float model[16], const float eye[3], const float viewProj[16], unsigned& budget,
                     std::vector<WindowPlan>& out)
    {
        out.assign(count, WindowPlan{});
        if (!windows || !Finite3(camLocal)) return;
        const bool inRange = DistanceToBox(camLocal, boxLo, boxHi) <= kRange;
        static thread_local std::vector<std::pair<float, uint32_t>> eligible;   // (distance^2, window): the budget's, nearest first
        eligible.clear();
        for (uint32_t i = 0; i < count; ++i)
        {
            const SeeWindow& w = windows[i];
            WindowPlan& p = out[i];
            // (a window the table doesn't open from outside is left as the client draws it, not even projected: a city's
            // thousands cost about a millisecond a frame projected; its batch then isn't drawn whole, see SeeThrough)
            if (w.unlisted || !Facing(w, camLocal)) continue;
            float quad[4][3];
            Corners(w, kQuadGrow, quad);
            Rect screen, clip;
            if (!ScreenRect(quad, 4, model, eye, viewProj, screen) || !Overlap(screen, walk, clip)) continue;
            p.clip = clip;
            p.state = kShut;
            const float d[3] = { camLocal[0] - w.center[0], camLocal[1] - w.center[1], camLocal[2] - w.center[2] };
            if (w.interior >= 0 && inRange && Dot(d, d) <= kRange * kRange) eligible.emplace_back(Dot(d, d), i);
        }
        std::stable_sort(eligible.begin(), eligible.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& e : eligible)
        {
            if (budget == 0) break;
            out[e.second].candidate = true;
            --budget;
        }
    }

    void FrustumRect(const Rect& clip, float out[4])
    {
        out[0] = (clip.minY + 1.0f) * 0.5f;
        out[1] = (clip.minX + 1.0f) * 0.5f;
        out[2] = (clip.maxY + 1.0f) * 0.5f;
        out[3] = (clip.maxX + 1.0f) * 0.5f;
    }

    Rect FromCRect(const float c[4]) { return Rect{ c[1], c[0], c[3], c[2] }; }

    void ToCRect(const Rect& r, float out[4])
    {
        out[0] = r.minY; out[1] = r.minX; out[2] = r.maxY; out[3] = r.maxX;
    }

    bool FacingIn(const SeeWindow& w, const float camLocal[3])
    {
        const float d[3] = { camLocal[0] - w.center[0], camLocal[1] - w.center[1], camLocal[2] - w.center[2] };
        return Dot(d, w.normal) < -kFacing;
    }

    PortalView ViewOf(const Rect& screen, float depth)
    {
        PortalView v;
        FrustumRect(screen, v.rect);
        v.maxViewDepth = std::isfinite(depth) && depth > 0.0f ? depth : 0.0f;
        return v;
    }

    bool FarthestDepth(const float (*corners)[3], int count, const float model[16], const float eye[3], const float viewProj[16], float& out)
    {
        bool any = false;
        out = 0.0f;
        for (int i = 0; corners && i < count; ++i)
        {
            float world[4], clip[4];
            Transform(corners[i], 1.0f, model, world);
            const float rel[3] = { world[0] - eye[0], world[1] - eye[1], world[2] - eye[2] };
            Transform(rel, 1.0f, viewProj, clip);
            if (!std::isfinite(clip[3])) continue;
            out = any ? std::max(out, clip[3]) : clip[3];
            any = true;
        }
        return any;
    }

    void PlanInside(const SeeWindow* windows, uint32_t count, const float camLocal[3], const Rect& walk, const float model[16],
                    const float eye[3], const float viewProj[16], unsigned& budget, std::vector<WindowPlan>& out, bool fakesFree)
    {
        out.assign(count, WindowPlan{});
        if (!windows || !Finite3(camLocal)) return;
        std::vector<std::pair<float, uint32_t>> inRange;   // (distance, window): the budget goes to the nearest first
        for (uint32_t i = 0; i < count; ++i)
        {
            const SeeWindow& w = windows[i];
            WindowPlan& p = out[i];
            if (!FacingIn(w, camLocal)) continue;
            float quad[4][3];
            Corners(w, kQuadGrow, quad);
            Rect screen, clip;
            if (!ScreenRect(quad, 4, model, eye, viewProj, screen) || !Overlap(screen, walk, clip)) continue;
            p.state = kShut;
            p.clip = clip;
            p.screen = screen;
            FarthestDepth(quad, 4, model, eye, viewProj, p.depth);
            if (fakesFree && w.interior < 0) continue;   // (nothing beyond it: shut, without a share of the budget)
            const float d[3] = { camLocal[0] - w.center[0], camLocal[1] - w.center[1], camLocal[2] - w.center[2] };
            const float distance = std::sqrt(Dot(d, d));
            if (distance <= kInsideRange) inRange.emplace_back(distance, i);
        }
        std::stable_sort(inRange.begin(), inRange.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [distance, i] : inRange)
        {
            if (!budget) break;
            out[i].candidate = true;
            --budget;
        }
    }

    namespace
    {
        // What a line of sight meets first through a building as the client draws it (faces one-sided; a pane seen from
        // behind see-through and passed; `outerPanes`: from inside, an outer pane either way is the way out and stops it).
        // (kOpening: from inside, a side of the window's own opening met from the front within its lining: Rays')
        enum class Met { kNothing, kRoomFront, kRoomBack, kShellFront, kShellBack, kRoomPane, kOuterPane, kOpening };

        Met Trace(const SightMesh* groups, size_t count, const float o[3], const float d[3], float tmin, float tmax, bool outerPanes,
                  float ownPane, float& at, const float* reveal = nullptr, float revealUntil = 0.0f, bool revealShell = false,
                  float* wallNormal = nullptr)
        {
            at = tmax;
            for (int pass = 0; pass < 8; ++pass)
            {
                float best = tmax;
                for (size_t g = 0; groups && g < count; ++g)
                {
                    if (!groups[g].mesh || groups[g].mesh->Empty()) continue;
                    float t = 0.0f;
                    if (groups[g].mesh->First(o, d, tmin, best, &t) != RoomMesh::kNone && t < best) best = t;
                }
                if (!(best < tmax)) return Met::kNothing;
                at = best;
                // every surface at that depth (a window's inner and outer panes may share a plane): an outer pane among them
                // is the way out (from inside, either way) or the building's outside (from outside, its front); else a wall;
                // else a room's pane from its front (but the window's own, before `ownPane`); else panes from behind, passed,
                // and (`reveal`: the window's normal and along) a room's wall round its opening or a side of it, from behind,
                // before `revealUntil` (`revealShell`: the shell's instead, an opening's lining from inside)
                const float edge = best + 1e-3f;
                bool outer = false, roomPane = false, wall = false, wallFront = false, wallExterior = false;
                for (size_t g = 0; groups && g < count; ++g)
                {
                    if (!groups[g].mesh || groups[g].mesh->Empty()) continue;
                    float t = 0.0f, n[3] = {};
                    uint8_t tag = 0;
                    const RoomMesh::Hit h = groups[g].mesh->First(o, d, tmin, edge, &t, &tag, n);
                    if (h == RoomMesh::kNone) continue;
                    const bool exterior = groups[g].exterior;
                    if (tag & kGlassTag)
                    {
                        if (exterior) outer = outer || outerPanes || h == RoomMesh::kFront;
                        else roomPane = roomPane || (h == RoomMesh::kFront && t >= ownPane);
                    }
                    else if (reveal && exterior == revealShell && h == RoomMesh::kBack && t < revealUntil &&
                             (std::fabs(Dot(n, reveal)) >= kRevealFacing || std::fabs(Dot(n, reveal + 3)) >= kRevealFacing))
                        continue;
                    else if (!wall)
                    {
                        wall = true;
                        wallFront = h == RoomMesh::kFront;
                        wallExterior = exterior;
                        if (wallNormal) for (int k = 0; k < 3; ++k) wallNormal[k] = n[k];
                    }
                }
                if (outer) return Met::kOuterPane;
                if (wall) return wallExterior ? (wallFront ? Met::kShellFront : Met::kShellBack) : (wallFront ? Met::kRoomFront : Met::kRoomBack);
                if (roomPane) return Met::kRoomPane;
                tmin = edge;   // panes from behind (and the window's own): see-through
            }
            return Met::kNothing;
        }

        // The rays through a window's sight points (or kSight's on its rectangle) from `eye`, each from `from` past the
        // glass (its own pane passed before `ownPane` past it), classified by `as`.
        template <class As>
        void Rays(const SeeWindow& w, const float eye[3], const SightMesh* groups, size_t count, const SightPoints* points, float from,
                  float ownPane, float reach, bool outerPanes, As as, Sight& out, float reveal = 0.0f, bool lining = false)
        {
            out = Sight{};
            if (!Finite3(eye)) return;
            const float axes[6] = { w.normal[0], w.normal[1], w.normal[2], w.along[0], w.along[1], w.along[2] };   // (Trace's reveal)
            out.onGlass = points && points->n > 0;
            const int rays = out.onGlass ? points->n : static_cast<int>(sizeof(kSight) / sizeof(kSight[0]));
            for (int i = 0; i < rays; ++i)
            {
                float d[3];
                for (int k = 0; k < 3; ++k)
                    d[k] = (out.onGlass ? points->p[i][k] : w.center[k] + w.along[k] * kSight[i][0] * w.halfWidth + w.up[k] * kSight[i][1] * w.halfHeight) - eye[k];
                const float len = std::sqrt(Dot(d, d));
                ++out.rays;
                RoomMesh::Hit seen = RoomMesh::kNone;   // (no ray: neither)
                float at = 0.0f;
                if (len > 1e-3f && std::isfinite(len))
                {
                    for (float& v : d) v /= len;
                    // (the reveal along the ray; the lining to a plane `reveal` past the glass, along the window's normal)
                    const float until = lining ? len + reveal / std::max(Dot(d, w.normal), 0.2f) : len + reveal;
                    float wallNormal[3] = {};
                    Met met = Trace(groups, count, eye, d, len + from, len + reach, outerPanes, len + ownPane, at,
                                    reveal > 0.0f ? axes : nullptr, until, lining, wallNormal);
                    // (from inside, out past its opening's lining: the building's own outside met from the front, another wing's
                    // face, is the street seen out of it, as the client draws it there)
                    if (lining && met == Met::kShellFront && at > until) met = Met::kOuterPane;
                    // (from inside, within the lining: a side of the window's own opening met from the front -- a jamb, the
                    // sill, the head: its normal along the window's width or height -- is what a slanted view through the glass
                    // sees of the deep opening, stone the way out draws behind the glass, 2026-10-24)
                    if (lining && (met == Met::kShellFront || met == Met::kRoomFront) && at <= until &&
                        (std::fabs(Dot(wallNormal, w.along)) >= kRevealFacing || std::fabs(Dot(wallNormal, w.up)) >= kRevealFacing))
                        met = Met::kOpening;
                    seen = as(met);
                }
                if (i == 0) out.middle = seen;
                if (seen == RoomMesh::kFront) { ++out.front; continue; }
                ++(seen == RoomMesh::kBack ? out.back : out.none);
                if (out.firstMiss < 0)
                {
                    out.firstMiss = i;
                    out.missHit = seen;
                    out.missDepth = std::isfinite(at - len) ? at - len : 0.0f;
                }
            }
        }
    }

    void NearGroups(const SeeWindow& w, int room, const GroupInfo* groups, size_t count, std::vector<uint16_t>& out)
    {
        out.clear();
        if (!groups || !Finite3(w.center)) return;
        float lo[3], hi[3];
        for (int k = 0; k < 3; ++k) lo[k] = hi[k] = w.center[k];
        if (room >= 0 && static_cast<size_t>(room) < count)
            for (int k = 0; k < 3; ++k)
            {
                lo[k] = std::min(lo[k], groups[room].lo[k]);
                hi[k] = std::max(hi[k], groups[room].hi[k]);
            }
        for (int k = 0; k < 3; ++k) { lo[k] -= kNearMargin; hi[k] += kNearMargin; }
        for (size_t g = 0; g < count && g < 65536; ++g)
        {
            bool meets = true;
            for (int k = 0; k < 3 && meets; ++k) meets = groups[g].lo[k] <= hi[k] && groups[g].hi[k] >= lo[k];
            if (meets) out.push_back(static_cast<uint16_t>(g));
        }
    }

    float RoomReach(const float eye[3], const GroupInfo& room)
    {
        if (!Finite3(eye)) return kVoidReach;
        float far2 = 0.0f;
        for (int c = 0; c < 8; ++c)
        {
            const float p[3] = { (c & 1) ? room.hi[0] + kNearMargin : room.lo[0] - kNearMargin, (c & 2) ? room.hi[1] + kNearMargin : room.lo[1] - kNearMargin,
                                 (c & 4) ? room.hi[2] + kNearMargin : room.lo[2] - kNearMargin };
            const float d[3] = { p[0] - eye[0], p[1] - eye[1], p[2] - eye[2] };
            far2 = std::max(far2, Dot(d, d));
        }
        const float far = std::sqrt(far2);
        return std::isfinite(far) ? std::min(std::max(far, kVoidReach), kRoomReach) : kVoidReach;
    }

    void LookIn(const SeeWindow& w, const float eye[3], const SightMesh* groups, size_t count, const SightPoints* points, Sight& out, float reach)
    {
        // (past its own outer pane: on its glass points, a hair; on its rectangle, its thickness too)
        const float skip = points && points->n > 0 ? 0.02f : 0.05f + w.thickness;
        Rays(w, eye, groups, count, points, skip, skip, std::isfinite(reach) && reach > 0.0f ? reach : kVoidReach, false, [](Met m) {
            switch (m)
            {
            case Met::kRoomFront: case Met::kRoomPane: return RoomMesh::kFront;
            case Met::kShellFront: case Met::kOuterPane: return RoomMesh::kNone;
            default: return RoomMesh::kBack;   // a surface from behind, or the void
            }
        }, out, kReveal);
    }

    void LookOut(const SeeWindow& w, const float eye[3], const SightMesh* groups, size_t count, const SightPoints* points, Sight& out)
    {
        // (from a hair before its own inner pane, which faces the room and is passed: an outer pane in its plane is the way out)
        Rays(w, eye, groups, count, points, -0.02f, 0.02f, kSightReach, true, [](Met m) {
            switch (m)
            {
            case Met::kOuterPane: case Met::kNothing: case Met::kOpening: return RoomMesh::kFront;
            case Met::kRoomBack: case Met::kShellBack: return RoomMesh::kBack;
            default: return RoomMesh::kNone;
            }
        }, out, w.outer > 0.0f ? w.outer + kLiningMargin : 0.0f, true);
    }

    int WindowOf(const float p[3], const SeeWindow* windows, uint32_t count)
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            const SeeWindow& w = windows[i];
            const float d[3] = { p[0] - w.center[0], p[1] - w.center[1], p[2] - w.center[2] };
            const float slab = std::max(w.thickness, kSlab) + kLookup;
            if (std::fabs(Dot(d, w.along)) <= w.halfWidth + kLookup && std::fabs(Dot(d, w.up)) <= w.halfHeight + kLookup &&
                std::fabs(Dot(d, w.normal)) <= slab)
                return static_cast<int>(i);
        }
        return -1;
    }

    BatchWindows WindowsOfBatch(const uint16_t* indices, uint32_t indexCount, const float* vertices, uint32_t vertexCount,
                                uint32_t start, uint32_t count, const SeeWindow* windows, uint32_t windowCount)
    {
        BatchWindows b;
        if (!indices || !vertices || count < 3 || count % 3 || start > indexCount || count > indexCount - start) return b;
        std::vector<bool> seen(windowCount, false);
        b.indices.assign(indices + start, indices + start + count);
        b.window.reserve(count / 3);
        for (uint32_t t = start; t < start + count; t += 3)
        {
            const float* v[3];
            for (int k = 0; k < 3; ++k)
            {
                const uint32_t i = indices[t + k];
                if (i >= vertexCount) return BatchWindows{};
                v[k] = vertices + 3 * static_cast<size_t>(i);
            }
            const float mid[3] = { (v[0][0] + v[1][0] + v[2][0]) / 3.0f, (v[0][1] + v[1][1] + v[2][1]) / 3.0f, (v[0][2] + v[1][2] + v[2][2]) / 3.0f };
            ++b.triangles;
            const int w = WindowOf(mid, windows, windowCount);
            b.window.push_back(static_cast<int16_t>(w >= 0 ? w : -1));
            if (w >= 0)
            {
                seen[static_cast<size_t>(w)] = true;
                continue;
            }
            ++b.strays;
            const float e1[3] = { v[1][0] - v[0][0], v[1][1] - v[0][1], v[1][2] - v[0][2] };
            const float e2[3] = { v[2][0] - v[0][0], v[2][1] - v[0][1], v[2][2] - v[0][2] };
            const float c[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
            const float area = 0.5f * std::sqrt(Dot(c, c));
            b.strayArea += std::isfinite(area) ? area : 1e6f;
        }
        for (uint32_t i = 0; i < windowCount; ++i)
            if (seen[i]) b.windows.push_back(static_cast<uint16_t>(i));
        b.valid = true;
        return b;
    }

    void SplitByWindow(const BatchWindows& b, const uint8_t* states, size_t stateCount, std::vector<uint16_t>& open,
                       std::vector<uint16_t>& rest)
    {
        open.clear();
        rest.clear();
        if (!b.valid || !states || b.window.size() * 3 != b.indices.size()) return;
        for (size_t t = 0; t < b.window.size(); ++t)
        {
            const int w = b.window[t];
            std::vector<uint16_t>& to = w >= 0 && static_cast<size_t>(w) < stateCount && states[w] == kOpened ? open : rest;
            to.insert(to.end(), b.indices.begin() + 3 * t, b.indices.begin() + 3 * t + 3);
        }
    }

    bool Deferrable(const BatchWindows& b, const uint8_t* states, size_t stateCount)
    {
        if (!b.valid || b.windows.empty() || !(b.strayArea <= kTrimArea) || !states) return false;
        bool opened = false;
        for (const uint16_t w : b.windows)
        {
            if (w >= stateCount) return false;
            opened = opened || states[w] == kOpened;
        }
        return opened;
    }

    FrostPlan PlanFrost(bool frosted, bool perPixel, float blur, bool blurFailed)
    {
        FrostPlan p;
        const bool blurs = std::isfinite(blur) && blur > 0.0f;
        p.opens = frosted && perPixel && (!blurs || !blurFailed);
        p.copies = p.opens && blurs;
        return p;
    }

    bool BatchUnlisted(const BatchWindows& b, const SeeWindow* windows, size_t count)
    {
        if (!windows) return false;
        for (const uint16_t w : b.windows)
            if (w < count && windows[w].unlisted) return true;
        return false;
    }

    bool DeferrableWhole(const BatchWindows& b, const uint8_t* states, size_t stateCount)
    {
        if (!Deferrable(b, states, stateCount)) return false;
        for (const uint16_t w : b.windows)
            if (states[w] == kShut) return false;
        return true;
    }

    void OpenBits(const BatchWindows& b, const SeeWindow* windows, const uint8_t* states, size_t count, float& first, uint32_t& bits,
                  const float* camLocal)
    {
        first = 0.0f;
        bits = 0;
        if (!windows || !states) return;
        // the open windows' ordinals, and the one nearest the camera
        static thread_local std::vector<uint32_t> open;
        open.clear();
        bool haveNear = false;
        uint32_t nearest = 0;
        float best = 0.0f;
        const bool cam = camLocal && Finite3(camLocal);
        for (const uint16_t w : b.windows)
        {
            if (w >= count || states[w] != kOpened) continue;
            open.push_back(windows[w].ordinal);
            if (!cam) continue;
            const float d[3] = { camLocal[0] - windows[w].center[0], camLocal[1] - windows[w].center[1], camLocal[2] - windows[w].center[2] };
            const float dd = Dot(d, d);
            if (!haveNear || dd < best || (dd == best && windows[w].ordinal < nearest)) { best = dd; nearest = windows[w].ordinal; haveNear = true; }
        }
        if (open.empty()) return;
        std::sort(open.begin(), open.end());
        open.erase(std::unique(open.begin(), open.end()), open.end());
        // the first: an open ordinal (taking in the nearest one when there is a camera) whose sixteen hold the most open windows
        uint32_t lo = open.front();
        size_t most = 0;
        for (size_t i = 0; i < open.size(); ++i)
        {
            const uint32_t from = open[i];
            if (haveNear && (from > nearest || nearest - from >= kOpenBits)) continue;
            size_t n = 0;
            for (size_t j = i; j < open.size() && open[j] - from < kOpenBits; ++j) ++n;
            if (n > most) { most = n; lo = from; }
        }
        first = static_cast<float>(lo);
        for (const uint16_t w : b.windows)
            if (w < count && states[w] == kOpened && windows[w].ordinal - lo < kOpenBits) bits |= 1u << (windows[w].ordinal - lo);
    }

    bool DrawArgsOf(const uint32_t batch[4], uint32_t baseVertexMode, uint32_t streamOffset, uint32_t streamStride, uint32_t indexOffset,
                    uint32_t d3dPrimitive, DrawArgs& out)
    {
        const uint32_t primitive = batch[0], start = batch[1], count = batch[2];
        const uint32_t minIndex = batch[3] & 0xFFFFu, maxIndex = batch[3] >> 16;
        if (primitive != client::kPrimTriangles || d3dPrimitive != client::kD3dTriangleList) return false;
        if (count < 3 || count % 3 || maxIndex < minIndex) return false;
        if (!baseVertexMode && !streamStride) return false;
        out.primitive = d3dPrimitive;
        out.baseVertex = baseVertexMode ? 0u : streamOffset / streamStride;
        out.minIndex = minIndex;
        out.vertices = maxIndex - minIndex + 1;
        out.startIndex = start + (indexOffset >> 1);
        out.primitives = count / 3;
        return true;
    }

    uint32_t BlendFactor(float opacity)
    {
        const float o = std::isfinite(opacity) ? std::clamp(opacity, 0.0f, 1.0f) : 1.0f;
        const uint32_t a = static_cast<uint32_t>(std::lround(o * 255.0f));
        return (a << 24) | (a << 16) | (a << 8) | a;
    }

    void ReplayConstants(float opacity, bool alphaTest, uint32_t alphaFunc, uint32_t alphaRef, float first, uint32_t bits, float out[4])
    {
        const float o = std::isfinite(opacity) ? std::clamp(opacity, 0.0f, 1.0f) : 1.0f;
        out[0] = 1.0f - o;
        const float ref = static_cast<float>(std::min<uint32_t>(alphaRef, 255u));
        out[1] = !alphaTest ? -1.0f
               : alphaFunc == kD3dCmpGreaterEqual ? ref / 255.0f
               : alphaFunc == kD3dCmpGreater ? (ref + 0.5f) / 255.0f
               : -1.0f;
        out[2] = std::isfinite(first) ? first : 0.0f;
        out[3] = static_cast<float>(bits & ((1u << kOpenBits) - 1u)) + 0.5f;
    }

    bool OpenBit(float ordinal, const float c219[4], float expError)
    {
        const float k = ordinal - c219[2];
        const float e = std::exp2(-(std::max(k, -1.0f) + 1.0f)) * expError;
        const float v = e * c219[3];
        return k >= 0.0f && v - std::floor(v) - 0.5f >= 0.0f;
    }

    void Deferral::BeginBatch(bool deferrable, uint32_t start, uint32_t count, float first, uint32_t bits, uint32_t material)
    {
        m_pending = deferrable;
        m_start = start;
        m_count = count;
        m_first = first;
        m_bits = bits;
        m_material = material;
        m_transFirst = false;
        m_open.clear();
        m_rest.clear();
    }

    Deferral::Draw Deferral::OnDraw(bool indexed, uint32_t primitive, uint32_t start, uint32_t count, uint32_t blendingMode)
    {
        m_why = Why::kNone;
        if (!m_pending) return Draw::Pass;
        const bool triangles = indexed && primitive == client::kPrimTriangles;
        const bool same = triangles && start == m_start && count == m_count;
        const bool full = m_entries.size() >= kMaxDeferred;
        // a transition batch's exterior-lit draw: silenced, still pending for its interior-lit one
        if (same && !full && !m_transFirst && blendingMode == client::kBlendTransExterior)
        {
            m_transFirst = true;
            ++m_silenced;
            return Draw::Silent;
        }
        m_pending = false;   // one draw a batch (a transition batch's: its second)
        const bool second = m_transFirst;
        m_transFirst = false;
        const bool blending = second ? blendingMode == client::kBlendTransInterior : blendingMode <= 1;
        if (!same || full || !blending)
        {
            m_why = !triangles ? Why::kNotTriangles : !same ? Why::kRange : full ? Why::kFull : second ? Why::kTransSecond : Why::kBlending;
            ++m_passed;
            return Draw::Pass;
        }
        m_trans = second;
        return Draw::Defer;
    }

    void Deferral::Captured(const Entry& entry)
    {
        m_entries.push_back(entry);
        m_entries.back().first = m_first;
        m_entries.back().bits = m_bits;
        m_entries.back().material = m_material;
        m_entries.back().batchStart = m_start;
        m_entries.back().batchCount = m_count;
        m_entries.back().trans = m_trans;
        m_entries.back().open = std::move(m_open);
        m_entries.back().rest = std::move(m_rest);
    }

    std::vector<Deferral::Entry> Deferral::Take()
    {
        std::vector<Entry> out;
        out.swap(m_entries);
        return out;
    }
}
