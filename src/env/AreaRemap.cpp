#include "AreaRemap.hpp"

#include "CdbcLoad.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace wxl_livingazeroth::arearemap
{
    namespace
    {
        constexpr WXL_SeyrisCdbcField kFields[] = {
            {"ID",          0, WXL_CDBC_FIELD_VALUE},
            {"AreaID",      1, WXL_CDBC_FIELD_VALUE},
            {"ContinentID", 2, WXL_CDBC_FIELD_VALUE},
            {"ParentID",    3, WXL_CDBC_FIELD_VALUE},
            {"StockAreaID", 4, WXL_CDBC_FIELD_VALUE},
            {"Comment",     5, WXL_CDBC_FIELD_STRING}, // for the editor (which place the row is), never read
        };
        constexpr WXL_SeyrisCdbcDefinition kDef = { "AreaRemap", "DBFilesClient\\AreaRemap.cdbc", kFields, 6 };

        // Sorted by area (one row per area: the generator writes one). Written only by Load/SetRows, on the main
        // thread before the features run; read from any thread.
        std::vector<Row>   g_rows;
        std::string        g_loadStatus = "not loaded yet";
        uint32_t           g_generation = 1;

        // Which areas a row applied to this session: a small set (a pack renumbers a few dozen), kept under a lock that
        // is taken only the first time an area applies.
        std::mutex            g_appliedLock;
        std::vector<uint32_t> g_applied;
        std::atomic<uint32_t> g_appliedCount{ 0 };
        std::atomic<uint64_t> g_last{ 0 }; // area << 32 | stock area
        std::string           g_status;

        void Clear()
        {
            std::lock_guard<std::mutex> lock(g_appliedLock);
            g_applied.clear();
            g_appliedCount = 0;
            g_last = 0;
        }

        void Sort()
        {
            std::sort(g_rows.begin(), g_rows.end(), [](const Row& a, const Row& b) { return a.area < b.area; });
        }
    }

    void Load(const WXL_SeyrisCdbcApi* cdbc)
    {
        g_rows.clear();
        Clear();
        ++g_generation;
        if (!cdbc || !cdbc->HasFeature("cdbc-load")) { g_loadStatus = "wxl-seyris-tools (cdbc) not available: no remap"; return; }
        char err[256] = {};
        void* table = cdbcload::LoadAppendOnly(cdbc, kDef, err, sizeof(err));
        if (!table) { g_loadStatus = std::string("no remap (") + err + ")"; return; }
        const uint32_t count = cdbc->RowCount(table);
        for (uint32_t i = 0; i < count; ++i)
        {
            const void* rec = cdbc->RowAt(table, i);
            if (!rec) continue;
            Row r;
            r.area = cdbc->Value(table, rec, "AreaID", 0);
            r.continent = cdbc->Value(table, rec, "ContinentID", 0);
            r.parent = cdbc->Value(table, rec, "ParentID", 0);
            r.stockArea = cdbc->Value(table, rec, "StockAreaID", 0);
            if (r.area) g_rows.push_back(r);
        }
        cdbc->Release(table);
        Sort();
        char line[96];
        std::snprintf(line, sizeof(line), "%zu row(s)", g_rows.size());
        g_loadStatus = line;
    }

    void SetRows(const Row* rows, size_t count)
    {
        g_rows.assign(rows, rows + (rows ? count : 0));
        Sort();
        Clear();
        ++g_generation;
        char line[96];
        std::snprintf(line, sizeof(line), "%zu row(s) (set)", g_rows.size());
        g_loadStatus = line;
    }

    bool Next(uint32_t area, uint32_t continent, uint32_t parent, uint32_t& next)
    {
        if (g_rows.empty()) return false;
        const auto it = std::lower_bound(g_rows.begin(), g_rows.end(), area, [](const Row& r, uint32_t a) { return r.area < a; });
        if (it == g_rows.end() || it->area != area || it->continent != continent || it->parent != parent) return false;
        next = it->stockArea;
        g_last = (static_cast<uint64_t>(area) << 32) | it->stockArea;
        // Counted once per area (uncontended: the area chain is walked about once a frame, the caches above it hold the rest).
        std::lock_guard<std::mutex> lock(g_appliedLock);
        if (std::find(g_applied.begin(), g_applied.end(), area) == g_applied.end() && g_applied.size() < 4096)
        {
            g_applied.push_back(area);
            g_appliedCount = static_cast<uint32_t>(g_applied.size());
        }
        return true;
    }

    size_t   RowCount() { return g_rows.size(); }
    uint32_t Generation() { return g_generation; }
    uint32_t AppliedAreas() { return g_appliedCount.load(); }
    uint32_t LastArea() { return static_cast<uint32_t>(g_last.load() >> 32); }
    uint32_t LastStockArea() { return static_cast<uint32_t>(g_last.load() & 0xFFFFFFFFu); }

    const char* Status()
    {
        // Built on the caller's thread (the panel, the snapshot, the log: the main thread).
        char line[192];
        const uint32_t applied = AppliedAreas();
        if (g_rows.empty()) std::snprintf(line, sizeof(line), "AreaRemap.cdbc: %s", g_loadStatus.c_str());
        else if (!applied) std::snprintf(line, sizeof(line), "AreaRemap.cdbc: %s; none applied this session (the stock areas)", g_loadStatus.c_str());
        else std::snprintf(line, sizeof(line), "AreaRemap.cdbc: %s; applied to %u area(s) this session, last %u -> %u", g_loadStatus.c_str(), applied,
                           LastArea(), LastStockArea());
        g_status = line;
        return g_status.c_str();
    }
}
