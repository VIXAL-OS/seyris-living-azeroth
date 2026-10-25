#include "DistantLightModel.hpp"

#include <algorithm>
#include <cmath>

namespace wxl_livingazeroth::distantlight
{
    namespace
    {
        float Smooth(float a, float b, float v)
        {
            if (!(b > a)) return v >= b ? 1.0f : 0.0f;
            const float t = std::clamp((v - a) / (b - a), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
        float Luminance(const float c[3]) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }
    }

    float NearFade(float distance, float nearRange)
    {
        if (!(nearRange > 0.0f)) return 1.0f;
        return Smooth(nearRange * kFadeFrom, nearRange, distance);
    }

    float FogVisibility(float distance, float start, float end, float pierce)
    {
        if (!std::isfinite(start) || !std::isfinite(end) || !(end > start + 1.0f)) return 1.0f;
        const float depth = std::max(0.0f, distance - std::max(0.0f, start)) / (end - std::max(0.0f, start));
        const float p = std::isfinite(pierce) ? std::max(0.1f, pierce) : 1.5f;
        return std::exp(-depth / p);
    }

    float FarClipFade(float distance, float farClip)
    {
        if (!(farClip > 0.0f) || !std::isfinite(farClip)) return 1.0f;
        return 1.0f - Smooth(farClip * 0.88f, farClip * 0.98f, distance);
    }

    float LampSize(float radius)
    {
        const float r = std::isfinite(radius) && radius > 0.0f ? radius : 16.0f;
        return std::clamp(0.35f + 0.035f * r, 0.4f, 1.4f);
    }

    float WindowSize(float halfWidth, float halfHeight)
    {
        const float h = std::max(std::isfinite(halfWidth) ? halfWidth : 1.0f, std::isfinite(halfHeight) ? halfHeight : 1.0f);
        return std::clamp(0.6f * h, 0.4f, 2.0f);
    }

    Screen OnScreen(float worldSize, float distance, float pixelsPerYard)
    {
        Screen s{};
        const float d = std::max(distance, 1.0f);
        s.corePixels = std::max(kMinCorePixels, worldSize * pixelsPerYard / d);
        s.halfPixels = std::max(kMinHaloPixels, s.corePixels * kHaloScale);
        return s;
    }

    void Cluster(const std::vector<Source>& in, const float eye[3], std::vector<Sprite>& out)
    {
        out.clear();
        // Each glow's cell as a key, sorted: a cell's glows sit together (no hash map: a city's thousands in tens of us).
        struct Item { uint64_t key; uint32_t index; float lum; };
        static std::vector<Item> items;
        items.clear();
        items.reserve(in.size());
        for (uint32_t i = 0; i < in.size(); ++i)
        {
            const Source& s = in[i];
            const float dx = s.pos[0] - eye[0], dy = s.pos[1] - eye[1], dz = s.pos[2] - eye[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            const float lum = Luminance(s.color);
            if (!(lum > 1e-4f) || !std::isfinite(d2)) continue;
            // band: floor(log2(d / 256)), 0..5, from d^2 without a log (each band 4x the squared distance)
            int band = 0;
            for (float edge = 512.0f * 512.0f; band < 5 && d2 >= edge; edge *= 4.0f) ++band;
            const float inv = 1.0f / (2.0f * static_cast<float>(1 << band));
            // (floor through a 32-bit truncation of a value made positive: the 32-bit client's float -> int64 is a slow call)
            auto q = [&](float v) { return static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(v * inv + 262144.0f)) & 0x7FFFFu); };
            items.push_back({ q(s.pos[0]) | (q(s.pos[1]) << 19) | (q(s.pos[2]) << 38) | (static_cast<uint64_t>(band) << 57), i, lum });
        }
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.key != b.key ? a.key < b.key : a.index < b.index; });
        out.reserve(items.size());
        for (size_t at = 0; at < items.size();)
        {
            double pos[3] = {};
            float weight = 0.0f, color[3] = {}, size = 0.0f;
            unsigned count = 0;
            bool lamp = false;
            const uint64_t key = items[at].key;
            for (; at < items.size() && items[at].key == key; ++at)
            {
                const Source& s = in[items[at].index];
                const float lum = items[at].lum;
                for (int k = 0; k < 3; ++k) { pos[k] += static_cast<double>(s.pos[k]) * lum; color[k] += s.color[k]; }
                weight += lum;
                size = std::max(size, s.size);
                ++count;
                lamp = lamp || s.kind == kLamp;
            }
            Sprite sp{};
            for (int k = 0; k < 3; ++k) sp.pos[k] = static_cast<float>(pos[k] / weight);
            const float keep = count > 1 ? 1.0f / std::sqrt(static_cast<float>(count)) : 1.0f;
            for (int k = 0; k < 3; ++k) sp.color[k] = color[k] * keep;
            sp.size = count > 1 ? size * (1.0f + 0.25f * std::log2(static_cast<float>(count))) : size;
            sp.count = count;
            sp.kind = lamp ? kLamp : kWindow;
            const float dx = sp.pos[0] - eye[0], dy = sp.pos[1] - eye[1], dz = sp.pos[2] - eye[2];
            sp.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            out.push_back(sp);
        }
    }

    void Cap(std::vector<Sprite>& sprites, size_t max)
    {
        if (sprites.size() > max)
        {
            // (scored once each, then the cut on the scores)
            static std::vector<std::pair<float, uint32_t>> scored;
            scored.clear();
            scored.reserve(sprites.size());
            for (uint32_t i = 0; i < sprites.size(); ++i) scored.push_back({ -Luminance(sprites[i].color) / (1.0f + sprites[i].distance / 400.0f), i });
            std::nth_element(scored.begin(), scored.begin() + static_cast<std::ptrdiff_t>(max), scored.end());
            static std::vector<Sprite> kept;
            kept.clear();
            for (size_t k = 0; k < max; ++k) kept.push_back(sprites[scored[k].second]);
            sprites.swap(kept);
        }
        std::sort(sprites.begin(), sprites.end(), [](const Sprite& a, const Sprite& b) { return a.distance < b.distance; });
    }
}
