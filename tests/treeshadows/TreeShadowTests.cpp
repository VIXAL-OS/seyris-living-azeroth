// Offline tests for env/TreeShadowModel (tree shadows that follow the wind, features/TreeShadows): the refresh a frame runs
// (RunFrame, the code the extension's hook calls) over a fake client that reads the query data where the client's own
// code does -- the offsets taken from its instructions when the exe is given -- which bands are drawn and how, the rate,
// the gates (quality, wxl-treewind, callbacks, the viewer, trees in range), faults; the foliage words; the batch count;
// and, given the client executable, every guard against its bytes and the tables the comments rely on. No game.
//
// Usage:  treeshadow_tests [<Wow.exe> [<another exe of the same build, e.g. Wow-WarcraftXL.exe>]]
#include "env/TreeShadowModel.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace ts = wxl_livingazeroth::treeshadow;

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
        template <class T> T Get(uintptr_t va) const
        {
            const void* p = At(va, sizeof(T));
            CHECK(p);
            T v;
            std::memcpy(&v, p, sizeof(T));
            return v;
        }
        uintptr_t CallTarget(uintptr_t at) const
        {
            CHECK(Get<uint8_t>(at) == 0xE8);
            return static_cast<uintptr_t>(at + 5 + Get<int32_t>(at + 1));
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
        CHECK(u32(pe) == 0x00004550);
        im.timestamp = u32(pe + 8);
        const uint32_t count = u16(pe + 6), optional = u16(pe + 20);
        im.base = u32(pe + 24 + 28);
        const size_t table = pe + 24 + optional;
        for (uint32_t i = 0; i < count; ++i)
        {
            const size_t s = table + 40 * i;
            im.sections.push_back({u32(s + 12), u32(s + 8), u32(s + 20), u32(s + 16)});
        }
        return im;
    }

    // --- where the client's own code reads and writes the query data (its instructions' displacements) ---
    struct Layout
    {
        size_t flags = ts::kQdFlags, active = ts::kQdActive, useCull = ts::kQdUseCull, extents = ts::kQdExtents,
               frustums = ts::kQdFrustums, pos = ts::kQdPos, texIndex = ts::kQdTexIndex, radius = ts::kQdRadius,
               bounds = ts::kQdBounds, texBounds = ts::kQdTexBounds, matrices = ts::kQdMatrices, lastBand = ts::kQdLastBand,
               useFrustum = ts::kQdUseFrustum;
        int32_t staticFlags = ts::kQueryStatic;
    };

    Layout LayoutFrom(const Image& im)
    {
        Layout l;
        l.lastBand = im.Get<uint32_t>(0x00874FCF + 2);          // cmp [edi+disp32], esi
        l.active = im.Get<uint8_t>(0x00874FE0 + 2);             // lea edx, [edi+disp8]
        l.texIndex = im.Get<uint32_t>(0x0087501A + 4);          // movzx ecx, byte [esi+edi+disp32]
        l.radius = im.Get<uint32_t>(0x008749F4 + 6 + 3);        // fstp [esi+edi*4+disp32]
        l.pos = im.Get<uint32_t>(0x00874A2A + 3) * 4;           // lea eax, [edi+edi*2+disp32], then [esi+eax*4]
        l.bounds = im.Get<uint32_t>(0x00874A90 + 2);            // fst [ebx+disp32], ebx = band*16 + data
        l.texBounds = im.Get<uint32_t>(0x00874C4C + 2) - 8;     // fstp [ebx+disp32]: m_texBounds[band][2]
        l.matrices = im.Get<uint32_t>(0x00874C60 + 7 + 3);      // lea eax, [edx+esi+disp32], after a push
        l.extents = im.Get<uint8_t>(0x00874C79 + 3);            // lea edx, [esi+ecx*8+disp8]
        l.frustums = im.Get<uint8_t>(0x00874C84 + 2);           // add ecx, disp8
        l.flags = 0;                                            // mov [esi+edi*4], imm32
        l.staticFlags = im.Get<int32_t>(0x00874CC2 + 3);
        l.useCull = im.Get<uint8_t>(0x00874CA9 + 3);            // mov [esi+edi*4+disp8], 1
        l.useFrustum = static_cast<size_t>(im.Get<int32_t>(0x008762FB + 2) + 0xB8C); // [ebp-0x104], data at [ebp-0xB8C]
        CHECK(im.Get<int32_t>(0x0087631A + 2) == -0xB8C);       // lea ecx, [ebp-0xB8C]: the data RenderBands is given
        return l;
    }

    // --- a fake client: what RunFrame calls, recorded; reads the data where the client's code does ---
    struct Seen
    {
        int32_t lastBand = -1, useFrustum = -1;
        int32_t active[3] = {}, flags[3] = {}, useCull[3] = {};
        float   pos[3][3] = {}, radius[3] = {}, bounds[3][4] = {}, texBounds[3][4] = {};
        uint8_t texIndex[3] = {};
    };

    struct Fake
    {
        int quality = 3;
        bool treeWind = true, stock = true, viewerOk = true, failQuery = false, failRender = false;
        float viewer[3] = {-9460.0f, 60.0f, 56.0f};
        ts::BandView band[2];
        double now = 10000.0;
        std::vector<uintptr_t> casters[2];        // the model of each batch a query gathers for the band
        std::map<uintptr_t, std::string> paths;
        unsigned groups = 2;
        Layout layout;
        std::vector<std::string> calls;
        std::vector<Seen> renders;
        std::vector<uintptr_t> gathered[3];
        unsigned queries = 0;
    };
    Fake g;

    template <class T> T At(const uint8_t* data, size_t offset) { T v; std::memcpy(&v, data + offset, sizeof(T)); return v; }

    Seen Read(const uint8_t* data)
    {
        const Layout& l = g.layout;
        Seen s;
        s.lastBand = At<int32_t>(data, l.lastBand);
        s.useFrustum = At<int32_t>(data, l.useFrustum);
        for (int b = 0; b < 3; ++b)
        {
            s.active[b] = At<int32_t>(data, l.active + b * 4);
            s.flags[b] = At<int32_t>(data, l.flags + b * 4);
            s.useCull[b] = At<int32_t>(data, l.useCull + b * 4);
            for (int k = 0; k < 3; ++k) s.pos[b][k] = At<float>(data, l.pos + b * 12 + k * 4);
            s.radius[b] = At<float>(data, l.radius + b * 4);
            for (int k = 0; k < 4; ++k)
            {
                s.bounds[b][k] = At<float>(data, l.bounds + b * 16 + k * 4);
                s.texBounds[b][k] = At<float>(data, l.texBounds + b * 16 + k * 4);
            }
            s.texIndex[b] = At<uint8_t>(data, l.texIndex + b);
        }
        return s;
    }

    int FQuality() { return g.quality; }
    bool FTreeWind() { return g.treeWind; }
    bool FStock() { return g.stock; }
    bool FViewer(float out[3]) { std::memcpy(out, g.viewer, 12); return g.viewerOk; }
    bool FBand(int i, ts::BandView& v) { v = g.band[i]; return true; }
    bool FFrustum(uint8_t* data, int band)
    {
        g.calls.push_back("frustum " + std::to_string(band));
        // As the callback and CFrustum::Extents do: the band's matrix and extents written.
        std::memset(data + g.layout.matrices + band * ts::kMatrixSize, 0x11, ts::kMatrixSize);
        std::memset(data + g.layout.extents + band * ts::kAaBoxSize, 0x22, ts::kAaBoxSize);
        g.now += 0.05;
        return true;
    }
    bool FQuery(uint8_t* data)
    {
        g.calls.push_back("query");
        ++g.queries;
        // As CShadowQuery::QueryCallback: the buffers emptied, then every band up to m_lastBand gathers what its flags ask
        // for (8: everything static).
        const Seen s = Read(data);
        for (auto& v : g.gathered) v.clear();
        for (int b = 0; b <= s.lastBand && b < 2; ++b)
            if (s.flags[b] & 8) g.gathered[b] = g.casters[b];
        g.now += 0.20;
        return !g.failQuery;
    }
    const char* FPathOf(uintptr_t model)
    {
        const auto it = g.paths.find(model);
        return it == g.paths.end() ? nullptr : it->second.c_str();
    }
    ts::FoliageCache g_cache;
    bool FCasters(int band, ts::Casters& out)
    {
        g.calls.push_back("casters " + std::to_string(band));
        std::vector<uint8_t> records(g.gathered[band].size() * ts::kBatchStride);
        for (size_t i = 0; i < g.gathered[band].size(); ++i) std::memcpy(records.data() + i * ts::kBatchStride, &g.gathered[band][i], 4);
        out.untextured = static_cast<unsigned>(g.gathered[band].size());
        out.groups = g.groups;
        if (!records.empty()) ts::CountBatches(records.data(), out.untextured, &FPathOf, g_cache, out);
        return true;
    }
    bool FRender(uint8_t* data)
    {
        g.calls.push_back("render");
        g.renders.push_back(Read(data));
        g.now += 0.30;
        return !g.failRender;
    }
    double FNow() { return g.now; }
    const ts::Client kFakeClient = {&FQuality, &FTreeWind, &FStock, &FViewer, &FBand, &FFrustum, &FQuery, &FCasters, &FRender, &FNow};

    // A forest and a town, as the query finds them.
    void Reset()
    {
        const Layout layout = g.layout;
        g = Fake{};
        g.layout = layout;
        g.band[0].radius = 40.0f;
        g.band[0].lastPos[0] = -9461.5f;
        g.band[0].lastPos[1] = 61.25f;
        g.band[0].lastPos[2] = 55.0f;
        g.band[0].current = 1;
        g.band[0].textures = true;
        g.band[1] = g.band[0];
        g.band[1].radius = 160.0f;
        g.band[1].current = 0;
        g.band[1].lastPos[0] = -9458.0f;
        g.paths[0x1000] = "World\\Azeroth\\Elwynn\\PassiveDoodads\\Trees\\ElwynnTreeCanopy01.m2";
        g.paths[0x2000] = "World\\Generic\\Human\\Passive Doodads\\StreetLamps\\StormwindStreetLamp01.m2";
        g.paths[0x3000] = "World\\Azeroth\\Elwynn\\PassiveDoodads\\Rocks\\ElwynnRock1.m2";
        g.paths[0x4000] = "World\\Expansion02\\Doodads\\GrizzlyHills\\Trees\\GrizzlyHills_Bush01.m2";
        g.casters[0] = {0x3000, 0x3000, 0x1000, 0x1000, 0x1000, 0x2000};
        g.casters[1] = {0x4000, 0x3000};
    }

    std::vector<uint8_t> Data()
    {
        // The client's query data after its own bands: band 0's slot holds the 20 yd map's query (flags 5).
        std::vector<uint8_t> d(ts::kQdSize, 0xCD);
        const int32_t five = 5;
        std::memcpy(d.data() + ts::kQdFlags, &five, 4);
        return d;
    }

    ts::Settings Near(float rate = 30.0f) { ts::Settings s; s.rate = rate; return s; }

    void CheckWholeBand(const Seen& s, int b, const ts::BandView& v)
    {
        CHECK(s.active[b] == 1 && s.flags[b] == g.layout.staticFlags && s.useCull[b] == 0);
        CHECK(s.pos[b][0] == v.lastPos[0] && s.pos[b][1] == v.lastPos[1] && s.pos[b][2] == v.lastPos[2]);
        CHECK(s.radius[b] == v.radius);
        CHECK(s.bounds[b][0] == -v.radius && s.bounds[b][1] == v.radius && s.bounds[b][2] == -v.radius && s.bounds[b][3] == v.radius);
        CHECK(s.texBounds[b][0] == 0.0f && s.texBounds[b][1] == 1.0f && s.texBounds[b][2] == 0.0f && s.texBounds[b][3] == 1.0f);
        CHECK(s.texIndex[b] == v.current);
    }
}

int main(int argc, char** argv)
{
    try
    {
        std::vector<Image> images;
        for (int i = 1; i < argc; ++i) images.push_back(LoadImage(argv[i]));

        for (size_t i = 0; i < images.size(); ++i)
        {
            const std::string name = std::string("guards against ") + argv[i + 1];
            Test(name.c_str(), [&]
            {
                const Image& im = images[i];
                CHECK(im.base == 0x00400000u && im.timestamp == 0x4C2452FEu);
                const ts::Guard* bad = ts::FirstMismatch([&](uintptr_t va, uint32_t size) { return im.At(va, size); });
                if (bad) throw std::runtime_error(std::string("differs: ") + bad->what);
                // Each guard catches a change of its own bytes.
                for (const ts::Guard& gd : ts::kGuards)
                {
                    Image changed = im;
                    auto* at = static_cast<uint8_t*>(const_cast<void*>(changed.At(gd.address, gd.size)));
                    CHECK(at);
                    at[gd.size - 1] ^= 0x01;
                    CHECK(ts::FirstMismatch([&](uintptr_t va, uint32_t size) { return changed.At(va, size); }) == &gd);
                }
                // What the comments say of the client: the band radii and the moved distances (squared) that start a
                // band's redraw, quality to shadow tier, Update calling RenderBands, GatherBands the extents.
                CHECK(im.Get<float>(0x00B1D520) == 40.0f && im.Get<float>(0x00B1D524) == 160.0f && im.Get<float>(0x00B1D528) == 640.0f);
                CHECK(im.Get<float>(0x00B1D52C) == 4.0f && im.Get<float>(0x00B1D530) == 16.0f && im.Get<float>(0x00B1D534) == 1024.0f);
                const uint32_t tiers[7] = {0, 1, 1, 2, 2, 3, 3};
                for (int q = 0; q < 7; ++q) CHECK(im.Get<uint32_t>(0x00B1D554 + q * 4) == tiers[q]);
                CHECK(im.CallTarget(0x00876321) == ts::kRenderBands);
                CHECK(im.CallTarget(0x00874C87) == ts::kFrustumExtents);
                // GatherBands: the cached path below quality 5 (cmp eax, 5 / jl), 9 tiles for the near bands, 25 for the far.
                CHECK(im.Get<uint8_t>(0x008748AC) == 0x83 && im.Get<uint8_t>(0x008748AE) == 5);
                CHECK(im.Get<uint8_t>(0x00874D7F) == 0x83 && im.Get<uint8_t>(0x00874D81) == 9);
                CHECK(im.Get<uint8_t>(0x00874D89) == 0x83 && im.Get<uint8_t>(0x00874D8B) == 0x19);
                // The query data's fields where the client's own instructions put them.
                const Layout l = LayoutFrom(im);
                CHECK(l.flags == ts::kQdFlags && l.active == ts::kQdActive && l.useCull == ts::kQdUseCull && l.extents == ts::kQdExtents &&
                      l.frustums == ts::kQdFrustums && l.pos == ts::kQdPos && l.texIndex == ts::kQdTexIndex && l.radius == ts::kQdRadius &&
                      l.bounds == ts::kQdBounds && l.texBounds == ts::kQdTexBounds && l.matrices == ts::kQdMatrices &&
                      l.lastBand == ts::kQdLastBand && l.useFrustum == ts::kQdUseFrustum && l.staticFlags == ts::kQueryStatic);
                // The band struct: radius, last position, current, direction where the client's instructions read them.
                CHECK(im.Get<uint32_t>(0x008749F4 + 2) == ts::kBands + ts::kBandRadius);
                CHECK(im.Get<uint32_t>(0x008749AA + 2) == ts::kBands + ts::kBandLastPos);
                CHECK(im.Get<uint32_t>(0x0087498B + 2) == ts::kBands + ts::kBandCurrent);
                CHECK(im.Get<uint32_t>(0x00874C60 + 2) == ts::kBands + ts::kBandDir);
                CHECK(im.Get<uint8_t>(0x0087506D + 2) == ts::kBandDir);
                CHECK(im.Get<uint32_t>(0x00876018 + 1) == ts::kViewer);
                CHECK(im.Get<uint32_t>(0x007BA842 + 1) == ts::kQueryBuffers + 0x14);
            });
        }
        if (images.empty()) std::printf("SKIP the guards and the client's layout (no Wow.exe given)\n");
        else g.layout = LayoutFrom(images[0]);

        Test("a band's setup touches only its own fields", [&]
        {
            std::vector<uint8_t> d(ts::kQdSize, 0xCD), before = d;
            const float c[3] = {1.0f, 2.0f, 3.0f};
            ts::SetUpBand(d.data(), 1, c, 160.0f, 1);
            ts::Finish(d.data(), 1);
            std::vector<bool> mine(ts::kQdSize, false);
            auto own = [&](size_t at, size_t n) { for (size_t k = 0; k < n; ++k) mine[at + k] = true; };
            own(ts::kQdFlags + 4, 4); own(ts::kQdActive + 4, 4); own(ts::kQdUseCull + 4, 4); own(ts::kQdPos + 12, 12);
            own(ts::kQdTexIndex + 1, 1); own(ts::kQdRadius + 4, 4); own(ts::kQdBounds + 16, 16); own(ts::kQdTexBounds + 16, 16);
            own(ts::kQdLastBand, 4); own(ts::kQdUseFrustum, 4);
            for (size_t k = 0; k < d.size(); ++k) CHECK(mine[k] || d[k] == before[k]);
            const Seen s = Read(d.data());
            ts::BandView v;
            v.radius = 160.0f; v.lastPos[0] = 1.0f; v.lastPos[1] = 2.0f; v.lastPos[2] = 3.0f; v.current = 1;
            CheckWholeBand(s, 1, v);
            CHECK(s.lastBand == 1 && s.useFrustum == 0);
            ts::Deactivate(d.data(), 1);
            CHECK(Read(d.data()).active[1] == 0 && Read(d.data()).flags[1] == 0);
        });

        Test("foliage by path, as wxl-treewind picks what sways", [&]
        {
            const char* trees[] = {
                "world\\azeroth\\elwynn\\passivedoodads\\trees\\elwynntreecanopy01.m2", "world\\azeroth\\elwynn\\passivedoodads\\trees\\elwynntree01\\elwynnpine01.m2",
                "world\\azeroth\\elwynn\\passivedoodads\\trees\\elwynnfirtree01.m2", "world\\azeroth\\elwynn\\passivedoodads\\trees\\canopylesstree01.m2",
                "world\\azeroth\\duskwood\\passivedoodads\\trees\\duskwoodtree05.m2", "world\\azeroth\\redridge\\passivedoodads\\trees\\redridgetree01.m2",
                "world\\azeroth\\stranglethorn\\passivedoodads\\trees\\stranglethorntree01\\stranglethorntree01.m2",
                "world\\expansion01\\doodads\\nagrand\\trees\\nagrandtree01.m2", "world\\expansion01\\doodads\\terokkar\\trees\\terokkartreelarge.m2",
                "world\\expansion02\\doodads\\crystalsongforest\\trees\\crystalsongaspentree01.m2",
                "world\\expansion02\\doodads\\grizzlyhills\\trees\\grizzly_amberpinetree.m2", "world\\expansion02\\doodads\\grizzlyhills\\trees\\grizzlyhills_bush01.m2",
                "World\\Expansion02\\Doodads\\HowlingFjord\\HFjord_Tree_01.m2", "world\\kalimdor\\ashenvale\\passivedoodads\\ashenvaletrees\\ashenvaletree01.m2",
                "world\\azeroth\\elwynn\\passivedoodads\\bush\\elwynnbush01.m2", "world\\expansion02\\doodads\\sholazar\\sholazar_fern_01.m2",
            };
            for (const char* p : trees) if (!ts::LooksLikeFoliage(p)) throw std::runtime_error(std::string("not foliage: ") + p);
            const char* others[] = {
                "World\\Generic\\Human\\Passive Doodads\\StreetLamps\\StormwindStreetLamp01.m2", "world\\azeroth\\elwynn\\passivedoodads\\rocks\\elwynnrock1.m2",
                "world\\expansion01\\doodads\\hellfirepeninsula\\infernalcrater\\hellfire_infernalrock_01.m2", "world\\generic\\human\\passive doodads\\barrel\\barrel01.m2",
                "creature\\infernal\\infernal.m2", "world\\expansion02\\doodads\\icecrown\\icecrown_spine_01.m2", "", nullptr,
            };
            for (const char* p : others) if (ts::LooksLikeFoliage(p)) throw std::runtime_error(std::string("foliage: ") + (p ? p : "(null)"));
            ts::FoliageCache cache;
            CHECK(cache.Is(trees[0]) && cache.Is(trees[0]) && !cache.Is(others[0]) && cache.Size() == 2);
        });

        Test("the batch lists: one model per run, its trees and bushes", [&]
        {
            Reset();
            g.gathered[0] = g.casters[0];
            ts::Casters c;
            CHECK(FCasters(0, c));
            CHECK(c.untextured == 6 && c.models == 3 && c.foliage == 1 && c.groups == 2); // runs: a rock, a tree, a street lamp
        });

        Test("the rate: an even share of frames, no burst after a pause", [&]
        {
            double next = 0.0;
            unsigned drawn = 0;
            for (int f = 0; f < 400; ++f) drawn += ts::Due(next, 1000.0 + f * 25.0, 30.0f) ? 1 : 0; // 10 s at 40 fps
            CHECK(drawn >= 298 && drawn <= 302);
            next = 0.0;
            drawn = 0;
            for (int f = 0; f < 100; ++f) drawn += ts::Due(next, 1000.0 + f * 50.0, 30.0f) ? 1 : 0; // 20 fps: every frame
            CHECK(drawn == 100);
            // A 5 s pause, then 40 fps again: the next second has 30, not a burst of 150.
            drawn = 0;
            for (int f = 0; f < 40; ++f) drawn += ts::Due(next, 11000.0 + f * 25.0, 30.0f) ? 1 : 0;
            CHECK(drawn >= 29 && drawn <= 31);
            for (int f = 0; f < 10; ++f) CHECK(ts::Due(next, 20000.0 + f, 0.0f));
        });

        Test("quality 3, a tree in range: the near band's shown texture drawn again, whole, where the client drew it", [&]
        {
            Reset();
            ts::State st;
            std::vector<uint8_t> d = Data();
            ts::RunFrame(kFakeClient, d.data(), Near(), st);
            CHECK(st.outcome[0] == ts::Outcome::Drawn && st.outcome[1] == ts::Outcome::Off);
            CHECK(g.calls.size() == 4 && g.calls[0] == "frustum 0" && g.calls[1] == "query" && g.calls[2] == "casters 0" && g.calls[3] == "render");
            CHECK(g.renders.size() == 1);
            const Seen& s = g.renders[0];
            CheckWholeBand(s, 0, g.band[0]);
            CHECK(s.lastBand == 0 && s.useFrustum == 0);
            CHECK(st.window[0].drawn == 1 && st.window[0].ms > 0.5 && st.window[0].last.foliage == 1);
            CHECK(std::strcmp(st.window[0].last.firstFoliage, "ElwynnTreeCanopy01.m2") == 0);
            // Quality 4 the same.
            Reset();
            g.quality = 4;
            ts::State st4;
            d = Data();
            ts::RunFrame(kFakeClient, d.data(), Near(), st4);
            CHECK(st4.outcome[0] == ts::Outcome::Drawn && g.renders.size() == 1);
        });

        Test("at most the rate; every frame at 0", [&]
        {
            Reset();
            ts::State st;
            std::vector<uint8_t> d = Data();
            unsigned drawn = 0;
            for (int f = 0; f < 120; ++f) // 2 s at 60 fps (the fake's calls add ~0.5 ms to the clock of a drawn frame)
            {
                g.now = 10000.0 + f * 16.6667;
                ts::RunFrame(kFakeClient, d.data(), Near(30.0f), st);
                drawn += st.outcome[0] == ts::Outcome::Drawn ? 1 : 0;
                CHECK(st.outcome[0] == ts::Outcome::Drawn || st.outcome[0] == ts::Outcome::NotDue);
            }
            CHECK(drawn >= 58 && drawn <= 62);
            Reset();
            ts::State every;
            drawn = 0;
            for (int f = 0; f < 60; ++f)
            {
                g.now = 10000.0 + f * 16.6667;
                ts::RunFrame(kFakeClient, d.data(), Near(0.0f), every);
                drawn += every.outcome[0] == ts::Outcome::Drawn ? 1 : 0;
            }
            CHECK(drawn == 60);
        });

        Test("nothing at quality 0-2 or 5, without its maps, wxl-treewind or the client's callbacks, or when off", [&]
        {
            struct Case { int quality; bool treeWind, stock, viewer; int enabled; ts::Outcome want; };
            const Case cases[] = {
                {2, true, true, true, 1, ts::Outcome::Quality},      {0, true, true, true, 1, ts::Outcome::Quality},
                {5, true, true, true, 1, ts::Outcome::ClientDoesIt}, {-1, true, true, true, 1, ts::Outcome::NoMaps},
                {3, false, true, true, 1, ts::Outcome::NoTreeWind},  {3, true, false, true, 1, ts::Outcome::Foreign},
                {3, true, true, false, 1, ts::Outcome::NoMaps},      {3, true, true, true, 0, ts::Outcome::Off},
            };
            for (const Case& k : cases)
            {
                Reset();
                g.quality = k.quality; g.treeWind = k.treeWind; g.stock = k.stock; g.viewerOk = k.viewer;
                ts::Settings s = Near();
                s.enabled = k.enabled;
                s.farTrees = 1;
                ts::State st;
                std::vector<uint8_t> d = Data(), before = d;
                ts::RunFrame(kFakeClient, d.data(), s, st);
                CHECK(st.outcome[0] == k.want && st.outcome[1] == (k.enabled ? k.want : ts::Outcome::Off));
                CHECK(g.calls.empty() && d == before);
            }
            // A band whose textures aren't made.
            Reset();
            g.band[0].textures = false;
            ts::State st;
            std::vector<uint8_t> d = Data();
            ts::RunFrame(kFakeClient, d.data(), Near(), st);
            CHECK(st.outcome[0] == ts::Outcome::NoMaps && g.calls.empty());
        });

        Test("the viewer outside the map shown (being drawn round them elsewhere): left to the client", [&]
        {
            Reset();
            g.viewer[0] = g.band[0].lastPos[0] + 41.0f;
            ts::State st;
            std::vector<uint8_t> d = Data();
            ts::RunFrame(kFakeClient, d.data(), Near(), st);
            CHECK(st.outcome[0] == ts::Outcome::Away && g.calls.empty());
            // After ClearBandTextures the bands sit at the origin.
            Reset();
            g.band[0].lastPos[0] = g.band[0].lastPos[1] = g.band[0].lastPos[2] = 0.0f;
            ts::RunFrame(kFakeClient, d.data(), Near(), st);
            CHECK(st.outcome[0] == ts::Outcome::Away && g.calls.empty());
        });

        Test("no tree or bush in range: looked, not drawn, looked again after half a second", [&]
        {
            Reset();
            g.casters[0] = {0x3000, 0x2000}; // a rock and a street lamp
            ts::State st;
            std::vector<uint8_t> d = Data();
            ts::RunFrame(kFakeClient, d.data(), Near(0.0f), st);
            CHECK(st.outcome[0] == ts::Outcome::NoFoliage && g.renders.empty() && g.queries == 1);
            g.now += 100.0;
            ts::RunFrame(kFakeClient, d.data(), Near(0.0f), st);
            CHECK(st.outcome[0] == ts::Outcome::Quiet && g.queries == 1);
            g.now += 450.0;
            g.casters[0].push_back(0x1000); // a tree now
            ts::RunFrame(kFakeClient, d.data(), Near(0.0f), st);
            CHECK(st.outcome[0] == ts::Outcome::Drawn && g.queries == 2 && g.renders.size() == 1);
            CHECK(st.window[0].looked == 1 && st.window[0].drawn == 1);
        });

        Test("the far band too: its own pass, the near band out of it", [&]
        {
            Reset();
            ts::Settings s = Near(0.0f);
            s.farTrees = 1;
            s.farRate = 0.0f;
            ts::State st;
            std::vector<uint8_t> d = Data();
            ts::RunFrame(kFakeClient, d.data(), s, st);
            CHECK(st.outcome[0] == ts::Outcome::Drawn && st.outcome[1] == ts::Outcome::Drawn && g.renders.size() == 2 && g.queries == 2);
            CheckWholeBand(g.renders[0], 0, g.band[0]);
            CHECK(g.renders[0].lastBand == 0);
            const Seen& far = g.renders[1];
            CHECK(far.lastBand == 1 && far.active[0] == 0 && far.flags[0] == 0 && far.useFrustum == 0);
            CheckWholeBand(far, 1, g.band[1]);
            CHECK(st.window[1].last.foliage == 1 && std::strcmp(st.window[1].last.firstFoliage, "GrizzlyHills_Bush01.m2") == 0);
            // Only the far band due: band 0 still taken out of the query and the render.
            Reset();
            s.rate = 1.0f;
            ts::State st2;
            st2.next[0] = g.now + 500.0;
            ts::RunFrame(kFakeClient, d.data(), s, st2);
            CHECK(st2.outcome[0] == ts::Outcome::NotDue && st2.outcome[1] == ts::Outcome::Drawn && g.renders.size() == 1);
            CHECK(g.renders[0].active[0] == 0 && g.renders[0].lastBand == 1);
        });

        Test("a client call that faults turns it off", [&]
        {
            Reset();
            g.failQuery = true;
            ts::State st;
            std::vector<uint8_t> d = Data();
            ts::RunFrame(kFakeClient, d.data(), Near(0.0f), st);
            CHECK(st.outcome[0] == ts::Outcome::Fault && st.faulted && g.renders.empty());
            g.failQuery = false;
            g.calls.clear();
            ts::RunFrame(kFakeClient, d.data(), Near(0.0f), st);
            CHECK(st.outcome[0] == ts::Outcome::Fault && g.calls.empty());
            Reset();
            g.failRender = true;
            ts::State st2;
            ts::RunFrame(kFakeClient, d.data(), Near(0.0f), st2);
            CHECK(st2.outcome[0] == ts::Outcome::Fault && st2.faulted);
        });

        Test("the panel's second: counts roll over, the log's keep adding up", [&]
        {
            Reset();
            ts::State st;
            std::vector<uint8_t> d = Data();
            ts::Tick(st, g.now);
            for (int f = 0; f < 50; ++f) // the panel's tick comes first in a frame (OnUpdate), the shadows after
            {
                g.now = 10000.0 + f * 25.0;
                ts::Tick(st, g.now);
                ts::RunFrame(kFakeClient, d.data(), Near(30.0f), st);
            }
            CHECK(st.second[0].drawn >= 29 && st.second[0].drawn <= 31 && st.secondFrames == 40);
            CHECK(st.sinceLog[0].drawn == st.second[0].drawn + st.window[0].drawn);
            CHECK(st.second[0].ms > 0.0 && st.second[0].maxMs > 0.5 && st.second[0].maxMs < 0.6);
        });
    }
    catch (const std::exception& e)
    {
        std::printf("FAIL %s\n", e.what());
        return 1;
    }
    std::printf("%u passed\n", g_passed);
    return 0;
}
