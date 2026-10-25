// A BLP2 texture's average colour, from its bytes and without a device: what the surface cover needs to look like
// the ground beside it (features/SurfaceCoverFill, blend::MatchGround); and its top level's colours (the see-through
// redraw's lead mask). Header-only, so it can be checked offline (tests/cover, tests/glass).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace wxl_livingazeroth::blp
{
    /// One DXT colour block (8 bytes: two RGB565 endpoints, then 2-bit indices, texel 0 in the lowest bits): its
    /// 16 texels' RGB (0..1), and whether each is opaque -- in DXT1 with the first endpoint not above the second,
    /// index 2 is the endpoints' middle and index 3 transparent black. DXT3 and DXT5 always use four colours.
    inline void DecodeColourBlock(const uint8_t* b, bool dxt1, float rgb[16][3], bool opaque[16])
    {
        const uint16_t c0 = static_cast<uint16_t>(b[0] | (b[1] << 8)), c1 = static_cast<uint16_t>(b[2] | (b[3] << 8));
        auto unpack = [](uint16_t c, float* o)
        {
            o[0] = ((c >> 11) & 31) / 31.0f;
            o[1] = ((c >> 5) & 63) / 63.0f;
            o[2] = (c & 31) / 31.0f;
        };
        float p[4][3];
        unpack(c0, p[0]);
        unpack(c1, p[1]);
        const bool four = !dxt1 || c0 > c1;
        for (int k = 0; k < 3; ++k)
        {
            p[2][k] = four ? (2.0f * p[0][k] + p[1][k]) / 3.0f : 0.5f * (p[0][k] + p[1][k]);
            p[3][k] = four ? (p[0][k] + 2.0f * p[1][k]) / 3.0f : 0.0f;
        }
        const uint32_t indices = b[4] | (b[5] << 8) | (b[6] << 16) | (static_cast<uint32_t>(b[7]) << 24);
        for (int t = 0; t < 16; ++t)
        {
            const int i = static_cast<int>((indices >> (2 * t)) & 3);
            for (int k = 0; k < 3; ++k) rgb[t][k] = p[i][k];
            opaque[t] = four || i != 3;
        }
    }

    /// The average RGB (0..1, as stored: gamma) of a BLP2 file's opaque texels, read from its mip near 32 pixels
    /// across (its mips are box-filtered, so that level keeps the average; the smallest ones may be junk in some
    /// files). Compression 1 (palette, alpha 0/1/4/8 bits), 2 (DXT1/3/5) and 3 (BGRA). False if the bytes aren't
    /// such a file.
    inline bool AverageColour(const uint8_t* file, size_t size, float out[3])
    {
        constexpr size_t kHeader = 148 + 1024;
        if (!file || size < kHeader || std::memcmp(file, "BLP2", 4) != 0) return false;
        auto u32 = [&](size_t at) { uint32_t v; std::memcpy(&v, file + at, 4); return v; };
        const uint8_t compression = file[8], alphaDepth = file[9], alphaType = file[10], hasMips = file[11];
        const uint32_t width = u32(12), height = u32(16);
        if (!width || !height || width > 4096 || height > 4096) return false;
        int level = 0;
        const int levels = hasMips ? 16 : 1;
        for (int m = 1; m < levels; ++m)
        {
            if (!u32(20 + 4 * m) || !u32(84 + 4 * m)) break;
            if (std::max(width >> (m - 1), height >> (m - 1)) <= 32) break;
            level = m;
        }
        const uint32_t w = std::max(width >> level, 1u), h = std::max(height >> level, 1u);
        const uint32_t offset = u32(20 + 4 * level), length = u32(84 + 4 * level);
        if (!length || offset + static_cast<size_t>(length) > size) return false;
        const uint8_t* src = file + offset;
        double sum[3] = {};
        double count = 0.0;
        auto add = [&](float r, float g, float b) { sum[0] += r; sum[1] += g; sum[2] += b; count += 1.0; };
        if (compression == 2)
        {
            const bool dxt1 = alphaType != 1 && alphaType != 7;
            const uint32_t block = dxt1 ? 8 : 16, across = (w + 3) / 4, down = (h + 3) / 4;
            bool shortMip = false; // what a cut-off level holds still counts
            for (uint32_t by = 0; by < down && !shortMip; ++by)
                for (uint32_t bx = 0; bx < across; ++bx)
                {
                    const size_t at = (static_cast<size_t>(by) * across + bx) * block;
                    if (at + block > length) { shortMip = true; break; }
                    float rgb[16][3];
                    bool opaque[16];
                    DecodeColourBlock(src + at + (dxt1 ? 0 : 8), dxt1, rgb, opaque);
                    for (int t = 0; t < 16; ++t)
                    {
                        const uint32_t x = bx * 4 + t % 4, y = by * 4 + t / 4;
                        if (x < w && y < h && opaque[t]) add(rgb[t][0], rgb[t][1], rgb[t][2]);
                    }
                }
        }
        else if (compression == 1 || compression == 3)
        {
            const size_t texels = static_cast<size_t>(w) * h;
            for (size_t i = 0; i < texels; ++i)
            {
                uint32_t c;
                if (compression == 3)
                {
                    if ((i + 1) * 4 > length) break;
                    std::memcpy(&c, src + i * 4, 4);
                }
                else
                {
                    if (i >= length) break;
                    c = u32(148 + 4 * static_cast<size_t>(src[i]));
                    // Transparent texels (1-bit alpha off, or an 8-bit alpha of 0) don't count.
                    if ((alphaDepth == 1 && texels + i / 8 < length && !((src[texels + i / 8] >> (i % 8)) & 1)) ||
                        (alphaDepth == 8 && texels + i < length && src[texels + i] == 0))
                        continue;
                }
                add(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f);
            }
        }
        else
            return false;
        if (count <= 0.0) return false;
        for (int k = 0; k < 3; ++k) out[k] = static_cast<float>(sum[k] / count);
        return true;
    }

    /// The colour a BLP2 file glows with (features/SpellLights: a particle's or a spell model's texture): the mean of its
    /// texels near 32 pixels across (the level AverageColour reads), each weighted by its brightest channel, so the black
    /// round an additive flare doesn't wash its hue out (0..1, as stored). `brightness` gets the texels' mean brightest
    /// channel (how much of the texture shines). The formats AverageColour reads; false if the bytes aren't such a file or
    /// nothing in it shines.
    inline bool GlowColour(const uint8_t* file, size_t size, float out[3], float* brightness = nullptr)
    {
        constexpr size_t kHeader = 148 + 1024;
        if (!file || size < kHeader || std::memcmp(file, "BLP2", 4) != 0) return false;
        auto u32 = [&](size_t at) { uint32_t v; std::memcpy(&v, file + at, 4); return v; };
        const uint8_t compression = file[8], alphaDepth = file[9], alphaType = file[10], hasMips = file[11];
        const uint32_t width = u32(12), height = u32(16);
        if (!width || !height || width > 4096 || height > 4096) return false;
        int level = 0;
        const int levels = hasMips ? 16 : 1;
        for (int m = 1; m < levels; ++m)
        {
            if (!u32(20 + 4 * m) || !u32(84 + 4 * m)) break;
            if (std::max(width >> (m - 1), height >> (m - 1)) <= 32) break;
            level = m;
        }
        const uint32_t w = std::max(width >> level, 1u), h = std::max(height >> level, 1u);
        const uint32_t offset = u32(20 + 4 * level), length = u32(84 + 4 * level);
        if (!length || offset + static_cast<size_t>(length) > size) return false;
        const uint8_t* src = file + offset;
        double sum[3] = {}, weight = 0.0, peakSum = 0.0, count = 0.0;
        auto add = [&](float r, float g, float b)
        {
            const float p = std::max(r, std::max(g, b));
            sum[0] += r * p; sum[1] += g * p; sum[2] += b * p;
            weight += p; peakSum += p; count += 1.0;
        };
        if (compression == 2)
        {
            const bool dxt1 = alphaType != 1 && alphaType != 7;
            const uint32_t block = dxt1 ? 8 : 16, across = (w + 3) / 4, down = (h + 3) / 4;
            bool shortMip = false;
            for (uint32_t by = 0; by < down && !shortMip; ++by)
                for (uint32_t bx = 0; bx < across; ++bx)
                {
                    const size_t at = (static_cast<size_t>(by) * across + bx) * block;
                    if (at + block > length) { shortMip = true; break; }
                    float rgb[16][3];
                    bool opaque[16];
                    DecodeColourBlock(src + at + (dxt1 ? 0 : 8), dxt1, rgb, opaque);
                    for (int t = 0; t < 16; ++t)
                    {
                        const uint32_t x = bx * 4 + t % 4, y = by * 4 + t / 4;
                        if (x < w && y < h && opaque[t]) add(rgb[t][0], rgb[t][1], rgb[t][2]);
                    }
                }
        }
        else if (compression == 1 || compression == 3)
        {
            const size_t texels = static_cast<size_t>(w) * h;
            for (size_t i = 0; i < texels; ++i)
            {
                uint32_t c;
                if (compression == 3)
                {
                    if ((i + 1) * 4 > length) break;
                    std::memcpy(&c, src + i * 4, 4);
                }
                else
                {
                    if (i >= length) break;
                    c = u32(148 + 4 * static_cast<size_t>(src[i]));
                    if ((alphaDepth == 1 && texels + i / 8 < length && !((src[texels + i / 8] >> (i % 8)) & 1)) ||
                        (alphaDepth == 8 && texels + i < length && src[texels + i] == 0))
                        continue;
                }
                add(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f);
            }
        }
        else
            return false;
        if (brightness) *brightness = count > 0.0 ? static_cast<float>(peakSum / count) : 0.0f;
        if (weight <= 1e-6) return false;
        for (int k = 0; k < 3; ++k) out[k] = static_cast<float>(sum[k] / weight);
        return true;
    }

    /// A BLP2 file's mip level `level` (0 = the top) as RGB (0..1, as stored: three floats a texel, rows top down; alpha
    /// left out), its size in w and h. The same formats as AverageColour; false if the bytes aren't such a file or the
    /// level is missing or cut short.
    inline bool DecodeLevel(const uint8_t* file, size_t size, uint32_t level, std::vector<float>& rgb, uint32_t& w, uint32_t& h)
    {
        constexpr size_t kHeader = 148 + 1024;
        rgb.clear();
        if (!file || size < kHeader || std::memcmp(file, "BLP2", 4) != 0 || level >= 16) return false;
        auto u32 = [&](size_t at) { uint32_t v; std::memcpy(&v, file + at, 4); return v; };
        const uint8_t compression = file[8], alphaType = file[10];
        w = u32(12);
        h = u32(16);
        if (!w || !h || w > 4096 || h > 4096) return false;
        w = std::max<uint32_t>(1, w >> level);
        h = std::max<uint32_t>(1, h >> level);
        const uint32_t offset = u32(20 + 4 * level), length = u32(84 + 4 * level);
        if (!length || offset + static_cast<size_t>(length) > size) return false;
        const uint8_t* src = file + offset;
        rgb.assign(static_cast<size_t>(w) * h * 3, 0.0f);
        if (compression == 2)
        {
            const bool dxt1 = alphaType != 1 && alphaType != 7;
            const uint32_t block = dxt1 ? 8 : 16, across = (w + 3) / 4, down = (h + 3) / 4;
            if (static_cast<size_t>(across) * down * block > length) { rgb.clear(); return false; }
            for (uint32_t by = 0; by < down; ++by)
                for (uint32_t bx = 0; bx < across; ++bx)
                {
                    float c[16][3];
                    bool opaque[16];
                    DecodeColourBlock(src + (static_cast<size_t>(by) * across + bx) * block + (dxt1 ? 0 : 8), dxt1, c, opaque);
                    for (int t = 0; t < 16; ++t)
                    {
                        const uint32_t x = bx * 4 + t % 4, y = by * 4 + t / 4;
                        if (x < w && y < h)
                            for (int k = 0; k < 3; ++k) rgb[(static_cast<size_t>(y) * w + x) * 3 + k] = c[t][k];
                    }
                }
            return true;
        }
        if (compression != 1 && compression != 3) { rgb.clear(); return false; }
        const size_t texels = static_cast<size_t>(w) * h;
        if ((compression == 3 ? texels * 4 : texels) > length) { rgb.clear(); return false; }
        for (size_t i = 0; i < texels; ++i)
        {
            uint32_t c;
            if (compression == 3) std::memcpy(&c, src + i * 4, 4);
            else c = u32(148 + 4 * static_cast<size_t>(src[i]));
            rgb[i * 3] = ((c >> 16) & 0xFF) / 255.0f;
            rgb[i * 3 + 1] = ((c >> 8) & 0xFF) / 255.0f;
            rgb[i * 3 + 2] = (c & 0xFF) / 255.0f;
        }
        return true;
    }

    /// The top level (DecodeLevel 0).
    inline bool DecodeTop(const uint8_t* file, size_t size, std::vector<float>& rgb, uint32_t& w, uint32_t& h)
    {
        return DecodeLevel(file, size, 0, rgb, w, h);
    }

    /// What the see-through redraw's lead mask is built from (env/GlassModel LeadMask): the largest level no wider or
    /// taller than `maxSide` (the file's own mip, else the smallest the file has). An HD art pack's 1024 x 1024 window
    /// (2026-10-25: Project Reforged B) then costs a 512 x 512 mask, not four times that, and a quarter of the decoding;
    /// the stock textures (256 x 256 and less) decode at their top level as before.
    inline bool DecodeAtMost(const uint8_t* file, size_t size, uint32_t maxSide, std::vector<float>& rgb, uint32_t& w, uint32_t& h)
    {
        for (uint32_t level = 0; level < 16; ++level)
        {
            if (!DecodeLevel(file, size, level, rgb, w, h)) return level > 0 && DecodeLevel(file, size, level - 1, rgb, w, h);
            if (std::max(w, h) <= maxSide || (w == 1 && h == 1)) return true;
        }
        return false;
    }
}
