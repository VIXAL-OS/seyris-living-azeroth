// Offline tests for the ash and sand covers: GroundMaterial, GroundMaterialSelector and SurfaceCover
// load through the real table code (features/GroundMaterialTable, SurfaceCoverTable) over a small
// stand-in for wxl-seyris-tools' cdbc reader, and resolve for sample (area, texture) pairs the way the
// terrain asks; the snow and every other row stay exactly as they were; and the two look rules of
// features/CoverBlend.hpp leave snow's numbers bit-identical. No game, no WXL core.
// Also mud (2026-10-17): no mud on paved ground (GroundMaterial kFlagSealed, blend::MudAllowed), Elwynn's shallow ruts,
// the trench edge filter (blend::TrenchProfile), feet flattening earlier rims (blend::StampedRim) and rain filling ruts.
//
// Usage:  cover_table_tests <data dir> [<data dir before the change>]
//   (the repo's data/; the second one, e.g. the files from git HEAD, adds the unchanged-rows checks)
#include "features/CoverBlend.hpp"
#include "render/BlpColour.hpp"
#include "features/GroundMaterialTable.hpp"
#include "features/SurfaceCoverTable.hpp"
#include "env/WorldQuery.hpp"
#include "wxl_seyris/CdbcApi.hpp"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace mat = wxl_livingazeroth::materials;
namespace ct = wxl_livingazeroth::covertable;
namespace blend = wxl_livingazeroth::cover::blend;
namespace blp = wxl_livingazeroth::blp;

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at line " + std::to_string(__LINE__)); } while (false)

// The table code asks for an area's parent zones; texture rows are global, so the area alone will do.
namespace wxl_livingazeroth::world
{
    int AreaChain(uint32_t area, uint32_t* out, int max)
    {
        if (!area || max < 1) return 0;
        out[0] = area;
        return 1;
    }
}

namespace
{
    // --- a stand-in for the cdbc reader: the same rules as wxl-seyris-tools' Load (magic, flat 4-byte
    // columns, every named field's column must exist), files from one folder by their base name. ---
    std::string g_dir;

    struct Table
    {
        std::vector<uint8_t>     bytes;
        uint32_t                 rows = 0, columns = 0, recordSize = 0;
        const uint8_t*           records = nullptr;
        const char*              strings = nullptr;
        uint32_t                 stringSize = 0;
        std::vector<std::string> names;   // the definition's fields
        std::vector<uint32_t>    indices;
    };

    uint32_t U32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

    int __cdecl HasFeature(const char* name) { return std::strcmp(name, "cdbc-load") == 0; }

    void* __cdecl Load(const WXL_SeyrisCdbcDefinition* def, char* err, size_t errSize)
    {
        auto fail = [&](const std::string& why) -> void* { if (err && errSize) std::snprintf(err, errSize, "%s", why.c_str()); return nullptr; };
        std::string file = def->filename;
        const size_t slash = file.find_last_of("\\/");
        if (slash != std::string::npos) file = file.substr(slash + 1);
        FILE* f = std::fopen((g_dir + "\\" + file).c_str(), "rb");
        if (!f) return fail("cdbc: '" + std::string(def->name) + "' not found");
        auto* t = new Table;
        for (int c; (c = std::fgetc(f)) != EOF;) t->bytes.push_back(static_cast<uint8_t>(c));
        std::fclose(f);
        if (t->bytes.size() < 20 || std::memcmp(t->bytes.data(), "WDBC", 4) != 0) { delete t; return fail("bad magic"); }
        t->rows = U32(&t->bytes[4]);
        const uint32_t fieldCount = U32(&t->bytes[8]);
        t->recordSize = U32(&t->bytes[12]);
        t->stringSize = U32(&t->bytes[16]);
        if (t->recordSize == 0 || t->recordSize % 4 || fieldCount * 4 != t->recordSize) { delete t; return fail("bad layout"); }
        if (20ull + static_cast<uint64_t>(t->rows) * t->recordSize + t->stringSize > t->bytes.size()) { delete t; return fail("truncated"); }
        t->columns = t->recordSize / 4;
        for (uint32_t i = 0; i < def->fieldCount; ++i)
            if (def->fields[i].index >= t->columns)
            {
                const std::string why = "cdbc: '" + std::string(def->name) + "' field '" + def->fields[i].name + "' has index " +
                                        std::to_string(def->fields[i].index) + " but the table only has " + std::to_string(t->columns) + " columns";
                delete t;
                return fail(why);
            }
        for (uint32_t i = 0; i < def->fieldCount; ++i) { t->names.push_back(def->fields[i].name); t->indices.push_back(def->fields[i].index); }
        t->records = t->bytes.data() + 20;
        t->strings = reinterpret_cast<const char*>(t->records + static_cast<size_t>(t->rows) * t->recordSize);
        return t;
    }
    void __cdecl Release(void* t) { delete static_cast<Table*>(t); }
    uint32_t __cdecl RowCount(void* t) { return static_cast<Table*>(t)->rows; }
    const void* __cdecl RowAt(void* t, uint32_t i)
    {
        const Table* tb = static_cast<Table*>(t);
        return i < tb->rows ? tb->records + static_cast<size_t>(i) * tb->recordSize : nullptr;
    }
    size_t __cdecl FieldIndex(void* t, const char* field)
    {
        const Table* tb = static_cast<Table*>(t);
        for (size_t i = 0; i < tb->names.size(); ++i) if (tb->names[i] == field) return i;
        return static_cast<size_t>(-1);
    }
    uint32_t __cdecl Value(void* t, const void* row, const char* field, uint32_t)
    {
        const size_t i = FieldIndex(t, field);
        return i == static_cast<size_t>(-1) ? 0 : U32(static_cast<const uint8_t*>(row) + 4 * static_cast<Table*>(t)->indices[i]);
    }
    const char* __cdecl GetString(void* t, const void* row, const char* field)
    {
        const Table* tb = static_cast<Table*>(t);
        const uint32_t offset = Value(t, row, field, 0);
        return offset < tb->stringSize ? tb->strings + offset : "";
    }

    WXL_SeyrisCdbcApi MakeApi()
    {
        WXL_SeyrisCdbcApi api{};
        api.structSize = sizeof(api);
        api.major = 1;
        api.HasFeature = HasFeature;
        api.Load = Load;
        api.Release = Release;
        api.RowCount = RowCount;
        api.RowAt = RowAt;
        api.Value = Value;
        api.GetString = GetString;
        api.FieldIndex = FieldIndex;
        return api;
    }
    const WXL_SeyrisCdbcApi g_api = MakeApi();

    void LoadFrom(const std::string& dir)
    {
        g_dir = dir;
        ct::Load(&g_api); // loads GroundMaterial and GroundMaterialSelector too
        const std::string status = ct::Status();
        std::printf("  %s: %s\n", dir.c_str(), status.c_str());
        CHECK(ct::RowCount() > 0 && mat::MaterialCount() > 0 && mat::SelectorCount() > 0);
        CHECK(status.find("GroundMaterial") == std::string::npos); // no "(GroundMaterial: <error>)" part
    }

    // --- what the terrain asks: area, map, the layer's texture (as the client spells it), ground effect, TerrainType ---
    struct Spot { const char* what; uint32_t area; int map; const char* texture; uint32_t effect; int terrain; };

    ct::Values At(const Spot& s) { return ct::Resolve(s.area, s.map, s.texture, s.effect, s.terrain); }
    bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }
    std::string CoverPath(const ct::Values& v) { return v.coverTexture ? ct::CoverTexturePath(v.coverTexture) : ""; }
    bool EndsWith(const std::string& s, const char* tail)
    {
        const size_t n = std::strlen(tail);
        return s.size() >= n && _stricmp(s.c_str() + s.size() - n, tail) == 0;
    }

    // Maps 0 Eastern Kingdoms, 1 Kalimdor, 530 Outland, 571 Northrend. Areas: 46 Burning Steppes,
    // 51 Searing Gorge, 4 Blasted Lands, 12 Elwynn Forest, 1 Dun Morogh, 440 Tanaris, 1377 Silithus,
    // 3519 Terokkar Forest, 3688 a Bone Wastes chunk's area (Auchindoun), 3483 Hellfire Peninsula,
    // 65 Dragonblight. Textures and effects as the zones' ADTs paint them.
    const Spot kSearingAsh   { "Searing Gorge ash",       51,   0,   "tileset\\burningstepps\\burningsteppsash01.blp", 440, 7 };
    const Spot kSteppesAsh   { "Burning Steppes ash",     46,   0,   "tileset\\burningstepps\\burningsteppsash01.blp", 453, 7 };
    const Spot kSteppesCrack { "Burning Steppes cracks",  46,   0,   "tileset\\burningstepps\\burningsteppsashcracks.blp", 5138, -1 };
    const Spot kSteppesChar  { "Burning Steppes charcoal", 46,  0,   "Tileset/BurningStepps/BurningSteppsCharcoal01.blp", 1046, -1 };
    const Spot kSteppesRock  { "Burning Steppes rock",    46,   0,   "tileset\\burningstepps\\burningsteppsrock01.blp", 454, 2 };
    const Spot kSteppesLava  { "Burning Steppes lava",    46,   0,   "tileset\\burningstepps\\burningsteppslavatest02.blp", 470, 0 };
    const Spot kBoneBase     { "Bone Wastes dirt",        3688, 530, "tileset\\expansion01\\bonewastes\\bonewastesdirtbase.blp", 22796, -1 };
    const Spot kBoneCracked  { "Bone Wastes cracked",     3688, 530, "tileset\\expansion01\\bonewastes\\bonewastesdirtcracked02.blp", 22798, -1 };
    const Spot kBoneHoles    { "Bone Wastes holes",       3519, 530, "tileset\\expansion01\\bonewastes\\bonewastesdirtholes.blp", 22797, -1 };
    const Spot kBoneRock     { "Bone Wastes rock",        3688, 530, "tileset\\expansion01\\bonewastes\\bonewastesrockbase.blp", 22806, -1 };
    const Spot kBoneTiles    { "Bone Wastes stone tiles", 3688, 530, "tileset\\expansion01\\bonewastes\\bonewastesstonetiles.blp", 22802, -1 };
    const Spot kTanarisSand  { "Tanaris sand",            440,  1,   "tileset\\tanaris\\tanarissandbase01.blp", 4538, 7 };
    const Spot kTanarisStone { "Tanaris stony sand",      440,  1,   "tileset\\tanaris\\tanarissandstones.blp", 4539, 7 };
    const Spot kTanarisCrack { "Tanaris cracked ground",  440,  1,   "tileset\\tanaris\\tanariscrackedground.blp", 4540, -1 };
    // The Caverns of Time (area 2300, in Tanaris): its floor's rock-base textures, and the same textures outside it.
    const Spot kCavernsBurn  { "Caverns of Time floor (burnt rock base)", 2300, 1, "tileset\\tanaris\\tanarisrockbaseburn.blp", 0, 7 };
    const Spot kCavernsRock  { "Caverns of Time floor (rock base)", 2300, 1, "tileset\\tanaris\\tanarisrockbase01.blp", 0, 7 };
    const Spot kTanarisBurn  { "Tanaris burnt rock base", 440,  1,   "tileset\\tanaris\\tanarisrockbaseburn.blp", 0, -1 };
    const Spot kTanarisRock  { "Tanaris rock base",       440,  1,   "tileset\\tanaris\\tanarisrockbase01.blp", 0, -1 };
    // Beaches painted with Darkshore's sand: Azuremyst Isle's west coast (area 3524), and Darkshore's (148).
    const Spot kAzuremystSand  { "Azuremyst beach sand",       3524, 530, "tileset\\darkshore\\darkshoresand3.blp", 0, 7 };
    const Spot kAzuremystRocks { "Azuremyst beach sand rocks", 3524, 530, "tileset\\darkshore\\darkshoresandrocks.blp", 0, 7 };
    const Spot kDarkshoreSand  { "Darkshore beach sand",       148,  1,   "tileset\\darkshore\\darkshoresand3.blp", 0, 7 };
    const Spot kDarkshoreWaves { "Darkshore wet sand",         148,  1,   "tileset\\darkshore\\darkshoresandwaves.blp", 0, 7 };
    const Spot kSilithusHi   { "Silithus sand",           1377, 1,   "tileset\\silithis\\silithissandhighlight.blp", 7439, 7 };
    const Spot kSilithus01   { "Silithus sand 01",        1377, 1,   "tileset\\silithis\\silithissand01.blp", 7436, 7 };
    const Spot kSilithusDet  { "Silithus detail sand",    1377, 1,   "tileset\\silithis\\silithissanddetail.blp", 7441, 7 };
    const Spot kSilithusRock { "Silithus rock highlight", 1377, 1,   "tileset\\silithis\\silithisrockhighlight2.blp", 7442, 7 };
    const Spot kBlasted      { "Blasted Lands dirt",      4,    0,   "tileset\\the blasted lands\\blastedlandsdirt01.blp", 2110, -1 };
    const Spot kHellfire     { "Hellfire dirt",           3483, 530, "tileset\\outland\\outlanddirt01.blp", 22371, -1 };
    const Spot kElwynnGrass  { "Elwynn grass",            12,   0,   "tileset\\elwynn\\elwynngrassbase.blp", 2, 5 };
    const Spot kDunMorogh    { "Dun Morogh snow",         1,    0,   "tileset\\ironforge\\ironforgesnow01solid.blp", 61, 3 };
    const Spot kNorthSnow    { "Dragonblight snow (by TerrainType)", 65, 571, "tileset\\expansion02\\dragonblight\\dragonblightfreshlumpysnow.blp", 0, 3 };
    const Spot kWinterspring { "Winterspring snow",       618,  1,   "tileset\\winterspring grove\\winterspringsnowsolid.blp", 0, 3 };
    const Spot kWestfall     { "Westfall sand",           40,   0,   "tileset\\westwood\\westfallsandbase.blp", 0, 7 };
    const Spot kWestfallWet  { "Westfall wet sand",       40,   0,   "tileset\\westwood\\westfallsandbasewet.blp", 0, 7 };
    // The coasts the full map survey found (waterfall-scan/beach_survey.py, 2026-10-11).
    const Spot kStvCoast     { "Stranglethorn coast sand", 33,  0,   "tileset\\westwood\\westfallsandbase.blp", 0, 7 };
    const Spot kDarkshorePlain { "Darkshore plain sand",  148,  1,   "tileset\\darkshore\\darkshoresand.blp", 0, 7 };
    const Spot kBoreanA      { "Borean Tundra sand A",    3537, 571, "tileset\\expansion02\\boreantundra\\bt_sanda.blp", 0, 7 };
    const Spot kBoreanC      { "Borean Tundra sand C",    3537, 571, "tileset\\expansion02\\boreantundra\\bt_sandc.blp", 0, 7 };
    const Spot kBoreanE      { "Borean Tundra pebbly sand", 3537, 571, "tileset\\expansion02\\boreantundra\\bt_sande.blp", 0, 7 };
    const Spot kSholazarSand { "Sholazar river sand",     3711, 571, "tileset\\expansion02\\sholazarbasin\\sb_sandb.blp", 0, 7 };
    const Spot kZoramStrand  { "Zoram Strand sand",       331,  1,   "tileset\\ashenvale\\ashenvalesand.blp", 0, 7 };
    const Spot kUngoroSand   { "Un'Goro lake sand",       490,  1,   "tileset\\feralas\\feralassand.blp", 0, 7 };
    const Spot kFjordShore   { "Howling Fjord pebble shore", 495, 571, "tileset\\expansion02\\howlingfjord\\hfjords_shorec.blp", 0, 7 };
    // Mud on dry shores (rings like sand); pebble shores and the underwater muck as they were.
    const Spot kDesolaceMud  { "Desolace mud",            405,  1,   "tileset\\desolace\\desolacemudsolid.blp", 0, 7 };
    const Spot kFjordMud     { "Howling Fjord muddy shore", 495, 571, "tileset\\expansion02\\howlingfjord\\hfjords_shored.blp", 0, 7 };
    const Spot kSilverpineMud { "Silverpine shore silt",  130,  0,   "tileset\\silverpine\\silverpineshore.blp", 0, 7 };
    const Spot kSwampMud     { "Misty Reed Strand mud",   8,    0,   "tileset\\swamp of sorrows\\swampsorrowsdirt02.blp", 0, 7 };
    const Spot kLochMud      { "Loch Modan mud",          38,   0,   "tileset\\loch modan\\lochmodanmud.blp", 0, 7 };
    const Spot kTirisfalShore { "Tirisfal pebble shore",  85,   0,   "tileset\\tirisfall\\tirisfallshore.blp", 0, 7 };
    const Spot kSwampMuck    { "Swamp of Sorrows muck (wet ground)", 8, 0, "tileset\\swamp of sorrows\\swampsorrowsmuckdark01.blp", 0, 8 };
    const Spot kElwynnDirt   { "Elwynn dirt (footprint outlines)", 12, 0, "tileset\\elwynn\\elwynndirtbase2.blp", 0, 0 };
    // Elwynn's road at Goldshire (Azeroth_31_49): ElwynnCobblestoneBase (effect 513) over ElwynnDirtBase2 (1106).
    const Spot kElwynnCobble { "Elwynn cobblestone road", 87,   0,   "tileset\\elwynn\\elwynncobblestonebase.blp", 513, -1 };
    const Spot kElwynnRoadDirt { "Elwynn road's dirt (between the cobbles)", 87, 0, "tileset\\elwynn\\elwynndirtbase2.blp", 1106, 0 };
    const Spot kDuskwoodCobble { "Duskwood cobblestones",  10,   0,   "tileset\\duskwood\\duskwoodcobblestone.blp", 521, -1 };
    const Spot kStormIce     { "Storm Peaks clear ice",   67,   571, "tileset\\expansion02\\stormpeaks\\sp_icea.blp", 0, -1 };

    // Everything a row decides, with the cover texture as its path (IDs can move when rows are added).
    struct Seen
    {
        ct::Values v;
        std::string path;
        bool snow = false;
        mat::Values m;
    };
    Seen Look(const Spot& s)
    {
        Seen r;
        r.v = At(s);
        r.path = CoverPath(r.v);
        r.snow = ct::IsSnow(r.v.coverMaterial, s.area, s.map);
        r.m = mat::Get(mat::Select(s.area, s.map, s.texture, s.effect, s.terrain));
        return r;
    }
    bool SameMaterial(const mat::Values& a, const mat::Values& b)
    {
        return a.restMoisture == b.restMoisture && a.stiffness == b.stiffness && a.absorbency == b.absorbency &&
               a.temperature == b.temperature && a.flags == b.flags && a.hasTemperature == b.hasTemperature &&
               a.restMoistureSet == b.restMoistureSet;
    }
    bool Same(const Seen& a, const Seen& b)
    {
        const ct::Values& x = a.v; const ct::Values& y = b.v;
        return x.depth == y.depth && x.maxSlope == y.maxSlope && x.slopeFade == y.slopeFade && x.driftNoise == y.driftNoise &&
               x.edgeBreakup == y.edgeBreakup && x.rim == y.rim && x.relaxSeconds == y.relaxSeconds && x.tintColor == y.tintColor &&
               x.tintStrength == y.tintStrength && x.zOffset == y.zOffset && x.wetness == y.wetness && x.flags == y.flags &&
               x.material == y.material && x.coverMaterial == y.coverMaterial && x.stiffness == y.stiffness &&
               x.meltKeptShare == y.meltKeptShare && x.meltGoneTemperature == y.meltGoneTemperature &&
               _stricmp(a.path.c_str(), b.path.c_str()) == 0 && a.snow == b.snow && SameMaterial(a.m, b.m);
    }

    // The look rules as SurfaceCover.cpp had them before CoverBlend.hpp.
    float OldBareSnowLookShare(float covered) { return 1.0f - (covered * 4.0f > 1.0f ? 1.0f : covered * 4.0f); }
    void OldStrongestTwo(const float* share, int count, int& a, int& c)
    {
        a = -1; c = -1;
        for (int i = 0; i < count; ++i)
        {
            if (a < 0 || share[i] > share[a]) { c = a; a = i; }
            else if (c < 0 || share[i] > share[c]) c = i;
        }
    }

    unsigned g_passed = 0;
    template <class F> void Test(const char* name, F f)
    {
        f();
        ++g_passed;
        std::printf("PASS %s\n", name);
    }

    void PrintSpot(const Spot& s)
    {
        const ct::Values v = At(s);
        std::printf("  %-34s material %3u \"%s\" (parent %u): depth %.3f, relax %.0f s, max slope %.0f, tint %06X x %.2f, z %.3f, flags 0x%X, texture \"%s\"%s\n",
                    s.what, v.material, mat::Name(v.material), mat::Parent(v.material), v.depth, v.relaxSeconds, v.maxSlope,
                    v.tintColor, v.tintStrength, v.zOffset, v.flags, CoverPath(v).c_str(), ct::IsSnow(v.coverMaterial, s.area, s.map) ? " [snow]" : "");
    }
}

int main(int argc, char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc < 2) { std::printf("usage: cover_table_tests <data dir> [<data dir before the change>]\n"); return 2; }
    const std::string now = argv[1], before = argc > 2 ? argv[2] : "";
    const Spot* const kUnchanged[] = { &kDunMorogh, &kNorthSnow, &kWinterspring, &kWestfallWet, &kFjordShore, &kTirisfalShore,
                                       &kSwampMuck, &kStormIce, &kElwynnGrass,
                                       &kSteppesRock, &kSteppesLava, &kSilithusRock, &kTanarisCrack, &kBlasted, &kHellfire, &kBoneRock, &kBoneTiles,
                                       &kTanarisBurn, &kTanarisRock, &kDarkshoreWaves };
    try
    {
        std::vector<Seen> old;
        std::vector<mat::Values> oldMaterials;
        if (!before.empty())
        {
            LoadFrom(before);
            for (const Spot* s : kUnchanged) old.push_back(Look(*s));
            for (uint32_t id = 0; id < 64; ++id) oldMaterials.push_back(mat::Get(id));
        }
        LoadFrom(now);
        std::printf("Resolved:\n");
        for (const Spot* s : { &kSearingAsh, &kSteppesAsh, &kSteppesCrack, &kSteppesChar, &kSteppesRock, &kBoneBase, &kBoneCracked, &kBoneHoles,
                               &kBoneRock, &kTanarisSand, &kTanarisStone, &kSilithusHi, &kSilithus01, &kSilithusDet, &kSilithusRock,
                               &kCavernsBurn, &kCavernsRock, &kTanarisBurn, &kTanarisRock,
                               &kAzuremystSand, &kAzuremystRocks, &kDarkshoreSand, &kDarkshoreWaves,
                               &kBlasted, &kHellfire, &kElwynnGrass, &kElwynnDirt, &kElwynnCobble, &kDunMorogh, &kNorthSnow })
            PrintSpot(*s);

        Test("Searing Gorge and Burning Steppes ash: the ash cover, thin, soft, its own texture, not under water", []
        {
            for (const Spot* s : { &kSearingAsh, &kSteppesAsh })
            {
                const ct::Values v = At(*s);
                CHECK(v.material == 15 && mat::Parent(15) == 50 && std::string(mat::Name(50)) == "Ash");
                CHECK(Near(v.depth, 0.10f) && v.depth >= 0.06f && v.depth <= 0.12f);
                CHECK(Near(v.relaxSeconds, 20.0f) && Near(v.maxSlope, 35.0f) && Near(v.slopeFade, 12.0f));
                CHECK((v.flags & ct::kFlagPreventUnderwater) && !(v.flags & ct::kFlagPreventOnLand));
                CHECK(v.stiffness == 0.0f && v.coverMaterial == 15 && !ct::IsSnow(v.coverMaterial, s->area, s->map));
                CHECK(EndsWith(CoverPath(v), "BurningStepps\\BurningSteppsAsh01.blp"));
                CHECK(v.tintColor == 0xBAB49Bu && Near(v.tintStrength, 0.2f) && Near(v.zOffset, -0.02f) && v.wetness == 0.0f);
            }
        });
        Test("the other ash textures: cracked crust 0.06, charcoal 0.08, each its own texture, the rest from Ash", []
        {
            const ct::Values cracks = At(kSteppesCrack), coal = At(kSteppesChar);
            CHECK(cracks.material == 52 && Near(cracks.depth, 0.06f) && EndsWith(CoverPath(cracks), "BurningSteppsAshCracks.blp"));
            CHECK(coal.material == 53 && Near(coal.depth, 0.08f) && EndsWith(CoverPath(coal), "BurningSteppsCharcoal01.blp"));
            for (const ct::Values& v : { cracks, coal })
                CHECK(mat::Parent(v.material) == 50 && Near(v.relaxSeconds, 20.0f) && (v.flags & ct::kFlagPreventUnderwater) && v.stiffness == 0.0f);
        });
        Test("the Bone Wastes: ash on the dirt textures (any area), not on rock or stone tiles", []
        {
            const ct::Values base = At(kBoneBase), cracked = At(kBoneCracked), holes = At(kBoneHoles);
            CHECK(base.material == 54 && Near(base.depth, 0.10f) && EndsWith(CoverPath(base), "BoneWastesDirtBase.blp"));
            CHECK(cracked.material == 55 && Near(cracked.depth, 0.06f) && holes.material == 56 && Near(holes.depth, 0.08f));
            for (const ct::Values& v : { base, cracked, holes })
                CHECK(mat::Parent(v.material) == 50 && Near(v.relaxSeconds, 20.0f) && (v.flags & ct::kFlagPreventUnderwater) && Near(v.tintStrength, 0.2f));
            CHECK(At(kBoneRock).depth == 0.0f && At(kBoneTiles).depth == 0.0f);
        });
        Test("Tanaris and Silithus sand: shallow, fills in faster than ash and snow, its own texture and tint", []
        {
            const ct::Values sand = At(kTanarisSand), stones = At(kTanarisStone), hi = At(kSilithusHi), s01 = At(kSilithus01), det = At(kSilithusDet);
            CHECK(sand.material == 2 && Near(sand.depth, 0.08f) && EndsWith(CoverPath(sand), "TanarisSandBase01.blp"));
            CHECK(stones.material == 3 && Near(stones.depth, 0.06f) && det.material == 9 && Near(det.depth, 0.06f));
            CHECK(hi.material == 8 && s01.material == 7 && Near(hi.depth, 0.08f) && Near(s01.depth, 0.08f));
            for (const ct::Values& v : { sand, stones, hi, s01, det })
            {
                CHECK(mat::Parent(v.material) == 51 && std::string(mat::Name(51)) == "Sand");
                CHECK(Near(v.relaxSeconds, 10.0f) && Near(v.maxSlope, 30.0f) && (v.flags & ct::kFlagPreventUnderwater));
                CHECK(Near(v.tintStrength, 0.2f) && Near(v.zOffset, -0.015f) && v.stiffness == 0.0f && !ct::IsSnow(v.coverMaterial, 440, 1));
            }
            CHECK(At(kTanarisCrack).depth == 0.0f);
            // No trenches in sand: rings around the prints instead (flag 0x10), on every sand row, the Caverns of
            // Time's and the beaches' too; ash and snow keep their trenches.
            for (const Spot* s : { &kTanarisSand, &kTanarisStone, &kSilithusHi, &kSilithus01, &kSilithusDet, &kCavernsBurn,
                                   &kCavernsRock, &kAzuremystSand, &kAzuremystRocks, &kDarkshoreSand })
                CHECK((At(*s).flags & ct::kFlagRipplePrints) && (At(*s).flags & ct::kFlagPreventUnderwater));
            for (const Spot* s : { &kSearingAsh, &kDunMorogh })
                CHECK(At(*s).depth > 0.0f && !(At(*s).flags & ct::kFlagRipplePrints));
            CHECK(sand.relaxSeconds < At(kSearingAsh).relaxSeconds && At(kSearingAsh).relaxSeconds < At(kDunMorogh).relaxSeconds);
        });
        Test("Silithus's dunes (45.0, 37.2): sand as in Tanaris, rims at every spot; the faint trench is the rock highlight's share, as before weather-polish", []
        {
            // "sand footprints in silithus do not work, only really faint trenches" (the user, 2026-10-18). The rows aren't
            // why: Silithus's sand is Tanaris's, unchanged since the sand came in (rim 0.5, relax 10 s, 0.015 yd under the
            // terrain, flags 0x15: rings around prints, no trench). The client put no prints down there: its dunes'
            // ground effects are TerrainType Dirt (features/CoverPrints.hpp), so no rims formed around them either.
            const ct::Values hi = At(kSilithusHi), s01 = At(kSilithus01), det = At(kSilithusDet), rock = At(kSilithusRock);
            for (const ct::Values& v : { hi, s01, det, At(kTanarisSand), At(kTanarisStone) })
                CHECK(Near(v.rim, 0.5f) && Near(v.relaxSeconds, 10.0f) && Near(v.zOffset, -0.015f) && v.flags == 0x15u && Near(v.maxSlope, 30.0f));
            // The rock highlight painted over the dunes: upstream's thin row (1 cm, its own trench), not a ring cover.
            CHECK(rock.material == 10 && Near(rock.depth, 0.01f) && Near(rock.rim, 0.6f) && Near(rock.relaxSeconds, 24.0f) &&
                  !(rock.flags & ct::kFlagRipplePrints));
            // The layer weights about the screenshot's spot (Kalimdor_30_44, the 5 x 5 cells around it, Living Azeroth's
            // weighing: each layer's alpha, layer 0 one minus the rest): SilithisSandHighlight 215..255,
            // SilithisRockHighlight2 4..193, SilithisSand01 0..40. The mean, the rockiest cell and the sandiest.
            struct Mix { const char* what; float sand01, highlight, rockHighlight; };
            const float human = 0.4f, cell = 0.25f; // a human's trench radius, level 0's grid
            const blend::TrenchShape shape = blend::TrenchShapeFor(human, cell, 1.5f);
            for (const Mix& m : { Mix{ "mean", 4.0f, 249.0f, 104.0f }, Mix{ "rockiest", 0.0f, 215.0f, 193.0f }, Mix{ "sandiest", 40.0f, 255.0f, 4.0f } })
            {
                const float sandW = m.sand01 + m.highlight, all = sandW + m.rockHighlight;
                const float ripple = sandW / all;                                  // the look's share from 0x10 rows
                const float depth = (m.sand01 * s01.depth + m.highlight * hi.depth + m.rockHighlight * rock.depth) / all;
                // At least half: the rims form (SurfaceCoverInternal's kRippleShareMin) and, now, the client's prints too.
                CHECK(ripple >= 0.5f);
                // Feet press the rest (keep = 1 - ripple). A walked path's floor, by the rules before weather-polish (the
                // largest press so far, rims the largest ring) and after (TrenchProfile, StampedRim): the same depth.
                const float keep = 1.0f - ripple;
                float pressOld = 0.0f, pressNew = 0.0f, rimNew = 0.0f, rimOld = 0.0f;
                for (float x = -2.0f; x <= 2.0f; x += 0.05f)
                {
                    const float d = std::fabs(x);
                    const float tOld = (d < human ? (d < 0.7f * human ? 1.0f : (human - d) / (0.3f * human)) : 0.0f) * keep;
                    pressOld = std::fmax(pressOld, tOld);
                    rimOld = std::fmax(rimOld, (1.0f - std::fabs(d - 1.25f * human) / (0.35f * human)) * keep);
                    const blend::TrenchStamp st = blend::TrenchProfile(d, shape);
                    rimNew = blend::StampedRim(rimNew, st.press * keep, st.rim * keep, pressNew);
                    pressNew = std::fmax(pressNew, st.press * keep);
                }
                CHECK(Near(pressOld, keep) && Near(pressNew, keep));
                const float dentOld = depth - blend::DeformedDepth(depth, 0.0f, 0.0f, pressOld, 0.0f, 1.0f);
                const float dentNew = depth - blend::DeformedDepth(depth, 0.0f, 0.0f, pressNew, 0.0f, 1.0f);
                std::printf("  Silithus %-8s sand %.0f%% of the look, cover %.3f yd: a walked path %.1f cm deep before weather-polish, %.1f cm now\n",
                            m.what, 100.0f * ripple, depth, 100.0f * dentOld, 100.0f * dentNew);
                CHECK(Near(dentOld, dentNew) && dentNew < 0.025f); // "only really faint trenches", the same before and after
            }
            // Pure sand (Tanaris's stony sand, all of the look): no trench at all, before and after.
            CHECK(blend::TrenchProfile(0.0f, shape).press * 0.0f == 0.0f && blend::StampedRim(0.0f, 0.0f, 0.0f, 0.0f) == 0.0f);
        });
        Test("the Caverns of Time: sand on its floor's rock-base textures, each its own texture; the same textures bare in the rest of Tanaris", []
        {
            const ct::Values burn = At(kCavernsBurn), rock = At(kCavernsRock);
            CHECK(burn.material == 57 && EndsWith(CoverPath(burn), "TanarisRockBaseBurn.blp") && burn.tintColor == 0x926122u);
            CHECK(rock.material == 58 && EndsWith(CoverPath(rock), "TanarisRockBase01.blp"));
            for (const ct::Values& v : { burn, rock })
            {
                CHECK(mat::Parent(v.material) == 51 && Near(v.depth, 0.08f) && Near(v.relaxSeconds, 10.0f) && Near(v.maxSlope, 30.0f));
                CHECK((v.flags & ct::kFlagPreventUnderwater) && Near(v.tintStrength, 0.2f) && !ct::IsSnow(v.coverMaterial, 2300, 1));
            }
            CHECK(At(kTanarisBurn).depth == 0.0f && At(kTanarisRock).depth == 0.0f);
            CHECK(At(kTanarisBurn).material == 0 && At(kTanarisRock).material == 0);
        });
        Test("beaches of Darkshore's sand (Darkshore, Azuremyst Isle): sand, its own texture; the wet strip at the water bare", []
        {
            const ct::Values sand = At(kAzuremystSand), rocks = At(kAzuremystRocks), home = At(kDarkshoreSand);
            CHECK(sand.material == 59 && Near(sand.depth, 0.08f) && EndsWith(CoverPath(sand), "DarkshoreSand3.blp"));
            CHECK(rocks.material == 60 && Near(rocks.depth, 0.06f) && EndsWith(CoverPath(rocks), "DarkshoreSandRocks.blp"));
            CHECK(home.material == 59 && Near(home.depth, 0.08f));
            for (const ct::Values& v : { sand, rocks, home })
                CHECK(mat::Parent(v.material) == 51 && Near(v.relaxSeconds, 10.0f) && (v.flags & ct::kFlagPreventUnderwater) &&
                      Near(v.tintStrength, 0.2f) && !ct::IsSnow(v.coverMaterial, 148, 1));
            CHECK(sand.tintColor == 0x909090u && At(kDarkshoreWaves).depth == 0.0f);
        });
        Test("the coasts the map survey found: sand on their sand textures (rings, the ground's vertex colour); wet sand and pebble shores as they were", []
        {
            // Borean Tundra's sands lie half over dark rock: their tint is the texture's own colour, lightly (0.05), not 1.68 x it.
            struct Want { const Spot* spot; uint32_t material; float depth; const char* texture; float tint; };
            const Want wants[] = { { &kWestfall, 6, 0.08f, "WestfallSandBase.blp", 0.2f }, { &kStvCoast, 6, 0.08f, "WestfallSandBase.blp", 0.2f },
                                   { &kDarkshorePlain, 61, 0.08f, "DarkshoreSand.blp", 0.2f }, { &kBoreanA, 62, 0.08f, "BT_SandA.blp", 0.05f },
                                   { &kBoreanC, 63, 0.08f, "BT_SandC.blp", 0.05f }, { &kBoreanE, 64, 0.06f, "BT_SandE.blp", 0.05f },
                                   { &kSholazarSand, 65, 0.08f, "SB_SandB.blp", 0.2f }, { &kZoramStrand, 66, 0.08f, "AshenvaleSand.blp", 0.2f },
                                   { &kUngoroSand, 67, 0.06f, "FeralasSand.blp", 0.2f } };
            for (const Want& w : wants)
            {
                const ct::Values v = At(*w.spot);
                if (!(v.material == w.material && Near(v.depth, w.depth) && EndsWith(CoverPath(v), w.texture) && mat::Parent(v.material) == 51 &&
                      Near(v.relaxSeconds, 10.0f) && Near(v.tintStrength, w.tint) && (v.flags & ct::kFlagRipplePrints) &&
                      (v.flags & ct::kFlagUseVertexColor) && (v.flags & ct::kFlagPreventUnderwater)))
                    throw std::runtime_error(std::string("not sand as wanted: ") + w.spot->what);
            }
            CHECK(At(kWestfall).tintColor == 0xFFFFA0u && At(kSholazarSand).tintColor == 0xFFFFF5u);
            CHECK(At(kBoreanA).tintColor == 0x6C675Cu && At(kBoreanC).tintColor == 0x4C4B46u && At(kBoreanE).tintColor == 0x52514Cu);
            CHECK(At(kWestfallWet).material == 11 && Near(At(kWestfallWet).depth, 0.05f) && !(At(kWestfallWet).flags & ct::kFlagRipplePrints));
            CHECK(At(kFjordShore).depth == 0.0f);
            CHECK(std::string(mat::Name(6)) == "WestfallSandBase"); // the existing material, reparented, keeps its name
        });
        Test("mud shores ring like sand (\"mud would make sense to have the footprint rings\"); pebble shores and the underwater muck as they were", []
        {
            struct Want { const Spot* spot; uint32_t material; const char* texture; };
            const Want wants[] = { { &kDesolaceMud, 71, "DesolaceMudSolid.blp" }, { &kFjordMud, 72, "HFjords_ShoreD.blp" },
                                   { &kSilverpineMud, 73, "SilverpineShore.blp" }, { &kSwampMud, 74, "SwampSorrowsDirt02.blp" },
                                   { &kLochMud, 75, "LochModanMud.blp" } };
            for (const Want& w : wants)
            {
                const ct::Values v = At(*w.spot);
                if (!(v.material == w.material && EndsWith(CoverPath(v), w.texture) && mat::Parent(v.material) == 70 &&
                      std::string(mat::Name(70)) == "Mud" && Near(v.depth, 0.05f) && Near(v.relaxSeconds, 20.0f) &&
                      Near(v.wetness, 0.25f) && Near(v.tintStrength, 0.1f) && (v.flags & ct::kFlagRipplePrints) &&
                      (v.flags & ct::kFlagUseVertexColor) && (v.flags & ct::kFlagPreventUnderwater) && !ct::IsSnow(v.coverMaterial, 0, 0)))
                    throw std::runtime_error(std::string("not mud as wanted: ") + w.spot->what);
            }
            CHECK(At(kFjordShore).depth == 0.0f && At(kTirisfalShore).depth == 0.0f);
            // The Swamp of Sorrows' own muck row (13): upstream's mud that forms on wet ground (MoistureMin 0.6, 2026-10-13),
            // no longer kept under water; still not one of our ripple covers.
            CHECK(At(kSwampMuck).material == 13 && ct::MoistureGated(At(kSwampMuck).moistureMin) && Near(At(kSwampMuck).moistureMin, 0.6f) &&
                  !(At(kSwampMuck).flags & ct::kFlagPreventOnLand) && !(At(kSwampMuck).flags & ct::kFlagRipplePrints));
        });
        Test("no cover: Elwynn grass, Burning Steppes rock and lava, the Blasted Lands, Hellfire", []
        {
            CHECK(At(kElwynnGrass).material == 0 && At(kElwynnGrass).depth == 0.0f);
            for (const Spot* s : { &kSteppesRock, &kSteppesLava, &kBlasted, &kHellfire }) CHECK(At(*s).depth == 0.0f);
            CHECK(At(kHellfire).material == 0 && At(kBlasted).material == 0);
        });
        Test("snow as it was: Dun Morogh's textured snow and Northrend's snow by TerrainType", []
        {
            const ct::Values dm = At(kDunMorogh), north = At(kNorthSnow);
            CHECK(dm.material == 4 && Near(dm.depth, 0.35f) && Near(dm.relaxSeconds, 30.0f) && ct::IsSnow(dm.coverMaterial, 1, 0));
            CHECK(north.material == 1 && Near(north.depth, 0.35f) && ct::IsSnow(north.coverMaterial, 65, 571) && north.coverTexture == 0);
        });
        Test("ground materials: the new ones read like no material (moisture, heat), the reparented ones keep their own", []
        {
            const mat::Values none = mat::Get(0);
            for (uint32_t id : { 50u, 51u, 52u, 53u, 54u, 55u, 56u })
            {
                const mat::Values v = mat::Get(id);
                CHECK(mat::Exists(id) && v.restMoisture == none.restMoisture && v.absorbency == none.absorbency && v.stiffness == none.stiffness);
                CHECK(!v.hasTemperature && !v.restMoistureSet && v.flags == 0);
            }
            const mat::Values hotAsh = mat::Get(15);
            CHECK(hotAsh.hasTemperature && hotAsh.temperature == 35.0f && Near(hotAsh.restMoisture, 0.2f));
        });
        if (!before.empty())
        {
            Test("against the files before the change: every other spot resolves bit for bit as before (snow, ice, mud, Westfall, bare)", [&]
            {
                for (size_t k = 0; k < old.size(); ++k)
                    if (!Same(old[k], Look(*kUnchanged[k]))) throw std::runtime_error(std::string("changed: ") + kUnchanged[k]->what);
            });
            Test("against the files before the change: every material's properties (moisture, stiffness, heat, flags) as before", [&]
            {
                for (uint32_t id = 0; id < 64; ++id)
                    if (!SameMaterial(oldMaterials[id], mat::Get(id))) throw std::runtime_error("material " + std::to_string(id) + " reads differently");
            });
        }
        Test("look rule: the bare ground's snow look counts exactly as before beside snow, not beside other covers", []
        {
            for (int i = 0; i <= 40; ++i)
            {
                const float covered = i / 40.0f;
                CHECK(blend::BareSnowLookShare(covered, covered, 0.0f) == OldBareSnowLookShare(covered)); // painted snow only
                CHECK(blend::BareSnowLookShare(covered, 0.0f, 0.0f) == OldBareSnowLookShare(covered));     // (nothing painted)
                CHECK(blend::BareSnowLookShare(covered, 0.0f, covered > 0.0f ? covered : 0.01f) == 0.0f); // ash, sand, mud only
            }
            CHECK(Near(blend::BareSnowLookShare(0.2f, 0.1f, 0.1f), 0.2f * 0.5f));
        });
        Test("slots: residents stay while the counts near the cut wobble; a new texture takes a free slot; no hurry without room", []
        {
            const int kSlots = 8;
            int slots[kSlots] = {};
            std::vector<unsigned> count(13, 0);
            for (int id = 1; id <= 10; ++id) count[id] = 100 - id;   // ten textures in view, eight slots
            CHECK(blend::ChooseSlots(count, slots, kSlots));
            for (int id = 1; id <= 8; ++id) CHECK(std::find(slots, slots + kSlots, id) != slots + kSlots);
            // The 9th and 10th creep past the 8th (but not by half again): nothing moves.
            count[9] = count[8] + 10; count[10] = count[8] + 20;
            int before[kSlots]; std::copy(slots, slots + kSlots, before);
            for (int round = 0; round < 20; ++round)
            {
                count[8] += (round % 2) ? 15 : -15;
                CHECK(!blend::ChooseSlots(count, slots, kSlots));
            }
            CHECK(std::equal(slots, slots + kSlots, before));
            // One clearly more present (over 1.5 x) takes the weakest resident's slot.
            count[10] = count[8] * 2 + 50;
            CHECK(blend::ChooseSlots(count, slots, kSlots));
            CHECK(std::find(slots, slots + kSlots, 10) != slots + kSlots && std::find(slots, slots + kSlots, 8) == slots + kSlots);
            // A texture gone from view frees its slot; a well-present newcomer takes it.
            count[2] = 0; count[11] = 300;
            CHECK(blend::ChooseSlots(count, slots, kSlots));
            CHECK(std::find(slots, slots + kSlots, 2) == slots + kSlots && std::find(slots, slots + kSlots, 11) != slots + kSlots);
            CHECK(blend::HurryRecount(8, kSlots) && blend::HurryRecount(3, kSlots) && !blend::HurryRecount(9, kSlots));
        });
        Test("look rule: two cover textures keep their shares exactly; more give the others' share to the strongest two", []
        {
            const float cases[][5] = { { 0.7f, 0, 0, 0, 0 }, { 0.3f, 0.6f, 0, 0, 0 }, { 0.5f, 0.5f, 0, 0, 0 }, { 0.5f, 0.3f, 0.2f, 0, 0 },
                                       { 0.1f, 0.4f, 0.2f, 0.3f, 0 }, { 0.25f, 0.25f, 0.25f, 0.25f, 0 }, { 0.05f, 0.05f, 0.05f, 0.05f, 0.05f } };
            const int counts[] = { 1, 2, 2, 3, 4, 4, 5 };
            for (int k = 0; k < 7; ++k)
            {
                int a, c, oa, oc;
                float sa, sc;
                blend::StrongestTwo(cases[k], counts[k], a, c, sa, sc);
                OldStrongestTwo(cases[k], counts[k], oa, oc);
                CHECK(a == oa && c == oc);
                float all = 0.0f;
                for (int i = 0; i < counts[k]; ++i) all += cases[k][i];
                if (counts[k] <= 2) CHECK(sa == cases[k][a] && sc == (c >= 0 ? cases[k][c] : 0.0f));
                else CHECK(Near(sa + sc, all) && Near(sa * cases[k][c], sc * cases[k][a]));
            }
            int a, c;
            float sa, sc;
            blend::StrongestTwo(nullptr, 0, a, c, sa, sc);
            CHECK(a == -1 && c == -1 && sa == 0.0f && sc == 0.0f);
        });
        Test("look rule: bare ground beside a cover uploads the cover's texture weights, so filtering keeps its colour to its edge", []
        {
            const uint8_t ash[8] = { 255, 0, 0, 0, 0, 0, 0, 0 }, mixed[8] = { 0, 140, 115, 0, 0, 0, 0, 0 };
            const uint8_t faint[8] = { 0, 0, 0, 60, 0, 0, 0, 0 }, plainSnow[8] = {};
            uint8_t out[8];
            const uint8_t* oneSide[3] = { nullptr, ash, nullptr };
            blend::BorrowedWeights(oneSide, 3, out);
            CHECK(out[0] == 255 && out[1] == 0);
            // Between that cell and the ash beside it, the filtered share stays whole (it fell to half).
            CHECK((ash[0] + out[0]) / 2 == 255);
            const uint8_t* two[3] = { faint, mixed, nullptr };
            blend::BorrowedWeights(two, 3, out);
            CHECK(out[1] == 140 && out[2] == 115 && out[3] == 0);   // the one with the most texture
            const uint8_t* none[2] = { nullptr, nullptr };
            blend::BorrowedWeights(none, 2, out);
            for (int s = 0; s < 8; ++s) CHECK(out[s] == 0);         // nothing beside it: as before
            const uint8_t* snowOnly[1] = { plainSnow };
            blend::BorrowedWeights(snowOnly, 1, out);
            for (int s = 0; s < 8; ++s) CHECK(out[s] == 0);         // plain snow beside it: white, as before
        });
        Test("look rule: sand over mixed ground takes the rest of the ground's colour at its share (the same as two lerps)", []
        {
            // Borean's shore: its sand cover (tint 0x6C675C at 0.05) where the alpha maps paint 44% dark rock.
            const float sand[3] = { 0.9f, 0.85f, 0.75f }, rock[3] = { 0.25f, 0.24f, 0.22f };
            float tint[3] = { 0x6C / 255.0f, 0x67 / 255.0f, 0x5C / 255.0f }, strength = 0.05f;
            const float ownTint[3] = { tint[0], tint[1], tint[2] }, ownStrength = strength;
            blend::MatchGround(tint, strength, rock, 0.44f);
            CHECK(strength > 0.44f && strength < 1.0f);
            for (int k = 0; k < 3; ++k)
            {
                const float once = sand[k] + (ownTint[k] - sand[k]) * ownStrength;        // the shader's lerp by its own tint
                const float twice = once + (rock[k] - once) * 0.44f;                     // then the ground's share
                const float merged = sand[k] + (tint[k] - sand[k]) * strength;           // what the shader now draws
                CHECK(std::fabs(merged - twice) < 1e-5f);
                CHECK(tint[k] >= 0.0f && tint[k] <= 1.0f);                               // still a colour
                CHECK(merged < sand[k]);                                                 // darker than pure sand
            }
            // No other ground there, or no tint (snow): untouched.
            float t2[3] = { 0.5f, 0.4f, 0.3f }, s2 = 0.2f;
            blend::MatchGround(t2, s2, rock, 0.0f);
            CHECK(Near(t2[0], 0.5f) && Near(t2[1], 0.4f) && Near(t2[2], 0.3f) && Near(s2, 0.2f));
            // All the ground other: the ground's colour, fully.
            float t3[3] = { 0.5f, 0.4f, 0.3f }, s3 = 0.2f;
            blend::MatchGround(t3, s3, rock, 1.0f);
            CHECK(Near(s3, 1.0f) && Near(t3[0], rock[0]) && Near(t3[1], rock[1]) && Near(t3[2], rock[2]));
        });
        Test("ground colour: DXT colour blocks decode as the formats say; a BLP's average from its ~32 px mip", []
        {
            // Pure red (0xF800) and pure blue (0x001F) endpoints; texel t's index in bits 2t.
            const uint8_t four[8] = { 0x00, 0xF8, 0x1F, 0x00, 0xE4, 0xE4, 0xE4, 0xE4 }; // indices 0,1,2,3 per row
            float rgb[16][3];
            bool opaque[16];
            blp::DecodeColourBlock(four, true, rgb, opaque);
            CHECK(Near(rgb[0][0], 1.0f) && Near(rgb[0][2], 0.0f));                         // index 0: red
            CHECK(Near(rgb[1][0], 0.0f) && Near(rgb[1][2], 1.0f));                         // index 1: blue
            CHECK(Near(rgb[2][0], 2.0f / 3.0f) && Near(rgb[2][2], 1.0f / 3.0f));           // index 2: 2/3 red
            CHECK(Near(rgb[3][0], 1.0f / 3.0f) && Near(rgb[3][2], 2.0f / 3.0f) && opaque[3]);
            const uint8_t three[8] = { 0x1F, 0x00, 0x00, 0xF8, 0xE4, 0xE4, 0xE4, 0xE4 };  // c0 <= c1: DXT1's 3 colours
            blp::DecodeColourBlock(three, true, rgb, opaque);
            CHECK(Near(rgb[2][0], 0.5f) && Near(rgb[2][2], 0.5f) && opaque[2] && !opaque[3]);
            blp::DecodeColourBlock(three, false, rgb, opaque);                              // DXT3/5: always 4 colours
            CHECK(opaque[3] && Near(rgb[2][2], 2.0f / 3.0f));

            // A 64 x 64 DXT1 BLP with mips 64, 32, 16: level 1 (32 px) is read. Level 0 all red, level 1 half red half
            // blue by blocks, level 2 all blue: the average is level 1's, half and half.
            std::vector<uint8_t> file(148 + 1024, 0);
            std::memcpy(file.data(), "BLP2", 4);
            file[4] = 1; file[8] = 2; file[9] = 0; file[10] = 0; file[11] = 1;
            auto put32 = [&](size_t at, uint32_t v) { std::memcpy(file.data() + at, &v, 4); };
            put32(12, 64); put32(16, 64);
            const uint8_t red[8] = { 0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0 }, blue[8] = { 0x1F, 0x00, 0x1F, 0x00, 0, 0, 0, 0 };
            const uint32_t sizes[3] = { 64, 32, 16 };
            for (int m = 0; m < 3; ++m)
            {
                put32(20 + 4 * m, static_cast<uint32_t>(file.size()));
                const uint32_t blocks = (sizes[m] / 4) * (sizes[m] / 4);
                put32(84 + 4 * m, blocks * 8);
                for (uint32_t b = 0; b < blocks; ++b)
                {
                    const uint8_t* block = m == 0 ? red : m == 2 ? blue : (b % 2 ? blue : red);
                    file.insert(file.end(), block, block + 8);
                }
            }
            float average[3] = {};
            CHECK(blp::AverageColour(file.data(), file.size(), average));
            CHECK(Near(average[0], 0.5f) && Near(average[1], 0.0f) && Near(average[2], 0.5f));
            CHECK(!blp::AverageColour(file.data(), 100, average));                           // not a whole header
            file[0] = 'X';
            CHECK(!blp::AverageColour(file.data(), file.size(), average));                   // not BLP2
        });
        Test("paved ground: Elwynn's cobbles (and the old world's other paved roads) are sealed; its dirt, grass and the rest aren't", []
        {
            const ct::Values cobble = At(kElwynnCobble);
            CHECK(cobble.material == 80 && std::string(mat::Name(80)) == "Paved" && mat::Sealed(80) && cobble.depth == 0.0f);
            CHECK(mat::Sealed(At(kDuskwoodCobble).material));
            for (const char* t : { "Tileset/Stranglethorn/StranglethornCobblestoneRoad.blp", "TILESET\\TIRISFALL\\TirisfallStoneRoad01.blp",
                                   "tileset\\burningstepps\\burningsteppsbrick01.blp" })
                CHECK(mat::Sealed(mat::Select(1, 0, t, 0, -1)));
            // Sealed reads like no material for everything else: moisture, stiffness, heat (the flag only).
            const mat::Values none = mat::Get(0), paved = mat::Get(80);
            CHECK(paved.restMoisture == none.restMoisture && paved.absorbency == none.absorbency && paved.stiffness == none.stiffness &&
                  !paved.hasTemperature && !paved.restMoistureSet && paved.flags == mat::kFlagSealed);
            CHECK(!ct::IsSnow(cobble.coverMaterial, 87, 0));
            // Not sealed: Elwynn's dirt (the road's and elsewhere), grass, mud, sand, snow, ice, no material.
            for (const Spot* sp : { &kElwynnDirt, &kElwynnRoadDirt, &kElwynnGrass, &kSwampMuck, &kDesolaceMud, &kTanarisSand, &kDunMorogh,
                                    &kNorthSnow, &kStormIce, &kBoneTiles, &kHellfire })
                if (mat::Sealed(At(*sp).material)) throw std::runtime_error(std::string("sealed: ") + sp->what);
            CHECK(!mat::Sealed(0) && !mat::Sealed(9999));
            // A child of a sealed material is sealed; the flag comes along the chain.
            CHECK(mat::Parent(80) == 0);
        });

        Test("mud on stone: none where a third is cobbled, all of it on open dirt, smooth between", []
        {
            CHECK(blend::MudAllowed(0.0f) == 1.0f && blend::MudAllowed(blend::kSealedFrom) == 1.0f);
            CHECK(blend::MudAllowed(blend::kSealedFull) == 0.0f && blend::MudAllowed(1.0f) == 0.0f);
            float last = 1.0f;
            for (float t = 0.0f; t <= 1.0f; t += 0.01f) { const float m = blend::MudAllowed(t); CHECK(m <= last + 1e-6f && m >= 0.0f); last = m; }
            CHECK(blend::MudAllowed(std::nanf("")) == 1.0f && blend::MudAllowed(-1.0f) == 1.0f);
            // Goldshire's road, sampled every yard (tools: adt_spot.py on Azeroth_31_49): the cobbles' painted share. Every
            // spot of the road holds its mud back; the verge (no cobbles, or one stone in 15) keeps it.
            for (float cobbles : { 1.0f, 0.93f, 0.87f, 0.8f, 0.73f, 0.67f, 0.6f, 0.53f, 0.47f, 0.4f, 0.33f })
                CHECK(blend::MudAllowed(cobbles) == 0.0f);
            for (float verge : { 0.0f, 0.05f }) CHECK(blend::MudAllowed(verge) == 1.0f);
            CHECK(blend::MudAllowed(0.13f) > 0.5f);
        });

        Test("Elwynn's mud: still on wet dirt only, hidden under the grass until feet push a rim up, then 3 cm proud, not 24", []
        {
            const ct::Values v = At(kElwynnDirt);
            CHECK(v.material == 12 && ct::MoistureGated(v.moistureMin) && Near(v.moistureMin, 0.6f) && Near(v.relaxSeconds, 22.0f));
            CHECK(Near(v.depth, 0.02f) && Near(v.zOffset, -0.025f) && Near(v.rim, 1.75f) && std::fabs(v.stiffness - 0.8f) < 1e-5f);
            CHECK(At(kElwynnRoadDirt).material == 12);
            auto top = [&](float depth, float zOffset, float rimShare, float rim, float press)
            { return zOffset + blend::DeformedDepth(depth, rimShare, rim, press, v.stiffness, 1.0f); };
            const float untouched = top(v.depth, v.zOffset, v.rim, 0.0f, 0.0f), ridge = top(v.depth, v.zOffset, v.rim, 1.0f, 0.0f);
            const float pressed = top(v.depth, v.zOffset, v.rim, 0.0f, 1.0f);
            std::printf("  mud above the grass: untouched %+.3f, a rim %+.3f, the trench floor %+.3f yd (before: rim %+.3f)\n", untouched, ridge, pressed,
                        top(0.04f, -0.08f, 7.0f, 1.0f, 0.0f));
            CHECK(untouched < 0.0f && untouched > -0.01f && pressed < 0.0f);
            CHECK(ridge > 0.02f && ridge < 0.035f);
            CHECK(top(0.04f, -0.08f, 7.0f, 1.0f, 0.0f) > 0.2f); // what it was
            // A rim only shows where the dirt is painted at about half or more (Shape smoothsteps the coverage).
            auto shape = [](float c) { return c * c * (3.0f - 2.0f * c); };
            CHECK(top(v.depth * shape(0.3f), v.zOffset, v.rim, 1.0f, 0.0f) < 0.0f && top(v.depth * shape(0.6f), v.zOffset, v.rim, 1.0f, 0.0f) > 0.0f);
            // The Swamp of Sorrows' muck as it was.
            const ct::Values m = At(kSwampMuck);
            CHECK(Near(m.depth, 0.02f) && Near(m.zOffset, -0.08f) && Near(m.rim, 6.0f));
        });

        Test("trench edge filter: no wall or rim narrower than its cells (the grid's staircase), the original widths for big trenches", []
        {
            const float cell = 0.25f;
            // Softness 0: exactly the original supports, for any size.
            for (float r : { 0.1f, 0.4f, 1.0f, 2.5f })
            {
                const blend::TrenchShape s = blend::TrenchShapeFor(r, cell, 0.0f);
                CHECK(Near(s.pressIn, 0.7f * r) && Near(s.pressOut, r) && Near(s.rimPeak, 1.25f * r) && Near(s.rimHalf, 0.35f * r));
                CHECK(blend::TrenchProfile(0.0f, s).press == 1.0f && blend::TrenchProfile(0.69f * r, s).press == 1.0f);
                CHECK(blend::TrenchProfile(r, s).press == 0.0f && blend::TrenchProfile(1.25f * r, s).rim == 1.0f);
                CHECK(blend::TrenchProfile(0.89f * r, s).rim == 0.0f && blend::TrenchProfile(1.61f * r, s).rim == 0.0f);
                CHECK(Near(blend::TrenchReach(s), 1.6f * r));
            }
            // From 1.25 yd up the filter changes nothing.
            for (float r : { 1.25f, 1.8f, 2.5f })
            {
                const blend::TrenchShape a = blend::TrenchShapeFor(r, cell, 0.0f), b = blend::TrenchShapeFor(r, cell, 1.5f);
                CHECK(Near(a.pressIn, b.pressIn) && Near(a.pressOut, b.pressOut) && Near(a.rimPeak, b.rimPeak) && Near(a.rimHalf, b.rimHalf));
            }
            // Small trenches (a human's 0.4 yd, a cat's 0.15): every wall and rim flank at least the filter's width; the
            // largest step between two vertices a cell apart, before and after.
            auto widest = [&](const blend::TrenchShape& s, bool rim)
            {
                float most = 0.0f;
                for (float d = 0.0f; d < 3.0f; d += 0.005f)
                {
                    const float a = rim ? blend::TrenchProfile(d, s).rim : blend::TrenchProfile(d, s).press;
                    const float b = rim ? blend::TrenchProfile(d + cell, s).rim : blend::TrenchProfile(d + cell, s).press;
                    most = std::fmax(most, std::fabs(a - b));
                }
                return most;
            };
            for (float r : { 0.15f, 0.4f, 0.8f })
            {
                const blend::TrenchShape before = blend::TrenchShapeFor(r, cell, 0.0f), after = blend::TrenchShapeFor(r, cell, 1.5f);
                CHECK(after.pressOut - after.pressIn >= 1.5f * cell - 1e-5f && after.rimHalf >= 1.5f * cell - 1e-5f);
                CHECK(after.amplitude > 0.0f && after.amplitude <= 1.0f && (r < 0.22f || after.amplitude == 1.0f));
                CHECK(Near(0.5f * (after.pressIn + after.pressOut), 0.85f * r) || after.pressIn == 0.0f); // the wall around the same middle
                CHECK(after.rimPeak >= after.pressOut - 1e-6f && after.rimPeak >= 1.25f * r - 1e-6f);
                const float stepBefore = std::fmax(widest(before, false), widest(before, true)), stepAfter = std::fmax(widest(after, false), widest(after, true));
                std::printf("  trench %.2f yd: largest step between neighbouring vertices %.2f -> %.2f\n", r, stepBefore, stepAfter);
                CHECK(stepAfter <= 0.86f && stepAfter < stepBefore);
                if (r < 0.5f) CHECK(stepBefore > 0.99f); // the original: all or nothing from one vertex to the next
            }
            // A critter's trench, too small for the filter: a dimple one wall wide, shallower; continuous in its size.
            CHECK(Near(blend::TrenchShapeFor(0.15f, cell, 1.5f).amplitude, 0.68f) && blend::TrenchShapeFor(0.4f, cell, 1.5f).amplitude == 1.0f);
            for (float r = 0.05f; r < 0.5f; r += 0.001f)
            {
                const blend::TrenchShape a = blend::TrenchShapeFor(r, cell, 1.5f), b = blend::TrenchShapeFor(r + 0.001f, cell, 1.5f);
                CHECK(std::fabs(a.amplitude - b.amplitude) < 0.01f && std::fabs(a.pressOut - b.pressOut) < 0.01f && std::fabs(a.pressIn - b.pressIn) < 0.01f);
            }
            // Continuous: no jump anywhere along the profile.
            const blend::TrenchShape h = blend::TrenchShapeFor(0.4f, cell, 1.5f);
            for (float d = 0.0f; d < 1.5f; d += 0.001f)
                CHECK(std::fabs(blend::TrenchProfile(d + 0.001f, h).press - blend::TrenchProfile(d, h).press) < 0.01f &&
                      std::fabs(blend::TrenchProfile(d + 0.001f, h).rim - blend::TrenchProfile(d, h).rim) < 0.01f);
        });

        Test("feet flatten the rims of earlier steps: a walked path is a trench between two rims, not one raised band", []
        {
            // A unit 0.4 yd wide walks along x (a step every 0.05 yd, as at 60 fps); vertices across its path every cell.
            const float cell = 0.25f, r = 0.4f;
            const blend::TrenchShape s = blend::TrenchShapeFor(r, cell, 1.5f);
            float pressNew[13] = {}, rimNew[13] = {}, pressOld[13] = {}, rimOld[13] = {};
            for (float x = -3.0f; x <= 3.0f; x += 0.05f)
                for (int k = 0; k < 13; ++k)
                {
                    const float y = (k - 6) * cell, d = std::sqrt(x * x + y * y);
                    const blend::TrenchStamp st = blend::TrenchProfile(d, s);
                    rimNew[k] = blend::StampedRim(rimNew[k], st.press, st.rim, pressNew[k]);
                    pressNew[k] = std::fmax(pressNew[k], st.press);
                    pressOld[k] = std::fmax(pressOld[k], st.press);
                    rimOld[k] = std::fmax(rimOld[k], st.rim);
                }
            // Before: the rim stood over the whole path, the trench floor included. Now: none where feet pressed.
            CHECK(rimOld[6] > 0.9f && pressOld[6] == 1.0f);
            CHECK(rimNew[6] < 0.01f && pressNew[6] == 1.0f);
            // The rims beside the path stay (the walker passes them by), and the path is symmetric.
            CHECK(rimNew[4] > 0.5f && rimNew[8] > 0.5f && Near(rimNew[4], rimNew[8]) && rimNew[0] == 0.0f);
            // Mud like Elwynn's: the walked path's middle stays under the grass, the rims beside it show.
            const float depth = 0.02f, zOffset = -0.025f, rimShare = 1.75f, stiff = 0.8f;
            const float middleNow = zOffset + blend::DeformedDepth(depth, rimShare, rimNew[6], pressNew[6], stiff, 1.0f);
            const float middleOld = zOffset + blend::DeformedDepth(depth, rimShare, rimOld[6], pressOld[6], stiff, 1.0f);
            const float side = zOffset + blend::DeformedDepth(depth, rimShare, rimNew[4], pressNew[4], stiff, 1.0f);
            std::printf("  a walked path in mud: its middle %+.3f yd (was %+.3f), a rim beside it %+.3f\n", middleNow, middleOld, side);
            CHECK(middleNow < 0.0f && middleOld > 0.0f && side > 0.0f);
            // Standing still: the stamp settles at once and stays (a ring outside the press untouched, inside it lowered).
            for (float d : { 0.3f, 0.5f, 0.7f })
            {
                const blend::TrenchStamp st = blend::TrenchProfile(d, s);
                float rim = 0.0f, press = 0.0f, settled = -1.0f;
                for (int i = 0; i < 10; ++i)
                {
                    rim = blend::StampedRim(rim, st.press, st.rim, press);
                    press = std::fmax(press, st.press);
                    if (i == 1) settled = rim;
                }
                CHECK(Near(rim, settled) && Near(rim, st.rim * (1.0f - st.press)));
            }
            CHECK(blend::StampedRim(0.8f, 0.0f, 0.3f, 0.0f) == 0.8f && blend::StampedRim(0.8f, 1.0f, 0.3f, 0.0f) == 0.3f);
            CHECK(Near(blend::StampedRim(0.8f, 0.5f, 0.1f, 0.0f), 0.4f) && blend::StampedRim(0.0f, 0.0f, 1.0f, 1.0f) == 0.0f);
        });

        Test("rain fills mud ruts: a quarter of the time in heavy rain, as the table says in dry weather and off mud", []
        {
            CHECK(blend::RainRelaxShare(0.0f, 1.0f, 0.75f) == 1.0f && blend::RainRelaxShare(1.0f, 0.0f, 0.75f) == 1.0f);
            CHECK(Near(blend::RainRelaxShare(1.0f, 1.0f, 0.75f), 0.25f) && Near(blend::RainRelaxShare(0.5f, 1.0f, 0.75f), 0.625f));
            CHECK(blend::RainRelaxShare(1.0f, 1.0f, 0.0f) == 1.0f && blend::RainRelaxShare(std::nanf(""), 1.0f, 0.75f) == 1.0f);
            CHECK(blend::RainRelaxShare(2.0f, 3.0f, 0.75f) == 0.25f);
            std::printf("  Elwynn's ruts fill in in %.1f s dry, %.1f s in heavy rain\n", 22.0f, 22.0f * blend::RainRelaxShare(1.0f, 1.0f, 0.75f));
        });

        Test("the tables reload: a second load gives the same answers", [&]
        {
            const ct::Values first = At(kSearingAsh);
            const uint32_t generation = ct::Generation();
            LoadFrom(now);
            const ct::Values again = At(kSearingAsh);
            CHECK(ct::Generation() != generation && again.material == first.material && again.depth == first.depth && CoverPath(again) == CoverPath(first));
        });

        std::printf("%u test groups passed\n", g_passed);
        return 0;
    }
    catch (const std::exception& e)
    {
        std::printf("FAIL %s\n", e.what());
        return 1;
    }
}
