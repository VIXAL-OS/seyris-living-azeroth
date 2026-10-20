#include "SeeThroughModel.hpp"

#include "GlassModel.hpp"

#include <algorithm>
#include <cmath>

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

    bool Deferrable(const BatchWindows& b, const uint8_t* states, size_t stateCount)
    {
        if (!b.valid || b.windows.empty() || !(b.strayArea <= kTrimArea) || !states) return false;
        bool opened = false;
        for (const uint16_t w : b.windows)
        {
            if (w >= stateCount || states[w] == kShut) return false;
            opened = opened || states[w] == kOpened;
        }
        return opened;
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

    void Deferral::BeginBatch(bool deferrable, uint32_t start, uint32_t count)
    {
        m_pending = deferrable;
        m_start = start;
        m_count = count;
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

    void Deferral::Captured(const Entry& entry) { m_entries.push_back(entry); }

    std::vector<Deferral::Entry> Deferral::Take()
    {
        std::vector<Entry> out;
        out.swap(m_entries);
        return out;
    }
}
