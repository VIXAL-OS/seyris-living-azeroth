// Living Azeroth's own memory, by feature. See MemoryUse.hpp.
#include "MemoryUse.hpp"

#include <windows.h>
#include <d3d9.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>

namespace wxl_livingazeroth::memuse
{
    namespace
    {
        // A block's header, right before what the caller gets; kHeader keeps the caller's pointer as aligned as malloc's.
        struct Header
        {
            uint32_t size;
            uint8_t  tag;
            uint8_t  magic;
            uint16_t check;   // from the size and the tag: a pointer this module didn't make won't pass for one of ours
        };
        static_assert(sizeof(Header) == 8, "the header is two words");
        constexpr size_t  kHeader = alignof(std::max_align_t) > sizeof(Header) ? alignof(std::max_align_t) : sizeof(Header);
        constexpr uint8_t kMagic = 0xA7;

        std::atomic<int64_t>  g_bytes[kTagCount];
        std::atomic<int64_t>  g_blocks[kTagCount];
        std::atomic<uint64_t> g_made[kTagCount];
        std::atomic<uint32_t> g_foreign{0};

        uint16_t Check(uint32_t size, uint8_t tag)
        {
            const uint32_t x = size * 0x9E3779B1u ^ (static_cast<uint32_t>(tag) << 11) ^ 0x5A3Cu;
            return static_cast<uint16_t>(x ^ (x >> 16));
        }

        Header* HeaderOf(void* p) { return reinterpret_cast<Header*>(static_cast<char*>(p) - sizeof(Header)); }

        void* Allocate(size_t n)
        {
            if (n > 0xFFFF0000u) return nullptr;   // a size the header can't hold (never in a 32-bit process)
            void* raw = std::malloc(n + kHeader);
            if (!raw) return nullptr;
            const uint8_t tag = t_tag < kTagCount ? t_tag : static_cast<uint8_t>(kOther);
            void* user = static_cast<char*>(raw) + kHeader;
            Header* h = HeaderOf(user);
            h->size = static_cast<uint32_t>(n);
            h->tag = tag;
            h->magic = kMagic;
            h->check = Check(h->size, tag);
            g_bytes[tag].fetch_add(static_cast<int64_t>(n), std::memory_order_relaxed);
            g_blocks[tag].fetch_add(1, std::memory_order_relaxed);
            g_made[tag].fetch_add(1, std::memory_order_relaxed);
            return user;
        }

        void Free(void* p)
        {
            if (!p) return;
            Header* h = HeaderOf(p);
            if (h->magic != kMagic || h->tag >= kTagCount || h->check != Check(h->size, h->tag))
            {
                // Not one of ours: freed the way the default operator delete would.
                g_foreign.fetch_add(1, std::memory_order_relaxed);
                std::free(p);
                return;
            }
            g_bytes[h->tag].fetch_sub(static_cast<int64_t>(h->size), std::memory_order_relaxed);
            g_blocks[h->tag].fetch_sub(1, std::memory_order_relaxed);
            h->magic = 0;
            std::free(static_cast<char*>(p) - kHeader);
        }

        void* AllocateOrThrow(size_t n)
        {
            for (;;)
            {
                if (void* p = Allocate(n ? n : 1)) return p;
                const std::new_handler handler = std::get_new_handler();
                if (!handler) throw std::bad_alloc();
                handler();
            }
        }

        // --- Direct3D -------------------------------------------------------------------------------------------------
        constexpr uint32_t FourCc(char a, char b, char c, char d)
        {
            return static_cast<uint32_t>(static_cast<uint8_t>(a)) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
        }

        void Charge(Gpu& g, D3DPOOL pool, uint64_t bytes, bool dynamicBuffer = false)
        {
            if (pool == D3DPOOL_MANAGED) g.managed += bytes;
            else if (pool == D3DPOOL_DEFAULT && dynamicBuffer) g.dynamic += bytes;
            else if (pool == D3DPOOL_DEFAULT) g.video += bytes;
            else g.system += bytes;
            ++g.resources;
        }

        uint64_t Samples(D3DMULTISAMPLE_TYPE type) { return type >= D3DMULTISAMPLE_2_SAMPLES && type <= D3DMULTISAMPLE_16_SAMPLES ? static_cast<uint64_t>(type) : 1u; }

        bool TextureBytes(IDirect3DBaseTexture9* t, D3DPOOL& pool, uint64_t& bytes)
        {
            __try
            {
                const DWORD levels = t->GetLevelCount();
                const D3DRESOURCETYPE type = t->GetType();
                bytes = 0;
                bool autogen = false;
                if (type == D3DRTYPE_TEXTURE || type == D3DRTYPE_CUBETEXTURE)
                {
                    for (DWORD l = 0; l < levels; ++l)
                    {
                        D3DSURFACE_DESC d{};
                        const HRESULT hr = type == D3DRTYPE_TEXTURE ? static_cast<IDirect3DTexture9*>(t)->GetLevelDesc(l, &d)
                                                                    : static_cast<IDirect3DCubeTexture9*>(t)->GetLevelDesc(l, &d);
                        if (FAILED(hr)) return false;
                        if (l == 0) { pool = d.Pool; autogen = (d.Usage & D3DUSAGE_AUTOGENMIPMAP) != 0; }
                        bytes += SurfaceBytes(d.Format, d.Width, d.Height) * Samples(d.MultiSampleType) * (type == D3DRTYPE_CUBETEXTURE ? 6u : 1u);
                    }
                }
                else if (type == D3DRTYPE_VOLUMETEXTURE)
                {
                    for (DWORD l = 0; l < levels; ++l)
                    {
                        D3DVOLUME_DESC d{};
                        if (FAILED(static_cast<IDirect3DVolumeTexture9*>(t)->GetLevelDesc(l, &d))) return false;
                        if (l == 0) pool = d.Pool;
                        bytes += SurfaceBytes(d.Format, d.Width, d.Height) * d.Depth;
                    }
                }
                else return false;
                if (autogen && levels == 1) bytes += bytes / 3;   // the chain the runtime makes below its one level
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool SurfaceDesc(IDirect3DSurface9* s, D3DSURFACE_DESC& d)
        {
            __try { return SUCCEEDED(s->GetDesc(&d)); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool VertexDesc(IDirect3DVertexBuffer9* b, D3DVERTEXBUFFER_DESC& d)
        {
            __try { return SUCCEEDED(b->GetDesc(&d)); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool IndexDesc(IDirect3DIndexBuffer9* b, D3DINDEXBUFFER_DESC& d)
        {
            __try { return SUCCEEDED(b->GetDesc(&d)); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
    }

    HeapUse Heap(int tag)
    {
        HeapUse h;
        if (tag < 0 || tag >= kTagCount) return h;
        h.bytes = g_bytes[tag].load(std::memory_order_relaxed);
        h.blocks = g_blocks[tag].load(std::memory_order_relaxed);
        h.made = g_made[tag].load(std::memory_order_relaxed);
        return h;
    }

    uint32_t ForeignFrees() { return g_foreign.load(std::memory_order_relaxed); }

    bool Counting()
    {
        for (int t = 0; t < kTagCount; ++t)
            if (g_made[t].load(std::memory_order_relaxed)) return true;
        return false;
    }

    uint64_t SurfaceBytes(uint32_t format, uint32_t width, uint32_t height)
    {
        const uint64_t w = width ? width : 1u, h = height ? height : 1u;
        const uint64_t blocks = ((w + 3) / 4) * ((h + 3) / 4);
        switch (format)
        {
        case FourCc('D', 'X', 'T', '1'): case FourCc('A', 'T', 'I', '1'): return blocks * 8;
        case FourCc('D', 'X', 'T', '2'): case FourCc('D', 'X', 'T', '3'): case FourCc('D', 'X', 'T', '4'): case FourCc('D', 'X', 'T', '5'):
        case FourCc('A', 'T', 'I', '2'): return blocks * 16;
        case FourCc('I', 'N', 'T', 'Z'): case FourCc('D', 'F', '2', '4'): case FourCc('R', 'A', 'W', 'Z'): return w * h * 4;
        case FourCc('D', 'F', '1', '6'): return w * h * 2;
        case FourCc('N', 'U', 'L', 'L'): case FourCc('R', 'E', 'S', 'Z'): return 0;
        case D3DFMT_A32B32G32R32F: return w * h * 16;
        case D3DFMT_A16B16G16R16: case D3DFMT_A16B16G16R16F: case D3DFMT_G32R32F: case D3DFMT_Q16W16V16U16: return w * h * 8;
        case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_A8B8G8R8: case D3DFMT_X8B8G8R8: case D3DFMT_A2R10G10B10:
        case D3DFMT_A2B10G10R10: case D3DFMT_G16R16: case D3DFMT_G16R16F: case D3DFMT_R32F: case D3DFMT_D24X8: case D3DFMT_D24S8:
        case D3DFMT_D24X4S4: case D3DFMT_D32: case D3DFMT_D24FS8: case D3DFMT_D32F_LOCKABLE: case D3DFMT_Q8W8V8U8:
        case D3DFMT_X8L8V8U8: case D3DFMT_V16U16: case D3DFMT_A2W10V10U10: case D3DFMT_INDEX32: return w * h * 4;
        case D3DFMT_R8G8B8: return w * h * 3;
        case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4: case D3DFMT_A8R3G3B2:
        case D3DFMT_X4R4G4B4: case D3DFMT_A8L8: case D3DFMT_V8U8: case D3DFMT_L6V5U5: case D3DFMT_L16: case D3DFMT_D16:
        case D3DFMT_D16_LOCKABLE: case D3DFMT_D15S1: case D3DFMT_R16F: case D3DFMT_A8P8: case D3DFMT_INDEX16: return w * h * 2;
        case D3DFMT_A8: case D3DFMT_L8: case D3DFMT_P8: case D3DFMT_A4L4: case D3DFMT_R3G3B2: return w * h;
        default: return 0;
        }
    }

    void Gpu::Add(IDirect3DBaseTexture9* texture)
    {
        if (!texture) return;
        D3DPOOL pool = D3DPOOL_DEFAULT;
        uint64_t bytes = 0;
        if (TextureBytes(texture, pool, bytes)) Charge(*this, pool, bytes);
        else ++unreadable;
    }

    void Gpu::Add(IDirect3DSurface9* surface)
    {
        if (!surface) return;
        D3DSURFACE_DESC d{};
        if (SurfaceDesc(surface, d)) Charge(*this, d.Pool, SurfaceBytes(d.Format, d.Width, d.Height) * Samples(d.MultiSampleType));
        else ++unreadable;
    }

    void Gpu::Add(IDirect3DVertexBuffer9* buffer)
    {
        if (!buffer) return;
        D3DVERTEXBUFFER_DESC d{};
        if (VertexDesc(buffer, d)) Charge(*this, d.Pool, d.Size, (d.Usage & D3DUSAGE_DYNAMIC) != 0);
        else ++unreadable;
    }

    void Gpu::Add(IDirect3DIndexBuffer9* buffer)
    {
        if (!buffer) return;
        D3DINDEXBUFFER_DESC d{};
        if (IndexDesc(buffer, d)) Charge(*this, d.Pool, d.Size, (d.Usage & D3DUSAGE_DYNAMIC) != 0);
        else ++unreadable;
    }
}

// --- operator new and delete, for this DLL alone ----------------------------------------------------------------------------
// (The aligned forms are the CRT's own: they pair with each other through _aligned_malloc and aren't counted.)
namespace mu = wxl_livingazeroth::memuse;

void* operator new(size_t n) { return mu::AllocateOrThrow(n); }
void* operator new[](size_t n) { return mu::AllocateOrThrow(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept
{
    try { return mu::AllocateOrThrow(n); }
    catch (...) { return nullptr; }
}
void* operator new[](size_t n, const std::nothrow_t&) noexcept
{
    try { return mu::AllocateOrThrow(n); }
    catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { mu::Free(p); }
void operator delete[](void* p) noexcept { mu::Free(p); }
void operator delete(void* p, size_t) noexcept { mu::Free(p); }
void operator delete[](void* p, size_t) noexcept { mu::Free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { mu::Free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { mu::Free(p); }
