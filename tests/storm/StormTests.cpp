// Offline tests for env/StormModel (storms: the rules env/Storm applies to the client): grade to phase,
// storminess, when flashes come, what a flash looks like over time, where strikes land, thunder's delay
// and loudness by distance, the day-night colour shading (and that it never builds up), falling snow's
// fog, sky and fog distance, the bolt's matrix, and -- given the client executable -- every guard against
// its bytes. No game.
// Also what the rain does elsewhere: the fog wake's weather scaling (features/FogWakeWeather.hpp); and the cloud deck's
// tint of what's drawn after it (features/CloudTint.hpp: the Temple of Storms' lightning, with the storm and snow shading).
//
// Usage:  storm_tests [<Wow.exe>] [<SoundEntries.dbc>]
//   (the client's SoundEntries.dbc, extracted: checks every thunder kit and Lua row against its rows)
#include "env/StormModel.hpp"
#include "features/CloudTint.hpp"
#include "features/FogWakeWeather.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace st = wxl_livingazeroth::storm;
namespace fw = wxl_livingazeroth::fogwake;
namespace ct = wxl_livingazeroth::cloudtint;

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at line " + std::to_string(__LINE__)); } while (false)

namespace
{
    unsigned g_passed = 0;
    template <class F> void Test(const char* name, F f)
    {
        f();
        ++g_passed;
        std::printf("PASS %s\n", name);
    }

    bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

    uint32_t Argb(int a, int r, int g, int b) { return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b); }
    int R(uint32_t c) { return (c >> 16) & 0xFF; }
    int G(uint32_t c) { return (c >> 8) & 0xFF; }
    int B(uint32_t c) { return c & 0xFF; }
    int Sum(uint32_t c) { return R(c) + G(c) + B(c); }
    float Lum(uint32_t c) { return 0.299f * R(c) + 0.587f * G(c) + 0.114f * B(c); }

    // The band shading as it was before the fog got its own storm (weights: darkening, flash in the sky, flash on the
    // ground), for the bands that still shade that way: the sky (2..7) and the clouds (11..13).
    uint32_t ShadeBefore(uint32_t argb, int band, const st::Shade& s)
    {
        struct W { float dark, sky, scene; };
        W w{};
        switch (band)
        {
            case 2: w = { 1.00f, 0.75f, 0.30f }; break;
            case 3: w = { 1.00f, 0.90f, 0.30f }; break;
            case 4: case 5: w = { 1.00f, 1.00f, 0.30f }; break;
            case 6: w = { 0.90f, 1.00f, 0.30f }; break;
            case 7: w = { 0.80f, 0.80f, 0.30f }; break;
            case 8: w = { 0.70f, 0.70f, 0.40f }; break; // the fog, as it was
            case 11: case 12: case 13: w = { 0.80f, 1.00f, 0.30f }; break;
            default: throw std::runtime_error("ShadeBefore: not a sky, cloud or fog band");
        }
        auto unit = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
        const float c[3] = { float(R(argb)), float(G(argb)), float(B(argb)) };
        const float keep = 1.0f - unit(s.dark * w.dark), grey = unit(s.desat * w.dark);
        const float luma = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
        const float add = 255.0f * st::kFlashGain * ((s.sky > 0.0f ? s.sky : 0.0f) * w.sky + (s.scene > 0.0f ? s.scene : 0.0f) * w.scene);
        uint32_t out = argb & 0xFF000000u;
        for (int k = 0; k < 3; ++k)
        {
            float v = (c[k] + (luma - c[k]) * grey) * keep + add * s.tint[k];
            v = v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v);
            out |= static_cast<uint32_t>(v + 0.5f) << (16 - 8 * k);
        }
        return out;
    }

    // Heavy rain in full storm with the settings' defaults (Storm.cpp: darkness 0.35, StormFogBrightness 1.3,
    // StormFogCool 0.6), at a time of day (0 night .. 1 day).
    st::Shade HeavyRain(float day)
    {
        st::Shade s;
        s.dark = 0.35f;
        s.desat = 0.6f * s.dark;
        s.fog = 1.0f;
        s.fogBright = 1.3f;
        s.fogCool = 0.6f;
        s.day = day;
        return s;
    }

    // Falling snow with the settings' defaults (Storm.cpp: SnowFogBrightness 2.2, SnowFogWhite 0.6), all of the client's
    // weather set showing, at a time of day (0 night .. 1 day).
    st::Shade Snow(float day, float share = 1.0f)
    {
        st::Shade s;
        s.snow = share;
        s.snowBright = 2.2f;
        s.snowWhite = 0.6f;
        s.day = day;
        return s;
    }

    // The client's linear zone fog (fog mode 0): how much of a thing at `d` yd is fog.
    float FogShare(const st::FogDistance& f, float d)
    {
        const float start = f.end * f.startMul;
        const float t = (d - start) / (f.end - start);
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    // --- the client's file: virtual addresses to bytes through its section table ---
    struct Image
    {
        std::vector<uint8_t> bytes;
        uint32_t base = 0, timestamp = 0;
        struct Section { uint32_t rva, size, raw, rawSize; };
        std::vector<Section> sections;

        const void* At(uintptr_t va, uint32_t size) const
        {
            if (va < base) return nullptr;
            const uint32_t rva = static_cast<uint32_t>(va - base);
            for (const Section& s : sections)
                if (rva >= s.rva && rva + size <= s.rva + s.rawSize && s.raw + (rva - s.rva) + size <= bytes.size())
                    return bytes.data() + s.raw + (rva - s.rva);
            return nullptr;
        }
    };

    Image LoadImage(const char* path)
    {
        Image im;
        FILE* f = std::fopen(path, "rb");
        if (!f) throw std::runtime_error(std::string("can't open ") + path);
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        im.bytes.resize(size > 0 ? static_cast<size_t>(size) : 0);
        const size_t got = im.bytes.empty() ? 0 : std::fread(im.bytes.data(), 1, im.bytes.size(), f);
        std::fclose(f);
        CHECK(got == im.bytes.size() && got > 0x400);
        auto u32 = [&](size_t at) { uint32_t v; CHECK(at + 4 <= im.bytes.size()); std::memcpy(&v, im.bytes.data() + at, 4); return v; };
        auto u16 = [&](size_t at) { return static_cast<uint32_t>(im.bytes.at(at) | (im.bytes.at(at + 1) << 8)); };
        const uint32_t pe = u32(0x3C);
        CHECK(u32(pe) == 0x00004550); // "PE\0\0"
        im.timestamp = u32(pe + 8);
        const uint32_t count = u16(pe + 6), optional = u16(pe + 20);
        im.base = u32(pe + 24 + 28);
        const size_t table = pe + 24 + optional;
        for (uint32_t i = 0; i < count; ++i)
        {
            const size_t s = table + 40 * i;
            im.sections.push_back({ u32(s + 12), u32(s + 8), u32(s + 20), u32(s + 16) });
        }
        return im;
    }

    template <size_t N> void CheckGuards(const Image& im, const st::Guard (&guards)[N])
    {
        const st::Guard* bad = st::FirstMismatch(guards, [&](uintptr_t va, uint32_t size) { return im.At(va, size); });
        if (bad) throw std::runtime_error(std::string("differs: ") + bad->what);
        for (const st::Guard& g : guards)
        {
            Image changed = im;
            auto* at = static_cast<uint8_t*>(const_cast<void*>(changed.At(g.address, g.size)));
            CHECK(at);
            at[g.size - 1] ^= 0x01;
            CHECK(st::FirstMismatch(guards, [&](uintptr_t va, uint32_t size) { return changed.At(va, size); }) == &g);
        }
    }

    // --- SoundEntries.dbc (3.3.5a: 30 columns: ID, SoundType, Name, File[10], Freq[10], DirectoryBase, Volume, Flags,
    // MinDistance, DistanceCutoff, EAXDef, SoundEntriesAdvancedID) ---
    struct SoundRow { int32_t id; std::string name; std::vector<std::string> files; float volume, minDistance, cutoff; uint32_t flags; };
    std::vector<SoundRow> LoadSoundEntries(const char* path)
    {
        FILE* f = std::fopen(path, "rb");
        if (!f) throw std::runtime_error(std::string("can't open ") + path);
        std::vector<uint8_t> b;
        for (int c; (c = std::fgetc(f)) != EOF;) b.push_back(static_cast<uint8_t>(c));
        std::fclose(f);
        CHECK(b.size() > 20 && std::memcmp(b.data(), "WDBC", 4) == 0);
        uint32_t h[5];
        std::memcpy(h, b.data(), 20);
        const uint32_t rows = h[1], fields = h[2], size = h[3], strings = h[4];
        CHECK(fields == 30 && size == 120 && 20ull + uint64_t(rows) * size + strings <= b.size());
        const char* text = reinterpret_cast<const char*>(b.data() + 20 + size_t(rows) * size);
        auto str = [&](uint32_t o) { CHECK(o < strings); return std::string(text + o); };
        std::vector<SoundRow> out;
        for (uint32_t i = 0; i < rows; ++i)
        {
            uint32_t r[30];
            std::memcpy(r, b.data() + 20 + size_t(i) * size, sizeof(r));
            SoundRow s;
            s.id = static_cast<int32_t>(r[0]);
            s.name = str(r[2]);
            for (int k = 0; k < 10; ++k) if (r[3 + k]) s.files.push_back(str(r[3 + k]));
            std::memcpy(&s.volume, &r[24], 4);
            s.flags = r[25];
            std::memcpy(&s.minDistance, &r[26], 4);
            std::memcpy(&s.cutoff, &r[27], 4);
            out.push_back(s);
        }
        return out;
    }

    // Counts the flashes a scheduler fires over `seconds` at 60 fps, and the shortest gap between two.
    struct Run { unsigned count = 0; float firstAt = -1.0f, minGap = 1e9f; };
    Run Simulate(st::Phase phase, float grade, float frequency, float seconds, uint32_t seed)
    {
        st::Rng rng;
        rng.state = seed;
        st::Scheduler s;
        Run r;
        float t = 0.0f, last = -1.0f;
        const float dt = 1.0f / 60.0f;
        for (int i = 0; t < seconds; ++i, t += dt)
            if (s.Step(dt, phase, grade, frequency, rng))
            {
                ++r.count;
                if (r.firstAt < 0.0f) r.firstAt = t;
                if (last >= 0.0f && t - last < r.minGap) r.minGap = t - last;
                last = t;
            }
        return r;
    }
}

// Thunder's distance filters (env/ThunderDspModel), on the helpers above.
#include "ThunderDspTests.hpp"

int main(int argc, char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try
    {
        ThunderDspTests(argc, argv);

        Test("phase: the server's cut-offs (fine below 0.27, light, medium from 0.40, heavy from 0.70); rain only", []
        {
            CHECK(st::PhaseFor(1, 0.0f) == st::Phase::None);
            CHECK(st::PhaseFor(1, 0.26f) == st::Phase::None);
            CHECK(st::PhaseFor(1, 0.27f) == st::Phase::Light);
            CHECK(st::PhaseFor(1, 0.39f) == st::Phase::Light);
            CHECK(st::PhaseFor(1, 0.40f) == st::Phase::Medium);
            CHECK(st::PhaseFor(1, 0.69f) == st::Phase::Medium);
            CHECK(st::PhaseFor(1, 0.70f) == st::Phase::Heavy);
            CHECK(st::PhaseFor(1, 1.0f) == st::Phase::Heavy);
            CHECK(st::PhaseFor(2, 0.9f) == st::Phase::None);  // snow
            CHECK(st::PhaseFor(3, 0.9f) == st::Phase::None);  // sand
            CHECK(st::PhaseFor(0, 0.9f) == st::Phase::None);
            CHECK(st::PhaseFor(1, std::nanf("")) == st::Phase::None);
        });

        Test("storminess: 0 up to light rain's start, rising through it and medium, 1 in full storm", []
        {
            CHECK(st::Storminess(1, 0.2f) == 0.0f);
            CHECK(st::Storminess(1, 0.27f) == 0.0f);
            float last = 0.0f;
            for (float g = 0.28f; g <= 0.9f; g += 0.01f)
            {
                const float s = st::Storminess(1, g);
                CHECK(s > last && s <= 1.0f);
                last = s;
            }
            CHECK(Near(st::Storminess(1, 0.9f), 1.0f, 1e-5f) && st::Storminess(1, 1.0f) == 1.0f);
            CHECK(st::Storminess(1, 0.40f) < 0.25f);      // light rain is the build-up: little of the storm yet
            CHECK(st::Storminess(1, 0.70f) > 0.6f);
            CHECK(st::Storminess(2, 0.9f) == 0.0f);
            CHECK(st::Storminess(1, std::nanf("")) == 0.0f);
        });

        Test("flash intervals: none in fine weather, shorter with every phase and grade", []
        {
            CHECK(st::MeanInterval(st::Phase::None, 0.5f) == 0.0f);
            const float light0 = st::MeanInterval(st::Phase::Light, 0.27f), light1 = st::MeanInterval(st::Phase::Light, 0.39f);
            const float medium0 = st::MeanInterval(st::Phase::Medium, 0.40f), medium1 = st::MeanInterval(st::Phase::Medium, 0.69f);
            const float heavy0 = st::MeanInterval(st::Phase::Heavy, 0.70f), heavy1 = st::MeanInterval(st::Phase::Heavy, 1.0f);
            CHECK(Near(light0, 80.0f, 0.01f) && Near(heavy1, 9.0f, 0.01f));
            CHECK(light0 > light1 && light1 > medium0 && medium0 > medium1 && medium1 > heavy0 && heavy0 > heavy1);
            st::Rng rng;
            for (int i = 0; i < 10000; ++i)
            {
                const float w = st::NextWait(rng, 20.0f);
                CHECK(w >= st::kMinGap && w <= st::kMinGap + 4.0f * (20.0f - st::kMinGap) + 1e-3f);
            }
        });

        Test("scheduler: flashes at the phase's rate, never closer than the minimum gap, none at once, none when fine", []
        {
            const float hour = 3600.0f;
            for (uint32_t seed : { 1u, 77u, 4242u })
            {
                const float mean = st::MeanInterval(st::Phase::Heavy, 0.85f);
                const Run heavy = Simulate(st::Phase::Heavy, 0.85f, 1.0f, 2.0f * hour, seed);
                const float expected = 2.0f * hour / mean;
                std::printf("  seed %u: heavy rain at 0.85: %u flashes in 2 h (mean %.1f s: about %.0f), first after %.1f s, closest %.1f s apart\n",
                            seed, heavy.count, mean, expected, heavy.firstAt, heavy.minGap);
                CHECK(heavy.count > expected * 0.8f && heavy.count < expected * 1.2f);
                CHECK(heavy.minGap >= st::kMinGap - 0.02f);
                CHECK(heavy.firstAt >= 0.3f * mean - 0.02f);
                const Run light = Simulate(st::Phase::Light, 0.30f, 1.0f, 4.0f * hour, seed);
                const float lightExpected = 4.0f * hour / st::MeanInterval(st::Phase::Light, 0.30f);
                CHECK(light.count > lightExpected * 0.75f && light.count < lightExpected * 1.25f);
                const Run twice = Simulate(st::Phase::Heavy, 0.85f, 2.0f, 2.0f * hour, seed);
                CHECK(twice.count > heavy.count * 1.6f);   // frequency 2: about twice as many (the minimum gap holds some back)
                CHECK(Simulate(st::Phase::None, 0.9f, 1.0f, hour, seed).count == 0);
                CHECK(Simulate(st::Phase::Heavy, 0.9f, 0.0f, hour, seed).count == 0);
            }
            // The rain growing heavier shortens a long wait at once: a strike comes within the heavy phase's
            // mean wait, not light rain's.
            st::Rng rng;
            rng.state = 99;
            st::Scheduler s;
            for (int i = 0; i < 60; ++i) s.Step(1.0f / 60.0f, st::Phase::Light, 0.3f, 1.0f, rng);
            CHECK(s.wait > 0.0f);
            s.wait = 70.0f;
            s.Step(1.0f / 60.0f, st::Phase::Heavy, 0.9f, 1.0f, rng);
            CHECK(s.wait <= st::MeanInterval(st::Phase::Heavy, 0.9f));
        });

        Test("a flash: rises fast, its pulses decay, gone by its end time; strikes near are brighter and light the ground", []
        {
            st::Rng rng;
            rng.state = 5;
            for (int n = 0; n < 200; ++n)
            {
                const st::Flash f = (n & 1) ? st::MakeStrike(rng, rng.Range(60.0f, 2500.0f), 10.0f) : st::MakeDistantFlash(rng, st::Phase::Medium, 10.0f);
                CHECK(f.pulses >= 1 && f.pulses <= st::kMaxPulses);
                CHECK(st::Envelope(f, 9.99f) == 0.0f);                                 // nothing before it starts
                CHECK(Near(st::Envelope(f, 10.0f + st::kPulseRise), f.amp[0], 1e-4f));  // its first peak
                float peak = 0.0f;
                for (float t = 10.0f; t < st::EndTime(f); t += 0.002f) peak = std::fmax(peak, st::Envelope(f, t));
                CHECK(peak <= 1.0f + 1e-5f);
                CHECK(st::Envelope(f, st::EndTime(f) + 0.001f) < 0.001f);
                CHECK(st::EndTime(f) - f.start < 1.6f);                                  // a flash is over in about a second
                if (f.strike) CHECK(f.pulses >= 2 && f.amp[0] == 1.0f);
            }
            st::Rng a, b;
            a.state = b.state = 11;
            const st::Flash nearStrike = st::MakeStrike(a, 100.0f, 0.0f), farStrike = st::MakeStrike(b, 2000.0f, 0.0f);
            CHECK(nearStrike.sky > farStrike.sky && nearStrike.scene > 0.9f && farStrike.scene < 0.2f);
            st::Rng c;
            const st::Flash lightFlash = st::MakeDistantFlash(c, st::Phase::Light, 0.0f);
            CHECK(lightFlash.sky > 0.0f && lightFlash.sky < 0.3f && lightFlash.scene < 0.05f && !lightFlash.strike);
        });

        Test("strikes: between 60 and 2500 yd, mostly far, a few near", []
        {
            st::Rng rng;
            rng.state = 1234;
            unsigned nearCount = 0, farCount = 0;
            const unsigned n = 20000;
            for (unsigned i = 0; i < n; ++i)
            {
                const float d = st::StrikeDistance(rng);
                CHECK(d >= st::kNearestStrike && d <= st::kFarthestStrike);
                nearCount += d < 250.0f ? 1 : 0;
                farCount += d >= 700.0f ? 1 : 0;
            }
            std::printf("  %.1f%% within 250 yd, %.1f%% beyond 700 yd\n", 100.0 * nearCount / n, 100.0 * farCount / n);
            CHECK(nearCount > n * 0.05 && nearCount < n * 0.09);
            CHECK(farCount > n * 0.66 && farCount < n * 0.74);
        });

        Test("thunder: about 3 s per km after the flash", []
        {
            CHECK(st::ThunderDelay(0.0f) == 0.0f && st::ThunderDelay(-5.0f) == 0.0f);
            const float km = 1000.0f / st::kYard; // yards in a kilometre
            CHECK(Near(st::ThunderDelay(km), 2.915f, 0.01f));
            CHECK(Near(st::ThunderDelay(343.0f / st::kYard), 1.0f, 1e-4f));
            CHECK(Near(st::ThunderDelay(2500.0f), 6.66f, 0.02f));
        });

        Test("thunder's loudness: full near, (190/d)^0.38 out, quieter all the way, fading out before the silent distance", []
        {
            CHECK(st::ThunderGain(0.0f, 1600.0f) == 1.0f && st::ThunderGain(60.0f, 1600.0f) == 1.0f && st::ThunderGain(190.0f, 1600.0f) == 1.0f);
            CHECK(Near(st::ThunderGain(250.0f, 1600.0f), 0.90f, 0.005f));
            CHECK(Near(st::ThunderGain(600.0f, 1600.0f), 0.646f, 0.005f));
            CHECK(Near(st::ThunderGain(1000.0f, 1600.0f), 0.532f, 0.005f));
            float last = 1.0f;
            for (float d = 191.0f; d < 1600.0f; d += 7.0f)
            {
                const float g = st::ThunderGain(d, 1600.0f);
                CHECK(g < last && g > 0.0f);
                last = g;
            }
            // No fade far from the end; gone at it; nothing beyond, or with no distance, or for a nonsense one.
            CHECK(Near(st::ThunderGain(1300.0f, 1600.0f), std::pow(190.0f / 1300.0f, 0.38f), 1e-6f));
            CHECK(st::ThunderGain(1600.0f, 1600.0f) == 0.0f && st::ThunderGain(1601.0f, 1600.0f) == 0.0f);
            CHECK(st::ThunderGain(100.0f, 0.0f) == 0.0f && st::ThunderGain(std::nanf(""), 1600.0f) == 0.0f);
            std::printf("  gain at 60 / 250 / 700 / 1000 / 1500 yd: %.2f %.2f %.2f %.2f %.2f\n", st::ThunderGain(60.0f, 1600.0f),
                        st::ThunderGain(250.0f, 1600.0f), st::ThunderGain(700.0f, 1600.0f), st::ThunderGain(1000.0f, 1600.0f), st::ThunderGain(1500.0f, 1600.0f));
        });

        Test("thunder's recordings: near the machine's crack (or Shaman thunder, Call Lightning), then the Blasted Lands bolt, far the rumble alone", []
        {
            // Near (under 150 yd): the crack by the pick with a second crack under it at the same moment and gain, the
            // rumble after them at 0.8.
            st::ThunderPlan p = st::PlanThunder(100.0f, 1600.0f, 0.2f, true);
            CHECK(p.count == 3 && p.layer[0].kit == st::Kit::Machine && p.layer[0].after == 0.0f);
            CHECK(p.layer[1].kit == st::Kit::ShamanThunder && p.layer[1].after == 0.0f && p.layer[1].gain == p.layer[0].gain);
            CHECK(p.layer[2].kit == st::Kit::Rumble && Near(p.layer[2].gain, 0.8f * p.layer[0].gain, 1e-6f) && p.layer[2].after > 0.0f);
            p = st::PlanThunder(100.0f, 1600.0f, 0.6f, true);
            CHECK(p.layer[0].kit == st::Kit::ShamanThunder && p.layer[1].kit == st::Kit::Machine && p.layer[1].after == 0.0f);
            p = st::PlanThunder(100.0f, 1600.0f, 0.9f, true);
            CHECK(p.layer[0].kit == st::Kit::CallLightning && p.layer[1].kit == st::Kit::ShamanThunder);
            // From 150 to 250 yd: one crack, the rumble after it.
            p = st::PlanThunder(200.0f, 1600.0f, 0.2f, true);
            CHECK(p.count == 2 && p.layer[0].kit == st::Kit::Machine && p.layer[1].kit == st::Kit::Rumble && Near(p.layer[1].gain, 0.8f * p.layer[0].gain, 1e-6f));
            // Half the near strikes are the machine's.
            unsigned machine = 0;
            st::Rng rng;
            for (int i = 0; i < 1000; ++i) machine += st::PlanThunder(120.0f, 1600.0f, rng.Uniform(), true).layer[0].kit == st::Kit::Machine ? 1 : 0;
            CHECK(machine > 430 && machine < 570);
            // Mid and distant: the Blasted Lands takes (Zul'Gurub's row), the rumble after them.
            for (float d : { 250.0f, 400.0f, 800.0f, 1199.0f })
            {
                p = st::PlanThunder(d, 1600.0f, 0.1f, true);
                CHECK(p.count == 2 && p.layer[0].kit == st::Kit::Bolt && p.layer[1].kit == st::Kit::Rumble && p.layer[1].after > 0.0f);
                CHECK(Near(p.layer[0].gain, st::ThunderGain(d, 1600.0f), 1e-6f));
            }
            // Very far: the rumble alone, a little louder than as a tail.
            p = st::PlanThunder(1400.0f, 1600.0f, 0.1f, true);
            CHECK(p.count == 1 && p.layer[0].kit == st::Kit::Rumble && p.layer[0].after == 0.0f && p.layer[0].gain > st::ThunderGain(1400.0f, 1600.0f));
            // The rumble off: cracks and bolts only, and very far strikes keep their bolt.
            CHECK(st::PlanThunder(100.0f, 1600.0f, 0.1f, false).count == 2 && st::PlanThunder(200.0f, 1600.0f, 0.1f, false).count == 1);
            p = st::PlanThunder(1400.0f, 1600.0f, 0.1f, false);
            CHECK(p.count == 1 && p.layer[0].kit == st::Kit::Bolt);
            // Silent beyond the distance (the setting), and for no distance setting at all.
            CHECK(st::PlanThunder(1700.0f, 1600.0f, 0.1f, true).count == 0 && st::PlanThunder(100.0f, 0.0f, 0.1f, true).count == 0);
            CHECK(st::PlanThunder(std::nanf(""), 1600.0f, 0.1f, true).count == 0);
            // Never Saurfang's bolt, never Netherstorm's crack (nor its quieter rows, TK_Lightning_Slow and UL_lightning).
            for (const st::KitInfo& k : st::kKits)
                CHECK(k.soundKitId != 17203 && k.soundKitId != 9843 && k.soundKitId != 10007 && k.soundKitId != 12779 &&
                      std::strstr(k.name, "Saurfang") == nullptr && std::strstr(k.name, "Netherstorm") == nullptr);
            for (const st::LuaRow& r : st::kLuaRows)
                CHECK(r.soundEntryId != 17203 && r.soundEntryId != 9843 && r.soundEntryId != 10007 && r.soundEntryId != 12779);
            // Every name fits the DBC's 31 characters (Lua PlaySound looks them up as stored).
            for (const st::KitInfo& k : st::kKits) CHECK(std::strlen(k.name) <= 31);
            for (const st::LuaRow& r : st::kLuaRows) CHECK(std::strlen(r.name) <= 31);
        });

        Test("thunder's volumes: louder than before, never above full for any layer, distance, setting or nonsense", []
        {
            // Every layer the plan makes, over the distances, the picks and the slider's whole range (and past it): its
            // volume as the client gets it within 0..1, and the scale the native path passes x the kit's volume too.
            bool inRange = true;
            for (float d = 0.0f; d <= 1700.0f; d += 13.0f)
                for (float pick : { 0.1f, 0.6f, 0.9f })
                    for (float setting : { 0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 10.0f })
                    {
                        const st::ThunderPlan p = st::PlanThunder(d, 1600.0f, pick, true);
                        for (int i = 0; i < p.count; ++i)
                        {
                            const float v = st::LayerVolume(p.layer[i].kit, p.layer[i].gain, setting);
                            const float scale = v / st::Info(p.layer[i].kit).volume;
                            inRange = inRange && v >= 0.0f && v <= 1.0f && scale * st::Info(p.layer[i].kit).volume <= 1.0f;
                        }
                    }
            CHECK(inRange);
            CHECK(st::LayerVolume(st::Kit::Machine, 1.0f, 3.0f) == 1.0f && st::LayerVolume(st::Kit::Machine, 1.0f, -1.0f) == 0.0f &&
                  st::LayerVolume(st::Kit::Machine, std::nanf(""), 1.0f) == 0.0f && Near(st::LayerVolume(st::Kit::Bolt, 0.5f, 1.0f), 0.5f, 1e-6f));
            // The main layer (the crack or the bolt) at 100, 250, 600 and 1000 yd: before, (60/d)^0.6 at 1; now at 1.5.
            std::printf("  thunder's main layer (the client's volume), before -> now:");
            for (float d : { 100.0f, 250.0f, 600.0f, 1000.0f })
            {
                const st::ThunderPlan p = st::PlanThunder(d, 1600.0f, 0.1f, true);
                const st::Kit kit = p.layer[0].kit;
                const float before = std::fmin(1.0f, st::Info(kit).volume * std::pow(60.0f / std::fmax(d, 60.0f), 0.6f));
                const float now = st::LayerVolume(kit, p.layer[0].gain, st::kThunderVolume);
                CHECK(now > before);
                std::printf(" %.0f yd %.2f -> %.2f%s;", d, before, now, p.count == 3 ? " (+ a second crack)" : "");
            }
            std::printf("\n");
            static_assert(st::kThunderVolume == 1.5f, "ThunderVolume's default");
        });

        Test("thunder's Lua fallback: the row nearest the volume; none when every row is far too loud", []
        {
            CHECK(std::strcmp(st::LuaRowFor(st::Kit::Machine, 0.8f), "Goblin_Weather_Machine_Lightnin") == 0);
            CHECK(std::strcmp(st::LuaRowFor(st::Kit::Machine, 0.5f), "Goblin_Weather_Machine_Lightnin") == 0); // 1.6x: still played
            CHECK(st::LuaRowFor(st::Kit::Machine, 0.3f) == nullptr);                                          // 2.7x: left out
            CHECK(std::strcmp(st::LuaRowFor(st::Kit::Bolt, 1.0f), "LightningBolt - ZulGurub") == 0);
            CHECK(std::strcmp(st::LuaRowFor(st::Kit::Bolt, 0.6f), "ChainHeal") == 0);                          // the 0.8 row of the same takes
            CHECK(std::strcmp(st::LuaRowFor(st::Kit::Rumble, 0.9f), "EarthquakeCameraShake (High Vol") == 0);
            CHECK(std::strcmp(st::LuaRowFor(st::Kit::Rumble, 0.35f), "EarthquakeCameraShake") == 0);
            CHECK(std::strcmp(st::LuaRowFor(st::Kit::Rumble, 0.15f), "EarthquakeCameraShake low volum") == 0);
            CHECK(std::strcmp(st::LuaRowFor(st::Kit::Rumble, 0.1f), "EarthquakeCameraShake low volum") == 0);
            CHECK(st::LuaRowFor(st::Kit::Rumble, 0.05f) == nullptr && st::LuaRowFor(st::Kit::Rumble, 0.0f) == nullptr);
            // Every kit has a row at its own volume.
            for (int i = 0; i < static_cast<int>(st::Kit::Count); ++i)
                CHECK(st::LuaRowFor(static_cast<st::Kit>(i), st::kKits[i].volume) != nullptr);
            // A whole storm through Lua: a far strike's bolt at its volume plays (the 0.8 row); at a quiet setting a bolt
            // with only loud rows to play it is heard as its rumble.
            const st::ThunderPlan p = st::PlanThunder(1100.0f, 1600.0f, 0.1f, true);
            CHECK(p.count == 2 && st::LuaRowFor(p.layer[0].kit, st::LayerVolume(p.layer[0].kit, p.layer[0].gain, 1.5f)) != nullptr);
            CHECK(st::LuaRowFor(p.layer[0].kit, st::LayerVolume(p.layer[0].kit, p.layer[0].gain, 0.5f)) == nullptr &&
                  st::LuaRowFor(p.layer[1].kit, st::LayerVolume(p.layer[1].kit, p.layer[1].gain, 0.5f)) != nullptr);
        });

        Test("thunder's direction: each positional kit is placed inside its MinDistance, so the client's rolloff keeps our volume", []
        {
            // The client's rolloff (ComputeRolloff 0x8782B0): 1 inside min, then min / ((d - min) x 4 + min), 0 at max.
            CHECK(st::ClientRolloff(18.0f, 45.0f, 10.0f) == 1.0f && st::ClientRolloff(18.0f, 45.0f, 18.0f) == 1.0f);
            CHECK(Near(st::ClientRolloff(18.0f, 45.0f, 27.0f), 18.0f / 54.0f, 1e-6f) && st::ClientRolloff(18.0f, 45.0f, 45.0f) == 0.0f);
            CHECK(st::ClientRolloff(50.0f, 100.0f, 95.0f) < st::ClientRolloff(50.0f, 100.0f, 89.0f));
            for (int i = 0; i < static_cast<int>(st::Kit::Count); ++i)
            {
                const st::KitInfo& k = st::kKits[i];
                const float place = st::PlacementDistance(static_cast<st::Kit>(i));
                if (!k.positional) { CHECK(place == 0.0f); continue; }
                CHECK(place > 1.0f && place <= 6.0f && place <= 0.25f * k.minDistance);
                CHECK(st::ClientRolloff(k.minDistance, k.cutoff, place) == 1.0f);
                // ... with room for the listener to move off: still full volume 3x as far.
                CHECK(st::ClientRolloff(k.minDistance, k.cutoff, 3.0f * place) == 1.0f);
            }
            CHECK(st::PlacementDistance(st::Kit::Machine) == 4.5f && st::PlacementDistance(st::Kit::Bolt) == 6.0f);
            CHECK(!st::kKits[static_cast<int>(st::Kit::Rumble)].positional); // a long, deep rumble: straight, at its volume
        });

        Test("thunder through the client's call: what its results mean for the Lua fallback", []
        {
            CHECK(st::Outcome(0) == st::SoundOutcome::Played);
            for (int declined : { 9, 10, 11, 14, 15, 17 }) CHECK(st::Outcome(declined) == st::SoundOutcome::Declined);
            for (int problem : { 5, 6, 8, 16, 20, -1, 99 }) CHECK(st::Outcome(problem) == st::SoundOutcome::Problem);
            static_assert(st::kSoundPropsVolume == 4 && st::kSoundPropsVolumeScale == 8 && st::kSoundPropsSize == 0xE8, "SE2SoundKitProperties");
        });

        Test("rain level: 0 up to light rain, 1 from heavy rain, rain only", []
        {
            CHECK(st::RainLevel(1, 0.0f) == 0.0f && st::RainLevel(1, 0.27f) == 0.0f);
            CHECK(Near(st::RainLevel(1, 0.485f), 0.5f, 1e-5f));
            CHECK(st::RainLevel(1, 0.70f) == 1.0f && st::RainLevel(1, 1.0f) == 1.0f);
            CHECK(st::RainLevel(2, 0.9f) == 0.0f && st::RainLevel(0, 0.9f) == 0.0f && st::RainLevel(1, std::nanf("")) == 0.0f);
            float last = -1.0f;
            for (float g = 0.28f; g < 0.70f; g += 0.01f) { const float r = st::RainLevel(1, g); CHECK(r > last); last = r; }
        });

        Test("fog wake in weather: dry calm fog as before; heavy rain leaves almost no wake and it fills in fast; wind helps", []
        {
            // Dry and calm (a foggy forest): nothing changes, exactly.
            for (float wind : { 0.0f, 0.1f, 0.3f, 0.35f })
            {
                const float m = fw::WeatherMixing(0.0f, 0.0f, wind, 1.0f, 0.5f);
                const fw::WakeScale k = fw::ScaleFor(m);
                CHECK(m == 0.0f && k.push == 1.0f && k.fill == 1.0f);
            }
            // Heavy rain (or a full storm): a twentieth of the push, a fifth of the fill-in time.
            const float wet[3][2] = { { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f } };
            for (const auto& w : wet)
            {
                const fw::WakeScale k = fw::ScaleFor(fw::WeatherMixing(w[0], w[1], 0.0f, 1.0f, 0.5f));
                CHECK(Near(k.push, 0.05f, 1e-6f) && Near(k.fill, 0.2f, 1e-6f));
                CHECK(0.9f * k.push < 0.05f && 3.0f * k.fill <= 0.6f + 1e-6f); // the defaults' 0.9 push and 3 s: "almost no wake"
            }
            // In between it grows with the rain: medium rain already halves the wake.
            float last = 0.0f;
            for (float g = 0.28f; g <= 0.71f; g += 0.01f)
            {
                const float m = fw::WeatherMixing(st::RainLevel(1, g), st::Storminess(1, g), 0.0f, 1.0f, 0.5f);
                CHECK(m > last - 1e-6f && m <= 1.0f);
                last = m;
            }
            CHECK(fw::ScaleFor(fw::WeatherMixing(st::RainLevel(1, 0.55f), st::Storminess(1, 0.55f), 0.0f, 1.0f, 0.5f)).push < 0.5f);
            // Wind: from a fresh breeze up, a strong one at half by default; with rain they add like chances.
            CHECK(fw::WeatherMixing(0.0f, 0.0f, 1.0f, 1.0f, 0.5f) == 0.5f && fw::WeatherMixing(0.0f, 0.0f, 2.0f, 1.0f, 0.5f) == 0.5f);
            CHECK(fw::WeatherMixing(0.0f, 0.0f, 0.6f, 1.0f, 0.5f) > 0.0f && fw::WeatherMixing(0.0f, 0.0f, 0.6f, 1.0f, 0.5f) < 0.5f);
            CHECK(Near(fw::WeatherMixing(0.5f, 0.0f, 1.0f, 1.0f, 0.5f), 0.75f, 1e-6f));
            // The settings at 0: weather changes nothing; NaN in, sane out.
            CHECK(fw::WeatherMixing(1.0f, 1.0f, 2.0f, 0.0f, 0.0f) == 0.0f);
            CHECK(fw::WeatherMixing(std::nanf(""), std::nanf(""), std::nanf(""), 1.0f, 0.5f) == 0.0f);
            CHECK(fw::ScaleFor(std::nanf("")).push == 1.0f && fw::ScaleFor(-1.0f).fill == 1.0f && Near(fw::ScaleFor(5.0f).push, fw::kPushInFullMix, 1e-6f));
        });

        Test("colours: no shade leaves them exactly; the storm darkens the sky most; a flash brightens; alpha kept", []
        {
            const uint32_t sky = Argb(0x80, 100, 150, 220), light = Argb(0xFF, 200, 190, 170), sun = Argb(0xFF, 255, 240, 200);
            const st::Shade none{};
            CHECK(st::Identity(none));
            for (int band = 0; band < st::kBandCount; ++band) CHECK(st::ShadeBand(sky, band, none) == sky);
            st::Shade storm;
            storm.dark = 0.35f;
            storm.desat = 0.6f * storm.dark;
            const uint32_t darkSky = st::ShadeBand(sky, 3, storm), darkLight = st::ShadeBand(light, 0, storm), darkAmbient = st::ShadeBand(light, 1, storm);
            CHECK((darkSky >> 24) == 0x80);
            CHECK(Sum(darkSky) < Sum(sky) * 0.7f);
            CHECK(Sum(darkLight) < Sum(light) && Sum(darkLight) > Sum(light) * 0.75f); // the light less than the sky
            CHECK(Sum(darkAmbient) > Sum(darkLight));
            CHECK(B(darkSky) - R(darkSky) < (B(sky) - R(sky)) * 0.65f);               // greyer, not only darker
            CHECK(Sum(st::ShadeBand(sun, 9, storm)) < Sum(sun) * 0.75f);               // the sun dims
            st::Shade flash;
            flash.sky = 1.0f;
            const uint32_t night = Argb(0xFF, 10, 15, 30);
            const uint32_t litNight = st::ShadeBand(night, 4, flash);
            CHECK(B(litNight) > 150 && B(litNight) > R(litNight));                     // bluish white on a night sky
            CHECK(st::ShadeBand(night, 0, flash) == night);                            // a flash in the clouds doesn't light the ground
            st::Shade strike;
            strike.scene = 1.0f;
            CHECK(Sum(st::ShadeBand(night, 1, strike)) > Sum(night) + 200);            // a near strike lights the scene
            const uint32_t white = Argb(0xFF, 250, 250, 250);
            const uint32_t clamped = st::ShadeBand(white, 4, flash);
            CHECK(R(clamped) == 255 && G(clamped) == 255 && B(clamped) == 255);
        });

        // Elwynn's light is Light.dbc 1 (the Eastern Kingdoms default). In rain the client swaps in its storm set
        // (LightParams 10), whose fog is one colour for every hour: #525452. Its clear set's fog: #000E21 at night,
        // #4D788F at noon, #5B3D4B at 21:00.
        Test("colours: heavy rain's fog (band 8) stays at least 0.9 of its brightness at night, 1.2 by day, and turns cool", []
        {
            const st::Shade night = HeavyRain(0.0f), day = HeavyRain(1.0f);
            const uint32_t elwynnStorm = Argb(0xFF, 0x52, 0x54, 0x52);
            // Before: darkened by 0.70 x 0.35 and greyed, #3E3F3E by day and by night alike.
            CHECK(ShadeBefore(elwynnStorm, 8, night) == Argb(0xFF, 0x3E, 0x3F, 0x3E));
            const uint32_t fogNight = st::ShadeBand(elwynnStorm, st::kFogBand, night), fogDay = st::ShadeBand(elwynnStorm, st::kFogBand, day);
            std::printf("     Elwynn's storm fog #525452 (L %.0f): before #3E3F3E (L %.0f); night #%06X (L %.0f), day #%06X (L %.0f)\n",
                        Lum(elwynnStorm), Lum(ShadeBefore(elwynnStorm, 8, night)), fogNight & 0xFFFFFF, Lum(fogNight), fogDay & 0xFFFFFF, Lum(fogDay));
            CHECK(Lum(fogNight) >= 0.9f * Lum(elwynnStorm) && Lum(fogNight) <= Lum(elwynnStorm));    // dim at night, not darker than the client's
            CHECK(Lum(fogDay) >= 1.2f * Lum(elwynnStorm));                                          // clearly bright by day
            CHECK(Lum(fogNight) > 1.2f * Lum(ShadeBefore(elwynnStorm, 8, night)));                  // brighter than the dirty grey
            CHECK(B(fogNight) > G(fogNight) && G(fogNight) > R(fogNight) && B(fogNight) >= R(fogNight) + 10); // a blue-grey
            CHECK(B(fogDay) > G(fogDay) && G(fogDay) > R(fogDay) && B(fogDay) >= R(fogDay) + 10);
            CHECK((fogNight >> 24) == 0xFF);

            // Whatever the client hands over (its clear colours, if the storm set isn't all in yet; a warm fog): at least
            // 0.9 of its brightness at night and 1.2 by day, never warmer, and the warm ones turn cool.
            const uint32_t inputs[] = { elwynnStorm, Argb(0xFF, 0x00, 0x0E, 0x21), Argb(0xFF, 0x4D, 0x78, 0x8F),
                                        Argb(0xFF, 0x5B, 0x3D, 0x4B), Argb(0xFF, 110, 95, 70), Argb(0xFF, 90, 90, 60) };
            for (uint32_t in : inputs)
            {
                const uint32_t n = st::ShadeBand(in, st::kFogBand, night), d = st::ShadeBand(in, st::kFogBand, day);
                CHECK(Lum(n) >= 0.9f * Lum(in) - 0.5f);
                CHECK(Lum(d) >= 1.2f * Lum(in) - 0.5f);
                CHECK(Lum(d) > Lum(n) || Lum(in) < 2.0f);
                CHECK(B(n) >= R(n) && B(d) >= R(d));                                                  // b >= r: not brown
                CHECK(R(n) - B(n) <= std::max(0, R(in) - B(in)) && G(n) - B(n) <= std::max(0, G(in) - B(in)) + 1);
                if (R(in) > B(in)) CHECK(R(n) - B(n) < R(in) - B(in) && R(d) - B(d) < R(in) - B(in)); // warm turns cool
            }
        });

        Test("colours: in heavy rain the fog is lighter than the sky; the sky and clouds darken exactly as before; flashes too", []
        {
            for (float day : { 0.0f, 0.5f, 1.0f })
            {
                st::Shade s = HeavyRain(day);
                // Elwynn's storm set: the sky (bands 3..7, #6F6F6F at the top to #525352 at the horizon) and its fog.
                const uint32_t fog = st::ShadeBand(Argb(0xFF, 0x52, 0x54, 0x52), st::kFogBand, s);
                CHECK(Lum(fog) > Lum(st::ShadeBand(Argb(0xFF, 0x6F, 0x6F, 0x6F), 3, s)));
                CHECK(Lum(fog) > Lum(st::ShadeBand(Argb(0xFF, 0x52, 0x53, 0x52), 7, s)));
                for (int pass = 0; pass < 2; ++pass)
                {
                    if (pass) { s.sky = 0.6f; s.scene = 0.3f; } // and with a flash going
                    for (int band : { 2, 3, 4, 5, 6, 7, 11, 12, 13 })
                        for (uint32_t c : { Argb(0x80, 100, 150, 220), Argb(0xFF, 0x6F, 0x6F, 0x6F), Argb(0xFF, 10, 15, 30) })
                            CHECK(st::ShadeBand(c, band, s) == ShadeBefore(c, band, s));
                }
            }
            // The fog's settings touch only the fog: every other band as with them off.
            st::Shade on = HeavyRain(1.0f), off = on;
            off.fog = 0.0f; off.fogBright = 1.0f; off.fogCool = 0.0f;
            for (int band = 0; band < st::kBandCount; ++band)
                if (band != st::kFogBand) CHECK(st::ShadeBand(Argb(0xFF, 120, 110, 90), band, on) == st::ShadeBand(Argb(0xFF, 120, 110, 90), band, off));
            // A flash lights the fog as before: on its own exactly so, and on top of the storm's fog by as much.
            st::Shade flash;
            flash.sky = 1.0f;
            flash.scene = 0.5f;
            const uint32_t nightFog = Argb(0xFF, 0x00, 0x0E, 0x21);
            CHECK(st::ShadeBand(nightFog, st::kFogBand, flash) == ShadeBefore(nightFog, 8, flash));
            st::Shade stormFlash = HeavyRain(0.0f);
            stormFlash.sky = 1.0f;
            const uint32_t lit = st::ShadeBand(Argb(0xFF, 0x52, 0x54, 0x52), st::kFogBand, stormFlash);
            const uint32_t unlit = st::ShadeBand(Argb(0xFF, 0x52, 0x54, 0x52), st::kFogBand, HeavyRain(0.0f));
            CHECK(Near(float(B(lit) - B(unlit)), 255.0f * st::kFlashGain * 0.7f * 1.0f, 1.01f));
        });

        Test("colours: the fog's floor, its ceiling, its settings off, no storm, nonsense", []
        {
            const uint32_t grey = Argb(0xFF, 0x52, 0x54, 0x52);
            // No storm for the fog: the fog as the client made it, whatever else shades.
            st::Shade calm;
            calm.dark = 0.35f;
            calm.desat = 0.21f;
            CHECK(st::ShadeBand(grey, st::kFogBand, calm) == grey);
            st::Shade only;
            only.fog = 1.0f;
            CHECK(!st::Identity(only));
            // Settings off (brightness 0, no cool): only the storm's darkening, a third of the sky's (0.30 x 0.35), no tint.
            st::Shade bare = HeavyRain(1.0f);
            bare.fogBright = 0.0f;
            bare.fogCool = 0.0f;
            const uint32_t plain = st::ShadeBand(Argb(0xFF, 100, 100, 100), st::kFogBand, bare);
            CHECK(R(plain) == G(plain) && G(plain) == B(plain) && Near(float(R(plain)), 100.0f * (1.0f - 0.35f * 0.30f), 0.51f));
            // The floor holds against any darkness.
            st::Shade black = HeavyRain(0.0f);
            black.dark = 1.0f;
            CHECK(Lum(st::ShadeBand(grey, st::kFogBand, black)) >= 0.9f * Lum(grey));
            // Its lift stops at the ceiling; a fog already brighter keeps its brightness.
            const uint32_t mid = st::ShadeBand(Argb(0xFF, 150, 150, 150), st::kFogBand, HeavyRain(1.0f));
            CHECK(Near(Lum(mid), 255.0f * st::kFogCeiling, 1.0f));
            const uint32_t bright = st::ShadeBand(Argb(0xFF, 200, 200, 200), st::kFogBand, HeavyRain(1.0f));
            CHECK(Lum(bright) >= 0.9f * 200.0f && Lum(bright) <= 201.0f);
            // Half a storm: half way.
            st::Shade half = HeavyRain(1.0f);
            half.fog = 0.5f;
            half.dark = 0.175f;
            const float l = Lum(st::ShadeBand(grey, st::kFogBand, half));
            CHECK(l > Lum(grey) && l < Lum(st::ShadeBand(grey, st::kFogBand, HeavyRain(1.0f))));
            // Nonsense in, sense out.
            st::Shade bad = HeavyRain(std::nanf(""));
            bad.fogBright = std::nanf("");
            bad.fogCool = std::nanf("");
            const uint32_t out = st::ShadeBand(grey, st::kFogBand, bad);
            CHECK((out >> 24) == 0xFF && Sum(out) > 0 && Sum(out) <= Sum(grey));
        });

        Test("colours: shading the same bands twice (the client didn't recompute them) doesn't darken them further", []
        {
            uint32_t bands[st::kBandCount];
            for (int i = 0; i < st::kBandCount; ++i) bands[i] = Argb(0xFF, 40 + 10 * i, 90, 200 - 5 * i);
            uint32_t original[st::kBandCount];
            std::memcpy(original, bands, sizeof(bands));
            st::Shade s;
            s.dark = 0.5f;
            s.desat = 0.3f;
            st::BandMemory memory;
            st::ShadeBands(bands, s, memory);
            uint32_t once[st::kBandCount];
            std::memcpy(once, bands, sizeof(bands));
            st::ShadeBands(bands, s, memory);                 // stale: same input as last written
            CHECK(std::memcmp(bands, once, sizeof(bands)) == 0);
            std::memcpy(bands, original, sizeof(bands));       // the client recomputed them
            st::ShadeBands(bands, s, memory);
            CHECK(std::memcmp(bands, once, sizeof(bands)) == 0);
            bands[2] = Argb(0xFF, 10, 20, 30);                 // a new value the client computed
            st::ShadeBands(bands, s, memory);
            CHECK(bands[2] == st::ShadeBand(Argb(0xFF, 10, 20, 30), 2, s));
            st::ShadeBands(bands, st::Shade{}, memory);        // no shade: nothing written, memory forgotten
            CHECK(!memory.valid && bands[2] == st::ShadeBand(Argb(0xFF, 10, 20, 30), 2, s));
        });

        // Winterspring at Frostfire Hot Springs (map 28.1, 34.8), 15:20, snowing: 1006 yd from the centre of Winterspring's
        // lilac light (Light.dbc 260, falloff 711..941 yd), so the client shows Kalimdor's default light (191). Its clear
        // set (192) at 15:20: fog #526275, the sky #112744 to #BBA181, the fog from 122 to 490 yd. Its weather set
        // (LightParams 10, the same as the Eastern Kingdoms' default): fog #525452, the sky #6F6F6F, #6D6D6D, #6F6F6F,
        // #676767, #525352 (LightIntBand 2..6 = bands 3..7), clouds #3D3F3E, the light #4E4E5F / #656565, the fog from
        // -139 to 278 yd. Two minutes later at 30.2, 42.6, inside the lilac light, its weather set: fog #784E9A, the sky
        // from #FB9FFF, the fog from 0 to 381 yd.
        Test("snow: the weather set's grey fog turns a bright pale blue-white by day, stays the client's at night, never darker", []
        {
            const uint32_t grey = Argb(0xFF, 0x52, 0x54, 0x52);
            const uint32_t day = st::ShadeBand(grey, st::kFogBand, Snow(1.0f)), dusk = st::ShadeBand(grey, st::kFogBand, Snow(0.5f)),
                           night = st::ShadeBand(grey, st::kFogBand, Snow(0.0f));
            std::printf("     the weather fog #525452 (L %.0f) in snow: day #%06X (L %.0f), dusk #%06X (L %.0f), night #%06X (L %.0f)\n",
                        Lum(grey), day & 0xFFFFFF, Lum(day), dusk & 0xFFFFFF, Lum(dusk), night & 0xFFFFFF, Lum(night));
            CHECK(Lum(day) >= 2.1f * Lum(grey) && Lum(day) <= 255.0f * st::kSnowCeiling + 1.0f);  // bright by day
            CHECK(B(day) > G(day) && G(day) > R(day) && B(day) >= R(day) + 10);                    // a pale blue-white
            CHECK(Lum(dusk) > Lum(grey) + 20.0f && Lum(dusk) < Lum(day) - 20.0f);
            CHECK(Near(Lum(night), Lum(grey), 1.0f) && B(night) > R(night));                       // the client's at night, cool
            CHECK((day >> 24) == 0xFF);
            CHECK(Lum(day) > Lum(Argb(0xFF, 0x52, 0x62, 0x75)) + 50.0f);                           // brighter than the clear day's fog

            // Never darker than the client's own colour (by more than rounding), any band, any hour, any colour; the
            // light, the sun, band 2 and the water exactly as the client made them.
            const uint32_t inputs[] = { grey, Argb(0xFF, 0x00, 0x0E, 0x21), Argb(0xFF, 0x78, 0x4E, 0x9A), Argb(0xFF, 0x74, 0xA7, 0xC4),
                                        Argb(0xFF, 0x3B, 0x3B, 0x3B), Argb(0xFF, 0x4F, 0x4F, 0x57), Argb(0xFF, 110, 95, 70),
                                        Argb(0xFF, 0, 0, 0), Argb(0xFF, 255, 255, 255), Argb(0x80, 0xE0, 0xE0, 0xE0) };
            for (float hour : { 0.0f, 0.25f, 0.5f, 1.0f })
                for (int band = 0; band < st::kBandCount; ++band)
                    for (uint32_t in : inputs)
                    {
                        const uint32_t out = st::ShadeBand(in, band, Snow(hour));
                        CHECK(Lum(out) >= Lum(in) - 1.0f);
                        CHECK((out >> 24) == (in >> 24));
                        if (st::SnowWeight(band) <= 0.0f) CHECK(out == in);
                    }
            for (int band : { 0, 1, 2, 9, 10, 14, 15, 16, 17 }) CHECK(st::SnowWeight(band) == 0.0f);
        });

        Test("snow: the sky and clouds lift with the fog (the fogged distance meets a sky as bright); a zone's coloured light stays", []
        {
            const uint32_t fog = st::ShadeBand(Argb(0xFF, 0x52, 0x54, 0x52), st::kFogBand, Snow(1.0f));
            const uint32_t top = st::ShadeBand(Argb(0xFF, 0x6F, 0x6F, 0x6F), 3, Snow(1.0f));
            const uint32_t horizon = st::ShadeBand(Argb(0xFF, 0x52, 0x53, 0x52), 7, Snow(1.0f));
            const uint32_t cloud = st::ShadeBand(Argb(0xFF, 0x3D, 0x3F, 0x3E), 11, Snow(1.0f));
            std::printf("     the weather sky in snow by day: top #6F6F6F -> #%06X, horizon #525352 -> #%06X; clouds #3D3F3E -> #%06X\n",
                        top & 0xFFFFFF, horizon & 0xFFFFFF, cloud & 0xFFFFFF);
            CHECK(std::fabs(Lum(horizon) - Lum(fog)) <= 4.0f);   // the fog meets the horizon
            CHECK(Lum(top) >= Lum(fog) - 1.0f && Lum(top) <= 255.0f * st::kSnowCeiling + 1.0f);
            CHECK(Lum(cloud) >= 2.1f * Lum(Argb(0xFF, 0x3D, 0x3F, 0x3E)));
            // Winterspring's own weather set (lilac fog, pink sky top) and Dun Morogh's sky-blue fog as the client has them;
            // a slightly tinted grey (Winterspring's eastern #4F4F57) lifts like the grey.
            const uint32_t lilac = Argb(0xFF, 0x78, 0x4E, 0x9A), pink = Argb(0xFF, 0xFB, 0x9F, 0xFF), blue = Argb(0xFF, 0x74, 0xA7, 0xC4);
            CHECK(st::ShadeBand(lilac, st::kFogBand, Snow(1.0f)) == lilac);
            CHECK(st::ShadeBand(pink, 3, Snow(1.0f)) == pink);
            CHECK(st::ShadeBand(blue, st::kFogBand, Snow(1.0f)) == blue);
            const uint32_t east = Argb(0xFF, 0x4F, 0x4F, 0x57);
            CHECK(Lum(st::ShadeBand(east, st::kFogBand, Snow(1.0f))) >= 2.1f * Lum(east));
        });

        Test("snow: its share, its ceiling, its settings off, rain untouched, nonsense", []
        {
            const uint32_t grey = Argb(0xFF, 0x52, 0x54, 0x52);
            // Half the weather set showing: half way.
            const float half = Lum(st::ShadeBand(grey, st::kFogBand, Snow(1.0f, 0.5f)));
            CHECK(half > Lum(grey) + 20.0f && half < Lum(st::ShadeBand(grey, st::kFogBand, Snow(1.0f))) - 20.0f);
            // The lift stops at the ceiling; a band already brighter keeps its brightness.
            CHECK(Near(Lum(st::ShadeBand(Argb(0xFF, 160, 160, 160), st::kFogBand, Snow(1.0f))), 255.0f * st::kSnowCeiling, 1.0f));
            const uint32_t bright = st::ShadeBand(Argb(0xFF, 224, 224, 224), st::kFogBand, Snow(1.0f));
            CHECK(Lum(bright) >= 223.0f && Lum(bright) <= 225.0f);
            // Settings off (x1, no white): as the client made it, exactly.
            st::Shade off = Snow(1.0f);
            off.snowBright = 1.0f;
            off.snowWhite = 0.0f;
            for (int band = 0; band < st::kBandCount; ++band) CHECK(st::ShadeBand(grey, band, off) == grey);
            // Snow alone is a shade; without it the snow settings change nothing, and the rain's shade is as before.
            st::Shade only;
            only.snow = 1.0f;
            CHECK(!st::Identity(only));
            for (int band = 0; band < st::kBandCount; ++band)
            {
                st::Shade rain = HeavyRain(1.0f), rainSettings = rain;
                rainSettings.snowBright = 3.0f;
                rainSettings.snowWhite = 1.0f;
                CHECK(st::ShadeBand(grey, band, rain) == st::ShadeBand(grey, band, rainSettings));
                CHECK(st::ShadeBand(grey, band, st::Shade{}) == grey);
            }
            // Nonsense in, sense out: never darker, never a NaN colour.
            st::Shade bad = Snow(std::nanf(""));
            bad.snowBright = std::nanf("");
            bad.snowWhite = std::nanf("");
            const uint32_t out = st::ShadeBand(grey, st::kFogBand, bad);
            CHECK((out >> 24) == 0xFF && Lum(out) >= Lum(grey) - 1.0f && Lum(out) <= Lum(grey) + 1.0f);
            st::Shade nanShare = Snow(1.0f);
            nanShare.snow = std::nanf("");
            CHECK(st::ShadeBand(grey, st::kFogBand, nanShare) == grey);
            st::Shade dim = Snow(1.0f);
            dim.snowBright = 0.2f; // below 1 doesn't darken
            CHECK(Lum(st::ShadeBand(grey, st::kFogBand, dim)) >= Lum(grey) - 1.0f);
        });

        Test("snow: the weather fog starts at the camera, not 139 yd behind it, and ends no nearer than 400 yd", []
        {
            const st::FogDistance weather{ 278.0f, -0.5f }, clear{ 490.0f, 0.25f }, lilac{ 381.0f, 0.0f };
            const st::FogDistance snow = st::SnowFogDistance(weather, 1.0f, 400.0f);
            std::printf("     the weather fog in snow: %.0f..%.0f yd -> %.0f..%.0f yd; fogged at 70 yd %.2f -> %.2f, at 150 yd %.2f -> %.2f (clear %.2f, %.2f)\n",
                        weather.end * weather.startMul, weather.end, snow.end * snow.startMul, snow.end, FogShare(weather, 70.0f),
                        FogShare(snow, 70.0f), FogShare(weather, 150.0f), FogShare(snow, 150.0f), FogShare(clear, 70.0f), FogShare(clear, 150.0f));
            CHECK(snow.end == 400.0f && snow.startMul == 0.0f);
            CHECK(Near(FogShare(weather, 0.0f), 1.0f / 3.0f, 0.01f) && FogShare(snow, 0.0f) == 0.0f);
            CHECK(FogShare(weather, 70.0f) >= 0.49f && FogShare(snow, 70.0f) <= 0.18f);
            CHECK(FogShare(snow, 150.0f) > FogShare(clear, 150.0f));          // still shorter views than clear weather
            // A zone's own longer or clearer fog stays (the clear set; Winterspring's lilac only to 400).
            const st::FogDistance c = st::SnowFogDistance(clear, 1.0f, 400.0f), l = st::SnowFogDistance(lilac, 1.0f, 400.0f);
            CHECK(c.end == 490.0f && c.startMul == 0.25f && l.end == 400.0f && l.startMul == 0.0f);
            const st::FogDistance none{ 1.0e10f, 0.5f };
            CHECK(st::SnowFogDistance(none, 1.0f, 400.0f).end == 1.0e10f);
            // By the share; not without snow, with the setting off, or for nonsense.
            const st::FogDistance halfway = st::SnowFogDistance(weather, 0.5f, 400.0f);
            CHECK(Near(halfway.end, 339.0f, 0.01f) && Near(halfway.startMul, -0.25f, 1e-6f));
            for (float share : { 0.0f, -1.0f, std::nanf("") })
            {
                const st::FogDistance same = st::SnowFogDistance(weather, share, 400.0f);
                CHECK(same.end == weather.end && same.startMul == weather.startMul);
            }
            for (float floor : { 0.0f, -5.0f, std::nanf(""), INFINITY })
            {
                const st::FogDistance same = st::SnowFogDistance(weather, 1.0f, floor);
                CHECK(same.end == weather.end && same.startMul == weather.startMul);
            }
            const st::FogDistance nanEnd = st::SnowFogDistance({ std::nanf(""), -0.5f }, 1.0f, 400.0f);
            CHECK(std::isnan(nanEnd.end) && nanEnd.startMul == 0.0f);
        });

        Test("snow: lifting the fog distance twice (the client didn't recompute it) doesn't lift it further", []
        {
            st::FogDistanceMemory memory;
            float end = 278.0f, start = -0.5f;
            st::LiftFogDistance(end, start, 0.5f, 400.0f, memory);
            const float onceEnd = end, onceStart = start;
            st::LiftFogDistance(end, start, 0.5f, 400.0f, memory);       // stale: what we wrote
            CHECK(end == onceEnd && start == onceStart);
            end = 278.0f; start = -0.5f;                                  // the client recomputed it
            st::LiftFogDistance(end, start, 0.5f, 400.0f, memory);
            CHECK(end == onceEnd && start == onceStart);
            end = 300.0f; start = -0.2f;                                  // a new value the client computed
            st::LiftFogDistance(end, start, 0.5f, 400.0f, memory);
            CHECK(Near(end, 350.0f, 0.01f) && Near(start, -0.1f, 1e-6f));
            end = 278.0f; start = -0.5f;
            st::LiftFogDistance(end, start, 0.0f, 400.0f, memory);       // no snow: nothing written, memory forgotten
            CHECK(!memory.valid && end == 278.0f && start == -0.5f);
        });

        // --- the cloud deck's tint (features/CloudTint): the Temple of Storms' lightning ---
        Test("cloud tint: the storm doodads by their path (the temple's lightning, Ulduar's clouds), nothing else", []
        {
            auto is = [](const char* p) { return ct::IsStormModel(p, std::strlen(p)); };
            CHECK(is("World\\Expansion02\\Doodads\\StormPeaks\\Lightning\\StormPeaksLightning01"));
            CHECK(is("world\\expansion02\\doodads\\stormpeaks\\lightning\\stormpeakslightning02.m2"));
            CHECK(is("WORLD/EXPANSION02/DOODADS/STORMPEAKS/FOG/STORMPEAKS_ULDUAR_CLOUDS_03.M2"));
            CHECK(is("World\\Expansion02\\Doodads\\StormPeaks\\Fog\\StormPeaks_Ulduar_Clouds"));
            CHECK(!is("World\\Expansion02\\Doodads\\StormPeaks\\Rocks\\StormPeaks_RockA"));
            CHECK(!is("World\\Generic\\PassiveDoodads\\ParticleEmitters\\BlastedLandsLightningBolt01")); // additive particles
            CHECK(!is("Environments\\Stars\\StormPeaks_SkyC_Ulduar"));                                 // the skybox: before the deck
            CHECK(!is("Spells\\CallLightning_Impact"));                                                // our own bolts (rain only)
            CHECK(!is("") && !ct::IsStormModel(nullptr, 10));
            // A stem longer than the client's inline field is read only that far.
            const std::string longStem = std::string(400, 'x') + "doodads\\stormpeaks\\lightning\\";
            CHECK(!ct::IsStormModel(longStem.c_str(), longStem.size()));
            CHECK(!ct::IsStormModel("doodads\\stormpeaks\\lightning\\abc", 28)); // cut before the folder ends
            CHECK(ct::IsStormModel("doodads\\stormpeaks\\lightning\\abc", 29));
        });

        // The report: Storm Peaks 36.8, 73.4 at 20:38, below the deck (1040..1950), under a clear sky; the Northrend
        // default light's fog at night is #002D41, the deck's colour then its night floor (0.30, 0.34, 0.45) or brighter.
        Test("cloud tint: the storm doodads take the deck's colour under any sky, from below, inside and above; not black", []
        {
            float stock[3], general[3], below[3], inside[3], above[3];
            ct::FromArgb(Argb(0xFF, 0x00, 0x2D, 0x41), stock);
            const float deck[3] = { 0.30f, 0.34f, 0.45f };
            // Before: the swap's colour under a clear sky was the stock night colour itself.
            ct::GeneralColour(stock, deck, 0.0f, general);
            CHECK(ct::Same(general, stock));
            ct::GeneralColour(stock, deck, 1.0f, general);
            CHECK(Near(general[0], deck[0], 1e-6f) && Near(general[2], deck[2], 1e-6f));
            // Now the storm doodads: the deck's, at its shade where the camera sees it.
            CHECK(ct::DeckShade(500.0f, 1040.0f, 1950.0f) == 0.85f);          // far below: its underside
            CHECK(ct::DeckShade(1040.0f, 1040.0f, 1950.0f) == 0.85f);
            CHECK(Near(ct::DeckShade(1495.0f, 1040.0f, 1950.0f), 0.975f, 1e-5f)); // half way in
            CHECK(Near(ct::DeckShade(2600.0f, 1040.0f, 1950.0f), 1.10f, 1e-6f)); // above: its top
            CHECK(ct::DeckShade(std::nanf(""), 1040.0f, 1950.0f) == 0.85f);
            CHECK(ct::DeckShade(1000.0f, 1000.0f, 1000.0f) == 0.85f);            // no thickness: no division by 0
            ct::StormColour(deck, ct::DeckShade(900.0f, 1040.0f, 1950.0f), below);
            ct::StormColour(deck, ct::DeckShade(1500.0f, 1040.0f, 1950.0f), inside);
            ct::StormColour(deck, ct::DeckShade(2300.0f, 1040.0f, 1950.0f), above);
            auto lum = [](const float c[3]) { return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]; };
            std::printf("     stock night fog L %.3f; storm doodads' colour below %.2f %.2f %.2f, inside %.2f %.2f %.2f, above %.2f %.2f %.2f\n",
                        lum(stock), below[0], below[1], below[2], inside[0], inside[1], inside[2], above[0], above[1], above[2]);
            CHECK(lum(below) > 2.0f * lum(stock) && lum(below) > 0.25f);
            CHECK(lum(below) < lum(inside) && lum(inside) < lum(above));
            CHECK(Near(below[2], 0.45f * 0.85f, 1e-5f));
            // A bright day deck is clamped, never above white.
            const float white[3] = { 0.98f, 1.0f, 0.99f };
            ct::StormColour(white, 1.1f, above);
            CHECK(above[0] == 1.0f && above[1] == 1.0f && above[2] == 1.0f);
            // The window: whenever the deck is drawn, with the tint on and the frame hooked; the camera's height isn't in it.
            CHECK(ct::WindowOpens(true, true, true));
            CHECK(!ct::WindowOpens(false, true, true) && !ct::WindowOpens(true, false, true) && !ct::WindowOpens(true, true, false));
        });

        // The client's way from band 8 to an M2 batch's c2: Sky.FogUpdate copies the band (0x7F171A) into the fog colour;
        // the batch's fog setup turns the model lighting's colour floats to bytes (x 255 + 0.5, truncated, 0x81FBAA) and
        // 0x873210 uploads them x 1/255 (0x3B808081).
        Test("cloud tint: the colour match holds with the storm and snow shading on (kFogColor is band 8 as shaded)", []
        {
            auto clientC2 = [](uint32_t argb, float out[3])
            {
                const int ch[3] = { R(argb), G(argb), B(argb) };
                for (int i = 0; i < 3; ++i)
                {
                    const float lighting = ch[i] * (1.0f / 255.0f);
                    const int back = static_cast<int>(lighting * 255.0f + 0.5f);
                    out[i] = back * (1.0f / 255.0f);
                }
            };
            const uint32_t nights[] = { Argb(0xFF, 0x00, 0x2D, 0x41), Argb(0xFF, 0x52, 0x54, 0x52), Argb(0xFF, 0x25, 0x1F, 0x32), Argb(0xFF, 0x37, 0x70, 0x83) };
            const st::Shade shades[] = { st::Shade{}, HeavyRain(0.0f), HeavyRain(1.0f), Snow(0.0f), Snow(1.0f), Snow(0.5f, 0.6f) };
            for (uint32_t band : nights)
                for (const st::Shade& s : shades)
                {
                    const uint32_t shaded = st::Identity(s) ? band : st::ShadeBand(band, st::kFogBand, s);
                    float stock[3], c2[3];
                    ct::FromArgb(shaded, stock); // what TintFogColour reads at kFogColor
                    clientC2(shaded, c2);        // what the batch uploads
                    CHECK(ct::MatchesStock(c2, stock));
                    CHECK(ct::Decide(ct::DrawIs{ false, ct::kFogScene }, c2, stock) == ct::Action::General);
                    CHECK(ct::Decide(ct::DrawIs{ false, ct::kFogUnknown }, c2, stock) == ct::Action::General);
                    CHECK(ct::Decide(ct::DrawIs{ true, ct::kFogScene }, c2, stock) == ct::Action::Storm);
                    // Were the two ever apart (a band shaded after the upload, say), the storm doodads still take theirs: by
                    // name and fog mode, not by colour.
                    float unshaded[3];
                    clientC2(band, unshaded);
                    if (!ct::MatchesStock(unshaded, stock))
                    {
                        CHECK(ct::Decide(ct::DrawIs{ true, ct::kFogScene }, unshaded, stock) == ct::Action::Storm);
                        CHECK(ct::Decide(ct::DrawIs{ false, ct::kFogUnknown }, unshaded, stock) == ct::Action::Leave);
                    }
                }
            // Heavy rain and snow do move band 8 (the case the match must follow).
            CHECK(st::ShadeBand(Argb(0xFF, 0x52, 0x54, 0x52), st::kFogBand, Snow(1.0f)) != Argb(0xFF, 0x52, 0x54, 0x52));
            CHECK(st::ShadeBand(Argb(0xFF, 0x52, 0x54, 0x52), st::kFogBand, HeavyRain(0.0f)) != Argb(0xFF, 0x52, 0x54, 0x52));
        });

        Test("cloud tint: a batch's own blend fog (black, white, grey, none) is kept; additive stays black even at a near-black night", []
        {
            float stock[3], black[3] = { 0, 0, 0 }, white[3] = { 1, 1, 1 }, grey[3];
            for (float& g : grey) g = 128.0f * (1.0f / 255.0f);
            ct::FromArgb(Argb(0xFF, 0x02, 0x03, 0x06), stock); // a night fog within the match of black
            CHECK(ct::MatchesStock(black, stock));
            for (int mode : { ct::kFogNone, ct::kFogBlack, ct::kFogWhite, ct::kFogGrey })
            {
                CHECK(ct::Decide(ct::DrawIs{ true, mode }, black, stock) == ct::Action::Leave);
                CHECK(ct::Decide(ct::DrawIs{ false, mode }, black, stock) == ct::Action::Leave);
            }
            // Without the fog mode (the guards differ): a storm batch's blend colours are told by their exact values.
            CHECK(ct::Decide(ct::DrawIs{ true, ct::kFogUnknown }, black, stock) == ct::Action::Leave);
            CHECK(ct::Decide(ct::DrawIs{ true, ct::kFogUnknown }, white, stock) == ct::Action::Leave);
            CHECK(ct::Decide(ct::DrawIs{ true, ct::kFogUnknown }, grey, stock) == ct::Action::Leave);
            CHECK(ct::Decide(ct::DrawIs{ true, ct::kFogUnknown }, stock, stock) == ct::Action::Storm);
            CHECK(ct::BlendModeColour(black) && ct::BlendModeColour(white) && ct::BlendModeColour(grey) && !ct::BlendModeColour(stock));
            // Not an M2 batch (particles, the weather): the colour match, as before.
            CHECK(ct::Decide(ct::DrawIs{}, stock, stock) == ct::Action::General);
            const float other[3] = { 0.5f, 0.2f, 0.1f };
            CHECK(ct::Decide(ct::DrawIs{}, other, stock) == ct::Action::Leave);
            CHECK(ct::Decide(ct::DrawIs{ true, ct::kFogScene }, other, stock) == ct::Action::Storm); // the storm's own: any colour
        });

        Test("cloud tint: c2 through a window: swapped per draw, kept while the client doesn't set it, its value back at the end", []
        {
            float stock[3];
            ct::FromArgb(Argb(0xFF, 0x00, 0x2D, 0x41), stock);
            const float general[3] = { 0.10f, 0.20f, 0.30f }, storm[3] = { 0.26f, 0.29f, 0.38f };
            ct::State s;
            float c2[3] = { stock[0], stock[1], stock[2] };
            bool dirty = false;
            // A lightning batch: the client set the stock colour; ours goes in.
            CHECK(ct::Step(s, c2, ct::DrawIs{ true, ct::kFogScene }, stock, general, storm, dirty) == ct::Action::Storm);
            CHECK(dirty && ct::Same(c2, storm) && s.holds && ct::Same(s.client, stock));
            // Its next draw (the client didn't set c2 again: the setter skips equal values): ours stays, nothing to upload.
            CHECK(ct::Step(s, c2, ct::DrawIs{ true, ct::kFogScene }, stock, general, storm, dirty) == ct::Action::Storm);
            CHECK(!dirty && ct::Same(c2, storm));
            // A draw outside any batch that didn't set c2 either: its value is the client's (the stock colour), so the
            // general colour, not ours mistaken for another fog colour.
            CHECK(ct::Step(s, c2, ct::DrawIs{}, stock, general, storm, dirty) == ct::Action::General);
            CHECK(dirty && ct::Same(c2, general) && ct::Same(s.client, stock));
            // An additive batch: the client set black; left.
            c2[0] = c2[1] = c2[2] = 0.0f;
            CHECK(ct::Step(s, c2, ct::DrawIs{ false, ct::kFogBlack }, stock, general, storm, dirty) == ct::Action::Leave);
            CHECK(!dirty && c2[0] == 0.0f && !s.holds);
            // A batch with a known mode other than the scene's while c2 still held ours: the client's value goes back.
            for (int i = 0; i < 3; ++i) c2[i] = stock[i];
            CHECK(ct::Step(s, c2, ct::DrawIs{ true, ct::kFogScene }, stock, general, storm, dirty) == ct::Action::Storm);
            CHECK(ct::Step(s, c2, ct::DrawIs{ false, ct::kFogWhite }, stock, general, storm, dirty) == ct::Action::Leave);
            CHECK(dirty && ct::Same(c2, stock) && !s.holds);
            // The window's end: the client's value back where ours still is.
            CHECK(ct::Step(s, c2, ct::DrawIs{ true, ct::kFogScene }, stock, general, storm, dirty) == ct::Action::Storm);
            CHECK(ct::End(s, c2) && ct::Same(c2, stock) && !s.holds);
            CHECK(!ct::End(s, c2)); // twice: nothing
            // The client set another value after our last write: not ours, left alone at the end.
            CHECK(ct::Step(s, c2, ct::DrawIs{ true, ct::kFogScene }, stock, general, storm, dirty) == ct::Action::Storm);
            c2[0] = 0.7f;
            CHECK(!ct::End(s, c2) && c2[0] == 0.7f);
            // A general colour equal to the client's (cover 0): held, nothing uploaded, nothing to give back.
            for (int i = 0; i < 3; ++i) c2[i] = stock[i];
            CHECK(ct::Step(s, c2, ct::DrawIs{}, stock, stock, storm, dirty) == ct::Action::General);
            CHECK(!dirty && !ct::End(s, c2) && ct::Same(c2, stock));
        });

        Test("bolt matrix: scaled, turned about the vertical, standing at the strike", []
        {
            const float pos[3] = { 100.0f, -200.0f, 50.0f };
            float m[16];
            st::BoltMatrix(pos, 0.0f, 6.0f, m);
            CHECK(m[0] == 6.0f && m[5] == 6.0f && m[10] == 6.0f && m[15] == 1.0f);
            CHECK(m[12] == 100.0f && m[13] == -200.0f && m[14] == 50.0f);
            st::BoltMatrix(pos, 1.5707963f, 2.0f, m);
            CHECK(Near(m[0], 0.0f, 1e-5f) && Near(m[1], 2.0f, 1e-5f) && Near(m[4], -2.0f, 1e-5f) && m[10] == 2.0f);
            // A point 11.5 yd up the model (row vector x matrix) ends up 11.5 x scale above the strike.
            const float up = 11.5f * m[10] + m[14];
            CHECK(Near(up, 50.0f + 23.0f, 1e-4f));
        });

        if (argc > 1)
        {
            const std::string name = std::string("guards against ") + argv[1];
            Test(name.c_str(), [&]
            {
                const Image im = LoadImage(argv[1]);
                CHECK(im.base == 0x00400000u && im.timestamp == 0x4C2452FEu);
                CheckGuards(im, st::kBandGuards);
                CheckGuards(im, st::kModelGuards);
                CheckGuards(im, st::kSoundGuards);
                CheckGuards(im, st::kSnowGuards);
                CheckGuards(im, ct::kGuards);
                // The cloud tint's: both M2 batch draws call the material setup, which stores the fog mode it switches on;
                // alpha blending (the lightning's) takes the scene's colour, the additive modes black; and the fog setter
                // pushes the colour it built (0xD43058) for pixel constant 2.
                {
                    auto callAt = [&](uintptr_t at)
                    {
                        const auto* c = static_cast<const uint8_t*>(im.At(at, 5));
                        CHECK(c && c[0] == 0xE8);
                        int32_t r = 0;
                        std::memcpy(&r, c + 1, 4);
                        return static_cast<uintptr_t>(at + 5 + r);
                    };
                    CHECK(callAt(0x0082040D) == ct::kMaterialSetup && callAt(0x00820B3E) == ct::kMaterialSetup);
                    const auto* modes = static_cast<const uint8_t*>(im.At(ct::kBlendFogModes, 32));
                    CHECK(modes);
                    for (int b = 0; b < 8; ++b)
                    {
                        uint32_t m = 0;
                        std::memcpy(&m, modes + 4 * b, 4);
                        CHECK(m == ct::kBlendFogModeTable[b]);
                    }
                    CHECK(ct::kBlendFogModeTable[2] == static_cast<uint32_t>(ct::kFogScene) && ct::kBlendFogModeTable[4] == static_cast<uint32_t>(ct::kFogBlack));
                    // The store is right after the table read in the setup: mov [esi+0x80], eax (esi: the context).
                    const auto* store = static_cast<const uint8_t*>(im.At(0x0081FB88, 6));
                    uint32_t disp = 0;
                    std::memcpy(&disp, store + 2, 4);
                    CHECK(disp == ct::kCtxFogMode);
                    const auto* setup = static_cast<const uint8_t*>(im.At(ct::kMaterialSetup, 10));
                    CHECK(setup && setup[7] == 0x56 && setup[8] == 0x8B && setup[9] == 0xF1); // push esi; mov esi, ecx
                    const auto* push = static_cast<const uint8_t*>(im.At(0x008732D9, 5));
                    uint32_t buffer = 0;
                    std::memcpy(&buffer, push + 1, 4);
                    CHECK(push[0] == 0x68 && buffer == 0x00D43058u);
                }
                // PlaySoundKit resets its own defaults with the same function (0x4C6ACD), and the listener getter we call
                // is the one the sound interface's own (0x4C5B4B) calls.
                auto callTarget = [&](uintptr_t at)
                {
                    const auto* c = static_cast<const uint8_t*>(im.At(at, 5));
                    CHECK(c && c[0] == 0xE8);
                    int32_t r = 0;
                    std::memcpy(&r, c + 1, 4);
                    return static_cast<uintptr_t>(at + 5 + r);
                };
                CHECK(callTarget(0x004C6ACD) == st::kResetSoundProps && callTarget(0x004C5B4B) == st::kListenerPosition);
                // The bands' address in the guarded instruction is the one shaded, and the copy is what the guard names.
                const auto* entry = static_cast<const uint8_t*>(im.At(st::kBandsToLight, 12));
                uint32_t ambient = 0;
                std::memcpy(&ambient, entry + 8, 4);
                CHECK(ambient == st::kBands + 4);
                // The guarded call at 0x7F333B lands on the band copy.
                const auto* call = static_cast<const uint8_t*>(im.At(0x007F333B, 5));
                int32_t rel = 0;
                std::memcpy(&rel, call + 1, 4);
                CHECK(static_cast<uintptr_t>(0x007F333B + 5 + rel) == st::kBandsToLight);
                // Snow's reads and writes are the addresses the guarded instructions use: the stored weather set share, the
                // camera's liquid (the colour resolve's call lands on the getter that returns it), the fog's end and start.
                auto u32At = [&](uintptr_t at) { uint32_t v = 0; const void* b = im.At(at, 4); CHECK(b); std::memcpy(&v, b, 4); return v; };
                CHECK(u32At(0x007F3995 + 2) == st::kStormBlend);
                std::memcpy(&rel, static_cast<const uint8_t*>(im.At(0x007F3245, 5)) + 1, 4);
                CHECK(static_cast<uintptr_t>(0x007F3245 + 5 + rel) == st::kCameraLiquid);
                CHECK(u32At(0x00780632 + 1) == st::kCameraInLiquid);
                CHECK(u32At(0x007F1705 + 2) == st::kFogEnd && st::kFogEnd == 0x00D38C1Cu);
                CHECK(u32At(0x007F1725 + 2) == st::kFogStartMul && st::kFogStartMul == 0x00D38C20u);
                // And the doodad spawn's scene global is the world scene.
                const auto* spawn = static_cast<const uint8_t*>(im.At(0x007BEEC8, 11));
                uint32_t scene = 0;
                std::memcpy(&scene, spawn + 2, 4);
                CHECK(scene == st::kWorldScene);
                std::memcpy(&rel, spawn + 7, 4);
                CHECK(static_cast<uintptr_t>(0x007BEEC8 + 11 + rel) == st::kCreateModel);
                std::printf("  %zu + %zu + %zu + %zu + %zu guards\n", sizeof(st::kBandGuards) / sizeof(st::kBandGuards[0]), sizeof(st::kModelGuards) / sizeof(st::kModelGuards[0]),
                            sizeof(st::kSoundGuards) / sizeof(st::kSoundGuards[0]), sizeof(st::kSnowGuards) / sizeof(st::kSnowGuards[0]),
                            sizeof(ct::kGuards) / sizeof(ct::kGuards[0]));
            });
        }
        else std::printf("SKIP guards (no Wow.exe given)\n");

        if (argc > 2)
        {
            const std::string name = std::string("thunder's kits and Lua rows against ") + argv[2];
            Test(name.c_str(), [&]
            {
                const std::vector<SoundRow> rows = LoadSoundEntries(argv[2]);
                auto byId = [&](int32_t id) -> const SoundRow&
                {
                    for (const SoundRow& r : rows) if (r.id == id) return r;
                    throw std::runtime_error("no SoundEntries row " + std::to_string(id));
                };
                auto lower = [](std::string v) { for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return v; };
                for (const st::KitInfo& k : st::kKits)
                {
                    const SoundRow& r = byId(k.soundKitId);
                    if (!(r.name == k.name && r.volume == k.volume && r.minDistance == k.minDistance && r.cutoff == k.cutoff && !r.files.empty()))
                        throw std::runtime_error(std::string("kit differs from its row: ") + k.name);
                    for (const std::string& f : r.files)
                        CHECK(lower(f).find("netherstorm") == std::string::npos && lower(f).find("saurfang") == std::string::npos);
                    std::printf("  kit %5d %-34s volume %.2f, min %3.0f, cutoff %3.0f, flags 0x%X, %zu take(s)\n", r.id, ("\"" + r.name + "\"").c_str(),
                                r.volume, r.minDistance, r.cutoff, r.flags, r.files.size());
                }
                for (const st::LuaRow& l : st::kLuaRows)
                {
                    // Lua PlaySound finds a row by its exact name: exactly one row has it, the one meant, at that volume,
                    // and it plays the kit's recordings.
                    unsigned named = 0;
                    for (const SoundRow& r : rows) named += r.name == l.name ? 1 : 0;
                    const SoundRow& r = byId(l.soundEntryId);
                    const SoundRow& kit = byId(st::Info(l.kit).soundKitId);
                    bool shared = false;
                    for (const std::string& f : r.files) for (const std::string& g : kit.files) shared = shared || lower(f) == lower(g);
                    if (!(named == 1 && r.name == l.name && r.volume == l.volume && shared))
                        throw std::runtime_error(std::string("Lua row isn't what it says: ") + l.name + " (" + std::to_string(named) + " rows by that name)");
                }
            });
        }
        else std::printf("SKIP SoundEntries checks (no SoundEntries.dbc given)\n");
    }
    catch (const std::exception& e)
    {
        std::printf("FAIL %s\n", e.what());
        return 1;
    }
    std::printf("%u passed\n", g_passed);
    return 0;
}
