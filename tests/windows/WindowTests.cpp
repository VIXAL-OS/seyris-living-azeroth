// Offline tests for env/WindowModel (window lights and glow): the night fade against the client's own curve, the
// window index, the spot's direction, the glass's rectangle, the light budget in a grid cell, and -- given the
// client executable -- the guards and the key table against its bytes, and -- given the data folder -- the shipped
// WindowLights.cdbc (inside the budget, its lights night only). No game.
//
// Usage:  windows_tests [<Wow.exe>] [<repo data folder>]
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
        float F(size_t r, size_t c) const { float f; std::memcpy(&f, &rows[r][c], 4); return f; }
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
        return t;
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
