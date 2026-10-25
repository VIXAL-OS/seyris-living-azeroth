// AreaRemap.cdbc (env/AreaRemap, tools/gen_area_remap.py) through the real reader and LA's real lookups: an art pack's
// renumbered caves and dungeon entrances keep their zone's climate and fog, and a stock client is untouched.
//
// The client's AreaTable is stood in for by the DBC files themselves (world::SetAreaRecordSource: each record at its
// on-disk layout, as the client keeps it), once the stock tables and once the pack's (the files the install loads:
// tools/mpq_chain.py). The tables are read through wxl-seyris-tools' cdbc reader (its Cdbc.cpp linked in) from a scratch
// client root, as in the game: AreaRemap.cdbc, AreaClimate.cdbc, FogSettings.cdbc.
//  1. The stock client with the remap loaded: every area's chain and every WMO group's is the same as with no remap
//     (no row matches a stock record), and the status says none applied.
//  2. The pack's tables with no remap: how many WMO groups' climate or fog differ from stock (the bug: the caves).
//  3. The pack's tables with the remap: every WMO group the stock table gives an area, and every area of the pack's
//     table that the stock one has, resolves to the same AreaClimate row (climate::For) and the same FogSettings values
//     for every fog kind (fogsettings::Resolve) as on the stock client, on the same map. The status counts the
//     applied areas.
//  4. The places by name: Fargodeep Mine's new areas reach Elwynn Forest, Blackrock Mountain's reach Blackrock
//     Mountain, Uldaman's instance area ends at itself (not Badlands).
// Run: area_remap_tests <stock DBC folder> <pack DBC folder> <repo data folder>
//      (each DBC folder holds AreaTable.dbc and WMOAreaTable.dbc, any case)
#include "env/AreaRemap.hpp"
#include "env/CdbcLoad.hpp"
#include "env/Climate.hpp"
#include "env/FogSettings.hpp"
#include "env/WorldQuery.hpp"
#include "wxl_seyris/CdbcApi.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace la = wxl_livingazeroth;

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

    int __cdecl LinkedHasFeature(const char* name)
    {
        return name && (std::strcmp(name, "cdbc-load") == 0 || std::strcmp(name, "cdbc-find-by-field") == 0);
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

    // --- a WDBC file, records kept as the client keeps them (on-disk layout) -------------------------------------------
    struct Dbc
    {
        std::vector<uint8_t> bytes;
        uint32_t count = 0, fields = 0, recSize = 0, strSize = 0;
        const uint8_t* Rec(uint32_t i) const { return bytes.data() + 20 + i * recSize; }
        uint32_t U(uint32_t i, uint32_t col) const { uint32_t v; std::memcpy(&v, Rec(i) + col * 4, 4); return v; }
        std::string Str(uint32_t off) const
        {
            const size_t base = 20 + static_cast<size_t>(count) * recSize;
            if (off >= strSize) return {};
            return std::string(reinterpret_cast<const char*>(bytes.data() + base + off));
        }
    };

    bool ReadDbc(const fs::path& folder, const char* name, Dbc& d)
    {
        fs::path file;
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(folder, ec))
        {
            std::string f = e.path().filename().string();
            std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::string want = name;
            std::transform(want.begin(), want.end(), want.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (f == want) { file = e.path(); break; }
        }
        if (file.empty()) return false;
        std::ifstream in(file, std::ios::binary);
        d.bytes.assign(std::istreambuf_iterator<char>(in), {});
        if (d.bytes.size() < 20 || std::memcmp(d.bytes.data(), "WDBC", 4) != 0) return false;
        std::memcpy(&d.count, d.bytes.data() + 4, 4);
        std::memcpy(&d.fields, d.bytes.data() + 8, 4);
        std::memcpy(&d.recSize, d.bytes.data() + 12, 4);
        std::memcpy(&d.strSize, d.bytes.data() + 16, 4);
        return d.bytes.size() >= 20 + static_cast<size_t>(d.count) * d.recSize + d.strSize;
    }

    struct Tables
    {
        Dbc area, wmo;
        std::map<uint32_t, uint32_t> index; // area id -> record
        std::map<uint32_t, uint32_t> wmoArea; // WMOAreaTable id -> AreaTableID
        bool Read(const fs::path& folder)
        {
            if (!ReadDbc(folder, "AreaTable.dbc", area) || !ReadDbc(folder, "WMOAreaTable.dbc", wmo)) return false;
            for (uint32_t i = 0; i < area.count; ++i) index[area.U(i, 0)] = i;
            for (uint32_t i = 0; i < wmo.count; ++i) wmoArea[wmo.U(i, 0)] = wmo.U(i, 10);
            return true;
        }
        const uint8_t* Record(uint32_t id) const
        {
            const auto it = index.find(id);
            return it == index.end() ? nullptr : area.Rec(it->second);
        }
        uint32_t Continent(uint32_t id) const { const auto it = index.find(id); return it == index.end() ? 0 : area.U(it->second, 1); }
        std::string Name(uint32_t id) const { const auto it = index.find(id); return it == index.end() ? "?" : area.Str(area.U(it->second, 11)); }
    };

    Tables g_stock, g_pack;
    const Tables* g_current = nullptr;
    const uint8_t* __cdecl CurrentRecord(uint32_t id) { return g_current ? g_current->Record(id) : nullptr; }

    std::vector<uint32_t> Chain(uint32_t area)
    {
        uint32_t c[la::world::kMaxAreaChain];
        const int n = la::world::AreaChain(area, c, la::world::kMaxAreaChain);
        return std::vector<uint32_t>(c, c + n);
    }
    std::vector<uint32_t> StockIds(const std::vector<uint32_t>& chain)
    {
        std::vector<uint32_t> out;
        for (uint32_t a : chain) if (g_stock.index.count(a)) out.push_back(a);
        return out;
    }
    std::string Text(const std::vector<uint32_t>& chain)
    {
        std::string s;
        for (uint32_t a : chain) s += (s.empty() ? "" : " < ") + g_pack.Name(a) + " " + std::to_string(a);
        return s.empty() ? "none" : s;
    }

    // What LA's lookups give a place: the climate row and the fog values for every kind, as one comparable record.
    struct Resolved
    {
        std::vector<float> v;
        bool operator==(const Resolved& o) const { return v == o.v; }
    };
    Resolved Resolve(uint32_t area, int map)
    {
        Resolved r;
        const la::climate::Row& c = la::climate::For(area, map);
        r.v = { c.dayTemp, c.nightTemp, c.seasonAmplitude, c.seasonOffset, c.humidity, c.fogginess, c.cloudBase, c.cloudTop, static_cast<float>(c.flags) };
        for (int kind = -1; kind < 5; ++kind)
        {
            const la::fogsettings::Values& f = la::fogsettings::Resolve(area, map, kind);
            const float add[] = { f.strength, f.density, f.thickness, f.buildUp, f.burnOff, f.windThin, f.windDrift, f.patchiness, f.weight,
                                  static_cast<float>(f.colorMode), f.color[0], f.color[1], f.color[2], f.saturation, f.brightness, static_cast<float>(f.flags) };
            r.v.insert(r.v.end(), std::begin(add), std::end(add));
        }
        return r;
    }

    // The places compared: every WMO group the stock table gives an area (on the map the pack's area is on: where the
    // group stands), and every area both tables have (on its own continent).
    struct Place { std::string what; uint32_t stockArea, packArea; int map; };
    std::vector<Place> Places()
    {
        std::vector<Place> out;
        for (const auto& [id, packArea] : g_pack.wmoArea)
        {
            const auto s = g_stock.wmoArea.find(id);
            if (s == g_stock.wmoArea.end() || !s->second || !packArea) continue;
            out.push_back({ "WMOAreaTable " + std::to_string(id), s->second, packArea, static_cast<int>(g_pack.Continent(packArea)) });
        }
        for (const auto& [id, rec] : g_pack.index)
            if (g_stock.index.count(id)) out.push_back({ "AreaTable " + std::to_string(id), id, id, static_cast<int>(g_stock.Continent(id)) });
        return out;
    }

    void Use(const Tables& t, const WXL_SeyrisCdbcApi* api, bool remap)
    {
        g_current = &t;
        la::world::SetAreaRecordSource(&CurrentRecord);
        if (remap) la::arearemap::Load(api);
        else la::arearemap::SetRows(nullptr, 0);
        la::climate::Load(api);       // clears its per-place cache
        la::fogsettings::Load(api);
    }
}

int main(int argc, char** argv)
{
    if (argc < 4) { std::printf("usage: area_remap_tests <stock DBC folder> <pack DBC folder> <repo data folder>\n"); return 2; }
    Check(g_stock.Read(argv[1]), std::string("the stock AreaTable and WMOAreaTable read from ") + argv[1]);
    Check(g_pack.Read(argv[2]), std::string("the pack's AreaTable and WMOAreaTable read from ") + argv[2]);
    if (g_failures) { std::printf("FAILED (%d)\n", g_failures); return 1; }

    // The scratch client root with copies of the tables (the reader opens DBFilesClient\*.cdbc relative to it).
    char tmp[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tmp);
    const fs::path root = fs::path(tmp) / ("la-area-remap-" + std::to_string(GetCurrentProcessId()));
    const fs::path db = root / "DBFilesClient";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(db, ec);
    bool copied = !ec;
    for (const char* name : { "AreaRemap.cdbc", "AreaClimate.cdbc", "FogSettings.cdbc" })
        copied = copied && fs::copy_file(fs::path(argv[3]) / name, db / name, fs::copy_options::overwrite_existing, ec);
    Check(copied, "AreaRemap, AreaClimate and FogSettings copied to " + db.string());
    if (!copied) { std::printf("FAILED (%d)\n", g_failures); return 1; }
    const fs::path cwd = fs::current_path();
    fs::current_path(root);
    const WXL_SeyrisCdbcApi api = LinkedApi();

    const std::vector<Place> places = Places();
    std::printf("      %zu places: the WMO groups the stock table gives an area and the areas both tables have\n", places.size());

    // 1. The stock client: the remap changes nothing.
    std::map<std::string, Resolved> stockResolved;
    std::map<uint32_t, std::vector<uint32_t>> stockChains;
    Use(g_stock, &api, false);
    for (const auto& [id, rec] : g_stock.index) stockChains[id] = Chain(id);
    for (const Place& p : places) stockResolved[p.what] = Resolve(p.stockArea, p.map);
    Use(g_stock, &api, true);
    Check(la::arearemap::RowCount() > 0, std::string("the remap loaded through the reader: ") + la::arearemap::Status());
    size_t chainDiff = 0, valueDiff = 0;
    for (const auto& [id, rec] : g_stock.index) chainDiff += Chain(id) != stockChains[id];
    for (const Place& p : places) valueDiff += !(Resolve(p.stockArea, p.map) == stockResolved[p.what]);
    Check(chainDiff == 0 && valueDiff == 0, "stock client with the remap: " + std::to_string(chainDiff) + " area chains and " +
          std::to_string(valueDiff) + " places' climate or fog differ from no remap (want 0 and 0)");
    Check(la::arearemap::AppliedAreas() == 0, std::string("stock client: ") + la::arearemap::Status());

    // 2. The pack's tables without the remap: the bug.
    Use(g_pack, &api, false);
    size_t before = 0, beforeWmo = 0;
    for (const Place& p : places)
        if (!(Resolve(p.packArea, p.map) == stockResolved[p.what])) { ++before; beforeWmo += p.what[0] == 'W'; }
    std::printf("      the pack's tables without the remap: %zu places' climate or fog differ from stock (%zu WMO groups)\n", before, beforeWmo);
    Check(before > 0, "without the remap the pack's caves lose their zone's rows (the bug is real in these tables)");

    // 3. With the remap.
    Use(g_pack, &api, true);
    size_t after = 0, chainsOff = 0;
    for (const Place& p : places)
    {
        const bool same = Resolve(p.packArea, p.map) == stockResolved[p.what];
        const std::vector<uint32_t> want = stockChains[p.stockArea];
        const std::vector<uint32_t> got = StockIds(Chain(p.packArea));
        if (!same || got != want)
        {
            if (after + chainsOff < 12)
                std::printf("      differs: %s area %u on map %d: %s, stock %s\n", p.what.c_str(), p.packArea, p.map, Text(Chain(p.packArea)).c_str(), Text(want).c_str());
            after += !same;
            chainsOff += got != want;
        }
    }
    Check(after == 0, "the pack's tables with the remap: " + std::to_string(after) + " places' climate or fog differ from stock (want 0)");
    Check(chainsOff == 0, "the pack's tables with the remap: " + std::to_string(chainsOff) + " chains (stock ids) differ from stock (want 0)");
    Check(la::arearemap::AppliedAreas() > 0, std::string("pack: ") + la::arearemap::Status());

    // 4. By name.
    auto has = [](const std::vector<uint32_t>& c, uint32_t a) { return std::find(c.begin(), c.end(), a) != c.end(); };
    for (uint32_t a : { 6001u, 6002u }) Check(has(Chain(a), 12), "Fargodeep Mine (" + std::to_string(a) + "): " + Text(Chain(a)));
    Check(has(Chain(6014), 25), "Blackrock Mountain (6014): " + Text(Chain(6014)));
    Check(Chain(1337) == std::vector<uint32_t>{ 1337 }, "Uldaman's instance area (1337, the pack's parent Badlands): " + Text(Chain(1337)));
    Check(has(Chain(6018), 1337) && !has(Chain(6018), 3), "Uldaman's entrance (6018): " + Text(Chain(6018)));

    la::world::SetAreaRecordSource(nullptr);
    fs::current_path(cwd);
    fs::remove_all(root, ec);
    std::printf(g_failures ? "FAILED (%d failure%s)\n" : "all passed\n", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
