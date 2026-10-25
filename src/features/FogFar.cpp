#include "FogFar.hpp"

#include "../env/Fog.hpp"
#include "../env/Regional.hpp"
#include "../env/TerrainHeight.hpp"

#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace wxl_livingazeroth::fogfar
{
    namespace
    {
        constexpr int   kGroundPerFrame = 96;    // chunks whose ground is sampled per frame (5 heights each)
        constexpr ULONGLONG kRebuildMs = 500;     // rebuilds at most twice a second (zones change slowly)
        constexpr float kNoTop = -1.0e5f;
        constexpr int   kFarKinds[] = { fog::kMist, fog::kAreaFog, fog::kHaze }; // the zone-wide kinds

        struct Chunk { int i = 0x7FFFFFFF, j = 0; bool groundKnown = false; float ground = 0.0f; };
        std::vector<Chunk> g_chunks;
        int       g_size = 0, g_cursor = 0;
        int       g_firstI = 0x7FFFFFFF, g_firstJ = 0;
        ULONGLONG g_lastBuild = 0;
        bool      g_any = false;
        float     g_highestTop = -1.0e9f;
        float     g_chunk = 0.0f;
        std::vector<float> g_topWorld; // per slot: the dilated top (world z) as uploaded, kNoTop for none

        IDirect3DTexture9* g_field = nullptr;
        IDirect3DTexture9* g_colour = nullptr;
        IDirect3DDevice9*  g_device = nullptr;

        uint16_t ToHalf(float f)
        {
            uint32_t x;
            std::memcpy(&x, &f, 4);
            const uint32_t sign = (x >> 16) & 0x8000u;
            const int exp = static_cast<int>((x >> 23) & 0xFF) - 127 + 15;
            const uint32_t mant = x & 0x7FFFFFu;
            if (exp <= 0) return static_cast<uint16_t>(sign);
            if (exp >= 31) return static_cast<uint16_t>(sign | 0x7BFFu);
            const uint32_t m = (mant + 0x1000u) >> 13;
            return static_cast<uint16_t>(sign | (exp << 10) | (m > 0x3FFu ? 0x3FFu : m));
        }

        int Mod(int v) { const int m = v % g_size; return m < 0 ? m + g_size : m; }

        // A chunk's fog: its light layer's density per yd at the floor, thickness, colour.
        struct ZoneLook { float density = 0.0f, thickness = 1.0f, rgb[3] = {}; };

        /// Where a chunk overlaps the near field (env/Fog), its started cells' real fog, averaged (all
        /// kinds, amount x density as the far map counts them), replaces the zone's typical fog by
        /// the share of the chunk they cover: at the handover the far map then shows what the near
        /// field shows (the zone's typical fog there made a seam). Colours per kind and area cached.
        void BlendNear(int i, int j, float chunk, ColourFn colour, ZoneLook& look)
        {
            const fog::Grid& g = fog::Current();
            if (!g.amount[0] || !g.groundKnown || !g.area) return;
            constexpr int N = fog::kGridSize;
            const int c0 = static_cast<int>(std::floor(i * chunk / fog::kCellSize)), c1 = static_cast<int>(std::floor((i + 1) * chunk / fog::kCellSize));
            const int r0 = static_cast<int>(std::floor(j * chunk / fog::kCellSize)), r1 = static_cast<int>(std::floor((j + 1) * chunk / fog::kCellSize));
            if (c1 <= g.firstI || c0 >= g.firstI + N || r1 <= g.firstJ || r0 >= g.firstJ + N) return; // no overlap
            static std::unordered_map<uint64_t, std::array<float, 3>> colours; // (kind, area) -> colour, per rebuild
            static ULONGLONG coloursAt = 0;
            const ULONGLONG now = GetTickCount64();
            if (now != coloursAt) { colours.clear(); coloursAt = now; }
            auto wrap = [](int v) { const int m = v % N; return m < 0 ? m + N : m; };
            // Every 2nd cell each way: plenty for a 33 yd average, a quarter of the work (every cell, with
            // the blended settings looked up per kind, cost ~0.4 ms per rebuild).
            int total = 0, count = 0;
            float sumD = 0.0f, sumW = 0.0f, rgb[3] = {};
            for (int r = r0; r < r1; r += 2)
                for (int c = c0; c < c1; c += 2)
                {
                    ++total;
                    if (c < g.firstI || c >= g.firstI + N || r < g.firstJ || r >= g.firstJ + N) continue;
                    const int index = wrap(r) * N + wrap(c);
                    if (!g.groundKnown[index] || (g.fresh && g.fresh[index])) continue;
                    ++count;
                    for (int k = 0; k < fog::kKindCount; ++k)
                    {
                        const fog::KindSettings s = fog::SettingsAt(k, index);
                        if (!s.enabled) continue;
                        const float d = g.amount[k][index] * s.density;
                        if (d <= 0.0f) continue;
                        sumD += d;
                        sumW += d * s.thickness;
                        const uint64_t key = (static_cast<uint64_t>(g.area[index]) << 8) | static_cast<uint64_t>(k);
                        auto found = colours.find(key);
                        if (found == colours.end())
                        {
                            std::array<float, 3> cl{};
                            colour(k, g.area[index], cl.data());
                            found = colours.emplace(key, cl).first;
                        }
                        for (int ch = 0; ch < 3; ++ch) rgb[ch] += d * found->second[ch];
                    }
                }
            if (!count || !total) return;
            const float cover = static_cast<float>(count) / total;
            const float nearD = sumD / count;
            const float dz = look.density * (1.0f - cover), dn = nearD * cover;
            const float d = dz + dn;
            if (d <= 0.0f) { look.density = 0.0f; return; }
            const float nearT = sumD > 0.0f ? sumW / sumD : look.thickness;
            look.thickness = (dz * look.thickness + dn * nearT) / d;
            for (int ch = 0; ch < 3; ++ch)
            {
                const float nearC = sumD > 0.0f ? rgb[ch] / sumD : look.rgb[ch];
                look.rgb[ch] = (dz * look.rgb[ch] + dn * nearC) / d;
            }
            look.density = d;
        }

        /// A chunk's lowest ground: its centre and four points halfway to its corners (the layer
        /// lies on the low parts, so it doesn't float over dips).
        bool LowestGround(int i, int j, float cell, float& out)
        {
            static const float kAt[5][2] = { { 0.5f, 0.5f }, { 0.25f, 0.25f }, { 0.75f, 0.25f }, { 0.25f, 0.75f }, { 0.75f, 0.75f } };
            bool any = false;
            for (const auto& p : kAt)
            {
                float z;
                float liquid = 0.0f;
                uint32_t type = 0;
                const float x = (i + p[0]) * cell, y = (j + p[1]) * cell;
                if (!terrain::HeightAt(x, y, z)) continue;
                if (terrain::LiquidAt(x, y, liquid, type) && liquid > z) z = liquid; // fog lies on water, as near
                out = any ? std::fmin(out, z) : z;
                any = true;
            }
            return any;
        }
    }

    namespace
    {
        // The map as built and as shown. Built at most twice a second (BlendNear, the near field's fog folded in, is
        // most of its ~0.4 ms), sooner when new ground comes in; shown values ease toward the built ones every frame,
        // so a chunk glides from one look to the next instead of stepping where the near field blends in, and a chunk
        // whose fog arrives late fades in ("squares", the user, 2026-10-15). Per slot (texel), world units.
        struct Look { float density = 0.0f, thickness = 1.0f, floor = 0.0f, top = kNoTop, rgb[3] = {}; };
        struct Shown : Look { int i = 0x7FFFFFFF, j = 0; };   // + the world chunk it shows
        std::vector<Look>  g_built;
        std::vector<Shown> g_shown;
        bool      g_haveBuilt = false;
        bool      g_newGround = false;      // a chunk's ground came in since the last build
        bool      g_upload = true;          // the shown map changed since it was uploaded
        float     g_uploadedRef = -1.0e9f;
        ULONGLONG g_lastEase = 0;
        constexpr ULONGLONG kRebuildSoonMs = 125; // new ground: built again this soon
        constexpr float kEaseSeconds = 0.25f;     // ~86% of a change shown within half a second

        void Build(const regional::ZoneMap& map, ColourFn colour)
        {
            // Per zone (a handful around): its fog's light-layer density, thickness and colour.
            std::unordered_map<uint32_t, ZoneLook> zones;
            auto lookOf = [&](uint32_t zone) -> const ZoneLook& {
                auto found = zones.find(zone);
                if (found != zones.end()) return found->second;
                ZoneLook l;
                float weighted = 0.0f, mix[3] = {};
                for (int k : kFarKinds)
                {
                    const fog::KindSettings& s = fog::SettingsOfArea(k, zone);
                    if (!s.enabled) continue;
                    const float d = fog::ZoneAmount(zone, k) * s.density;
                    if (d <= 0.0f) continue;
                    float c[3];
                    colour(k, zone, c);
                    l.density += d;
                    weighted += d * s.thickness;
                    for (int ch = 0; ch < 3; ++ch) mix[ch] += d * c[ch];
                }
                if (l.density > 0.0f)
                {
                    l.thickness = weighted / l.density;
                    for (int ch = 0; ch < 3; ++ch) l.rgb[ch] = mix[ch] / l.density;
                }
                return zones.emplace(zone, l).first->second;
            };

            const int N = g_size;
            auto inWindow = [&](int i, int j) { return i >= g_firstI && i < g_firstI + N && j >= g_firstJ && j < g_firstJ + N; };
            auto chunkAt = [&](int i, int j) -> const Chunk* {   // a world chunk whose ground is read
                if (!inWindow(i, j)) return nullptr;
                const Chunk& n = g_chunks[Mod(j) * N + Mod(i)];
                return n.i == i && n.j == j && n.groundKnown ? &n : nullptr;
            };
            static std::vector<float> top, ground;
            static std::vector<uint8_t> have;
            static std::vector<ZoneLook> looks;
            top.assign(static_cast<size_t>(N) * N, kNoTop);
            ground.assign(static_cast<size_t>(N) * N, 0.0f);
            have.assign(static_cast<size_t>(N) * N, 0);
            looks.assign(static_cast<size_t>(N) * N, ZoneLook{});
            for (int r = 0; r < N; ++r)
                for (int c = 0; c < N; ++c)
                {
                    const int i = g_firstI + c, j = g_firstJ + r;           // world chunk (texel = its slot)
                    const int slot = Mod(j) * N + Mod(i);
                    uint32_t zone = map.zone[slot];
                    float g = 0.0f;
                    if (const Chunk* own = chunkAt(i, j))
                        g = own->ground;
                    else
                    {
                        // Not read yet: on the lowest of its read neighbours' ground (in their zone if its own isn't
                        // known), so where its terrain comes in it's in fog already. A chunk's ground is read up to
                        // ~0.7 s after it loads (96 a frame), and its fog came in at the next build: freshly loaded
                        // terrain stood clear, then its fog arrived a chunk at a time.
                        float low = 1.0e9f;
                        for (int dj = -1; dj <= 1; ++dj)
                            for (int di = -1; di <= 1; ++di)
                            {
                                const Chunk* n = (di || dj) ? chunkAt(i + di, j + dj) : nullptr;
                                if (!n) continue;
                                low = std::fmin(low, n->ground);
                                if (!zone) zone = map.zone[Mod(j + dj) * N + Mod(i + di)];
                            }
                        if (low > 1.0e8f) continue;
                        g = low;
                    }
                    have[slot] = 1;
                    ground[slot] = g;
                    ZoneLook l = zone ? lookOf(zone) : ZoneLook{};
                    BlendNear(i, j, map.cellSize, colour, l);
                    if (l.density <= 1.0e-5f) continue;
                    looks[slot] = l;
                    top[slot] = g + l.thickness * 4.0f;
                }
            for (int r = 0; r < N; ++r)
                for (int c = 0; c < N; ++c)
                {
                    const int slot = r * N + c; // texel (c, r) holds slot (c, r): the zone map's own layout
                    const int i = g_firstI + Mod(c - g_firstI), j = g_firstJ + Mod(r - g_firstJ); // its world chunk
                    float most = kNoTop;
                    for (int dr = -1; dr <= 1; ++dr)
                        for (int dc = -1; dc <= 1; ++dc)
                            most = std::fmax(most, top[Mod(r + dr) * N + Mod(c + dc)]);
                    // The layer lies on the lowest ground among the chunk and its 8 neighbours (~100 yd),
                    // like the near field's fog floor: level in valleys, cliffs rising out of it. On each
                    // chunk's own lowest ground the floor jumped tens of yards between chunks on cliffs,
                    // and the march's shells around the camera cut it into flickering rings.
                    float low = 1.0e9f;
                    for (int dj = -1; dj <= 1; ++dj)
                        for (int di = -1; di <= 1; ++di)
                        {
                            if (!inWindow(i + di, j + dj)) continue;
                            const int n = Mod(j + dj) * N + Mod(i + di);
                            if (have[n]) low = std::fmin(low, ground[n]);
                        }
                    // Chunks without fog take their foggy neighbour's colour (the filter fades the
                    // density to them), on the same low floor.
                    const ZoneLook& l = looks[slot];
                    const ZoneLook* colourFrom = &l;
                    if (l.density <= 1.0e-5f)
                        for (int dr = -1; dr <= 1 && colourFrom == &l; ++dr)
                            for (int dc = -1; dc <= 1; ++dc)
                            {
                                const int n = Mod(r + dr) * N + Mod(c + dc);
                                if (looks[n].density > 1.0e-5f) { colourFrom = &looks[n]; break; }
                            }
                    Look& b = g_built[slot];
                    b.density = l.density;
                    b.thickness = colourFrom->thickness;
                    b.floor = low < 1.0e8f ? low : ground[slot];
                    b.top = most;
                    for (int ch = 0; ch < 3; ++ch) b.rgb[ch] = colourFrom->rgb[ch];
                    // A slot that now shows another chunk (the window moved; the first build) takes its look at once.
                    Shown& s = g_shown[slot];
                    if (s.i != i || s.j != j)
                    {
                        static_cast<Look&>(s) = b;
                        s.i = i;
                        s.j = j;
                        g_upload = true;
                    }
                }
            g_haveBuilt = true;
        }

        void Ease(ULONGLONG now)
        {
            const float dt = g_lastEase ? std::fmin(static_cast<float>(now - g_lastEase) / 1000.0f, 0.5f) : 0.0f;
            g_lastEase = now;
            if (dt <= 0.0f) return;
            const float k = 1.0f - std::exp(-dt / kEaseSeconds), kTop = 1.0f - std::exp(-dt / (2.0f * kEaseSeconds));
            bool moving = false;
            auto toward = [&](float& v, float target, float rate, float close) {
                const float d = target - v;
                if (d == 0.0f) return;
                v = std::fabs(d) <= close ? target : v + d * rate;
                moving = true;
            };
            for (size_t n = 0; n < g_shown.size(); ++n)
            {
                Shown& s = g_shown[n];
                const Look& b = g_built[n];
                toward(s.density, b.density, k, 1.0e-5f);
                toward(s.thickness, b.thickness, k, 0.01f);
                toward(s.floor, b.floor, k, 0.02f);
                for (int ch = 0; ch < 3; ++ch) toward(s.rgb[ch], b.rgb[ch], k, 0.002f);
                // The top bounds the fog shown (the march skips the air above it): up at once, down slower than the
                // fog under it, gone only once that fog is.
                if (b.top > s.top) { s.top = b.top; moving = true; }
                else if (b.top > kNoTop) toward(s.top, b.top, kTop, 0.05f);
                else if (s.top > kNoTop && s.density <= 1.0e-5f) { s.top = kNoTop; moving = true; }
            }
            if (moving) g_upload = true;
        }

        bool UploadShown(float groundRef)
        {
            D3DLOCKED_RECT lf{}, lc{};
            if (FAILED(g_field->LockRect(0, &lf, nullptr, 0))) return false;
            if (FAILED(g_colour->LockRect(0, &lc, nullptr, 0))) { g_field->UnlockRect(0); return false; }
            auto byteOf = [](float v) { v *= 0.5f; return static_cast<uint8_t>((v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v)) * 255.0f + 0.5f); };
            const int N = g_size;
            g_any = false;
            g_highestTop = -1.0e9f;
            g_topWorld.assign(static_cast<size_t>(N) * N, kNoTop);
            for (int r = 0; r < N; ++r)
            {
                auto* texel = reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(lf.pBits) + r * lf.Pitch);
                auto* rgb = static_cast<uint8_t*>(lc.pBits) + r * lc.Pitch;
                for (int c = 0; c < N; ++c, texel += 4, rgb += 4)
                {
                    const int slot = r * N + c;
                    const Shown& s = g_shown[slot];
                    texel[0] = ToHalf(s.density);
                    texel[1] = ToHalf(s.thickness);
                    texel[2] = ToHalf(s.floor - groundRef);
                    texel[3] = ToHalf(s.top > kNoTop ? s.top - groundRef : kNoTop);
                    g_topWorld[slot] = s.top;
                    rgb[0] = byteOf(s.rgb[2]);
                    rgb[1] = byteOf(s.rgb[1]);
                    rgb[2] = byteOf(s.rgb[0]);
                    rgb[3] = 255;
                    if (s.density > 1.0e-5f)
                    {
                        g_any = true;
                        if (s.top > g_highestTop) g_highestTop = s.top;
                    }
                }
            }
            g_field->UnlockRect(0);
            g_colour->UnlockRect(0);
            g_uploadedRef = groundRef;
            g_upload = false;
            return true;
        }
    }

    bool Update(IDirect3DDevice9* dev, float groundRef, ColourFn colour, Maps& out)
    {
        const regional::ZoneMap& map = regional::Map();
        if (!map.zone || map.size <= 0) return false;
        if (dev != g_device) { Release(); g_device = dev; }
        if (g_size != map.size)
        {
            g_size = map.size;
            const size_t slots = static_cast<size_t>(g_size) * g_size;
            g_chunks.assign(slots, Chunk{});
            g_built.assign(slots, Look{});
            g_shown.assign(slots, Shown{});
            g_haveBuilt = false;
        }

        // Ground: chunks that now hold another world chunk, or whose terrain wasn't loaded yet.
        int sampled = 0;
        for (int n = 0; n < g_size * g_size && sampled < kGroundPerFrame; ++n)
        {
            const int k = g_cursor;
            g_cursor = (g_cursor + 1) % (g_size * g_size);
            const int i = map.firstI + k % g_size, j = map.firstJ + k / g_size;
            Chunk& c = g_chunks[Mod(j) * g_size + Mod(i)];
            if (c.i == i && c.j == j && c.groundKnown) continue;
            c.i = i; c.j = j;
            c.groundKnown = LowestGround(i, j, map.cellSize, c.ground);
            if (c.groundKnown) g_newGround = true;
            ++sampled;
        }

        // Built when the window moved, twice a second, and soon after new ground (heights are kept in world units:
        // a change of the reference height only uploads again).
        const bool moved = map.firstI != g_firstI || map.firstJ != g_firstJ;
        const ULONGLONG now = GetTickCount64();
        if (!g_haveBuilt || moved || now - g_lastBuild >= kRebuildMs || (g_newGround && now - g_lastBuild >= kRebuildSoonMs))
        {
            g_firstI = map.firstI; g_firstJ = map.firstJ;
            g_lastBuild = now;
            g_newGround = false;
            g_chunk = map.cellSize;
            Build(map, colour);
        }
        Ease(now);

        bool created = false;
        if (!g_field)
        {
            if (FAILED(dev->CreateTexture(g_size, g_size, 1, 0, D3DFMT_A16B16G16R16F, D3DPOOL_MANAGED, &g_field, nullptr))) { g_field = nullptr; return false; }
            created = true;
        }
        if (!g_colour)
        {
            if (FAILED(dev->CreateTexture(g_size, g_size, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_colour, nullptr))) { g_colour = nullptr; return false; }
            created = true;
        }
        if ((created || g_upload || groundRef != g_uploadedRef) && !UploadShown(groundRef)) return false;
        if (!g_any) return false;

        out.field = g_field;
        out.colour = g_colour;
        // The texture wraps like the zone map: texel (Mod(i), Mod(j)) holds world chunk (i, j), so
        // world x maps to u = x / (size x chunk) with wrapping; the valid window is firstI .. +size.
        out.originX = g_firstI * map.cellSize;
        out.originY = g_firstJ * map.cellSize;
        out.invExtent = 1.0f / (g_size * map.cellSize);
        out.highestTop = g_highestTop;
        return true;
    }

    float TopAt(float x, float y)
    {
        if (!g_any || g_chunk <= 0.0f || g_topWorld.empty()) return kNoTop;
        const int i = static_cast<int>(std::floor(x / g_chunk)), j = static_cast<int>(std::floor(y / g_chunk));
        if (i < g_firstI || i >= g_firstI + g_size || j < g_firstJ || j >= g_firstJ + g_size) return kNoTop;
        return g_topWorld[Mod(j) * g_size + Mod(i)];
    }

    void Release()
    {
        if (g_field) { g_field->Release(); g_field = nullptr; }
        if (g_colour) { g_colour->Release(); g_colour = nullptr; }
        g_lastBuild = 0;
    }
}

namespace wxl_livingazeroth::fogfar
{
    void GpuMemory(memuse::Gpu& out)
    {
        out.Add(g_field);
        out.Add(g_colour);
    }
}
