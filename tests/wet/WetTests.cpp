// Wet surfaces (env/WetSurfaceModel, features/WetSurfaces, their pieces in features/TerrainWetness's terrain edit and
// features/ModelIce's wet style for buildings) offline. env/Fields' part (the grid's surface bytes) runs in
// fields_shore_tests, which has the stub world that the real Fields.cpp needs.
//  1. The rules: a zone's film and puddles in rain and after it (the dry-out time, the climate's pace), a spot's share
//     (roofs, water, snow, hot ground), the puddle potential and threshold, the layers' and the buildings' textures' shine,
//     the shaders' constants (all zeros while off).
//  2. The ground tables through the real reader (wxl-seyris-tools' Cdbc.cpp linked in, materials::Load as the game runs it,
//     on copies of the repo's tables in a scratch DBFilesClient): WetShine read and inherited along Parent, the layers' shine
//     for real textures through GroundMaterialSelector (wetsurfaces::LayerShine, what the chunk draw sets c160 from), and an
//     older 8-column GroundMaterial.cdbc still loading (every layer by its TerrainType then).
//  3. The shaders: every stock terrain pixel shader through the real edit (terrainwet::EditPixelForTests) with the pieces and
//     without them: all assemble within ps_3_0's 512 slots, every one carries them; the largest both ways. Every WMO shadow
//     variant with the rims and the glass blocks (modelice::EditPixelWmoGlassForTests) and every M2 one with the block:
//     the facing's two lines in each, the largest within kGlassMinSlots and 512.
//  4. On WARP (the CPU rasterizer behind D3D9On12; no GPU): a patched 4-layer terrain variant drawn in perspective over a
//     40-yard patch, stone on one side and grass on the other (their shine from wetsurfaces::LayerShine by TerrainType),
//     the constants from wetsurf::TerrainConstants, our light grid from the real pipeline (lights::UpdateForTests,
//     modellights::PrepareForTests) and the surface texture laid out as the game's (the moisture grid's toroidal layout):
//     dry exactly as with the stage off; wet stone darker with the sky in it, the grass as it was; at night a puddle on
//     the stone with a torch whose mirror image falls in it. A contact sheet (--sheet <bmp>).
//  5. On WARP: the model block in the wet style (wetsurfaces::BuildingConstants) on a floor, a wall and a ceiling; the ice
//     with the facing constant zero exactly as its weight says.
//  6. Cost: the zones' step and the per-cell work over a 128 x 128 grid.
//  The compare key (debug/Compare) carries wet surfaces' own switch (with the rules).
// Run: wet_tests <folder with Terrain*.bls> <folder with the M2 ps_Combiners_*.bls> <folder with the WMO ps_MapObj*.bls>
//      <repo data folder> [<map scan's WMO texture lists (.tsv)>...] [--no-warp] [--sheet <bmp>]
#include "debug/Compare.hpp"
#include "env/Lights.hpp"
#include "env/WetSurfaceModel.hpp"
#include "features/GroundMaterialTable.hpp"
#include "features/ModelIce.hpp"
#include "features/ModelLights.hpp"
#include "features/TerrainIce.hpp"
#include "features/TerrainWetness.hpp"
#include "features/WetSurfaces.hpp"
#include "render/ShaderPatch.hpp"
#include "wxl/PluginApi.h"
#include "wxl_seyris/CdbcApi.hpp"

#include <windows.h>
#include <d3d9.h>
#include <d3d9on12.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace la = wxl_livingazeroth;
namespace ws = wxl_livingazeroth::wetsurf;

// wxl-seyris-tools' reader (its src/Cdbc.cpp, linked in; what its Extension.cpp publishes as "seyris.cdbc").
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
    std::string F(const char* fmt, ...)
    {
        char buf[1024];
        va_list a;
        va_start(a, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, a);
        va_end(a);
        return buf;
    }
    template <class T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

    // --- 1. the rules ---------------------------------------------------------------------------------------------------
    void Rules()
    {
        std::printf("-- the rules (env/WetSurfaceModel)\n");
        // Rain: heavy rain wets surfaces in about kWetUpSeconds, light rain slower; puddles fill slower than the film.
        {
            ws::ZoneWet heavy, light;
            for (int k = 0; k < 45; ++k) { ws::Step(heavy, 1.0f, true, 1.0f, 1.0f, 15.0f); ws::Step(light, 1.0f, true, 0.0f, 1.0f, 15.0f); }
            Check(heavy.film > 0.6f && heavy.film < 0.66f && light.film < 0.3f && light.film > 0.2f && heavy.pool < 0.2f,
                  F("rain for 45 s: heavy rain's film %.2f (63%% in kWetUpSeconds), light rain's %.2f, the puddles %.2f behind", heavy.film, light.film, heavy.pool));
            ws::ZoneWet full;
            for (int k = 0; k < 600; ++k) ws::Step(full, 1.0f, true, 1.0f, 1.0f, 15.0f);
            Check(full.film > 0.999f && full.pool > 0.9f, F("10 minutes of heavy rain: film %.3f, puddles %.3f", full.film, full.pool));
        }
        // Drying: linear, the film over DryOutMinutes (a mild day), the puddles kPoolDryFactor x as long; a hot, dry, windy
        // day twice as fast (the pace capped at 2).
        {
            ws::ZoneWet z{ 1.0f, 1.0f };
            int filmGone = -1, poolGone = -1;
            for (int s = 1; s <= 3600 && (filmGone < 0 || poolGone < 0); ++s)
            {
                ws::Step(z, 1.0f, false, 0.0f, 1.0f, 15.0f);
                if (filmGone < 0 && z.film <= 0.0f) filmGone = s;
                if (poolGone < 0 && z.pool <= 0.0f) poolGone = s;
            }
            ws::ZoneWet hot{ 1.0f, 1.0f };
            int hotGone = -1;
            for (int s = 1; s <= 3600 && hotGone < 0; ++s) { ws::Step(hot, 1.0f, false, 0.0f, 5.0f, 15.0f); if (hot.film <= 0.0f) hotGone = s; }
            Check(std::abs(filmGone - 900) <= 1 && std::abs(poolGone - 1350) <= 1 && std::abs(hotGone - 450) <= 1,
                  F("drying after the rain (DryOutMinutes 15): the film gone in %d s, the puddles in %d s; at the capped pace x2 in %d s", filmGone, poolGone, hotGone));
            Check(ws::DryRateFactor(3.0f, 1.0f) == 2.0f && ws::DryRateFactor(0.1f, 1.0f) == 0.5f && ws::DryRateFactor(NAN, 1.0f) == 1.0f,
                  "the climate's drying pace, relative to a mild day, kept within 0.5 .. 2");
            ws::ZoneWet bad{ NAN, 7.0f };
            ws::Step(bad, NAN, true, NAN, NAN, NAN);
            ws::Step(bad, 1.0f, false, NAN, NAN, NAN);
            Check(std::isfinite(bad.film) && std::isfinite(bad.pool) && bad.film >= 0.0f && bad.pool <= 1.0f, "NaN in, finite 0..1 out");
        }
        // A spot's share: open sky, no roof, water, snow or hot ground.
        Check(ws::SpotFilm(1.0f, 1.0f, 0.0f, 0.0f, false) == 1.0f && ws::SpotFilm(1.0f, 0.0f, 0.0f, 0.0f, false) == 0.0f &&
              ws::SpotFilm(1.0f, 1.0f, 0.0f, 0.0f, true) == 0.0f && ws::SpotFilm(1.0f, 1.0f, 0.05f, 0.0f, false) == 0.0f &&
              ws::SpotFilm(1.0f, 1.0f, 0.0f, 1.0f, false) == 0.0f && std::fabs(ws::SpotFilm(1.0f, 1.0f, 0.025f, 0.0f, false) - 0.5f) < 1e-5f,
              "a spot's film: all of it in the open, none under a roof, under water, under 5 cm of snow or on hot ground; half under 2.5 cm");
        // Puddles: flat hollows first, then flat patches by the noise as the pool rises; never on slopes or beyond the grid.
        {
            const float hollow = ws::PuddlePotential(0.3f, 0.02f, 0.5f), flat = ws::PuddlePotential(0.0f, 0.02f, 0.5f), steep = ws::PuddlePotential(0.3f, 0.3f, 1.0f);
            Check(hollow > 0.75f && flat > 0.25f && flat < 0.35f && steep == 0.0f,
                  F("the potential: a flat hollow 0.3 yd deep %.2f, flat ground at middling noise %.2f, a slope %.2f", hollow, flat, steep));
            Check(ws::PuddleAt(hollow, 0.0f, 1.0f) == 0.0f && ws::PuddleAt(hollow, 1.0f, 1.0f) > 0.99f && ws::PuddleAt(flat, 1.0f, 1.0f) == 0.0f &&
                  ws::PuddleAt(flat, 1.0f, 2.0f) > 0.9f && ws::PuddleAt(hollow, 1.0f, 0.0f) == 0.0f && ws::PuddleThreshold(1.0f, 2.0f) == ws::kMinThreshold,
                  "puddles: none before the pool rises, the hollow full when it's full, flat ground only with Puddles 2, none with Puddles 0; the threshold never under kMinThreshold");
            // The noise: the same per cell, 0..1, smooth (neighbours close), varied (a spread over a field).
            float lo = 1.0f, hi = 0.0f, jump = 0.0f;
            for (int j = -50; j < 50; ++j)
                for (int i = -50; i < 50; ++i)
                {
                    const float n = ws::CellNoise(i, j);
                    lo = std::min(lo, n); hi = std::max(hi, n);
                    jump = std::max(jump, std::fabs(n - ws::CellNoise(i + 1, j)));
                }
            Check(ws::CellNoise(7, -3) == ws::CellNoise(7, -3) && lo >= 0.0f && hi <= 1.0f && hi - lo > 0.6f && jump < 0.45f,
                  F("the cells' noise: %.2f .. %.2f over 100 x 100 cells, neighbours at most %.2f apart", lo, hi, jump));
        }
        // What shines: grass and leaves never, stone most; a material's own value wins; ice, snow and hot ground never.
        Check(ws::TerrainTypeShine(5) == 0.0f && ws::TerrainTypeShine(6) == 0.0f && ws::TerrainTypeShine(2) == 1.0f && ws::TerrainTypeShine(3) == 0.0f &&
              ws::TerrainTypeShine(0) == 0.8f && ws::TerrainTypeShine(-1) == 0.6f, "TerrainType: grass 0, leaves 0, stone 1, snow 0, dirt 0.8, no ground effect 0.6");
        Check(ws::LayerShine(true, 0.35f, false, false, false, 2) == 0.35f && ws::LayerShine(false, 0.0f, false, false, false, 2) == 1.0f &&
              ws::LayerShine(true, 1.0f, true, false, false, 2) == 0.0f && ws::LayerShine(true, 1.0f, false, true, false, 2) == 0.0f &&
              ws::LayerShine(true, 1.0f, false, false, true, 2) == 0.0f, "a layer: its material's WetShine over its TerrainType; clear ice, snow, hot ground none");
        // Buildings' textures.
        struct Tex { const char* name; float shine; };
        const Tex texes[] = {
            { "World\\wmo\\Azeroth\\Buildings\\Stormwind\\mm_strmwnd_wall_04.blp", 1.0f }, { "mm_strmwnd_int_floor_01.blp", 1.0f },
            { "zuldrak_roadcurb_01.blp", 1.0f }, { "Dungeons\\Textures\\Floor\\JLO_Flagstone_01.BLP", 1.0f }, { "mm_woodroof_01.blp", 0.7f },
            { "redridgedockplank02.blp", 0.5f }, { "jlo_worc_metal01.blp", 0.8f }, { "mm_westfall_thatch_01.blp", 0.15f },
            { "mm_elwynn_wnd_ext__01.blp", 0.0f }, { "jlo_worc_bannertorn.blp", 0.0f }, { "mm_rope_01.blp", 0.0f }, { "wr_wall_ionic_snow.blp", 0.0f },
            { "jlo_draenei_envmap.blp", 0.0f }, { "jlo_icec_floorskull.blp", 0.0f }, { "dalaran_crystal_01.blp", 0.0f }, { "mm_leaves_01.blp", 0.0f },
            { "fx_firering.blp", 0.0f }, { "something_else.blp", 0.5f },
        };
        bool texOk = true;
        std::string texBad;
        for (const Tex& t : texes)
            if (std::fabs(ws::WmoTextureShine(t.name) - t.shine) > 1e-6f) { texOk = false; texBad += std::string(" ") + t.name + F("=%.2f", ws::WmoTextureShine(t.name)); }
        Check(texOk, "buildings' textures: walls, floors, curbs, flagstones 1; roofs 0.7; metal 0.8; planks 0.5; thatch 0.15; glass, banners, rope, snow, "
                     "environment maps, Icecrown's ice, crystals, leaves, fire none; anything else 0.5" + texBad);
        Check(la::wetsurfaces::BuildingShine("mm_strmwnd_wall_04.blp", 0, 0) == 1.0f && la::wetsurfaces::BuildingShine("mm_strmwnd_wall_04.blp", 2, 0) == 0.0f &&
              la::wetsurfaces::BuildingShine("mm_strmwnd_wall_04.blp", 0, 0x1) == 0.0f && la::wetsurfaces::BuildingShine("mm_strmwnd_wall_04.blp", 1, 0x10) == 0.0f,
              "a building material: opaque or alpha-keyed only, never unlit or night-lit");
        // The compare key (debug/Compare) switches wet surfaces off and back through their own Enabled.
        {
            size_t n = 0;
            const la::compare::Switch* sw = la::compare::Switches(n);
            const la::compare::Switch* wet = nullptr;
            for (size_t i = 0; i < n; ++i) if (std::strcmp(sw[i].name, "wet surfaces") == 0) wet = &sw[i];
            bool ok = wet && wet->off == 0.0f && wet->get() == 1.0f;
            if (wet)
            {
                wet->set(wet->off);
                ok = ok && la::wetsurfaces::Config().enabled == 0;
                wet->set(1.0f);
                ok = ok && la::wetsurfaces::Config().enabled == 1;
            }
            Check(ok, F("the compare key's %zu switches include wet surfaces' own Enabled, off and back", n));
        }
        // The constants: off, every highlight's gain, the film and the darkening 0, no puddles.
        {
            ws::Settings off;
            off.enabled = 0;
            ws::Frame f;
            f.film = 1.0f; f.pool = 1.0f;
            float c[4 * ws::kTerrainRegisters];
            ws::TerrainConstants(off, f, c);
            Check(c[3] == 0.0f && c[4] == -1.0f && c[6] == 0.0f && c[16] == 0.0f && c[17] == 0.0f && c[24] == 0.0f && c[25] == 0.0f,
                  "the constants while off: no far film, no puddles, no darkening, no sky, no highlights");
            ws::Settings on;
            ws::TerrainConstants(on, f, c);
            Check(std::fabs(c[3] - ws::kSurfaceShare) < 1e-6f && std::fabs(c[4] + ws::PuddleThreshold(1.0f, 1.0f)) < 1e-6f && c[8] * (1.0f - ws::kWetLobeCos) > 0.999f &&
                  std::fabs(c[8] * ws::kWetLobeCos + c[9]) < 1e-4f && c[16] > 0.97f && c[17] == ws::kF0,
                  "the constants while on: the far film at kSurfaceShare, the threshold, the lobes 1 along the mirror and 0 at their cosines, Fresnel");
        }
    }

    // --- 2. the tables through the real reader ---------------------------------------------------------------------------
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

    // The table without its last column (an older file's layout).
    bool WriteWithoutLastColumn(const fs::path& from, const fs::path& to)
    {
        std::ifstream f(from, std::ios::binary);
        std::vector<char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (b.size() < 20 || std::memcmp(b.data(), "WDBC", 4) != 0) return false;
        uint32_t h[4];
        std::memcpy(h, b.data() + 4, 16);
        const uint32_t rows = h[0], cols = h[1], strings = h[3];
        if (h[2] != cols * 4 || cols < 2) return false;
        std::ofstream o(to, std::ios::binary | std::ios::trunc);
        const uint32_t nh[4] = { rows, cols - 1, (cols - 1) * 4, strings };
        o.write("WDBC", 4);
        o.write(reinterpret_cast<const char*>(nh), 16);
        for (uint32_t r = 0; r < rows; ++r) o.write(b.data() + 20 + static_cast<size_t>(r) * cols * 4, (cols - 1) * 4);
        o.write(b.data() + 20 + static_cast<size_t>(rows) * cols * 4, strings);
        return static_cast<bool>(o);
    }

    float Shine(const char* texture, int terrainType)
    {
        la::terrain::Surface s;
        s.texture = texture;
        s.area = 0;
        s.groundEffect = 0;
        s.terrainType = terrainType;
        return la::wetsurfaces::LayerShine(la::materials::Select(0, 0, texture, 0, terrainType), s, 0);
    }

    void Tables(const fs::path& data)
    {
        std::printf("-- the ground tables through the real reader (materials::Load over wxl-seyris-tools' Cdbc.cpp)\n");
        std::error_code ec;
        const fs::path root = fs::temp_directory_path() / ("la-wet-tables-" + std::to_string(GetCurrentProcessId()));
        const fs::path db = root / "DBFilesClient";
        fs::create_directories(db, ec);
        bool copied = true;
        for (const char* name : { "GroundMaterial.cdbc", "GroundMaterialSelector.cdbc" })
            copied = copied && fs::copy_file(data / name, db / name, fs::copy_options::overwrite_existing, ec);
        Check(copied, "the tables copied to " + db.string());
        if (!copied) return;
        const fs::path cwd = fs::current_path();
        fs::current_path(root);
        const WXL_SeyrisCdbcApi api = LinkedApi();
        la::materials::Load(&api);
        const std::string status = la::materials::Status();
        Check(status.find("11 with WetShine") != std::string::npos && status.find("GroundMaterial:") == std::string::npos,
              "loaded: " + status);
        using la::materials::Get;
        const auto set = [](uint32_t id, float v) { const la::materials::Values m = Get(id); return m.wetShineSet && std::fabs(m.wetShine - v) < 1e-6f; };
        Check(set(80, 1.0f) && set(70, 0.9f) && set(71, 0.9f) && set(51, 0.35f) && set(2, 0.35f) && set(1, 0.0f) && set(4, 0.0f) && set(30, 0.0f) &&
              !Get(12).wetShineSet && !Get(22).wetShineSet,
              "WetShine read and along Parent: Paved 1, Mud 0.9 and DesolaceMudSolid (its child) 0.9, Sand 0.35 and TanarisSandBase01 0.35, "
              "Snow 0 and IronforgeSnow01Solid 0, ClearIce 0; ElwynnDirtBase2 and Vashj'ir Sand not set");
        // The layers as the chunk draw sees them (texture, its ground effect's TerrainType).
        const float cobble = Shine("Tileset\\Elwynn\\ElwynnCobblestoneBase.blp", 2), dirt = Shine("Tileset\\Elwynn\\ElwynnDirtBase2.blp", 0),
                    grass = Shine("Tileset\\Elwynn\\ElwynnGrassBase.blp", 5), ice = Shine("Tileset\\Expansion02\\StormPeaks\\SP_IceA.blp", -1),
                    snow = Shine("Tileset\\Expansion02\\StormPeaks\\SP_SnowA.blp", 3), sand = Shine("Tileset\\Tanaris\\TanarisSandBase01.blp", 7),
                    lava = Shine("Tileset\\BurningStepps\\BurningSteppsLavaTest02.blp", 0), rock = Shine("Tileset\\Elwynn\\ElwynnRockBase.blp", -1),
                    muck = Shine("Tileset\\swamp of sorrows\\SwampSorrowsMuckDark01.blp", 8);
        Check(cobble == 1.0f && std::fabs(dirt - 0.8f) < 1e-6f && grass == 0.0f && ice == 0.0f && snow == 0.0f && std::fabs(sand - 0.35f) < 1e-6f &&
                  lava == 0.0f && std::fabs(rock - 0.6f) < 1e-6f && std::fabs(muck - 0.9f) < 1e-6f,
              F("the layers: Elwynn's cobbles (Paved) %.2f, its dirt (TerrainType Dirt) %.2f, its grass (Grass) %.2f, Storm Peaks' ice (ClearIce) %.2f and snow %.2f, "
                "Tanaris sand %.2f, Burning Steppes' lava %.2f, rock without a ground effect %.2f, Swamp of Sorrows' muck %.2f",
                cobble, dirt, grass, ice, snow, sand, lava, rock, muck));
        // An older GroundMaterial.cdbc (8 columns, before WetShine): loads, nothing set, every layer by its TerrainType.
        const bool older = WriteWithoutLastColumn(data / "GroundMaterial.cdbc", db / "GroundMaterial.cdbc");
        la::materials::Load(&api);
        Check(older && Get(80).wetShineSet == false && la::materials::MaterialCount() > 40 && Shine("Tileset\\Elwynn\\ElwynnCobblestoneBase.blp", 2) == 1.0f &&
                  Shine("Tileset\\Tanaris\\TanarisSandBase01.blp", 7) == 0.5f,
              "an 8-column GroundMaterial.cdbc still loads (" + std::string(la::materials::Status()) + "), every layer by its TerrainType");
        fs::current_path(cwd);
        fs::remove_all(root, ec);
    }

    // --- 3. the shaders -----------------------------------------------------------------------------------------------------
    struct Blob { std::string name; std::vector<uint8_t> code; };
    std::vector<Blob> LoadBlobs(const fs::path& dir, uint32_t version = 0xFFFF0300u)
    {
        std::vector<Blob> out;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, ec))
        {
            if (entry.path().extension() != ".bls") continue;
            std::ifstream f(entry.path(), std::ios::binary);
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            int k = 0;
            for (size_t i = 0; i + 8 <= data.size(); i += 4)
            {
                uint32_t token;
                std::memcpy(&token, data.data() + i, 4);
                if (token != version) continue;
                size_t j = i + 4;
                for (; j + 4 <= data.size(); j += 4)
                {
                    uint32_t t;
                    std::memcpy(&t, data.data() + j, 4);
                    if (t == 0x0000FFFFu) break;
                }
                if (j + 4 > data.size()) break;
                char name[96];
                std::snprintf(name, sizeof(name), "%s_%02d", entry.path().stem().string().c_str(), k++);
                out.push_back({ name, std::vector<uint8_t>(data.begin() + i, data.begin() + j + 4) });
                i = j;
            }
        }
        return out;
    }
    int Slots(const std::vector<uint8_t>& code)
    {
        std::string text, why;
        if (!la::shaderpatch::Disassemble(code.data(), code.size(), text, why)) return -1;
        std::smatch m;
        static const std::regex slots(R"(approximately (\d+) instruction slots)");
        return std::regex_search(text, m, slots) ? std::stoi(m[1].str()) : -1;
    }

    struct TerrainPick { std::string name, text; std::vector<uint8_t> code; };

    // Every terrain variant with and without the pieces; the variant section 4 draws.
    TerrainPick TerrainShaders(const fs::path& folder)
    {
        std::printf("-- the terrain shaders (terrainwet::EditPixelForTests)\n");
        const std::vector<Blob> blobs = LoadBlobs(folder);
        Check(blobs.size() == 292, F("stock terrain pixel shaders found: %zu", blobs.size()));
        TerrainPick pick;
        int with = 0, assembled = 0, maxWith = 0, maxWithout = 0, slotsWithout = 0;
        std::string maxName;
        for (const Blob& b : blobs)
        {
            std::string stock, why;
            if (!la::shaderpatch::Disassemble(b.code.data(), b.code.size(), stock, why)) continue;
            std::string src = stock, plain = stock;
            la::wetsurfaces::SetPiecesForTests(false);
            const bool okPlain = la::terrainwet::EditPixelForTests(plain, why);
            la::wetsurfaces::SetPiecesForTests(true);
            if (!la::terrainwet::EditPixelForTests(src, why)) continue;
            std::vector<uint8_t> code, plainCode;
            if (!la::shaderpatch::Assemble(src, b.name.c_str(), code, why))
            {
                static bool dumped = false;
                if (!dumped) { dumped = true; std::printf("      first failure: %s: %s\n", b.name.c_str(), why.substr(0, 300).c_str()); }
                continue;
            }
            ++assembled;
            with += src.find("dcl_2d s15") != std::string::npos && src.find("mad r4.xyz, r19, r5.x, r4") != std::string::npos ? 1 : 0;
            const int s = Slots(code);
            if (s > maxWith) { maxWith = s; maxName = b.name; slotsWithout = -1; }
            if (okPlain && la::shaderpatch::Assemble(plain, b.name.c_str(), plainCode, why))
            {
                const int p = Slots(plainCode);
                maxWithout = std::max(maxWithout, p);
                if (b.name == maxName) slotsWithout = p;
                if (const char* slotsFile = std::getenv("WET_SLOTS")) std::ofstream(slotsFile, std::ios::app) << "Terrain\t" << b.name << "\t" << p << "\t" << s << "\n";
            }
            // A 4-layer variant that blends in sequence, without shadows (section 4 draws it; it has every piece).
            if (pick.name.empty() && b.name.rfind("Terrain1_", 0) == 0 && stock.find("dcl_2d s4") != std::string::npos &&
                stock.find("dcl_2d s5") == std::string::npos && stock.find("lrp") != std::string::npos)
                pick = TerrainPick{ b.name, src, code };
        }
        Check(assembled == 289 && with == 289, F("every patched variant assembles (%d of 289) and carries the wet pieces (%d)", assembled, with));
        Check(maxWith > 0 && maxWith <= 512, F("largest patched variant with the wet pieces: %d of 512 instruction slots (%s; %d of it without them; the largest "
                                               "without them %d: the terrain wetness's debug views out of the shader gave 9 back, from 475)", maxWith, maxName.c_str(),
                                               slotsWithout, maxWithout));
        return pick;
    }

    struct ModelPick { std::string name, text, vP, vN; std::vector<uint8_t> code; };

    // The block as it was before the facing (its two lines out, the weight c179.x again): the variant's slots then.
    int SlotsBeforeFacing(const std::string& src, const char* name)
    {
        std::string old = src;
        for (const char* line : { "\n      mad_sat r30.w, -r15.z, c77.y, c77.x", "\n      mad r30.w, -r30.w, c179.x, c179.x" })
        {
            const size_t at = old.find(line);
            if (at == std::string::npos) return -1;
            old.erase(at, std::strlen(line));
        }
        const std::string now = "mul r31.xyz, r31, r30.w";
        const size_t at = old.find(now);
        if (at == std::string::npos) return -1;
        old.replace(at, now.size(), "mul r31.xyz, r31, c179.x");
        std::vector<uint8_t> code;
        std::string why;
        return la::shaderpatch::Assemble(old, name, code, why) ? Slots(code) : -1;
    }

    ModelPick ModelShaders(const fs::path& m2, const fs::path& wmo)
    {
        std::printf("-- the model shaders (modelice: the block's weight by its facing, c77)\n");
        ModelPick pick;
        int m2Patched = 0, m2Facing = 0, m2Max = 0, wmoPatched = 0, wmoFacing = 0, wmoMax = 0, worstDelta = 0, deltas = 0;
        std::string wmoMaxName;
        // WET_SLOTS=<file>: every patched M2 and WMO variant's slots before the facing and with it.
        const char* slotsFile = std::getenv("WET_SLOTS");
        std::ofstream slotsOut;
        if (slotsFile) slotsOut.open(slotsFile, std::ios::app);
        auto note = [&](const char* kind, const std::string& name, const std::string& src, int after)
        {
            const int before = SlotsBeforeFacing(src, name.c_str());
            if (before > 0) { worstDelta = std::max(worstDelta, after - before); ++deltas; }
            if (slotsOut) slotsOut << kind << "\t" << name << "\t" << before << "\t" << after << "\n";
        };
        static const std::regex position(R"(dp3 r\d+\.\w, (v\d+), c3\b)"), normal(R"(dp3 r\d+\.\w, c4, (v\d+))");
        for (const Blob& b : LoadBlobs(m2))
        {
            std::string src, why;
            if (!la::shaderpatch::Disassemble(b.code.data(), b.code.size(), src, why)) continue;
            const std::string stock = src;
            if (!la::modelice::EditPixelForTests(src, why)) continue;
            std::vector<uint8_t> code;
            if (!la::shaderpatch::Assemble(src, b.name.c_str(), code, why)) continue;
            ++m2Patched;
            m2Facing += src.find("mad_sat r30.w, -r15.z, c77.y, c77.x") != std::string::npos ? 1 : 0;
            const int m2Slots = Slots(code);
            m2Max = std::max(m2Max, m2Slots);
            note("M2", b.name, src, m2Slots);
            std::smatch mp, mn;
            if (pick.name.empty() && b.name.rfind("ps_Combiners_Opaque_", 0) == 0 && std::regex_search(stock, mp, position) && std::regex_search(stock, mn, normal))
                pick = ModelPick{ b.name, src, mp[1].str(), mn[1].str(), code };
        }
        for (const Blob& b : LoadBlobs(wmo))
        {
            std::string src, why;
            if (!la::shaderpatch::Disassemble(b.code.data(), b.code.size(), src, why)) continue;
            if (!la::modelice::EditPixelWmoGlassForTests(src, why)) continue;
            std::vector<uint8_t> code;
            if (!la::shaderpatch::Assemble(src, b.name.c_str(), code, why)) continue;
            ++wmoPatched;
            wmoFacing += src.find("mad_sat r30.w, -r15.z, c77.y, c77.x") != std::string::npos ? 1 : 0;
            const int s = Slots(code);
            if (s > wmoMax) { wmoMax = s; wmoMaxName = b.name; }
            note("WMO", b.name, src, s);
        }
        Check(deltas == m2Patched + wmoPatched && worstDelta == 2,
              F("every model variant's slots with the facing against the block without it (%d variants): at most %d more", deltas, worstDelta));
        Check(m2Patched > 200 && m2Facing == m2Patched && m2Max <= 512, F("every M2 shadow variant with the block carries the facing (%d of %d), largest %d of 512 slots",
                                                                         m2Facing, m2Patched, m2Max));
        Check(wmoPatched > 50 && wmoFacing == wmoPatched && wmoMax <= static_cast<int>(la::modelice::kGlassMinSlots),
              F("every WMO shadow variant with the rims and the glass blocks carries the facing (%d of %d), largest %d of %u slots (%s; 2027 before: the facing is 2)",
                wmoFacing, wmoPatched, wmoMax, la::modelice::kGlassMinSlots, wmoMaxName.c_str()));
        return pick;
    }

    // --- 4./5. on WARP -----------------------------------------------------------------------------------------------------
    struct Warp
    {
        ID3D12Device* d12 = nullptr;
        IDirect3D9* d3d = nullptr;
        IDirect3DDevice9* dev = nullptr;
        ~Warp() { Release(dev); Release(d3d); Release(d12); }
    };

    bool InitWarp(Warp& w, std::string& why)
    {
        IDXGIFactory4* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory)))) { why = "no DXGI factory"; return false; }
        IDXGIAdapter* adapter = nullptr;
        const HRESULT ha = factory->EnumWarpAdapter(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&adapter));
        factory->Release();
        if (FAILED(ha) || !adapter) { why = "no WARP adapter"; return false; }
        const HRESULT hd = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&w.d12));
        adapter->Release();
        if (FAILED(hd) || !w.d12) { why = "no D3D12 device on WARP"; return false; }
        using Create9On12 = IDirect3D9*(WINAPI*)(UINT, D3D9ON12_ARGS*, UINT);
        HMODULE d9 = LoadLibraryW(L"d3d9.dll");
        auto create = d9 ? reinterpret_cast<Create9On12>(GetProcAddress(d9, "Direct3DCreate9On12")) : nullptr;
        if (!create) { why = "no Direct3DCreate9On12"; return false; }
        D3D9ON12_ARGS a{};
        a.Enable9On12 = TRUE;
        a.pD3D12Device = w.d12;
        w.d3d = create(D3D_SDK_VERSION, &a, 1);
        if (!w.d3d) { why = "D3D9On12 failed"; return false; }
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"WetTests";
        RegisterClassW(&wc);
        HWND window = CreateWindowW(wc.lpszClassName, L"wet", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window;
        pp.BackBufferWidth = 64; pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        if (FAILED(w.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &w.dev))) { why = "CreateDevice failed"; return false; }
        return true;
    }

    template <class Fn> IDirect3DTexture9* MakeTexture(IDirect3DDevice9* dev, int size, Fn texel)
    {
        IDirect3DTexture9* t = nullptr;
        if (FAILED(dev->CreateTexture(size, size, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr))) return nullptr;
        D3DLOCKED_RECT lr{};
        t->LockRect(0, &lr, nullptr, 0);
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch)[x] = texel(x, y);
        t->UnlockRect(0);
        return t;
    }
    uint32_t Argb(float r, float g, float b, float a = 1.0f)
    {
        auto c = [](float f) { return static_cast<uint32_t>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
        return (c(a) << 24) | (c(r) << 16) | (c(g) << 8) | c(b);
    }
    float Hash01(int x, int y, int seed)
    {
        uint32_t h = static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u ^ static_cast<uint32_t>(seed) * 83492791u;
        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        return static_cast<float>(h & 0xFFFF) / 65535.0f;
    }

    // The camera: at C, looking along +x and pitched down; LH, row vectors for dp4 (world (x, y, z, 1) . row k = clip k).
    struct Camera
    {
        float pos[3] = { -2.0f, 0.0f, 3.0f };
        float rows[16] = {};
        int   width = 480, height = 300;
        void Build(float pitchDeg, float fovYDeg)
        {
            const float p = pitchDeg * 3.14159265f / 180.0f;
            const float f[3] = { std::cos(p), 0.0f, -std::sin(p) }, r[3] = { 0.0f, -1.0f, 0.0f }, u[3] = { std::sin(p), 0.0f, std::cos(p) };
            const float sy = 1.0f / std::tan(fovYDeg * 3.14159265f / 360.0f), sx = sy * height / width;
            const float zn = 0.5f, zf = 200.0f, A = zf / (zf - zn), B = -zn * A;
            auto dot = [&](const float* a) { return a[0] * pos[0] + a[1] * pos[1] + a[2] * pos[2]; };
            for (int k = 0; k < 3; ++k) { rows[k] = sx * r[k]; rows[4 + k] = sy * u[k]; rows[8 + k] = A * f[k]; rows[12 + k] = f[k]; }
            rows[3] = -sx * dot(r); rows[7] = -sy * dot(u); rows[11] = -A * dot(f) + B; rows[15] = -dot(f);
        }
        bool Project(const float w[3], float& px, float& py) const
        {
            float c[4];
            for (int k = 0; k < 4; ++k) c[k] = rows[k * 4] * w[0] + rows[k * 4 + 1] * w[1] + rows[k * 4 + 2] * w[2] + rows[k * 4 + 3];
            if (c[3] <= 0.0f) return false;
            px = (c[0] / c[3] + 1.0f) * 0.5f * width;
            py = (1.0f - c[1] / c[3]) * 0.5f * height;
            return true;
        }
    };

    // A vs_3_0 for the terrain variant: clip position from c0..c3, the vertex light c5, the specular colour c6, the fog
    // factor c7, texcoord7 the world position, every other texcoord (u, v, v, u) with u, v = the patch's 0..1 (c8).
    bool TerrainVertexShader(IDirect3DDevice9* dev, const std::string& ps, IDirect3DVertexShader9** out)
    {
        static const std::regex input(R"(dcl_(color|texcoord|fog)(\d*) v(\d+))");
        std::string decls = "vs_3_0\ndcl_position v0\ndcl_position o0\n", body =
            "mov r0.xyz, v0\nmov r0.w, c4.w\ndp4 o0.x, r0, c0\ndp4 o0.y, r0, c1\ndp4 o0.z, r0, c2\ndp4 o0.w, r0, c3\n"
            "mad r1.xy, v0.xy, c8.xy, c8.zw\n";
        int o = 1;
        for (auto it = std::sregex_iterator(ps.begin(), ps.end(), input); it != std::sregex_iterator(); ++it, ++o)
        {
            const std::string usage = (*it)[1].str(), index = (*it)[2].str();
            const std::string reg = "o" + std::to_string(o);
            decls += "dcl_" + usage + index + " " + reg + "\n";
            if (usage == "color") body += "mov " + reg + (index == "1" ? ", c6\n" : ", c5\n");
            else if (usage == "fog") body += "mov " + reg + ", c7\n";
            else if (index == "7") body += "mov " + reg + ", r0\n";
            else body += "mov " + reg + ", r1.xyyx\n";
        }
        std::vector<uint8_t> code;
        std::string why;
        if (!la::shaderpatch::Assemble(decls + body, "wet test vs", code, why)) { std::printf("vs: %s\n", why.c_str()); return false; }
        return SUCCEEDED(dev->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()), out));
    }

    struct Scene
    {
        bool  night = false;
        bool  stageOn = true;    // c160 from the layers (else zeros: the stage off)
        float film = 0.0f;       // the zone's (the surface texture's red x kSurfaceShare, and the far film)
        float pool = 0.0f;
        bool  puddle = false;    // a hollow on the stone at kPuddle (the potential 1 at its middle, 0 from 3.5 yd)
        bool  torch = false;     // a torch whose mirror image the camera sees at kPuddle
    };
    constexpr float kPuddle[3] = { 14.0f, -6.0f, 0.0f };
    constexpr float kTorch[3] = { 22.0f, -9.0f, 1.5f };   // the camera's ray to kPuddle, mirrored, half way on

    struct Image { int w = 0, h = 0; std::vector<float> rgb; const float* At(int x, int y) const { return &rgb[(static_cast<size_t>(y) * w + x) * 3]; } };
    float Luma(const float* c) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }
    float Diff3(const float* a, const float* b) { return std::max({ std::fabs(a[0] - b[0]), std::fabs(a[1] - b[1]), std::fabs(a[2] - b[2]) }); }

    struct TerrainRig
    {
        Warp* w = nullptr;
        Camera cam;
        IDirect3DPixelShader9* ps = nullptr;
        IDirect3DVertexShader9* vs = nullptr;
        IDirect3DVertexDeclaration9* decl = nullptr;
        IDirect3DSurface9* rt = nullptr;
        IDirect3DSurface9* sys = nullptr;
        IDirect3DTexture9 *stone = nullptr, *grass = nullptr, *alpha = nullptr, *moisture = nullptr, *zero = nullptr;
        ~TerrainRig() { Release(ps); Release(vs); Release(decl); Release(rt); Release(sys); Release(stone); Release(grass); Release(alpha); Release(moisture); Release(zero); }

        bool Init(Warp& warp, const TerrainPick& p, std::string& why)
        {
            w = &warp;
            IDirect3DDevice9* d = w->dev;
            cam.Build(13.0f, 60.0f);
            if (FAILED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(p.code.data()), &ps))) { why = "CreatePixelShader"; return false; }
            if (!TerrainVertexShader(d, p.text, &vs)) { why = "the vertex shader"; return false; }
            const D3DVERTEXELEMENT9 el[] = { { 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 }, D3DDECL_END() };
            if (FAILED(d->CreateVertexDeclaration(el, &decl))) { why = "declaration"; return false; }
            if (FAILED(d->CreateRenderTarget(cam.width, cam.height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, nullptr)) ||
                FAILED(d->CreateOffscreenPlainSurface(cam.width, cam.height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr)))
            { why = "targets"; return false; }
            // Cobbles (cells with darker joints) and grass (green noise), 256 texels over the 40-yard patch.
            stone = MakeTexture(d, 256, [](int x, int y)
            {
                const int cx = x / 12, cy = y / 12;
                const float fx = (x % 12) / 12.0f, fy = (y % 12) / 12.0f;
                const bool joint = fx < 0.12f || fy < 0.12f;
                const float tone = 0.48f + 0.12f * Hash01(cx, cy, 1) + 0.04f * Hash01(x, y, 2);
                return joint ? Argb(0.24f, 0.22f, 0.2f, 0.0f) : Argb(tone, tone * 0.97f, tone * 0.92f, 0.0f);
            });
            grass = MakeTexture(d, 256, [](int x, int y)
            {
                const float n = 0.75f + 0.5f * Hash01(x, y, 3);
                return Argb(0.16f * n, 0.34f * n, 0.11f * n, 0.0f);
            });
            // The layers' blend: layer 1 (grass) where v > 0.5 (world y > 0); the baked shadow (.w) none.
            alpha = MakeTexture(d, 256, [](int, int y) { return y >= 128 ? 0xFFFF0000u : 0xFF000000u; });
            moisture = MakeTexture(d, 1, [](int, int) { return 0x00808000u; });   // excess 0, the normal up
            zero = MakeTexture(d, 1, [](int, int) { return 0u; });
            return stone && grass && alpha && moisture && zero;
        }

        // The surface texture in the moisture grid's layout (64 cells of 1 yd, toroidal: world cell i at column mod(i, 64)).
        IDirect3DTexture9* Surface(const Scene& s)
        {
            return MakeTexture(w->dev, 64, [&](int col, int row)
            {
                const float x = (col < 48 ? col : col - 64) + 0.5f, y = (row < 32 ? row : row - 64) + 0.5f;
                float potential = 0.0f;
                if (s.puddle)
                {
                    const float d = std::hypot(x - kPuddle[0], y - kPuddle[1]);
                    potential = 1.0f - ws::SmoothStep(1.5f, 3.5f, d);
                }
                const uint32_t film = static_cast<uint32_t>(std::lround(std::clamp(s.film, 0.0f, 1.0f) * ws::kSurfaceShare * 255.0f));
                const uint32_t pot = static_cast<uint32_t>(std::lround(potential * 255.0f));
                return (film << 16) | (pot << 8);
            });
        }

        bool Draw(const Scene& s, Image& out)
        {
            IDirect3DDevice9* d = w->dev;
            // Our lights through the real pipeline: the torch as one of the scene's lights, the grid, the texture.
            std::vector<la::lights::FrameLight> scene;
            if (s.torch)
            {
                la::lights::FrameLight t{};
                for (int k = 0; k < 3; ++k) t.pos[k] = kTorch[k];
                t.color[0] = 1.1f; t.color[1] = 0.68f; t.color[2] = 0.33f;
                t.radius = 16.0f;
                t.model = reinterpret_cast<const void*>(static_cast<uintptr_t>(0x7000));
                t.indoor = 0;
                scene.push_back(t);
            }
            la::lights::UpdateForTests(cam.pos, 1.0f / 60.0f, {}, scene);
            float view[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
            la::modellights::PrepareForTests(d, view, cam.pos);
            float c206[4] = {}, c212[4] = {};
            la::modellights::GridConstants(c206, c212);

            IDirect3DTexture9* surface = Surface(s);
            d->SetRenderTarget(0, rt);
            d->SetDepthStencilSurface(nullptr);
            D3DVIEWPORT9 vp{ 0, 0, static_cast<DWORD>(cam.width), static_cast<DWORD>(cam.height), 0.0f, 1.0f };
            d->SetViewport(&vp);
            d->SetRenderState(D3DRS_ZENABLE, FALSE);
            d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            const float sky[4] = { s.night ? 0.03f : 0.62f, s.night ? 0.04f : 0.68f, s.night ? 0.075f : 0.78f, 1.0f };
            d->Clear(0, nullptr, D3DCLEAR_TARGET, Argb(sky[0], sky[1], sky[2]), 1.0f, 0);
            IDirect3DTexture9* layers[5] = { stone, grass, zero, zero, alpha };
            for (DWORD k = 0; k < 5; ++k) d->SetTexture(k, layers[k]);
            d->SetTexture(11, moisture);
            d->SetTexture(12, zero);
            d->SetTexture(13, la::modellights::Texture());
            d->SetTexture(14, zero);
            d->SetTexture(15, surface);
            for (DWORD k = 0; k < 16; ++k)
            {
                const bool point = k == 13 || k == 4;
                d->SetSamplerState(k, D3DSAMP_MINFILTER, point ? D3DTEXF_POINT : D3DTEXF_LINEAR);
                d->SetSamplerState(k, D3DSAMP_MAGFILTER, point ? D3DTEXF_POINT : D3DTEXF_LINEAR);
                d->SetSamplerState(k, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
                d->SetSamplerState(k, D3DSAMP_ADDRESSU, k == 13 || k == 4 ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP);
                d->SetSamplerState(k, D3DSAMP_ADDRESSV, k == 13 || k == 4 ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP);
                d->SetSamplerState(k, D3DSAMP_SRGBTEXTURE, FALSE);
            }
            // The vertex shader's constants.
            float vsc[9 * 4] = {};
            std::memcpy(vsc, cam.rows, sizeof(cam.rows));
            vsc[19] = 1.0f;                                                       // c4.w
            const float light = s.night ? 0.07f : 0.5f;                           // the stock lighting is 2 x this
            vsc[20] = vsc[21] = vsc[22] = light;
            vsc[28] = vsc[29] = vsc[30] = vsc[31] = 1.0f;                        // c7: no fog
            vsc[32] = 1.0f / 40.0f; vsc[33] = 1.0f / 40.0f; vsc[34] = 0.0f; vsc[35] = 0.5f; // c8: u = x / 40, v = y / 40 + 0.5
            d->SetVertexShaderConstantF(0, vsc, 9);
            // The pixel shader's: the stock fog colour; the moisture block inside its grid (c200 1 / 64 yd, no excess);
            // the lights' grid; no ice, no scorch; wet surfaces' as the game sets them.
            float zeros[4 * 24] = {};
            d->SetPixelShaderConstantF(200, zeros, 24);
            d->SetPixelShaderConstantF(2, sky, 1);
            const float c200[4] = { 1.0f / 64.0f, 0.0f, 0.0f, 0.0f }, c201[4] = { -1.0e4f, -1.0e4f, 1.0e4f, 1.0e4f }, c203[4] = { 0, 0, 0, 1.0f / 64.0f };
            d->SetPixelShaderConstantF(200, c200, 1);
            d->SetPixelShaderConstantF(201, c201, 1);
            d->SetPixelShaderConstantF(203, c203, 1);
            d->SetPixelShaderConstantF(206, c206, 1);
            d->SetPixelShaderConstantF(212, c212, 1);
            la::terrain::Surface stoneLayer, grassLayer;
            stoneLayer.texture = "test stone"; stoneLayer.terrainType = 2;
            grassLayer.texture = "test grass"; grassLayer.terrainType = 5;
            const float c160[4] = { s.stageOn ? la::wetsurfaces::LayerShine(0, stoneLayer, 0) : 0.0f, s.stageOn ? la::wetsurfaces::LayerShine(0, grassLayer, 0) : 0.0f, 0.0f, 0.0f };
            d->SetPixelShaderConstantF(ws::kMaskConstant, c160, 1);
            ws::Settings settings;
            ws::Frame f;
            f.film = s.film; f.pool = s.pool;
            for (int k = 0; k < 3; ++k) f.camera[k] = cam.pos[k];
            const float sun[3] = { 0.86f, 0.12f, 0.5f };   // low, ahead: its mirror on the wet stone
            for (int k = 0; k < 3; ++k) { f.sun[k] = sun[k]; f.sunColour[k] = 1.0f; }
            f.daylight = s.night ? 0.0f : 1.0f;
            f.overcast = 0.3f;
            if (!s.stageOn) settings.enabled = 0;
            float c[4 * ws::kTerrainRegisters];
            ws::TerrainConstants(settings, f, c);
            d->SetPixelShaderConstantF(ws::kTerrainConstant, c, ws::kTerrainRegisters);

            d->SetVertexDeclaration(decl);
            d->SetVertexShader(vs);
            d->SetPixelShader(ps);
            const float quad[4][3] = { { 0.0f, 20.0f, 0.0f }, { 40.0f, 20.0f, 0.0f }, { 0.0f, -20.0f, 0.0f }, { 40.0f, -20.0f, 0.0f } };
            bool ok = SUCCEEDED(d->BeginScene()) && SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])));
            d->EndScene();
            ok = ok && SUCCEEDED(d->GetRenderTargetData(rt, sys));
            D3DLOCKED_RECT lr{};
            if (ok && SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY)))
            {
                out.w = cam.width; out.h = cam.height;
                out.rgb.resize(static_cast<size_t>(out.w) * out.h * 3);
                for (int y = 0; y < out.h; ++y)
                    for (int x = 0; x < out.w; ++x)
                    {
                        const uint32_t p = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(lr.pBits) + y * lr.Pitch)[x];
                        float* o = &out.rgb[(static_cast<size_t>(y) * out.w + x) * 3];
                        o[0] = ((p >> 16) & 255) / 255.0f; o[1] = ((p >> 8) & 255) / 255.0f; o[2] = (p & 255) / 255.0f;
                    }
                sys->UnlockRect();
            }
            else ok = false;
            d->SetPixelShader(nullptr);
            d->SetVertexShader(nullptr);
            d->SetTexture(15, nullptr);
            Release(surface);
            return ok;
        }
    };

    // A world point's pixel, and the mean colour of the 3 x 3 pixels round it.
    bool Sample(const Camera& cam, const Image& im, const float wpos[3], float out[3], int* px = nullptr, int* py = nullptr)
    {
        float fx, fy;
        if (!cam.Project(wpos, fx, fy)) return false;
        const int x = static_cast<int>(fx), y = static_cast<int>(fy);
        if (x < 2 || y < 2 || x >= im.w - 2 || y >= im.h - 2) return false;
        out[0] = out[1] = out[2] = 0.0f;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                for (int k = 0; k < 3; ++k) out[k] += im.At(x + dx, y + dy)[k] / 25.0f;
        if (px) *px = x;
        if (py) *py = y;
        return true;
    }

    void WriteSheet(const std::string& path, const std::vector<const Image*>& tiles, int columns)
    {
        if (tiles.empty() || path.empty()) return;
        const int tw = tiles[0]->w, th = tiles[0]->h, rows = static_cast<int>((tiles.size() + columns - 1) / columns);
        const int width = tw * columns + 4 * (columns - 1), height = th * rows + 4 * (rows - 1);
        const int stride = (width * 3 + 3) & ~3;
        std::vector<uint8_t> bmp(54 + static_cast<size_t>(stride) * height, 255);
        auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&bmp[at], &v, 4); };
        bmp[0] = 'B'; bmp[1] = 'M'; put32(2, static_cast<uint32_t>(bmp.size())); put32(6, 0); put32(10, 54); put32(14, 40);
        put32(18, static_cast<uint32_t>(width)); put32(22, static_cast<uint32_t>(height));
        bmp[26] = 1; bmp[27] = 0; bmp[28] = 24; bmp[29] = 0; put32(30, 0); put32(34, 0); put32(38, 2835); put32(42, 2835); put32(46, 0); put32(50, 0);
        for (size_t t = 0; t < tiles.size(); ++t)
        {
            const int ox = static_cast<int>(t % columns) * (tw + 4), oy = static_cast<int>(t / columns) * (th + 4);
            for (int y = 0; y < th; ++y)
                for (int x = 0; x < tw; ++x)
                {
                    const float* c = tiles[t]->At(x, y);
                    const int Y = height - 1 - (oy + y);
                    const size_t at = 54 + static_cast<size_t>(Y) * stride + (ox + x) * 3;
                    bmp[at] = static_cast<uint8_t>(std::lround(std::clamp(c[2], 0.0f, 1.0f) * 255.0f));
                    bmp[at + 1] = static_cast<uint8_t>(std::lround(std::clamp(c[1], 0.0f, 1.0f) * 255.0f));
                    bmp[at + 2] = static_cast<uint8_t>(std::lround(std::clamp(c[0], 0.0f, 1.0f) * 255.0f));
                }
        }
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bmp.data()), static_cast<std::streamsize>(bmp.size()));
        std::printf("      contact sheet: %s (rows: by day after rain, at night with a torch; columns: dry, wet, wet with the puddle full)\n", path.c_str());
    }

    void TerrainOnWarp(Warp& w, const TerrainPick& pick, const std::string& sheet)
    {
        std::printf("-- the patched terrain on WARP (%s)\n", pick.name.c_str());
        TerrainRig rig;
        std::string why;
        if (!rig.Init(w, pick, why)) { Check(false, "the terrain rig: " + why); return; }
        Image dayOff, dayDry, dayWet, dayPuddle, nightDry, nightSheen, nightWet;
        Scene s;
        s.stageOn = false;
        bool ok = rig.Draw(s, dayOff);
        s.stageOn = true;
        s.puddle = true;                    // the hollow is there, but no water in it yet (pool 0)
        ok = ok && rig.Draw(s, dayDry);
        s.film = 1.0f; s.pool = 0.0f;
        ok = ok && rig.Draw(s, dayWet);
        s.pool = 1.0f;
        ok = ok && rig.Draw(s, dayPuddle);
        Scene n;
        n.night = true; n.torch = true; n.puddle = true;
        ok = ok && rig.Draw(n, nightDry);
        n.film = 1.0f;
        ok = ok && rig.Draw(n, nightSheen);
        n.pool = 1.0f;
        ok = ok && rig.Draw(n, nightWet);
        Check(ok, "drawn: by day off, dry, wet and with the puddle full; at night with a torch dry, wet and with the puddle full");
        if (!ok) return;

        // Dry: exactly the stage off (the pieces add nothing while the surface holds no water).
        float worst = 0.0f;
        for (size_t k = 0; k < dayOff.rgb.size(); ++k) worst = std::max(worst, std::fabs(dayOff.rgb[k] - dayDry.rgb[k]));
        Check(worst == 0.0f, F("dry ground with the pieces on draws exactly as with the stage off (worst channel off by %.4f)", worst));
        // Wet stone near: darker; far, at a slant: toward the sky; the grass as it was.
        const float nearStone[3] = { 7.0f, -4.0f, 0.0f }, farStone[3] = { 32.0f, -9.0f, 0.0f }, nearGrass[3] = { 7.0f, 5.0f, 0.0f }, farGrass[3] = { 30.0f, 10.0f, 0.0f };
        float a[3], b[3], g0[3], g1[3], h0[3], h1[3], fa[3], fb[3];
        const bool got = Sample(rig.cam, dayDry, nearStone, a) && Sample(rig.cam, dayWet, nearStone, b) && Sample(rig.cam, dayDry, nearGrass, g0) &&
                         Sample(rig.cam, dayWet, nearGrass, g1) && Sample(rig.cam, dayDry, farGrass, h0) && Sample(rig.cam, dayWet, farGrass, h1) &&
                         Sample(rig.cam, dayDry, farStone, fa) && Sample(rig.cam, dayWet, farStone, fb);
        const float sky[3] = { 0.62f, 0.68f, 0.78f };
        // The sky (0.62, 0.68, 0.78) is bluer than the stone: the blue share of a colour rises with the sky in it (the
        // darkening leaves the hue).
        auto blue = [](const float* c) { return c[2] / std::max(1e-4f, c[0] + c[1] + c[2]); };
        Check(got && Luma(b) < 0.95f * Luma(a), F("by day: wet stone near the camera darker, %.3f -> %.3f (x%.2f)", Luma(a), Luma(b), Luma(b) / std::max(1e-4f, Luma(a))));
        Check(got && blue(fb) > blue(fa) + 0.01f, F("by day: wet stone far off, at a slant, takes the sky (%.3f %.3f %.3f -> %.3f %.3f %.3f; blue share %.3f -> %.3f)",
                                                    fa[0], fa[1], fa[2], fb[0], fb[1], fb[2], blue(fa), blue(fb)));
        float grassWorst = 0.0f;
        for (int y = 0; y < dayDry.h; ++y)
            for (int x = 0; x < dayDry.w; ++x)
            {
                // Pixels on the grass side (left of the image: world y > 0) away from the seam.
                if (x > dayDry.w * 0.4f) continue;
                for (int k = 0; k < 3; ++k) grassWorst = std::max(grassWorst, std::fabs(dayDry.At(x, y)[k] - dayWet.At(x, y)[k]));
            }
        Check(got && grassWorst <= 1.5f / 255.0f && Diff3(g0, g1) == 0.0f && Diff3(h0, h1) == 0.0f,
              F("by day: the grass doesn't change when wet (the image's grass side off by at most %.4f)", grassWorst));
        // At night: the torch's image in the puddle, where the camera's ray mirrors to it.
        float pd[3], pw[3];
        int px = 0, py = 0;
        const bool atP = Sample(rig.cam, nightDry, kPuddle, pd, &px, &py) && Sample(rig.cam, nightWet, kPuddle, pw);
        int bx = 0, by = 0;
        float best = -1.0f;
        for (int y = std::max(0, py - 38); y < std::min(nightWet.h, py + 38); ++y)
            for (int x = std::max(0, px - 60); x < std::min(nightWet.w, px + 60); ++x)
                if (Luma(nightWet.At(x, y)) > best) { best = Luma(nightWet.At(x, y)); bx = x; by = y; }
        Check(atP && Luma(pw) > 2.0f * Luma(pd) + 0.1f, F("at night: the torch reflected in the puddle, %.3f -> %.3f at its mirror point", Luma(pd), Luma(pw)));
        Check(atP && std::abs(bx - px) <= 12 && std::abs(by - py) <= 12, F("at night: the brightest pixel round the puddle (%.3f at %d, %d) is at the torch's mirror point (%d, %d)",
                                                                       best, bx, by, px, py));
        // Off the puddle the wet stone has a broad sheen of the torch, much weaker than its image in the puddle; at the
        // mirror point without the water, a sheen too, far from the mirror's image.
        const float beside[3] = { kPuddle[0] - 5.0f, kPuddle[1] + 1.0f, 0.0f };
        float sd[3], sw[3], ps[3];
        const bool atB = Sample(rig.cam, nightDry, beside, sd) && Sample(rig.cam, nightWet, beside, sw) && Sample(rig.cam, nightSheen, kPuddle, ps);
        Check(atB && Luma(sw) - Luma(sd) < 0.5f * (Luma(pw) - Luma(pd)), F("at night: 5 yd nearer, on wet stone without a puddle, %.3f -> %.3f (a sheen, not a mirror)", Luma(sd), Luma(sw)));
        Check(atB && Luma(ps) > Luma(pd) && Luma(pw) - Luma(pd) > 4.0f * (Luma(ps) - Luma(pd)),
              F("at night: the mirror point on wet stone with the puddle still empty %.3f (dry %.3f, the full puddle %.3f): a sheen of the torch", Luma(ps), Luma(pd), Luma(pw)));
        // By day the full puddle mirrors the sky: nearer the sky's colour than the wet stone it lies on.
        float dw[3], dp[3];
        const bool atD = Sample(rig.cam, dayWet, kPuddle, dw) && Sample(rig.cam, dayPuddle, kPuddle, dp);
        Check(atD && blue(dp) > blue(dw) + 0.01f && Luma(dp) < Luma(dw) + 0.1f,
              F("by day: the full puddle takes more of the sky than the wet stone it lies on (%.3f %.3f %.3f, blue share %.3f; the wet stone %.3f %.3f %.3f, %.3f)",
                dp[0], dp[1], dp[2], blue(dp), dw[0], dw[1], dw[2], blue(dw)));
        WriteSheet(sheet, { &dayDry, &dayWet, &dayPuddle, &nightDry, &nightSheen, &nightWet }, 3);
    }

    // The model block on a quad: its view position c3 and normal c4 (as the variant reads them); colour out.
    bool DrawBlock(Warp& w, const ModelPick& p, const float vP[3], const float vN[3], bool on, const float c179[4], const float* look, const float c190[4],
                   const float c77[4], float out[3])
    {
        IDirect3DDevice9* d = w.dev;
        static const std::regex input(R"(dcl_(color|texcoord|fog)(\d*) (v\d+)(\.\w+)?)");
        std::string decls = "vs_3_0\ndcl_position v0\ndcl_texcoord v1\ndcl_position o0\n", body = "mov o0, v0\n";
        int o = 1;
        for (auto it = std::sregex_iterator(p.text.begin(), p.text.end(), input); it != std::sregex_iterator(); ++it, ++o)
        {
            const std::string usage = (*it)[1].str(), v = (*it)[3].str(), mask = (*it)[4].str();
            const std::string reg = "o" + std::to_string(o);
            decls += "dcl_" + usage + (*it)[2].str() + " " + reg + "\n";
            if (usage == "color") body += "mov " + reg + ", c0\n";
            else if (usage == "fog") body += "mov " + reg + ", c2\n";
            else if (v == p.vP) body += "mov " + reg + ", c3\n";
            else if (v == p.vN) body += "mov " + reg + ", c4\n";
            else if (mask == ".xy") body += "mov " + reg + ", v1\n";
            else body += "mov " + reg + ", c5\n";
        }
        std::vector<uint8_t> code;
        std::string why;
        IDirect3DVertexShader9* vs = nullptr;
        IDirect3DPixelShader9* ps = nullptr;
        IDirect3DVertexDeclaration9* decl = nullptr;
        IDirect3DSurface9 *rt = nullptr, *sys = nullptr;
        IDirect3DTexture9* base = MakeTexture(d, 1, [](int, int) { return Argb(0.55f, 0.5f, 0.45f); });
        IDirect3DTexture9* white = MakeTexture(d, 1, [](int, int) { return 0xFFFFFFFFu; });
        const D3DVERTEXELEMENT9 el[] = { { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
                                         { 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 }, D3DDECL_END() };
        bool ok = la::shaderpatch::Assemble(decls + body, "wet block vs", code, why) && SUCCEEDED(d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()), &vs)) &&
                  SUCCEEDED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(p.code.data()), &ps)) && SUCCEEDED(d->CreateVertexDeclaration(el, &decl)) &&
                  SUCCEEDED(d->CreateRenderTarget(4, 4, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, nullptr)) &&
                  SUCCEEDED(d->CreateOffscreenPlainSurface(4, 4, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &sys, nullptr)) && base && white;
        if (ok)
        {
            d->SetRenderTarget(0, rt);
            D3DVIEWPORT9 vp{ 0, 0, 4, 4, 0.0f, 1.0f };
            d->SetViewport(&vp);
            d->SetRenderState(D3DRS_ZENABLE, FALSE);
            d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            d->SetTexture(0, base);
            for (DWORD k = 1; k < 8; ++k) d->SetTexture(k, white);
            d->SetTexture(14, la::wetsurfaces::BuildingDetail(d));
            const float vsc[24] = { 1, 1, 1, 1,  0, 0, 0, 0,  1, 1, 1, 1,  vP[0], vP[1], vP[2], 1,  vN[0], vN[1], vN[2], 0,  5, 5, 0.5f, 1 };
            d->SetVertexShaderConstantF(0, vsc, 6);
            float zeros[4 * 224] = {};
            d->SetPixelShaderConstantF(0, zeros, 224);
            const float fog[4] = { 0.5f, 0.6f, 0.7f, 1.0f };
            d->SetPixelShaderConstantF(2, fog, 1);
            d->SetPixelShaderConstantF(la::modelice::kWeightConstant, c179, 1);
            d->SetPixelShaderConstantF(la::modelice::kLookConstant, look, la::terrainice::kModelConstantCount);
            d->SetPixelShaderConstantF(la::modelice::kStyleConstant, c190, 1);
            d->SetPixelShaderConstantF(ws::kFacingConstant, c77, 1);
            const BOOL b = on ? TRUE : FALSE;
            d->SetPixelShaderConstantB(la::modelice::kSwitch, &b, 1);
            d->SetVertexDeclaration(decl);
            d->SetVertexShader(vs);
            d->SetPixelShader(ps);
            const float quad[4][6] = { { -1, 1, 0.5f, 1, 0, 0 }, { 1, 1, 0.5f, 1, 1, 0 }, { -1, -1, 0.5f, 1, 0, 1 }, { 1, -1, 0.5f, 1, 1, 1 } };
            ok = SUCCEEDED(d->BeginScene()) && SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])));
            d->EndScene();
            D3DLOCKED_RECT lr{};
            ok = ok && SUCCEEDED(d->GetRenderTargetData(rt, sys)) && SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY));
            if (ok)
            {
                const float* px = reinterpret_cast<const float*>(static_cast<const uint8_t*>(lr.pBits) + lr.Pitch) + 4;
                out[0] = px[0]; out[1] = px[1]; out[2] = px[2];
                sys->UnlockRect();
            }
            const BOOL offB = FALSE;
            d->SetPixelShaderConstantB(la::modelice::kSwitch, &offB, 1);
            d->SetPixelShader(nullptr);
            d->SetVertexShader(nullptr);
        }
        Release(vs); Release(ps); Release(decl); Release(rt); Release(sys); Release(base); Release(white);
        return ok;
    }

    void BlockOnWarp(Warp& w, const ModelPick& p)
    {
        std::printf("-- the model block's wet style on WARP (%s)\n", p.name.c_str());
        if (p.name.empty()) { Check(false, "an M2 variant with the block"); return; }
        // The player's zone soaked; at night (no sun glint), so only the facing differs between a floor, a wall and a ceiling
        // seen at the same angle (60 degrees off each one's normal).
        la::wetsurfaces::SetZoneForTests(0, 1, 1.0f, 0.0f);
        float look[4 * la::terrainice::kModelConstantCount], c179[4], c190[4], c77[4];
        la::terrainice::FillModelConstants(look);
        la::wetsurfaces::BuildingConstants(1.0f, look, c179, c190, c77);
        const float floorN[3] = { 0, 0, 1 }, wallN[3] = { 1, 0, 0 }, ceilN[3] = { 0, 0, -1 };
        const float floorP[3] = { 0, -5.196f, -3.0f }, wallP[3] = { -3.0f, -5.196f, 0 }, ceilP[3] = { 0, -5.196f, 3.0f };
        float stockF[3], stockW[3], stockC[3], wetF[3], wetW[3], wetC[3];
        const float zero4[4] = {};
        bool ok = DrawBlock(w, p, floorP, floorN, false, c179, look, c190, c77, stockF) && DrawBlock(w, p, wallP, wallN, false, c179, look, c190, c77, stockW) &&
                  DrawBlock(w, p, ceilP, ceilN, false, c179, look, c190, c77, stockC) && DrawBlock(w, p, floorP, floorN, true, c179, look, c190, c77, wetF) &&
                  DrawBlock(w, p, wallP, wallN, true, c179, look, c190, c77, wetW) && DrawBlock(w, p, ceilP, ceilN, true, c179, look, c190, c77, wetC);
        Check(ok, "drawn: a floor, a wall and a ceiling, stock and wet");
        if (!ok) return;
        const float dF = Diff3(wetF, stockF), dW = Diff3(wetW, stockW), dC = Diff3(wetC, stockC);
        Check(Luma(wetF) < Luma(stockF) && dF > 0.05f, F("a wet floor: %.3f -> %.3f (darker, the sky in it)", Luma(stockF), Luma(wetF)));
        Check(std::fabs(dW / dF - ws::kWallShare) < 0.05f && dC < 1e-4f,
              F("the facing: a wall changes %.3f of what the floor does (kWallShare %.2f), a ceiling %.5f", dW / dF, ws::kWallShare, dC));
        // The ice with the facing constant zero: its change in proportion to its weight, as before the facing (x 1).
        float iceLook[4 * la::terrainice::kModelConstantCount], i179[4], i190[4];
        la::terrainice::FillModelConstants(iceLook);
        la::modelice::StyleConstants(la::modelice::Style::Ice, 1.0f, iceLook, i179, i190);
        float full[3], half[3], base[3];
        float h179[4] = { 0.5f, i179[1], i179[2], i179[3] };
        ok = DrawBlock(w, p, wallP, wallN, false, i179, iceLook, i190, zero4, base) && DrawBlock(w, p, wallP, wallN, true, i179, iceLook, i190, zero4, full) &&
             DrawBlock(w, p, wallP, wallN, true, h179, iceLook, i190, zero4, half);
        float ratio = 0.0f;
        for (int k = 0; k < 3; ++k) ratio = std::max(ratio, std::fabs((half[k] - base[k]) - 0.5f * (full[k] - base[k])));
        Check(ok && Diff3(full, base) > 0.05f && ratio < 1e-4f, F("the ice with c77 zero: weight 0.5 changes exactly half of weight 1 (off by %.6f)", ratio));
        la::wetsurfaces::DryNow();
    }

    // --- 6. cost ----------------------------------------------------------------------------------------------------------
    void Cost()
    {
        std::printf("-- cost\n");
        using clock = std::chrono::steady_clock;
        // The per-cell work env/Fields adds to its moisture step (four a second) over the whole 128 x 128 grid, and the
        // lowness pass's slice a frame (256 cells x 16 neighbours).
        std::vector<float> z(128 * 128);
        for (int k = 0; k < 128 * 128; ++k) z[k] = 10.0f + std::sin(k * 0.01f);
        std::vector<uint8_t> bytes(128 * 128 * 2);
        volatile float sink = 0.0f;
        const auto t0 = clock::now();
        constexpr int kRounds = 50;
        for (int r = 0; r < kRounds; ++r)
            for (int k = 0; k < 128 * 128; ++k)
            {
                const float film = ws::SpotFilm(0.8f, 1.0f, 0.0f, 0.0f, false), pot = ws::SpotPotential(0.4f, 1.0f, 0.0f, 0.0f, false);
                bytes[k * 2] = static_cast<uint8_t>(film * 153.0f);
                bytes[k * 2 + 1] = static_cast<uint8_t>(pot * 255.0f);
            }
        const double stepMs = std::chrono::duration<double, std::milli>(clock::now() - t0).count() / kRounds;
        const auto t1 = clock::now();
        for (int r = 0; r < kRounds; ++r)
            for (int n = 0; n < 256; ++n)
            {
                const int k = (r * 256 + n) % (128 * 128), a = k % 128, b = k / 128;
                float sum = 0.0f;
                int count = 0;
                for (const auto& o : ws::kRing)
                {
                    const int na = a + o[0], nb = b + o[1];
                    if (na < 0 || nb < 0 || na >= 128 || nb >= 128) continue;
                    sum += z[nb * 128 + na];
                    ++count;
                }
                sink = sink + ws::PuddlePotential(count ? sum / count - z[k] : 0.0f, 0.02f, ws::CellNoise(a, b));
            }
        const double sliceMs = std::chrono::duration<double, std::milli>(clock::now() - t1).count() / kRounds;
        ws::ZoneWet zones[16];
        const auto t2 = clock::now();
        for (int r = 0; r < 10000; ++r) for (auto& zz : zones) ws::Step(zz, 0.016f, r % 2 == 0, 0.5f, 1.0f, 15.0f);
        const double zonesMs = std::chrono::duration<double, std::milli>(clock::now() - t2).count() / 10000.0;
        std::printf("TIMING the surface bytes over 128 x 128 cells: %.3f ms a moisture step (4 a second); the lowness slice: %.4f ms a frame; 16 zones' step %.5f ms a frame\n",
                    stepMs, sliceMs, zonesMs);
        Check(stepMs < 2.0 && sliceMs < 0.2 && zonesMs < 0.05, "the CPU's share stays small (this machine, at Idle priority, one core)");
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> args, lists;
    bool warp = true;
    std::string sheet;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--no-warp") == 0) warp = false;
        else if (std::strcmp(argv[i], "--sheet") == 0 && i + 1 < argc) sheet = argv[++i];
        else if (args.size() < 4) args.push_back(argv[i]);
        else lists.push_back(argv[i]);
    }
    if (args.size() < 4)
    {
        std::printf("usage: wet_tests <Terrain*.bls folder> <M2 ps_Combiners folder> <WMO ps_MapObj folder> <repo data folder> [<wmo texture .tsv>...] [--no-warp] [--sheet <bmp>]\n");
        return 2;
    }
    Rules();
    // The map scan's WMO textures by class (the classifier's spread over every building texture in use).
    for (const std::string& l : lists)
    {
        std::ifstream f(l);
        std::string line;
        std::map<float, unsigned> classes;
        std::map<std::string, bool> seen;
        std::getline(f, line);
        while (std::getline(f, line))
        {
            const size_t tab = line.rfind('\t');
            const std::string tex = tab == std::string::npos ? line : line.substr(tab + 1);
            if (seen.emplace(tex, true).second) classes[ws::WmoTextureShine(tex.c_str())]++;
        }
        std::string spread;
        for (const auto& c : classes) spread += F(" %.2f: %u;", c.first, c.second);
        std::printf("info  %s: %zu textures by their wet shine:%s\n", l.c_str(), seen.size(), spread.c_str());
    }
    Tables(args[3]);
    const TerrainPick terrain = TerrainShaders(args[0]);
    const ModelPick model = ModelShaders(args[1], args[2]);
    if (warp)
    {
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) std::printf("skip  WARP: %s\n", why.c_str());
        else
        {
            if (!terrain.name.empty()) TerrainOnWarp(w, terrain, sheet);
            else Check(false, "a 4-layer terrain variant without shadows to draw");
            BlockOnWarp(w, model);
        }
    }
    else std::printf("(--no-warp: the drawn checks left out)\n");
    Cost();
    std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
