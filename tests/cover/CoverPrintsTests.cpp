// Offline tests for features/CoverPrints (the client's footprints on top of the surface cover): every
// guard against the client executable itself, lifting and putting back prints in a pool laid out like
// the client's, and that the trench depth the prints follow is the one the cover uploads. No game.
//
// Usage:  cover_prints_tests <Wow.exe> [<another copy of the 12340 client, e.g. the launcher's exe>]
#include "features/CoverBlend.hpp"
#include "features/CoverPrints.hpp"
#include "features/CoverRipples.hpp"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace cp = wxl_livingazeroth::coverprints;
namespace blend = wxl_livingazeroth::cover::blend;
namespace rp = wxl_livingazeroth::cover::ripples;

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
        for (int c; (c = std::fgetc(f)) != EOF;) im.bytes.push_back(static_cast<uint8_t>(c));
        std::fclose(f);
        auto u32 = [&](size_t at)
        {
            if (at + 4 > im.bytes.size()) throw std::runtime_error("truncated");
            uint32_t v;
            std::memcpy(&v, im.bytes.data() + at, 4);
            return v;
        };
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

    // --- a pool laid out like the client's ---
    struct FakePrint
    {
        uint32_t entry;
        float x, y;
        std::vector<uint8_t> vertices; // kVertexSize each
    };

    float Terrain(float x, float y) { return 10.0f + 0.05f * x - 0.02f * y; } // a gentle slope
    bool TerrainAt(float x, float y, float& z)
    {
        if (x > 900.0f) return false; // "a hole / not loaded" there
        z = Terrain(x, y);
        return true;
    }

    void Put(std::vector<uint8_t>& pool, const FakePrint& p, uint32_t count)
    {
        uint8_t* e = pool.data() + p.entry * cp::kEntrySize;
        std::memcpy(e + cp::kEntryPosition, &p.x, 4);
        std::memcpy(e + cp::kEntryPosition + 4, &p.y, 4);
        const float z = Terrain(p.x, p.y);
        std::memcpy(e + cp::kEntryPosition + 8, &z, 4);
        std::memcpy(e + cp::kEntryVertexCount, &count, 4);
        const uint32_t ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p.vertices.data()));
        std::memcpy(e + cp::kEntryVertices, &ptr, 4);
    }

    // A print of `n` vertices around (x, y) at the terrain's height plus `above` (a WMO floor: > 0).
    FakePrint MakePrint(uint32_t entry, float x, float y, uint32_t n, float above = 0.0f)
    {
        FakePrint p{ entry, x, y, std::vector<uint8_t>(n * cp::kVertexSize, 0) };
        for (uint32_t k = 0; k < n; ++k)
        {
            const float vx = x + 2.0f * std::cos(k * 1.7f), vy = y + 2.0f * std::sin(k * 1.7f), vz = Terrain(vx, vy) + above;
            const float uv[4] = { 0.1f * k, 0.2f * k, 0.3f, 0.4f };
            uint8_t* v = p.vertices.data() + k * cp::kVertexSize;
            std::memcpy(v, &vx, 4); std::memcpy(v + 4, &vy, 4); std::memcpy(v + 8, &vz, 4); std::memcpy(v + 12, uv, 16);
        }
        return p;
    }

    float Z(const FakePrint& p, uint32_t k) { return cp::ReadFloat(p.vertices.data() + k * cp::kVertexSize + cp::kVertexZ); }

    // The trench depth as SurfaceCover.cpp uploaded it before CoverBlend.hpp had it.
    float OldUpload(float cover, float rimCell, float rimMul, float rim, float press, uint8_t stiff, float k)
    {
        const float deformed = cover * (1.0f + rimCell * rimMul * rim) * (1.0f - press * (1.0f - stiff / 255.0f));
        return cover + (deformed - cover) * k;
    }
}

int main(int argc, char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc < 2) { std::printf("usage: cover_prints_tests <Wow.exe> [<another copy>]\n"); return 2; }
    try
    {
        for (int a = 1; a < argc; ++a)
        {
            const std::string name = std::string("guards against ") + argv[a];
            Test(name.c_str(), [&]
            {
                const Image im = LoadImage(argv[a]);
                CHECK(im.base == cp::kClientImageBase && im.timestamp == cp::kClientTimestamp);
                const cp::Guard* bad = cp::FirstMismatch([&](uintptr_t va, uint32_t size) { return im.At(va, size); });
                if (bad) throw std::runtime_error(std::string("differs: ") + bad->what);
                // And a guard does catch a changed byte: each in turn.
                for (const cp::Guard& g : cp::kGuards)
                {
                    Image changed = im;
                    auto* at = static_cast<uint8_t*>(const_cast<void*>(changed.At(g.address, g.size)));
                    CHECK(at);
                    at[g.size - 1] ^= 0x01;
                    CHECK(cp::FirstMismatch([&](uintptr_t va, uint32_t size) { return changed.At(va, size); }) == &g);
                }
                std::printf("  %zu guards, %zu bytes\n", sizeof(cp::kGuards) / sizeof(cp::kGuards[0]),
                            [] { size_t n = 0; for (const cp::Guard& g : cp::kGuards) n += g.size; return n; }());
            });
        }

        Test("lift: prints on covered terrain rise by the cover there (+ the margin), the rest stay; put back exactly", []
        {
            std::vector<uint8_t> pool(cp::kPoolEntries * cp::kEntrySize, 0);
            FakePrint covered = MakePrint(3, 100.0f, 200.0f, 6);       // on the cover
            FakePrint player = MakePrint(0, 102.0f, 200.0f, 3);        // the player's ring, also covered
            FakePrint bare = MakePrint(70, 300.0f, 200.0f, 6);         // no cover there
            FakePrint wmo = MakePrint(71, 100.5f, 200.0f, 6, 2.0f);    // on a WMO floor above covered terrain
            FakePrint dead = MakePrint(72, 101.0f, 200.0f, 6);         // run out: count 0
            FakePrint huge = MakePrint(73, 101.5f, 200.0f, 6);         // a count no print has
            FakePrint hole = MakePrint(74, 950.0f, 200.0f, 6);         // no terrain under its centre
            Put(pool, covered, 6); Put(pool, player, 3); Put(pool, bare, 6); Put(pool, wmo, 6); Put(pool, dead, 0);
            Put(pool, huge, cp::kMaxVertices + 1); Put(pool, hole, 6);
            const std::vector<uint8_t> poolBefore = pool;
            const FakePrint before[] = { covered, player, bare, wmo, dead, huge, hole };

            auto liftAt = [](float x, float, float) { return x < 200.0f ? 0.08f : 0.0f; }; // cover west of x = 200
            cp::LiftRun run;
            cp::LiftPrints(pool.data(), liftAt, TerrainAt, run);
            CHECK(run.live == 5 && run.onCover == 2 && run.lifted.size() == 2); // run out or impossible: not live
            for (uint32_t k = 0; k < 6; ++k) CHECK(std::fabs(Z(covered, k) - (Z(before[0], k) + 0.08f + cp::kAboveSurface)) < 1e-5f);
            for (uint32_t k = 0; k < 3; ++k) CHECK(std::fabs(Z(player, k) - (Z(before[1], k) + 0.08f + cp::kAboveSurface)) < 1e-5f);
            CHECK(bare.vertices == before[2].vertices && wmo.vertices == before[3].vertices && dead.vertices == before[4].vertices &&
                  huge.vertices == before[5].vertices && hole.vertices == before[6].vertices);
            CHECK(pool == poolBefore); // only vertices move, never the pool itself
            // x, y and the texture coordinates never change
            for (uint32_t k = 0; k < 6; ++k)
                CHECK(std::memcmp(covered.vertices.data() + k * cp::kVertexSize, before[0].vertices.data() + k * cp::kVertexSize, 8) == 0 &&
                      std::memcmp(covered.vertices.data() + k * cp::kVertexSize + 12, before[0].vertices.data() + k * cp::kVertexSize + 12, 16) == 0);

            // The client lets a print run out during its draw (count to 0, memory kept): still put back.
            const uint32_t zero = 0;
            std::memcpy(pool.data() + covered.entry * cp::kEntrySize + cp::kEntryVertexCount, &zero, 4);
            cp::RestorePrints(pool.data(), run);
            CHECK(covered.vertices == before[0].vertices && player.vertices == before[1].vertices);
        });
        Test("put back: never into another vertex array, and only what was saved", []
        {
            std::vector<uint8_t> pool(cp::kPoolEntries * cp::kEntrySize, 0);
            FakePrint a = MakePrint(5, 10.0f, 10.0f, 6), b = MakePrint(6, 12.0f, 10.0f, 6);
            Put(pool, a, 6); Put(pool, b, 6);
            const FakePrint a0 = a, b0 = b;
            cp::LiftRun run;
            cp::LiftPrints(pool.data(), [](float, float, float) { return 0.1f; }, TerrainAt, run);
            CHECK(run.onCover == 2);
            // Entry 5 now points at another array: that one isn't touched, nor is the old one written.
            FakePrint other = MakePrint(5, 10.0f, 10.0f, 6);
            const FakePrint other0 = other;
            Put(pool, other, 6);
            // A lift cut short: entry 6 saved only 2 of its 6 heights.
            run.z.resize(run.lifted[1].firstZ + 2);
            cp::RestorePrints(pool.data(), run);
            CHECK(other.vertices == other0.vertices);
            CHECK(a.vertices != a0.vertices); // left lifted: its array was no longer the entry's
            CHECK(Z(b, 0) == Z(b0, 0) && Z(b, 1) == Z(b0, 1) && Z(b, 2) != Z(b0, 2));
        });
        Test("an empty pool: nothing live, nothing lifted", []
        {
            std::vector<uint8_t> pool(cp::kPoolEntries * cp::kEntrySize, 0);
            cp::LiftRun run;
            cp::LiftPrints(pool.data(), [](float, float, float) { return 1.0f; }, TerrainAt, run);
            CHECK(run.live == 0 && run.onCover == 0 && run.lifted.empty() && run.z.empty());
            cp::RestorePrints(pool.data(), run);
        });
        Test("the trench depth prints follow is bit for bit the one the cover uploads", []
        {
            const float covers[] = { 0.0f, 0.004f, 0.06f, 0.1f, 0.35f, 0.6f };
            const float presses[] = { 0.0f, 0.13f, 0.5f, 0.97f, 1.0f };
            const float rims[] = { 0.0f, 0.4f, 1.0f };
            const uint8_t stiffs[] = { 0, 51, 255 };
            const float ks[] = { 0.0f, 0.35f, 1.0f };
            const float rimCells[] = { 0.3f, 0.4f, 4.0f };
            const float rimMuls[] = { 0.0f, 1.0f, 1.7f };
            unsigned n = 0;
            for (float c : covers) for (float p : presses) for (float r : rims) for (uint8_t s : stiffs) for (float k : ks)
                for (float rc : rimCells) for (float rm : rimMuls)
                {
                    const float now = blend::DeformedDepth(c, rc * rm, r, p, s / 255.0f, k), old = OldUpload(c, rc, rm, r, p, s, k);
                    CHECK(std::memcmp(&now, &old, 4) == 0);
                    ++n;
                }
            CHECK(blend::DeformedDepth(0.1f, 0.4f, 0.0f, 1.0f, 0.0f, 1.0f) == 0.0f);   // fully pressed, soft: to the ground
            CHECK(blend::DeformedDepth(0.1f, 0.4f, 0.0f, 0.0f, 0.0f, 1.0f) == 0.1f);   // untouched
            std::printf("  %u combinations\n", n);
        });

        Test("new prints: the first walk only notes what is there; then each print put down counts once, dead ones never", []
        {
            std::vector<uint8_t> pool(cp::kPoolEntries * cp::kEntrySize, 0);
            FakePrint a = MakePrint(3, 100.0f, 200.0f, 6), b = MakePrint(70, 110.0f, 205.0f, 3);
            Put(pool, a, 6);
            std::vector<cp::PrintSeen> seen;
            std::vector<cp::NewPrint> found;
            const auto walk = [&] { found.clear(); cp::FindNewPrints(pool.data(), seen, [&](const cp::NewPrint& p) { found.push_back(p); }); };
            walk();
            CHECK(found.empty() && seen.size() == cp::kPoolEntries && seen[3].live);  // there before anyone looked
            walk();
            CHECK(found.empty());                                                       // still the same print
            Put(pool, b, 3);
            walk();
            CHECK(found.size() == 1 && found[0].entry == 70 && found[0].x == 110.0f && found[0].y == 205.0f &&
                  std::fabs(found[0].z - Terrain(110.0f, 205.0f)) < 1e-6f);
            FakePrint a2 = MakePrint(3, 100.5f, 200.0f, 6);                             // entry 3 reused for the next step
            Put(pool, a2, 6);
            walk();
            CHECK(found.size() == 1 && found[0].entry == 3 && found[0].x == 100.5f);
            const uint32_t zero = 0;                                                    // b runs out...
            std::memcpy(pool.data() + 70 * cp::kEntrySize + cp::kEntryVertexCount, &zero, 4);
            walk();
            CHECK(found.empty() && !seen[70].live);
            Put(pool, b, 3);                                                            // ...and a print comes back there
            walk();
            CHECK(found.size() == 1 && found[0].entry == 70);
            const uint32_t huge = cp::kMaxVertices + 1;                                 // impossible counts are not prints
            std::memcpy(pool.data() + 9 * cp::kEntrySize + cp::kEntryVertexCount, &huge, 4);
            walk();
            CHECK(found.empty() && !seen[9].live);
        });

        Test("sand's rims: the print's outline pointed along the stride, lasting 2.5 x the relax time, the youngest 48 kept", []
        {
            float along = 0, across = 0;
            rp::Outline(0.33f, 0.15f, true, along, across);
            CHECK(std::fabs(along - 0.165f) < 1e-6f && std::fabs(across - 0.075f) < 1e-6f);
            rp::Outline(0.33f, 0.15f, false, along, across);                       // no direction yet: a circle
            CHECK(along == across && std::fabs(along - 0.165f) < 1e-6f);
            rp::Outline(0.0f, 0.0f, true, along, across);                          // unknown: a human's
            CHECK(std::fabs(along - 0.165f) < 1e-6f && across < along);
            rp::Outline(10.0f, 10.0f, true, along, across);
            CHECK(along == 1.2f && across == 1.2f);
            // Newest first: the other foot 0.5 yd back and to the side, the same foot a stride (1 yd) straight back.
            const float history[4] = { 9.6f, 0.2f, 9.0f, 0.0f };
            float axis[2] = {};
            CHECK(rp::StrideDirection(history, 2, 10.0f, 0.0f, axis) && std::fabs(axis[0] - 1.0f) < 1e-6f && std::fabs(axis[1]) < 1e-6f);
            CHECK(rp::StrideDirection(history, 1, 10.0f, 0.0f, axis) && axis[0] > 0.8f && axis[1] < -0.3f);   // only the other foot's
            const float away[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            CHECK(!rp::StrideDirection(away, 2, 10.0f, 0.0f, axis) && !rp::StrideDirection(away, 0, 10.0f, 0.0f, axis));
            CHECK(rp::LifeFor(10.0f) == 25.0f && rp::LifeFor(20.0f) == 50.0f && rp::LifeFor(2.0f) == 20.0f);
            CHECK(rp::Strength(0.0f, 20.0f) == 1.0f && rp::Strength(10.0f, 20.0f) == 1.0f);
            CHECK(std::fabs(rp::Strength(15.0f, 20.0f) - 0.5f) < 1e-5f);
            CHECK(rp::Strength(20.0f, 20.0f) == 0.0f && rp::Strength(25.0f, 20.0f) == 0.0f && rp::Strength(1.0f, 0.0f) == 0.0f);
            float last = 1.0f;
            for (float age = 10.0f; age <= 20.0f; age += 0.25f) { const float s = rp::Strength(age, 20.0f); CHECK(s <= last); last = s; }
            rp::Set set;
            for (int k = 0; k < rp::kMax + 5; ++k)
            {
                rp::Ripple r;
                r.x = static_cast<float>(k);
                r.born = static_cast<float>(k);
                r.life = 100.0f;
                set.Add(r);
            }
            CHECK(set.List().size() == static_cast<size_t>(rp::kMax));
            for (const rp::Ripple& r : set.List()) CHECK(r.born >= 5.0f);  // the five oldest made room
            set.Expire(104.5f);                                              // 5..36 live until 105..136
            CHECK(set.List().size() == static_cast<size_t>(rp::kMax));
            set.Expire(110.0f);                                              // born 5..10 gone
            CHECK(set.List().size() == static_cast<size_t>(rp::kMax - 6));
            for (const rp::Ripple& r : set.List()) CHECK(r.born > 10.0f);
        });

        // A print laid out the way the client lays it: its texture projected from above, turned `turn` (radians, the way
        // the toes point), across x along yd, mirrored for the other foot, centred on (x, y); vertices a few yd around.
        const auto mapping = [](float turn, float acrossYd, float alongYd, bool mirrored, float map[6])
        {
            const float c = std::cos(turn), sn = std::sin(turn), m = mirrored ? -1.0f : 1.0f;
            map[0] = m * -sn / acrossYd; map[1] = m * c / acrossYd; map[2] = 0.5f;   // u across the print
            map[3] = -c / alongYd;       map[4] = -sn / alongYd;    map[5] = 0.5f;   // v from the toes back
        };
        const auto texturedPrint = [](uint32_t entry, float x, float y, const float map[6], uint32_t n)
        {
            FakePrint p{ entry, x, y, std::vector<uint8_t>(n * cp::kVertexSize, 0) };
            for (uint32_t k = 0; k < n; ++k)
            {
                const float dx = 2.5f * std::cos(k * 2.1f) + 0.3f * k, dy = 2.5f * std::sin(k * 2.1f) - 0.2f * k;
                const float vx = x + dx, vy = y + dy, vz = Terrain(vx, vy);
                const float uv[4] = { map[0] * dx + map[1] * dy + map[2], map[3] * dx + map[4] * dy + map[5], 0.5f, 0.5f };
                uint8_t* v = p.vertices.data() + k * cp::kVertexSize;
                std::memcpy(v, &vx, 4); std::memcpy(v + 4, &vy, 4); std::memcpy(v + 8, &vz, 4); std::memcpy(v + 12, uv, 16);
            }
            return p;
        };

        Test("print placement: the texture's coordinates over the ground from a print's own vertices, turned and mirrored", [&]
        {
            float want[6], got[6];
            mapping(0.7f, 0.15f, 0.33f, true, want);
            FakePrint p = texturedPrint(5, 1234.5f, -567.25f, want, 6);
            CHECK(cp::FitPrintMapping(p.vertices.data(), 6, p.x, p.y, got));
            for (int i = 0; i < 6; ++i) CHECK(std::fabs(got[i] - want[i]) < 2e-3f * (1.0f + std::fabs(want[i])));
            CHECK(!cp::FitPrintMapping(p.vertices.data(), 2, p.x, p.y, got));                // too few
            FakePrint junk = MakePrint(6, 10.0f, 20.0f, 6);                                    // coordinates no plane gives
            CHECK(!cp::FitPrintMapping(junk.vertices.data(), 6, junk.x, junk.y, got));
            FakePrint line = texturedPrint(7, 0.0f, 0.0f, want, 4);
            for (uint32_t k = 0; k < 4; ++k)                                                   // all in a line
            {
                const float vx = static_cast<float>(k), vy = 2.0f * k;
                std::memcpy(line.vertices.data() + k * cp::kVertexSize, &vx, 4);
                std::memcpy(line.vertices.data() + k * cp::kVertexSize + 4, &vy, 4);
            }
            CHECK(!cp::FitPrintMapping(line.vertices.data(), 4, 0.0f, 0.0f, got));
            // Found with the print: its texture and placement.
            std::vector<uint8_t> pool(cp::kPoolEntries * cp::kEntrySize, 0);
            std::vector<cp::PrintSeen> seen;
            std::vector<cp::NewPrint> found;
            cp::FindNewPrints(pool.data(), seen, [&](const cp::NewPrint& n) { found.push_back(n); });
            Put(pool, p, 6);
            const uint32_t texture = 7;
            std::memcpy(pool.data() + p.entry * cp::kEntrySize + cp::kEntryTexture, &texture, 4);
            cp::FindNewPrints(pool.data(), seen, [&](const cp::NewPrint& n) { found.push_back(n); });
            CHECK(found.size() == 1 && found[0].texture == 7 && found[0].mapped);
            for (int i = 0; i < 6; ++i) CHECK(std::fabs(found[0].map[i] - want[i]) < 2e-3f * (1.0f + std::fabs(want[i])));
        });

        Test("rims follow the print's own outline: each texture's taken onto the ground, turned and mirrored with the print, the rim just outside it", [&]
        {
            // An oval: the series gives its radius back.
            const float axis[2] = { 0.6f, 0.8f };
            rp::Shape oval;
            CHECK(rp::ShapeFromOval(5.0f, 7.0f, axis, 0.165f, 0.075f, oval));
            float worstOval = 0.0f;
            for (int i = 0; i < 90; ++i)
            {
                const float t = 6.2831853f * i / 90, c = std::cos(t), sn = std::sin(t);
                const float along = c * axis[0] + sn * axis[1], across = -c * axis[1] + sn * axis[0];
                const float r = 1.0f / std::sqrt(along * along / (0.165f * 0.165f) + across * across / (0.075f * 0.075f));
                worstOval = std::max(worstOval, std::fabs(rp::ShapeRadius(oval, t) * oval.r0 - r) / r);
            }
            CHECK(worstOval < 0.05f);
            rp::Shape circle;
            CHECK(rp::ShapeFromOval(0.0f, 0.0f, axis, 0.2f, 0.2f, circle) && std::fabs(circle.r0 - 0.2f) < 2e-3f);
            for (int k = 0; k < rp::kShapeTerms; ++k) CHECK(std::fabs(circle.a[k]) < 2e-3f && std::fabs(circle.b[k]) < 2e-3f);

            float worstMean = 0.0f, worstEdge = 0.0f, worstTurn = 0.0f, worstMirror = 0.0f, leastPeak = 1.0f, mostAtEdge = 0.0f;
            for (const rp::BakedOutline& o : rp::kOutlines)
            {
                float map[6];
                mapping(0.4f, 0.3f, 0.45f, false, map);
                rp::Shape s;
                CHECK(rp::ShapeFromPrint(o.texture, map, 300.0f, 400.0f, s));
                CHECK(s.r0 > 0.05f && s.r0 < 0.3f);
                // The outline's own points, onto the ground: the series lies close to them, and the rim starts there.
                const float det = map[0] * map[4] - map[1] * map[3];
                float sum = 0.0f, onEdge = 0.0f;
                for (int i = 0; i < rp::kOutlineRays; ++i)
                {
                    const float t = 6.2831853f * i / rp::kOutlineRays;
                    const float du = o.cu + o.radius[i] * std::cos(t) - map[2], dv = o.cv + o.radius[i] * std::sin(t) - map[5];
                    const float x = 300.0f + (map[4] * du - map[1] * dv) / det, y = 400.0f + (map[0] * dv - map[3] * du) / det;
                    const float d = std::hypot(x - s.cx, y - s.cy), at = std::atan2(y - s.cy, x - s.cx);
                    const float off = std::fabs(rp::ShapeRadius(s, at) * s.r0 - d) / s.r0;
                    sum += off;
                    worstEdge = std::max(worstEdge, off);
                    float h = 0.0f, g[2] = {};
                    rp::RimShade(s, 1.0f, x, y, 1e-4f, h, g);
                    onEdge += h;
                    // On the rim's centre line, kRimOffset r0 further out along the same direction.
                    const float r = rp::ShapeRadius(s, at) + rp::kRimOffset;
                    float hp = 0.0f;
                    rp::RimShade(s, 1.0f, s.cx + r * s.r0 * std::cos(at), s.cy + r * s.r0 * std::sin(at), 1e-4f, hp, g);
                    leastPeak = std::min(leastPeak, hp);
                }
                worstMean = std::max(worstMean, sum / rp::kOutlineRays);
                mostAtEdge = std::max(mostAtEdge, onEdge / rp::kOutlineRays);
                // Turned with the print.
                float turned[6];
                mapping(0.4f + 1.3f, 0.3f, 0.45f, false, turned);
                rp::Shape t2;
                CHECK(rp::ShapeFromPrint(o.texture, turned, 300.0f, 400.0f, t2));
                // Mirrored for the other foot: mirrored about the line the print points along (direction 0.4).
                float mirrored[6];
                mapping(0.4f, 0.3f, 0.45f, true, mirrored);
                rp::Shape m2;
                CHECK(rp::ShapeFromPrint(o.texture, mirrored, 300.0f, 400.0f, m2));
                for (int i = 0; i < 72; ++i)
                {
                    const float t = 6.2831853f * i / 72;
                    worstTurn = std::max(worstTurn, std::fabs(rp::ShapeRadius(t2, t + 1.3f) * t2.r0 - rp::ShapeRadius(s, t) * s.r0) / s.r0);
                    worstMirror = std::max(worstMirror, std::fabs(rp::ShapeRadius(m2, 0.8f - t) * m2.r0 - rp::ShapeRadius(s, t) * s.r0) / s.r0);
                }
                // Its rim never reaches past where the shader stops looking.
                for (int i = 0; i < 360; ++i)
                {
                    const float t = 6.2831853f * i / 360;
                    const float c = std::cos(t), sn = std::sin(t), edge = s.reach / std::max(std::fabs(c), std::fabs(sn));
                    float h = 0.0f, g[2] = {};
                    rp::RimShade(s, 1.0f, s.cx + edge * c, s.cy + edge * sn, 1e-4f, h, g);
                    CHECK(h == 0.0f);
                }
            }
            std::printf("  oval within %.3f; outlines: off by %.3f r0 on average at worst, %.3f at the worst point; turned %.4f, mirrored %.4f; "
                        "rim on the outline %.3f on average at most, on its centre line at least %.3f\n", worstOval, worstMean, worstEdge, worstTurn, worstMirror, mostAtEdge, leastPeak);
            // Eight terms bridge the cloven hoof's narrow cleft and round the claw's tips (the worst points); elsewhere the
            // series keeps within a few hundredths of r0, and the rim rises only just outside the outline.
            CHECK(worstMean < 0.1f && worstEdge < 0.7f && worstTurn < 0.03f && worstMirror < 0.03f && mostAtEdge < 0.25f && leastPeak > 0.97f);
            float any[6];
            mapping(0.0f, 0.3f, 0.45f, false, any);
            CHECK(!rp::ShapeFromPrint(2, any, 0.0f, 0.0f, oval));                       // no texture 2 in FootprintTextures.dbc

            // As the shaders get it: the coefficients to 0.001, the reach and the strength.
            for (float c = -1.0f; c <= 1.0f; c += 0.0137f)
            {
                const float v = rp::PackPair(c, -0.5f * c);
                CHECK(std::fabs(rp::UnpackLo(v) - c) < 1.1e-3f && std::fabs(rp::UnpackHi(v) + 0.5f * c) < 1.1e-3f);
            }
            rp::Shape s;
            float map[6];
            mapping(2.0f, 0.3f, 0.45f, true, map);
            CHECK(rp::ShapeFromPrint(5, map, 1000.0f, 2000.0f, s));
            float regs[4 * rp::kRegistersPerRim];
            rp::PackRim(s, 990.0f, 2010.0f, 0.37f, regs);
            CHECK(std::fabs(regs[0] - (s.cx - 990.0f)) < 1e-4f && std::fabs(regs[1] - (s.cy - 2010.0f)) < 1e-4f && std::fabs(regs[2] * s.r0 - 1.0f) < 1e-5f);
            CHECK(std::floor(regs[3]) / 1024.0f >= s.reach && std::floor(regs[3]) / 1024.0f < s.reach + 0.001f);
            CHECK(std::fabs((regs[3] - std::floor(regs[3])) / 0.99f - 0.37f) < 1e-3f);
        });

        std::printf("%u test groups passed\n", g_passed);
        return 0;
    }
    catch (const std::exception& e)
    {
        std::printf("FAIL %s\n", e.what());
        return 1;
    }
}
