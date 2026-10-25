// The client's own caches, read for the memory readout. See ClientMemory.hpp.
#include "ClientMemory.hpp"

#include <windows.h>

#include <cctype>
#include <cstdio>

namespace wxl_livingazeroth::clientmem
{
    namespace
    {
        constexpr uint32_t kMaxTextures = 500000, kMaxPools = 200000, kMaxModels = 400000, kMaxRoots = 100000;

        // The client is 32-bit: its pointers are words.
        inline uint32_t U32(uintptr_t a) { return *reinterpret_cast<const volatile uint32_t*>(a); }
        inline uintptr_t P(uintptr_t a) { return static_cast<uintptr_t>(U32(a)); }
        inline bool Node(uintptr_t p) { return p >= 0x10000 && !(p & 3); }   // a Storm list's end has bit 0 set

        uint64_t LevelBytes(uint32_t format, uint32_t w, uint32_t h)
        {
            const uint64_t x = w ? w : 1u, y = h ? h : 1u;
            switch (format)
            {
            case kDxt1: return ((x + 3) / 4) * ((y + 3) / 4) * 8;
            case kDxt3: case kDxt5: return ((x + 3) / 4) * ((y + 3) / 4) * 16;
            case kArgb4444: case kArgb1555: case kRgb565: case kUv88: return x * y * 2;
            default: return x * y * 4;   // ABGR8888, ARGB8888, GR1616F, R32F, D24X8
            }
        }

        bool PrefixIs(const char* s, const char* prefix)
        {
            for (; *prefix; ++s, ++prefix)
            {
                char a = static_cast<char>(std::tolower(static_cast<unsigned char>(*s)));
                if (a == '/') a = '\\';
                if (a != *prefix) return false;   // (the name's end mismatches too)
            }
            return true;
        }

        void Keep(Caches& out, const TextureEntry& e)
        {
            int at = out.largestCount;
            if (at == kLargestKept)
            {
                if (e.bytes <= out.largest[kLargestKept - 1].bytes) return;
                at = kLargestKept - 1;
            }
            else ++out.largestCount;
            while (at > 0 && out.largest[at - 1].bytes < e.bytes) { out.largest[at] = out.largest[at - 1]; --at; }
            out.largest[at] = e;
        }

        // One texture (a CTexture on the list).
        void Texture(uintptr_t tex, uint32_t baseMip, Caches& out)
        {
            const uintptr_t atlas = P(tex + kTexAtlas);   // read first: a node that can't be read isn't counted
            ++out.textures;
            if (atlas) { ++out.onAtlas; return; }
            const uintptr_t gx = P(tex + kTexGx);
            if (!gx || !P(gx + kGxApi)) return;   // not made yet (loading), or made and freed
            ++out.created;
            const uint32_t width = U32(gx + kGxWidth), height = U32(gx + kGxHeight), format = U32(gx + kGxFormat), flags = U32(gx + kGxFlags);
            const uint64_t bytes = GxTexBytes(width, height, format, flags, U32(gx + kGxTarget), baseMip);
            const bool target = (flags & kFlagRenderTarget) != 0;
            if (target) { ++out.targets; out.targetBytes += bytes; }
            else out.managedBytes += bytes;
            const char* name = reinterpret_cast<const char*>(tex + kTexName);
            const int kind = KindOf(name);
            out.kindBytes[kind] += bytes;
            ++out.kindCount[kind];
            const uint32_t f = format < kFormats ? format : kUnknown;
            out.formatBytes[f] += bytes;
            ++out.formatCount[f];
            if (out.largestCount == kLargestKept && bytes <= out.largest[kLargestKept - 1].bytes) return;
            TextureEntry e;
            e.bytes = bytes;
            e.width = width;
            e.height = height;
            e.format = static_cast<uint8_t>(f);
            e.kind = static_cast<uint8_t>(kind);
            e.target = target;
            size_t n = 0;
            while (n + 1 < sizeof(e.name) && n < kTexNameSize && name[n]) { e.name[n] = name[n]; ++n; }
            e.name[n] = 0;
            Keep(out, e);
        }

        // Every pass, inside one guard: nothing here has a destructor.
        bool Pass(const Addresses& at, Caches& out, const char*& phase, uint32_t& done)
        {
            __try
            {
                phase = "the Gx device";
                const uintptr_t dev = P(at.gxDevicePtr);
                out.baseMip = dev ? U32(dev + kDevBaseMip) : 0;
                if (out.baseMip > 15) out.baseMip = 0;

                phase = "the textures";
                done = 0;
                const uint32_t texLink = U32(at.textureList + kListOffset);
                if (texLink == 0 || texLink > 0x1000)
                {
                    std::snprintf(out.why, sizeof(out.why), "the texture list's link offset is 0x%X (0x64 expected): not read", texLink);
                    return false;
                }
                for (uintptr_t t = P(at.textureList + kListHead); Node(t) && done < kMaxTextures; t = P(t + texLink + 4), ++done)
                    Texture(t, out.baseMip, out);

                phase = "the GxTex reuse cache";
                out.gxCacheSize = static_cast<int32_t>(U32(at.gxTexCacheSize));
                out.gxCacheMax = static_cast<int32_t>(U32(at.gxTexCacheMax));

                phase = "the Gx pools";
                done = 0;
                if (dev)
                {
                    const uintptr_t list = dev + kDevPools;
                    const uint32_t link = U32(list + kListOffset);
                    if (link <= 0x100)
                        for (uintptr_t p = P(list + kListHead); Node(p) && done < kMaxPools; p = P(p + link + kPoolNext), ++done)
                        {
                            const uint32_t target = U32(p + kPoolTarget), usage = U32(p + kPoolUsage);
                            const int32_t size = static_cast<int32_t>(U32(p + kPoolSize));
                            if (target > 1 || usage > 2) continue;
                            ++out.pools[target][usage];
                            if (size > 0) out.poolBytes[target][usage] += static_cast<uint64_t>(size);
                        }
                }

                phase = "the M2 models";
                done = 0;
                for (uint32_t b = 0; b < kM2BucketCount && done < kMaxModels; ++b)
                    for (uintptr_t s = P(at.m2Cache + kM2Buckets + 4 * b); Node(s) && done < kMaxModels; s = P(s + kSharedNext), ++done)
                    {
                        ++out.m2Models;
                        out.m2FileBytes += U32(s + kSharedFileSize);
                    }
                done = 0;
                for (uintptr_t s = P(at.m2Cache + kM2FreeList); Node(s) && done < kMaxModels; s = P(s + kSharedFreeNext), ++done) ++out.m2Waiting;

                phase = "the WMO roots";
                done = 0;
                const uintptr_t roots = at.wmoHash + kHashFullList;
                const uint32_t rootLink = U32(roots + kListOffset);
                if (rootLink <= 0x1000)
                    for (uintptr_t r = P(roots + kListHead); Node(r) && done < kMaxRoots; r = P(r + rootLink + 4), ++done) ++out.wmoRoots;
                phase = nullptr;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
        }
    }

    const char* FormatName(int format)
    {
        static const char* const kNames[kFormats] = { "unknown", "ABGR8888", "ARGB8888", "ARGB4444", "ARGB1555", "RGB565", "DXT1", "DXT3", "DXT5",
                                                      "UV88", "GR1616F", "R32F", "D24X8" };
        return format >= 0 && format < kFormats ? kNames[format] : "?";
    }

    const char* KindName(int kind)
    {
        static const char* const kNames[kKinds] = { "character skins (composited)", "character", "items", "creatures", "world (doodads, buildings)",
                                                    "terrain (tilesets)", "dungeons", "spells", "sky", "interface", "other files", "unnamed" };
        return kind >= 0 && kind < kKinds ? kNames[kind] : "?";
    }

    int KindOf(const char* name)
    {
        if (!name || !name[0] || PrefixIs(name, "solidtexture") || PrefixIs(name, "uniquetexture")) return kUnnamed;
        if (PrefixIs(name, "characterbaseskin")) return kCharacterSkins;
        if (PrefixIs(name, "character\\")) return kCharacter;
        if (PrefixIs(name, "item\\")) return kItem;
        if (PrefixIs(name, "creature\\")) return kCreature;
        if (PrefixIs(name, "world\\")) return kWorld;
        if (PrefixIs(name, "tileset\\")) return kTerrain;
        if (PrefixIs(name, "dungeons\\")) return kDungeons;
        if (PrefixIs(name, "spells\\")) return kSpells;
        if (PrefixIs(name, "environments\\")) return kSky;
        if (PrefixIs(name, "interface\\")) return kInterface;
        return kOtherNamed;
    }

    uint64_t GxTexBytes(uint32_t width, uint32_t height, uint32_t format, uint32_t flags, uint32_t target, uint32_t baseMip)
    {
        if (!width || !height || width > 65536 || height > 65536) return 0;
        const uint32_t filter = flags & kFlagFilter;
        uint32_t base = 0, mipCount = 1, w = width, h = height;
        // CGxDevice::ITexWHDStartEnd: no mips for a nearest or linear filter, or mips the runtime makes, unless tracked
        if (!((filter == 0 || filter == 1 || (flags & kFlagGenerateMips)) && !(flags & kFlagForceMip)))
        {
            if (Compressed(static_cast<int>(format)))
                for (uint32_t size = width < height ? width : height; size > 4; size >>= 1) ++mipCount;
            else
                for (uint32_t size = width > height ? width : height; size > 1; size >>= 1) ++mipCount;
            base = baseMip < mipCount - 1 ? baseMip : mipCount - 1;
            w >>= base;
            h >>= base;
            if (flags & kFlagForceMip) mipCount = base + 1;
        }
        // CGxDeviceD3d::ITexCreate: a depth texture has one level, the others mipCount - base
        const uint32_t levels = format == kD24X8 ? 1u : mipCount - base;
        uint64_t bytes = 0;
        for (uint32_t l = 0; l < levels; ++l) bytes += LevelBytes(format, w >> l, h >> l);
        if ((flags & kFlagGenerateMips) && levels == 1) bytes += bytes / 3;   // D3DUSAGE_AUTOGENMIPMAP: the runtime's chain
        return target == 1 ? bytes * 6 : bytes;                            // GxTex_CubeMap
    }

    bool Read(const Addresses& at, Caches& out)
    {
        out = Caches{};
        LARGE_INTEGER f{}, t0{}, t1{};
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t0);
        const char* phase = nullptr;
        uint32_t done = 0;
        const bool started = Pass(at, out, phase, done);
        QueryPerformanceCounter(&t1);
        out.ms = f.QuadPart ? static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart) : 0.0;
        out.tick = GetTickCount64();
        if (!started) return false;
        out.read = true;
        if (phase) std::snprintf(out.why, sizeof(out.why), "stopped part way: an unreadable node in %s after %u", phase, done);
        else std::snprintf(out.why, sizeof(out.why), "read whole");
        return true;
    }
}
