#include "SpellLightModel.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <unordered_map>

namespace wxl_livingazeroth::spelllight
{
    const char* KindName(Kind k)
    {
        switch (k)
        {
            case Kind::Missile: return "missile";
            case Kind::Effect: return "effect";
            case Kind::Enchant: return "enchant";
            default: return "none";
        }
    }

    std::string NormalizePath(const char* path)
    {
        std::string s = path ? path : "";
        for (char& c : s)
        {
            if (c == '/') c = '\\';
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        const size_t dot = s.rfind('.');
        if (dot != std::string::npos && dot + 4 == s.size() && (s.compare(dot, 4, ".mdx") == 0 || s.compare(dot, 4, ".mdl") == 0))
            s.replace(dot, 4, ".m2");
        return s;
    }

    int LoadVisualTable(const uint8_t* bytes, size_t size, int fileField, std::unordered_set<std::string>& out)
    {
        if (!bytes || size < 20 || std::memcmp(bytes, "WDBC", 4) != 0 || fileField < 0) return -1;
        uint32_t h[4];
        std::memcpy(h, bytes + 4, sizeof(h));
        const uint32_t records = h[0], fields = h[1], recordSize = h[2], stringSize = h[3];
        if (static_cast<uint32_t>(fileField) >= fields || recordSize < fields * 4 ||
            20ull + static_cast<uint64_t>(records) * recordSize + stringSize > size)
            return -1;
        const uint8_t* rows = bytes + 20;
        const char* strings = reinterpret_cast<const char*>(rows + static_cast<size_t>(records) * recordSize);
        int read = 0;
        for (uint32_t r = 0; r < records; ++r)
        {
            uint32_t at = 0;
            std::memcpy(&at, rows + static_cast<size_t>(r) * recordSize + fileField * 4u, 4);
            ++read;
            if (at >= stringSize || !strings[at]) continue;
            const char* s = strings + at;
            const size_t len = strnlen(s, stringSize - at);
            if (len >= stringSize - at) continue;   // not terminated inside the block
            std::string n = NormalizePath(s);
            // Some rows name the artists' own drive ("\\guldan\drive2\..."): the client finds nothing there.
            if (!n.empty() && n[0] != '\\') out.insert(std::move(n));
        }
        return read;
    }

    namespace
    {
        bool StartsWith(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }
    }

    Kind PathKind(const std::string& path, const VisualSets& sets)
    {
        if (NeverLit(path)) return Kind::None;
        if (sets.items.count(path) || StartsWith(path, "spells\\enchantments\\")) return Kind::Enchant;
        if (sets.spells.count(path) || StartsWith(path, "spells\\") || StartsWith(path, "particles\\")) return Kind::Effect;
        return Kind::None;
    }

    bool NeverLit(const std::string& path) { return path.empty() || path == "spells\\errorcube.m2"; }

    Kind KindOf(Kind pathKind, bool neverLit, uint32_t creationFlags, bool inMissileList)
    {
        if (neverLit || (creationFlags & kFlagMapDoodad)) return Kind::None;
        if (inMissileList || (creationFlags & kFlagMissile)) return Kind::Missile;
        return pathKind;
    }

    Kind Classify(const std::string& path, uint32_t creationFlags, bool inMissileList, const VisualSets& sets)
    {
        return KindOf(PathKind(path, sets), NeverLit(path), creationFlags, inMissileList);
    }

    float MaxChannel(const float c[3]) { return std::fmax(c[0], std::fmax(c[1], c[2])); }

    float Saturation(const float c[3])
    {
        const float hi = MaxChannel(c), lo = std::fmin(c[0], std::fmin(c[1], c[2]));
        return hi > 1e-4f ? (hi - lo) / hi : 0.0f;
    }

    bool KeywordColour(const std::string& path, float out[3])
    {
        // The file's own name (the folders say little: spells\, particles\).
        const size_t slash = path.rfind('\\');
        const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        struct Word { const char* text; float c[3]; };
        // Colour words first ("fireball_blue" is blue fire), then fel before fire, lightning before light.
        static const Word kWords[] = {
            { "black", { 0.0f, 0.0f, 0.0f } },       { "red", { 1.0f, 0.25f, 0.15f } },     { "green", { 0.35f, 1.0f, 0.3f } },
            { "blue", { 0.35f, 0.55f, 1.0f } },      { "purple", { 0.7f, 0.35f, 1.0f } },   { "yellow", { 1.0f, 0.9f, 0.35f } },
            { "orange", { 1.0f, 0.6f, 0.2f } },      { "white", { 1.0f, 1.0f, 1.0f } },
            { "fel", { 0.45f, 1.0f, 0.2f } },
            { "lightning", { 0.6f, 0.75f, 1.0f } },  { "shock", { 0.6f, 0.75f, 1.0f } },    { "thunder", { 0.6f, 0.75f, 1.0f } },
            { "fire", { 1.0f, 0.55f, 0.2f } },       { "flame", { 1.0f, 0.55f, 0.2f } },    { "lava", { 1.0f, 0.45f, 0.15f } },
            { "immolat", { 1.0f, 0.5f, 0.2f } },     { "incinerat", { 1.0f, 0.5f, 0.2f } }, { "meteor", { 1.0f, 0.5f, 0.2f } },
            { "pyro", { 1.0f, 0.5f, 0.2f } },        { "scorch", { 1.0f, 0.5f, 0.2f } },    { "magma", { 1.0f, 0.45f, 0.15f } },
            { "frost", { 0.45f, 0.7f, 1.0f } },      { "ice", { 0.5f, 0.75f, 1.0f } },      { "cold", { 0.5f, 0.75f, 1.0f } },
            { "blizzard", { 0.5f, 0.75f, 1.0f } },   { "snow", { 0.7f, 0.85f, 1.0f } },     { "freez", { 0.5f, 0.75f, 1.0f } },
            { "holy", { 1.0f, 0.9f, 0.6f } },        { "divine", { 1.0f, 0.9f, 0.6f } },    { "smite", { 1.0f, 0.9f, 0.6f } },
            { "heal", { 1.0f, 0.95f, 0.7f } },       { "consecrat", { 1.0f, 0.85f, 0.5f } }, { "light", { 1.0f, 0.92f, 0.7f } },
            { "arcane", { 0.75f, 0.45f, 1.0f } },    { "nether", { 0.6f, 0.35f, 1.0f } },   { "mana", { 0.5f, 0.55f, 1.0f } },
            { "shadow", { 0.5f, 0.25f, 0.8f } },     { "void", { 0.45f, 0.2f, 0.75f } },    { "curse", { 0.5f, 0.25f, 0.7f } },
            { "unholy", { 0.45f, 0.8f, 0.3f } },     { "plague", { 0.5f, 0.85f, 0.3f } },
            { "nature", { 0.45f, 0.9f, 0.35f } },    { "poison", { 0.45f, 0.9f, 0.3f } },
        };
        for (const Word& w : kWords)
            if (name.find(w.text) != std::string::npos) { std::memcpy(out, w.c, sizeof(w.c)); return true; }
        return false;
    }

    namespace
    {
        // A key track's value at life fraction t (0..1): keys at their times (fixed16 / 32767), linear between; without
        // times (or a single key), evenly spread.
        float KeyTime(const int16_t* times, uint32_t count, uint32_t k, uint32_t keys)
        {
            if (times && count == keys && count > 0) return std::clamp(times[k] / 32767.0f, 0.0f, 1.0f);
            return keys > 1 ? static_cast<float>(k) / (keys - 1) : 0.0f;
        }

        template <class Value>
        void Sample(const int16_t* times, uint32_t timeCount, uint32_t keys, float t, Value value)
        {
            if (keys == 1) { value(0, 0, 0.0f); return; }
            for (uint32_t k = 0; k + 1 < keys; ++k)
            {
                const float t0 = KeyTime(times, timeCount, k, keys), t1 = KeyTime(times, timeCount, k + 1, keys);
                if (t <= t1 || k + 2 == keys)
                {
                    const float f = t1 > t0 ? std::clamp((t - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;
                    value(k, k + 1, f);
                    return;
                }
            }
        }
    }

    void EmitterTrackColour(const int16_t* colourTimes, uint32_t colourTimeCount, const float* colours, uint32_t colourCount,
                            const int16_t* alphaTimes, uint32_t alphaTimeCount, const int16_t* alphas, uint32_t alphaCount,
                            float out[3], float& alphaMean)
    {
        out[0] = out[1] = out[2] = 1.0f;
        alphaMean = 1.0f;
        if (!colours || !colourCount) return;
        colourCount = std::min(colourCount, 16u);
        alphaCount = alphas ? std::min(alphaCount, 16u) : 0u;
        double sum[3] = {}, weight = 0.0, alphaSum = 0.0;
        constexpr int kSamples = 8;
        for (int s = 0; s < kSamples; ++s)
        {
            const float t = (s + 0.5f) / kSamples;
            float c[3] = {};
            Sample(colourTimes, colourTimeCount, colourCount, t, [&](uint32_t a, uint32_t b, float f)
            {
                for (int k = 0; k < 3; ++k) c[k] = colours[a * 3 + k] + (colours[b * 3 + k] - colours[a * 3 + k]) * f;
            });
            float alpha = 1.0f;
            if (alphaCount)
                Sample(alphaTimes, alphaTimeCount, alphaCount, t, [&](uint32_t a, uint32_t b, float f)
                {
                    alpha = (alphas[a] + (alphas[b] - alphas[a]) * f) / 32767.0f;
                });
            alpha = std::clamp(alpha, 0.0f, 1.0f);
            for (int k = 0; k < 3; ++k) sum[k] += std::clamp(c[k] / 255.0f, 0.0f, 1.0f) * alpha;
            weight += alpha;
            alphaSum += alpha;
        }
        alphaMean = static_cast<float>(alphaSum / kSamples);
        if (weight > 1e-6)
            for (int k = 0; k < 3; ++k) out[k] = static_cast<float>(sum[k] / weight);
    }

    float BlendWeight(uint8_t blend)
    {
        switch (blend)
        {
            case 0: return 0.25f;   // opaque
            case 1: return 0.3f;    // alpha key
            case 2: return 0.45f;   // alpha blend
            case 3: case 4: return 1.0f; // additive
            default: return 0.0f;   // modulate, mod2x
        }
    }

    float ScaleWeight(float meanScale)
    {
        if (!std::isfinite(meanScale) || meanScale <= 0.0f) return 1.0f;
        return std::clamp(std::sqrt(meanScale / 0.5f), 0.3f, 3.0f);
    }

    bool Shade(const Look& look, float out[3])
    {
        out[0] = out[1] = out[2] = 0.0f;
        // The particles: their colour as bright as it is, weighted by how much of each is light and alive.
        double pw = 0.0, pc[3] = {};
        for (unsigned e = 0; e < look.emitterCount && e < kMaxEmitters; ++e)
        {
            const EmitterLook& em = look.emitters[e];
            if (!(em.weight > 0.0f)) continue;
            pw += em.weight;
            for (int k = 0; k < 3; ++k) pc[k] += em.weight * em.color[k];
        }
        // The rest of it (its geometry: a missile's bolt, a shield) joins the particles' (and ribbons') mix.
        const double gw = look.geometryWeight > 0.0f ? std::min(1.0f, look.geometryWeight) : 0.0;
        double mix[3] = {};
        for (int k = 0; k < 3; ++k) mix[k] = pc[k] + gw * look.geometry[k];
        const double mw = pw + gw;
        float glow[3] = {};
        if (mw > 1e-6)
            for (int k = 0; k < 3; ++k) glow[k] = static_cast<float>(mix[k] / mw);
        const float alpha = std::clamp(look.alpha, 0.0f, 1.0f);
        const float glowStrength = static_cast<float>(std::min(1.0, mw)) * alpha;

        const float lightPeak = look.hasLight ? MaxChannel(look.light) : 0.0f;
        if (lightPeak > 0.02f)
        {
            // Its own light: the client's colour (x its intensity and alpha), its brightest channel up to 1.
            float hue[3];
            for (int k = 0; k < 3; ++k) hue[k] = look.light[k] / lightPeak;
            if (Saturation(hue) < 0.2f)
            {
                // A white or grey light lit the model's own geometry; the glow round it is its glow's colour (leant to its
                // name's school, as below), else its name's.
                const float kp = look.hasKeyword ? MaxChannel(look.keyword) : 0.0f;
                const float gp = MaxChannel(glow);
                if (mw > 1e-6 && gp > 1e-3f && Saturation(glow) >= 0.25f)
                    for (int k = 0; k < 3; ++k) hue[k] = kp > 1e-3f ? (1.0f - kNameLean) * glow[k] / gp + kNameLean * look.keyword[k] / kp : glow[k] / gp;
                else if (kp > 1e-3f)
                    for (int k = 0; k < 3; ++k) hue[k] = look.keyword[k] / kp;
                const float hp = MaxChannel(hue);
                for (int k = 0; k < 3; ++k) hue[k] /= hp;
            }
            const float strength = std::min(1.0f, lightPeak);
            for (int k = 0; k < 3; ++k) out[k] = hue[k] * strength;
            return MaxChannel(out) > 0.01f;
        }
        if (!(glowStrength > 0.0f)) return false;
        for (int k = 0; k < 3; ++k) out[k] = glow[k] * glowStrength;

        const float peak = MaxChannel(out);
        if (look.hasKeyword && peak > 1e-4f)
        {
            const float kp = MaxChannel(look.keyword);
            if (Saturation(out) < 0.12f || kp <= 1e-4f)
            {
                // Still grey (white particles whose texture holds the colour, before it is read), or a black glow: the
                // name's hue (black: none).
                for (int k = 0; k < 3; ++k) out[k] = kp > 1e-4f ? look.keyword[k] / kp * peak : 0.0f;
            }
            else
            {
                // The school's colour the name gives leans the hue halfway (kNameLean): a particle system's mean colour
                // is a guess (Arcane Missiles: golden sparkles round a purple bolt; Fel Fireball: an orange flare and a
                // red halo over green fire).
                float hue[3];
                for (int k = 0; k < 3; ++k) hue[k] = (1.0f - kNameLean) * out[k] / peak + kNameLean * look.keyword[k] / kp;
                const float hp = MaxChannel(hue);
                for (int k = 0; k < 3; ++k) out[k] = hue[k] / hp * peak;
            }
        }
        return MaxChannel(out) > 0.01f;
    }

    float SizeOf(float boundsRadius, float scale)
    {
        if (!std::isfinite(boundsRadius) || boundsRadius <= 0.0f) boundsRadius = 1.0f;
        if (!std::isfinite(scale) || scale <= 0.0f) scale = 1.0f;
        return std::clamp(boundsRadius * scale, 0.3f, 20.0f);
    }

    float LightRadius(const Settings& s, Kind kind, float size)
    {
        const float k = std::clamp(0.55f + 0.35f * std::sqrt(std::max(size, 0.0f)), 0.7f, 2.4f);
        const float r = std::max(1.0f, s.radius) * k;
        return kind == Kind::Enchant ? 0.5f * r : r;
    }

    float SizeGain(float size) { return std::clamp(0.8f + 0.2f * std::sqrt(std::max(size, 0.0f)), 0.8f, 1.4f); }

    float Lift(float size) { return std::clamp(0.25f * size, 0.3f, 1.5f); }

    void StepFade(Fade& f, const float target[3], float dt)
    {
        const float peak = MaxChannel(target);
        if (!f.started)
        {
            f.started = true;
            f.level = 0.0f;
            for (int k = 0; k < 3; ++k) f.hue[k] = peak > 1e-4f ? target[k] / peak : 0.0f;
        }
        dt = std::clamp(dt, 0.0f, 0.5f);
        const float tau = peak > f.level ? kRiseSeconds : kFallSeconds;
        f.level += (peak - f.level) * (1.0f - std::exp(-dt / tau));
        if (f.level < 1e-4f && peak <= 0.0f) f.level = 0.0f;
        if (peak > 1e-4f)
        {
            const float h = 1.0f - std::exp(-dt / kHueSeconds);
            for (int k = 0; k < 3; ++k) f.hue[k] += (target[k] / peak - f.hue[k]) * h;
        }
    }

    void FadedColour(const Fade& f, float out[3])
    {
        const float hp = MaxChannel(f.hue);
        for (int k = 0; k < 3; ++k) out[k] = hp > 1e-4f ? f.hue[k] / hp * f.level : 0.0f;
    }

    float MergeKeep(unsigned sources) { return sources > 1 ? 1.0f / std::sqrt(static_cast<float>(sources)) : 1.0f; }

    float Score(const Light& l, const float eye[3])
    {
        const float dx = l.pos[0] - eye[0], dy = l.pos[1] - eye[1], dz = l.pos[2] - eye[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        return MaxChannel(l.color) * l.radius / (1.0f + d2 / (15.0f * 15.0f));
    }

    float Crowding(const std::vector<Light>& lights, size_t i)
    {
        float k = 1.0f;
        const Light& a = lights[i];
        for (size_t j = 0; j < lights.size(); ++j)
        {
            if (j == i) continue;
            const Light& b = lights[j];
            const float dx = a.pos[0] - b.pos[0], dy = a.pos[1] - b.pos[1], dz = a.pos[2] - b.pos[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (!(b.radius > 0.0f) || d2 >= b.radius * b.radius) continue;
            const float t2 = d2 / (b.radius * b.radius);
            k += (1.0f - t2) * (1.0f - t2);
        }
        return k;
    }

    void MergeAndCap(std::vector<Light>& lights, const float eye[3], const Settings& s, MergeStats& st)
    {
        st = MergeStats{};
        st.in = static_cast<unsigned>(lights.size());
        const float range = std::max(0.0f, s.range);
        lights.erase(std::remove_if(lights.begin(), lights.end(), [&](const Light& l)
        {
            const float dx = l.pos[0] - eye[0], dy = l.pos[1] - eye[1], dz = l.pos[2] - eye[2];
            return !(MaxChannel(l.color) > 0.0f) || !(l.radius > 0.0f) || dx * dx + dy * dy + dz * dz > range * range;
        }), lights.end());
        st.inRange = static_cast<unsigned>(lights.size());
        const size_t cap = static_cast<size_t>(std::clamp(s.maxLights, 0, kMaxLightsCap));

        // A raid can hold hundreds: only the best 4 x the cap (at least 64) by score go on to merging.
        const size_t keepForMerge = std::max<size_t>(64, 4 * cap);
        if (lights.size() > keepForMerge)
        {
            for (Light& l : lights) l.score = Score(l, eye);
            std::nth_element(lights.begin(), lights.begin() + keepForMerge, lights.end(), [](const Light& x, const Light& y)
            {
                return x.score != y.score ? x.score > y.score : std::less<const void*>()(x.model, y.model);
            });
            st.dropped += static_cast<unsigned>(lights.size() - keepForMerge);
            lights.resize(keepForMerge);
        }

        // Merging, brightest first, against the lights' positions before any merge: a sorted list of merge-distance cells,
        // looked up by binary search (no allocation once the scratch has grown).
        const float md = s.mergeDistance;
        if (md > 1e-3f && lights.size() > 1)
        {
            std::sort(lights.begin(), lights.end(), [](const Light& x, const Light& y)
            {
                const float px = MaxChannel(x.color), py = MaxChannel(y.color);
                return px != py ? px > py : std::less<const void*>()(x.model, y.model);
            });
            const float inv = 1.0f / md;
            auto cellOf = [&](const float p[3], int out[3]) { for (int k = 0; k < 3; ++k) out[k] = static_cast<int>(std::floor(p[k] * inv)); };
            auto keyOf = [](int x, int y, int z)
            {
                return (static_cast<uint64_t>(x + 1048576) & 0x1FFFFF) | ((static_cast<uint64_t>(y + 1048576) & 0x1FFFFF) << 21) |
                       ((static_cast<uint64_t>(z + 1048576) & 0x1FFFFF) << 42);
            };
            static std::vector<std::pair<uint64_t, uint32_t>> cells;
            static std::vector<float> original;
            static std::vector<char> gone;
            static std::vector<uint32_t> members;
            cells.clear();
            original.resize(lights.size() * 3);
            gone.assign(lights.size(), 0);
            for (size_t i = 0; i < lights.size(); ++i)
            {
                std::memcpy(&original[i * 3], lights[i].pos, sizeof(lights[i].pos));
                int c[3];
                cellOf(lights[i].pos, c);
                cells.push_back({ keyOf(c[0], c[1], c[2]), static_cast<uint32_t>(i) });
            }
            std::sort(cells.begin(), cells.end());
            for (size_t a = 0; a < lights.size(); ++a)
            {
                if (gone[a]) continue;
                Light& la = lights[a];
                const float* pa = &original[a * 3];
                double wsum = MaxChannel(la.color);
                double pos[3] = { la.pos[0] * wsum, la.pos[1] * wsum, la.pos[2] * wsum };
                float spread = 0.0f;
                unsigned sources = la.sources;
                members.clear();
                int ca[3];
                cellOf(pa, ca);
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            const uint64_t key = keyOf(ca[0] + dx, ca[1] + dy, ca[2] + dz);
                            for (auto it = std::lower_bound(cells.begin(), cells.end(), std::make_pair(key, 0u)); it != cells.end() && it->first == key; ++it)
                            {
                                const uint32_t b = it->second;
                                if (b == a || gone[b]) continue;
                                const float* pb = &original[b * 3];
                                const float ex = pb[0] - pa[0], ey = pb[1] - pa[1], ez = pb[2] - pa[2];
                                if (ex * ex + ey * ey + ez * ez > md * md) continue;
                                members.push_back(b);
                            }
                        }
                if (members.empty()) continue;
                std::sort(members.begin(), members.end());
                for (uint32_t b : members)
                {
                    Light& lb = lights[b];
                    const float w = MaxChannel(lb.color);
                    for (int k = 0; k < 3; ++k) { la.color[k] += lb.color[k]; pos[k] += lb.pos[k] * w; }
                    wsum += w;
                    la.radius = std::max(la.radius, lb.radius);
                    sources += lb.sources;
                    const float* pb = &original[b * 3];
                    const float ex = pb[0] - pa[0], ey = pb[1] - pa[1], ez = pb[2] - pa[2];
                    spread = std::max(spread, std::sqrt(ex * ex + ey * ey + ez * ez));
                    gone[b] = 1;
                    ++st.merged;
                }
                if (wsum > 1e-6)
                    for (int k = 0; k < 3; ++k) la.pos[k] = static_cast<float>(pos[k] / wsum);
                // The summed colour of the separate sources (each one light): sqrt N of it (MergeKeep), as for env/Lights'
                // small flames.
                const float keep = MergeKeep(sources);
                for (int k = 0; k < 3; ++k) la.color[k] *= keep;
                la.sources = sources;
                la.radius += 0.5f * spread;
                ++st.clusters;
            }
            size_t w = 0;
            for (size_t i = 0; i < lights.size(); ++i)
                if (!gone[i]) lights[w++] = lights[i];
            lights.resize(w);
        }

        for (Light& l : lights) l.score = Score(l, eye);
        std::sort(lights.begin(), lights.end(), [](const Light& a, const Light& b)
        {
            return a.score != b.score ? a.score > b.score : std::less<const void*>()(a.model, b.model);
        });
        if (lights.size() > cap)
        {
            st.dropped += static_cast<unsigned>(lights.size() - cap);
            lights.resize(cap);
        }
        st.out = static_cast<unsigned>(lights.size());
        // Crowding: overlapping lights share their glow (each from the colours before any is scaled).
        std::vector<float> keep(lights.size(), 1.0f);
        for (size_t i = 0; i < lights.size(); ++i) keep[i] = 1.0f / std::sqrt(Crowding(lights, i));
        for (size_t i = 0; i < lights.size(); ++i)
        {
            if (keep[i] < 0.99f) ++st.crowded;
            for (float& c : lights[i].color) c *= keep[i];
        }
    }
}
