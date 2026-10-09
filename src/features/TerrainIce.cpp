#include "TerrainIce.hpp"

#include "GroundMaterialTable.hpp"
#include "TerrainWetness.hpp"
#include "../env/TerrainHeight.hpp"
#include "../env/WorldQuery.hpp"

#include "game/Camera.hpp"
#include "game/Gfx.hpp"
#include "game/Gx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <regex>
#include <unordered_map>

namespace wxl_livingazeroth::terrainice
{
    namespace
    {
        namespace gx = wxl::game::gx;

        // The terrain draw node handed to the per-chunk draw (core offsets/game/ADT.hpp RenderNode):
        // layer count, the chunk object behind it, and its layer records (MCLY texture index at +0x08).
        constexpr size_t kNodeLayerCount = 0x09, kNodeChunk = 0x10, kNodeLayers = 0x34, kLayerStride = 0x14, kLayerTexId = 0x08;

        // PS constants: c215 the chunk's per-layer ice mask; c216 (camera, on x strength); c217 (sun
        // direction, world, toward the sun; glint exponent); c218 (glint colour; reflection strength);
        // c219 (deep body colour; clarity); c220 (depths 1, 2 in yd; detail repeats per yd 1, 2);
        // c221 (crack/bubble highlight colour; sky reflection brightness); c222 (debug tint).
        constexpr int   kMaskConstant = 215, kFirstConstant = 216, kConstantCount = 7;
        constexpr DWORD kSampler = 14;
        constexpr int   kDetailSize = 256, kDetailLevels = 9;

        using ChunkDrawFn = void(__fastcall*)(void* node, void* edx);
        ChunkDrawFn g_origDraw = nullptr;
        bool  g_hooked = false;

        bool  g_enabled = true;
        bool  g_debug = false;
        float g_strength = 1.0f;
        float g_clarity = 0.55f;

        // This terrain stage.
        bool  g_stageOn = false;
        bool  g_needSun = false;
        float g_view[16] = {};
        float g_camera[3] = {};
        bool  g_haveView = false;
        float g_sun[3] = { 0.3f, 0.2f, 0.93f };   // world, toward the sun (until the first reading)
        float g_sunView[3] = { 0.3f, 0.6f, -0.74f }; // the same in view space (the M2 shaders' space)
        float g_sunColour[3] = { 1.0f, 0.95f, 0.85f };
        unsigned g_chunks = 0, g_iceChunks = 0, g_lastChunks = 0, g_lastIceChunks = 0;

        // Variants (shader creation).
        unsigned g_withIce = 0, g_withoutIce = 0;
        std::string g_withoutReasons;

        // Materials.
        uint32_t g_generation = 0;
        bool     g_anyIce = false;
        struct CacheEntry { std::string name; uint32_t area = 0; uint32_t effect = 0; int terrain = -1; int map = -1; uint32_t generation = 0; bool ice = false; };
        std::unordered_map<const char*, CacheEntry> g_cache;

        IDirect3DTexture9* g_detail = nullptr;
        IDirect3DDevice9*  g_detailDevice = nullptr;
        bool               g_detailFailed = false;

        std::string g_status;

        bool IsIce(const terrain::Surface& s, int map)
        {
            if (!s.texture) return false;
            CacheEntry& e = g_cache[s.texture];
            if (e.generation == g_generation && e.area == s.area && e.effect == s.groundEffect && e.terrain == s.terrainType &&
                e.map == map && e.name == s.texture)
                return e.ice;
            e.name = s.texture; e.area = s.area; e.effect = s.groundEffect; e.terrain = s.terrainType; e.map = map; e.generation = g_generation;
            const uint32_t material = materials::Select(s.area, map, s.texture, s.groundEffect, s.terrainType);
            e.ice = material && (materials::Get(material).flags & materials::kFlagClearIce) != 0;
            return e.ice;
        }

        // The terrain's sun: VS c24 (view space, toward the sun) and c26 (diffuse colour), as the
        // client set them for the chunk just drawn (features/SurfaceCover reads the same), turned into
        // world space with the scene view's rotation rows.
        void ReadSun(IDirect3DDevice9* dev)
        {
            float light[12] = {};
            if (!g_haveView || FAILED(dev->GetVertexShaderConstantF(24, light, 3))) return;
            float w[3];
            for (int r = 0; r < 3; ++r) w[r] = g_view[r * 4 + 0] * light[0] + g_view[r * 4 + 1] * light[1] + g_view[r * 4 + 2] * light[2];
            const float len = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
            if (!(len > 0.01f) || !std::isfinite(len)) return;
            const float viewLen = std::sqrt(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
            for (int k = 0; k < 3; ++k)
            {
                g_sun[k] = w[k] / len;
                g_sunView[k] = light[k] / viewLen;
                g_sunColour[k] = std::clamp(light[8 + k], 0.0f, 2.0f);
            }
            if (!g_stageOn) return;
            const float c217[8] = { g_sun[0], g_sun[1], g_sun[2], 350.0f,
                                    g_sunColour[0] * 1.3f, g_sunColour[1] * 1.3f, g_sunColour[2] * 1.3f, 0.65f };
            dev->SetPixelShaderConstantF(kFirstConstant + 1, c217, 2);
        }

        void __fastcall hkChunkDraw(void* node, void* edx)
        {
            float mask[4] = {};
            bool masked = false;
            auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice());
            if (g_stageOn && node && dev)
            {
                const auto* n = static_cast<const uint8_t*>(node);
                const unsigned layers = n[kNodeLayerCount];
                const void* chunk = *reinterpret_cast<void* const*>(n + kNodeChunk);
                const int map = world::Current().mapId;
                for (unsigned i = 0; i < layers && i < 4; ++i)
                {
                    const uint32_t texId = *reinterpret_cast<const uint32_t*>(n + kNodeLayers + i * kLayerStride + kLayerTexId);
                    terrain::Surface s;
                    if (terrain::ChunkLayerSurface(chunk, texId, s) && IsIce(s, map)) { mask[i] = 1.0f; masked = true; }
                }
                ++g_chunks;
                if (masked)
                {
                    ++g_iceChunks;
                    dev->SetPixelShaderConstantF(kMaskConstant, mask, 1);
                }
            }
            g_origDraw(node, edx);
            if (masked)
            {
                const float zero[4] = {};
                dev->SetPixelShaderConstantF(kMaskConstant, zero, 1);
            }
            if (g_needSun && dev)
            {
                g_needSun = false;
                ReadSun(dev);
            }
        }

        // --- the detail texture ------------------------------------------------------------------
        uint32_t Hash(uint32_t x)
        {
            x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
            return x;
        }
        float Rand(int i, int j, uint32_t seed)
        {
            return static_cast<float>(Hash(static_cast<uint32_t>(i) * 73856093u ^ static_cast<uint32_t>(j) * 19349663u ^ seed * 83492791u) & 0xFFFFFF) / 16777216.0f;
        }
        int Wrap(int i, int p) { return ((i % p) + p) % p; }
        float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }
        float SmoothStep(float a, float b, float x) { return Smooth(std::clamp((x - a) / (b - a), 0.0f, 1.0f)); }

        // Value noise with period p lattice cells across the texture (x, y in texture units).
        float ValueNoise(float x, float y, int p, uint32_t seed)
        {
            const float fx = x * p, fy = y * p;
            const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
            const float tx = Smooth(fx - ix), ty = Smooth(fy - iy);
            const float a = Rand(Wrap(ix, p), Wrap(iy, p), seed), b = Rand(Wrap(ix + 1, p), Wrap(iy, p), seed);
            const float c = Rand(Wrap(ix, p), Wrap(iy + 1, p), seed), d = Rand(Wrap(ix + 1, p), Wrap(iy + 1, p), seed);
            return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
        }
        float Fbm(float x, float y, int period, int octaves, uint32_t seed)
        {
            float sum = 0.0f, amp = 0.5f, norm = 0.0f;
            for (int k = 0; k < octaves; ++k, amp *= 0.5f) { sum += amp * ValueNoise(x, y, period << k, seed + k); norm += amp; }
            return sum / norm;
        }
        // Distances (in cells) to the nearest and second-nearest feature point, n x n cells, tiling. The
        // points roam almost their whole cell, so the second-nearest can be two cells away: 5 x 5 search.
        void Voronoi(float x, float y, int n, uint32_t seed, float& f1, float& f2)
        {
            const float fx = x * n, fy = y * n;
            const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
            f1 = f2 = 9.0f;
            for (int dj = -2; dj <= 2; ++dj)
                for (int di = -2; di <= 2; ++di)
                {
                    const int ci = ix + di, cj = iy + dj;
                    const float px = ci + 0.05f + 0.9f * Rand(Wrap(ci, n), Wrap(cj, n), seed);
                    const float py = cj + 0.05f + 0.9f * Rand(Wrap(ci, n), Wrap(cj, n), seed + 101);
                    const float d = std::sqrt((px - fx) * (px - fx) + (py - fy) * (py - fy));
                    if (d < f1) { f2 = f1; f1 = d; } else if (d < f2) f2 = d;
                }
        }

        bool EnsureDetail(IDirect3DDevice9* dev)
        {
            if (g_detail && g_detailDevice == dev) return true;
            if (g_detailDevice == dev && g_detailFailed) return false;
            if (g_detail) { g_detail->Release(); g_detail = nullptr; }
            g_detailDevice = dev;
            g_detailFailed = true;
            std::vector<uint32_t> level;
            BuildDetail(level, kDetailSize);
            IDirect3DTexture9* tex = nullptr;
            if (FAILED(dev->CreateTexture(kDetailSize, kDetailSize, kDetailLevels, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) || !tex)
                return false;
            int size = kDetailSize;
            for (int l = 0; l < kDetailLevels; ++l)
            {
                D3DLOCKED_RECT lr;
                if (FAILED(tex->LockRect(l, &lr, nullptr, 0))) { tex->Release(); return false; }
                for (int y = 0; y < size; ++y) std::memcpy(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch, level.data() + y * size, size * 4);
                tex->UnlockRect(l);
                if (size == 1) break;
                // The next level: 2 x 2 box filter, per channel.
                const int half = size / 2;
                std::vector<uint32_t> next(static_cast<size_t>(half) * half);
                for (int y = 0; y < half; ++y)
                    for (int x = 0; x < half; ++x)
                    {
                        uint32_t out = 0;
                        for (int c = 0; c < 32; c += 8)
                        {
                            const uint32_t s = ((level[(2 * y) * size + 2 * x] >> c) & 255) + ((level[(2 * y) * size + 2 * x + 1] >> c) & 255) +
                                               ((level[(2 * y + 1) * size + 2 * x] >> c) & 255) + ((level[(2 * y + 1) * size + 2 * x + 1] >> c) & 255);
                            out |= ((s + 2) / 4) << c;
                        }
                        next[static_cast<size_t>(y) * half + x] = out;
                    }
                level.swap(next);
                size = half;
            }
            g_detail = tex;
            g_detailFailed = false;
            return true;
        }
    }

    // --- shader edits ---------------------------------------------------------------------------

    bool Prepare(std::string& src, Variant& v, std::string& why)
    {
        v = Variant{};
        if (src.find(" r25") != std::string::npos || src.find("c215") != std::string::npos || src.find(" s14") != std::string::npos ||
            src.find("c190") != std::string::npos || src.find("c194") != std::string::npos)
        { why = "r25/c190/c194/c215/s14 in use"; return false; }
        // Layer samplers s0..s(n-1) and the combined alpha map s(n); the stock shadow maps are s5..s8.
        static const std::regex dcl(R"(dcl_(2d|cube) s(\d+)\b)");
        bool declared[5] = {}, flat[5] = {};
        for (auto it = std::sregex_iterator(src.begin(), src.end(), dcl); it != std::sregex_iterator(); ++it)
        {
            const int s = std::stoi((*it)[2].str());
            if (s <= 4) { declared[s] = true; flat[s] = (*it)[1].str() == "2d"; }
        }
        int count = 0;
        while (count < 5 && declared[count]) ++count;
        for (int s = count; s < 5; ++s)
            if (declared[s]) { why = "layer samplers not contiguous"; return false; }
        if (count == 0) { why = "no layer sampler"; return false; }
        if (count == 1) { v.layers = 1; v.blend = Blend::Single; return true; }
        const int alpha = count - 1;
        if (!flat[alpha]) { why = "combined alpha map not 2d"; return false; }
        const std::regex sample("(\\n[ \\t]*)texld (r\\d+), v\\d+(\\.\\w+)?, s" + std::to_string(alpha) + "\\b[^\\n]*");
        std::smatch m;
        if (!std::regex_search(src, m, sample)) { why = "no alpha sample"; return false; }
        v.alpha = m[2].str();
        v.layers = alpha;
        const std::regex chain("lrp r\\d+(\\.\\w+)?, " + v.alpha + "\\.[xyz]\\b");
        const std::regex sum("dp3_sat r\\d+\\.\\w+, " + v.alpha + ", c\\d+");
        const bool isChain = std::regex_search(src, chain), isSum = std::regex_search(src, sum);
        if (isChain && isSum) { why = "both blend forms"; return false; }
        if (isChain) v.blend = Blend::Chain;
        else if (isSum) v.blend = Blend::Sum;
        else if (alpha == 1) v.blend = Blend::Single;
        else { why = "unknown layer blend"; return false; }
        src.insert(static_cast<size_t>(m.position(0) + m.length(0)), m[1].str() + "mov r25, " + v.alpha);
        return true;
    }

    std::string WeightBlock(const Variant& v)
    {
        std::string b;
        switch (v.blend)
        {
        case Blend::Single:
            b = "\n    mov r25.x, c215.x";
            break;
        case Blend::Chain:
            // lerp(lerp(lerp(l0, l1, a.x), l2, a.y), l3, a.z): w3 = z, w2 = y(1 - z), w1 = x(1 - y)(1 - z),
            // w0 = (1 - x)(1 - y)(1 - z); a layer the variant doesn't have contributes nothing.
            if (v.layers == 2) b += "\n    mov r25.yz, c190.y";
            if (v.layers == 3) b += "\n    mov r25.z, c190.y";
            b += "\n    add r10.xyz, c190.x, -r25"
                 "\n    mul r11.x, r25.x, r10.y"
                 "\n    mul r11.x, r11.x, r10.z"
                 "\n    mul r11.y, r25.y, r10.z"
                 "\n    mov r11.z, r25.z"
                 "\n    mul r11.w, r10.x, r10.y"
                 "\n    mul r11.w, r11.w, r10.z"
                 "\n    dp3 r12.x, r11, c215.yzww"
                 "\n    mad r25.x, r11.w, c215.x, r12.x";
            break;
        case Blend::Sum:
            // l0 x (1 - saturate(x + y + z)) + l1 x + l2 y + l3 z, as the stock does.
            b = "\n    dp3_sat r11.w, r25, c190.x"
                "\n    add r11.w, c190.x, -r11.w"
                "\n    dp3 r12.x, r25, c215.yzww"
                "\n    mad r25.x, r11.w, c215.x, r12.x";
            break;
        }
        b += "\n    mul r25.x, r25.x, c216.w"
             "\n    add r25.y, c190.x, -r25.x";
        return b;
    }

    std::string WetScale() { return "\n    mul r30.x, r30.x, r25.y"; }

    std::string LookBlock(const std::string& r)
    {
        // ps_3_0 reads at most one constant register per instruction.
        return
            // V = normalize(camera - position); the parallax step along the ground per yard of depth.
            "\n    add r10.xyz, c216, -v9"
            "\n    nrm r11.xyz, r10"
            "\n    max r12.x, r11.z, c191.w"
            "\n    rcp r12.x, r12.x"
            "\n    mul r12.xy, r11, r12.x"
            // Two layers of the detail texture, c220.x and c220.y yards down, at two scales.
            "\n    mad r13.xy, r12, -c220.x, v9"
            "\n    mul r13.xy, r13, c220.z"
            "\n    texld r14, r13, s14"
            "\n    mad r13.xy, r12, -c220.y, v9"
            "\n    mul r13.xy, r13, c220.w"
            "\n    texld r15, r13, s14"
            // (sketchy cracks, bubbles, cloud, bold cracks), the nearer layer stronger. The ground uses the
            // bold cracks (w); the ice models the sketchy ones (x).
            "\n    mul r16, r14, c192.x"
            "\n    mad r16, r15, c192.y, r16"
            // The deep body, cloudier in places; the cracks and bubbles inside it blend it toward the
            // highlight colour (at most all the way: never brighter than that).
            "\n    mul r17.x, r16.z, c192.w"
            "\n    add r17.x, r17.x, c192.z"
            "\n    mul r17.xyz, r17.x, c219"
            "\n    mul r18.x, r16.y, c193.w"
            "\n    mad_sat r18.x, r16.w, c193.z, r18.x"
            "\n    mov r20.xyz, c221"
            "\n    lrp r22.xyz, r18.x, r20, r17"
            // Lit like the surface: twice its brightness (the stock lighting is 2 x the vertex colour),
            // at most 1.3.
            "\n    dp3 r19.x, " + r + ", c191"
            "\n    mul r19.x, r19.x, c190.w"
            "\n    min r19.x, r19.x, c194.x"
            "\n    mul r17.xyz, r22, r19.x"
            // The surface texture over it, by the clarity.
            "\n    lrp r18.xyz, c219.w, r17, " + r +
            // The sky's colour reflected: Schlick's Fresnel against up, F0 0.04.
            "\n    add_sat r19.y, c190.x, -r11.z"
            "\n    mul r19.z, r19.y, r19.y"
            "\n    mul r19.z, r19.z, r19.z"
            "\n    mul r19.z, r19.z, r19.y"
            "\n    mul r19.z, r19.z, c193.y"
            "\n    add r19.z, r19.z, c193.x"
            "\n    mul r19.z, r19.z, c218.w"
            // (lrp's destination can't be its first or third source.)
            "\n    mov r20.xyz, c2"
            "\n    mul r20.xyz, r20, c221.w"
            "\n    lrp r23.xyz, r19.z, r20, r18"
            // The sun's glint (half vector against up), dimmed with the surface's own light (shadows).
            "\n    add r21.xyz, r11, c217"
            "\n    nrm r22.xyz, r21"
            "\n    max r22.z, r22.z, c190.y"
            "\n    pow r22.z, r22.z, c217.w"
            "\n    min r19.w, r19.x, c190.x"
            "\n    mul r22.z, r22.z, r19.w"
            "\n    mad r23.xyz, r22.z, c218, r23"
            // By the ice weight (none: the colour exactly as it was); the debug tint on top.
            "\n    lrp r24.xyz, r25.x, r23, " + r +
            "\n    mad " + r + ".xyz, r25.x, c222, r24";
    }

    std::string Definitions()
    {
        return "    def c190, 1, 0, 0.5, 2\n"
               "    def c191, 0.2126, 0.7152, 0.0722, 0.15\n"
               "    def c192, 0.6, 0.4, 0.35, 0.65\n"
               "    def c193, 0.04, 0.96, 0.55, 0.4\n"
               "    def c194, 1.3, 0, 0, 0\n"
               "    dcl_2d s14\n";
    }

    void NoteVariant(bool withIce, const std::string& why)
    {
        if (withIce) { ++g_withIce; return; }
        ++g_withoutIce;
        if (g_withoutReasons.find(why) == std::string::npos) g_withoutReasons += (g_withoutReasons.empty() ? "" : "; ") + why;
    }

    void BuildDetail(std::vector<uint32_t>& texels, int size)
    {
        texels.assign(static_cast<size_t>(size) * size, 0);
        std::vector<float> bubbles(static_cast<size_t>(size) * size, 0.0f);
        // Bubbles: small soft discs, mostly in loose clusters.
        for (int cluster = 0; cluster < 70; ++cluster)
        {
            const float cx = Rand(cluster, 1, 7) * size, cy = Rand(cluster, 2, 7) * size;
            const int count = 2 + static_cast<int>(Rand(cluster, 3, 7) * 7.0f);
            for (int k = 0; k < count; ++k)
            {
                const float bx = cx + (Rand(cluster, 10 + k, 8) - 0.5f) * 18.0f, by = cy + (Rand(cluster, 30 + k, 8) - 0.5f) * 18.0f;
                const float radius = 0.7f + 1.8f * Rand(cluster, 50 + k, 8) * Rand(cluster, 70 + k, 8);
                const int r = static_cast<int>(std::ceil(radius)) + 1;
                for (int dy = -r; dy <= r; ++dy)
                    for (int dx = -r; dx <= r; ++dx)
                    {
                        const int x = Wrap(static_cast<int>(std::floor(bx)) + dx, size), y = Wrap(static_cast<int>(std::floor(by)) + dy, size);
                        const float px = std::floor(bx) + dx + 0.5f - bx, py = std::floor(by) + dy + 0.5f - by;
                        const float d = std::sqrt(px * px + py * py);
                        // A bright rim and a fainter middle, like a bubble seen in ice.
                        const float disc = 1.0f - SmoothStep(radius * 0.75f, radius, d);
                        const float rim = disc * (0.55f + 0.45f * SmoothStep(radius * 0.3f, radius * 0.8f, d));
                        float& b = bubbles[static_cast<size_t>(y) * size + x];
                        b = std::max(b, rim);
                    }
            }
        }
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const float u = (x + 0.5f) / size, w = (y + 0.5f) / size;
                // Cracks: the edges between Voronoi cells at two scales, drawn thin and sketchy rather than as
                // a honeycomb (the user's word): the lookup is warped by noise so no cell wall is straight,
                // the edges are broken into fragments by finer noise, and their strength wanders along them.
                const float wu = u + 0.22f * (Fbm(u, w, 6, 4, 71) - 0.5f), ww = w + 0.22f * (Fbm(u, w, 6, 4, 83) - 0.5f);
                float a1, a2, b1, b2;
                Voronoi(wu, ww, 5, 11, a1, a2);
                Voronoi(wu, ww, 11, 23, b1, b2);
                const float wobble = Fbm(u, w, 8, 3, 31);
                const float bigKeep = SmoothStep(0.42f, 0.56f, Fbm(u, w, 16, 2, 91)) * SmoothStep(0.3f, 0.6f, wobble);
                const float fineKeep = SmoothStep(0.5f, 0.62f, Fbm(u, w, 24, 2, 97)) * SmoothStep(0.45f, 0.75f, Fbm(u, w, 8, 3, 47));
                const float big = (1.0f - SmoothStep(0.0f, 0.03f, a2 - a1)) * bigKeep;
                const float fine = (1.0f - SmoothStep(0.0f, 0.035f, b2 - b1)) * fineKeep * 0.6f;
                const float crack = std::min(1.0f, (big + fine) * (0.55f + 0.45f * Fbm(u, w, 32, 2, 103)));
                // Bold cracks for the ground (alpha; the user missed them there): the same warped cells, edges
                // as wide as before and only faded in and out, not broken up.
                const float boldBig = (1.0f - SmoothStep(0.0f, 0.07f, a2 - a1)) * SmoothStep(0.3f, 0.6f, wobble);
                const float boldFine = (1.0f - SmoothStep(0.0f, 0.06f, b2 - b1)) * SmoothStep(0.45f, 0.75f, Fbm(u, w, 8, 3, 47)) * 0.6f;
                const float bold = std::min(1.0f, boldBig + boldFine);
                const float cloud = SmoothStep(0.25f, 0.8f, Fbm(u, w, 4, 4, 59));
                const float bubble = bubbles[static_cast<size_t>(y) * size + x];
                const auto b8 = [](float f) { return static_cast<uint32_t>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
                texels[static_cast<size_t>(y) * size + x] = (b8(bold) << 24) | (b8(crack) << 16) | (b8(bubble) << 8) | b8(cloud);
            }
    }

    void Install(const WXL_Api* api)
    {
        g_hooked = api->HookAttachByName("Adt.SurfaceChunkDrawShader", reinterpret_cast<void*>(&hkChunkDraw), reinterpret_cast<void**>(&g_origDraw), WXL_HOOK_DEFAULT_PRIORITY) != 0 ||
                   api->HookAttachByName("Adt.SurfaceChunkDrawShader", reinterpret_cast<void*>(&hkChunkDraw), reinterpret_cast<void**>(&g_origDraw), 1000) != 0;
        if (!g_hooked) api->Log(WXL_LOG_WARN, "wxl-seyris-living-azeroth", "clear ice: the terrain chunk draw hook point is missing; no ice look");
    }

    void BeforeTerrainStage(IDirect3DDevice9* dev)
    {
        g_lastChunks = g_chunks; g_lastIceChunks = g_iceChunks;
        g_chunks = g_iceChunks = 0;
        g_stageOn = false;
        g_needSun = false;
        if (!dev) return;
        const float zero[4] = {};
        dev->SetPixelShaderConstantF(kMaskConstant, zero, 1);
        if (materials::Generation() != g_generation)
        {
            g_generation = materials::Generation();
            g_anyIce = materials::AnyWithFlags(materials::kFlagClearIce);
            g_cache.clear();
        }
        if (g_cache.size() > 4096) g_cache.clear();
        float c[4 * kConstantCount] = {};
        // The view, camera and sun (read at the first chunk) every frame: the ice models use them too.
        {
            float P[16];
            g_haveView = wxl::game::gfx::SceneMatrices(g_view, P);
            wxl::game::camera::GetPosition(g_camera);
            g_needSun = g_hooked;
        }
        const bool on = g_enabled && g_hooked && g_anyIce && g_withIce > 0 && terrainwet::PatchActive() && EnsureDetail(dev);
        if (on)
        {
            const float* cam = g_camera;
            c[0] = cam[0]; c[1] = cam[1]; c[2] = cam[2]; c[3] = g_strength;
            c[4] = g_sun[0]; c[5] = g_sun[1]; c[6] = g_sun[2]; c[7] = 350.0f;
            for (int k = 0; k < 3; ++k) c[8 + k] = g_sunColour[k] * 1.3f;
            c[11] = 0.65f;
            c[12] = 0.04f; c[13] = 0.20f; c[14] = 0.30f; c[15] = g_clarity;
            c[16] = 0.7f; c[17] = 2.2f; c[18] = 1.0f / 7.0f; c[19] = 1.0f / 13.0f;
            c[20] = 0.60f; c[21] = 0.85f; c[22] = 1.0f; c[23] = 1.15f;
            if (g_debug) { c[24] = 0.8f; c[25] = 0.0f; c[26] = 0.8f; }
            dev->SetTexture(kSampler, g_detail);
            dev->SetSamplerState(kSampler, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(kSampler, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(kSampler, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(kSampler, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
            dev->SetSamplerState(kSampler, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
            dev->SetSamplerState(kSampler, D3DSAMP_SRGBTEXTURE, FALSE);
            g_stageOn = true;
        }
        dev->SetPixelShaderConstantF(kFirstConstant, c, kConstantCount);
    }

    void FillModelConstants(float c[4 * kModelConstantCount])
    {
        std::memset(c, 0, sizeof(float) * 4 * kModelConstantCount);
        // c180 camera (world); c181..c183 the view's rows (world-relative = dot(view vector, row r)).
        for (int k = 0; k < 3; ++k) c[k] = g_camera[k];
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 3; ++k) c[4 + r * 4 + k] = g_haveView ? g_view[r * 4 + k] : (r == k ? 1.0f : 0.0f);
        // c184 the sun (view space, toward it), glint exponent; c185 glint colour, Fresnel strength.
        for (int k = 0; k < 3; ++k) { c[16 + k] = g_sunView[k]; c[20 + k] = g_sunColour[k] * 1.3f; }
        c[19] = 350.0f; c[23] = 0.65f;
        // c186 deep body, clarity; c187 depths 1, 2 (yd), detail repeats per yd 1, 2; c188 highlight,
        // sky reflection brightness; c189 debug tint (the ice weight comes per batch, c179).
        c[24] = 0.04f; c[25] = 0.20f; c[26] = 0.30f; c[27] = g_clarity;
        c[28] = 0.35f; c[29] = 1.1f; c[30] = 1.0f / 4.0f; c[31] = 1.0f / 7.0f;
        c[32] = 0.60f; c[33] = 0.85f; c[34] = 1.0f; c[35] = 1.15f;
        if (g_debug) { c[36] = 0.8f; c[38] = 0.8f; }
    }

    IDirect3DTexture9* DetailTexture(IDirect3DDevice9* dev) { return dev && EnsureDetail(dev) ? g_detail : nullptr; }

    void  SetEnabled(bool enabled)   { g_enabled = enabled; }
    bool  Enabled()                  { return g_enabled; }
    void  SetStrength(float s)       { g_strength = std::clamp(s, 0.0f, 1.0f); }
    float Strength()                 { return g_strength; }
    void  SetClarity(float c)        { g_clarity = std::clamp(c, 0.0f, 1.0f); }
    float Clarity()                  { return g_clarity; }
    void  SetDebug(bool on)          { g_debug = on; }
    bool  Debug()                    { return g_debug; }

    const char* StatusLine()
    {
        char line[512];
        std::snprintf(line, sizeof(line), "clear ice: %s; terrain shader variants with ice %u, without %u%s%s; ice materials %s; last frame %u of %u chunks icy%s",
                      g_stageOn ? "on" : "off", g_withIce, g_withoutIce, g_withoutReasons.empty() ? "" : " (", g_withoutReasons.empty() ? "" : (g_withoutReasons + ")").c_str(),
                      g_anyIce ? "yes" : "none (GroundMaterial Flags 0x1)", g_lastIceChunks, g_lastChunks,
                      !g_hooked ? "; chunk draw hook missing" : (g_detailFailed ? "; detail texture failed" : ""));
        g_status = line;
        return g_status.c_str();
    }
}
