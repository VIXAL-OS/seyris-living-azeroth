// The 12340 client's own caches, read for the memory readout (2026-10-25, before HD texture packs): its textures, the GxTex
// reuse cache, the Gx vertex and index pools, the M2 model cache and the WMO roots. Where they are comes from the decomp
// (BASE\wowee-work\wowdecomp: Engine\Source\Services\Texture.cpp, Gx\CGxDevice, Gx\CGxDeviceD3d, Model2\M2Cache.cpp,
// WorldClient\MapObj.cpp); kGuards checks the client's own instructions that use each of them, in Wow.exe and
// Wow-WarcraftXL.exe alike (memory_tests holds them against both files), before anything is read.
//  - Textures: every CTexture is on s_textureList (a Storm list: its link offset, then its terminator's prev and next; an
//    end is marked by bit 0). A texture's CGxTex holds what the D3D texture was made from; the size is worked out the way
//    CGxDevice::ITexWHDStartEnd and CGxDeviceD3d::ITexCreate make it (its mips from its filter, the device's base mip
//    level, a cube's six faces), in the managed pool unless it's a render target. A managed texture is in video memory
//    and, as Direct3D's system-memory copy, in the 32-bit address space too: for HD texture packs, the likely biggest cost.
//  - The GxTex reuse cache: textures freed but kept for reuse, s_gxTexCacheSize of s_gxTexCacheMaxSize bytes.
//  - The Gx pools: the device's list of vertex and index pools, their sizes by target and usage (static pools are
//    managed buffers: a copy in the address space too).
//  - M2 models: CM2Cache's 1021 hash buckets of CM2Shared (each keeps its file in memory: m_fileSize), and its free list
//    (models no longer used, freed 10 s later).
//  - WMO roots: CMapObj::mapObjHash's full list.
// The read is one pass on the main thread between frames (the client adds and frees textures there too), every read
// guarded; a pass stops at the first unreadable node and says so.
#pragma once

#include <cstdint>
#include <cstring>

namespace wxl_livingazeroth::clientmem
{
    struct Addresses
    {
        uintptr_t textureList = 0x00AC3348;      // s_textureList
        uintptr_t gxTexCacheSize = 0x00B49C98;   // s_gxTexCacheSize
        uintptr_t gxTexCacheMax = 0x00B49C9C;    // s_gxTexCacheMaxSize
        uintptr_t gxDevicePtr = 0x00C5DF88;      // g_theGxDevicePtr
        uintptr_t m2Cache = 0x00D3FCF0;          // CM2Cache s_cache
        uintptr_t wmoHash = 0x00D1C454;          // CMapObj::mapObjHash
    };

    // A Storm list: +0 the link offset in each node, +4 the terminator's prev link, +8 its next (bit 0 set: the end).
    constexpr uint32_t kListOffset = 0x00, kListHead = 0x08;
    // CTexture
    constexpr uint32_t kTexGx = 0x44, kTexAtlas = 0x5C, kTexName = 0x6C, kTexNameSize = 260;
    // CGxTex
    constexpr uint32_t kGxWidth = 0x14, kGxHeight = 0x18, kGxTarget = 0x20, kGxFormat = 0x24, kGxFlags = 0x2C, kGxApi = 0x38;
    // CGxTexFlags
    constexpr uint32_t kFlagFilter = 0x07, kFlagForceMip = 0x20, kFlagGenerateMips = 0x40, kFlagRenderTarget = 0x80;
    // CGxDevice
    constexpr uint32_t kDevBaseMip = 0x350, kDevPools = 0x2760;
    // CGxPool (its link at +0: prev, next at +4)
    constexpr uint32_t kPoolNext = 0x04, kPoolTarget = 0x08, kPoolUsage = 0x0C, kPoolSize = 0x10;
    // CM2Cache, CM2Shared
    constexpr uint32_t kM2FreeList = 0x08, kM2Buckets = 0x10, kM2BucketCount = 1021;
    constexpr uint32_t kSharedFreeNext = 0x34, kSharedNext = 0x148, kSharedFileSize = 0x16C;
    // TSHashTable: +0 its vtable, +4 its full list (a Storm list)
    constexpr uint32_t kHashFullList = 0x04;

    // Gx texture formats (EGxTexFormat)
    enum GxFormat : uint8_t { kUnknown, kAbgr8888, kArgb8888, kArgb4444, kArgb1555, kRgb565, kDxt1, kDxt3, kDxt5, kUv88, kGr1616F, kR32F, kD24X8, kFormats };
    const char* FormatName(int format);
    inline bool Compressed(int format) { return format == kDxt1 || format == kDxt3 || format == kDxt5; }

    /// What a texture is for, by its file's path.
    enum Kind : uint8_t { kCharacterSkins, kCharacter, kItem, kCreature, kWorld, kTerrain, kDungeons, kSpells, kSky, kInterface, kOtherNamed, kUnnamed, kKinds };
    const char* KindName(int kind);
    int KindOf(const char* name);

    /// A texture's bytes as the client makes its D3D texture (one face; a cube's are x 6), from its CGxTex.
    uint64_t GxTexBytes(uint32_t width, uint32_t height, uint32_t format, uint32_t flags, uint32_t target, uint32_t baseMip);

    struct TextureEntry
    {
        uint64_t bytes = 0;
        uint32_t width = 0, height = 0;   // as the client asked (before the base mip)
        uint8_t  format = 0, kind = 0;
        bool     target = false;
        char     name[112] = {};
    };
    constexpr int kLargestKept = 25;

    struct Caches
    {
        bool     read = false;
        char     why[200] = "not read yet";
        double   ms = 0.0;               // the pass's cost
        uint64_t tick = 0;               // GetTickCount64 at the pass
        uint32_t baseMip = 0;
        // textures
        uint32_t textures = 0;           // on s_textureList
        uint32_t created = 0;            // with a D3D texture
        uint32_t onAtlas = 0;            // sharing an atlas's (counted with it, not again)
        uint32_t targets = 0;            // render targets (the default pool)
        uint64_t managedBytes = 0, targetBytes = 0;   // managedBytes: every texture that isn't a render target
        bool     deviceEx = false;   // gxApi "D3D9Ex" (CGxDeviceD3d9Ex, vtable 0xA2F500): those are in the default pool,
                                     // video memory only (0x6A5FE0), not managed: no copy in the address space
        uint64_t kindBytes[kKinds] = {};
        uint32_t kindCount[kKinds] = {};
        uint64_t formatBytes[kFormats] = {};
        uint32_t formatCount[kFormats] = {};
        TextureEntry largest[kLargestKept];
        int      largestCount = 0;
        // the GxTex reuse cache
        int32_t  gxCacheSize = 0, gxCacheMax = 0;
        // the Gx pools: [vertex, index][static, dynamic, stream]
        uint32_t pools[2][3] = {};
        uint64_t poolBytes[2][3] = {};
        // M2 models, WMO roots
        uint32_t m2Models = 0, m2Waiting = 0;
        uint64_t m2FileBytes = 0;
        uint32_t wmoRoots = 0;

        uint64_t UncompressedBytes() const
        {
            uint64_t s = 0;
            for (int f = 0; f < kFormats; ++f)
                if (!Compressed(f)) s += formatBytes[f];
            return s;
        }
    };

    /// One pass over the caches (main thread, between frames). False if it couldn't start (out.why says why); a pass that
    /// stopped part way keeps what it read and says where.
    bool Read(const Addresses& at, Caches& out);

    // --- the client's code as the 12340 client has it (Wow.exe and Wow-WarcraftXL.exe alike) ------------------------------
    struct Guard
    {
        const char* what;
        uintptr_t   address;
        uint32_t    size;
        uint8_t     bytes[16];
    };
    inline constexpr Guard kGuards[] = {
        {"TextureDestroy: the texture list's head (s_textureList + 8)", 0x004B848D, 6, {0x8B, 0x1D, 0x50, 0x33, 0xAC, 0x00}},
        {"TextureDestroy: a texture's name at +0x6C", 0x004B84A9, 3, {0x8D, 0x4B, 0x6C}},
        {"TextureDestroy: the list's link offset, the next node at the link + 4", 0x004B84B7, 10, {0x8B, 0x15, 0x48, 0x33, 0xAC, 0x00, 0x8B, 0x5C, 0x1A, 0x04}},
        {"TextureIsReadyNow: the texture's CGxTex at +0x44", 0x004B62AC, 3, {0x8B, 0x49, 0x44}},
        {"TextureIsReadyNow: the Gx device (g_theGxDevicePtr)", 0x004B62B4, 6, {0x8B, 0x0D, 0x88, 0xDF, 0xC5, 0x00}},
        {"TextureOnAtlas: the texture's atlas at +0x5C", 0x004B5477, 3, {0x39, 0x48, 0x5C}},
        {"ITexCreate: the flags (+0x2C), render target (bit 7) the default pool, the format into +0x24", 0x006A2C30, 16,
         {0x8B, 0x4E, 0x2C, 0x33, 0xD2, 0x84, 0xC9, 0xBF, 0x01, 0x00, 0x00, 0x00, 0x89, 0x46, 0x24, 0x89}},
        {"ITexCreate: the D3D texture into +0x38", 0x006A2D63, 6, {0x8B, 0x4D, 0xF0, 0x89, 0x4E, 0x38}},
        // gxApi "D3D9Ex": CGxDeviceD3d9Ex::ITexCreate (0x6A5FE0) makes them in D3DPOOL_DEFAULT, into the same +0x38.
        {"Ex ITexCreate: D3DPOOL_DEFAULT (push 0)", 0x006A6041, 2, {0x6A, 0x00}},
        {"Ex ITexCreate: the D3D texture into +0x38", 0x006A6082, 3, {0x89, 0x56, 0x38}},
        {"the Ex device class's vtable (0xA2F500) starts with 0x6A7100", 0x00A2F500, 4, {0x00, 0x71, 0x6A, 0x00}},
        {"ITexWHDStartEnd: the width at +0x14", 0x006A5EF6, 3, {0x8B, 0x42, 0x14}},
        {"ITexWHDStartEnd: the height at +0x18", 0x006A5F03, 3, {0x8B, 0x42, 0x18}},
        {"ITexWHDStartEnd: the flags at +0x2C", 0x006A5F08, 3, {0x8B, 0x42, 0x2C}},
        {"ITexWHDStartEnd: the filter (bits 0-2)", 0x006A5F0D, 3, {0x83, 0xE6, 0x07}},
        {"ITexWHDStartEnd: generate mips (0x40)", 0x006A5F17, 2, {0xA8, 0x40}},
        {"ITexWHDStartEnd: force mip tracking (0x20)", 0x006A5F1B, 2, {0xA8, 0x20}},
        {"ITexWHDStartEnd: the format at +0x24, DXT1", 0x006A5F37, 6, {0x8B, 0x42, 0x24, 0x83, 0xF8, 0x06}},
        {"ITexWHDStartEnd: the device's base mip level at +0x350", 0x006A5FAE, 6, {0x8B, 0x89, 0x50, 0x03, 0x00, 0x00}},
        {"TextureCacheUpdate: the GxTex cache's max and size", 0x004B6AE6, 11, {0x8B, 0x15, 0x9C, 0x9C, 0xB4, 0x00, 0xA1, 0x98, 0x9C, 0xB4, 0x00}},
        {"M2Initialize: the M2 cache (s_cache)", 0x0081C6E9, 5, {0xB9, 0xF0, 0xFC, 0xD3, 0x00}},
        {"CreateShared: 1021 buckets", 0x0081C4E6, 6, {0x69, 0xC9, 0xFD, 0x03, 0x00, 0x00}},
        {"CreateShared: the buckets at +0x10", 0x0081C4F0, 4, {0x8B, 0x74, 0x90, 0x10}},
        {"CreateShared: a model's next in its bucket at +0x148", 0x0081C549, 6, {0x81, 0xC6, 0x48, 0x01, 0x00, 0x00}},
        {"CM2Shared::Load: the file's size at +0x16C", 0x0083D442, 6, {0x89, 0x86, 0x6C, 0x01, 0x00, 0x00}},
        {"GarbageCollect: the free list at +8", 0x0081C2A2, 3, {0x83, 0xC7, 0x08}},
        {"GarbageCollect: a model's next on it at +0x34", 0x0081C2C4, 3, {0x8B, 0x46, 0x34}},
        {"the WMO roots' walk: the hash's full list head", 0x007AD10D, 6, {0x8B, 0x1D, 0x60, 0xC4, 0xD1, 0x00}},
        {"the WMO roots' walk: its link offset, the next", 0x007AD131, 9, {0xA1, 0x58, 0xC4, 0xD1, 0x00, 0x8B, 0x44, 0x03, 0x04}},
        {"PoolCreate: the target at +8", 0x006876F5, 3, {0x89, 0x48, 0x08}},
        {"PoolCreate: the size at +0x10", 0x006876FB, 3, {0x89, 0x48, 0x10}},
        {"PoolCreate: the usage at +0xC", 0x00687703, 3, {0x89, 0x50, 0x0C}},
        {"PoolCreate: the device's pool list at +0x2760", 0x0068772C, 6, {0x8D, 0x8F, 0x60, 0x27, 0x00, 0x00}},
        {"the pools' walk: the head at +0x2768", 0x0068C1F4, 6, {0x8B, 0x9E, 0x68, 0x27, 0x00, 0x00}},
        {"the pools' walk: the next at +4", 0x0068C216, 3, {0x8B, 0x5B, 0x04}},
    };

    /// The first guard whose bytes differ, or nullptr. `code(address, size)` returns the bytes there (nullptr: unreadable).
    template <class CodeAt>
    const Guard* FirstMismatch(CodeAt code)
    {
        for (const Guard& g : kGuards)
        {
            const void* p = code(g.address, g.size);
            if (!p || std::memcmp(p, g.bytes, g.size) != 0) return &g;
        }
        return nullptr;
    }
}
