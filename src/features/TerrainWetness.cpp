#include "TerrainWetness.hpp"

#include "TerrainIce.hpp"
#include "TerrainLights.hpp"
#include "WetSurfaces.hpp"
#include "../render/MoistureTexture.hpp"
#include "../render/ShaderPatch.hpp"

#include <cmath>
#include <cstdio>
#include <regex>
#include <string>

namespace wxl_livingazeroth::terrainwet
{
    namespace
    {
        // The client's terrain shader tables (filled by the terrain shader load 0x79E7C0 through the
        // set loader 0x6AA130, which fills a table before creating any of its shaders; layout from
        // debug/ShaderDump, verified 2026-10-02/03).
        constexpr uintptr_t kTerrainVs = 0x00CE0008;  // 0x80 "Terrain"
        const std::vector<std::pair<uintptr_t, int>> kTerrainPs = {
            { 0x00CE0488, 3 },    // Terrain0
            { 0x00CE0004, 1 },    // Terrain0_env
            { 0x00CE0408, 0x20 }, // Terrain1 / Terrain1w (overlapping tables)
            { 0x00CE0388, 0x20 }, // Terrain2 / Terrain2_pcf
            { 0x00CE0208, 0x60 }, // Terrain3 / Terrain3_pcf
        };

        // Registers the patch uses, all free in every stock variant (from the dumps: PS inputs up to
        // v8, temps up to r7, constants up to c13, samplers up to s8): PS v9, r29..r31, c200..c202, s11.
        // Wet surfaces (features/WetSurfaces) add r1..r6 (the stock's temps, dead by the fog ending: only the
        // colour, r0, is read after it), c160..c167 and s15.
        // The world XY rides as texcoord7 (.xy), the PS reads v9.xy. Where the VS has an output register
        // left (vs_3_0 allows 11 without point size), it gets its own. The 32 variants using all 11
        // declare texcoord3 as their view-space position (.xyz, `mov oN.xyz, r0`), which no terrain
        // pixel shader reads (from the dumps: the only PS reading texcoord3 read it .xy, a layer UV,
        // paired with the VS that write it that way) -- that output is re-labelled texcoord7 and
        // carries the world XY instead. Packing a second semantic into a register's free .zw does NOT
        // work (in-client 2026-10-04: the PS read zeros, whichever components it declared).
        // c200 = (1 / grid extent, 0, beyond everything, strength), c201 = grid box (world yd), c203 = (0, 0, 0,
        // 1 / edge fade yards), c204 = (1 / far map extent, 0, far map on, 0), c205 = the far map's box (world yd);
        // c202 is defined in the shader. Inside its last kEdgeFadeYards the grid fades into the one
        // regional value used beyond it, so its edge doesn't show. The debug views (whole grid wet, whole terrain,
        // stripes) were in the shader until 2026-10-25; they are a debug copy of the texture now (moisturetex::BindDebug,
        // the box made huge for "whole terrain"), which gave wet surfaces 9 of the 512 slots back.
        constexpr int kPsConstant = 200;
        // A scorch mark (SetScorch): c223 = (x, y, 1 / radius, strength), the last constant ps_3_0 has (the
        // light patch holds c206..c214, the ice c215..c222). Set at every terrain stage, zeros when none.
        constexpr int kScorchConstant = 223;
        float g_scorch[4] = {};

        bool  g_enabled = true;
        unsigned g_packed = 0; // vertex variants whose unread view-position output carries it (all 11 outputs in use)
        int   g_debug = 0; // 0 off, 1 whole grid, 2 whole terrain, 3 10 yd stripes
        float g_strength = 0.45f;
        std::string g_status;

        std::string::size_type FirstInstruction(const std::string& src)
        {
            // After the version line, the first line that isn't a declaration, a definition or empty.
            std::string::size_type at = src.find('\n');
            while (at != std::string::npos && at + 1 < src.size())
            {
                const std::string::size_type end = src.find('\n', at + 1);
                std::string line = src.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
                const auto first = line.find_first_not_of(" \t");
                if (first != std::string::npos)
                {
                    line = line.substr(first);
                    if (line.rfind("dcl", 0) != 0 && line.rfind("def", 0) != 0 && line.rfind("//", 0) != 0) return at + 1;
                }
                at = end;
            }
            return std::string::npos;
        }

        bool EditVertex(std::string& src, std::string& why)
        {
            if (src.find("vs_3_0") == std::string::npos) { why = "not vs_3_0"; return false; }
            if (src.find("dcl_position v0") == std::string::npos) { why = "no position input"; return false; }
            if (src.find("dcl_texcoord7") != std::string::npos) { why = "texcoord7 in use"; return false; }
            const auto at = FirstInstruction(src);
            if (at == std::string::npos) { why = "no instructions"; return false; }
            // Output registers in use.
            static const std::regex outputDcl(R"(dcl_\w+ o(\d+))");
            int highest = -1, declared = 0;
            for (auto it = std::sregex_iterator(src.begin(), src.end(), outputDcl); it != std::sregex_iterator(); ++it)
            {
                ++declared;
                const int n = std::stoi((*it)[1].str());
                highest = n > highest ? n : highest;
            }
            // The world position: .xy for the moisture, .z too for the point lights (features/TerrainLights).
            if (declared < 11)
            {
                const std::string o = "o" + std::to_string(highest + 1);
                src.insert(at, "    mov " + o + ".xyz, v0.xyz\n");
                src.insert(src.find('\n') + 1, "    dcl_texcoord7 " + o + ".xyz\n");
                terrainlights::EditVertex(src); // the stock point lights through a switch
                return true;
            }
            // All outputs in use: take over the unread view-position output (texcoord3 .xyz).
            static const std::regex viewDcl(R"(dcl_texcoord3 (o\d+)\.xyz\b)");
            std::smatch m;
            if (!std::regex_search(src, m, viewDcl)) { why = "all outputs in use and no view-position output to take over"; return false; }
            const std::string o = m[1].str();
            const std::regex viewWrite("\n([ \t]*)mov " + o + R"(\.xyz, r\d+[ \t]*\n)");
            std::smatch w;
            if (!std::regex_search(src, w, viewWrite)) { why = "view-position output written unexpectedly"; return false; }
            src.replace(static_cast<size_t>(w.position(0)), static_cast<size_t>(w.length(0)), "\n" + w[1].str() + "mov " + o + ".xyz, v0.xyz\n");
            src.replace(static_cast<size_t>(m.position(0)), static_cast<size_t>(m.length(0)), "dcl_texcoord7 " + o + ".xyz");
            terrainlights::EditVertex(src);
            ++g_packed;
            return true;
        }

        bool EditPixel(std::string& src, std::string& why)
        {
            if (src.find("ps_3_0") == std::string::npos) { why = "not ps_3_0"; return false; }
            if (src.find(" v9") != std::string::npos || src.find(" s11") != std::string::npos || src.find(" s12") != std::string::npos ||
                src.find(" s13") != std::string::npos || src.find("c200") != std::string::npos || src.find(" i0") != std::string::npos ||
                src.find(" s15") != std::string::npos || src.find("c160") != std::string::npos || src.find("c161") != std::string::npos)
            { why = "v9/s11/s12/s13/s15/c160/c161/c200/i0 in use"; return false; }
            // Clear ice (features/TerrainIce): the variant's layer blend, copied right after its sample
            // (before the searches below, which take positions in the edited text).
            terrainice::Variant ice;
            std::string iceWhy;
            const bool withIce = terrainice::Prepare(src, ice, iceWhy);
            // The stock ending: the colour minus the fog colour, then lerped toward it by the fog.
            static const std::regex fog(R"((\n[ \t]*)add (r\d+)\.xyz, \2, -c2[ \t]*\n[ \t]*mad oC0\.xyz, v\d+\.x, \2, c2)");
            std::smatch m;
            if (!std::regex_search(src, m, fog)) { why = "no fog ending"; return false; }
            const std::string r = m[2].str();
            // Wet surfaces (features/WetSurfaces): with the layers' weights (the ice's alpha copy), r25.z = the pixel's
            // share of shine (c160 per chunk), and the pieces it adds below; none without them. The stock's temps
            // r1..r9 must be dead here: only the colour register may be read after the fog ending.
            const bool withWet = withIce && r == "r0" && wetsurfaces::PiecesOn();
            const wetsurfaces::TerrainPieces wet = withWet ? wetsurfaces::TerrainShader(r) : wetsurfaces::TerrainPieces{};
            wetsurfaces::NoteVariant(withWet, !withIce ? "no layer weights (no ice code): " + iceWhy : (r != "r0" ? "the colour isn't r0" : "left out (tests)"));
            // The ice weight first (r25), then the wet look (none on ice), then the ice look.
            const std::string block = (withIce ? terrainice::WeightBlock(ice, withWet) : std::string()) +
                "\n    add r31.xy, v9.xy, -c201.xy"
                "\n    add r31.zw, -v9.xyxy, c201.zwzw"
                "\n    min r31.xy, r31.xy, r31.zw"
                "\n    min r31.x, r31.x, r31.y"
                "\n    mul_sat r31.x, r31.x, c203.w"
                "\n    mul r29.xy, v9.xy, c200.x"
                "\n    texld r30, r29, s11"
                // Beyond the near grid: the far map (per chunk, its zone's value) inside its box, the
                // single fallback value outside it.
                "\n    mul r27.xy, v9.xy, c204.x"
                "\n    texld r27, r27, s12"
                "\n    add r26.xy, v9.xy, -c205.xy"
                "\n    add r26.zw, -v9.xyxy, c205.zwzw"
                "\n    min r26.xy, r26.xy, r26.zw"
                "\n    min r26.x, r26.x, r26.y"
                "\n    cmp r26.x, r26.x, c202.x, c202.y"
                "\n    mul r26.x, r26.x, c204.z"
                "\n    add r27.w, r27.w, -c200.z"
                "\n    mad r27.w, r26.x, r27.w, c200.z"
                "\n    add r30.w, r30.w, -r27.w"
                "\n    mad r30.w, r31.x, r30.w, r27.w"
                "\n    mul r30.x, r30.w, c200.w" +
                // The surface's wetness (r1.x) and puddle (r1.y); its darkening joins the soil's.
                wet.texel +
                (withIce ? terrainice::WetScale() : std::string()) +
                "\n    mad " + r + ".xyz, " + r + ", -r30.x, " + r +
                (withIce ? terrainice::LookBlock(r) : std::string());
            // Then the point lights (features/TerrainLights), on the wet colour; wet surfaces' highlights of them and of
            // the sun, and the sky in the water, with them.
            std::string lightDefs;
            const std::string lightBlock = terrainlights::PixelBlock(r, lightDefs, withWet ? &wet : nullptr);
            // Last, before the fog: a scorch mark (c223, SetScorch) darkens the lit colour in a soft disc,
            // (1 - d^2)^2 of the way to strength at its centre; strength 0 leaves the colour exactly.
            const std::string scorchBlock =
                "\n    add r28.xy, v9.xy, -c223.xy"
                "\n    mul r28.xy, r28.xy, c223.z"
                "\n    dp2add r28.x, r28.xy, r28.xy, c202.y"
                "\n    add_sat r28.x, c202.x, -r28.x"
                "\n    mul r28.x, r28.x, r28.x"
                "\n    mul r28.x, r28.x, c223.w"
                "\n    mad " + r + ".xyz, " + r + ", -r28.x, " + r;
            src.insert(static_cast<size_t>(m.position(0)), block + lightBlock + scorchBlock);
            src.insert(src.find('\n') + 1, "    def c202, 1, 0, 0.5, 0\n" + lightDefs + (withIce ? terrainice::Definitions() : std::string()) +
                                            "    dcl_texcoord7 v9.xyz\n    dcl_2d s11\n    dcl_2d s12\n" + wet.declarations);
            terrainice::NoteVariant(withIce, iceWhy);
            return true;
        }

        const shaderpatch::RuleStatus* Find(const std::vector<shaderpatch::RuleStatus>& all, const char* name)
        {
            for (const auto& s : all) if (s.name == name) return &s;
            return nullptr;
        }

        // Usable only when no terrain vertex shader failed: a patched pixel shader reads v9, which
        // only a patched vertex shader writes.
        bool PairOk(const std::vector<shaderpatch::RuleStatus>& all)
        {
            const shaderpatch::RuleStatus* vs = Find(all, "terrain wetness (vertex)");
            const shaderpatch::RuleStatus* ps = Find(all, "terrain wetness (pixel)");
            return vs && ps && vs->failed == 0 && vs->skipped == 0 && vs->applied > 0 && ps->applied > 0;
        }
    }

    bool EditPixelForTests(std::string& src, std::string& why) { return EditPixel(src, why); }

    void Register()
    {
        shaderpatch::Register(shaderpatch::TableRule{ "terrain wetness (vertex)", false, { { kTerrainVs, 0x80 } }, &EditVertex });
        shaderpatch::Register(shaderpatch::TableRule{ "terrain wetness (pixel)", true, kTerrainPs, &EditPixel });
    }

    void BeforeTerrainStage(IDirect3DDevice9* device)
    {
        if (!device) return;
        float c[16] = {};
        float inverseExtent = 0.0f, box[4] = {};
        // The moisture texture also carries the ground's normal for the point lights: bound whenever
        // the patch is active, wet look or not. The debug views bind a copy with its excess replaced.
        const bool mapped = PairOk(shaderpatch::Status()) && moisturetex::Mapping(inverseExtent, box);
        const bool bound = mapped && (g_debug ? moisturetex::BindDebug(device, 11, g_debug) : moisturetex::Bind(device, 11));
        const bool on = g_enabled && bound;
        c[0] = inverseExtent;
        c[3] = on ? (g_debug ? 0.8f : g_strength) : 0.0f;
        for (int k = 0; k < 4; ++k) c[4 + k] = box[k];
        if (g_debug == 2) { c[4] = c[5] = -1.0e9f; c[6] = c[7] = 1.0e9f; } // the whole terrain: no box edge anywhere
        c[8] = 1.0f; c[9] = 1.0f; c[10] = 0.0f; c[11] = 0.0f; // c202: the shader's own def wins
        c[2] = g_debug ? 0.0f : moisturetex::FarExcess();
        c[15] = 1.0f / moisturetex::kEdgeFadeYards;
        device->SetPixelShaderConstantF(kPsConstant, c, 2);
        device->SetPixelShaderConstantF(kPsConstant + 3, c + 12, 1);

        // The far map (per terrain chunk, its zone's value) on s12.
        float farConstants[8] = {};
        int farSize = 0;
        float outside = 0.0f;
        const bool farOn = on && !g_debug && moisturetex::FarMapping(farConstants[0], farConstants + 4, farSize, outside) && moisturetex::BindFarPixel(device, 12);
        farConstants[2] = farOn ? 1.0f : 0.0f;
        device->SetPixelShaderConstantF(kPsConstant + 4, farConstants, 2);

        // The scorch mark, or none (a constant left over from anything else must not darken the ground).
        const float none[4] = {};
        device->SetPixelShaderConstantF(kScorchConstant, bound && g_scorch[3] > 0.0f ? g_scorch : none, 1);

        // Wet surfaces: their texture on s15 and their constants (zeros, so nothing changes, when they're off).
        wetsurfaces::BeforeTerrainStage(device, bound);
    }

    bool  PatchActive()            { return PairOk(shaderpatch::Status()); }
    void  SetEnabled(bool enabled) { g_enabled = enabled; }
    void  SetDebug(int mode)       { g_debug = mode < 0 ? 0 : (mode > 3 ? 3 : mode); }
    int   Debug()                  { return g_debug; }
    bool  Enabled()                { return g_enabled; }
    void  SetStrength(float s)     { g_strength = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s); }
    float Strength()               { return g_strength; }

    void SetScorch(float x, float y, float radius, float strength)
    {
        const bool valid = std::isfinite(x) && std::isfinite(y) && radius > 0.01f && std::isfinite(strength) && strength > 0.0f;
        g_scorch[0] = valid ? x : 0.0f;
        g_scorch[1] = valid ? y : 0.0f;
        g_scorch[2] = valid ? 1.0f / radius : 0.0f;
        g_scorch[3] = valid ? (strength > 1.0f ? 1.0f : strength) : 0.0f;
    }

    const char* StatusLine()
    {
        const auto all = shaderpatch::Status();
        const shaderpatch::RuleStatus* vs = Find(all, "terrain wetness (vertex)");
        const shaderpatch::RuleStatus* ps = Find(all, "terrain wetness (pixel)");
        char line[320];
        std::snprintf(line, sizeof(line), "terrain shaders patched: vertex %u (%u reusing an output, skipped %u, failed %u), pixel %u (skipped %u, failed %u)%s%s%s",
                      vs ? vs->applied : 0, g_packed, vs ? vs->skipped : 0, vs ? vs->failed : 0,
                      ps ? ps->applied : 0, ps ? ps->skipped : 0, ps ? ps->failed : 0,
                      PairOk(all) ? "" : " -- not active",
                      (vs && !vs->lastError.empty()) ? ("; vs: " + vs->lastError).c_str() : "",
                      (ps && !ps->lastError.empty()) ? ("; ps: " + ps->lastError).c_str() : "");
        g_status = line;
        return g_status.c_str();
    }
}
