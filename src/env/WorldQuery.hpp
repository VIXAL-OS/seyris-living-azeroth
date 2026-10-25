// One per-frame snapshot of where the player is and what the sky is doing: map, area chain
// (sub-area -> zone -> ...), the client's own indoor/outdoor verdict, and the live weather. Every
// environment feature reads this instead of querying the client itself.
#pragma once

#include <cstdint>

namespace wxl_livingazeroth::world
{
    constexpr int kMaxAreaChain = 8;

    struct Snapshot
    {
        bool     inWorld = false;          // a player object exists
        float    playerPos[3] = {};
        int      mapId = -1;

        uint32_t areaChain[kMaxAreaChain] = {}; // most specific first: sub-area, zone, ...
        int      areaCount = 0;
        uint32_t areaFlags = 0;            // AreaTable's Flags of every area of the chain, or'ed (a capital city's: Storm's nights)

        bool     outdoors = true;          // the client's verdict (gates fog/sky/outdoor light)
        int      playerTerrainType = -1;   // TerrainType ID the client says the player stands on (-1 = none)

        float    weatherRaw = 0.0f;        // storm intensity as the weather object stores it
        float    weatherIntensity = 0.0f;  // 0..1, after the engine's own dead zone
        int      weatherType = 0;          // 0 fine, 1 rain, 2 snow, 3 sand

        const char* zoneText = "";         // for eyeballing the area ids in the debug panel
        const char* subZoneText = "";
    };

    /// Re-reads everything. Call once per frame, on the main thread (OnUpdate).
    const Snapshot& Refresh();

    /// The last snapshot Refresh() produced.
    const Snapshot& Current();

    /// An area and its parents, most specific first (sub-area, zone, ...), from AreaTable, with AreaRemap.cdbc's rows
    /// applied (env/AreaRemap: an art pack's renumbered caves keep their zone). Returns how many were written (0 for area 0).
    int AreaChain(uint32_t area, uint32_t* out, int max);

    /// Where AreaChain reads AreaTable records (on-disk column layout: ContinentID +0x04, ParentAreaID +0x08,
    /// Flags +0x10): the client's table by default; a test's stand-in (nullptr = the client's again).
    using AreaRecordFn = const uint8_t* (*)(uint32_t id);
    void SetAreaRecordSource(AreaRecordFn fn);
}
