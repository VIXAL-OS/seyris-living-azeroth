#include "WindowModel.hpp"

#include <algorithm>
#include <cmath>

namespace wxl_livingazeroth::windows
{
    float NightFade(float t)
    {
        // As 0x7ED3B0: the time clamped to 0..1; the first key at or after it is the next one (none: the first, the
        // keys wrap round midnight), the one before it the previous; linear between them, the gap measured forward.
        constexpr int n = static_cast<int>(sizeof(kNightKeys) / sizeof(kNightKeys[0]));
        if (!(t >= 0.0f)) t = 0.0f; // also NaN
        if (t > 1.0f) t = 1.0f;
        int next = 0;
        while (next < n && t > kNightKeys[next].time) ++next;
        int prev;
        if (next == n) { next = 0; prev = n - 1; }
        else prev = next == 0 ? n - 1 : next - 1;
        float span = kNightKeys[next].time - kNightKeys[prev].time;
        if (std::fabs(span) < 0.001f) return kNightKeys[prev].value;
        if (span < 0.0f) span += 1.0f;
        float dt = t - kNightKeys[prev].time;
        if (dt < 0.0f) dt += 1.0f;
        return kNightKeys[prev].value + (kNightKeys[next].value - kNightKeys[prev].value) * (dt / span);
    }

    uint64_t Index::Key(uint32_t map, int tx, int ty)
    {
        return (static_cast<uint64_t>(map & 0xFFFF) << 48) | (static_cast<uint64_t>(static_cast<uint32_t>(tx + 0x800000) & 0xFFFFFF) << 24) |
               static_cast<uint64_t>(static_cast<uint32_t>(ty + 0x800000) & 0xFFFFFF);
    }

    void Index::Build(std::vector<Window> windows)
    {
        m_windows = std::move(windows);
        m_tiles.clear();
        for (uint32_t i = 0; i < m_windows.size(); ++i)
        {
            const Window& w = m_windows[i];
            if (!std::isfinite(w.pos[0]) || !std::isfinite(w.pos[1]) || !std::isfinite(w.pos[2])) continue;
            m_tiles[Key(w.map, static_cast<int>(std::floor(w.pos[0] / kTile)), static_cast<int>(std::floor(w.pos[1] / kTile)))].push_back(i);
        }
    }

    void Index::Near(uint32_t map, const float c[3], float range, bool glowOnly, size_t max, std::vector<const Window*>& out) const
    {
        out.clear();
        if (!(range > 0.0f) || !max || m_windows.empty()) return;
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
                    const Window& w = m_windows[i];
                    if ((w.flags & kGlowOnly) && !glowOnly) continue;
                    const float dx = w.pos[0] - c[0], dy = w.pos[1] - c[1], dz = w.pos[2] - c[2];
                    const float d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 <= r2) found.push_back({ d2, i });
                }
            }
        // Nearest first; ties by row, so the choice is the same every frame.
        if (found.size() > max)
        {
            std::nth_element(found.begin(), found.begin() + max, found.end());
            found.resize(max);
        }
        std::sort(found.begin(), found.end());
        for (const auto& f : found) out.push_back(&m_windows[f.second]);
    }

    void SpotDirection(const float facing[3], float out[3])
    {
        float d[3] = { facing[0], facing[1], facing[2] - kTilt };
        const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len < 1e-5f) { out[0] = 0.0f; out[1] = 0.0f; out[2] = -1.0f; return; }
        for (int k = 0; k < 3; ++k) out[k] = d[k] / len;
    }

    void GlassCorners(const Window& w, float margin, float out[4][3])
    {
        // Along the facade: up x facing (horizontal); up it: facing x along.
        float u[3] = { -w.facing[1], w.facing[0], 0.0f };
        float ul = std::sqrt(u[0] * u[0] + u[1] * u[1]);
        if (ul < 1e-5f) { u[0] = 1.0f; u[1] = 0.0f; ul = 1.0f; }
        u[0] /= ul; u[1] /= ul;
        const float v[3] = { w.facing[1] * u[2] - w.facing[2] * u[1], w.facing[2] * u[0] - w.facing[0] * u[2], w.facing[0] * u[1] - w.facing[1] * u[0] };
        const float hw = w.halfWidth + margin, hh = w.halfHeight + margin;
        const float mid[3] = { w.pos[0] - w.facing[0] * w.back, w.pos[1] - w.facing[1] * w.back, w.pos[2] - w.facing[2] * w.back };
        const float su[4] = { -1.0f, 1.0f, -1.0f, 1.0f }, sv[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 3; ++k) out[c][k] = mid[k] + u[k] * hw * su[c] + v[k] * hh * sv[c];
    }

    unsigned PickCell(const CellEntry* e, size_t n, int perCell, int windowCap, uint16_t* out, unsigned& windowsLeftOut)
    {
        unsigned taken = 0;
        int windows = 0;
        for (size_t k = 0; k < n && taken < static_cast<unsigned>(std::max(perCell, 0)); ++k)
        {
            if (e[k].window)
            {
                if (windows >= windowCap) { ++windowsLeftOut; continue; }
                ++windows;
            }
            out[taken++] = e[k].light;
        }
        return taken;
    }
}
