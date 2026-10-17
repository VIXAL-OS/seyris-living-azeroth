// Offline tests for env/StormModel (storms: the rules env/Storm applies to the client): grade to phase,
// storminess, when flashes come, what a flash looks like over time, where strikes land, thunder's delay
// and loudness by distance, the day-night colour shading (and that it never builds up), the bolt's
// matrix, and -- given the client executable -- every guard against its bytes. No game.
//
// Usage:  storm_tests [<Wow.exe>]
#include "env/StormModel.hpp"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace st = wxl_livingazeroth::storm;

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

int main(int argc, char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try
    {
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

        Test("thunder: about 3 s per km after the flash; quieter entries farther away, silence beyond the distance", []
        {
            CHECK(st::ThunderDelay(0.0f) == 0.0f && st::ThunderDelay(-5.0f) == 0.0f);
            const float km = 1000.0f / st::kYard; // yards in a kilometre
            CHECK(Near(st::ThunderDelay(km), 2.915f, 0.01f));
            CHECK(Near(st::ThunderDelay(343.0f / st::kYard), 1.0f, 1e-4f));
            CHECK(Near(st::ThunderDelay(2500.0f), 6.66f, 0.02f));
            CHECK(std::strcmp(st::ThunderSound(100.0f, 1600.0f), "StormPeaksLightning") == 0);
            CHECK(std::strcmp(st::ThunderSound(400.0f, 1600.0f), "NetherstormLightningStrikes") == 0);
            CHECK(std::strcmp(st::ThunderSound(800.0f, 1600.0f), "TK_Lightning_Slow") == 0);
            CHECK(std::strcmp(st::ThunderSound(1500.0f, 1600.0f), "UL_lightning") == 0);
            CHECK(st::ThunderSound(1700.0f, 1600.0f) == nullptr);
            CHECK(st::ThunderSound(100.0f, 0.0f) == nullptr);
            CHECK(st::ThunderSound(std::nanf(""), 1600.0f) == nullptr);
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
                // And the doodad spawn's scene global is the world scene.
                const auto* spawn = static_cast<const uint8_t*>(im.At(0x007BEEC8, 11));
                uint32_t scene = 0;
                std::memcpy(&scene, spawn + 2, 4);
                CHECK(scene == st::kWorldScene);
                std::memcpy(&rel, spawn + 7, 4);
                CHECK(static_cast<uintptr_t>(0x007BEEC8 + 11 + rel) == st::kCreateModel);
                std::printf("  %zu + %zu guards\n", sizeof(st::kBandGuards) / sizeof(st::kBandGuards[0]), sizeof(st::kModelGuards) / sizeof(st::kModelGuards[0]));
            });
        }
        else std::printf("SKIP guards (no Wow.exe given)\n");
    }
    catch (const std::exception& e)
    {
        std::printf("FAIL %s\n", e.what());
        return 1;
    }
    std::printf("%u passed\n", g_passed);
    return 0;
}
