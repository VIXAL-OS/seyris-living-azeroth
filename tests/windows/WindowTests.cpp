// Offline tests for env/WindowModel (window lights and glow): the night fade against the client's own curve, the
// window index, the spot's direction, the glass's rectangle, the light budget in a grid cell, and -- given the
// client executable -- the guards and the key table against its bytes, and -- given the data folder -- the shipped
// WindowLights.cdbc (inside the budget, its lights night only). No game.
// Following the rooms behind the glass (env/GlassModel, as features/Windows uses it): a merged light's lit share, a
// room fading over 1.5 s going dark and lit again (the clock, the time override's jumps, the night factor), the
// toggles, lit frosted glass going dark and flickering (the hearth: in time with the glass), and the shipped
// WindowRooms.cdbc (against WindowLights, GlassBuildings, RoomStyles; its frosted cells).
//
// Usage:  windows_tests [<Wow.exe>] [<repo data folder>]
#include "env/GlassModel.hpp"
#include "env/WindowModel.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace wm = wxl_livingazeroth::windows;
namespace gl = wxl_livingazeroth::glass;

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

    bool Close(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

    // The curve as the four keys say, written out piece by piece (not through the interpolator).
    float Expected(float t)
    {
        const float a = 6.0f / 24.0f, b = 7.0f / 24.0f, c = 20.5f / 24.0f, d = 21.5f / 24.0f; // 06:00, 07:00, 20:30, 21:30
        if (t <= a) return 1.0f;
        if (t <= b) return 1.0f - (t - a) / (b - a);
        if (t <= c) return 0.0f;
        if (t <= d) return (t - c) / (d - c);
        return 1.0f;
    }

    std::vector<uint8_t> ReadFile(const std::string& path)
    {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) throw std::runtime_error("can't open " + path);
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        std::vector<uint8_t> bytes(size > 0 ? static_cast<size_t>(size) : 0);
        const size_t got = bytes.empty() ? 0 : std::fread(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
        CHECK(got == bytes.size());
        return bytes;
    }

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
        im.bytes = ReadFile(path);
        CHECK(im.bytes.size() > 0x400);
        auto u32 = [&](size_t at) { uint32_t v; CHECK(at + 4 <= im.bytes.size()); std::memcpy(&v, im.bytes.data() + at, 4); return v; };
        auto u16 = [&](size_t at) { return static_cast<uint32_t>(im.bytes.at(at) | (im.bytes.at(at + 1) << 8)); };
        const uint32_t pe = u32(0x3C);
        CHECK(u32(pe) == 0x00004550);
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

    template <size_t N> void CheckGuards(const Image& im, const wm::Guard (&guards)[N])
    {
        const wm::Guard* bad = wm::FirstMismatch(guards, [&](uintptr_t va, uint32_t size) { return im.At(va, size); });
        if (bad) throw std::runtime_error(std::string("differs: ") + bad->what);
        for (const wm::Guard& g : guards)
        {
            Image changed = im;
            auto* at = static_cast<uint8_t*>(const_cast<void*>(changed.At(g.address, g.size)));
            CHECK(at);
            at[g.size - 1] ^= 0x01;
            CHECK(wm::FirstMismatch(guards, [&](uintptr_t va, uint32_t size) { return changed.At(va, size); }) == &g);
        }
    }

    // A plain WDBC table: rows of raw uint32 columns and the string block.
    struct Table
    {
        uint32_t fields = 0;
        std::vector<std::vector<uint32_t>> rows;
        std::vector<char> strings;
        float F(size_t r, size_t c) const { float f; std::memcpy(&f, &rows[r][c], 4); return f; }
        std::string S(size_t r, size_t c) const { const uint32_t o = rows[r][c]; return o < strings.size() ? std::string(&strings[o]) : std::string(); }
    };
    Table ReadTable(const std::string& path)
    {
        const std::vector<uint8_t> b = ReadFile(path);
        CHECK(b.size() >= 20 && std::memcmp(b.data(), "WDBC", 4) == 0);
        uint32_t h[4];
        std::memcpy(h, b.data() + 4, 16);
        Table t;
        t.fields = h[1];
        CHECK(h[2] == h[1] * 4 && 20 + static_cast<size_t>(h[0]) * h[2] + h[3] == b.size());
        for (uint32_t r = 0; r < h[0]; ++r)
        {
            std::vector<uint32_t> row(h[1]);
            std::memcpy(row.data(), b.data() + 20 + static_cast<size_t>(r) * h[2], h[2]);
            t.rows.push_back(row);
        }
        const size_t at = 20 + static_cast<size_t>(h[0]) * h[2];
        t.strings.assign(b.begin() + static_cast<std::ptrdiff_t>(at), b.end());
        t.strings.push_back('\0');
        return t;
    }

    // Following the rooms: a frame-by-frame run of the rooms' clock (60 fps) toward `target(t)` (hour, night), and
    // each room's lit value (hash in `hashes`) under a style's dark share, as features/Windows works it out.
    struct Run
    {
        std::vector<std::vector<float>> lit;   // per frame, per room
        double dt = 1.0 / 60.0;
    };
    template <class Target> Run RunClock(const gl::RoomStyle& style, uint32_t flags, const gl::RoomToggles& t, const std::vector<float>& hashes,
                                         double seconds, Target target)
    {
        Run run;
        gl::RoomClock clock;
        for (double s = 0.0; s <= seconds + 1e-9; s += run.dt)
        {
            float hour, night;
            target(s, hour, night);
            clock.Advance(1000.0 + s, hour, night);
            const gl::DarkBand band = gl::BandOf(clock, style, flags, t);
            std::vector<float> row;
            for (float h : hashes) row.push_back(gl::RoomLit(h, band));
            run.lit.push_back(row);
        }
        return run;
    }
    // Per room: the biggest step between two frames, and how long it took from leaving its first value to reaching
    // its last (seconds; 0 if it never moved).
    void Steps(const Run& run, size_t room, float& biggest, double& took)
    {
        biggest = 0.0f;
        int first = -1, last = -1;
        const float a = run.lit.front()[room], b = run.lit.back()[room];
        for (size_t f = 1; f < run.lit.size(); ++f)
        {
            biggest = std::max(biggest, std::fabs(run.lit[f][room] - run.lit[f - 1][room]));
            if (first < 0 && run.lit[f][room] != a) first = static_cast<int>(f);
            if (run.lit[f][room] != b) last = static_cast<int>(f);
        }
        took = first < 0 ? 0.0 : (last + 1 - first + 1) * run.dt;
    }

    wm::Window At(uint32_t map, float x, float y, float z, uint32_t flags = 0)
    {
        wm::Window w;
        w.map = map; w.pos[0] = x; w.pos[1] = y; w.pos[2] = z; w.facing[0] = 1.0f; w.flags = flags;
        return w;
    }
}

int main(int argc, char** argv)
{
    try
    {
        Test("night fade: the client's keys (1 at 06:00, 0 07:00-20:30, 1 from 21:30)", []
        {
            const float dawn = wm::kNightKeys[0].time, day = wm::kNightKeys[1].time, dusk = wm::kNightKeys[2].time, night = wm::kNightKeys[3].time;
            CHECK(Close(dawn * 24.0f, 6.0f, 1e-5f) && Close(day * 24.0f, 7.0f, 1e-5f) && Close(dusk * 24.0f, 20.5f, 1e-5f) && Close(night * 24.0f, 21.5f, 1e-5f));
            CHECK(wm::NightFade(0.0f) == 1.0f);
            CHECK(wm::NightFade(dawn) == 1.0f);
            CHECK(Close(wm::NightFade(6.5f / 24.0f), 0.5f, 1e-4f));               // 06:30
            CHECK(wm::NightFade(day) == 0.0f);
            CHECK(wm::NightFade(0.5f) == 0.0f);                                   // noon
            CHECK(wm::NightFade(dusk) == 0.0f);
            CHECK(Close(wm::NightFade(21.0f / 24.0f), 0.5f, 1e-4f));              // 21:00
            CHECK(Close(wm::NightFade(night), 1.0f, 1e-6f));
            CHECK(wm::NightFade(1.0f) == 1.0f);
            CHECK(wm::NightFade(-0.5f) == 1.0f && wm::NightFade(1.5f) == 1.0f);   // clamped, as the client does
        });
        Test("night fade: the whole day against the keys written out", []
        {
            float worst = 0.0f;
            for (int i = 0; i <= 14400; ++i)
            {
                const float t = i / 14400.0f;
                worst = std::fmax(worst, std::fabs(wm::NightFade(t) - Expected(t)));
            }
            CHECK(worst < 1e-4f);
            // Dawn only falls, dusk only rises.
            for (float t = 0.25f; t < 0.2917f; t += 0.001f) CHECK(wm::NightFade(t + 0.001f) <= wm::NightFade(t) + 1e-6f);
            for (float t = 0.8541f; t < 0.8958f; t += 0.001f) CHECK(wm::NightFade(t + 0.001f) >= wm::NightFade(t) - 1e-6f);
        });
        Test("window index: by map, in range, nearest first, glow-only rows on request, at most max", []
        {
            std::vector<wm::Window> rows = { At(0, 10, 0, 0), At(0, 100, 0, 0), At(0, 5, 0, 0), At(1, 1, 0, 0), At(0, 3, 0, 0, wm::kGlowOnly),
                                             At(0, -70, 20, 0), At(0, 0, 0, 300) };
            for (uint32_t i = 0; i < rows.size(); ++i) rows[i].id = i + 1;
            wm::Index index;
            index.Build(rows);
            std::vector<const wm::Window*> out;
            const float c[3] = { 0, 0, 0 };
            index.Near(0, c, 80.0f, false, 10, out);
            CHECK(out.size() == 3 && out[0]->id == 3 && out[1]->id == 1 && out[2]->id == 6); // (300 up and 100 away: out of range)
            index.Near(0, c, 80.0f, true, 10, out);
            CHECK(out.size() == 4 && out[0]->id == 5);
            index.Near(0, c, 80.0f, true, 2, out);
            CHECK(out.size() == 2 && out[0]->id == 5 && out[1]->id == 3);
            index.Near(1, c, 80.0f, true, 10, out);
            CHECK(out.size() == 1 && out[0]->id == 4);
            index.Near(530, c, 80.0f, true, 10, out);
            CHECK(out.empty());
            // Across tile borders (64 yd) and negative coordinates.
            const float away[3] = { -64.0f, 10.0f, 0.0f };
            index.Near(0, away, 15.0f, false, 10, out);
            CHECK(out.size() == 1 && out[0]->id == 6);
        });
        Test("the light: a spot out of the window, ~24 degrees down", []
        {
            const float east[3] = { 1, 0, 0 };
            float d[3];
            wm::SpotDirection(east, d);
            CHECK(Close(d[0] * d[0] + d[1] * d[1] + d[2] * d[2], 1.0f, 1e-5f));
            CHECK(d[0] > 0.9f && d[1] == 0.0f && d[2] < 0.0f);
            const float down = std::atan2(-d[2], d[0]) * 57.29578f;
            CHECK(down > 22.0f && down < 26.0f);
            const float diag[3] = { 0.7071068f, -0.7071068f, 0 };
            wm::SpotDirection(diag, d);
            CHECK(Close(d[0], -d[1], 1e-6f) && d[2] < 0.0f);
        });
        Test("the glass: a rectangle on the facade, back behind the light, upright", []
        {
            wm::Window w;
            w.pos[0] = 10; w.pos[1] = 20; w.pos[2] = 30;
            w.facing[0] = 0; w.facing[1] = 1; w.facing[2] = 0; // facing north
            w.back = 0.75f; w.halfWidth = 2.0f; w.halfHeight = 1.0f;
            float c[4][3];
            wm::GlassCorners(w, 0.0f, c);
            for (int k = 0; k < 4; ++k) CHECK(Close(c[k][1], 20.0f - 0.75f, 1e-5f)); // all on the glass's plane
            CHECK(Close(std::fabs(c[1][0] - c[0][0]), 4.0f, 1e-5f) && Close(c[0][2], c[1][2], 1e-6f)); // along: level
            CHECK(Close(c[2][2] - c[0][2], 2.0f, 1e-5f) && Close(c[2][0], c[0][0], 1e-6f));           // up: straight up
            CHECK(Close((c[0][0] + c[3][0]) * 0.5f, 10.0f, 1e-5f) && Close((c[0][2] + c[3][2]) * 0.5f, 30.0f, 1e-5f));
            wm::GlassCorners(w, 0.25f, c);
            CHECK(Close(std::fabs(c[1][0] - c[0][0]), 4.5f, 1e-5f) && Close(c[2][2] - c[0][2], 2.5f, 1e-5f));
        });
        Test("the light budget: a full cell keeps torches; windows at most their cap", []
        {
            // Sorted strongest first, as env/Lights sorts them (a window already scored down by kWindowRank).
            const wm::CellEntry cell[] = { { 5.0f, 1, false }, { 4.0f, 2, true }, { 3.0f, 3, true }, { 2.5f, 4, true }, { 1.0f, 5, false } };
            uint16_t out[8];
            unsigned left = 0;
            CHECK(wm::PickCell(cell, 5, 4, 2, out, left) == 4);
            CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3 && out[3] == 5 && left == 1);
            left = 0;
            CHECK(wm::PickCell(cell, 5, 16, 0, out, left) == 2 && out[0] == 1 && out[1] == 5 && left == 3);
            left = 0;
            CHECK(wm::PickCell(cell, 5, 2, 4, out, left) == 2 && out[0] == 1 && out[1] == 2 && left == 0);
            CHECK(wm::PickCell(cell, 0, 4, 4, out, left) == 0);
            // A window as bright as a torch ranks below it.
            static_assert(wm::kWindowRank > 0.0f && wm::kWindowRank < 1.0f, "a window ranks below a torch as bright");
        });

        Test("following the rooms: a merged light's lit share is its glass's share with a lit room", []
        {
            // Four rooms' cells (0.3, 0.3, 0.2, 0.1 of the glass) and 0.1 of stained glass (no room: always lit).
            const float hash[4] = { 0.2f, 0.45f, 0.7f, 0.95f }, share[4] = { 0.3f, 0.3f, 0.2f, 0.1f };
            auto at = [&](float threshold) {
                const gl::DarkBand b = gl::Band(threshold, threshold);
                gl::LightCell c[4];
                for (int i = 0; i < 4; ++i) { c[i].hash = hash[i]; c[i].share = share[i]; c[i].band = b; }
                return gl::LitShare(c, 4);
            };
            CHECK(Close(at(0.0f), 1.0f, 1e-6f));                 // evening: every room lit
            CHECK(Close(at(0.3f), 0.7f, 1e-6f));                 // the first room dark: 0.3 of the glass gone
            CHECK(Close(at(0.5f), 0.4f, 1e-6f));                 // two dark
            CHECK(Close(at(0.8f), 0.2f, 1e-6f));                 // three dark: the last room and the stained glass
            CHECK(Close(at(1.0f), 0.1f, 1e-6f));                 // all dark: only the stained glass still lights the street
            // ... and only room glass: dark is dark; no rooms at all (stained, frosted, no glass entry): always full.
            gl::LightCell only[2];
            only[0].hash = 0.3f; only[0].share = 0.6f; only[1].hash = 0.6f; only[1].share = 0.4f;
            for (auto& c : only) c.band = gl::Band(1.0f, 1.0f);
            CHECK(gl::LitShare(only, 2) == 0.0f && gl::LitShare(nullptr, 0) == 1.0f);
            // Half way through a fade the fading room counts half.
            gl::LightCell fading[4];
            const gl::DarkBand fade = gl::Band(0.5f, 0.4f);   // the threshold went 0.4 -> 0.5 in the last 1.5 s
            for (int i = 0; i < 4; ++i) { fading[i].hash = hash[i]; fading[i].share = share[i]; fading[i].band = fade; }
            CHECK(Close(gl::LitShare(fading, 4), 0.1f + 0.3f * 0.5f + 0.2f + 0.1f, 1e-5f));
            // A row of windows (24 rooms' cells) dims step by step as the threshold rises, never up.
            std::vector<gl::LightCell> row(24);
            float last = 2.0f;
            int steps = 0;
            for (float t = 0.0f; t <= 1.0f; t += 0.01f)
            {
                const gl::DarkBand b = gl::Band(t, t);
                for (size_t i = 0; i < row.size(); ++i)
                {
                    row[i].hash = gl::LitHash(static_cast<float>(i % 6), static_cast<float>(i / 6), 311.0f, 17.0f);
                    row[i].share = 1.0f / row.size(); row[i].band = b;
                }
                const float s = gl::LitShare(row.data(), row.size());
                CHECK(s <= last + 1e-6f);
                steps += s < last - 1e-6f ? 1 : 0;
                last = s;
            }
            CHECK(steps >= 12 && last < 0.05f);
            std::printf("  a row of 24 rooms dims in %d steps from lit to dark\n", steps);
        });

        Test("following the rooms: a room going dark at night fades over 1.5 s, and lights again at dawn the same way", []
        {
            const gl::RoomStyle house;
            const gl::RoomToggles on;
            // The clock running (1 game second a real second) from 23:00 and from 05:00 (early risers: the share falls).
            const float h23 = gl::LightsOutShare(23.0f + 20.0f / 3600.0f, 0, 1), h05 = gl::LightsOutShare(5.0f + 20.0f / 3600.0f, 0, 1);
            const Run night = RunClock(house, 0, on, { h23 }, 40.0, [](double s, float& hour, float& n) { hour = 23.0f + static_cast<float>(s / 3600.0); n = 1; });
            const Run dawn = RunClock(house, 0, on, { h05 }, 40.0, [](double s, float& hour, float& n) { hour = 5.0f + static_cast<float>(s / 3600.0); n = 1; });
            float bigN, bigD;
            double tookN, tookD;
            Steps(night, 0, bigN, tookN);
            Steps(dawn, 0, bigD, tookD);
            std::printf("  23:00: lit %.0f -> %.0f over %.2f s (biggest step a frame %.3f); 05:00: %.0f -> %.0f over %.2f s (%.3f)\n",
                        night.lit.front()[0], night.lit.back()[0], tookN, bigN, dawn.lit.front()[0], dawn.lit.back()[0], tookD, bigD);
            // (a frame's step is 1/90 on average; the float hour's own 7 ms steps make it uneven: at most 1/40)
            CHECK(night.lit.front()[0] == 1.0f && night.lit.back()[0] == 0.0f && tookN > 1.4 && tookN < 1.6 && bigN < 0.025f);
            CHECK(dawn.lit.front()[0] == 0.0f && dawn.lit.back()[0] == 1.0f && tookD > 1.4 && tookD < 1.6 && bigD < 0.025f);
        });

        Test("following the rooms: the time override's jumps cross-fade (a dragged slider waits for it to stop), each room over 1.5 s", []
        {
            const gl::RoomStyle house;
            const gl::RoomToggles on;
            std::vector<float> hashes;
            for (int k = 0; k < 289; ++k) hashes.push_back((k + 0.5f) / 289.0f);
            // The panel's time set at 1 s: 20:00 -> 02:00, back again, noon -> 23:00 (the night factor 0 -> 1 with it),
            // 23:59 -> 00:01 (across midnight); its slider dragged from 20:00 to 02:00 over a second (from 1 s to 2 s).
            struct Jump { const char* name; float from, to, nightFrom, nightTo; bool drag; };
            const Jump jumps[] = { { "20:00 -> 02:00", 20.0f, 2.0f, 0.0f, 1.0f, false }, { "02:00 -> 20:00", 2.0f, 20.0f, 1.0f, 0.0f, false },
                                   { "12:00 -> 23:00", 12.0f, 23.0f, 0.0f, 1.0f, false }, { "23:59 -> 00:01", 23.983f, 0.0167f, 1.0f, 1.0f, false },
                                   { "dragged 20:00 -> 02:00", 20.0f, 26.0f, 0.0f, 1.0f, true } };
            for (const Jump& j : jumps)
            {
                const Run run = RunClock(house, 0, on, hashes, 8.0, [&](double s, float& hour, float& n) {
                    const double f = j.drag ? std::min(1.0, std::max(0.0, s - 1.0)) : (s < 1.0 ? 0.0 : 1.0);
                    hour = static_cast<float>(std::fmod(j.from + (j.to - j.from) * f, 24.0));
                    n = static_cast<float>(j.nightFrom + (j.nightTo - j.nightFrom) * f);
                });
                float worst = 0.0f;
                double longest = 0.0, shortest = 1e9;
                int moved = 0, wrong = 0;
                const float want = gl::DarkShare(house, 0, on, j.nightTo, std::fmod(j.to, 24.0f));
                for (size_t r = 0; r < hashes.size(); ++r)
                {
                    float big;
                    double took;
                    Steps(run, r, big, took);
                    worst = std::max(worst, big);
                    if (took > 0.0) { ++moved; longest = std::max(longest, took); shortest = std::min(shortest, took); }
                    wrong += run.lit.back()[r] != (hashes[r] > want ? 1.0f : 0.0f) ? 1 : 0;
                }
                std::printf("  %s: %d of %zu rooms changed, each over %.2f .. %.2f s, the biggest step a frame %.3f; %d not as the new time has them\n",
                            j.name, moved, hashes.size(), moved ? shortest : 0.0, longest, worst, wrong);
                // each room that changes fades over 1.5 s (all together after a jump; a dragged slider holds them until it
                // stops, at 2 s, then the same)
                CHECK(wrong == 0 && moved > 0 && worst < 0.015f && shortest >= 1.45 && longest <= 1.6);
            }
        });

        Test("following the rooms: RoomVariety or LightsOut off, the lights stay at full at night", []
        {
            gl::RoomStyle house;
            gl::RoomToggles on, noVariety, noLightsOut, none;
            noVariety.variety = false; noLightsOut.lightsOut = false; none.lightsOutStrength = 0.0f;
            for (float hour : { 22.0f, 0.0f, 2.0f, 4.5f })
            {
                CHECK(gl::DarkShare(house, 0, on, 1.0f, hour) > 0.2f);
                CHECK(gl::DarkShare(house, 0, noVariety, 1.0f, hour) == 0.0f);
                CHECK(gl::DarkShare(house, 0, noLightsOut, 1.0f, hour) == 0.0f);
                CHECK(gl::DarkShare(house, 0, none, 1.0f, hour) == 0.0f);
                // every room lit: a light's share 1 whatever its cells' hashes
                const gl::DarkBand b = gl::Band(gl::DarkShare(house, 0, noVariety, 1.0f, hour), gl::DarkShare(house, 0, noVariety, 1.0f, hour));
                gl::LightCell c[3];
                for (int i = 0; i < 3; ++i) { c[i].hash = (i * 100 + 0.5f) / 289.0f; c[i].share = 0.3f; c[i].band = b; }
                CHECK(gl::LitShare(c, 3) == 1.0f);
            }
            // By day, the style's few (with variety); an inn late, a shop early; a dark place or an empty house all dark.
            gl::RoomStyle inn = house; inn.lateHours = 2.5f; inn.nightCap = 0.55f;
            gl::RoomStyle shop = house; shop.lateHours = -1.0f;
            CHECK(Close(gl::DarkShare(house, 0, on, 0.0f, 13.0f), house.darkShare, 1e-6f) && gl::DarkShare(house, 0, noVariety, 0.0f, 13.0f) == 0.0f);
            CHECK(gl::DarkShare(inn, 0, on, 1.0f, 0.0f) < gl::DarkShare(house, 0, on, 1.0f, 0.0f) &&
                  gl::DarkShare(shop, 0, on, 1.0f, 22.0f) > gl::DarkShare(house, 0, on, 1.0f, 22.0f) && gl::DarkShare(inn, 0, on, 1.0f, 3.0f) <= 0.55f);
            CHECK(gl::DarkShare(house, gl::kBuildingDarkPlace, on, 1.0f, 21.0f) == 1.0f && gl::DarkShare(house, gl::kBuildingEmpty, on, 0.0f, 12.0f) == 1.0f &&
                  gl::DarkShare(house, gl::kBuildingEmpty, noVariety, 1.0f, 23.0f) == 0.0f);
        });

        Test("following frosted glass: dark by its building's hours, a lit window's light flickering with its hearth", []
        {
            // A light over two windows of frosted glass and a sliver of stained: one cell dark, one lit and flickering.
            const gl::DarkBand band = gl::Band(0.5f, 0.5f);
            const float A = 0.08f;
            float lo = 1, hi = 0, step = 0, last = -1;
            double sum = 0;
            int n = 0;
            for (int f = 0; f < 60 * 60; ++f)   // a minute at 60 frames a second
            {
                const float clock = 300.0f + f / 60.0f;
                gl::LightCell c[2];
                c[0].hash = 200.5f / 289.0f; c[0].share = 0.55f; c[0].band = band; c[0].dip = gl::HearthDip(c[0].hash, clock, A);
                c[1].hash = 30.5f / 289.0f; c[1].share = 0.4f; c[1].band = band; c[1].dip = gl::HearthDip(c[1].hash, clock, A);
                const float s = gl::LitShare(c, 2);
                lo = std::min(lo, s); hi = std::max(hi, s); sum += s; ++n;
                if (last >= 0) step = std::max(step, std::fabs(s - last));
                last = s;
            }
            // lit 0.05 (the stained sliver) + 0.55 x (1 - dip): 0.05 + 0.55 x 0.92 .. 0.6
            std::printf("  a frosted light: %.4f .. %.4f (mean %.4f), a frame's step at most %.4f\n", lo, hi, sum / n, step);
            CHECK(lo >= 0.05f + 0.55f * (1 - A) - 1e-5f && hi <= 0.6f + 1e-6f && hi - lo > 0.6f * 0.55f * A && step < 0.07f * A);
            CHECK(std::fabs(sum / n - (0.05 + 0.55 * (1 - A / 2))) < 0.05 * 0.55 * A);
            // No flicker (amplitude 0): exactly the lights out's share.
            gl::LightCell still[2];
            still[0].hash = 200.5f / 289.0f; still[0].share = 0.55f; still[0].band = band; still[0].dip = gl::HearthDip(still[0].hash, 12.0f, 0.0f);
            still[1] = still[0]; still[1].hash = 30.5f / 289.0f; still[1].share = 0.4f;
            CHECK(Close(gl::LitShare(still, 2), 0.05f + 0.55f, 1e-6f) && still[0].dip == 0.0f);
        });

        if (argc > 1 && argv[1][0])
        {
            const std::string name = std::string("guards and the night keys against ") + argv[1];
            Test(name.c_str(), [&]
            {
                const Image im = LoadImage(argv[1]);
                CHECK(im.base == 0x00400000u && im.timestamp == 0x4C2452FEu);
                CheckGuards(im, wm::kNightGuards);
                CheckGuards(im, wm::kGlowGuards);
                // The keys the update hands the interpolator are ours.
                const auto* keys = static_cast<const float*>(im.At(wm::kNightKeyTable, 32));
                CHECK(keys);
                for (int k = 0; k < 4; ++k) CHECK(keys[2 * k] == wm::kNightKeys[k].time && keys[2 * k + 1] == wm::kNightKeys[k].value);
                // The guarded update: the key table, a call to the interpolator, the store into the block's +0x1DC.
                const auto* up = static_cast<const uint8_t*>(im.At(0x007F38DB, 16));
                uint32_t table = 0, store = 0;
                int32_t rel = 0;
                std::memcpy(&table, up + 1, 4);
                std::memcpy(&rel, up + 6, 4);
                std::memcpy(&store, up + 12, 4);
                CHECK(table == wm::kNightKeyTable);
                CHECK(static_cast<uintptr_t>(0x007F38E0 + 5 + rel) == 0x007ED3B0u);
                CHECK(store == wm::kDayNight + wm::kWindowNight);
                // DayNightGetInfo returns the block; the material update reads the factor at +0x1DC.
                uint32_t block = 0, offset = 0;
                std::memcpy(&block, static_cast<const uint8_t*>(im.At(0x007ECEF0, 6)) + 1, 4);
                std::memcpy(&offset, static_cast<const uint8_t*>(im.At(0x007A855C, 9)) + 5, 4);
                CHECK(block == wm::kDayNight && offset == wm::kWindowNight);
                // The glow object's global, and the CVar's int at +0x30 behind its +0x04.
                uint32_t global = 0;
                std::memcpy(&global, static_cast<const uint8_t*>(im.At(0x004FAEBB, 5)) + 1, 4);
                CHECK(global == wm::kGlowEffect);
                const auto* test = static_cast<const uint8_t*>(im.At(0x008BFD34, 7));
                CHECK(test[2] == wm::kGlowCVar && test[5] == wm::kCVarInt);
                std::printf("  %zu + %zu guards\n", sizeof(wm::kNightGuards) / sizeof(wm::kNightGuards[0]), sizeof(wm::kGlowGuards) / sizeof(wm::kGlowGuards[0]));
            });
        }
        else std::printf("SKIP guards (no Wow.exe given)\n");

        if (argc > 2)
        {
            const std::string dir = argv[2];
            Test("the shipped WindowLights.cdbc: well formed, on the continents, inside the light budget, night only", [&]
            {
                const Table w = ReadTable(dir + "\\WindowLights.cdbc");
                const Table p = ReadTable(dir + "\\DoodadLightProperties.cdbc");
                CHECK(w.fields == 17 && w.rows.size() > 500);
                std::map<uint32_t, uint32_t> flagsOf;
                for (const auto& r : p.rows) flagsOf[r[0]] = r[12];
                std::map<std::tuple<uint32_t, int, int>, int> cells;
                uint32_t lastId = 0, casting = 0;
                for (size_t i = 0; i < w.rows.size(); ++i)
                {
                    const auto& r = w.rows[i];
                    CHECK(r[0] > lastId);   // sorted by ID (the cdbc reader expects it)
                    lastId = r[0];
                    CHECK(r[1] == 0 || r[1] == 1 || r[1] == 530 || r[1] == 571);
                    const float fx = w.F(i, 6), fy = w.F(i, 7), fz = w.F(i, 8);
                    CHECK(Close(fx * fx + fy * fy + fz * fz, 1.0f, 1e-3f) && std::fabs(fz) <= 0.7072f);
                    CHECK(w.F(i, 10) > 0.0f && w.F(i, 10) <= 6.0f && w.F(i, 11) > 0.0f && w.F(i, 11) <= 6.75f);
                    const float radius = w.F(i, 12);
                    CHECK(radius >= 6.0f && radius <= 10.0f);
                    CHECK(flagsOf.count(r[13]) && (flagsOf[r[13]] & 0x2)); // its light is night only
                    if (r[15] & wm::kGlowOnly) continue;
                    ++casting;
                    // Every 16-yd cell of the lights' grid this light reaches: at most 5 window lights.
                    const float x = w.F(i, 3), y = w.F(i, 4);
                    for (int cx = static_cast<int>(std::floor((x - radius) / 16)); cx <= static_cast<int>(std::floor((x + radius) / 16)); ++cx)
                        for (int cy = static_cast<int>(std::floor((y - radius) / 16)); cy <= static_cast<int>(std::floor((y + radius) / 16)); ++cy)
                        {
                            const float nx = std::fmax(cx * 16.0f, std::fmin(x, (cx + 1) * 16.0f)) - x;
                            const float ny = std::fmax(cy * 16.0f, std::fmin(y, (cy + 1) * 16.0f)) - y;
                            if (nx * nx + ny * ny <= radius * radius) ++cells[{ r[1], cx, cy }];
                        }
                }
                int busiest = 0;
                for (const auto& c : cells) busiest = std::max(busiest, c.second);
                CHECK(busiest <= 5 && casting > 0);
                std::printf("  %zu windows, %u cast light, busiest 16-yd cell %d window lights\n", w.rows.size(), casting, busiest);
            });

            Test("the shipped WindowRooms.cdbc: each light's rooms in GlassBuildings, their shares, a night's lights out", [&]
            {
                const Table r = ReadTable(dir + "\\WindowRooms.cdbc");
                const Table w = ReadTable(dir + "\\WindowLights.cdbc");
                const Table b = ReadTable(dir + "\\GlassBuildings.cdbc");
                const Table st = ReadTable(dir + "\\RoomStyles.cdbc");
                CHECK(r.fields == 8 && r.rows.size() > 1000 && b.fields == 13 && st.fields == 25);
                std::map<uint32_t, size_t> window, building, style;
                for (size_t i = 0; i < w.rows.size(); ++i) window[w.rows[i][0]] = i;
                for (size_t i = 0; i < b.rows.size(); ++i) building[b.rows[i][0]] = i;
                for (size_t i = 0; i < st.rows.size(); ++i) style[st.rows[i][0]] = i;
                // per window, its rows as read with stained glass's rooms (StainedRooms: not those flagged 0x4) and without
                // (StainedRooms off: not those flagged 0x2 or 0x8, the cells as they were before)
                std::map<uint32_t, std::vector<size_t>> cells, cellsWithout;
                std::map<uint32_t, bool> frosted;                // per window: it has lit frosted glass
                uint32_t lastId = 0;
                unsigned noBuilding = 0, frostCells = 0, stainCells = 0;
                for (size_t i = 0; i < r.rows.size(); ++i)
                {
                    const auto& row = r.rows[i];
                    CHECK(row[0] > lastId);
                    lastId = row[0];
                    CHECK(window.count(row[1]));
                    const float share = r.F(i, 5);
                    CHECK(share > 0.0f && share <= 1.0f);
                    // Flags: 0x1 lit frosted glass, 0x2 stained glass (with its rooms only), 0x4 only without them, 0x8 only with
                    CHECK(row[7] <= 0xFu && !((row[7] & 2u) && (row[7] & 5u)) && !((row[7] & 4u) && (row[7] & 8u)));
                    if ((row[7] & 1u) && !(row[7] & 4u)) { ++frostCells; frosted[row[1]] = true; }
                    stainCells += (row[7] & 2u) ? 1u : 0u;
                    const int32_t cx = static_cast<int32_t>(row[3]), cy = static_cast<int32_t>(row[4]);
                    CHECK(cx > -200000 && cx < 200000 && cy > -200000 && cy < 200000);   // a cell about its anchor, its offset in
                    if (!row[2]) { ++noBuilding; continue; }
                    CHECK(building.count(row[2]));
                    const size_t bi = building[row[2]], wi = window[row[1]];
                    // the same map, the building's origin near the light, the seed its WMO's path (the shader's fallback)
                    CHECK(b.rows[bi][1] == w.rows[wi][1] && b.rows[bi][2] != 0);
                    const float dx = b.F(bi, 3) - w.F(wi, 3), dy = b.F(bi, 4) - w.F(wi, 4);
                    CHECK(dx * dx + dy * dy < 1200.0f * 1200.0f);
                    CHECK(row[6] == gl::PathHash(b.S(bi, 11).c_str()));
                    if (!(row[7] & 4u)) cells[row[1]].push_back(i);
                    if (!(row[7] & 0xAu)) cellsWithout[row[1]].push_back(i);
                }
                size_t most = 0;
                for (const auto* view : { &cells, &cellsWithout })
                    for (const auto& c : *view)
                    {
                        double sum = 0;
                        for (size_t i : c.second) sum += r.F(i, 5);
                        CHECK(sum <= 1.0001);
                        most = std::max(most, c.second.size());
                    }
                CHECK(stainCells > 1000);
                CHECK(most <= 32);
                // A night: the lights' mean lit share by the hour, each cell by its building's style and seed (at rest).
                auto styleOf = [&](size_t bi) {
                    gl::RoomStyle s;
                    auto it = style.find(b.rows[bi][7]);
                    if (it == style.end()) return s;
                    s.darkShare = st.F(it->second, 17); s.lateHours = st.F(it->second, 22); s.nightCap = st.F(it->second, 23);
                    return s;
                };
                const float hours[] = { 21.0f, 22.0f, 23.0f, 0.0f, 1.0f, 2.0f, 3.0f, 4.5f, 6.0f };
                float mean[9] = {}, place[2][9] = {}, frostPlace[3][9] = {};
                unsigned placeLights[2] = {}, frostLights[3] = {};
                const char* const frostTowns[3] = { "Kharanos:", "Thelsamar:", "Orgrimmar:" };
                const gl::RoomToggles on;
                for (int k = 0; k < 9; ++k)
                {
                    double sum = 0, sumPlace[2] = {}, sumFrost[3] = {};
                    for (const auto& c : cells)
                    {
                        std::vector<gl::LightCell> lc;
                        for (size_t i : c.second)
                        {
                            const size_t bi = building[r.rows[i][2]];
                            const uint32_t seed = b.rows[bi][8];
                            gl::LightCell x;
                            x.hash = gl::LitHash(static_cast<float>(static_cast<int32_t>(r.rows[i][3])), static_cast<float>(static_cast<int32_t>(r.rows[i][4])),
                                                 static_cast<float>(seed & 1023u), static_cast<float>((seed >> 10) & 1023u));
                            x.share = r.F(i, 5);
                            const float dark = gl::DarkShare(styleOf(bi), b.rows[bi][9], on, 1.0f, hours[k]);
                            x.band = gl::Band(dark, dark);
                            lc.push_back(x);
                        }
                        const float share = gl::LitShare(lc.data(), lc.size());
                        sum += share;
                        const std::string comment = w.S(window[c.first], 16);
                        for (int p = 0; p < 2; ++p)
                            if (comment.rfind(p ? "Goldshire:" : "Stormwind City:", 0) == 0) { sumPlace[p] += share; if (k == 0) ++placeLights[p]; }
                        for (int p = 0; p < 3; ++p)
                            if (frosted.count(c.first) && comment.rfind(frostTowns[p], 0) == 0) { sumFrost[p] += share; if (k == 0) ++frostLights[p]; }
                    }
                    mean[k] = static_cast<float>(sum / cells.size());
                    for (int p = 0; p < 2; ++p) place[p][k] = placeLights[p] ? static_cast<float>(sumPlace[p] / placeLights[p]) : 0.0f;
                    for (int p = 0; p < 3; ++p) frostPlace[p][k] = frostLights[p] ? static_cast<float>(sumFrost[p] / frostLights[p]) : 0.0f;
                }
                std::printf("  %zu rooms (windows) behind %zu lights (%u without a building; %u stained glass), at most %zu a light\n", r.rows.size(), cells.size(),
                            noBuilding, stainCells, most);
                std::printf("  the lights with rooms, lit on average:");
                for (int k = 0; k < 9; ++k) std::printf(" %02d:%02d %.0f%%", static_cast<int>(hours[k]), static_cast<int>((hours[k] - std::floor(hours[k])) * 60), 100 * mean[k]);
                std::printf("\n  Stormwind (%u lights) at 23:00 %.0f%%, 02:00 %.0f%%; Goldshire (%u) at 23:00 %.0f%%, 02:00 %.0f%%\n",
                            placeLights[0], 100 * place[0][2], 100 * place[0][5], placeLights[1], 100 * place[1][2], 100 * place[1][5]);
                std::printf("  lit frosted glass: %u windows at %zu lights; Kharanos (%u lights) at 23:00 %.0f%%, 02:00 %.0f%%; Thelsamar (%u) %.0f%%, %.0f%%; "
                            "Orgrimmar (%u) %.0f%%, %.0f%%\n", frostCells, frosted.size(), frostLights[0], 100 * frostPlace[0][2], 100 * frostPlace[0][5],
                            frostLights[1], 100 * frostPlace[1][2], 100 * frostPlace[1][5], frostLights[2], 100 * frostPlace[2][2], 100 * frostPlace[2][5]);
                // the dwarves' and the orcs' frosted windows follow the night too: each town never brighter at 02:00 than at
                // 21:00, the three together dimmer (Kharanos's are its inn's and its smithy's, both late: by the windows'
                // hashes all eight may stay lit); none at Stormwind's or Goldshire's (leaded)
                double frostFirst = 0, frostLate = 0;
                unsigned frostAll = 0;
                for (int p = 0; p < 3; ++p)
                {
                    CHECK(frostLights[p] > 0 && frostPlace[p][0] >= frostPlace[p][5]);
                    frostFirst += frostPlace[p][0] * frostLights[p];
                    frostLate += frostPlace[p][5] * frostLights[p];
                    frostAll += frostLights[p];
                }
                CHECK(frostAll > 0 && frostLate < frostFirst);
                CHECK(frostCells > 400 && frosted.size() > 300);
                // dimming through the night to the small hours, early risers lighting some again at dawn
                CHECK(mean[0] > 0.8f && mean[0] > mean[2] && mean[2] > mean[3] && mean[3] > mean[5] && mean[7] < 0.45f && mean[8] > mean[7]);
                CHECK(noBuilding == 0 && placeLights[0] > 50 && placeLights[1] > 10);
            });
        }
        else std::printf("SKIP the shipped table (no data folder given)\n");
    }
    catch (const std::exception& e)
    {
        std::printf("FAIL %s\n", e.what());
        return 1;
    }
    std::printf("%u passed\n", g_passed);
    return 0;
}
