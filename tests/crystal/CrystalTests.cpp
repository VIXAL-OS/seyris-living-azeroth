// Crystals (features/Crystal, the crystal part of features/ModelIce's block, data/CrystalMaterials.cdbc).
//  1. The shipped table (argv data folder): it reads back; every family has rows; keys are unique; a row's -1 takes its
//     family's values, its mask (-1000) none; no crystal texture is one of the ice's.
//  2. Which batches: a crystal texture drawn opaque or alpha-keyed takes the look (unlit kept in the class), drawn alpha,
//     additive or modulated it doesn't; any folder, case or extension; other textures don't.
//  3. With the map scan (argv survey folder: m2_batches.tsv, placements.tsv, wmo_materials.tsv): the classification counts
//     by family (models, batches and placements; WMO materials, WMOs and placements), the crystals already see-through or
//     glowing (every crystal texture batch of theirs alpha or additive), named models in and out, no ice model counted.
//  4. The shader edit: every stock M2 pixel shader's shadow variant (argv folder of ps_Combiners_*.bls) takes the block
//     with the crystal code and assembles within 512 slots, with the doodads' dissolve too; every WMO shadow variant
//     (argv folder of ps_MapObj*.bls) with the rims within modelice::kRingsMinSlots and with the glass blocks within
//     kGlassMinSlots. The largest of each reported.
//  5. On WARP (the CPU rasterizer behind D3D9On12; no GPU), a crystal drawn with the patched one-texture M2 variant: a
//     mask that keeps nothing gives exactly the stock pixels; no NaN or Inf (black, white and saturated texels, day and
//     night, head on to grazing, the largest constants the table allows); the glow brighter at night; the core deeper
//     with the deepening. A WMO variant the same for NaN. --no-warp skips it.
//  6. The facets follow the crystal's size: CellScale (1 for a shard, smooth and rising, at most kMaxCellScale; finite
//     for any input), the constants it makes (a size of 0 gives exactly the old ones; the facets, the layers' depths and
//     the fade distances grow together; CrystalCellScale multiplies them), PieceRadius on made-up meshes (a crystal split
//     at its hard edges is one piece; the median by vertex), and on the real files through the same readers the game
//     runs (argv survey folder's m2\ files, rebased as the client loads them; argv WMO groups folder: the Exodar's giant
//     crystals, Tempest Keep's wall, Dalaran's tiny ones, Diamond Mountain's). On WARP, no NaN or Inf at any size or
//     setting.
// CRYSTAL_TEXTURES=<folder of the crystals' BLPs (waterscan --extract-list names)> and CRYSTAL_SHEETS=<folder>: contact
// sheets on WARP, one panel per texture: columns by day stock, by day crystal, at night stock, at night crystal; rows a
// crystal seen head on and from above (its sides at a grazing angle). BMPs, one per texture. With CRYSTAL_SCALE_SHEETS=
// <folder>: a shard, a medium crystal (the Exodar's big M2 ones) and a giant one (the Exodar's WMO crystals), each seen
// whole and from close by: columns stock, the facets before (a hand-sized crystal's at any size), after (by its size).
// Run: crystal_tests <M2 shader folder> <WMO shader folder> <repo data folder> [<survey folder> [<WMO groups folder>]] [--no-warp]
#include "features/Crystal.hpp"
#include "features/DoodadFade.hpp"
#include "features/ModelIce.hpp"
#include "features/TerrainIce.hpp"
#include "features/WmoIce.hpp"
#include "render/BlpTexture.hpp"
#include "render/ShaderPatch.hpp"

#include <windows.h>
#include <d3d9.h>
#include <d3d9on12.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace cr = wxl_livingazeroth::crystal;
namespace mi = wxl_livingazeroth::modelice;
namespace fs = std::filesystem;

namespace
{
    int g_failures = 0;
    void Check(bool ok, const std::string& what)
    {
        std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++g_failures;
    }
    void Info(const std::string& what) { std::printf("info  %s\n", what.c_str()); }

    std::string Lower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        return s;
    }

    // --- 1. the table ---------------------------------------------------------------------------------------------
    bool ReadTable(const fs::path& file, std::vector<cr::Material>& out, std::string& why)
    {
        std::ifstream f(file, std::ios::binary);
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (d.size() < 20 || std::memcmp(d.data(), "WDBC", 4) != 0) { why = "not a WDBC file"; return false; }
        uint32_t count, fields, size, strings;
        std::memcpy(&count, &d[4], 4); std::memcpy(&fields, &d[8], 4); std::memcpy(&size, &d[12], 4); std::memcpy(&strings, &d[16], 4);
        if (fields != 22 || size != 88 || d.size() != 20 + static_cast<size_t>(count) * size + strings) { why = "unexpected layout"; return false; }
        const uint8_t* block = &d[20 + static_cast<size_t>(count) * size];
        auto u = [&](uint32_t row, int k) { uint32_t v; std::memcpy(&v, &d[20 + static_cast<size_t>(row) * size + 4 * k], 4); return v; };
        auto f32 = [&](uint32_t row, int k) { const uint32_t v = u(row, k); float x; std::memcpy(&x, &v, 4); return x; };
        auto str = [&](uint32_t row, int k) { const uint32_t at = u(row, k); return at < strings ? std::string(reinterpret_cast<const char*>(block + at)) : std::string(); };
        for (uint32_t r = 0; r < count; ++r)
        {
            cr::Material m;
            m.id = u(r, 0); m.texture = str(r, 1); m.family = u(r, 2); m.flags = u(r, 3); m.weight = f32(r, 4);
            for (int k = 0; k < 3; ++k) { m.tint[k] = f32(r, 5 + k); m.glow[k] = f32(r, 8 + k); }
            m.dayGlow = f32(r, 11); m.nightGlow = f32(r, 12); m.deepen = f32(r, 13); m.rim = f32(r, 14); m.facetYards = f32(r, 15);
            for (int k = 0; k < 4; ++k) m.mask[k] = f32(r, 16 + k);
            m.model = str(r, 21);
            out.push_back(m);
        }
        return true;
    }

    void TableChecks(const std::vector<cr::Material>& raw)
    {
        cr::SetMaterials(raw);
        Check(raw.size() >= 80 && cr::MaterialCount() == raw.size(), "CrystalMaterials.cdbc: " + std::to_string(raw.size()) + " rows, " +
              std::to_string(cr::MaterialCount()) + " crystal textures loaded");
        std::map<uint32_t, int> perFamily;
        std::set<std::string> keys;
        std::set<uint32_t> ids;
        bool unique = true, inherits = true, ranges = true, notIce = true;
        int masked = 0;
        for (const cr::Material& r : raw)
        {
            ++perFamily[r.family];
            unique = keys.insert(cr::Key(r.texture.c_str())).second && ids.insert(r.id).second && unique;
            const cr::Material m = cr::Resolve(r);
            const cr::FamilyLook& f = cr::FamilyOf(r.family);
            if (r.weight < -0.5f && m.weight != 1.0f) inherits = false;
            for (int k = 0; k < 3; ++k)
            {
                if (r.tint[k] < -0.5f && m.tint[k] != f.tint[k]) inherits = false;
                if (r.glow[k] < -0.5f && m.glow[k] != f.glow[k]) inherits = false;
            }
            if (r.deepen < -0.5f && m.deepen != f.deepen) inherits = false;
            if (r.mask[3] <= -1000.0f && (m.mask[0] != 0.0f || m.mask[1] != 0.0f || m.mask[2] != 0.0f || m.mask[3] != 1.0f)) inherits = false;
            masked += r.mask[3] > -1000.0f ? 1 : 0;
            ranges = ranges && m.weight > 0.0f && m.weight <= 1.0f && m.facetYards > 0.0f && m.deepen <= 1.0f && m.rim <= 1.0f &&
                     r.family >= 1 && r.family < cr::kFamilyCount;
            notIce = notIce && !wxl_livingazeroth::wmoice::IsIceTexture(r.texture.c_str()) && !mi::IsIceModel(r.texture.c_str());
        }
        std::string families;
        for (uint32_t k = 1; k < cr::kFamilyCount; ++k) families += std::string(k > 1 ? ", " : "") + cr::FamilyOf(k).name + " " + std::to_string(perFamily[k]);
        Check(perFamily.size() == cr::kFamilyCount - 1, "every family has rows: " + families);
        Check(unique, "the textures and the IDs are unique");
        Check(inherits, "a row's -1 takes its family's value, its mask at -1000 none (" + std::to_string(masked) + " rows masked)");
        Check(ranges, "every row's weight, facets, deepening, rim and family in range");
        Check(notIce, "no crystal texture is one of the ice's (the ice keeps the ice)");
    }

    // --- 2. which batches -----------------------------------------------------------------------------------------
    void ClassifyChecks()
    {
        const char* exodar = "World\\Expansion01\\Doodads\\TheExodar\\PassiveDoodads\\Crystals\\JG_Draenei_Crystal04_Edit2.BLP";
        const uint16_t opaque = cr::Classify(0, 0x1, exodar), keyed = cr::Classify(1, 0, exodar);
        const cr::Material* m = cr::MaterialAt(opaque & ~cr::kUnlitBit);
        Check(opaque && (opaque & cr::kUnlitBit) && m && m->family == cr::kDraenei && keyed && !(keyed & cr::kUnlitBit),
              "the Exodar's crystal drawn opaque (unlit) and alpha-keyed takes the look, draenei violet");
        bool none = true;
        for (uint32_t blend : { 2u, 3u, 4u, 5u, 6u }) none = none && cr::Classify(blend, 0, exodar) == 0;
        Check(none, "drawn alpha, additive or modulated it doesn't (its own shells and glows stay as they are)");
        Check(cr::Classify(0, 0, "dungeons/textures/draenei/jlo_draenei_crystal02") != 0 && cr::Classify(0, 0, "JLO_DRAENEI_CRYSTAL02.blp") != 0,
              "any folder, case or slashes, with or without .blp");
        const char* atlas = "World\\Expansion01\\Doodads\\Terokkar\\Crystal\\TerrokarFoliage01.blp";
        Check(cr::Classify(0, 1, atlas, "World\\Expansion01\\Doodads\\Terokkar\\Crystal\\TerokkarCrystal01") != 0 &&
              cr::Classify(1, 4, atlas, "world/expansion01/doodads/terokkar/trees/terokkartreesmall") == 0 && cr::Classify(0, 0, atlas) == 0,
              "a row naming its model: Terokkar's crystal takes it, its trees with the same atlas don't, nor a WMO");
        Check(cr::Classify(0, 0, "World\\Expansion01\\Doodads\\Netherstorm\\Rocks\\Netherstorm_FloatingRock_02.blp") == 0 &&
              cr::Classify(0, 0, "world\\environment\\doodad\\icecrown\\iceshards\\tex\\iceshard_noalpha.blp") == 0 &&
              cr::Classify(0, 0, "") == 0 && cr::Classify(0, 0, nullptr) == 0,
              "rock, the ice's textures, an empty or missing name: no");
    }

    // --- 3. the map scan --------------------------------------------------------------------------------------------
    std::vector<std::vector<std::string>> ReadTsv(const fs::path& file)
    {
        std::vector<std::vector<std::string>> rows;
        std::ifstream f(file);
        std::string line;
        bool header = true;
        while (std::getline(f, line))
        {
            if (header) { header = false; continue; }
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::vector<std::string> cells;
            std::stringstream ss(line);
            std::string cell;
            while (std::getline(ss, cell, '\t')) cells.push_back(cell);
            rows.push_back(cells);
        }
        return rows;
    }

    std::string Base(const std::string& path)
    {
        const size_t at = path.find_last_of("\\/");
        return at == std::string::npos ? path : path.substr(at + 1);
    }

    void SurveyChecks(const fs::path& folder)
    {
        const auto batches = ReadTsv(folder / "m2_batches.tsv");
        const auto placed = ReadTsv(folder / "placements.tsv");
        const auto materials = ReadTsv(folder / "wmo_materials.tsv");
        if (batches.empty() || placed.empty() || materials.empty()) { Check(false, "survey files in " + folder.string()); return; }
        std::map<std::string, std::pair<long, long>> placements;
        for (const auto& r : placed) if (r.size() >= 3) placements[r[0]] = { std::stol(r[1]), std::stol(r[2]) };
        // Per model: its crystal batches (taking the look), and its crystal-texture batches drawn see-through or glowing.
        struct Model { uint32_t family = 0; int taken = 0, skipped = 0, tris = 0; };
        std::map<std::string, Model> models;
        int iceWithCrystal = 0;
        for (const auto& r : batches)
        {
            if (r.size() < 8 || r[1] == "-1") continue;
            const std::string& model = r[0];
            const uint32_t blend = static_cast<uint32_t>(std::stoi(r[7]));
            const uint32_t flags = static_cast<uint32_t>(std::stoul(r[6], nullptr, 16));
            const uint32_t row = cr::MaterialOf(r[4].c_str());
            if (!row) continue;
            const std::string& only = cr::MaterialAt(row)->model;
            if (!only.empty() && Lower(model).find(only) == std::string::npos) continue;   // a shared texture, not this model's crystal
            Model& m = models[model];
            const uint16_t c = cr::Classify(blend, flags, r[4].c_str(), model.c_str());
            if (c)
            {
                ++m.taken;
                m.tris += std::stoi(r[3]);
                m.family = cr::MaterialAt(c & ~cr::kUnlitBit)->family;
            }
            else ++m.skipped;
        }
        std::map<uint32_t, std::array<long, 4>> fam;   // models, batches, ADT placements, WMO-doodad placements
        int seeThrough = 0;
        long seeThroughPlaced = 0, total = 0;
        std::string seeThroughNames;
        for (const auto& kv : models)
        {
            const auto p = placements.count(kv.first) ? placements[kv.first] : std::pair<long, long>{ 0, 0 };
            if (mi::IsIceModel(kv.first.c_str()) && kv.second.taken) ++iceWithCrystal;
            if (!kv.second.taken)
            {
                ++seeThrough;
                seeThroughPlaced += p.first + p.second;
                if (p.first + p.second > 0 && seeThroughNames.size() < 400) seeThroughNames += (seeThroughNames.empty() ? "" : ", ") + Base(kv.first);
                continue;
            }
            auto& f = fam[kv.second.family];
            f[0] += 1; f[1] += kv.second.taken; f[2] += p.first; f[3] += p.second;
            total += p.first + p.second;
        }
        long modelsTaken = 0;
        for (const auto& kv : fam)
        {
            char line[200];
            std::snprintf(line, sizeof(line), "%-15s %3ld models, %4ld batches, placed %6ld by the maps + %6ld in buildings", cr::FamilyOf(kv.first).name,
                          kv.second[0], kv.second[1], kv.second[2], kv.second[3]);
            Info(line);
            modelsTaken += kv.second[0];
        }
        Check(modelsTaken >= 150 && total >= 15000, "M2s taking the crystal look: " + std::to_string(modelsTaken) + " models, " + std::to_string(total) +
              " placements (maps and buildings' doodads; server-spawned objects come on top)");
        Info("already see-through or glowing (every crystal batch alpha or additive): " + std::to_string(seeThrough) + " models, " +
             std::to_string(seeThroughPlaced) + " placements: " + seeThroughNames);
        Check(iceWithCrystal == 0, "no ice model has a batch counted as crystal (the ice keeps the ice)");
        auto taken = [&](const char* stem) { for (const auto& kv : models) if (kv.first.find(stem) != std::string::npos) return kv.second.taken > 0; return false; };
        bool in = true;
        std::string missing;
        for (const char* stem : { "exodar_crystal_large.m2", "silvermystcrystalsmall02.m2", "bloodmystcrystal01.m2", "ogrila_crystal03.m2",
                                  "chromaticcrystralformation_03.m2", "terokkarcrystal01.m2", "crystalsongtree01.m2", "amethystcrystal02.m2",
                                  "ul_crystal_light01.m2", "ungorocrystal_blue01.m2", "shattrath_crystal_temple_int.m2", "ns_rock_09.m2",
                                  "diamondcrystal03.m2", "be_demoncrystal_01.m2", "sholazar_crystal_01.m2", "terokkarcrystal02.m2" })
            if (!taken(stem)) { in = false; missing += std::string(" ") + stem; }
        Check(in, "the draenei's, Bloodmyst's, Ogri'la's, Shadowmoon's, Terokkar's, Crystalsong's, Maraudon's, Ulduar's, Un'Goro's, Shattrath's, "
                  "Netherstorm's, Silvermoon's and Sholazar's crystals take it" + missing);
        bool out = true;
        for (const char* stem : { "bloodelf_powerorb_green.m2", "zuldrak_gem_blue_01.m2", "dalaran_buildingcrystal_01.m2", "dragonblight_iceshard_01.m2",
                                  "crystalsong_glow01.m2", "exodar_center_naaru_crystals.m2", "terokkartreesmall.m2", "terokkarbush01.m2" })
            out = out && !taken(stem);
        Check(out, "the blood elves' power orbs, Zul'Drak's gems, Dalaran's floating crystals, the Exodar's naaru crystals (see-through or glowing), "
                   "Dragonblight's ice shards (the ice) and Terokkar's trees (its crystals' atlas) don't");
        // WMO materials.
        std::map<uint32_t, std::array<long, 3>> wf;   // materials, WMOs, placements
        std::map<uint32_t, std::set<std::string>> wmos;
        std::set<std::string> seenWmo;
        long wmoPlaced = 0, skippedMaterials = 0;
        for (const auto& r : materials)
        {
            if (r.size() < 9) continue;
            const uint32_t flags = static_cast<uint32_t>(std::stoul(r[4], nullptr, 16)), blend = static_cast<uint32_t>(std::stoi(r[6]));
            if (!cr::MaterialOf(r[2].c_str())) continue;
            const uint16_t c = cr::Classify(blend, flags, r[2].c_str());
            if (!c) { ++skippedMaterials; continue; }
            const uint32_t family = cr::MaterialAt(c & ~cr::kUnlitBit)->family;
            auto& f = wf[family];
            f[0] += 1;
            if (wmos[family].insert(r[0]).second) { f[1] += 1; f[2] += std::stol(r[8]); }
            if (seenWmo.insert(r[0]).second) wmoPlaced += std::stol(r[8]);
        }
        long wmoMaterials = 0;
        for (const auto& kv : wf)
        {
            char line[200];
            std::snprintf(line, sizeof(line), "%-15s %3ld WMO materials in %3ld WMOs placed %4ld times", cr::FamilyOf(kv.first).name, kv.second[0], kv.second[1], kv.second[2]);
            Info(line);
            wmoMaterials += kv.second[0];
        }
        Check(wmoMaterials >= 100 && seenWmo.size() >= 40, "WMO materials taking it: " + std::to_string(wmoMaterials) + " in " + std::to_string(seenWmo.size()) +
              " WMOs placed " + std::to_string(wmoPlaced) + " times; " + std::to_string(skippedMaterials) + " crystal materials drawn see-through left as they are");
    }

    // --- 4. the shader edit ------------------------------------------------------------------------------------------
    struct Blob { std::string name; std::vector<uint8_t> code; };

    std::vector<Blob> LoadBlobs(const fs::path& dir)
    {
        std::vector<Blob> out;
        for (const auto& entry : fs::directory_iterator(dir))
        {
            if (entry.path().extension() != ".bls") continue;
            std::ifstream f(entry.path(), std::ios::binary);
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            int k = 0;
            for (size_t i = 0; i + 8 <= data.size(); i += 4)
            {
                uint32_t token;
                std::memcpy(&token, data.data() + i, 4);
                if (token != 0xFFFF0300u) continue;
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
        if (!wxl_livingazeroth::shaderpatch::Disassemble(code.data(), code.size(), text, why)) return -1;
        std::smatch m;
        static const std::regex slots(R"(approximately (\d+) instruction slots)");
        return std::regex_search(text, m, slots) ? std::stoi(m[1].str()) : -1;
    }

    struct Pick { std::string text, vP, vN; std::vector<uint8_t> code; };
    const std::regex kPos(R"(dp3 r\d+\.\w, (v\d+), c3\b)"), kNrm(R"(dp3 r\d+\.\w, c4, (v\d+))");

    bool HasCrystalCode(const std::string& src)
    {
        return src.find("c79.w") != std::string::npos && src.find("c80.w") != std::string::npos && src.find(", c81") != std::string::npos &&
               src.find(", c78") != std::string::npos;
    }

    using EditFn = bool (*)(std::string&, std::string&);
    struct EditResult { int shadow = 0, patched = 0, assembled = 0, crystal = 0, maxSlots = 0; std::string maxName; };

    EditResult EditAll(const std::vector<Blob>& blobs, EditFn edit, bool dissolve, std::map<std::string, Pick>* picks, const char* prefix)
    {
        EditResult r;
        for (const Blob& b : blobs)
        {
            std::string src, why;
            if (!wxl_livingazeroth::shaderpatch::Disassemble(b.code.data(), b.code.size(), src, why)) continue;
            std::smatch mp, mn;
            const bool shadow = std::regex_search(src, mp, kPos) && std::regex_search(src, mn, kNrm);
            const std::string vP = shadow ? mp[1].str() : std::string(), vN = shadow ? mn[1].str() : std::string();
            r.shadow += shadow ? 1 : 0;
            const std::string stock = src;
            const bool edited = edit(src, why);
            if (!edited) src = stock;
            if (dissolve) { std::string whyNot; wxl_livingazeroth::doodadfade::EditPixelDissolve(src, whyNot); }
            if (!edited) continue;
            ++r.patched;
            r.crystal += HasCrystalCode(src) ? 1 : 0;
            std::vector<uint8_t> code;
            if (!wxl_livingazeroth::shaderpatch::Assemble(src, b.name.c_str(), code, why)) { std::printf("      %s: %s\n", b.name.c_str(), why.substr(0, 200).c_str()); continue; }
            ++r.assembled;
            const int slots = Slots(code);
            if (slots > r.maxSlots) { r.maxSlots = slots; r.maxName = b.name; }
            if (picks && prefix && b.name.rfind(prefix, 0) == 0 && b.name.size() == std::strlen(prefix) + 2 && !picks->count(prefix))
                (*picks)[prefix] = Pick{ src, vP, vN, code };
        }
        return r;
    }

    // --- 6. the facets follow the crystal's size ----------------------------------------------------------------------
    std::string Fmt(const char* format, double a, double b = 0.0, double c = 0.0, double d = 0.0)
    {
        char line[200];
        std::snprintf(line, sizeof(line), format, a, b, c, d);
        return line;
    }

    bool Near(float got, float want, float relative, float absolute = 0.0f)
    {
        return std::isfinite(got) && std::fabs(got - want) <= relative * std::fabs(want) + absolute;
    }

    // A cube of side `side` at `x`, each face with its own four corners (two triangles), as a model's hard edges split them.
    void Cube(std::vector<float>& out, float x, float side)
    {
        const float s = side;
        const float c[8][3] = { { x, 0, 0 }, { x + s, 0, 0 }, { x + s, s, 0 }, { x, s, 0 }, { x, 0, s }, { x + s, 0, s }, { x + s, s, s }, { x, s, s } };
        const int faces[6][4] = { { 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 1, 2, 6, 5 }, { 2, 3, 7, 6 }, { 3, 0, 4, 7 } };
        for (const auto& f : faces)
            for (int k : { f[0], f[1], f[2], f[0], f[2], f[3] }) out.insert(out.end(), c[k], c[k] + 3);
    }

    void SizeChecks()
    {
        // 6a. CellScale: 1 for a shard, rising smoothly, levelling off at the most; finite for anything.
        bool finite = true, inRange = true, rising = true;
        float last = cr::CellScale(0.0f), worstStep = 0.0f;
        for (float size = 0.01f; size < 2.0e5f; size *= 1.02f)
        {
            const float s = cr::CellScale(size);
            finite = finite && std::isfinite(s);
            inRange = inRange && s >= 1.0f && s <= cr::kMaxCellScale;
            rising = rising && s >= last - 1e-5f;
            worstStep = std::max(worstStep, s / last - 1.0f);
            last = s;
        }
        bool odd = true;
        for (float v : { 0.0f, -3.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::max() })
            odd = odd && cr::CellScale(v) == 1.0f;
        const float huge = cr::CellScale(std::numeric_limits<float>::max());
        Check(finite && inRange && rising && worstStep < 0.02f && odd && std::isfinite(huge) && huge <= cr::kMaxCellScale,
              Fmt("CellScale: 1 at 0, for a negative or non-finite size; rising, never more than %.0f; no step over %.2f%% per 2%% of size "
                  "(0.01 to 200,000 yd); the largest float: x%.3f", cr::kMaxCellScale, 100.0 * worstStep, huge));
        const float gem = cr::CellScale(0.34f), shard = cr::CellScale(1.37f), medium = cr::CellScale(21.5f), giant = cr::CellScale(105.1f),
                    spire = cr::CellScale(159.2f);
        Check(gem <= 1.01f && shard < 1.15f, Fmt("a hand-sized shard keeps today's facets: a jeweller's gem (0.34 yd) x%.3f, a Silvermyst crystal (1.37 yd) x%.3f", gem, shard));
        Check(medium > 3.5f && medium < 5.0f, Fmt("the Exodar's big M2 crystals (exodar_crystal_large, 21.5 yd): facets x%.2f", medium));
        Check(giant > 7.0f && spire > giant && spire <= cr::kMaxCellScale,
              Fmt("the Exodar's giant crystals (the one by the walkway 105 yd, its spire 159 yd): facets x%.2f, x%.2f", giant, spire));

        // 6b. The constants: a size of 0 gives the old ones exactly; the facets, the depths and the fade grow together.
        const cr::Material* violet = cr::MaterialAt(cr::MaterialOf("jlo_draenei_crystal01"));
        if (!violet) { Check(false, "jlo_draenei_crystal01's row"); return; }
        constexpr int kLook = 4 * wxl_livingazeroth::terrainice::kModelConstantCount;
        struct Out { float look[kLook], c179[4], c190[4], c[4 * cr::kRegisters]; };
        auto make = [&](const cr::Material& m, const cr::Settings& s, float size) {
            Out o{};
            wxl_livingazeroth::terrainice::FillModelConstants(o.look);
            cr::Constants(m, s, 0.3f, false, o.look, o.c179, o.c190, o.c, size);
            return o;
        };
        const float facet = violet->facetYards;
        const Out base = make(*violet, cr::Settings{}, 0.0f);
        Check(base.look[28] == 0.25f * facet && base.look[29] == 0.8f * facet && base.look[30] == 1.0f / facet && base.look[31] == 1.0f / (1.7f * facet) &&
              base.c190[3] == -1.0f / 120.0f && base.look[39] == 180.0f / 120.0f,
              Fmt("a size of 0 (and CrystalCellScale 1): the row's facets (%.1f yd), depths, repeats and the 60..180 yd fade, exactly as before", facet));
        const float k = cr::CellScale(105.1f);
        const Out big = make(*violet, cr::Settings{}, 105.1f);
        bool others = true;
        for (int i = 0; i < kLook; ++i) others = others && (i >= 28 && i <= 31 ? true : big.look[i] == base.look[i]);
        for (int i = 0; i < 4; ++i) others = others && big.c179[i] == base.c179[i] && (i == 3 || big.c190[i] == base.c190[i]);
        for (int i = 0; i < 4 * cr::kRegisters; ++i) others = others && big.c[i] == base.c[i];
        Check(Near(big.look[28], 0.25f * facet * k, 1e-5f) && Near(big.look[29], 0.8f * facet * k, 1e-5f) && Near(big.look[30], 1.0f / (facet * k), 1e-5f) &&
              Near(big.look[31], 1.0f / (1.7f * facet * k), 1e-5f) && Near(big.c190[3], -1.0f / (120.0f * k), 1e-5f) && big.look[39] == base.look[39] && others,
              Fmt("a 105 yd crystal: facets %.1f yd a repeat (x%.2f), the layers %.1f and %.1f yd deep", facet * k, k, big.look[28], big.look[29]) +
              Fmt(", the detail fading from %.0f to %.0f yd; nothing else changes", 60.0 * k, 180.0 * k));
        cr::Settings twice; twice.cellScale = 2.0f;
        cr::Settings nan; nan.cellScale = std::numeric_limits<float>::quiet_NaN();
        cr::Settings over; over.cellScale = 100.0f;
        cr::Settings under; under.cellScale = 0.0f;
        const Out x2 = make(*violet, twice, 105.1f), xn = make(*violet, nan, 105.1f), xo = make(*violet, over, 0.0f), xu = make(*violet, under, 0.0f);
        Check(Near(x2.look[28], 2.0f * big.look[28], 1e-5f) && Near(x2.look[30], 0.5f * big.look[30], 1e-5f) && Near(x2.c190[3], 0.5f * big.c190[3], 1e-5f) &&
              xn.look[28] == big.look[28] && Near(xo.look[28], cr::kMaxCellSetting * base.look[28], 1e-5f) &&
              Near(xu.look[28], cr::kMinCellSetting * base.look[28], 1e-5f),
              "CrystalCellScale multiplies the facets, their depths and the fade (2: twice; not a number: 1; clamped to 0.25..4)");
        bool allFinite = true;
        float smallest = 1e30f, largest = 0.0f;
        cr::Material fine = *violet, coarse = *violet;
        fine.facetYards = 0.2f; coarse.facetYards = 40.0f;
        for (const cr::Material* m : { violet, static_cast<const cr::Material*>(&fine), static_cast<const cr::Material*>(&coarse) })
            for (float size : { 0.0f, 1e-30f, 105.0f, 1e30f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
                for (float setting : { 1.0f, 1e30f, -5.0f, std::numeric_limits<float>::quiet_NaN() })
                {
                    cr::Settings s; s.cellScale = setting;
                    const Out o = make(*m, s, size);
                    for (float v : o.look) allFinite = allFinite && std::isfinite(v);
                    for (float v : o.c190) allFinite = allFinite && std::isfinite(v);
                    smallest = std::min(smallest, 1.0f / o.look[30]); largest = std::max(largest, 1.0f / o.look[30]);
                }
        Check(allFinite && smallest >= 0.05f && largest <= 2000.0f,
              Fmt("the constants are finite for any size and setting (the facets %.2f to %.0f yd a repeat)", smallest, largest));

        // 6c. PieceRadius: pieces joined at the same place, the median by vertex.
        std::vector<float> one, four, bad;
        Cube(one, 0.0f, 1.0f);
        Cube(four, 0.0f, 1.0f); Cube(four, 5.0f, 1.0f); Cube(four, 10.0f, 1.0f); Cube(four, 20.0f, 10.0f);
        Cube(bad, 0.0f, 2.0f);
        for (int k2 = 0; k2 < 3; ++k2) bad.push_back(std::numeric_limits<float>::quiet_NaN());
        bad.insert(bad.end(), { 100, 100, 100, 101, 100, 100 });
        const float cube = 0.5f * std::sqrt(3.0f);
        const float r1 = cr::PieceRadius(one.data(), one.size() / 3), r4 = cr::PieceRadius(four.data(), four.size() / 3),
                    rb = cr::PieceRadius(bad.data(), bad.size() / 3);
        Check(Near(r1, cube, 1e-4f) && Near(r4, cube, 1e-4f) && Near(rb, 2.0f * cube, 1e-4f) && cr::PieceRadius(nullptr, 9) == 0.0f &&
              cr::PieceRadius(one.data(), 2) == 0.0f,
              Fmt("PieceRadius: a cube split at its edges is one piece (%.3f yd); three small cubes and a big one: the small ones' (%.3f); a triangle "
                  "with a corner not a number is left out (%.3f); none: 0", r1, r4, rb));
    }

    // The client's in-place load of a file's arrays: the M2Array at `at` (count, file offset) gets the absolute pointer.
    static_assert(sizeof(void*) == 4, "the client's layout: 32-bit pointers (build the tests for Win32)");
    void Rebase(std::vector<uint8_t>& buf, size_t at)
    {
        uint32_t offset = 0;
        std::memcpy(&offset, buf.data() + at + 4, 4);
        const uint32_t p = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf.data()) + offset);
        std::memcpy(buf.data() + at + 4, &p, 4);
    }
    void PutPointer(std::vector<uint8_t>& buf, size_t at, const void* p)
    {
        const uint32_t v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
        std::memcpy(buf.data() + at, &v, 4);
    }
    std::vector<uint8_t> ReadFile(const fs::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }

    // An M2 batch as the game holds it: the .m2 and its 00.skin, rebased as the client's load does, and a draw context, its
    // batch element, the instance and the runtime model pointing at them; the size through crystal::M2BatchPieceRadius.
    float M2Size(const fs::path& folder, std::string model, uint32_t batch, std::string& why)
    {
        std::replace(model.begin(), model.end(), '\\', '#');
        std::vector<uint8_t> m2 = ReadFile(folder / model), skin = ReadFile(folder / (model.substr(0, model.size() - 3) + "00.skin"));
        if (m2.size() < 0x100 || skin.size() < 0x30) { why = "files"; return -1.0f; }
        Rebase(m2, 0x3C);
        for (size_t at : { 0x04u, 0x0Cu, 0x1Cu, 0x24u }) Rebase(skin, at);
        uint32_t batches = 0, records = 0;
        std::memcpy(&batches, skin.data() + 0x24, 4);
        std::memcpy(&records, skin.data() + 0x28, 4);
        if (batch >= batches) { why = "no such batch"; return -1.0f; }
        const uint8_t* record = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(records)) + 0x18 * batch;
        std::vector<uint8_t> ctx(0x100), element(0x40), instance(0x200), runtime(0x200);
        PutPointer(ctx, 0x48, m2.data()); PutPointer(ctx, 0x50, element.data()); PutPointer(ctx, 0x60, instance.data());
        PutPointer(element, 0x28, record); PutPointer(instance, 0x2C, runtime.data()); PutPointer(runtime, 0x170, skin.data());
        if (cr::M2BatchKey(ctx.data()) != record) { why = "batch key"; return -1.0f; }
        return cr::M2BatchPieceRadius(ctx.data());
    }

    // A WMO batch as the game holds it: the group file's MOVI and MOVT behind a group object's pointers and counts, and its
    // MOBA record; the size through crystal::WmoBatchPieceRadius.
    float WmoSize(const fs::path& folder, std::string wmo, int group, uint32_t batch, std::string& why)
    {
        std::replace(wmo.begin(), wmo.end(), '\\', '#');
        char suffix[16];
        std::snprintf(suffix, sizeof(suffix), "_%03d.wmo", group);
        const std::vector<uint8_t> d = ReadFile(folder / (wmo.substr(0, wmo.size() - 4) + suffix));
        if (d.size() < 20 + 0x44 || std::memcmp(d.data() + 12, "PGOM", 4) != 0) { why = "group file"; return -1.0f; }
        const uint8_t *movi = nullptr, *movt = nullptr, *moba = nullptr;
        uint32_t moviSize = 0, movtSize = 0, mobaSize = 0;
        for (size_t o = 20 + 0x44; o + 8 <= d.size();)
        {
            uint32_t size = 0;
            std::memcpy(&size, d.data() + o + 4, 4);
            const std::string tag(reinterpret_cast<const char*>(d.data() + o), 4);
            if (tag == "IVOM" && !movi) { movi = d.data() + o + 8; moviSize = size; }
            if (tag == "TVOM" && !movt) { movt = d.data() + o + 8; movtSize = size; }
            if (tag == "ABOM" && !moba) { moba = d.data() + o + 8; mobaSize = size; }
            o += 8 + size;
        }
        if (!movi || !movt || !moba || batch >= mobaSize / 24) { why = "chunks"; return -1.0f; }
        std::vector<uint8_t> object(0x200);
        PutPointer(object, 0xE0, movi); PutPointer(object, 0xE8, movt);
        const uint32_t indices = moviSize / 2, vertices = movtSize / 12;
        std::memcpy(object.data() + 0x154, &indices, 4);
        std::memcpy(object.data() + 0x15C, &vertices, 4);
        return cr::WmoBatchPieceRadius(object.data(), moba + 24 * batch);
    }

    void GeometryChecks(const fs::path& survey, const fs::path* wmoGroups)
    {
        // The sizes the survey's own scripts found from the files (outside the repo: m2_sizes.py and wmo_batches.py;
        // connected pieces, positions joined to 1/256 yd, the median by distinct position), against the readers the game runs.
        struct M2Case { const char* model; uint32_t batch; float want; const char* what; };
        const M2Case m2s[] = {
            { "world\\expansion01\\doodads\\generic\\tradeskill\\jewelcrafting\\jewelcraft_gemcut_03.m2", 0, 0.338f, "a jeweller's cut gem" },
            { "world\\expansion01\\doodads\\silvermyst\\crystals\\silvermystcrystalsmall02.m2", 0, 1.370f, "Azuremyst's small Silvermyst crystal" },
            { "world\\kalimdor\\ungoro\\passivedoodads\\crystals\\ungorocrystal_blue01.m2", 0, 0.531f, "Un'Goro's blue cluster (its crystals, not the model's 5.6 yd)" },
            { "world\\expansion01\\doodads\\bloodmyst\\trees\\bloodmysttreecrystal02.m2", 0, 2.493f, "Bloodmyst's crystal tree's crystals (the model: 33.8 yd)" },
            { "world\\expansion01\\doodads\\theexodar\\passivedoodads\\crystals\\exodar_crystal_large.m2", 3, 21.54f, "the Exodar's big M2 crystal" },
        };
        const fs::path m2Folder = survey / "m2";
        int good = 0;
        std::string lines;
        for (const M2Case& c : m2s)
        {
            std::string why;
            const float got = M2Size(m2Folder, c.model, c.batch, why);
            const bool ok = Near(got, c.want, 0.05f, 0.02f);
            good += ok ? 1 : 0;
            lines += Fmt("\n        %.3f yd (want %.3f), facets x%.2f: ", got, c.want, cr::CellScale(got)) + c.what + (why.empty() ? "" : " (" + why + ")");
        }
        Check(good == static_cast<int>(std::size(m2s)), "M2 batches' crystals' size through the game's reader (the skin and vertices rebased as loaded):" + lines);
        {
            std::vector<uint8_t> ctx(0x100);
            const float none = cr::M2BatchPieceRadius(ctx.data());
            PutPointer(ctx, 0x48, reinterpret_cast<const void*>(0x10)); PutPointer(ctx, 0x50, reinterpret_cast<const void*>(0x20));
            PutPointer(ctx, 0x60, reinterpret_cast<const void*>(0x30));
            const float wild = cr::M2BatchPieceRadius(ctx.data());
            Check(none == 0.0f && wild == 0.0f && cr::M2BatchPieceRadius(nullptr) == 0.0f && cr::WmoBatchPieceRadius(nullptr, ctx.data()) == 0.0f &&
                  cr::WmoBatchPieceRadius(reinterpret_cast<const void*>(0x40), reinterpret_cast<const void*>(0x50)) == 0.0f,
                  "unreadable batches (null or wild pointers) read as 0, the row's facets, without a fault");
        }
        if (!wmoGroups) { Info("no WMO groups folder: the WMO batches' sizes not checked"); return; }
        struct WmoCase { const char* wmo; int group; uint32_t batch; float want; const char* what; };
        const WmoCase wmos[] = {
            { "world\\wmo\\kalimdor\\draeneicity\\ol_draenei_city.wmo", 15, 14, 105.15f, "the Exodar's giant crystal by the walkway (54.7, 39.2; jlo_draenei_crystal02)" },
            { "world\\wmo\\kalimdor\\draeneicity\\ol_draenei_city.wmo", 4, 0, 159.15f, "the Exodar's crystal spire (Chrystal01)" },
            { "world\\wmo\\kalimdor\\draeneicity\\ol_draenei_city.wmo", 2, 0, 132.23f, "the Exodar's Hall02 crystal" },
            { "world\\wmo\\dungeon\\ol_tempestkeep\\tk_raid.wmo", 7, 11, 115.99f, "Tempest Keep's crystal wall" },
            { "world\\wmo\\northrend\\dalaran\\nd_dalaran.wmo", 65, 25, 0.908f, "Dalaran's tiny red crystals (57 of them over a 200 yd group)" },
            { "world\\wmo\\dungeon\\md_diamondmt\\diamondmountain.wmo", 2, 2, 1.528f, "Diamond Mountain's crystals (97 of them)" },
        };
        good = 0;
        lines.clear();
        for (const WmoCase& c : wmos)
        {
            std::string why;
            const float got = WmoSize(*wmoGroups, c.wmo, c.group, c.batch, why);
            const bool ok = Near(got, c.want, 0.05f, 0.02f);
            good += ok ? 1 : 0;
            lines += Fmt("\n        %.2f yd (want %.2f), facets x%.2f: ", got, c.want, cr::CellScale(got)) + c.what + (why.empty() ? "" : " (" + why + ")");
        }
        Check(good == static_cast<int>(std::size(wmos)), "WMO batches' crystals' size through the game's reader (the group's MOVI, MOVT and MOBA):" + lines);
    }

    // --- 5. WARP ---------------------------------------------------------------------------------------------------
    template <class T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

    struct Warp
    {
        ID3D12Device* d12 = nullptr;
        IDirect3D9* d3d = nullptr;
        IDirect3DDevice9* dev = nullptr;
        IDirect3DVertexDeclaration9* decl = nullptr;
        IDirect3DTexture9* white = nullptr;
        ~Warp() { Release(white); Release(decl); Release(dev); Release(d3d); Release(d12); }
    };

    IDirect3DTexture9* Solid(IDirect3DDevice9* dev, uint32_t argb)
    {
        IDirect3DTexture9* t = nullptr;
        if (FAILED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr))) return nullptr;
        D3DLOCKED_RECT lr;
        t->LockRect(0, &lr, nullptr, 0);
        *static_cast<uint32_t*>(lr.pBits) = argb;
        t->UnlockRect(0);
        return t;
    }

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
        wc.lpszClassName = L"CrystalTests";
        RegisterClassW(&wc);
        HWND window = CreateWindowW(wc.lpszClassName, L"crystal", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window;
        pp.BackBufferWidth = 64; pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        if (FAILED(w.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &w.dev))) { why = "CreateDevice failed"; return false; }
        const D3DVERTEXELEMENT9 elements[] = {
            { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
            { 0, 16, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
            { 0, 28, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },
            { 0, 40, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2 },
            { 0, 48, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
            D3DDECL_END() };
        if (FAILED(w.dev->CreateVertexDeclaration(elements, &w.decl))) { why = "no vertex declaration"; return false; }
        w.white = Solid(w.dev, 0xFFFFFFFFu);
        return w.white != nullptr;
    }

    // A vertex: clip position, view-space position (x ahead, y left, z up) and normal, uv, the lit colour.
    struct Vtx { float clip[4]; float view[3]; float normal[3]; float uv[2]; float colour[4]; };

    // A vs_3_0 writing every input the pixel shader declares: the colour, fog factor 1 (c0), the view position and normal,
    // the uv for every two-component texcoord, shadow coordinates outside the shadow map (c1) for the rest.
    bool BuildVs(IDirect3DDevice9* dev, const Pick& p, IDirect3DVertexShader9** out)
    {
        static const std::regex input(R"(dcl_(color|texcoord|fog)(\d*) (v\d+)(\.\w+)?)");
        std::string decls = "vs_3_0\ndcl_position v0\ndcl_texcoord0 v1\ndcl_texcoord1 v2\ndcl_texcoord2 v3\ndcl_color v4\ndcl_position o0\n", body = "mov o0, v0\n";
        int o = 1;
        for (auto it = std::sregex_iterator(p.text.begin(), p.text.end(), input); it != std::sregex_iterator(); ++it, ++o)
        {
            const std::string usage = (*it)[1].str(), index = (*it)[2].str(), v = (*it)[3].str(), mask = (*it)[4].str();
            const std::string reg = "o" + std::to_string(o);
            decls += "dcl_" + usage + index + " " + reg + "\n";
            if (usage == "color") body += "mov " + reg + ", v4\n";
            else if (usage == "fog") body += "mov " + reg + ", c0\n";
            else if (v == p.vP) body += "mov " + reg + ", v1\n";
            else if (v == p.vN) body += "mov " + reg + ", v2\n";
            else if (mask == ".xy") body += "mov " + reg + ", v3\n";
            else body += "mov " + reg + ", c1\n";
        }
        std::vector<uint8_t> code;
        std::string why;
        if (!wxl_livingazeroth::shaderpatch::Assemble(decls + body, "crystal test vs", code, why)) { std::printf("vs: %s\n", why.c_str()); return false; }
        return SUCCEEDED(dev->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()), out));
    }

    // The scene: a hexagonal crystal (radius 1, a 3 yd body and a 1.2 yd point; x `scale`) seen from `elevation` degrees,
    // turned by `yaw`, `distance` yd off a point `targetZ` up its axis, the far plane `farPlane` yd off; by day (the sun from up
    // behind the camera's left) or at night (a dim blue moon); unlit materials as the client draws them (their texture at
    // full); the uv rectangle of the texture the crystal shows, repeated `tile` times up a face.
    struct View { float elevation = 8.0f, yaw = 30.0f, distance = 7.0f, scale = 1.0f, targetZ = 1.6f, farPlane = 100.0f, tile = 1.0f; };
    struct Light { bool night = false; bool unlit = true; };

    void Sub(const float a[3], const float b[3], float o[3]) { for (int k = 0; k < 3; ++k) o[k] = a[k] - b[k]; }
    float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
    void Cross(const float a[3], const float b[3], float o[3]) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
    void Norm(float v[3]) { const float n = std::sqrt(Dot(v, v)); if (n > 0) for (int k = 0; k < 3; ++k) v[k] /= n; }

    struct Frame { float cam[3], f[3], r[3], u[3]; };
    Frame Camera(const View& v)
    {
        Frame fr;
        const float e = v.elevation * 3.14159265f / 180.0f;
        fr.cam[0] = -v.distance * std::cos(e); fr.cam[1] = 0.0f; fr.cam[2] = v.distance * std::sin(e) + v.targetZ;
        const float target[3] = { 0.0f, 0.0f, v.targetZ }, up[3] = { 0.0f, 0.0f, 1.0f };
        Sub(target, fr.cam, fr.f); Norm(fr.f);
        Cross(fr.f, up, fr.r); Norm(fr.r);
        Cross(fr.r, fr.f, fr.u);
        return fr;
    }
    void ToView(const Frame& fr, const float p[3], float o[3]) { float d[3]; Sub(p, fr.cam, d); o[0] = Dot(d, fr.f); o[1] = -Dot(d, fr.r); o[2] = Dot(d, fr.u); }
    void DirToView(const Frame& fr, const float n[3], float o[3]) { o[0] = Dot(n, fr.f); o[1] = -Dot(n, fr.r); o[2] = Dot(n, fr.u); }

    const float kSunWorld[3] = { -0.62f, 0.38f, 0.69f };     // toward the sun (world)
    const float kMoonWorld[3] = { -0.30f, -0.55f, 0.78f };

    // The crystal's triangles in the world (the corners, 3 floats each), as Crystal draws them: what PieceRadius sees.
    std::vector<float> CrystalCorners(const View& v);

    std::vector<Vtx> Crystal(const View& v, const Light& l, const float uvRect[4], float aspect)
    {
        const Frame fr = Camera(v);
        const float yaw = v.yaw * 3.14159265f / 180.0f, H = 3.0f * v.scale, T = 1.2f * v.scale, R = v.scale, f = 2.4f;
        float L[3] = { l.night ? kMoonWorld[0] : kSunWorld[0], l.night ? kMoonWorld[1] : kSunWorld[1], l.night ? kMoonWorld[2] : kSunWorld[2] };
        Norm(L);
        std::vector<Vtx> out;
        auto vertex = [&](const float p[3], const float n[3], float u, float w)
        {
            Vtx x{};
            ToView(fr, p, x.view);
            DirToView(fr, n, x.normal);
            x.clip[0] = -x.view[1] * f / aspect; x.clip[1] = x.view[2] * f; x.clip[3] = x.view[0];
            x.clip[2] = (x.view[0] - 0.1f) / v.farPlane * x.view[0];
            x.uv[0] = uvRect[0] + u * (uvRect[2] - uvRect[0]);
            x.uv[1] = uvRect[1] + w * v.tile * (uvRect[3] - uvRect[1]);
            const float d = std::max(0.0f, Dot(n, L));
            for (int k = 0; k < 3; ++k)
            {
                const float ambient = l.night ? (k == 2 ? 0.22f : 0.12f) : (k == 2 ? 0.50f : 0.44f);
                const float sun = l.night ? (k == 2 ? 0.40f : 0.26f) : (k == 0 ? 0.80f : k == 1 ? 0.76f : 0.68f);
                x.colour[k] = l.unlit ? 1.0f : ambient + sun * d;
            }
            x.colour[3] = 1.0f;
            out.push_back(x);
        };
        for (int s = 0; s < 6; ++s)
        {
            const float a0 = yaw + s * 1.0471976f, a1 = a0 + 1.0471976f, am = 0.5f * (a0 + a1);
            const float p0[3] = { R * std::cos(a0), R * std::sin(a0), 0.0f }, p1[3] = { R * std::cos(a1), R * std::sin(a1), 0.0f };
            const float q0[3] = { p0[0], p0[1], H }, q1[3] = { p1[0], p1[1], H }, tip[3] = { 0.0f, 0.0f, H + T };
            const float n[3] = { std::cos(am), std::sin(am), 0.0f };
            vertex(p0, n, 0, 1); vertex(p1, n, 1, 1); vertex(q1, n, 1, 0.25f);
            vertex(p0, n, 0, 1); vertex(q1, n, 1, 0.25f); vertex(q0, n, 0, 0.25f);
            // the point's face: its normal from the slope
            float e1[3], e2[3], nt[3];
            Sub(q1, q0, e1); Sub(tip, q0, e2); Cross(e1, e2, nt); Norm(nt);
            vertex(q0, nt, 0, 0.25f); vertex(q1, nt, 1, 0.25f); vertex(tip, nt, 0.5f, 0.0f);
        }
        return out;
    }

    std::vector<float> CrystalCorners(const View& v)
    {
        const float yaw = v.yaw * 3.14159265f / 180.0f, H = 3.0f * v.scale, T = 1.2f * v.scale, R = v.scale;
        std::vector<float> out;
        auto corner = [&](const float p[3]) { out.insert(out.end(), p, p + 3); };
        for (int s = 0; s < 6; ++s)
        {
            const float a0 = yaw + s * 1.0471976f, a1 = a0 + 1.0471976f;
            // each face's own corners (split at the hard edges, as a model's are)
            const float p0[3] = { R * std::cos(a0), R * std::sin(a0), 0.0f }, p1[3] = { R * std::cos(a1), R * std::sin(a1), 0.0f };
            const float q0[3] = { p0[0], p0[1], H }, q1[3] = { p1[0], p1[1], H }, tip[3] = { 0.0f, 0.0f, H + T };
            corner(p0); corner(p1); corner(q1); corner(p0); corner(q1); corner(q0);
            corner(q0); corner(q1); corner(tip);
        }
        return out;
    }

    struct Scene
    {
        IDirect3DTexture9* base = nullptr;
        float fog[4] = { 0.55f, 0.65f, 0.78f, 1.0f };
        bool on = false;              // b7
        const cr::Material* material = nullptr;
        cr::Settings settings;
        float night = 0.0f;
        float glowNight = -1.0f;      // the night the glow goes by, -1: `night`
        bool unlit = true;
        bool zeroCrystal = false;     // c78..c81 zeros (the ice's), with the crystal's other constants
        const float* overrideC78 = nullptr;   // 4 floats replacing c78
        float size = 0.0f;            // the crystal's size (yd, PieceRadius) the facets go by; 0: a hand-sized crystal's
    };

    // Draws the crystal into `target` (and `depth`) with the patched variant; false on a failure.
    bool Draw(Warp& w, const Pick& p, IDirect3DPixelShader9* ps, IDirect3DVertexShader9* vs, IDirect3DSurface9* target, IDirect3DSurface9* depth,
              UINT width, UINT height, const View& view, const Scene& s, const float uvRect[4])
    {
        IDirect3DDevice9* dev = w.dev;
        dev->SetRenderTarget(0, target);
        dev->SetDepthStencilSurface(depth);
        D3DVIEWPORT9 vp{ 0, 0, width, height, 0.0f, 1.0f };
        dev->SetViewport(&vp);
        dev->SetRenderState(D3DRS_ZENABLE, TRUE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        const D3DCOLOR clear = D3DCOLOR_COLORVALUE(s.fog[0], s.fog[1], s.fog[2], 1.0f);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear, 1.0f, 0);
        dev->SetTexture(0, s.base);
        dev->SetTexture(1, w.white);
        for (DWORD k = 4; k < 8; ++k) dev->SetTexture(k, w.white);
        dev->SetTexture(14, cr::DetailTexture(dev));
        for (DWORD k : { 0u, 14u })
        {
            dev->SetSamplerState(k, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(k, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(k, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(k, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
            dev->SetSamplerState(k, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        }
        const float vsc[8] = { 1, 1, 1, 1, 5, 5, 0.5f, 1 };
        dev->SetVertexShaderConstantF(0, vsc, 2);
        float zeros[4 * 15] = {};
        dev->SetPixelShaderConstantF(0, zeros, 15);
        dev->SetPixelShaderConstantF(2, s.fog, 1);
        float look[4 * wxl_livingazeroth::terrainice::kModelConstantCount];
        wxl_livingazeroth::terrainice::FillModelConstants(look);
        // The view's rows (view -> world) and the camera, as the game gives them; the sun (view space).
        const Frame fr = Camera(view);
        for (int k = 0; k < 3; ++k) look[k] = fr.cam[k];
        for (int k = 0; k < 3; ++k) { look[4 + 4 * k + 0] = fr.f[k]; look[4 + 4 * k + 1] = -fr.r[k]; look[4 + 4 * k + 2] = fr.u[k]; }
        float L[3] = { s.night > 0.5f ? kMoonWorld[0] : kSunWorld[0], s.night > 0.5f ? kMoonWorld[1] : kSunWorld[1], s.night > 0.5f ? kMoonWorld[2] : kSunWorld[2] };
        Norm(L);
        DirToView(fr, L, look + 16);
        if (s.night > 0.5f) for (int k = 20; k < 23; ++k) look[k] *= 0.25f;
        float c179[4] = { 0, 0, 0, 0 }, c190[4] = { 0, 0, 0, 0 }, c[4 * cr::kRegisters] = {};
        if (s.material) cr::Constants(*s.material, s.settings, s.glowNight >= 0.0f ? s.glowNight : s.night, s.unlit, look, c179, c190, c, s.size);
        if (s.zeroCrystal) std::fill(std::begin(c), std::end(c), 0.0f);
        if (s.overrideC78) for (int k = 0; k < 4; ++k) c[k] = s.overrideC78[k];
        dev->SetPixelShaderConstantF(mi::kWeightConstant, c179, 1);
        dev->SetPixelShaderConstantF(mi::kLookConstant, look, wxl_livingazeroth::terrainice::kModelConstantCount);
        dev->SetPixelShaderConstantF(mi::kStyleConstant, c190, 1);
        dev->SetPixelShaderConstantF(cr::kConstant, c, cr::kRegisters);
        const BOOL b = s.on ? TRUE : FALSE;
        dev->SetPixelShaderConstantB(mi::kSwitch, &b, 1);
        const BOOL off = FALSE;
        for (int k = 8; k < 16; ++k) dev->SetPixelShaderConstantB(k, &off, 1);
        dev->SetVertexDeclaration(w.decl);
        dev->SetVertexShader(vs);
        dev->SetPixelShader(ps);
        const Light light{ s.night > 0.5f, s.unlit };
        const std::vector<Vtx> mesh = Crystal(view, light, uvRect, static_cast<float>(width) / height);
        const bool ok = SUCCEEDED(dev->BeginScene()) &&
                        SUCCEEDED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(mesh.size() / 3), mesh.data(), sizeof(Vtx)));
        dev->EndScene();
        (void)p;
        return ok;
    }

    struct Target
    {
        IDirect3DSurface9* rt = nullptr;
        IDirect3DSurface9* ds = nullptr;
        IDirect3DSurface9* rb = nullptr;
        UINT width = 0, height = 0;
        D3DFORMAT format = D3DFMT_A8R8G8B8;
        ~Target() { Release(rt); Release(ds); Release(rb); }
        bool Create(IDirect3DDevice9* dev, UINT w, UINT h, D3DFORMAT f)
        {
            width = w; height = h; format = f;
            return SUCCEEDED(dev->CreateRenderTarget(w, h, f, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, nullptr)) &&
                   SUCCEEDED(dev->CreateDepthStencilSurface(w, h, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &ds, nullptr)) &&
                   SUCCEEDED(dev->CreateOffscreenPlainSurface(w, h, f, D3DPOOL_SYSTEMMEM, &rb, nullptr));
        }
        // RGBA floats per pixel.
        bool Read(IDirect3DDevice9* dev, std::vector<float>& out)
        {
            D3DLOCKED_RECT lr;
            if (FAILED(dev->GetRenderTargetData(rt, rb)) || FAILED(rb->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
            out.resize(static_cast<size_t>(width) * height * 4);
            for (UINT y = 0; y < height; ++y)
                for (UINT x = 0; x < width; ++x)
                {
                    const uint8_t* row = static_cast<const uint8_t*>(lr.pBits) + y * lr.Pitch;
                    float* o = &out[(static_cast<size_t>(y) * width + x) * 4];
                    if (format == D3DFMT_A32B32G32R32F) std::memcpy(o, row + 16 * x, 16);
                    else
                    {
                        const uint32_t px = reinterpret_cast<const uint32_t*>(row)[x];
                        o[0] = ((px >> 16) & 255) / 255.0f; o[1] = ((px >> 8) & 255) / 255.0f; o[2] = (px & 255) / 255.0f; o[3] = (px >> 24) / 255.0f;
                    }
                }
            rb->UnlockRect();
            return true;
        }
    };

    // The crystal's pixels: those not the clear colour (the fog, as the target stores it: 8 bits a channel, truncated).
    struct Stats { int pixels = 0, bad = 0; double luma = 0.0, saturation = 0.0; };
    Stats Measure(const std::vector<float>& px, const float fog[4], const std::vector<bool>* mask = nullptr)
    {
        Stats s;
        for (size_t i = 0; i < px.size() / 4; ++i)
        {
            const float* c = &px[4 * i];
            if (!std::isfinite(c[0]) || !std::isfinite(c[1]) || !std::isfinite(c[2]) || !std::isfinite(c[3])) { ++s.bad; continue; }
            if (mask ? !(*mask)[i] : (std::fabs(c[0] - fog[0]) < 0.006f && std::fabs(c[1] - fog[1]) < 0.006f && std::fabs(c[2] - fog[2]) < 0.006f)) continue;
            ++s.pixels;
            s.luma += 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
            const float mx = std::max(c[0], std::max(c[1], c[2])), mn = std::min(c[0], std::min(c[1], c[2]));
            s.saturation += mx > 1e-4f ? (mx - mn) / mx : 0.0f;
        }
        if (s.pixels) { s.luma /= s.pixels; s.saturation /= s.pixels; }
        return s;
    }

    IDirect3DTexture9* LoadTexture(IDirect3DDevice9* dev, const fs::path& folder, const std::string& name)
    {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(folder, ec))
        {
            const std::string file = Lower(e.path().filename().string());
            const std::string want = "#" + name + ".blp";
            if (file.size() < want.size() || file.compare(file.size() - want.size(), want.size(), want) != 0) continue;
            std::ifstream f(e.path(), std::ios::binary);
            std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            std::string why;
            return wxl_livingazeroth::blp::LoadFromMemory(dev, d.data(), d.size(), why);
        }
        return nullptr;
    }

    void WriteBmp(const fs::path& path, const std::vector<uint32_t>& image, uint32_t width, uint32_t height)
    {
        const uint32_t stride = (width * 3 + 3) & ~3u, size = 54 + stride * height;
        std::vector<uint8_t> bmp(size, 0);
        auto put = [&](size_t at, uint32_t v, int n) { for (int i = 0; i < n; ++i) bmp[at + i] = static_cast<uint8_t>(v >> (8 * i)); };
        bmp[0] = 'B'; bmp[1] = 'M'; put(2, size, 4); put(10, 54, 4); put(14, 40, 4); put(18, width, 4); put(22, height, 4);
        put(26, 1, 2); put(28, 24, 2); put(34, stride * height, 4);
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x)
            {
                const uint32_t px = image[static_cast<size_t>(height - 1 - y) * width + x];
                const size_t at = 54 + static_cast<size_t>(y) * stride + x * 3;
                bmp[at] = px & 255; bmp[at + 1] = (px >> 8) & 255; bmp[at + 2] = (px >> 16) & 255;
            }
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bmp.data()), bmp.size());
    }

    void WarpChecks(const Pick& m2, const Pick* wmo, const char* texturesEnv, const char* sheetsEnv, const char* scaleSheetsEnv)
    {
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { Check(false, "WARP: " + why); return; }
        IDirect3DPixelShader9* ps = nullptr;
        IDirect3DVertexShader9* vs = nullptr;
        if (FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(m2.code.data()), &ps)) || !BuildVs(w.dev, m2, &vs))
        { Check(false, "WARP: the patched M2 variant and its test vertex shader"); Release(ps); Release(vs); return; }
        Target t;
        if (!t.Create(w.dev, 96, 128, D3DFMT_A32B32G32R32F)) { Check(false, "WARP: a float target"); Release(ps); Release(vs); return; }
        const float whole[4] = { 0, 0, 1, 1 };
        const cr::Material* violet = cr::MaterialAt(cr::MaterialOf("jlo_draenei_crystal01"));
        if (!violet) { Check(false, "WARP: jlo_draenei_crystal01's row"); Release(ps); Release(vs); return; }
        IDirect3DTexture9* tex = Solid(w.dev, 0xFF8A6FD8u);   // a violet crystal texel
        Scene s;
        s.base = tex;
        s.material = violet;
        std::vector<float> stock, px;
        // 5a. a mask that keeps nothing: exactly the stock pixels.
        View head;
        s.on = false;
        bool drew = Draw(w, m2, ps, vs, t.rt, t.ds, t.width, t.height, head, s, whole) && t.Read(w.dev, stock);
        const float cutAll[4] = { 0, 0, 0, 2.0f };
        s.on = true; s.overrideC78 = cutAll;
        drew = drew && Draw(w, m2, ps, vs, t.rt, t.ds, t.width, t.height, head, s, whole) && t.Read(w.dev, px);
        s.overrideC78 = nullptr;
        const Stats st = Measure(stock, s.fog);
        Check(drew && st.pixels > 1000 && px == stock, "a mask that keeps nothing gives exactly the stock pixels (" + std::to_string(st.pixels) + " crystal pixels)");
        // ... and the look itself changes them.
        drew = Draw(w, m2, ps, vs, t.rt, t.ds, t.width, t.height, head, s, whole) && t.Read(w.dev, px);
        Check(drew && px != stock, "the crystal look changes them");
        // 5b. no NaN or Inf: texels black, white, saturated; day and night; head on, from above, close and far; the
        // largest constants the table allows (Resolve clamps), a mask on a black texel.
        int bad = 0, draws = 0;
        cr::Material extreme = *violet;
        extreme.weight = 1.0f; extreme.deepen = 1.0f; extreme.rim = 1.0f; extreme.facetYards = 0.2f; extreme.dayGlow = 1.0f; extreme.nightGlow = 1.0f;
        for (int k = 0; k < 3; ++k) { extreme.tint[k] = 1.5f; extreme.glow[k] = 2.0f; }
        extreme.mask[0] = 20.0f; extreme.mask[1] = -20.0f; extreme.mask[2] = 20.0f; extreme.mask[3] = -20.0f;
        cr::Settings strong;
        strong.glow = 3.0f;
        for (uint32_t texel : { 0xFF000000u, 0xFFFFFFFFu, 0xFFFF0000u, 0xFF00FF40u, 0xFF8A6FD8u })
            for (int lightIndex = 0; lightIndex < 2; ++lightIndex)
                for (const View& v : { View{ 8, 30, 7 }, View{ 70, 15, 7 }, View{ 89, 0, 7 }, View{ 0, 0, 1.6f }, View{ 20, 45, 90 } })
                    for (const cr::Material* m : { violet, static_cast<const cr::Material*>(&extreme) })
                    {
                        IDirect3DTexture9* tt = Solid(w.dev, texel);
                        Scene e;
                        e.base = tt; e.on = true; e.material = m; e.night = static_cast<float>(lightIndex); e.unlit = lightIndex == 0;
                        e.settings = m == violet ? cr::Settings{} : strong;
                        if (lightIndex) { e.fog[0] = 0.08f; e.fog[1] = 0.1f; e.fog[2] = 0.18f; }
                        if (Draw(w, m2, ps, vs, t.rt, t.ds, t.width, t.height, v, e, whole) && t.Read(w.dev, px)) { bad += Measure(px, e.fog).bad; ++draws; }
                        Release(tt);
                    }
        Check(draws == 100 && bad == 0, "no NaN or Inf: " + std::to_string(draws) + " draws (black, white and saturated texels, day and night, head on, from above, "
                                        "edge-on, close, 90 yd; the strongest constants), " + std::to_string(bad) + " bad pixels");
        // 5c. the glow: in the same night light (a lit material), stronger by the night factor than by day's, and none without.
        {
            Scene n;
            n.base = tex; n.on = true; n.material = violet; n.unlit = false; n.night = 1.0f;
            n.fog[0] = 0.08f; n.fog[1] = 0.1f; n.fog[2] = 0.18f;
            std::vector<float> atNight, byDay, none;
            const bool a = Draw(w, m2, ps, vs, t.rt, t.ds, t.width, t.height, head, n, whole) && t.Read(w.dev, atNight);
            n.glowNight = 0.0f;
            const bool b = Draw(w, m2, ps, vs, t.rt, t.ds, t.width, t.height, head, n, whole) && t.Read(w.dev, byDay);
            n.settings.glow = 0.0f;
            const bool c = Draw(w, m2, ps, vs, t.rt, t.ds, t.width, t.height, head, n, whole) && t.Read(w.dev, none);
            const Stats sn = Measure(atNight, n.fog), sd = Measure(byDay, n.fog), s0 = Measure(none, n.fog);
            char line[220];
            std::snprintf(line, sizeof(line), "the inner glow, in the night's light: mean luminance %.3f with the night's glow, %.3f with the day's, %.3f with none",
                          sn.luma, sd.luma, s0.luma);
            Check(a && b && c && sn.luma > sd.luma * 1.1 && sd.luma > s0.luma, line);
        }
        // 5d. the deepening: the crystal's middle (face on) darker and more saturated with it than without.
        {
            cr::Material flat = *violet;
            flat.deepen = 0.0f;
            Scene d;
            d.base = Solid(w.dev, 0xFFB0A0E8u); d.on = true; d.material = violet;
            std::vector<float> deep, plain;
            const bool a = Draw(w, m2, ps, vs, t.rt, t.ds, t.width, t.height, View{ 5, 0, 7 }, d, whole) && t.Read(w.dev, deep);
            d.material = &flat;
            const bool b = Draw(w, m2, ps, vs, t.rt, t.ds, t.width, t.height, View{ 5, 0, 7 }, d, whole) && t.Read(w.dev, plain);
            // the middle third of the image's columns, the body's rows
            std::vector<bool> middle(static_cast<size_t>(t.width) * t.height, false);
            for (UINT y = t.height / 3; y < 2 * t.height / 3; ++y)
                for (UINT x = t.width * 2 / 5; x < t.width * 3 / 5; ++x) middle[static_cast<size_t>(y) * t.width + x] = true;
            const Stats sa = Measure(deep, d.fog, &middle), sb = Measure(plain, d.fog, &middle);
            char line[220];
            std::snprintf(line, sizeof(line), "the core deeper face on: luminance %.3f (without %.3f), saturation %.3f (without %.3f)", sa.luma, sb.luma, sa.saturation, sb.saturation);
            Check(a && b && sa.pixels > 100 && sa.luma < sb.luma && sa.saturation > sb.saturation, line);
            Release(d.base);
        }
        // 5e. a WMO variant (the buildings' block, with the rims and the glass blocks off): no NaN either.
        if (wmo)
        {
            IDirect3DPixelShader9* wps = nullptr;
            IDirect3DVertexShader9* wvs = nullptr;
            int wbad = 0, wdraws = 0;
            if (SUCCEEDED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(wmo->code.data()), &wps)) && BuildVs(w.dev, *wmo, &wvs))
                for (const View& v : { View{ 8, 30, 7 }, View{ 89, 0, 7 }, View{ 0, 0, 1.6f } })
                    for (int night = 0; night < 2; ++night)
                    {
                        Scene e;
                        e.base = tex; e.on = true; e.material = &extreme; e.settings = strong; e.night = static_cast<float>(night);
                        if (Draw(w, *wmo, wps, wvs, t.rt, t.ds, t.width, t.height, v, e, whole) && t.Read(w.dev, px)) { wbad += Measure(px, e.fog).bad; ++wdraws; }
                    }
            Check(wdraws == 6 && wbad == 0, "a WMO variant with the crystal look: no NaN or Inf (" + std::to_string(wdraws) + " draws)");
            Release(wps); Release(wvs);
        }
        // 5f. any crystal size and any CrystalCellScale (6): no NaN or Inf either, the M2 variant and the WMO one.
        {
            int fbad = 0, fdraws = 0, want = 0;
            const float sizes[] = { 0.0f, 0.5f, 21.5f, 159.0f, 1e30f, std::numeric_limits<float>::quiet_NaN(), -3.0f };
            const float settings[] = { 0.25f, 1.0f, 4.0f, std::numeric_limits<float>::quiet_NaN() };
            const View views[] = { View{ 8, 30, 7 }, View{ 0, 0, 1.6f } };
            for (float size : sizes)
                for (float setting : settings)
                    for (const View& v : views)
                    {
                        Scene e;
                        e.base = tex; e.on = true; e.material = &extreme; e.settings = strong; e.settings.cellScale = setting; e.size = size;
                        ++want;
                        if (Draw(w, m2, ps, vs, t.rt, t.ds, t.width, t.height, v, e, whole) && t.Read(w.dev, px)) { fbad += Measure(px, e.fog).bad; ++fdraws; }
                    }
            IDirect3DPixelShader9* wps = nullptr;
            IDirect3DVertexShader9* wvs = nullptr;
            if (wmo && SUCCEEDED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(wmo->code.data()), &wps)) && BuildVs(w.dev, *wmo, &wvs))
                for (float size : { 159.0f, 1e30f })
                    for (const View& v : views)
                    {
                        Scene e;
                        e.base = tex; e.on = true; e.material = &extreme; e.settings = strong; e.settings.cellScale = 4.0f; e.size = size;
                        ++want;
                        if (Draw(w, *wmo, wps, wvs, t.rt, t.ds, t.width, t.height, v, e, whole) && t.Read(w.dev, px)) { fbad += Measure(px, e.fog).bad; ++fdraws; }
                    }
            Release(wps); Release(wvs);
            Check(fdraws == want && want == 60 && fbad == 0, "no NaN or Inf at any crystal size (0 to 1e30 yd, not a number, negative) or CrystalCellScale (0.25 to 4, "
                                                             "not a number): " + std::to_string(fdraws) + " draws, " + std::to_string(fbad) + " bad pixels");
        }
        Release(tex);

        // Contact sheets.
        if (texturesEnv && sheetsEnv)
        {
            struct Entry { const char* texture; bool unlit; float uv[4]; };
            const Entry entries[] = {
                { "jg_draenei_crystal04_edit2", true, { 0, 0, 1, 1 } },      // the Exodar's crystals
                { "jlo_draenei_crystal01", true, { 0, 0, 1, 1 } },           // Azuremyst's Silvermyst crystals
                { "jlo_draenei_crystal02", false, { 0, 0, 1, 1 } },          // a draenei building's crystal (lit)
                { "bloodmyst_crystal_001", true, { 0, 0, 1, 1 } },           // Bloodmyst
                { "hellfirecrystal01", true, { 0, 0, 1, 0.62f } },           // Blade's Edge, Hellfire, Shadowmoon (fel green)
                { "netherstorm_rock_gems_03", false, { 0, 0, 1, 1 } },       // Netherstorm's gems in their rock
                { "shattrath_crystal_orig", true, { 0, 0, 1, 1 } },          // Shattrath
                { "crystalsong_magictrunk01b", false, { 0, 0, 1, 1 } },      // Crystalsong
                { "maraudondiamond01", true, { 0, 0, 1, 1 } },               // Silvermoon's and Karazhan's diamonds (mana blue)
                { "maraudoncrystalpurple02", true, { 0, 0, 1, 1 } },         // amethysts (Maraudon, the Exodar, Silvermoon)
                { "terrokarfoliage01", true, { 0.5f, 0, 0.75f, 0.25f } },    // Terokkar's crystals (the atlas's crystal corner)
                { "ungorocrystalblue", true, { 0, 0, 1, 1 } },               // Un'Goro (prism)
                { "jg_draenei_crystal04_edit2_yellow", true, { 0, 0, 1, 1 } },// the Exodar's yellow crystals (naaru gold)
                { "sholazar_deep_red", false, { 0, 0, 1, 1 } },              // Sholazar's geodes (ruby)
            };
            constexpr UINT kW = 200, kH = 260;
            Target sheet;
            if (!sheet.Create(w.dev, kW, kH, D3DFMT_A8R8G8B8)) { Check(false, "sheet target"); Release(ps); Release(vs); return; }
            int written = 0, sheetBad = 0;
            const fs::path folder(texturesEnv), outFolder(sheetsEnv);
            fs::create_directories(outFolder);
            for (const Entry& en : entries)
            {
                IDirect3DTexture9* base = LoadTexture(w.dev, folder, en.texture);
                const cr::Material* m = cr::MaterialAt(cr::MaterialOf(en.texture));
                if (!base || !m) { std::printf("      sheet: %s %s\n", en.texture, base ? "has no row" : "not found"); Release(base); continue; }
                std::vector<uint32_t> image(static_cast<size_t>(kW) * 4 * kH * 2);
                for (int row = 0; row < 2; ++row)
                    for (int col = 0; col < 4; ++col)
                    {
                        Scene e;
                        e.base = base; e.material = m; e.unlit = en.unlit;
                        e.on = (col % 2) == 1;
                        e.night = col >= 2 ? 1.0f : 0.0f;
                        if (col >= 2) { e.fog[0] = 0.07f; e.fog[1] = 0.09f; e.fog[2] = 0.16f; }
                        const View v = row == 0 ? View{ 6, 30, 6.5f } : View{ 72, 12, 6.5f };
                        std::vector<float> got;
                        if (!Draw(w, m2, ps, vs, sheet.rt, sheet.ds, kW, kH, v, e, en.uv) || !sheet.Read(w.dev, got)) continue;
                        sheetBad += Measure(got, e.fog).bad;
                        for (UINT y = 0; y < kH; ++y)
                            for (UINT x = 0; x < kW; ++x)
                            {
                                const float* c = &got[(static_cast<size_t>(y) * kW + x) * 4];
                                auto b8 = [](float f) { return static_cast<uint32_t>(std::lround(std::clamp(std::isfinite(f) ? f : 1.0f, 0.0f, 1.0f) * 255.0f)); };
                                image[(static_cast<size_t>(row) * kH + y) * kW * 4 + col * kW + x] = (b8(c[0]) << 16) | (b8(c[1]) << 8) | b8(c[2]);
                            }
                    }
                WriteBmp(outFolder / (std::string("crystal_") + en.texture + ".bmp"), image, kW * 4, kH * 2);
                ++written;
                Release(base);
            }
            Check(written >= 12 && sheetBad == 0, "contact sheets: " + std::to_string(written) + " textures written to " + outFolder.string() +
                                                  " (columns day stock, day crystal, night stock, night crystal; rows head on, from above)");
        }

        // The facets by size: a shard, a medium crystal (the Exodar's big M2 ones) and a giant (the Exodar's WMO crystals),
        // each whole and from close by (the giant from a walkway 20 yd off its face); columns stock, before (the row's
        // facets at any size), after (by the crystal's PieceRadius, as the game works it out). By day.
        if (texturesEnv && scaleSheetsEnv)
        {
            struct Case { const char* name; const char* texture; bool wmoVariant; bool unlit; float scale; };
            const Case cases[] = {
                { "shard", "jlo_draenei_crystal01", false, true, 0.35f },       // Azuremyst's Silvermyst crystals' texture
                { "medium", "jg_draenei_crystal04_edit2", false, true, 8.5f },  // the Exodar's big M2 crystals (21.5 yd)
                { "giant", "jlo_draenei_crystal02", true, false, 45.0f },       // the Exodar's WMO crystals (105-159 yd)
            };
            IDirect3DPixelShader9* wps = nullptr;
            IDirect3DVertexShader9* wvs = nullptr;
            const bool haveWmo = wmo && SUCCEEDED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(wmo->code.data()), &wps)) && BuildVs(w.dev, *wmo, &wvs);
            constexpr UINT kW = 240, kH = 300;
            Target sheet;
            const fs::path folder(texturesEnv), outFolder(scaleSheetsEnv);
            fs::create_directories(outFolder);
            int written = 0, sheetBad = 0;
            std::string sizes;
            if (sheet.Create(w.dev, kW, kH, D3DFMT_A8R8G8B8))
                for (const Case& k : cases)
                {
                    if (k.wmoVariant && !haveWmo) continue;
                    IDirect3DTexture9* base = LoadTexture(w.dev, folder, k.texture);
                    const cr::Material* m = cr::MaterialAt(cr::MaterialOf(k.texture));
                    if (!base || !m) { std::printf("      scale sheet: %s %s\n", k.texture, base ? "has no row" : "not found"); Release(base); continue; }
                    const float H = 3.0f * k.scale, R = k.scale, farPlane = std::max(100.0f, 20.0f * k.scale), tile = std::max(1.0f, H / 12.0f);
                    const View whole2{ 6, 30, 6.5f * k.scale, k.scale, 1.6f * k.scale, farPlane, tile };
                    const View close{ 5, 30, 0.87f * R + std::clamp(0.25f * H, 1.0f, 20.0f), k.scale, std::min(0.5f * H, 20.0f), farPlane, tile };
                    const std::vector<float> corners = CrystalCorners(whole2);
                    const float size = cr::PieceRadius(corners.data(), corners.size() / 3);
                    sizes += std::string(k.name) + Fmt(" %.2f yd (facets x%.2f, %.1f yd a repeat); ", size, cr::CellScale(size), m->facetYards * cr::CellScale(size));
                    std::vector<uint32_t> image(static_cast<size_t>(kW) * 3 * kH * 2);
                    for (int row = 0; row < 2; ++row)
                        for (int col = 0; col < 3; ++col)
                        {
                            Scene e;
                            e.base = base; e.material = m; e.unlit = k.unlit;
                            e.on = col > 0;
                            e.size = col == 2 ? size : 0.0f;
                            std::vector<float> got;
                            const Pick& pick = k.wmoVariant ? *wmo : m2;
                            if (!Draw(w, pick, k.wmoVariant ? wps : ps, k.wmoVariant ? wvs : vs, sheet.rt, sheet.ds, kW, kH, row == 0 ? whole2 : close, e, whole) ||
                                !sheet.Read(w.dev, got))
                                continue;
                            sheetBad += Measure(got, e.fog).bad;
                            for (UINT y = 0; y < kH; ++y)
                                for (UINT x = 0; x < kW; ++x)
                                {
                                    const float* c = &got[(static_cast<size_t>(y) * kW + x) * 4];
                                    auto b8 = [](float f) { return static_cast<uint32_t>(std::lround(std::clamp(std::isfinite(f) ? f : 1.0f, 0.0f, 1.0f) * 255.0f)); };
                                    image[(static_cast<size_t>(row) * kH + y) * kW * 3 + col * kW + x] = (b8(c[0]) << 16) | (b8(c[1]) << 8) | b8(c[2]);
                                }
                        }
                    WriteBmp(outFolder / (std::string("scale_") + k.name + ".bmp"), image, kW * 3, kH * 2);
                    ++written;
                    Release(base);
                }
            Release(wps); Release(wvs);
            Check(written == 3 && sheetBad == 0, "facet-size sheets: " + std::to_string(written) + " written to " + outFolder.string() +
                                                 " (columns stock, before, after; rows whole, close by): " + sizes);
        }
        Release(ps);
        Release(vs);
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> args;
    bool warp = true;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--no-warp") warp = false;
        else args.push_back(argv[i]);
    }
    if (args.size() < 3)
    {
        std::printf("usage: crystal_tests <M2 shader folder> <WMO shader folder> <repo data folder> [<survey folder> [<WMO groups folder>]] [--no-warp]\n");
        return 2;
    }

    // 1, 2.
    std::vector<cr::Material> raw;
    std::string why;
    const bool read = ReadTable(fs::path(args[2]) / "CrystalMaterials.cdbc", raw, why);
    Check(read, "CrystalMaterials.cdbc reads back" + (read ? std::string() : ": " + why));
    if (read) { TableChecks(raw); ClassifyChecks(); }
    // 3.
    if (args.size() >= 4 && read) SurveyChecks(args[3]);
    // 6 (but WARP's).
    if (read) SizeChecks();
    if (args.size() >= 4)
    {
        const fs::path groups = args.size() >= 5 ? fs::path(args[4]) : fs::path();
        GeometryChecks(args[3], args.size() >= 5 ? &groups : nullptr);
    }

    // 4.
    const std::vector<Blob> models = LoadBlobs(args[0]), wmos = LoadBlobs(args[1]);
    std::map<std::string, Pick> picks;
    const EditResult m2 = EditAll(models, &mi::EditPixelForTests, false, &picks, "ps_Combiners_Opaque_");
    Check(models.size() == 368 && m2.shadow == 276 && m2.patched == m2.shadow && m2.crystal == m2.patched && m2.assembled == m2.patched,
          "every M2 shadow variant takes the block with the crystal code and assembles: " + std::to_string(m2.assembled) + " of " + std::to_string(m2.shadow) +
          " (" + std::to_string(models.size()) + " stock)");
    Check(m2.maxSlots > 0 && m2.maxSlots <= 512, "largest M2 variant: " + std::to_string(m2.maxSlots) + " of 512 instruction slots (" + m2.maxName + ")");
    const EditResult m2d = EditAll(models, &mi::EditPixelForTests, true, nullptr, nullptr);
    Check(m2d.assembled == m2d.patched && m2d.maxSlots > 0 && m2d.maxSlots <= 512,
          "with the doodads' dissolve after it: largest " + std::to_string(m2d.maxSlots) + " of 512 instruction slots (" + m2d.maxName + ")");
    std::map<std::string, Pick> wpicks;
    const EditResult wr = EditAll(wmos, &mi::EditPixelWmoForTests, false, &wpicks, "ps_MapObjOpaque_");
    Check(wmos.size() == 112 && wr.shadow == 84 && wr.patched == wr.shadow && wr.crystal == wr.patched && wr.assembled == wr.patched &&
          wr.maxSlots > 0 && static_cast<unsigned>(wr.maxSlots) <= mi::kRingsMinSlots,
          "every WMO shadow variant with the rims: " + std::to_string(wr.assembled) + " of " + std::to_string(wr.shadow) + ", largest " +
          std::to_string(wr.maxSlots) + " of " + std::to_string(mi::kRingsMinSlots) + " slots (" + wr.maxName + ")");
    const EditResult wg = EditAll(wmos, &mi::EditPixelWmoGlassForTests, false, nullptr, nullptr);
    Check(wg.patched == wg.shadow && wg.crystal == wg.patched && wg.assembled == wg.patched && wg.maxSlots > 0 && static_cast<unsigned>(wg.maxSlots) <= mi::kGlassMinSlots,
          "every WMO shadow variant with the rims and the glass blocks: largest " + std::to_string(wg.maxSlots) + " of " + std::to_string(mi::kGlassMinSlots) +
          " slots (" + wg.maxName + ")");

    // 5.
    if (warp && read)
    {
        if (!picks.count("ps_Combiners_Opaque_")) Check(false, "a one-texture M2 shadow variant to draw with");
        else WarpChecks(picks["ps_Combiners_Opaque_"], wpicks.count("ps_MapObjOpaque_") ? &wpicks["ps_MapObjOpaque_"] : nullptr,
                        std::getenv("CRYSTAL_TEXTURES"), std::getenv("CRYSTAL_SHEETS"), std::getenv("CRYSTAL_SCALE_SHEETS"));
    }
    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}
