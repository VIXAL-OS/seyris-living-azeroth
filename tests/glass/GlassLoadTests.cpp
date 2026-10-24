// The glass tables through the real reader: Living Azeroth's own loading (features/Glass: glass::Load) over
// wxl-seyris-tools' cdbc reader -- its Cdbc.cpp linked in, and with --dll a wxl-seyris-tools.dll's published
// "seyris.cdbc" as the game hands it to LA (WXL_Query, WXL_Load with a stand-in core, GetInterface) -- on copies of the
// tables in a scratch folder laid out as the client's (DBFilesClient\*.cdbc: the reader opens loose files relative to
// the working folder, the client's root in the game).
//  1. The reader's Value: its last argument is the array element, not a default (any other than 0 reads 0).
//  2. glass::Load: each table read raw (its WDBC bytes) and what Load kept checked against it. Every window of a clear,
//     leaded, frosted or stained kind with a WMO is in the window table and its group's grid is found (a stained window's
//     in its group's stained grid, and never in the other); every whole placement (a unique ID, group -1) is a placement
//     and is found at its own origin (placed, not by WMO); a city group under its placement; every style's class is the
//     file's; GlassMaterials' corner that isn't glass (NotGlassU, NotGlassV) on the eight human window atlases (the
//     forge's fire slots) and on nothing else, and Goldshire's smithy left its four real windows; GlassLead.cdbc's panes
//     (the see-through redraw's lead mask) on those eight textures, 19 rectangles each, and (2026-10-22) stained glass's
//     and Northrend's arched windows' five, each with its own Dark: the arched windows' the whole texture, the stained
//     sheets' the lattice's four lights and three quadrants. The lines the lights panel shows
//     say so, and LogLoad's one line to the log (info).
//  2b. The window lights' cells (features/Windows: WindowLights.cdbc, WindowRooms.cdbc through the same reader): each
//     light's share lit as its cells say, with stained glass's rooms (its stained cells, not the ones read only without
//     them) and without (StainedRooms off: the cells as they were before), against the rules worked here from the raw rows;
//     a light with neither kind of cell the same both ways.
//  3. An older GlassWindows.cdbc (without its last column, Thickness) loads every window the same way.
//  4. The tables as the bug read them (every window's kind 0, every building's group 0; Value given a default as its
//     element, 2026-10-19): Load names the rows it left out and why, and LogLoad warns.
// Run: glass_load_tests <folder with the tables (the repo's data, or the game's DBFilesClient)> [--dll <wxl-seyris-tools.dll>]
// Never reads a table it didn't copy: the reader falls back to the client's file layer, which a test can't call.
#include "env/CdbcLoad.hpp"
#include "env/GlassModel.hpp"
#include "env/GlassRooms.hpp"
#include "features/Glass.hpp"
#include "features/Windows.hpp"
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

    uint32_t Bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

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

    const char* const kTables[] = { "GlassMaterials.cdbc", "GlassPanes.cdbc", "RoomStyles.cdbc", "GlassBuildings.cdbc", "GlassWindows.cdbc",
                                    "WindowLights.cdbc", "WindowRooms.cdbc", "GlassLead.cdbc", "SeeThroughBuildings.cdbc" };

    // What the files say Load should keep (tools/gen_glass.py's columns).
    struct Expected
    {
        size_t windows = 0, grids = 0, styles = 0, buildingRows = 0, placements = 0, cityGroups = 0, stained = 0, frosted = 0;
        std::set<uint64_t> windowGroups;               // (PathHash, group | kStainedGrid for stained windows) of the windows kept
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
            if (!path || !(kind == gl::kClear || kind == gl::kLeaded || kind == gl::kFrosted || kind == gl::kStained)) continue;
            ++e.windows;
            e.stained += kind == gl::kStained ? 1 : 0;
            e.frosted += kind == gl::kFrosted ? 1 : 0;
            e.windowGroups.insert((static_cast<uint64_t>(path) << 32) | windows.At(r, 2) | (kind == gl::kStained ? gl::WindowTable::kStainedGrid : 0u));
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
        size_t keptStained = 0, keptFrosted = 0;
        for (const gl::Window& w : wt.All()) { keptStained += w.kind == gl::kStained ? 1 : 0; keptFrosted += w.kind == gl::kFrosted ? 1 : 0; }
        std::snprintf(line, sizeof(line), "%s: GlassWindows.cdbc: %zu windows kept of %zu clear, leaded, frosted or stained with a WMO (frosted %zu of %zu, stained %zu "
                      "of %zu); %zu WMO groups of %zu", via, wt.Windows(), windowsWanted, keptFrosted, e.frosted, keptStained, e.stained, wt.Grids(), e.grids);
        Check(wt.Windows() > 0 && wt.Windows() == windowsWanted && wt.Grids() == e.grids && keptStained == e.stained && keptFrosted == e.frosted &&
              e.stained > 1000, line);
        // each grid found as its batch asks for it: a stained window's with `stained`, never without (it's apart from the others)
        size_t found = 0, crossed = 0;
        for (uint64_t key : e.windowGroups)
        {
            const uint32_t path = static_cast<uint32_t>(key >> 32), group = static_cast<uint32_t>(key) & ~gl::WindowTable::kStainedGrid;
            const bool stained = (static_cast<uint32_t>(key) & gl::WindowTable::kStainedGrid) != 0;
            found += wt.Find(path, group, stained) ? 1 : 0;
            if (stained && !e.windowGroups.count((static_cast<uint64_t>(path) << 32) | group)) crossed += wt.Find(path, group) ? 1 : 0;
        }
        std::snprintf(line, sizeof(line), "%s: each window's group grid found (%zu of %zu; a stained-only group found without `stained` %zu)", via, found,
                      e.windowGroups.size(), crossed);
        Check(found == e.windowGroups.size() && found > 0 && crossed == 0, line);

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

        // GlassMaterials' corner that isn't glass (NotGlassU, NotGlassV, 2026-10-20): the human window atlases' fourth
        // quadrant, a smithy's forge mouth, as the reader gives it; every other texture none. And the forge's slots no
        // window's: Goldshire's smithy keeps its four real windows.
        size_t cornered = 0, quadrants = 0;
        for (uint32_t i = 1; const gl::Material* m = gl::MaterialAt(i); ++i)
        {
            cornered += m->notGlass[0] < 1.0f || m->notGlass[1] < 1.0f ? 1 : 0;
            quadrants += m->notGlass[0] == 0.5f && m->notGlass[1] == 0.5f ? 1 : 0;
        }
        const gl::Material* elwynn = gl::MaterialAt(gl::MaterialOf("Dungeons\\Textures\\Windows\\MM_Elwynn_Wnd_Ext__01.blp"));
        const uint32_t smithy = gl::PathHash("world\\wmo\\azeroth\\buildings\\goldshireblacksmith\\goldshireblacksmith.wmo");
        size_t smithyWindows = 0;
        for (const gl::Window& w : wt.All()) smithyWindows += w.path == smithy ? 1 : 0;
        std::snprintf(line, sizeof(line), "%s: GlassMaterials.cdbc: %zu textures with a corner that isn't glass (%zu the fourth quadrant; Elwynn's %.2f %.2f); "
                      "Goldshire's smithy %zu windows", via, cornered, quadrants, elwynn ? elwynn->notGlass[0] : -1.0f, elwynn ? elwynn->notGlass[1] : -1.0f,
                      smithyWindows);
        Check(cornered == 8 && quadrants == 8 && elwynn && elwynn->notGlass[0] == 0.5f && elwynn->notGlass[1] == 0.5f && smithyWindows == 4 &&
              status.find("8 with a corner that isn't glass") != std::string::npos, line);

        // GlassLead.cdbc (2026-10-21): the see-through redraw's panes as the reader gives them, on the eight human window
        // atlases (the inn's among them, by any case); since 2026-10-22 also on stained glass's and Northrend's arched
        // windows' five, each its own Dark (the abbey's 0.4, the others' 0.5): the arched windows one rectangle, the whole
        // texture, the stained sheets seven; on nothing else; their texture's path as GlassMaterials has it.
        size_t ruled = 0, rects = 0;
        for (uint32_t i = 1; gl::MaterialAt(i); ++i)
            if (const gl::LeadRule* r = gl::LeadOf(i)) { ++ruled; rects += r->rects.size(); }
        auto own = [](const char* texture, size_t rects, float dark) {
            const gl::LeadRule* r = gl::LeadOf(gl::MaterialOf(texture));
            return r && r->rects.size() == rects && r->Whole() == (rects == 1) && std::fabs(r->dark - dark) < 1e-6f && r->line == gl::kLeadLine;
        };
        auto whole = [&](const char* texture, float dark) { return own(texture, 1, dark); };
        const bool wholeOk = own("Dungeons\\Textures\\Windows\\MM_Stained_01.blp", 7, 0.4f) && own("dungeons\\textures\\windows\\mm_redridge_stained_01.blp", 7, 0.5f) &&
                             whole("dungeons\\textures\\northrend\\humans\\nr_hu_window.blp", 0.5f) &&
                             whole("dungeons\\textures\\northrend\\humans\\nr_hu_window_half.blp", 0.5f) &&
                             whole("dungeons\\textures\\northrend\\humans\\nr_hu_interwindow.blp", 0.5f);
        const uint32_t innMaterial = gl::MaterialOf("Dungeons\\Textures\\Windows\\MM_Elwynn_Wnd_Ext__01.blp");
        const gl::LeadRule* inn = gl::LeadOf(innMaterial);
        const char* innTexture = gl::TextureOf(innMaterial);
        std::snprintf(line, sizeof(line), "%s: GlassLead.cdbc: %zu textures with panes, %zu rectangles; the inn's texture %zu (its first %.4f %.4f .. %.4f %.4f, "
                      "dark %.2f, line %.2f; its path %s); the five others %s", via, ruled, rects, inn ? inn->rects.size() : 0, inn ? inn->rects[0].u0 : -1.0f,
                      inn ? inn->rects[0].v0 : -1.0f, inn ? inn->rects[0].u1 : -1.0f, inn ? inn->rects[0].v1 : -1.0f, inn ? inn->dark : -1.0f, inn ? inn->line : -1.0f,
                      innTexture ? innTexture : "none", wholeOk ? "as written" : "not as written");
        Check(ruled == 13 && rects == 169 && wholeOk && inn && inn->rects.size() == 19 && inn->rects[0].u0 == 32.0f / 256 && inn->rects[0].v0 == 8.0f / 256 &&
              inn->rects[0].u1 == 61.0f / 256 && inn->rects[0].v1 == 57.0f / 256 && inn->dark == gl::kLeadDark && inn->line == gl::kLeadLine && innTexture &&
              std::string(innTexture) == "dungeons\\textures\\windows\\mm_elwynn_wnd_ext__01.blp" &&
              status.find("GlassLead.cdbc: 169 rectangles over 13 textures;") != std::string::npos, line);

        // SeeThroughBuildings.cdbc (2026-10-22): the see-through windows' buildings as the reader gives them, by their WMO's
        // path in any case: the inns, the two-storey houses, the mage towers, the farmhouses and Redridge's prison camp
        // headquarters, from outside and from inside; not Stormwind, a barn, a smithy or the inn's closed variant. Since
        // 2026-10-22 the stained glass's too: the chapel from outside and inside, Northrend's town hall from outside only; and
        // the frosted glass's (Phase B): the dwarven house from both, the zeppelin house from inside only; and (2026-10-23,
        // Phase C, between rooms) Ironforge by its groups: the smithy's (47) 0x4, the Great Forge's (65, its ring) 0x4 since the
        // first in-game report, the Commons' hall by the Stonefire Tavern (60) none, the city 0x4 in all.
        auto see = [](const char* wmo) { return gl::SeeThroughFlags(gl::PathHash(wmo)); };
        const uint32_t seeInn = see("World\\WMO\\Azeroth\\Buildings\\GoldshireInn\\GoldshireInn.wmo");
        const uint32_t seeHouse = see("world\\wmo\\azeroth\\buildings\\duskwood_humantwostory\\duskwood_humantwostory.wmo");
        const uint32_t seeTower = see("world\\wmo\\azeroth\\buildings\\magetower\\magetower.wmo");
        const uint32_t seeFarm = see("world\\wmo\\buildings\\oldstrat_farm.wmo");
        const uint32_t seeChapel = see("World\\wmo\\Azeroth\\Buildings\\Chapel\\RedridgeChapel.wmo");
        const uint32_t seeHall = see("world\\wmo\\northrend\\buildings\\human\\nd_human_townhall\\nd_human_townhall.wmo");
        const uint32_t seeHovel = see("World\\WMO\\KhazModan\\Buildings\\Dwarven_2story_Hovel\\Snow_2story\\Snow_2story.wmo");
        const uint32_t seeZeppelin = see("world\\wmo\\kalimdor\\buildings\\orczeppelinhouse\\orczeppelinhouse.wmo");
        const uint32_t ironforge = gl::PathHash("World\\wmo\\KhazModan\\Cities\\Ironforge\\ironforge.wmo");
        const uint32_t seeCity = gl::SeeThroughFlags(ironforge), seeSmithy = gl::SeeThroughGroupFlags(ironforge, 47);
        const uint32_t seeForge = gl::SeeThroughGroupFlags(ironforge, 65);
        const uint32_t seeCommons = gl::SeeThroughGroupFlags(ironforge, 60), seeInnGroup = gl::SeeThroughGroupFlags(gl::PathHash("world\\wmo\\azeroth\\buildings\\goldshireinn\\goldshireinn.wmo"), 5);
        const uint32_t seeNone = see("world\\wmo\\azeroth\\buildings\\stormwind\\stormwind.wmo") | see("world\\wmo\\azeroth\\buildings\\human_barn_silo\\barn.wmo") |
                                 see("world\\wmo\\azeroth\\buildings\\goldshireblacksmith\\goldshireblacksmith.wmo") |
                                 see("world\\wmo\\azeroth\\buildings\\goldshireinn\\goldshireinn_closed.wmo");
        std::snprintf(line, sizeof(line), "%s: SeeThroughBuildings.cdbc: %zu buildings; the inn's flags %u, a two-storey house's %u, a mage tower's %u, a "
                      "farmhouse's %u, the chapel's %u, Northrend's town hall's %u, the dwarven house's %u, the zeppelin house's %u; Stormwind, a barn, a "
                      "smithy, the closed inn %u; Ironforge %u (%zu group rows), its smithy's group %u, the Great Forge's %u, the Commons' %u; the inn's group 5 %u (the "
                      "WMO's)", via,
                      gl::SeeThroughBuildings(), seeInn, seeHouse, seeTower, seeFarm, seeChapel, seeHall, seeHovel, seeZeppelin, seeNone, seeCity,
                      gl::SeeThroughGroupRows(), seeSmithy, seeForge, seeCommons, seeInnGroup);
        Check(gl::SeeThroughBuildings() == 48 && seeInn == 3 && seeHouse == 3 && seeTower == 3 && seeFarm == 3 && seeChapel == 3 && seeHall == 1 &&
              seeHovel == 3 && seeZeppelin == 2 && seeNone == 0 && seeCity == 4 && gl::SeeThroughGroupRows() == 14 && seeSmithy == 4 && seeForge == 4 && seeCommons == 0 &&
              seeInnGroup == 3 && status.find("SeeThroughBuildings.cdbc: 48 buildings") != std::string::npos, line);

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
                      "leaded, frosted or stained glass", w.rows, w.rows, w.rows);
        Check(logged.first == WXL_LOG_WARN && gl::LoadProblem() && logged.second.find(want) != std::string::npos &&
              logged.second.find("0 placements") != std::string::npos && logged.second.find("(none placed: every building by its WMO)") != std::string::npos,
              std::string(via) + ": no window kept and no placement: a warning naming the rows left out and why");
    }

    // 2b. The window lights' cells, through the same reader: each light's share lit with and without stained glass's rooms
    // against the rules worked here from the raw rows (features/Windows' choice of cells; env/GlassModel's band and hash).
    void LightTests(const WXL_SeyrisCdbcApi* cdbc, const char* via, const fs::path& db)
    {
        Raw lights, cells;
        if (!ReadRaw(db / "WindowLights.cdbc", lights) || !ReadRaw(db / "WindowRooms.cdbc", cells)) { Check(false, std::string(via) + ": the window lights' tables read raw"); return; }
        namespace wn = wxl_livingazeroth::windows;
        gl::Settings& gs = gl::Config();
        const gl::Settings saved = gs;
        gs.varietyStrength = 4.0f;   // (by day a few rooms dark: four times the share, so the shares differ)
        if (!gl::PatchedVariants()) gl::NotePatchedVariant();   // (the blocks in the shaders: the rooms hide the glow)
        std::map<uint32_t, std::vector<uint32_t>> byLight;
        for (uint32_t r = 0; r < cells.rows; ++r) byLight[cells.At(r, 1)].push_back(r);
        // A light's share as the rules have it: its cells read in this state (stained glass's rooms or not), each lit by its
        // building's band now (a room's, frosted glass's, stained glass's; none for a dungeon's frosted glass or titan glass
        // lit from behind), the rest of its glass lit. (By day: no flicker.)
        auto expected = [&](uint32_t light, bool stainedOn) {
            std::vector<gl::LightCell> lc;
            const gl::DarkBand allLit = gl::Band(0.0f, 0.0f);
            for (uint32_t r : byLight[light])
            {
                const uint32_t flags = cells.At(r, 7);
                if (flags & (stainedOn ? wn::kCellUnstained : (wn::kCellStained | wn::kCellWithStained))) continue;
                const gl::Building* b = cells.At(r, 2) ? gl::Buildings().ById(cells.At(r, 2)) : nullptr;
                const gl::RoomStyle* st = b ? gl::StyleById(b->style) : nullptr;
                if (!st) b = nullptr;
                const uint32_t seed = st ? b->seed : cells.At(r, 6), bflags = b ? b->flags : 0;
                gl::LightCell c;
                c.hash = gl::LitHash(static_cast<float>(static_cast<int32_t>(cells.At(r, 3))), static_cast<float>(static_cast<int32_t>(cells.At(r, 4))),
                                     static_cast<float>(seed & 1023u), static_cast<float>((seed >> 10) & 1023u));
                c.share = cells.F(r, 5);
                const gl::RoomStyle& style = st ? *st : gl::DefaultStyle();
                if (flags & wn::kCellStained) c.band = gl::StainedHideGlow() && !(gs.titanBacklit && (bflags & gl::kBuildingBacklit)) ? gl::BandNow(style, bflags) : allLit;
                else if (flags & wn::kCellFrosted) c.band = gl::FrostedHideGlow() && !(bflags & gl::kBuildingDungeon) ? gl::FrostedBandNow(style, b) : allLit;
                else c.band = gl::RoomsHideGlow() ? gl::BandNow(style, bflags) : allLit;
                if (c.share > 0.0f && std::isfinite(c.share)) lc.push_back(c);
            }
            return gl::LitShare(lc.data(), lc.size());
        };
        int checked = 0, offWrong = 0, onWrong = 0, stainedLights = 0, stainedDim = 0, stainedDimOff = 0;
        std::string worst;
        for (int pass = 0; pass < 2; ++pass)
        {
            gs.stainedRooms = pass;
            wn::Load(cdbc);   // (a fresh frame's shares)
            for (uint32_t map : { 0u, 1u, 530u, 571u })
            {
                std::vector<const wn::Window*> lightsOnMap;
                const float origin[3] = { 0, 0, 0 };
                wn::Near(map, origin, 24500.0f, true, 1u << 20, lightsOnMap);   // (the whole map: its corners 24,135 yd out)
                for (const auto* w : lightsOnMap)
                {
                    const float got = wn::LitShare(*w), want = expected(w->id, pass == 1);
                    bool stained = false, legacy = false;
                    for (uint32_t r : byLight[w->id]) { stained = stained || (cells.At(r, 7) & wn::kCellStained); legacy = legacy || (cells.At(r, 7) & (wn::kCellUnstained | wn::kCellWithStained)); }
                    if (Bits(got) != Bits(want))
                    {
                        (pass ? onWrong : offWrong)++;
                        if (worst.empty()) { char at[160]; std::snprintf(at, sizeof(at), " (light %u, stained rooms %s: %.4f against %.4f)", w->id, pass ? "on" : "off", got, want); worst = at; }
                    }
                    if (pass == 1 && stained) { ++stainedLights; stainedDim += got < 0.999f ? 1 : 0; }
                    if (pass == 0 && stained) stainedDimOff += legacy ? 0 : (got < 0.999f ? 1 : 0);
                    ++checked;
                }
            }
        }
        gs = saved;
        wn::Load(cdbc);
        char line[600];
        std::snprintf(line, sizeof(line), "%s: the window lights' cells: %d lights' shares (with stained glass's rooms and without) against the rules from the raw rows: "
                      "%d differ with, %d without%s; %d lights over stained glass, %d of them dimmed by its dark rooms (by day, 4 x the dark share), %d of those "
                      "with only stained cells dimmed without", via, checked, onWrong, offWrong, worst.c_str(), stainedLights, stainedDim, stainedDimOff);
        Check(checked > 5000 && onWrong == 0 && offWrong == 0 && stainedLights > 1000 && stainedDim > 50, line);
    }

    void Run(const WXL_SeyrisCdbcApi* cdbc, const char* via, const fs::path& src, const fs::path& db, const Expected& e, const Raw& windows, const Raw& buildings)
    {
        Check(cdbc && cdbc->HasFeature && cdbc->HasFeature("cdbc-load"), std::string(via) + ": seyris.cdbc with cdbc-load");
        if (!cdbc || !cdbc->HasFeature("cdbc-load")) return;
        ValueTests(cdbc, via);
        LoadTests(cdbc, via, e, buildings, e.windows);
        LightTests(cdbc, via, db);

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
    Check(copied, "the five glass tables and the window lights' two copied to " + db.string());
    if (!copied) { std::printf("FAILED (%d failure%s)\n", g_failures, g_failures == 1 ? "" : "s"); return 1; }
    const fs::path cwd = fs::current_path();
    fs::current_path(root);

    Expected e;
    Raw windows, buildings;
    Check(ReadExpected(db, e, windows, buildings), "the tables read raw");
    std::printf("      the files: %zu windows (clear, leaded, frosted or stained with a WMO; %zu frosted, %zu stained) in %zu WMO groups; %zu styles in %zu "
                "classes; %zu building rows, %zu whole placements, %zu city group rows\n", e.windows, e.frosted, e.stained, e.grids, e.styles, e.classes.size(),
                e.buildingRows, e.placements, e.cityGroups);

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
