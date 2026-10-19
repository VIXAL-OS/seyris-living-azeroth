// The glass tables through the real reader: Living Azeroth's own loading (features/Glass: glass::Load) over
// wxl-seyris-tools' cdbc reader -- its Cdbc.cpp linked in, and with --dll a wxl-seyris-tools.dll's published
// "seyris.cdbc" as the game hands it to LA (WXL_Query, WXL_Load with a stand-in core, GetInterface) -- on copies of the
// tables in a scratch folder laid out as the client's (DBFilesClient\*.cdbc: the reader opens loose files relative to
// the working folder, the client's root in the game).
//  1. The reader's Value: its last argument is the array element, not a default (any other than 0 reads 0).
//  2. glass::Load: each table read raw (its WDBC bytes) and what Load kept checked against it. Every window of a clear,
//     leaded or frosted kind with a WMO is in the window table and its group's grid is found; every whole placement (a
//     unique ID, group -1) is a placement and is found at its own origin (placed, not by WMO); a city group under its
//     placement; every style's class is the file's. The lines the lights panel shows say so, and LogLoad's one line
//     to the log (info).
//  3. An older GlassWindows.cdbc (without its last column, Thickness) loads every window the same way.
//  4. The tables as the bug read them (every window's kind 0, every building's group 0; Value given a default as its
//     element, 2026-10-19): Load names the rows it left out and why, and LogLoad warns.
// Run: glass_load_tests <folder with the tables (the repo's data, or the game's DBFilesClient)> [--dll <wxl-seyris-tools.dll>]
// Never reads a table it didn't copy: the reader falls back to the client's file layer, which a test can't call.
#include "env/CdbcLoad.hpp"
#include "env/GlassModel.hpp"
#include "env/GlassRooms.hpp"
#include "features/Glass.hpp"
#include "wxl/PluginApi.h"
#include "wxl_seyris/CdbcApi.hpp"

#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace gl = wxl_livingazeroth::glass;

// wxl-seyris-tools' reader (its src/Cdbc.cpp, linked in; the functions its Extension.cpp publishes as "seyris.cdbc").
namespace wxl_seyris::cdbc
{
    void*       Load(const WXL_SeyrisCdbcDefinition* def, char* errorBuf, size_t errorBufSize);
    void        Release(void* table);
    uint32_t    RowCount(void* table);
    const void* RowAt(void* table, uint32_t index);
    const void* FindRow(void* table, uint32_t id);
    uint32_t    Value(void* table, const void* row, const char* field, uint32_t element);
    const char* GetString(void* table, const void* row, const char* field);
    size_t      FieldIndex(void* table, const char* field);
    const void* FindRowByField(void* table, const char* field, uint32_t value, const void* after);
    void*       RowAdd(void* table);
    int         RowUpdateValue(void* table, void* row, const char* field, uint32_t value);
    int         RowUpdateString(void* table, void* row, const char* field, const char* value);
    int         RowDelete(void* table, void* row);
    int         Save(void* table, char* errorBuf, size_t errorBufSize);
    void*       Reload(void* table, char* errorBuf, size_t errorBufSize);
}

namespace
{
    int g_failures = 0;

    void Check(bool ok, const std::string& what)
    {
        std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++g_failures;
    }

    // --- the reader, linked in: the interface as wxl-seyris-tools' Extension.cpp publishes it ------------------------
    int __cdecl LinkedHasFeature(const char* name)
    {
        return name && (std::strcmp(name, "cdbc-load") == 0 || std::strcmp(name, "cdbc-find-by-field") == 0 || std::strcmp(name, "cdbc-write") == 0);
    }

    WXL_SeyrisCdbcApi LinkedApi()
    {
        WXL_SeyrisCdbcApi api{};
        api.structSize = sizeof(WXL_SeyrisCdbcApi);
        api.major = 1; api.minor = 3; api.patch = 0;
        api.HasFeature = &LinkedHasFeature;
        api.Load = &wxl_seyris::cdbc::Load;
        api.Release = &wxl_seyris::cdbc::Release;
        api.RowCount = &wxl_seyris::cdbc::RowCount;
        api.RowAt = &wxl_seyris::cdbc::RowAt;
        api.FindRow = &wxl_seyris::cdbc::FindRow;
        api.Value = &wxl_seyris::cdbc::Value;
        api.GetString = &wxl_seyris::cdbc::GetString;
        api.FieldIndex = &wxl_seyris::cdbc::FieldIndex;
        api.FindRowByField = &wxl_seyris::cdbc::FindRowByField;
        api.RowAdd = &wxl_seyris::cdbc::RowAdd;
        api.RowUpdateValue = &wxl_seyris::cdbc::RowUpdateValue;
        api.RowUpdateString = &wxl_seyris::cdbc::RowUpdateString;
        api.RowDelete = &wxl_seyris::cdbc::RowDelete;
        api.Save = &wxl_seyris::cdbc::Save;
        api.Reload = &wxl_seyris::cdbc::Reload;
        return api;
    }

    // --- a stand-in core for a wxl-seyris-tools.dll (--dll): what its WXL_Load calls ----------------------------------
    std::map<std::string, std::pair<uint32_t, void*>> g_ifaces;
    void __cdecl FakeLog(int level, const char* tag, const char* fmt, ...)
    {
        std::printf("  [log %d] %s: ", level, tag ? tag : "?");
        va_list args;
        va_start(args, fmt);
        std::vprintf(fmt, args);
        va_end(args);
        std::printf("\n");
    }
    // LA's log lines (glass::LogLoad), kept for the checks.
    std::vector<std::pair<int, std::string>> g_logged;
    void __cdecl CaptureLog(int level, const char*, const char* fmt, ...)
    {
        char buf[4096];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        g_logged.emplace_back(level, buf);
    }
    void __cdecl FakeSubscribe(uint32_t, WXL_EventFn, void*) {}
    void __cdecl FakeEmit(uint32_t, const void*) {}
    int __cdecl FakeHookAttach(const char*, uintptr_t, void*, void**, int) { return 0; }
    int __cdecl FakeHookAttachByName(const char*, void*, void**, int) { return 0; }
    void __cdecl FakePublish(const char* name, uint32_t version, void* iface) { if (name) g_ifaces[name] = { version, iface }; }
    void* __cdecl FakeGetInterface(const char* name, uint32_t version)
    {
        const auto it = g_ifaces.find(name ? name : "");
        return it != g_ifaces.end() && it->second.first == version ? it->second.second : nullptr;
    }
    void __cdecl FakeAddPanel(const char*, WXL_PanelFn, void*) {}
    int __cdecl FakeIsOpen() { return 0; }
    void __cdecl FakeText(const char*) {}
    void __cdecl FakeSeparator() {}
    int __cdecl FakeButton(const char*) { return 0; }
    int __cdecl FakeCheckbox(const char*, int*) { return 0; }
    int __cdecl FakeSliderFloat(const char*, float*, float, float) { return 0; }
    int __cdecl FakeSliderInt(const char*, int*, int, int) { return 0; }
    int __cdecl FakeColorEdit(const char*, float*) { return 0; }
    void __cdecl FakeSameLine() {}
    int __cdecl FakeCombo(const char*, int*, const char* const*, int) { return 0; }
    int __cdecl FakeCollapsingHeader(const char*) { return 0; }
    int __cdecl FakeInputText(const char*, char*, size_t) { return 0; }

    WXL_Api StandInCore()
    {
        WXL_Api api{};
        api.structSize = sizeof(WXL_Api);
        api.apiVersion = WXL_API_VERSION;
        api.Log = &FakeLog;
        api.Subscribe = &FakeSubscribe;
        api.Emit = &FakeEmit;
        api.HookAttach = &FakeHookAttach;
        api.HookAttachByName = &FakeHookAttachByName;
        api.PublishInterface = &FakePublish;
        api.GetInterface = &FakeGetInterface;
        api.UiAddPanel = &FakeAddPanel;
        api.UiIsOpen = &FakeIsOpen;
        api.UiText = &FakeText;
        api.UiSeparator = &FakeSeparator;
        api.UiButton = &FakeButton;
        api.UiCheckbox = &FakeCheckbox;
        api.UiSliderFloat = &FakeSliderFloat;
        api.UiSliderInt = &FakeSliderInt;
        api.UiColorEdit = &FakeColorEdit;
        api.UiSameLine = &FakeSameLine;
        api.UiCombo = &FakeCombo;
        api.UiCollapsingHeader = &FakeCollapsingHeader;
        api.UiInputText = &FakeInputText;
        return api;
    }

    // --- a table's raw bytes (WDBC: the header, uint32 columns, the string block) ---------------------------------------
    struct Raw
    {
        uint32_t rows = 0, cols = 0;
        std::vector<uint32_t> cells;
        std::vector<char> strings;
        uint32_t At(uint32_t r, uint32_t c) const { return cells[static_cast<size_t>(r) * cols + c]; }
        float F(uint32_t r, uint32_t c) const { const uint32_t b = At(r, c); float f; std::memcpy(&f, &b, 4); return f; }
        const char* Str(uint32_t r, uint32_t c) const { const uint32_t o = At(r, c); return o < strings.size() ? &strings[o] : ""; }
    };

    bool ReadRaw(const fs::path& p, Raw& t)
    {
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        std::vector<char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (b.size() < 20 || std::memcmp(b.data(), "WDBC", 4) != 0) return false;
        uint32_t h[4];
        std::memcpy(h, b.data() + 4, 16);
        t.rows = h[0]; t.cols = h[1];
        if (h[2] != t.cols * 4 || b.size() < 20 + static_cast<size_t>(h[0]) * h[2] + h[3]) return false;
        t.cells.resize(static_cast<size_t>(t.rows) * t.cols);
        std::memcpy(t.cells.data(), b.data() + 20, t.cells.size() * 4);
        t.strings.assign(b.begin() + 20 + t.cells.size() * 4, b.begin() + 20 + t.cells.size() * 4 + h[3]);
        return true;
    }

    // The table without its last `drop` columns (an older file's layout; the strings stay as they are).
    bool WriteTrimmed(const Raw& t, uint32_t drop, const fs::path& p)
    {
        const uint32_t cols = t.cols - drop;
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        const uint32_t h[4] = { t.rows, cols, cols * 4, static_cast<uint32_t>(t.strings.size()) };
        f.write("WDBC", 4);
        f.write(reinterpret_cast<const char*>(h), 16);
        for (uint32_t r = 0; r < t.rows; ++r) f.write(reinterpret_cast<const char*>(&t.cells[static_cast<size_t>(r) * t.cols]), cols * 4);
        f.write(t.strings.data(), static_cast<std::streamsize>(t.strings.size()));
        return static_cast<bool>(f);
    }

    const char* const kTables[] = { "GlassMaterials.cdbc", "GlassPanes.cdbc", "RoomStyles.cdbc", "GlassBuildings.cdbc", "GlassWindows.cdbc" };

    // What the files say Load should keep (tools/gen_glass.py's columns).
    struct Expected
    {
        size_t windows = 0, grids = 0, styles = 0, buildingRows = 0, placements = 0, cityGroups = 0;
        std::set<uint64_t> windowGroups;               // (PathHash, group) of the windows kept
        std::map<uint32_t, uint32_t> styleClass;       // style ID -> class
        std::set<uint32_t> classes;
    };

    bool ReadExpected(const fs::path& db, Expected& e, Raw& windows, Raw& buildings)
    {
        Raw styles;
        if (!ReadRaw(db / "GlassWindows.cdbc", windows) || !ReadRaw(db / "GlassBuildings.cdbc", buildings) || !ReadRaw(db / "RoomStyles.cdbc", styles))
            return false;
        for (uint32_t r = 0; r < windows.rows; ++r)
        {
            const uint32_t kind = windows.At(r, 4), path = gl::PathHash(windows.Str(r, 1));
            if (!path || !(kind == gl::kClear || kind == gl::kLeaded || kind == gl::kFrosted)) continue;
            ++e.windows;
            e.windowGroups.insert((static_cast<uint64_t>(path) << 32) | windows.At(r, 2));
        }
        e.grids = e.windowGroups.size();
        e.buildingRows = buildings.rows;
        std::set<uint64_t> placed;
        for (uint32_t r = 0; r < buildings.rows; ++r)
        {
            const bool finite = std::isfinite(buildings.F(r, 3)) && std::isfinite(buildings.F(r, 4)) && std::isfinite(buildings.F(r, 5));
            if (buildings.At(r, 2) && buildings.At(r, 6) == gl::kWholePlacement && finite)
            {
                ++e.placements;
                placed.insert((static_cast<uint64_t>(buildings.At(r, 1)) << 32) | buildings.At(r, 2));
            }
        }
        for (uint32_t r = 0; r < buildings.rows; ++r)
            if (buildings.At(r, 2) && buildings.At(r, 6) != gl::kWholePlacement && placed.count((static_cast<uint64_t>(buildings.At(r, 1)) << 32) | buildings.At(r, 2)))
                ++e.cityGroups;
        e.styles = styles.rows;
        for (uint32_t r = 0; r < styles.rows; ++r) { e.styleClass[styles.At(r, 0)] = styles.At(r, 1); e.classes.insert(styles.At(r, 1)); }
        return true;
    }

    // 1. Value's last argument.
    void ValueTests(const WXL_SeyrisCdbcApi* cdbc, const char* via)
    {
        constexpr WXL_SeyrisCdbcField fields[] = { { "ID", 0, WXL_CDBC_FIELD_VALUE }, { "Kind", 4, WXL_CDBC_FIELD_VALUE } };
        const WXL_SeyrisCdbcDefinition def = { "GlassWindows", "DBFilesClient\\GlassWindows.cdbc", fields, 2 };
        char err[256] = {};
        void* t = cdbc->Load(&def, err, sizeof(err));
        Check(t != nullptr, std::string(via) + ": GlassWindows.cdbc loads (" + (t ? "ok" : err) + ")");
        if (!t) return;
        uint32_t nonzero = 0, elementOne = 0;
        for (uint32_t i = 0, n = cdbc->RowCount(t); i < n; ++i)
        {
            const void* row = cdbc->RowAt(t, i);
            nonzero += cdbc->Value(t, row, "Kind", 0) ? 1 : 0;
            elementOne += cdbc->Value(t, row, "Kind", 1) ? 1 : 0;
        }
        cdbc->Release(t);
        char line[256];
        std::snprintf(line, sizeof(line), "%s: Value's last argument is the array element, not a default: Kind with 0 reads %u rows nonzero, "
                      "with 1 (Kind::kClear, as a default) %u -- every row 0", via, nonzero, elementOne);
        Check(nonzero > 0 && elementOne == 0, line);
    }

    // glass::Load as Extension.cpp runs it on the first frame: then its line to the log (wxl-core.log in the game).
    std::pair<int, std::string> LoadAndLog(const WXL_SeyrisCdbcApi* cdbc)
    {
        static WXL_Api core = [] { WXL_Api a{}; a.structSize = sizeof(WXL_Api); a.apiVersion = WXL_API_VERSION; a.Log = &CaptureLog; return a; }();
        g_logged.clear();
        gl::Load(cdbc);
        gl::LogLoad(&core);
        return g_logged.size() == 1 ? g_logged[0] : std::pair<int, std::string>(-1, "");
    }

    // 2. glass::Load against the files.
    void LoadTests(const WXL_SeyrisCdbcApi* cdbc, const char* via, const Expected& e, const Raw& buildings, size_t windowsWanted)
    {
        const auto logged = LoadAndLog(cdbc);
        const std::string status = gl::Status();
        std::printf("      %s: status: %s\n", via, status.c_str());
        // (the reader's errors begin "cdbc: '<table>'")
        Check(status.find("not found") == std::string::npos && status.find("cdbc: '") == std::string::npos,
              std::string(via) + ": every table found and read (no \"not found\", no reader error in the status)");

        const gl::WindowTable& wt = gl::Windows();
        char line[400];
        std::snprintf(line, sizeof(line), "%s: GlassWindows.cdbc: %zu windows kept of %zu clear, leaded or frosted with a WMO; %zu WMO groups of %zu",
                      via, wt.Windows(), windowsWanted, wt.Grids(), e.grids);
        Check(wt.Windows() > 0 && wt.Windows() == windowsWanted && wt.Grids() == e.grids, line);
        size_t found = 0;
        for (uint64_t key : e.windowGroups) found += wt.Find(static_cast<uint32_t>(key >> 32), static_cast<uint32_t>(key)) ? 1 : 0;
        std::snprintf(line, sizeof(line), "%s: each window's group grid found (%zu of %zu)", via, found, e.windowGroups.size());
        Check(found == e.windowGroups.size() && found > 0, line);

        const gl::BuildingIndex& bi = gl::Buildings();
        std::snprintf(line, sizeof(line), "%s: GlassBuildings.cdbc: %zu rows (file %zu), %zu placements (file: %zu whole placements)", via, bi.Size(),
                      e.buildingRows, bi.Placements(), e.placements);
        Check(bi.Size() == e.buildingRows && bi.Placements() > 0 && bi.Placements() == e.placements, line);

        // Each whole placement found at its own origin, as an instance drawn there is (placed: its unique ID); a city's
        // group rows under their placement.
        size_t placedSelf = 0, placedAny = 0, groupsFound = 0;
        for (uint32_t r = 0; r < buildings.rows; ++r)
        {
            const uint32_t map = buildings.At(r, 1), uid = buildings.At(r, 2), group = buildings.At(r, 6);
            const float pos[3] = { buildings.F(r, 3), buildings.F(r, 4), buildings.F(r, 5) };
            if (!uid || !std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2])) continue;
            const gl::Building* b = bi.Find(map, pos, gl::PathHash(buildings.Str(r, 11)), group);
            if (group == gl::kWholePlacement)
            {
                placedAny += b && b->uniqueId ? 1 : 0;
                placedSelf += b && b->uniqueId == uid && b->map == map ? 1 : 0;
            }
            else groupsFound += b && b->uniqueId == uid && b->group == group ? 1 : 0;
        }
        std::snprintf(line, sizeof(line), "%s: placements found at their origins as placed (%zu of %zu; as themselves %zu)", via, placedAny, e.placements, placedSelf);
        Check(placedAny == e.placements && placedSelf * 100 >= e.placements * 99, line);
        std::snprintf(line, sizeof(line), "%s: a city's group rows found under their placement (%zu of %zu)", via, groupsFound, e.cityGroups);
        Check(groupsFound == e.cityGroups, line);

        size_t same = 0;
        std::set<uint32_t> loadedClasses;
        for (const auto& [id, cls] : e.styleClass)
            if (const gl::RoomStyle* s = gl::StyleById(id)) { same += s->cls == cls ? 1 : 0; loadedClasses.insert(s->cls); }
        std::snprintf(line, sizeof(line), "%s: RoomStyles.cdbc: every style's class the file's (%zu of %zu; %zu classes, file %zu)", via, same,
                      e.styleClass.size(), loadedClasses.size(), e.classes.size());
        Check(same == e.styleClass.size() && same > 0 && loadedClasses.size() == e.classes.size(), line);

        // The lights panel's lines (the in-game report's).
        const std::string rooms = gl::RoomLine(), windows = gl::WindowLine();
        std::printf("      %s: panel: %s\n      %s: panel: %s\n", via, rooms.c_str(), via, windows.c_str());
        char want[96];
        std::snprintf(want, sizeof(want), "%zu placements (%zu rows)", e.placements, e.buildingRows);
        Check(rooms.find(want) != std::string::npos, std::string(via) + ": the rooms line reads \"" + want + "\"");
        std::snprintf(want, sizeof(want), "GlassWindows.cdbc: %zu windows in %zu WMO groups", windowsWanted, e.grids);
        Check(windows.rfind(want, 0) == 0, std::string(via) + ": the windows line reads \"" + want + "\"");
        Check(windows.find("(0 waiting, 0 failed)") != std::string::npos, std::string(via) + ": the windows line counts the texture sets waiting (none before a frame)");

        // The log: one line, info, with what was kept.
        std::printf("      %s: log (level %d): %s\n", via, logged.first, logged.second.c_str());
        char placements[64], classes[64], kept[96];
        std::snprintf(placements, sizeof(placements), "%zu rows: %zu placements", e.buildingRows, e.placements);
        std::snprintf(classes, sizeof(classes), "%zu styles (%zu classes)", e.styles, e.classes.size());
        std::snprintf(kept, sizeof(kept), "; GlassWindows.cdbc: %zu windows in %zu WMO groups", windowsWanted, e.grids);
        const std::string& log = logged.second;
        Check(logged.first == WXL_LOG_INFO && !gl::LoadProblem() && log.rfind("glass: tables loaded: GlassMaterials.cdbc: ", 0) == 0 &&
              log.find(placements) != std::string::npos && log.find(classes) != std::string::npos && log.find(kept) != std::string::npos &&
              log.find("left out") == std::string::npos,
              std::string(via) + ": LogLoad writes one info line: every table, the placements, the styles' classes, the windows kept");
    }

    // The bug's tables, as the panel saw them (every window's kind read as 0, every building's group as 0): Load says
    // what it left out, and LogLoad warns.
    void DiagnosticTests(const WXL_SeyrisCdbcApi* cdbc, const char* via, const fs::path& db, const Raw& windows, const Raw& buildings)
    {
        Raw w = windows, b = buildings;
        for (uint32_t r = 0; r < w.rows; ++r) w.cells[static_cast<size_t>(r) * w.cols + 4] = 0;
        for (uint32_t r = 0; r < b.rows; ++r) b.cells[static_cast<size_t>(r) * b.cols + 6] = 0;
        if (!WriteTrimmed(w, 0, db / "GlassWindows.cdbc") || !WriteTrimmed(b, 0, db / "GlassBuildings.cdbc")) { Check(false, std::string(via) + ": the bug's tables written"); return; }
        const auto logged = LoadAndLog(cdbc);
        std::printf("      %s: the bug's tables: log (level %d): %s\n", via, logged.first, logged.second.c_str());
        char want[256];
        std::snprintf(want, sizeof(want), "GlassWindows.cdbc: %u rows, no window kept (no rooms behind the glass); %u rows left out: 0 without a WMO, %u not clear, "
                      "leaded or frosted glass", w.rows, w.rows, w.rows);
        Check(logged.first == WXL_LOG_WARN && gl::LoadProblem() && logged.second.find(want) != std::string::npos &&
              logged.second.find("0 placements") != std::string::npos && logged.second.find("(none placed: every building by its WMO)") != std::string::npos,
              std::string(via) + ": no window kept and no placement: a warning naming the rows left out and why");
    }

    void Run(const WXL_SeyrisCdbcApi* cdbc, const char* via, const fs::path& src, const fs::path& db, const Expected& e, const Raw& windows, const Raw& buildings)
    {
        Check(cdbc && cdbc->HasFeature && cdbc->HasFeature("cdbc-load"), std::string(via) + ": seyris.cdbc with cdbc-load");
        if (!cdbc || !cdbc->HasFeature("cdbc-load")) return;
        ValueTests(cdbc, via);
        LoadTests(cdbc, via, e, buildings, e.windows);

        // 3. An older GlassWindows.cdbc, without Thickness: LoadAppendOnly's probe and the fields that exist.
        const fs::path file = db / "GlassWindows.cdbc";
        if (WriteTrimmed(windows, 1, file))
        {
            gl::Load(cdbc);
            char line[200];
            std::snprintf(line, sizeof(line), "%s: GlassWindows.cdbc without its last column (Thickness): %zu windows of %zu", via, gl::Windows().Windows(), e.windows);
            Check(gl::Windows().Windows() == e.windows, line);
        }
        else Check(false, std::string(via) + ": an older GlassWindows.cdbc written");

        DiagnosticTests(cdbc, via, db, windows, buildings);

        // The tables as given back for the next run.
        std::error_code ec;
        bool back = true;
        for (const char* name : { "GlassWindows.cdbc", "GlassBuildings.cdbc" })
            back = back && fs::copy_file(src / name, db / name, fs::copy_options::overwrite_existing, ec);
        Check(back, std::string(via) + ": the tables put back");
    }
}

int main(int argc, char** argv)
{
    std::string data, dll;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--dll") == 0 && i + 1 < argc) dll = argv[++i];
        else data = argv[i];
    }
    if (data.empty()) { std::printf("usage: glass_load_tests <folder with the glass tables> [--dll <wxl-seyris-tools.dll>]\n"); return 2; }

    // The scratch client root: <temp>\la-glass-load-<pid>\DBFilesClient\ with copies of the tables.
    char tmp[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tmp);
    const fs::path root = fs::path(tmp) / ("la-glass-load-" + std::to_string(GetCurrentProcessId()));
    const fs::path db = root / "DBFilesClient";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(db, ec);
    bool copied = !ec;
    for (const char* name : kTables) copied = copied && fs::copy_file(fs::path(data) / name, db / name, fs::copy_options::overwrite_existing, ec);
    Check(copied, "the five glass tables copied to " + db.string());
    if (!copied) { std::printf("FAILED (%d failure%s)\n", g_failures, g_failures == 1 ? "" : "s"); return 1; }
    const fs::path cwd = fs::current_path();
    fs::current_path(root);

    Expected e;
    Raw windows, buildings;
    Check(ReadExpected(db, e, windows, buildings), "the tables read raw");
    std::printf("      the files: %zu windows (clear, leaded or frosted with a WMO) in %zu WMO groups; %zu styles in %zu classes; %zu building rows, "
                "%zu whole placements, %zu city group rows\n", e.windows, e.grids, e.styles, e.classes.size(), e.buildingRows, e.placements, e.cityGroups);

    const WXL_SeyrisCdbcApi linked = LinkedApi();
    Run(&linked, "linked Cdbc.cpp", data, db, e, windows, buildings);

    if (!dll.empty())
    {
        HMODULE module = LoadLibraryA(dll.c_str());
        const auto query = module ? reinterpret_cast<WXL_QueryFn>(GetProcAddress(module, "WXL_Query")) : nullptr;
        const auto load = module ? reinterpret_cast<WXL_LoadFn>(GetProcAddress(module, "WXL_Load")) : nullptr;
        Check(query && load, "--dll " + dll + ": exports WXL_Query and WXL_Load");
        static WXL_Api core = StandInCore();
        if (query && load && load(&core) == 1)
        {
            const auto* cdbc = static_cast<const WXL_SeyrisCdbcApi*>(FakeGetInterface(WXL_SEYRIS_CDBC_INTERFACE_NAME, WXL_SEYRIS_CDBC_INTERFACE_VERSION));
            char via[64];
            std::snprintf(via, sizeof(via), "the DLL's seyris.cdbc %u.%u.%u", cdbc ? cdbc->major : 0, cdbc ? cdbc->minor : 0, cdbc ? cdbc->patch : 0);
            Run(cdbc, via, data, db, e, windows, buildings);
        }
        else Check(false, "--dll: WXL_Load returns 1");
    }

    fs::current_path(cwd);
    fs::remove_all(root, ec);
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
