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

    bool Whitelisted(uint32_t pathHash)
    {
        if (!pathHash) return false;
        for (const char* p : kWhitelist)
            if (glass::PathHash(p) == pathHash) return true;
        return false;
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

    RootWindows BuildRoot(uint32_t path, const std::vector<glass::Window>& all, const GroupInfo* groups, size_t count)
    {
        RootWindows r;
        r.path = path;
        r.byGroup.assign(count, { 0u, 0u });
        for (const glass::Window& w : all)
        {
            if (w.path != path || !(w.flags & kWindowExterior) || w.group >= count) continue;
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
            s.interior = InteriorBehind(s.center, s.normal, groups, count);
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
            (r.windows[i].interior >= 0 ? r.real : r.fake) += 1;
        }
        return r;
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
        *this = RoomMesh{};
        if (!vertices || !indices) return false;
        for (const auto& r : ranges)
        {
            if (r.first > indexCount || r.second > indexCount - r.first || r.second % 3) { *this = RoomMesh{}; return false; }
            for (uint32_t k = r.first; k < r.first + r.second; ++k)
            {
                const uint32_t i = indices[k];
                if (i >= vertexCount) { *this = RoomMesh{}; return false; }
                for (int c = 0; c < 3; ++c) m_tris.push_back(vertices[3 * static_cast<size_t>(i) + c]);
            }
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

    RoomMesh::Hit RoomMesh::First(const float o[3], const float d[3], float tmin, float tmax, float* tOut) const
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
        for (uint32_t i = 0; i < count; ++i)
        {
            const SeeWindow& w = windows[i];
            WindowPlan& p = out[i];
            if (!Facing(w, camLocal)) continue;
            float quad[4][3];
            Corners(w, kQuadGrow, quad);
            Rect screen, clip;
            if (!ScreenRect(quad, 4, model, eye, viewProj, screen) || !Overlap(screen, walk, clip)) continue;
            p.clip = clip;
            p.state = kShut;
            if (w.interior >= 0 && inRange && budget > 0)
            {
                p.candidate = true;
                --budget;
            }
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

    bool DeferrableWhole(const BatchWindows& b, const uint8_t* states, size_t stateCount)
    {
        if (!Deferrable(b, states, stateCount)) return false;
        for (const uint16_t w : b.windows)
            if (states[w] == kShut) return false;
        return true;
    }

    void OpenBits(const BatchWindows& b, const SeeWindow* windows, const uint8_t* states, size_t count, float& first, uint32_t& bits)
    {
        first = 0.0f;
        bits = 0;
        if (!windows || !states) return;
        bool any = false;
        uint32_t lo = 0;
        for (const uint16_t w : b.windows)
            if (w < count && states[w] == kOpened && (!any || windows[w].ordinal < lo))
            {
                lo = windows[w].ordinal;
                any = true;
            }
        if (!any) return;
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
        m_open.clear();
        m_rest.clear();
    }

    Deferral::Draw Deferral::OnDraw(bool indexed, uint32_t primitive, uint32_t start, uint32_t count, uint32_t blendingMode)
    {
        if (!m_pending) return Draw::Pass;
        m_pending = false;   // one draw a batch
        if (!indexed || primitive != client::kPrimTriangles || start != m_start || count != m_count || blendingMode > 1 ||
            m_entries.size() >= kMaxDeferred)
        {
            ++m_passed;
            return Draw::Pass;
        }
        return Draw::Defer;
    }

    void Deferral::Captured(const Entry& entry)
    {
        m_entries.push_back(entry);
        m_entries.back().first = m_first;
        m_entries.back().bits = m_bits;
        m_entries.back().material = m_material;
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
