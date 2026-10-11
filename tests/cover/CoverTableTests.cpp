// Offline tests for the ash and sand covers: GroundMaterial, GroundMaterialSelector and SurfaceCover
// load through the real table code (features/GroundMaterialTable, SurfaceCoverTable) over a small
// stand-in for wxl-seyris-tools' cdbc reader, and resolve for sample (area, texture) pairs the way the
// terrain asks; the snow and every other row stay exactly as they were; and the two look rules of
// features/CoverBlend.hpp leave snow's numbers bit-identical. No game, no WXL core.
//
// Usage:  cover_table_tests <data dir> [<data dir before the change>]
//   (the repo's data/; the second one, e.g. the files from git HEAD, adds the unchanged-rows checks)
#include "features/CoverBlend.hpp"
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
    const Spot kElwynnDirt   { "Elwynn dirt (footprint outlines)", 12, 0, "tileset\\elwynn\\elwynndirtbase2.blp", 0, 0 };
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
    const Spot* const kUnchanged[] = { &kDunMorogh, &kNorthSnow, &kWinterspring, &kWestfall, &kElwynnDirt, &kStormIce, &kElwynnGrass,
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
                               &kBlasted, &kHellfire, &kElwynnGrass, &kDunMorogh, &kNorthSnow })
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
