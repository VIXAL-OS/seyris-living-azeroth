#include "ModelIce.hpp"

#include "TerrainIce.hpp"
#include "../render/M2Effects.hpp"
#include "../render/ShaderPatch.hpp"

#include "game/Gx.hpp"

#include <d3d9.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <regex>
#include <unordered_map>

namespace wxl_livingazeroth::modelice
{
    namespace
    {
        namespace gx = wxl::game::gx;

        // The M2 batch draw's context (core offsets/engine/Gx.hpp): +0x60 the instance, whose +0x2C is
        // the shared model, whose +0x3C holds its path stem inline (offsets/game/M2.hpp M2Model).
        constexpr size_t kCtxInstance = 0x60, kInstShared = 0x2C, kSharedStem = 0x3C, kStemCapacity = 0x150 - 0x3C;
        constexpr DWORD  kSampler = 14;

        // Whole-model ice from the map scan (waterfall-scan ice_summary.py): placed M2s whose every batch
        // is ice. Left out: the icebreaker's paddlewheel, the Icethorn herb, and models where ice is
        // only part of the mesh (the sword in ice, the frozen dragons, the bone clutter).
        const char* const kIceModels[] = {
            "dragonblight\\dragonblight_shoreline_ice", "dragonblight\\dragonblight_iceberg", "dragonblight\\dragonblight_icecliff",
            "dragonblight\\dragonblight_icerock", "dragonblight\\dragonblight_iceshard", "dragonblight\\dragonblight_icearch",
            "ulduar\\ul_icewall",
            "nexus\\nexus_spike_a_ice", "nexus\\nexus_spike_b_ice", "nexus\\nexus_spike_c_ice", "nexus\\nexus_spike_d_ice",
            "nexus\\nexus_ice_conduit",
        };
        // Frozen waterfalls (Winterspring's, and Northrend's: Bor's Fall in the Storm Peaks and the smaller
        // falls), drawn in the frozen-fall style.
        const char* const kFrozenFalls[] = {
            "frozenwaterfalls\\frozenwaterfall", "waterfalls\\northrendfrozenwaterfall",
        };

        using DrawBatchFn = void(__fastcall*)(void* ctx, void* edx);
        using DrawDoodadFn = void(__fastcall*)(void* ctx, void* edx, void* elements, void* indices);
        DrawBatchFn  g_origBatch = nullptr;
        DrawDoodadFn g_origDoodad = nullptr;
        bool g_hookedBatch = false, g_hookedDoodad = false;

        bool g_enabled = true;
        unsigned g_patched = 0, g_skipped = 0;
        std::string g_skipReasons;
        unsigned g_iceDraws = 0;
        std::string g_status;

        struct Entry { std::string stem; Style style = Style::None; };
        std::unordered_map<const void*, Entry> g_models;

        Style StyleFor(const void* ctx)
        {
            if (!ctx) return Style::None;
            const auto* instance = *reinterpret_cast<const uint8_t* const*>(static_cast<const uint8_t*>(ctx) + kCtxInstance);
            if (!instance) return Style::None;
            const auto* shared = *reinterpret_cast<const uint8_t* const*>(instance + kInstShared);
            if (!shared) return Style::None;
            const char* stem = reinterpret_cast<const char*>(shared + kSharedStem);
            const size_t length = strnlen(stem, kStemCapacity);
            Entry& e = g_models[shared];
            if (e.stem.size() != length || std::memcmp(e.stem.data(), stem, length) != 0)
            {
                e.stem.assign(stem, length);
                e.style = StyleOf(e.stem.c_str());
            }
            if (g_models.size() > 8192) { const Style style = e.style; g_models.clear(); return style; }
            return e.style;
        }

        // Switches the block on for one batch (main thread, inside the M2 pass).
        bool Begin(void* ctx)
        {
            if (!g_enabled || g_patched == 0) return false;
            const Style style = StyleFor(ctx);
            if (style == Style::None) return false;
            auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice());
            if (!dev) return false;
            IDirect3DTexture9* detail = terrainice::DetailTexture(dev);
            if (!detail) return false;
            float look[4 * terrainice::kModelConstantCount];
            terrainice::FillModelConstants(look);
            float weight[4], more[4];
            StyleConstants(style, terrainice::Strength(), look, weight, more);
            dev->SetPixelShaderConstantF(kWeightConstant, weight, 1);
            dev->SetPixelShaderConstantF(kLookConstant, look, terrainice::kModelConstantCount);
            dev->SetPixelShaderConstantF(kStyleConstant, more, 1);
            dev->SetTexture(kSampler, detail);
            dev->SetSamplerState(kSampler, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(kSampler, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(kSampler, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(kSampler, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
            dev->SetSamplerState(kSampler, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
            dev->SetSamplerState(kSampler, D3DSAMP_SRGBTEXTURE, FALSE);
            const BOOL on = TRUE;
            dev->SetPixelShaderConstantB(kSwitch, &on, 1);
            ++g_iceDraws;
            return true;
        }

        void End()
        {
            if (auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice()))
            {
                const BOOL off = FALSE;
                dev->SetPixelShaderConstantB(kSwitch, &off, 1);
            }
        }

        void __fastcall hkBatch(void* ctx, void* edx)
        {
            const bool ice = Begin(ctx);
            g_origBatch(ctx, edx);
            if (ice) End();
        }

        void __fastcall hkDoodad(void* ctx, void* edx, void* elements, void* indices)
        {
            const bool ice = Begin(ctx);
            g_origDoodad(ctx, edx, elements, indices);
            if (ice) End();
        }

        // The ice block, view space: N, V; world-space normal, view direction and position (camera +
        // the view's rows) for the detail texture, sampled on three planes by the normal (two depths
        // below the surface along the view: parallax); then the terrain's look (deep body by clarity,
        // Fresnel sky reflection, sun glint) and r31 = weight x (ice - base) + weight x debug tint.
        // The batch's style (c179.yzw, zero for plain ice): the detail's world height scaled by 1 - y
        // (streaks along a fall), the body brightened by 1 + z, and w added to the Fresnel reflection.
        // More of it (c190, zero for plain ice): x moves the body from the deep ice colour, scaled by the
        // texture's brightness, to the texture's own colour (with y of the cloudy depth left on it), and
        // the cracks and bubbles are strengthened by 1 + z.
        // ps_3_0 reads at most one constant register per instruction; lrp's destination can't be its
        // first or third source.
        std::string Block(const std::string& vP, const std::string& vN)
        {
            return
                "\n    mov r31.xyz, c175.y"
                "\n    if b7"
                "\n      nrm r13.xyz, " + vN +
                "\n      mov r14.xyz, -" + vP +
                "\n      nrm r16.xyz, r14"
                "\n      mov r14.xyz, r16"
                "\n      dp3 r15.x, r13, c181"
                "\n      dp3 r15.y, r13, c182"
                "\n      dp3 r15.z, r13, c183"
                "\n      dp3 r16.x, r14, c181"
                "\n      dp3 r16.y, r14, c182"
                "\n      dp3 r16.z, r14, c183"
                "\n      dp3 r17.x, " + vP + ", c181"
                "\n      dp3 r17.y, " + vP + ", c182"
                "\n      dp3 r17.z, " + vP + ", c183"
                "\n      add r17.xyz, r17, c180"
                "\n      dp3 r18.w, r13, r14"
                "\n      max r18.w, r18_abs.w, c176.w"
                "\n      rcp r18.w, r18.w"
                "\n      mul r18.xyz, r16, -r18.w"
                "\n      mul r19.xyz, r15, r15"
                "\n      mul r19.xyz, r19, r19"
                "\n      dp3 r19.w, r19, c175.x"
                "\n      rcp r19.w, r19.w"
                "\n      mul r19.xyz, r19, r19.w"
                "\n      mad r20.xyz, r18, c187.x, r17"
                "\n      mul r20.xyz, r20, c187.z"
                "\n      mad r20.z, r20.z, -c179.y, r20.z"
                "\n      mov r22.xy, r20.yz"
                "\n      texld r21, r22, s14"
                "\n      mul r23, r21, r19.x"
                "\n      mov r22.xy, r20.xz"
                "\n      texld r21, r22, s14"
                "\n      mad r23, r21, r19.y, r23"
                "\n      texld r21, r20, s14"
                "\n      mad r23, r21, r19.z, r23"
                "\n      mad r20.xyz, r18, c187.y, r17"
                "\n      mul r20.xyz, r20, c187.w"
                "\n      mad r20.z, r20.z, -c179.y, r20.z"
                "\n      mov r22.xy, r20.yz"
                "\n      texld r21, r22, s14"
                "\n      mul r24, r21, r19.x"
                "\n      mov r22.xy, r20.xz"
                "\n      texld r21, r22, s14"
                "\n      mad r24, r21, r19.y, r24"
                "\n      texld r21, r20, s14"
                "\n      mad r24, r21, r19.z, r24"
                "\n      mul r25.xyz, r23, c177.x"
                "\n      mad r25.xyz, r24, c177.y, r25"
                "\n      mul r26.x, r25.z, c177.w"
                "\n      add r26.x, r26.x, c177.z"
                "\n      mov r29.w, c190.y"
                "\n      lrp r30.y, r29.w, r26.x, c175.x"
                "\n      mul r29.xyz, r12, r30.y"
                "\n      mul r28.xyz, r26.x, c186"
                "\n      lrp r26.xyz, c190.x, r29, r28"
                "\n      mul r27.x, r25.y, c178.w"
                "\n      mad_sat r27.x, r25.x, c178.z, r27.x"
                "\n      mad_sat r27.x, r27.x, c190.z, r27.x"
                "\n      mov r29.xyz, c188"
                "\n      lrp r28.xyz, r27.x, r29, r26"
                "\n      dp3 r27.y, r12, c176"
                "\n      mul r27.y, r27.y, c175.w"
                "\n      min r27.y, r27.y, c174.x"
                "\n      mov r29.w, c190.x"
                "\n      lrp r30.z, r29.w, c175.x, r27.y"
                "\n      mul r26.xyz, r28, r30.z"
                "\n      mad r26.xyz, r26, c179.z, r26"
                "\n      lrp r28.xyz, c186.w, r26, r12"
                "\n      dp3 r27.w, r13, r14"
                "\n      add_sat r27.z, c175.x, -r27_abs.w"
                "\n      mul r29.x, r27.z, r27.z"
                "\n      mul r29.x, r29.x, r29.x"
                "\n      mul r29.x, r29.x, r27.z"
                "\n      mul r29.x, r29.x, c178.y"
                "\n      add r29.x, r29.x, c178.x"
                "\n      mul r29.x, r29.x, c185.w"
                "\n      add r29.x, r29.x, c179.w"
                "\n      min r29.x, r29.x, c175.x"
                "\n      mov r30.xyz, c2"
                "\n      mul r30.xyz, r30, c188.w"
                "\n      lrp r31.xyz, r29.x, r30, r28"
                "\n      add r24.xyz, r14, c184"
                "\n      nrm r30.xyz, r24"
                "\n      dp3_sat r30.w, r13, r30"
                "\n      pow r30.w, r30.w, c184.w"
                "\n      min r27.y, r27.y, c175.x"
                "\n      mul r30.w, r30.w, r27.y"
                "\n      mad r31.xyz, r30.w, c185, r31"
                "\n      add r31.xyz, r31, -r12"
                "\n      mul r31.xyz, r31, c179.x"
                "\n      mov r30.xyz, c189"
                "\n      mad r31.xyz, r30, c179.x, r31"
                "\n    endif";
        }

        bool EditPixel(std::string& src, std::string& why)
        {
            if (src.find("ps_3_0") == std::string::npos) { why = "not ps_3_0"; return false; }
            static const std::regex clash(R"(\b(r1[2-9]|r2[0-9]|r3[01]|b\d+|i\d+|s14|c17[4-9]|c18[0-9]|c190)\b)");
            if (std::regex_search(src, clash)) { why = "uses a register the patch needs"; return false; }
            // The variant's view-space position and normal: what its shadow fade reads (dp3 with c3) and
            // what its sun-facing term reads (dp3 with c4). Shadow-less variants have neither.
            static const std::regex position(R"(dp3 r\d+\.\w, (v\d+), c3\b)"), normal(R"(dp3 r\d+\.\w, c4, (v\d+))");
            std::smatch mp, mn;
            if (!std::regex_search(src, mp, position) || !std::regex_search(src, mn, normal)) { why = "no view position/normal inputs (shadow-less variant)"; return false; }
            // The stock fog ending: the colour minus the fog colour (mad: a product minus it), then lerped
            // toward it by the fog factor. When the product already reads a constant, the fog colour is
            // moved into a register first (mov rK.xyz, c2 ... -rK): the WMO variants that write alpha. The
            // composite ones among them keep the colour in .yzw (alpha in .x): -rK.xxyz, read back as .yzww.
            static const std::regex fog(R"(\n[ \t]*(mad|add) (r\d+)\.(xyz|yzw), ([^\n]*), -(c2|r\d+)(\.xxyz)?[ \t]*(\n[ \t]*mad oC0\.xyz, v\d+\.x, \2(\.yzww)?, c2))");
            std::smatch m;
            if (!std::regex_search(src, m, fog)) { why = "no fog ending"; return false; }
            const bool shifted = m[3].str() == "yzw";
            if (shifted != (m[6].matched && m[8].matched) || (!shifted && (m[6].matched || m[8].matched)))
            { why = "no fog ending"; return false; }
            if (m[5].str() != "c2")
            {
                // The line right before the subtraction must be that load.
                const std::string load = "mov " + m[5].str() + ".xyz, c2";
                const size_t at = static_cast<size_t>(m.position(0));
                const size_t lineStart = at > 0 ? src.rfind('\n', at - 1) : std::string::npos;
                const size_t text = lineStart == std::string::npos ? std::string::npos : src.find_first_not_of(" \t", lineStart + 1);
                if (text == std::string::npos || text >= at || src.compare(text, load.size(), load) != 0)
                { why = "no fog ending"; return false; }
            }
            const std::string r = m[2].str();
            // The operands of the product (or the add's colour), read into r12.xyz: in the shifted form each
            // operand's .yzw becomes .xyz (no swizzle -> .yzww, abcd -> bcdd; a replicated one stays).
            std::string operands = m[4].str();
            if (shifted)
            {
                static const std::regex operand(R"(^(-?)([rcv]\d+)(?:\.([xyzw]+))?$)");
                std::string out;
                size_t from = 0;
                while (from <= operands.size())
                {
                    const size_t comma = operands.find(", ", from);
                    const std::string token = operands.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
                    std::smatch o;
                    if (!std::regex_match(token, o, operand)) { why = "no fog ending"; return false; }
                    const std::string swizzle = o[3].str();
                    std::string shiftedSwizzle;
                    if (swizzle.empty()) shiftedSwizzle = ".yzww";
                    else if (swizzle.size() == 1) shiftedSwizzle = "." + swizzle;
                    else if (swizzle.size() == 4) shiftedSwizzle = "." + swizzle.substr(1) + swizzle.substr(3);
                    else { why = "no fog ending"; return false; }
                    out += (out.empty() ? "" : ", ") + o[1].str() + o[2].str() + shiftedSwizzle;
                    if (comma == std::string::npos) break;
                    from = comma + 2;
                }
                operands = out;
            }
            const std::string base = (m[1].str() == "mad" ? "\n    mul r12.xyz, " : "\n    mov r12.xyz, ") + operands;
            const size_t subtraction = static_cast<size_t>(m.position(0)), afterSubtraction = static_cast<size_t>(m.position(7));
            src.insert(afterSubtraction, shifted ? "\n    add " + r + ".yzw, " + r + ", r31.xxyz" : "\n    add " + r + ".xyz, " + r + ", r31");
            src.insert(subtraction, base + Block(mp[1].str(), mn[1].str()));
            src.insert(src.find('\n') + 1,
                       "    def c175, 1, 0, 0.5, 2\n"
                       "    def c176, 0.2126, 0.7152, 0.0722, 0.2\n"
                       "    def c177, 0.6, 0.4, 0.35, 0.65\n"
                       "    def c178, 0.04, 0.96, 0.55, 0.4\n"
                       "    def c174, 1.3, 0, 0, 0\n"
                       "    dcl_2d s14\n");
            return true;
        }

        bool EditPixelCounted(std::string& src, std::string& why)
        {
            const bool ok = EditPixel(src, why);
            if (ok) ++g_patched;
            else
            {
                ++g_skipped;
                if (g_skipReasons.find(why) == std::string::npos) g_skipReasons += (g_skipReasons.empty() ? "" : "; ") + why;
            }
            return ok;
        }
    }

    Style StyleOf(const char* path)
    {
        if (!path) return Style::None;
        std::string p(path);
        for (char& c : p) c = c == '/' ? '\\' : static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        for (const char* m : kFrozenFalls)
            if (p.find(m) != std::string::npos) return Style::FrozenFall;
        for (const char* m : kIceModels)
            if (p.find(m) != std::string::npos) return Style::Light;
        return Style::None;
    }

    bool IsIceModel(const char* path) { return StyleOf(path) != Style::None; }

    void StyleConstants(Style style, float weight, float* look, float c179[4], float c190[4])
    {
        c179[0] = weight; c179[1] = 0.0f; c179[2] = 0.0f; c179[3] = 0.0f;
        c190[0] = 0.0f; c190[1] = 0.0f; c190[2] = 0.0f; c190[3] = 0.0f;
        if (style == Style::Light)
        {
            // Ice models and buildings: the texture's own colour is the body (icebergs, shoreline floes and
            // Icecrown's and the Nexus's ice are already bright, saturated ice, which the deep body only dulled), with a little of the cloudy depth
            // left on it and a touch of brightness to make up for it. Whiter, stronger cracks and bubbles,
            // so they read against a bright surface, and a softer Fresnel sheen; the glint as it is.
            c190[0] = 1.0f; c190[1] = 0.3f; c190[2] = 0.6f;
            c179[2] = 0.1f;
            look[23] = 0.45f;                                          // c185.w Fresnel strength
            look[27] = 1.0f;                                           // c186.w clarity
            look[32] = 0.85f; look[33] = 0.95f; look[34] = 1.0f;       // c188.xyz crack/bubble highlight
            return;
        }
        if (style != Style::FrozenFall) return;
        // Frozen falls: the detail stretched 5x along the flow at 2.5x the scale (streaks that still read
        // from far off: Bor's Fall is 366 yd tall), layers 4x deeper, a paler glacial body brightened 1.5x
        // over more of the model's own texture, softer cracks, and a glassier surface: stronger Fresnel with
        // a base reflection even face on, brighter sky in it, and a broader, gentler sheen than the glint.
        c179[1] = 0.8f; c179[2] = 0.5f; c179[3] = 0.08f;
        look[19] = 120.0f;                                         // c184.w glint exponent
        for (int k = 20; k < 23; ++k) look[k] *= 0.55f;            // c185.xyz glint colour
        look[23] = 1.0f;                                           // c185.w Fresnel strength
        look[24] = 0.14f; look[25] = 0.40f; look[26] = 0.56f;      // c186.xyz deep body
        look[27] = std::min(look[27], 0.45f);                      // c186.w clarity
        look[28] = 1.5f; look[29] = 4.0f;                          // c187.xy depths (yd)
        look[30] = 1.0f / 10.0f; look[31] = 1.0f / 18.0f;          // c187.zw detail repeats per yd
        look[32] = 0.62f; look[33] = 0.84f; look[34] = 0.97f;      // c188.xyz crack/bubble highlight
        look[35] = 1.3f;                                           // c188.w sky reflection brightness
    }

    bool EditPixelForTests(std::string& src, std::string& why) { return EditPixel(src, why); }

    void Register()
    {
        shaderpatch::TableRule rule;
        rule.name = "model ice (M2 pixel)";
        rule.pixel = true;
        rule.contains = [](const void* wrapper) { return m2effects::IsPixelWrapper(wrapper); };
        rule.edit = &EditPixelCounted;
        shaderpatch::Register(std::move(rule));
    }

    void Install(const WXL_Api* api)
    {
        // Behind the core's own detour on the batch draw if it already holds the head (live from start-up).
        g_hookedBatch = api->HookAttachByName("M2.DrawBatch", reinterpret_cast<void*>(&hkBatch), reinterpret_cast<void**>(&g_origBatch), WXL_HOOK_DEFAULT_PRIORITY) != 0 ||
                        api->HookAttachByName("M2.DrawBatch", reinterpret_cast<void*>(&hkBatch), reinterpret_cast<void**>(&g_origBatch), 1000) != 0;
        g_hookedDoodad = api->HookAttachByName("M2.DrawBatchDoodad", reinterpret_cast<void*>(&hkDoodad), reinterpret_cast<void**>(&g_origDoodad), WXL_HOOK_DEFAULT_PRIORITY) != 0 ||
                         api->HookAttachByName("M2.DrawBatchDoodad", reinterpret_cast<void*>(&hkDoodad), reinterpret_cast<void**>(&g_origDoodad), 1000) != 0;
        if (!g_hookedBatch || !g_hookedDoodad)
            api->Log(WXL_LOG_WARN, "wxl-seyris-living-azeroth", "model ice: batch draw hook missing (batch %d, doodad %d)", g_hookedBatch ? 1 : 0, g_hookedDoodad ? 1 : 0);
    }

    void SetEnabled(bool enabled) { g_enabled = enabled; }
    bool Enabled()                { return g_enabled; }
    unsigned PatchedVariants()    { return g_patched; }

    const char* StatusLine()
    {
        char line[512];
        std::snprintf(line, sizeof(line), "ice models: %s; M2 pixel shader variants with ice %u, left stock %u%s%s%s; ice batches drawn so far %u",
                      g_enabled ? "on" : "off", g_patched, g_skipped, g_skipReasons.empty() ? "" : " (", g_skipReasons.c_str(), g_skipReasons.empty() ? "" : ")",
                      g_iceDraws);
        if (!g_hookedBatch || !g_hookedDoodad) std::strncat(line, "; batch draw hook missing", sizeof(line) - std::strlen(line) - 1);
        g_status = line;
        return g_status.c_str();
    }
}
