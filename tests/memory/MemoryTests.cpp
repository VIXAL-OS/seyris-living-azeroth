// The memory readout (debug/Memory, debug/MemoryUse, env/AddressSpace, env/ClientMemory; 2026-10-25, before HD texture
// packs: "do we have sufficient logging to catch this if it happens and figure out what costs the most memory?"). Through
// the module's own code, linked whole (living_test_objects: operator new is replaced in this test as in the DLL):
//  1. the client's code the caches' reader checks first, against Wow.exe and Wow-WarcraftXL.exe on disk, and a changed
//     byte caught;
//  2. a texture's size as the client makes its D3D texture (ITexWHDStartEnd and ITexCreate's rules), and the kinds;
//  3. the reader over a stand-in client laid out as the decomp has it (the texture list, CGxTex, the Gx device's pools
//     and base mip, the M2 cache's buckets and free list, the WMO hash), a broken node, a bad list;
//  4. the address space walked for real (this process): totals that add up, against Windows' own count of free space; a
//     256 MB reservation, a commit and a mapped file seen where they are; images named and grouped; the walk in slices;
//  5. the heap by feature: blocks charged to the scope open on their thread and given back, a profiler scope's feature,
//     another thread's, the array and nothrow forms; no frees of blocks not its own;
//  6. Direct3D sizes: the formats, and on WARP (the CPU rasterizer behind D3D9On12, no GPU) every pool and kind of
//     resource sized as Direct3D describes it, and the ice and crystals' real textures reported by their features;
//  7. the warning's rule;
//  8. the settings through config::Apply over a stand-in settings interface: the memory keys, their defaults, a value from
//     the ini applied, no description naming a bracketed section;
//  9. the readout run as the game runs it (Install, then Update once a frame): its walk's slices and cost, the log's
//     line, the overview's lines, a snapshot's section, a warning.
// Run: memory_tests <Wow.exe> [<Wow-WarcraftXL.exe>] [--no-warp]
#include "../../src/Config.hpp"   // (the core has a Config.hpp of its own on the include path)
#include "debug/Memory.hpp"
#include "debug/MemoryUse.hpp"
#include "debug/Profiler.hpp"
#include "env/AddressSpace.hpp"
#include "env/ClientMemory.hpp"
#include "features/Crystal.hpp"
#include "features/TerrainIce.hpp"
#include "wxl_seyris/SettingsApi.hpp"

#include <windows.h>
#include <d3d9.h>
#include <d3d12.h>
#include <d3d9on12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace
{
    namespace as = wxl_livingazeroth::addrspace;
    namespace cm = wxl_livingazeroth::clientmem;
    namespace mu = wxl_livingazeroth::memuse;
    namespace me = wxl_livingazeroth::memory;
    constexpr uint64_t kMB = 1ull << 20;

    int g_failures = 0;
    bool g_warpRan = false;
    void Check(bool ok, const std::string& what)
    {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
        if (!ok) ++g_failures;
    }
    std::string F(const char* format, ...)
    {
        char line[2048];
        va_list a;
        va_start(a, format);
        std::vsnprintf(line, sizeof(line), format, a);
        va_end(a);
        return line;
    }
    bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

    // --- 1. the guards against the client's files -----------------------------------------------------------------------
    struct Pe
    {
        std::vector<uint8_t> data;
        uint32_t base = 0;
        struct Section { uint32_t va, vsize, rsize, raw; };
        std::vector<Section> sections;
        bool Load(const char* path)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f) return false;
            data.assign(std::istreambuf_iterator<char>(f), {});
            if (data.size() < 0x400) return false;
            const uint32_t pe = *reinterpret_cast<const uint32_t*>(&data[0x3C]);
            const uint16_t count = *reinterpret_cast<const uint16_t*>(&data[pe + 6]);
            const uint16_t optional = *reinterpret_cast<const uint16_t*>(&data[pe + 20]);
            base = *reinterpret_cast<const uint32_t*>(&data[pe + 24 + 28]);
            for (uint32_t i = 0, at = pe + 24 + optional; i < count; ++i, at += 40)
                sections.push_back({ *reinterpret_cast<const uint32_t*>(&data[at + 12]), *reinterpret_cast<const uint32_t*>(&data[at + 8]),
                                     *reinterpret_cast<const uint32_t*>(&data[at + 16]), *reinterpret_cast<const uint32_t*>(&data[at + 20]) });
            return true;
        }
        const uint8_t* At(uintptr_t va, uint32_t n) const
        {
            for (const Section& s : sections)
            {
                const uint32_t start = base + s.va;
                if (va >= start && va + n <= start + std::max(s.vsize, s.rsize) && va - start + n <= s.rsize)
                    return &data[s.raw + (va - start)];
            }
            return nullptr;
        }
    };

    void Guards(const std::vector<std::string>& exes)
    {
        std::printf("-- 1. the client's code, from its files\n");
        for (const std::string& path : exes)
        {
            Pe pe;
            if (!pe.Load(path.c_str())) { Check(false, "read " + path); continue; }
            const cm::Guard* bad = cm::FirstMismatch([&](uintptr_t a, uint32_t n) -> const void* { return pe.At(a, n); });
            Check(!bad, F("the caches' %zu guards hold against %s%s%s", sizeof(cm::kGuards) / sizeof(cm::kGuards[0]), path.c_str(),
                          bad ? ": not " : "", bad ? bad->what : ""));
            // one byte changed in the texture list's head: caught, and named
            static uint8_t changed[16];
            const cm::Guard* caught = cm::FirstMismatch([&](uintptr_t a, uint32_t n) -> const void* {
                const uint8_t* p = pe.At(a, n);
                if (!p || a != cm::kGuards[0].address) return p;
                std::memcpy(changed, p, n);
                changed[2] ^= 0x01;
                return changed;
            });
            Check(caught == &cm::kGuards[0], F("a changed byte in the texture list's head is caught (%s)", caught ? caught->what : "none"));
        }
    }

    // --- 2. sizes and kinds ---------------------------------------------------------------------------------------------
    void Sizes()
    {
        std::printf("-- 2. a texture's size as the client makes it\n");
        struct Case { const char* what; uint32_t w, h, format, flags, target, baseMip; uint64_t want; };
        const Case cases[] = {
            {"DXT1 256x256 mipped (LinearMipNearest), base mip 0: 256 down to 4", 256, 256, cm::kDxt1, 3, 0, 0, 43688},
            {"the same at base mip 1: 128 down to 4", 256, 256, cm::kDxt1, 3, 0, 1, 10920},
            {"DXT5 512x512 mipped at base mip 1: 256 down to 4", 512, 512, cm::kDxt5, 3, 0, 1, 87376},
            {"ARGB8888 512x256 mipped at base mip 0: 512x256 down to 1x1 (the short side stays 1)", 512, 256, cm::kArgb8888, 4, 0, 0, 699052},
            {"ARGB8888 64x32 linear: one level", 64, 32, cm::kArgb8888, 1, 0, 2, 8192},
            {"ARGB8888 64x32, mips the runtime makes: one level and its chain", 64, 32, cm::kArgb8888, 0x40 | 3, 0, 0, 10922},
            {"ARGB8888 64x32 linear, mips tracked, base mip 1: one level at 32x16", 64, 32, cm::kArgb8888, 0x20 | 1, 0, 1, 2048},
            {"RGB565 128x128 linear", 128, 128, cm::kRgb565, 1, 0, 0, 32768},
            {"a cube's six DXT1 64x64 faces", 64, 64, cm::kDxt1, 1, 1, 0, 12288},
            {"a D24X8 render target 1024x768: one level", 1024, 768, cm::kD24X8, 0x80 | 3, 0, 0, 3145728},
            {"none for no width", 0, 64, cm::kArgb8888, 1, 0, 0, 0},
        };
        for (const Case& c : cases)
        {
            const uint64_t got = cm::GxTexBytes(c.w, c.h, c.format, c.flags, c.target, c.baseMip);
            Check(got == c.want, F("%s: %llu bytes (%llu expected)", c.what, static_cast<unsigned long long>(got), static_cast<unsigned long long>(c.want)));
        }
        struct Name { const char* name; int kind; };
        const Name names[] = {
            {"Character\\BloodElf\\Female\\BloodElfFemaleSkin00_00.blp", cm::kCharacter}, {"creature/murloc/murloc.blp", cm::kCreature},
            {"CharacterBaseSkin", cm::kCharacterSkins}, {"ITEM\\TextureComponents\\TorsoUpperTexture\\x.blp", cm::kItem},
            {"World\\Generic\\Human\\x.blp", cm::kWorld}, {"Tileset\\Elwynn\\ElwynnGrass.blp", cm::kTerrain}, {"Dungeons\\Textures\\x.blp", cm::kDungeons},
            {"Spells\\Fire.blp", cm::kSpells}, {"Environments\\Stars\\x.blp", cm::kSky}, {"Interface\\Icons\\x.blp", cm::kInterface},
            {"Textures\\ShaneCube.blp", cm::kOtherNamed}, {"", cm::kUnnamed}, {"SolidTexture", cm::kUnnamed}, {"UniqueTexture", cm::kUnnamed},
            {"Char", cm::kOtherNamed},
        };
        bool all = true;
        std::string wrong;
        for (const Name& n : names)
            if (cm::KindOf(n.name) != n.kind) { all = false; wrong += std::string(" ") + n.name; }
        Check(all, "a texture's kind by its path's first part, either slash, any case; generated ones unnamed" + (all ? std::string() : ":" + wrong));
    }

    // --- 3. the reader over a stand-in client --------------------------------------------------------------------------
    struct Client
    {
        uint8_t* block = nullptr;
        uint8_t* p = nullptr;
        cm::Addresses at;
        uint8_t* Alloc(size_t n)
        {
            uint8_t* r = p;
            p += (n + 15) & ~size_t(15);
            return r;
        }
        Client() { block = static_cast<uint8_t*>(VirtualAlloc(nullptr, 1 << 20, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)); p = block; }
        ~Client() { VirtualFree(block, 0, MEM_RELEASE); }
    };
    void W32(uint8_t* at, uint32_t v) { std::memcpy(at, &v, 4); }
    void WPtr(uint8_t* at, const void* v) { W32(at, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(v))); }

    // A Storm list at `list` (link offset, terminator prev, next) over `nodes` linked at `link`.
    void Link(uint8_t* list, uint32_t link, const std::vector<uint8_t*>& nodes)
    {
        W32(list, link);
        uint8_t* terminator = list + 4;
        const uint32_t end = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(terminator)) | 1u;
        WPtr(list + 8, nodes.empty() ? nullptr : nodes[0]);
        if (nodes.empty()) W32(list + 8, end);
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            uint8_t* l = nodes[i] + link;
            WPtr(l, i ? nodes[i - 1] + link : terminator);
            if (i + 1 < nodes.size()) WPtr(l + 4, nodes[i + 1]);
            else W32(l + 4, end);
        }
    }

    void Reader()
    {
        std::printf("-- 3. the client's caches over a stand-in client laid out as the decomp has it\n");
        Client c;
        // the textures
        struct T { const char* name; uint32_t w, h, format, flags, target; bool gx, api, atlas; };
        const T ts[] = {
            {"Creature\\Murloc\\Murloc.blp", 256, 256, cm::kDxt1, 3, 0, true, true, false},
            {"Tileset\\Elwynn\\ElwynnGrass.blp", 512, 512, cm::kDxt5, 3, 0, true, true, false},
            {"CharacterBaseSkin", 512, 512, cm::kArgb8888, 1, 0, true, true, false},
            {"UniqueTexture", 1024, 1024, cm::kArgb8888, 0x80 | 1, 0, true, true, false},
            {"Interface\\Loading.blp", 64, 64, cm::kDxt1, 1, 0, false, false, false},     // loading: no CGxTex yet
            {"Interface\\Atlas.blp", 64, 64, cm::kDxt1, 1, 0, true, true, true},          // on an atlas
            {"Interface\\Made.blp", 64, 64, cm::kDxt1, 1, 0, true, false, false},         // its D3D texture not made
            {"Environments\\Stars\\Stars.blp", 64, 64, cm::kDxt1, 1, 1, true, true, false},
        };
        std::vector<uint8_t*> nodes;
        static int fakeD3d = 0;
        for (const T& t : ts)
        {
            uint8_t* tex = c.Alloc(0x6C + 260);
            std::memcpy(tex + cm::kTexName, t.name, std::strlen(t.name) + 1);
            if (t.atlas) WPtr(tex + cm::kTexAtlas, tex);
            if (t.gx)
            {
                uint8_t* gx = c.Alloc(0x60);
                W32(gx + cm::kGxWidth, t.w); W32(gx + cm::kGxHeight, t.h); W32(gx + cm::kGxFormat, t.format); W32(gx + cm::kGxFlags, t.flags);
                W32(gx + cm::kGxTarget, t.target);
                if (t.api) WPtr(gx + cm::kGxApi, &fakeD3d);
                WPtr(tex + cm::kTexGx, gx);
            }
            nodes.push_back(tex);
        }
        uint8_t* list = c.Alloc(12);
        Link(list, 0x64, nodes);
        c.at.textureList = reinterpret_cast<uintptr_t>(list);
        // the Gx device: base mip 1, three pools
        uint8_t* dev = c.Alloc(0x2800);
        W32(dev + cm::kDevBaseMip, 1);
        std::vector<uint8_t*> pools;
        const uint32_t poolSpec[3][3] = { {0, 0, 1u << 20}, {1, 0, 256u << 10}, {0, 2, 2u << 20} };   // target, usage, size
        for (const auto& ps : poolSpec)
        {
            uint8_t* pool = c.Alloc(0x34);
            W32(pool + cm::kPoolTarget, ps[0]); W32(pool + cm::kPoolUsage, ps[1]); W32(pool + cm::kPoolSize, ps[2]);
            pools.push_back(pool);
        }
        Link(dev + cm::kDevPools, 0, pools);
        uint8_t* devPtr = c.Alloc(4);
        WPtr(devPtr, dev);
        c.at.gxDevicePtr = reinterpret_cast<uintptr_t>(devPtr);
        // the GxTex reuse cache
        uint8_t* sizes = c.Alloc(8);
        W32(sizes, 1234567); W32(sizes + 4, 0x4000000);
        c.at.gxTexCacheSize = reinterpret_cast<uintptr_t>(sizes);
        c.at.gxTexCacheMax = reinterpret_cast<uintptr_t>(sizes + 4);
        // the M2 cache: three models in two buckets, one of them on the free list
        uint8_t* m2 = c.Alloc(0x10 + 4 * cm::kM2BucketCount + 0x100);
        uint8_t* shared[3];
        for (int i = 0; i < 3; ++i) { shared[i] = c.Alloc(0x170); W32(shared[i] + cm::kSharedFileSize, 1000u * (i + 1)); }
        WPtr(m2 + cm::kM2Buckets + 4 * 7, shared[0]);
        WPtr(shared[0] + cm::kSharedNext, shared[1]);
        WPtr(m2 + cm::kM2Buckets + 4 * 1020, shared[2]);
        WPtr(m2 + cm::kM2FreeList, shared[1]);
        c.at.m2Cache = reinterpret_cast<uintptr_t>(m2);
        // the WMO roots: two, linked at +0xC (TSHashObject's m_linktofull)
        uint8_t* hash = c.Alloc(0x28);
        Link(hash + cm::kHashFullList, 0x0C, { c.Alloc(0x20), c.Alloc(0x20) });
        c.at.wmoHash = reinterpret_cast<uintptr_t>(hash);

        cm::Caches got;
        const bool read = cm::Read(c.at, got);
        Check(read && got.read && std::strcmp(got.why, "read whole") == 0, F("read (%s, %.3f ms)", got.why, got.ms));
        Check(got.textures == 8 && got.created == 5 && got.onAtlas == 1 && got.targets == 1,
              F("the textures: %u listed, %u with a D3D texture, %u on an atlas, %u render targets (8, 5, 1, 1)", got.textures, got.created, got.onAtlas, got.targets));
        const uint64_t managed = 10920 + 87376 + 1048576 + 12288;
        Check(got.baseMip == 1 && got.managedBytes == managed && got.targetBytes == 4194304,
              F("at the device's base mip %u: %llu bytes managed (%llu), %llu of render targets (4194304)", got.baseMip,
                static_cast<unsigned long long>(got.managedBytes), static_cast<unsigned long long>(managed), static_cast<unsigned long long>(got.targetBytes)));
        Check(got.kindBytes[cm::kCreature] == 10920 && got.kindBytes[cm::kTerrain] == 87376 && got.kindBytes[cm::kCharacterSkins] == 1048576 &&
              got.kindBytes[cm::kUnnamed] == 4194304 && got.kindBytes[cm::kSky] == 12288 && got.kindCount[cm::kInterface] == 0,
              "by kind: the creature's, the terrain's, the composited skin, the unnamed target, the sky's cube; nothing for the three not counted");
        Check(got.formatBytes[cm::kDxt1] == 10920 + 12288 && got.formatBytes[cm::kDxt5] == 87376 && got.formatBytes[cm::kArgb8888] == 1048576 + 4194304 &&
              got.UncompressedBytes() == 1048576 + 4194304, "by format: DXT1, DXT5, ARGB8888 (the uncompressed)");
        Check(got.largestCount == 5 && got.largest[0].bytes == 4194304 && got.largest[0].target && std::strcmp(got.largest[1].name, "CharacterBaseSkin") == 0 &&
              got.largest[4].bytes == 10920, F("the largest, largest first: %s, %s, ... %s", got.largest[0].name, got.largest[1].name, got.largest[4].name));
        Check(got.pools[0][0] == 1 && got.pools[1][0] == 1 && got.pools[0][2] == 1 && got.poolBytes[0][0] == (1u << 20) && got.poolBytes[1][0] == (256u << 10) &&
              got.poolBytes[0][2] == (2u << 20), "the pools by target and usage: a static vertex, a static index, a stream vertex pool");
        Check(got.gxCacheSize == 1234567 && got.gxCacheMax == 0x4000000, "the GxTex reuse cache's size and its most");
        Check(got.m2Models == 3 && got.m2FileBytes == 6000 && got.m2Waiting == 1 && got.wmoRoots == 2,
              F("M2 models %u (%llu bytes of files), %u waiting to be freed; WMO roots %u (3, 6000, 1, 2)", got.m2Models,
                static_cast<unsigned long long>(got.m2FileBytes), got.m2Waiting, got.wmoRoots));

        // a node pointing into memory that's gone: what was read is kept, and where it stopped is said
        uint8_t* gone = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        VirtualFree(gone, 0, MEM_RELEASE);
        WPtr(nodes[2] + 0x64 + 4, gone);
        cm::Caches broken;
        cm::Read(c.at, broken);
        Check(broken.read && Has(broken.why, "stopped part way") && Has(broken.why, "the textures") && broken.textures == 3,
              F("a node in memory that's gone: %s; %u textures kept", broken.why, broken.textures));
        // a list whose link offset isn't one: not read
        W32(list, 0);
        cm::Caches bad;
        const bool badRead = cm::Read(c.at, bad);
        Check(!badRead && !bad.read && Has(bad.why, "link offset"), F("a bad list: not read (%s)", bad.why));
    }

    // --- 4. the address space, walked for real --------------------------------------------------------------------------
    const as::Owner* Find(const as::Totals& t, uintptr_t base)
    {
        for (const as::Owner& o : t.owners)
            if (o.base == base) return &o;
        return nullptr;
    }

    void AddressSpace()
    {
        std::printf("-- 4. this process's address space, walked\n");
        HMODULE d3d9 = LoadLibraryW(L"d3d9.dll");
        as::Walker w;
        w.WalkAll();
        as::Totals a = w.Last();
        MEMORYSTATUSEX ms{ sizeof(ms) };
        GlobalMemoryStatusEx(&ms);
        Check(a.walk == 1 && a.regions > 100 && a.Used() + a.free == a.space,
              F("walk 1: %u regions, %u allocations; used %llu MB + free %llu MB = the space %llu MB", a.regions, a.allocations, a.Used() / kMB, a.free / kMB, a.space / kMB));
        const int64_t diff = static_cast<int64_t>(a.free) - static_cast<int64_t>(ms.ullAvailVirtual);
        Check(std::llabs(diff) < static_cast<int64_t>(32 * kMB), F("free %llu MB, Windows' own count %llu MB", a.free / kMB, ms.ullAvailVirtual / kMB));
        Check(a.largestFree > 0 && a.largestFree <= a.free && (a.freeOver64 > 0) == (a.largestFree >= as::kBigFree),
              F("the largest free block %llu MB at 0x%08llX, %u free blocks of 64 MB or more", a.largestFree / kMB,
                static_cast<unsigned long long>(a.largestFreeAt), a.freeOver64));
        const uintptr_t exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const as::Owner* self = Find(a, exe);
        const as::Owner* kernel = Find(a, reinterpret_cast<uintptr_t>(GetModuleHandleW(L"kernel32.dll")));
        const as::Owner* d3d = Find(a, reinterpret_cast<uintptr_t>(d3d9));
        Check(self && self->kind == as::kImage && Has(self->name, "memory_tests") && as::GroupOf(*self, exe) == as::kClient && self->committed > 0,
              F("this exe: an image, %s, the client's group", self ? self->name.c_str() : "not found"));
        Check(kernel && _stricmp(kernel->name.c_str(), "kernel32.dll") == 0 && as::GroupOf(*kernel, exe) == as::kWindows,
              F("kernel32.dll: %s, Windows' (%s)", kernel ? kernel->name.c_str() : "not found", kernel ? kernel->path.c_str() : ""));
        Check(d3d && _stricmp(d3d->name.c_str(), "d3d9.dll") == 0 && as::GroupOf(*d3d, exe) == as::kDirect3D, "d3d9.dll: Direct3D's");
        as::Owner probe;
        probe.path = "\\Device\\HarddiskVolume3\\Games\\ChromieCraft_3.3.5a\\Extensions\\wxl-seyris-living-azeroth\\wxl-seyris-living-azeroth.dll";
        probe.name = as::FileName(probe.path);
        const bool ext = as::GroupOf(probe, exe) == as::kExtension;
        probe.path = "\\Device\\HarddiskVolume3\\Games\\ChromieCraft_3.3.5a\\WarcraftXL.dll"; probe.name = "WarcraftXL.dll";
        const bool core = as::GroupOf(probe, exe) == as::kCore;
        probe.path = "\\Device\\HarddiskVolume2\\Windows\\System32\\DriverStore\\FileRepository\\nv_dispi.inf_amd64_x\\nvd3dum.dll"; probe.name = "nvd3dum.dll";
        const bool driver = as::GroupOf(probe, exe) == as::kDriver;
        probe.path = "\\Device\\HarddiskVolume3\\Games\\ChromieCraft_3.3.5a\\DivxDecoder.dll"; probe.name = "DivxDecoder.dll";
        const bool other = as::GroupOf(probe, exe) == as::kOtherImage;
        Check(ext && core && driver && other, "the groups by path: an extension, WarcraftXL's core, the graphics driver (in the DriverStore), the game folder's other DLLs");

        // a 256 MB reservation, 8 MB of it committed, and a mapped file
        void* big = VirtualAlloc(nullptr, 256 * kMB, MEM_RESERVE, PAGE_NOACCESS);
        if (big) VirtualAlloc(big, 8 * kMB, MEM_COMMIT, PAGE_READWRITE);
        wchar_t tmp[MAX_PATH], file[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        GetTempFileNameW(tmp, L"lam", 0, file);
        HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        HANDLE map = h != INVALID_HANDLE_VALUE ? CreateFileMappingW(h, nullptr, PAGE_READWRITE, 0, 4 * 1024 * 1024, nullptr) : nullptr;
        void* view = map ? MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0) : nullptr;
        w.WalkAll();
        const as::Totals b = w.Last();
        const int64_t reserved = static_cast<int64_t>(b.reserved[as::kPrivate]) - static_cast<int64_t>(a.reserved[as::kPrivate]);
        const int64_t committed = static_cast<int64_t>(b.committed[as::kPrivate]) - static_cast<int64_t>(a.committed[as::kPrivate]);
        Check(big && reserved >= static_cast<int64_t>(240 * kMB) && reserved <= static_cast<int64_t>(270 * kMB) && committed >= static_cast<int64_t>(8 * kMB) &&
              a.free - b.free >= 256 * kMB, F("256 MB reserved, 8 MB of it committed: private reserved +%lld MB, committed +%lld MB, free -%llu MB", reserved / static_cast<int64_t>(kMB),
                                              committed / static_cast<int64_t>(kMB), (a.free - b.free) / kMB));
        bool listed = false;
        for (const as::PrivateBlock& p : b.largestPrivate)
            if (p.base == reinterpret_cast<uintptr_t>(big) && p.committed == 8 * kMB && p.reserved == 248 * kMB) listed = true;
        Check(listed, "the reservation among the largest private allocations, its committed and reserved parts");
        const as::Owner* mapped = view ? Find(b, reinterpret_cast<uintptr_t>(view)) : nullptr;
        char fileName[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, wcsrchr(file, L'\\') + 1, -1, fileName, sizeof(fileName), nullptr, nullptr);
        Check(mapped && mapped->kind == as::kMapped && _stricmp(mapped->name.c_str(), fileName) == 0 && mapped->committed == 4 * 1024 * 1024,
              F("a mapped view of %s: %s, %llu bytes committed", fileName, mapped ? mapped->name.c_str() : "not found", mapped ? static_cast<unsigned long long>(mapped->committed) : 0ull));
        Check(b.walk == 2 && b.lookups < a.lookups, F("the second walk looked up %u names (the first %u: names kept by base)", b.lookups, a.lookups));

        // in slices, as the game walks it
        w.Start();
        uint32_t steps = 0;
        double most = 0.0;
        while (!w.Step(0.02) && steps < 1000000) ++steps;
        const as::Totals& s = w.Last();
        most = s.maxStepMs;
        Check(s.frames > 3 && std::abs(static_cast<int>(s.regions) - static_cast<int>(b.regions)) < 50 && s.Used() + s.free == s.space,
              F("in 0.02 ms slices: %u steps, %u regions (%u whole), the most in one step %.3f ms, %.2f ms in all", s.frames, s.regions, b.regions, most, s.ms));
        Check(most < 2.0, F("a step stays near its budget: %.3f ms at most (16 regions between clock checks)", most));

        if (view) UnmapViewOfFile(view);
        if (map) CloseHandle(map);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        if (big) VirtualFree(big, 0, MEM_RELEASE);
    }

    // --- 5. the heap by feature ------------------------------------------------------------------------------------------
    void Heap()
    {
        std::printf("-- 5. Living Azeroth's heap by feature\n");
        Check(mu::Counting(), "operator new is this module's (blocks counted)");
        const mu::HeapUse glass0 = mu::Heap(mu::kGlass), other0 = mu::Heap(mu::kOther);
        std::vector<char>* v = nullptr;
        {
            mu::Scope s(mu::kGlass);
            v = new std::vector<char>(1 << 20);
        }
        const mu::HeapUse glass1 = mu::Heap(mu::kGlass);
        // (the standard allocator asks for a block of 4 KB or more with 35 bytes more on x86, to align it to 32)
        const int64_t asked = glass1.bytes - glass0.bytes, least = static_cast<int64_t>(sizeof(std::vector<char>) + (1 << 20));
        Check(asked >= least && asked <= least + 64 && glass1.blocks - glass0.blocks == 2 && glass1.made - glass0.made == 2,
              F("made in glass's scope: +%lld bytes (the vector and its MB, %lld, and the allocator's alignment) in +%lld blocks", asked, least,
                glass1.blocks - glass0.blocks));
        delete v;   // freed outside the scope: still glass's
        const mu::HeapUse glass2 = mu::Heap(mu::kGlass);
        Check(glass2.bytes == glass0.bytes && glass2.blocks == glass0.blocks, "freed outside it: given back to glass, exactly");
        int* untagged = new int[1000];
        const int64_t otherDelta = mu::Heap(mu::kOther).bytes - other0.bytes;
        delete[] untagged;
        Check(otherDelta == 4000, F("made outside every scope: other's (+%lld bytes)", otherDelta));
        {
            mu::Scope outer(mu::kFog);
            {
                mu::Scope inner(mu::kGrass);
                Check(mu::t_tag == mu::kGrass, "a nested scope's feature while it's open");
            }
            Check(mu::t_tag == mu::kFog, "the outer one's again after it");
            { mu::Scope keep(-1); Check(mu::t_tag == mu::kFog, "a negative tag keeps the open one"); }
        }
        Check(mu::t_tag == mu::kOther, "none open: other");
        {
            wxl_livingazeroth::prof::Scope p(wxl_livingazeroth::prof::kOursCoverDraw);
            Check(mu::t_tag == mu::kCover, "a profiler scope (the cover's draw) opens its feature, the profiler off");
            wxl_livingazeroth::prof::Scope q(wxl_livingazeroth::prof::kM2);
            Check(mu::t_tag == mu::kCover, "a client category's scope leaves it as it is");
        }
        const mu::HeapUse fog0 = mu::Heap(mu::kFog);
        int64_t onThread = 0;
        std::thread t([&] {
            mu::Scope s(mu::kFog);
            char* b = new char[12345];
            onThread = mu::Heap(mu::kFog).bytes - fog0.bytes;
            delete[] b;
        });
        t.join();
        Check(onThread == 12345 && mu::Heap(mu::kFog).bytes == fog0.bytes && mu::t_tag == mu::kOther, "another thread's scope is its own: its block the fog's, this thread's tag untouched");
        int* n = new (std::nothrow) int(7);
        const bool nothrowOk = n && *n == 7;
        delete n;
        Check(nothrowOk, "the nothrow form");
        Check(mu::ForeignFrees() == 0, "no frees of blocks this module didn't make");
    }

    // --- 6. Direct3D sizes ------------------------------------------------------------------------------------------------
    template <class T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

    struct Warp
    {
        ID3D12Device* d12 = nullptr;
        IDirect3D9* d3d = nullptr;
        IDirect3DDevice9* dev = nullptr;
        ~Warp() { Release(dev); Release(d3d); Release(d12); }
    };

    bool InitWarp(Warp& w, std::string& why)
    {
        IDXGIFactory4* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory)))) { why = "no DXGI factory"; return false; }
        IDXGIAdapter* adapter = nullptr;
        const HRESULT ha = factory->EnumWarpAdapter(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&adapter));
        factory->Release();
        if (FAILED(ha) || !adapter) { why = "no WARP adapter"; return false; }
        const HRESULT hd = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&w.d12));
        adapter->Release();
        if (FAILED(hd) || !w.d12) { why = "no D3D12 device on WARP"; return false; }
        using Create9On12 = IDirect3D9*(WINAPI*)(UINT, D3D9ON12_ARGS*, UINT);
        HMODULE d9 = LoadLibraryW(L"d3d9.dll");
        auto create = d9 ? reinterpret_cast<Create9On12>(GetProcAddress(d9, "Direct3DCreate9On12")) : nullptr;
        if (!create) { why = "no Direct3DCreate9On12"; return false; }
        D3D9ON12_ARGS a{};
        a.Enable9On12 = TRUE;
        a.pD3D12Device = w.d12;
        w.d3d = create(D3D_SDK_VERSION, &a, 1);
        if (!w.d3d) { why = "D3D9On12 failed"; return false; }
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"MemoryTests";
        RegisterClassW(&wc);
        HWND window = CreateWindowW(wc.lpszClassName, L"memory", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window;
        pp.BackBufferWidth = 64; pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        if (FAILED(w.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &w.dev))) { why = "CreateDevice failed"; return false; }
        return true;
    }

    void Direct3D(bool warp)
    {
        std::printf("-- 6. Direct3D sizes\n");
        const auto fourcc = [](const char* s) { return static_cast<uint32_t>(s[0]) | (static_cast<uint32_t>(s[1]) << 8) | (static_cast<uint32_t>(s[2]) << 16) | (static_cast<uint32_t>(s[3]) << 24); };
        Check(mu::SurfaceBytes(D3DFMT_A8R8G8B8, 256, 256) == 262144 && mu::SurfaceBytes(fourcc("DXT1"), 256, 256) == 32768 &&
              mu::SurfaceBytes(fourcc("DXT1"), 1, 1) == 8 && mu::SurfaceBytes(fourcc("DXT5"), 6, 6) == 64 && mu::SurfaceBytes(D3DFMT_A16B16G16R16F, 10, 10) == 800 &&
              mu::SurfaceBytes(D3DFMT_A32B32G32R32F, 4, 4) == 256 && mu::SurfaceBytes(D3DFMT_R5G6B5, 4, 4) == 32 && mu::SurfaceBytes(D3DFMT_L8, 4, 4) == 16 &&
              mu::SurfaceBytes(fourcc("INTZ"), 4, 4) == 64 && mu::SurfaceBytes(D3DFMT_UNKNOWN, 4, 4) == 0,
              "a level's bytes by format: 32-bit, DXT1 and DXT5 blocks (a 1x1 level a whole block), 64- and 128-bit floats, 16 and 8 bits, INTZ, unknown");
        if (!warp) { std::printf("skip  WARP checks: --no-warp\n"); return; }
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("skip  WARP checks: %s\n", why.c_str()); return; }
        IDirect3DTexture9 *managed = nullptr, *target = nullptr, *autogen = nullptr;
        IDirect3DCubeTexture9* cube = nullptr;
        IDirect3DVolumeTexture9* volume = nullptr;
        IDirect3DSurface9* sys = nullptr;
        IDirect3DVertexBuffer9 *vb = nullptr, *dynVb = nullptr;
        IDirect3DIndexBuffer9* ib = nullptr;
        IDirect3DDevice9* d = w.dev;
        std::string failed;
        const auto made1 = [&](HRESULT hr, const char* what) { if (FAILED(hr)) failed += std::string(" ") + what; };
        made1(d->CreateTexture(256, 256, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &managed, nullptr), "managed");
        made1(d->CreateTexture(128, 128, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &target, nullptr), "target");
        made1(d->CreateCubeTexture(64, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &cube, nullptr), "cube");
        made1(d->CreateVolumeTexture(32, 32, 32, 1, 0, D3DFMT_L8, D3DPOOL_MANAGED, &volume, nullptr), "volume");
        made1(d->CreateOffscreenPlainSurface(64, 64, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr), "system surface");
        made1(d->CreateVertexBuffer(1000, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vb, nullptr), "managed vertex buffer");
        made1(d->CreateVertexBuffer(4096, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, 0, D3DPOOL_DEFAULT, &dynVb, nullptr), "dynamic vertex buffer");
        made1(d->CreateIndexBuffer(2000, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &ib, nullptr), "index buffer");
        const bool made = failed.empty();
        Check(made, "on WARP: a mipped managed texture, a render target, a cube, a volume, a system-memory surface, three buffers" + (made ? std::string() : ", not made:" + failed));
        if (made)
        {
            mu::Gpu g;
            g.Add(managed); const uint64_t m1 = g.managed;
            g.Add(target); const uint64_t v1 = g.video;
            g.Add(cube); const uint64_t m2 = g.managed - m1;
            g.Add(volume); const uint64_t m3 = g.managed - m1 - m2;
            g.Add(sys); g.Add(vb); g.Add(dynVb); g.Add(ib);
            Check(m1 == 349524, F("the managed 256x256 with its 9 levels: %llu bytes (349524)", static_cast<unsigned long long>(m1)));
            Check(v1 == 65536 && m2 == 12288 && m3 == 32768, F("the target %llu (65536, video only), the cube's six faces %llu (12288), the volume %llu (32768)",
                                                              static_cast<unsigned long long>(v1), static_cast<unsigned long long>(m2), static_cast<unsigned long long>(m3)));
            Check(g.system == 16384 && g.managed == 349524 + 12288 + 32768 + 1000 && g.dynamic == 4096 && g.video == 65536 + 2000 && g.resources == 8 && g.unreadable == 0,
                  F("the system-memory surface %llu, the managed buffer in managed, the dynamic one %llu in the address space, the static default one %llu more video; 8 resources",
                    static_cast<unsigned long long>(g.system), static_cast<unsigned long long>(g.dynamic), static_cast<unsigned long long>(g.video - 65536)));
            Check(g.InAddressSpace() == g.managed + g.system + g.dynamic && g.InVideo() == g.managed + g.video, "in the address space: managed, system and dynamic; in video memory: managed and default");
            mu::Gpu none;
            none.Add(static_cast<IDirect3DBaseTexture9*>(nullptr));
            Check(none.resources == 0 && none.unreadable == 0, "nothing listed for a null");
        }
        Release(managed); Release(target); Release(cube); Release(volume); Release(sys); Release(vb); Release(dynVb); Release(ib);

        // the features' own: the ice's and the crystals' detail textures, made by them on this device, reported by them
        IDirect3DTexture9* ice = wxl_livingazeroth::terrainice::DetailTexture(d);
        IDirect3DTexture9* crystal = wxl_livingazeroth::crystal::DetailTexture(d);
        mu::Gpu iceUse, crystalUse;
        wxl_livingazeroth::terrainice::GpuMemory(iceUse);
        wxl_livingazeroth::crystal::GpuMemory(crystalUse);
        g_warpRan = ice && crystal;
        Check(ice && iceUse.resources == 1 && iceUse.managed == 349524, F("terrainice::GpuMemory: its detail texture, %llu bytes managed (256x256, 9 levels)", static_cast<unsigned long long>(iceUse.managed)));
        Check(crystal && crystalUse.resources == 1 && crystalUse.managed > 0, F("crystal::GpuMemory: its detail texture, %llu bytes managed", static_cast<unsigned long long>(crystalUse.managed)));
    }

    // --- 7. the warning's rule ---------------------------------------------------------------------------------------------
    void Warning()
    {
        std::printf("-- 7. the warning's rule\n");
        me::Settings c;   // 400 MB free, a 64 MB largest block, 10 minutes
        me::WarnState s;
        std::string why;
        const uint64_t t0 = 1000000;
        Check(!me::ShouldWarn(s, t0, 500 * kMB, 300 * kMB, c, why), "500 MB free, a 300 MB largest block: no warning");
        bool w = me::ShouldWarn(s, t0, 350 * kMB, 0, c, why);
        Check(w && Has(why, "WarnFreeMB 400"), "350 MB free (the largest not walked yet): a warning, " + why);
        Check(!me::ShouldWarn(s, t0 + 1000, 340 * kMB, 0, c, why), "a second later at 340 MB: none (not 100 MB worse)");
        Check(me::ShouldWarn(s, t0 + 2000, 245 * kMB, 0, c, why), "at 245 MB: again (100 MB worse than at the warning)");
        Check(!me::ShouldWarn(s, t0 + 9 * 60000, 244 * kMB, 0, c, why), "9 minutes later, a little worse: none");
        Check(me::ShouldWarn(s, t0 + 2000 + 10 * 60000, 244 * kMB, 0, c, why), "10 minutes after the last: again");
        me::WarnState f;
        w = me::ShouldWarn(f, t0, 900 * kMB, 50 * kMB, c, why);
        Check(w && Has(why, "WarnLargestFreeMB 64") && !Has(why, "WarnFreeMB 400"), "plenty free but a 50 MB largest block: a warning, " + why);
        Check(!me::ShouldWarn(f, t0 + 1000, 900 * kMB, 40 * kMB, c, why), "then 40 MB: none");
        Check(me::ShouldWarn(f, t0 + 2000, 900 * kMB, 25 * kMB, c, why), "then 25 MB: again (the block halved)");
        me::Settings off;
        off.warnFreeMB = 0.0f;
        off.warnLargestMB = 0.0f;
        me::WarnState o;
        Check(!me::ShouldWarn(o, t0, 10 * kMB, 1 * kMB, off, why), "both thresholds 0: never");
    }

    // --- 8. the settings ----------------------------------------------------------------------------------------------------
    struct Key { std::string description; float value; };
    std::map<std::string, Key> g_keys;          // "section/key"
    std::map<std::string, float> g_ini;         // values "in the ini"

    float __cdecl FakeGetFloat(const char* ns, const char* key, const char* description, float fallback)
    {
        const std::string k = std::string(ns) + "/" + key;
        const auto it = g_ini.find(k);
        g_keys[k] = { description ? description : "", fallback };
        return it != g_ini.end() ? it->second : fallback;
    }
    int __cdecl FakeGetBool(const char* ns, const char* key, const char* description, int fallback)
    {
        return FakeGetFloat(ns, key, description, static_cast<float>(fallback)) != 0.0f ? 1 : 0;
    }
    int __cdecl FakeHasFeature(const char* name) { return std::strcmp(name, "get-float") == 0; }
    WXL_SeyrisSettingsApi g_settingsApi = { sizeof(WXL_SeyrisSettingsApi), 1, 1, 0, &FakeHasFeature, &FakeGetBool, &FakeGetFloat };

    std::vector<std::string> g_log;
    void __cdecl FakeLog(int level, const char* tag, const char* format, ...)
    {
        char line[4096];
        va_list a;
        va_start(a, format);
        std::vsnprintf(line, sizeof(line), format, a);
        va_end(a);
        g_log.push_back(F("%d %s: %s", level, tag ? tag : "", line));
    }
    void* __cdecl FakeGetInterface(const char* name, uint32_t /*version*/)
    {
        return std::strcmp(name, WXL_SEYRIS_SETTINGS_INTERFACE_NAME) == 0 ? &g_settingsApi : nullptr;
    }
    WXL_Api MakeApi()
    {
        WXL_Api a{};
        a.structSize = sizeof(WXL_Api);
        a.apiVersion = WXL_API_VERSION;
        a.Log = &FakeLog;
        a.GetInterface = &FakeGetInterface;
        return a;
    }

    void Settings(const WXL_Api* api)
    {
        std::printf("-- 8. the settings, through config::Apply\n");
        g_ini["LivingAzeroth.Memory/WarnFreeMB"] = 300.0f;
        wxl_livingazeroth::config::Apply(api);
        const char* s = "LivingAzeroth.Memory/";
        struct Want { const char* key; float value; };
        const Want wants[] = { {"Readout", 1}, {"WalkSeconds", 5}, {"ClientCachesSeconds", 30}, {"WarnFreeMB", 400}, {"WarnLargestFreeMB", 64}, {"WarnMinutes", 10} };
        bool all = true;
        std::string missing;
        for (const Want& w : wants)
        {
            const auto it = g_keys.find(std::string(s) + w.key);
            if (it == g_keys.end() || it->second.value != w.value || it->second.description.size() < 40) { all = false; missing += std::string(" ") + w.key; }
        }
        Check(all, "the Memory section's six keys, described, written with their defaults (Readout 1: on; 5 s, 30 s, 400 MB, 64 MB, 10 minutes)" +
                   (all ? std::string() : ":" + missing));
        const me::Settings& m = me::Config();
        Check(m.warnFreeMB == 300.0f && m.enabled == 1 && m.walkSeconds == 5.0f && m.cachesSeconds == 30.0f, "WarnFreeMB 300 in the ini: applied; the others at their defaults");
        std::string bracketed;
        for (const auto& k : g_keys)
            if (k.second.description.find('[') != std::string::npos) bracketed += " " + k.first;
        Check(bracketed.empty(), F("no setting's description names a bracketed section (%zu settings)%s", g_keys.size(), bracketed.c_str()));
        bool logged = false;
        for (const std::string& l : g_log) logged |= Has(l, "config: settings read from") && Has(l, "[LivingAzeroth.Memory]");
        Check(logged, "the config line names the Memory section");
        me::Config().warnFreeMB = 400.0f;
    }

    // --- 9. the readout as the game runs it -----------------------------------------------------------------------------
    void Readout(const WXL_Api* api)
    {
        std::printf("-- 9. the readout, as the game runs it\n");
        g_log.clear();
        me::Install(api);
        bool guardLine = false;
        for (const std::string& l : g_log) guardLine |= Has(l, "memory: the client's caches: not read (the client's code differs");
        Check(guardLine, "Install: the guards checked first; in this test (no client) the caches aren't read, and the log says why");
        me::Config().cachesSeconds = 30.0f;
        Check(std::strcmp(me::LogPart(), "memory: not walked yet") == 0, "before the first walk the log says so");
        // frames: Update once each
        int frames = 0;
        LARGE_INTEGER f{}, t0{}, t1{};
        QueryPerformanceFrequency(&f);
        double most = 0.0, total = 0.0;
        while (std::strncmp(me::LogPart(), "memory: used", 12) != 0 && frames < 200000)
        {
            QueryPerformanceCounter(&t0);
            me::Update(true);
            QueryPerformanceCounter(&t1);
            const double ms = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
            most = std::max(most, ms);
            total += ms;
            ++frames;
        }
        const std::string line = me::LogPart();
        Check(frames > 1 && Has(line, "memory: used") && Has(line, "largest free") && Has(line, "walk #1") && Has(line, "client caches not read"),
              F("the first walk over %d frames; the 30 s line: %s", frames, line.c_str()));
        Check(most < 3.0, F("a frame's Update: %.3f ms at most, %.4f ms on average over the walk's %d frames", most, total / frames, frames));
        bool first = false;
        for (const std::string& l : g_log) first |= Has(l, "memory: the first walk of the address space:");
        Check(first, "the first walk's line in the log");
        std::vector<std::string> overview;
        me::OverviewLines(overview);
        std::string joined;
        for (const std::string& l : overview) joined += l + "\n";
        Check(overview.size() >= 9 && Has(joined, "Memory: the 32-bit address space, walked every 5 s (walk #1") && Has(joined, "the images by group:") &&
              Has(joined, "the largest private allocations:") && Has(joined, "Living Azeroth by feature:") && Has(joined, "The readout's own cost:") &&
              Has(joined, "Memory warnings: none yet"), F("the overview's %zu lines:\n%s", overview.size(), joined.c_str()));
        std::vector<std::string> snap;
        me::SnapshotLines(snap);
        std::string all;
        for (const std::string& l : snap) all += l + "\n";
        Check(Has(all, "Every image and mapped file") && Has(all, "memory_tests.exe") && Has(all, "Settings: Readout 1, WalkSeconds 5") &&
              Has(all, "  other (untagged)") && Has(all, "the largest private allocations:"),
              F("a snapshot's section: %zu lines, every image named (this exe among them), the features' table, the settings", snap.size()));
        if (g_warpRan)
            Check(Has(all, "  ice and crystals") && Has(all, "Direct3D 2 resources"), "the ice's and the crystals' WARP textures in the features' table, from their reporters");
        std::printf("---- a snapshot's memory section, as written ----\n%s---- end ----\n", all.c_str());
        // a warning: the threshold above what's free
        me::Config().warnFreeMB = 100000.0f;
        g_log.clear();
        bool warned = false;
        for (const DWORD start = GetTickCount(); !warned && GetTickCount() - start < 3000;)
        {
            me::Update(true);
            for (const std::string& l : g_log) warned |= Has(l, "memory: WARNING") && Has(l, "under WarnFreeMB 100000");
            Sleep(10);
        }
        overview.clear();
        me::OverviewLines(overview);
        Check(warned && Has(overview.back(), "a snapshot asked for"), "free under WarnFreeMB: a warning in the log (and chat, in the game), a snapshot asked for: " + overview.back());
        const size_t before = g_log.size();
        for (const DWORD start = GetTickCount(); GetTickCount() - start < 1500;) { me::Update(true); Sleep(10); }
        Check(g_log.size() == before, "and not again within a second and a half (WarnMinutes apart unless worse)");
        me::Config().warnFreeMB = 400.0f;
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> exes;
    bool warp = true;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--no-warp") == 0) warp = false;
        else exes.push_back(argv[i]);
    }
    if (exes.empty()) { std::printf("usage: memory_tests <Wow.exe> [<Wow-WarcraftXL.exe>] [--no-warp]\n"); return 2; }
    static WXL_Api api = MakeApi();
    Guards(exes);
    Sizes();
    Reader();
    AddressSpace();
    Heap();
    Direct3D(warp);
    Warning();
    Settings(&api);
    Readout(&api);
    Check(mu::ForeignFrees() == 0, "after it all: no frees of blocks this module didn't make");
    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures);
    return g_failures;
}
