// The process's address space, walked with VirtualQuery a slice at a time (2026-10-25, before HD texture packs: the client
// is 32-bit and large-address-aware, so it runs out at about 4 GB of address space, not at the machine's RAM, and the
// fragmentation of what's left usually ends it before the total does: a large allocation needs one free block that big).
// A walk is spread over frames, each step within a time budget, and sums what Windows can tell about each region:
//  - free, reserved or committed; the largest free block and how many free blocks are over 64 MB;
//  - who owns it: an image (an EXE or a DLL: each by its allocation base, its sections together), a mapped file (each), or
//    private memory (heaps, VirtualAlloc: the largest allocations kept).
// Names come from the file each image or view maps (GetMappedFileName: no loader lock), looked up once per allocation base.
// Pure Win32 on the calling process: the tests walk their own.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace wxl_livingazeroth::addrspace
{
    enum Kind : uint8_t { kImage, kMapped, kPrivate, kKinds };

    /// An image or a mapped view: its regions together (one allocation base).
    struct Owner
    {
        uintptr_t   base = 0;
        uint64_t    committed = 0, reserved = 0;
        Kind        kind = kImage;
        std::string path;   // the mapped file's path as Windows names it (\Device\HarddiskVolumeN\...), empty if it has none
        std::string name;   // its last part (Wow-WarcraftXL.exe, d3d9.dll), or "(shared memory, no file)"
    };

    struct PrivateBlock
    {
        uintptr_t base = 0;
        uint64_t  committed = 0, reserved = 0;
    };

    struct Totals
    {
        uint64_t  committed[kKinds] = {}, reserved[kKinds] = {};
        uint64_t  free = 0, largestFree = 0;
        uintptr_t largestFreeAt = 0;
        uint32_t  freeOver64 = 0;     // free blocks of 64 MB or more
        uint32_t  regions = 0;        // VirtualQuery's answers
        uint32_t  allocations = 0;    // distinct allocation bases (reserved or committed)
        uint64_t  space = 0;          // the address space the walk covered: up to the highest user address
        std::vector<Owner> owners;    // the images and mapped views, in address order
        std::vector<PrivateBlock> largestPrivate;   // the largest private allocations (reserved + committed), largest first

        // its cost
        uint32_t walk = 0;            // its number (1 = the first)
        uint32_t frames = 0;          // steps it took
        double   ms = 0.0;            // all its steps together
        double   maxStepMs = 0.0;
        uint32_t lookups = 0;         // names looked up (new allocation bases)
        uint64_t finishedTick = 0;    // GetTickCount64 when it finished

        uint64_t Committed() const { return committed[0] + committed[1] + committed[2]; }
        uint64_t Reserved() const { return reserved[0] + reserved[1] + reserved[2]; }
        uint64_t Used() const { return Committed() + Reserved(); }
    };

    constexpr uint64_t kBigFree = 64ull << 20;
    constexpr size_t   kLargestPrivateKept = 12;

    class Walker
    {
    public:
        Walker();
        /// Starts a walk from address 0 (a walk under way starts over).
        void Start();
        bool Active() const { return m_active; }
        /// Walks on for about `budgetMs`; true when this step finished the walk (Last() is then the new one).
        bool Step(double budgetMs);
        /// Start, then Step until done: the whole walk at once (tests, a snapshot asked for before the first walk).
        void WalkAll();
        const Totals& Last() const { return m_last; }
        bool HaveWalked() const { return m_last.walk > 0; }

    private:
        void Region(const void* info);
        void EndAllocation();
        const std::string& PathOf(uintptr_t base, Kind kind);

        Totals    m_work, m_last;
        bool      m_active = false;
        uintptr_t m_next = 0, m_top = 0;
        uint32_t  m_walks = 0;
        // the allocation being summed (its regions are contiguous)
        uintptr_t m_allocBase = 0;
        Kind      m_allocKind = kPrivate;
        uint64_t  m_allocCommitted = 0, m_allocReserved = 0;
        bool      m_inAlloc = false;
        std::unordered_map<uintptr_t, std::string> m_paths, m_seenPaths;   // names by base: kept from the last walk, this walk's
        double    m_freq = 0.0;
    };

    /// The last part of a path ("\Device\HarddiskVolume3\Games\x\Wow.exe" -> "Wow.exe").
    std::string FileName(const std::string& path);

    /// What an image is to the readout, by its path and name: 0 the client's exe, 1 WarcraftXL (its core), 2 an extension
    /// (under Extensions\), 3 Direct3D, 4 the graphics driver, 5 Windows, 6 other.
    enum ImageGroup : uint8_t { kClient, kCore, kExtension, kDirect3D, kDriver, kWindows, kOtherImage, kImageGroups };
    ImageGroup GroupOf(const Owner& image, uintptr_t exeBase);
    const char* GroupName(int group);
}
