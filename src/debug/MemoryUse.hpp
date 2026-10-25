// Living Azeroth's own memory, by feature (debug/MemoryTag.hpp names the features). Two halves:
//  - the heap: operator new and delete are replaced in this DLL (the static CRT keeps the replacement module-local: the
//    client and the other modules keep theirs). Each block carries a small header with its size and the tag open on its
//    thread when it was made, so it's given back to the same feature when freed. Heap() reads a tag's live bytes;
//  - Direct3D: each feature lists its own textures, surfaces and buffers into a Gpu (its GpuMemory functions), sized from
//    their descriptions. A managed resource is in video memory and, as a system-memory copy, in the 32-bit address space
//    too; a default-pool one only in video memory, except a dynamic vertex or index buffer, which the driver usually
//    keeps in system memory mapped into this process (counted in the address space); a system-memory one only there.
#pragma once

#include "MemoryTag.hpp"

#include <cstddef>
#include <cstdint>

struct IDirect3DBaseTexture9;
struct IDirect3DSurface9;
struct IDirect3DVertexBuffer9;
struct IDirect3DIndexBuffer9;

namespace wxl_livingazeroth::memuse
{
    /// One tag's heap: what's live now, and how many blocks it has made since the start.
    struct HeapUse
    {
        int64_t  bytes = 0;    // the blocks' sizes as asked for (the header and the heap's own rounding not counted)
        int64_t  blocks = 0;
        uint64_t made = 0;
    };
    HeapUse Heap(int tag);
    /// Frees of blocks this module's operator new didn't make (0 expected: something freed across modules).
    uint32_t ForeignFrees();
    /// Whether this build replaced operator new (false in a test that doesn't link debug/MemoryUse.cpp).
    bool Counting();

    /// One feature's Direct3D resources.
    struct Gpu
    {
        uint64_t managed = 0;   // D3DPOOL_MANAGED: video memory, and its copy in the address space
        uint64_t video = 0;     // D3DPOOL_DEFAULT: video memory only
        uint64_t dynamic = 0;   // D3DPOOL_DEFAULT, D3DUSAGE_DYNAMIC vertex and index buffers: usually mapped system memory
        uint64_t system = 0;    // D3DPOOL_SYSTEMMEM, D3DPOOL_SCRATCH: the address space only
        uint32_t resources = 0;
        uint32_t unreadable = 0; // listed but not readable (counted nowhere)

        void Add(IDirect3DBaseTexture9* texture);
        void Add(IDirect3DSurface9* surface);
        void Add(IDirect3DVertexBuffer9* buffer);
        void Add(IDirect3DIndexBuffer9* buffer);

        uint64_t InAddressSpace() const { return managed + system + dynamic; }
        uint64_t InVideo() const { return managed + video; }
        Gpu& operator+=(const Gpu& o)
        {
            managed += o.managed; video += o.video; dynamic += o.dynamic; system += o.system; resources += o.resources;
            unreadable += o.unreadable;
            return *this;
        }
    };

    /// Bytes of one level of a surface in a D3D9 format (block formats by their 4x4 blocks); 0 for a format it doesn't know.
    uint64_t SurfaceBytes(uint32_t d3dFormat, uint32_t width, uint32_t height);
}
