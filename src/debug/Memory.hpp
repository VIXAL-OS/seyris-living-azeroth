// The memory readout (2026-10-25, before HD texture packs: "do we have sufficient logging to catch this if it happens and
// figure out what costs the most memory?"). The 32-bit client is large-address-aware: it runs out at about 4 GB of address
// space, not at the machine's RAM, and usually sooner, when no free block is left big enough for what it asks. The readout
// says what uses the space, from inside the process:
//  - the address space (env/AddressSpace): walked every WalkSeconds, a quarter of a millisecond a frame: committed,
//    reserved and free, the largest free block and the free blocks over 64 MB, and who owns it: each image (the client, the
//    WarcraftXL core, each extension, Direct3D, the graphics driver), each mapped file, private memory (heaps,
//    VirtualAlloc) and its largest allocations; the walk's own cost;
//  - Direct3D 9: the driver's available texture memory;
//  - the client's caches (env/ClientMemory), every ClientCachesSeconds in one pass: its textures by kind and format (each a
//    managed texture: a copy in video memory and, Direct3D's system-memory copy, another in this address space), the GxTex
//    reuse cache, the vertex and index pools, the M2 models and the WMO roots;
//  - Living Azeroth's own use by feature (debug/MemoryUse): its heap, and its Direct3D resources.
// On the overview page (its memory section), in the log's 30 s status line, and in snapshots (all of it, with the largest
// textures, images and allocations named). Below WarnFreeMB free or a WarnLargestFreeMB largest block it says so in chat
// once (WarnMinutes apart unless it gets worse) and writes a snapshot, so a crash for lack of memory leaves evidence.
#pragma once

#include "wxl/PluginApi.h"

#include <cstdint>
#include <string>
#include <vector>

namespace wxl_livingazeroth::memory
{
    struct Settings
    {
        int   enabled = 1;            // Readout
        float walkSeconds = 5.0f;     // WalkSeconds
        float cachesSeconds = 30.0f;  // ClientCachesSeconds (0: only for snapshots and warnings)
        float warnFreeMB = 400.0f;    // WarnFreeMB
        float warnLargestMB = 64.0f;  // WarnLargestFreeMB
        float warnMinutes = 10.0f;    // WarnMinutes
    };
    Settings& Config();

    /// From WXL_Load.
    void Install(const WXL_Api* api);
    /// Once per frame on the main thread (OnUpdate), before the snapshot key: the walk's step, the caches when due, the
    /// warnings.
    void Update(bool inWorld);

    /// The overview page's section.
    void OverviewLines(std::vector<std::string>& out);
    /// The part of the 30 s status line (one line).
    const char* LogPart();
    /// A snapshot's section: everything, with the client's caches read again now.
    void SnapshotLines(std::vector<std::string>& out);

    // --- the warning's rule (pure: memory_tests) ------------------------------------------------------------------------
    struct WarnState
    {
        bool     warned = false;
        uint64_t tick = 0;        // when it last warned (GetTickCount64)
        uint64_t free = 0, largest = 0;
    };
    /// Whether to warn now: free below WarnFreeMB or the largest block below WarnLargestFreeMB (largest 0: not known), and
    /// never warned, or WarnMinutes since, or worse since (100 MB less free, or the largest block halved). Updates `s` when
    /// it says yes; `why` says which.
    bool ShouldWarn(WarnState& s, uint64_t nowTick, uint64_t freeBytes, uint64_t largestBytes, const Settings& c, std::string& why);
}
