// AreaRemap.cdbc (tools/gen_area_remap.py): every place keeps its zone when an art pack renumbers the client's areas.
//
// Project Reforged's M and S (2026-10-25) move 685 WMO groups -- caves and dungeon entrances -- to 43 new areas with no
// parent zone, and give three instances' own areas a parent zone on the continent. world::AreaChain asks this table at
// every link: when the client's AreaTable record of an area is the one a row was made for (its continent and parent
// as the pack ships them), the chain goes on at the stock area the pack's replaced (or ends, as in the stock table).
// A stock client's record never matches, so the same file is safe with and without the packs.
#pragma once

#include <cstddef>
#include <cstdint>

struct WXL_SeyrisCdbcApi;

namespace wxl_livingazeroth::arearemap
{
    struct Row
    {
        uint32_t area = 0;      // the area the client reports (the pack's id)
        uint32_t continent = 0; // the pack's AreaTable ContinentID for it
        uint32_t parent = 0;    // ... and its ParentAreaID: the row applies only to that record
        uint32_t stockArea = 0; // where the chain goes on (0 = it ends here)
    };

    /// (Re)loads the table. Without the file nothing is remapped.
    void Load(const WXL_SeyrisCdbcApi* cdbc);
    /// Replaces the rows (tests); nullptr/0 clears them.
    void SetRows(const Row* rows, size_t count);

    /// Where the chain goes after `area`, whose client record says `continent` and `parent`: true with `next` set when a
    /// row applies (the stock area, 0 = the chain ends), false when none does (follow the record's parent). Thread-safe.
    bool Next(uint32_t area, uint32_t continent, uint32_t parent, uint32_t& next);

    size_t      RowCount();
    uint32_t    Generation();
    /// "46 row(s); applied to 2 area(s) this session, last 6014 -> 25" (the climate panel, the F11 snapshot, the log).
    const char* Status();
    /// Areas a row applied to this session, and the last one (0 = none yet).
    uint32_t    AppliedAreas();
    uint32_t    LastArea();
    uint32_t    LastStockArea();
}
