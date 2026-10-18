#include "ModelIce.hpp"
#include "DoodadFade.hpp"
#include "Glass.hpp"
#include "GlassShader.hpp"

#include "CoverRipples.hpp"

#include "TerrainIce.hpp"
#include "../render/M2Effects.hpp"
#include "../render/ShaderPatch.hpp"

#include "game/Gx.hpp"

#include <d3d9.h>

#include <algorithm>
#include <cmath>
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

        // The waterfall "hats" (the sheet a river pours over a fall's edge in): 60-100 yd of streaky cascade texture
        // laid over the river, whose upstream end showed as a jagged edge on our water (the user at Glacial Falls,
        // 2026-10-11: "the top of the hat still has jagged edges rather than laminar flow in our water"). Hidden
        // altogether, the hat took the flow with it ("waterfall hat is also completely gone, no laminar flow
        // either"), so it stays, and its upstream end fades in over kHatFades' run instead: the vertex alpha times
        // saturate((x - start) / run) in model space (VS b15, c250; the edit is EditVertexHatFade, applied by the
        // model lights' vertex rule). g_hideHats is left as a test switch.
        bool g_hideHats = false;
        bool g_fadeHats = true;
        unsigned g_hatsHidden = 0, g_hatsFaded = 0, g_fadeVariants = 0;
        constexpr int kHatFadeConstant = 250;  // VS c250 = (1 / run, -start / run, 0, 0)
        constexpr int kHatFadeSwitch = 15;     // VS b15

        // Where each hat's water sheets start upstream (model x; the latest start among its batches, so no sheet's
        // own end shows) and the run (model yards) to full; from waterfall-scan's m2_water.py on the four hats the
        // world places (their sheets run along +x to the edge at 0.5 .. 9.2).
        struct HatFade { const char* name; float start; float run; };
        const HatFade kHatFades[] = {
            { "waterfalls_set1_high_hat", -59.6f, 20.0f },
            { "waterfalls_set2_high_hat", -59.6f, 20.0f },
            { "waterfalls_set1_med_hat",  -48.2f, 20.0f },
            { "waterfalls_set1_low_hat",  -13.1f, 6.0f },
        };

        const HatFade* HatFadeOf(const char* path)
        {
            std::string p(path ? path : "");
            for (char& c : p) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            for (const HatFade& h : kHatFades)
                if (p.find(h.name) != std::string::npos) return &h;
            return nullptr;
        }

        struct Kind { Style style = Style::None; bool hat = false; const HatFade* fade = nullptr; };
        struct Entry { std::string stem; Kind kind; };
        std::unordered_map<const void*, Entry> g_models;

        Kind KindFor(const void* ctx)
        {
            if (!ctx) return {};
            const auto* instance = *reinterpret_cast<const uint8_t* const*>(static_cast<const uint8_t*>(ctx) + kCtxInstance);
            if (!instance) return {};
            const auto* shared = *reinterpret_cast<const uint8_t* const*>(instance + kInstShared);
            if (!shared) return {};
            const char* stem = reinterpret_cast<const char*>(shared + kSharedStem);
            const size_t length = strnlen(stem, kStemCapacity);
            Entry& e = g_models[shared];
            if (e.stem.size() != length || std::memcmp(e.stem.data(), stem, length) != 0)
            {
                e.stem.assign(stem, length);
                e.kind = { StyleOf(e.stem.c_str()), IsWaterfallHat(e.stem.c_str()), HatFadeOf(e.stem.c_str()) };
            }
            if (g_models.size() > 8192) { const Kind kind = e.kind; g_models.clear(); return kind; }
            return e.kind;
        }

        // A hat's fade on for its batch: false when it isn't a hat, the fade is off, or no vertex shader took it.
        bool BeginHatFade(const Kind& kind)
        {
            if (!g_fadeHats || !kind.fade || g_fadeVariants == 0) return false;
            auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice());
            if (!dev) return false;
            const float fade[4] = { 1.0f / kind.fade->run, -kind.fade->start / kind.fade->run, 0.0f, 0.0f };
            dev->SetVertexShaderConstantF(kHatFadeConstant, fade, 1);
            const BOOL on = TRUE;
            dev->SetVertexShaderConstantB(kHatFadeSwitch, &on, 1);
            ++g_hatsFaded;
            return true;
        }

        void EndHatFade()
        {
            if (auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice()))
            {
                const BOOL off = FALSE;
                dev->SetVertexShaderConstantB(kHatFadeSwitch, &off, 1);
            }
        }

        // Switches the block on for one batch (main thread, inside the M2 pass).
        bool Begin(Style style)
        {
            if (!g_enabled || g_patched == 0) return false;
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
            const Kind kind = KindFor(ctx);
            if (kind.hat && g_hideHats) { ++g_hatsHidden; return; }
            const bool faded = BeginHatFade(kind);
            const bool ice = Begin(kind.style);
            g_origBatch(ctx, edx);
            if (ice) End();
            if (faded) EndHatFade();
        }

        void __fastcall hkDoodad(void* ctx, void* edx, void* elements, void* indices)
        {
            const Kind kind = KindFor(ctx);
            if (kind.hat && g_hideHats) { ++g_hatsHidden; return; }
            const bool faded = BeginHatFade(kind);
            const bool ice = Begin(kind.style);
            g_origDoodad(ctx, edx, elements, indices);
            if (ice) End();
            if (faded) EndHatFade();
        }

        // The rims block (b8), each rim following its print's own outline (CoverRipples.hpp, as SurfaceCover's RippleRings):
        // the world position from the view's rows (as the ice block). A pixel shader can't index constants by a counter
        // and working out every rim's outline would not fit, so a first pass over the rims only picks the (at most) two
        // whose reach covers the pixel -- the first into r13..r15, the second into r16..r18 (r19.xy: taken) -- and only
        // those are worked out (RingOne). The heights add up in r20.z, the slopes in r19.zw; then r31 += the colour x
        // (slope . sun x the slope gain + height x the height gain). r13..r30 are free here: the ice block's values are
        // all in r31 by now, and r12 (the colour) is only read.
        std::string RingOne(const std::string& a, const std::string& p1, const std::string& p2)
        {
            return
                // n = (the pixel - the outline's centre) / r0; q = |n|, r21.w = 1 / q; z1 = (cos t, sin t)
                "\n        add r21.xy, r20, -" + a +
                "\n        mul r21.xy, r21, " + a + ".z"
                "\n        dp2add r21.z, r21, r21, c175.y"
                "\n        max r21.z, r21.z, c174.z"
                "\n        rsq r21.w, r21.z"
                "\n        mul r21.z, r21.z, r21.w"
                "\n        mul r22.xy, r21, r21.w"
                // cos k t (r23: k = 1..4, r25: 5..8) and sin k t (r24, r26), by complex products
                "\n        mov r23.x, r22.x"
                "\n        mov r24.x, r22.y"
                "\n        mul r23.y, r22.x, r22.x"
                "\n        mad r23.y, r22.y, -r22.y, r23.y"
                "\n        mul r24.y, r22.x, r22.y"
                "\n        add r24.y, r24.y, r24.y"
                "\n        mul r23.zw, r23.xxxy, r23.y"
                "\n        mad r23.zw, r24.xxxy, -r24.y, r23"
                "\n        mul r24.zw, r24.xxxy, r23.y"
                "\n        mad r24.zw, r23.xxxy, r24.y, r24"
                "\n        mul r25, r23, r23.w"
                "\n        mad r25, r24, -r24.w, r25"
                "\n        mul r26, r24, r23.w"
                "\n        mad r26, r23, r24.w, r26"
                // the coefficients, two to a float (lo + 1024 hi, each (v + 1) x 511.5): a_k in r27 / r29, b_k in r28 / r30
                "\n        mul r28, " + p1 + ", c222.y"
                "\n        frc r27, r28"
                "\n        add r28, r28, -r27"
                "\n        mad r27, r28, -c222.x, " + p1 +
                "\n        mad r27, r27, c222.z, c222.w"
                "\n        mad r28, r28, c222.z, c222.w"
                "\n        mul r30, " + p2 + ", c222.y"
                "\n        frc r29, r30"
                "\n        add r30, r30, -r29"
                "\n        mad r29, r30, -c222.x, " + p2 +
                "\n        mad r29, r29, c222.z, c222.w"
                "\n        mad r30, r30, c222.z, c222.w"
                // the outline's distance there, r = 1 + sum a_k cos k t + b_k sin k t (r22.z), and its slope dr/dt (r22.w)
                "\n        dp4 r22.z, r27, r23"
                "\n        dp4 r22.w, r28, r24"
                "\n        add r22.z, r22.z, r22.w"
                "\n        dp4 r22.w, r29, r25"
                "\n        add r22.z, r22.z, r22.w"
                "\n        dp4 r22.w, r30, r26"
                "\n        add r22.z, r22.z, r22.w"
                "\n        add r22.z, r22.z, c175.x"
                "\n        mul r28, r28, r23"
                "\n        mad r28, r27, -r24, r28"
                "\n        mul r30, r30, r25"
                "\n        mad r30, r29, -r26, r30"
                "\n        dp4 r22.w, r28, c223"
                "\n        dp4 r27.x, r30, c223"
                "\n        dp4 r27.y, r30, c175.x"
                "\n        mad r27.x, r27.y, c173.w, r27.x"
                "\n        add r22.w, r22.w, r27.x"
                // the band's phase, clamped to -pi..pi (where its raised cosine and its slope are nothing)
                "\n        add r27.x, r21.z, -r22.z"
                "\n        mad r27.x, r27.x, c173.x, c173.y"
                "\n        max r27.x, r27.x, -c174.y"
                "\n        min r27.x, r27.x, c174.y"
                "\n        sincos r29.xy, r27.x"
                // the band's slope on the ground, (cos t + dr/q sin t, sin t - dr/q cos t) / r0, made a unit
                "\n        mul r27.y, r22.w, r21.w"
                "\n        mad r28.x, r27.y, r22.y, r22.x"
                "\n        mad r28.y, r27.y, -r22.x, r22.y"
                "\n        mul r28.xy, r28, " + a + ".z"
                "\n        dp2add r28.z, r28, r28, c175.y"
                "\n        max r28.z, r28.z, c174.z"
                "\n        rsq r28.z, r28.z"
                "\n        mul r28.xy, r28, r28.z"
                // strength (a.w's fraction / 0.99); slope -= sin x strength along it, height += (1 + cos) / 2 x strength
                "\n        frc r27.z, " + a + ".w"
                "\n        mul r27.z, r27.z, c173.z"
                "\n        mul r27.w, r29.y, r27.z"
                "\n        mad r19.zw, r28.xxxy, -r27.w, r19"
                "\n        add r29.x, r29.x, c175.x"
                "\n        mul r29.x, r29.x, c175.z"
                "\n        mad r20.z, r29.x, r27.z, r20.z";
        }

        std::string RingBlock(const std::string& vP)
        {
            std::string s =
                "\n    if b8"
                "\n      dp3 r20.x, " + vP + ", c181"
                "\n      dp3 r20.y, " + vP + ", c182"
                "\n      add r20.xy, r20, c180"
                "\n      mov r20.z, c175.y"
                "\n      mov r19, c175.y"
                "\n      mov r13, c175.y"
                "\n      mov r14, c175.y"
                "\n      mov r15, c175.y"
                "\n      mov r16, c175.y"
                "\n      mov r17, c175.y"
                "\n      mov r18, c175.y";
            for (int k = 0; k < kWmoRings; ++k)
            {
                const std::string a = "c" + std::to_string(kRingConstant + 3 * k), p1 = "c" + std::to_string(kRingConstant + 3 * k + 1),
                                  p2 = "c" + std::to_string(kRingConstant + 3 * k + 2);
                // Within its reach (a.w's whole part, 1/1024 yd; never for a rim not in use): r21.y; the second found
                // (r21.z) or the first (r21.w).
                s += "\n      add r21.xy, r20, -" + a +
                     "\n      max r21.x, r21_abs.x, r21_abs.y"
                     "\n      mad r21.x, r21.x, c222.x, -c222.w"
                     "\n      add r21.x, r21.x, -" + a + ".w"
                     "\n      cmp r21.y, r21.x, c175.y, c175.x"
                     "\n      mul r21.z, r21.y, r19.x"
                     "\n      add r21.w, r21.y, -r21.z"
                     "\n      cmp r16, -r21.z, r16, " + a +
                     "\n      cmp r17, -r21.z, r17, " + p1 +
                     "\n      cmp r18, -r21.z, r18, " + p2 +
                     "\n      cmp r13, -r21.w, r13, " + a +
                     "\n      cmp r14, -r21.w, r14, " + p1 +
                     "\n      cmp r15, -r21.w, r15, " + p2 +
                     "\n      max r19.y, r19.y, r21.z"
                     "\n      max r19.x, r19.x, r21.y";
            }
            s += "\n      if_gt r19.x, c175.y" + RingOne("r13", "r14", "r15") +
                 "\n      endif"
                 "\n      if_gt r19.y, c175.y" + RingOne("r16", "r17", "r18") +
                 "\n      endif"
                 "\n      mov r26.xy, c191"
                 "\n      dp2add r25.x, r19.zwzw, r26, c175.y"
                 "\n      mul r25.x, r25.x, c191.z"
                 "\n      mad r25.x, r20.z, c191.w, r25.x"
                 "\n      mad r31.xyz, r12, r25.x, r31"
                 "\n    endif";
            return s;
        }

        // The defined constants the rims block reads besides c174, c175: c173 = (2 pi / kRimBand, -kRimOffset x that,
        // 1 / 0.99, 4), c222 = (1024, 1 / 1024, 1 / 511.5, -1), c223 = (1, 2, 3, 4).
        std::string RingDefs()
        {
            char line[256];
            const double phase = 6.283185307179586 / cover::ripples::kRimBand;
            std::snprintf(line, sizeof(line), "    def c173, %.7g, %.7g, %.7g, 4\n    def c222, 1024, 0.0009765625, %.9g, -1\n    def c223, 1, 2, 3, 4\n",
                          phase, -cover::ripples::kRimOffset * phase, 1.0 / 0.99, 1.0 / 511.5);
            return line;
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
        // With distance (r22.w = saturate(distance x c190.w + c189.w): 1 up close, 0 far) the parallax and the
        // cracks fade out: seen nearly edge-on far away, the layers' view-dependent offset changed faster than
        // the pixels and drew moire bands across the glacier faces (the user, 2026-10-11).
        // ps_3_0 reads at most one constant register per instruction; lrp's destination can't be its
        // first or third source.
        std::string Block(const std::string& vP, const std::string& vN, bool rims)
        {
            return
                "\n    mov r31.xyz, c175.y"
                "\n    if b7"
                "\n      nrm r13.xyz, " + vN +
                "\n      mov r14.xyz, -" + vP +
                "\n      dp3 r22.w, r14, r14"
                "\n      rsq r22.w, r22.w"
                "\n      rcp r22.w, r22.w"
                "\n      mul r22.w, r22.w, c190.w"
                "\n      add_sat r22.w, r22.w, c189.w"
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
                "\n      mul r18.xyz, r18, r22.w"
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
                "\n      mul r27.x, r27.x, r22.w"
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
                "\n    endif" + (rims ? RingBlock(vP) : std::string());
        }

        bool EditPixel(std::string& src, std::string& why, bool rims, bool glassBlocks = false)
        {
            if (src.find("ps_3_0") == std::string::npos) { why = "not ps_3_0"; return false; }
            static const std::regex clash(R"(\b(r1[2-9]|r2[0-9]|r3[01]|b\d+|i\d+|s14|c10[1-9]|c1[1-9][0-9]|c2[0-2][0-9])\b)");
            if (std::regex_search(src, clash)) { why = "uses a register the patch needs"; return false; }
            // The variant's view-space position and normal: what its shadow fade reads (dp3 with c3) and
            // what its sun-facing term reads (dp3 with c4). Shadow-less variants have neither.
            static const std::regex position(R"(dp3 r\d+\.\w, (v\d+), c3\b)"), normal(R"(dp3 r\d+\.\w, c4, (v\d+))");
            std::smatch mp, mn;
            if (!std::regex_search(src, mp, position) || !std::regex_search(src, mn, normal)) { why = "no view position/normal inputs (shadow-less variant)"; return false; }
            // Copied out now: the matches point into src, which the inserts below reallocate (reading them after an
            // insert read freed memory, and only came out right while nothing had reused it yet).
            const std::string vP = mp[1].str(), vN = mn[1].str();
            static const std::regex texcoord(R"(texld r\d+, (v\d+), s0\b)");
            std::smatch mt;
            const bool glassHere = glassBlocks && std::regex_search(src, mt, texcoord);
            const std::string vT = glassHere ? mt[1].str() : std::string();
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
            // The buildings' glass and floor light (features/GlassShader) after the ice and the rims: they need the
            // variant's first texture's coordinates (its stock `texld rK, vT, s0`) as well.
            const std::string glassText = glassHere ? glass::GlassBlock(vP, vN, vT) + glass::FloorBlock(vP, vN, vT) : std::string();
            src.insert(subtraction, base + Block(vP, vN, rims) + glassText);
            if (glassHere) src.insert(src.find('\n') + 1, glass::ShaderDefs());
            src.insert(src.find('\n') + 1,
                       "    def c175, 1, 0, 0.5, 2\n"
                       "    def c176, 0.2126, 0.7152, 0.0722, 0.2\n"
                       "    def c177, 0.6, 0.4, 0.35, 0.65\n"
                       "    def c178, 0.04, 0.96, 0.55, 0.4\n"
                       "    def c174, 1.3, 3.14159265, 0.000001, 0\n" + RingDefs() +
                       "    dcl_2d s14\n");
            return true;
        }

        // Whether the device takes the buildings' variants with the rims (kRingsMinSlots), asked once.
        bool RingsFit()
        {
            static int fit = -1;
            if (fit < 0)
            {
                D3DCAPS9 caps = {};
                auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice());
                fit = dev && SUCCEEDED(dev->GetDeviceCaps(&caps)) && caps.MaxPixelShader30InstructionSlots >= kRingsMinSlots ? 1 : 0;
            }
            return fit == 1;
        }

        // Whether the device takes the buildings' variants with the glass blocks too (kGlassMinSlots), asked once.
        bool GlassFit()
        {
            static int fit = -1;
            if (fit < 0)
            {
                D3DCAPS9 caps = {};
                auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice());
                fit = dev && SUCCEEDED(dev->GetDeviceCaps(&caps)) && caps.MaxPixelShader30InstructionSlots >= kGlassMinSlots ? 1 : 0;
            }
            return fit == 1;
        }

        bool EditPixelCountedWith(std::string& src, std::string& why, bool rims, bool glassBlocks = false);
        bool EditPixelCounted(std::string& src, std::string& why) { return EditPixelCountedWith(src, why, false); }
        bool EditPixelWmoCounted(std::string& src, std::string& why)
        {
            const bool rims = RingsFit();
            return EditPixelCountedWith(src, why, rims, rims && GlassFit());
        }

        bool EditPixelCountedWith(std::string& src, std::string& why, bool rims, bool glassBlocks)
        {
            const bool ok = EditPixel(src, why, rims, glassBlocks);
            if (ok && glassBlocks && src.find("if b9") != std::string::npos) glass::NotePatchedVariant();
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
        // The floating chunks all over Northrend's lakes and coasts (Wintergrasp's lake, Borean's shore: these
        // models, 7 x 11 yd placed at about 2.3x, up to 13x) take the sea-ice style: the clear light look (the floe
        // style had turned them into opaque slabs: "these wintergrasp ice chunks don't look as transparent", the
        // user, 2026-10-11) with wider, deeper detail ("some frozen sea ice should have bigger tiling too imo").
        if (p.find("dragonblight\\dragonblight_shoreline_ice") != std::string::npos)
            return Style::SeaIce;
        for (const char* m : kIceModels)
            if (p.find(m) != std::string::npos)
                return std::strncmp(m, "dragonblight\\", 13) == 0 ? Style::Glacier : Style::Light;
        return Style::None;
    }

    bool IsIceModel(const char* path) { return StyleOf(path) != Style::None; }

    bool EditVertexHatFade(std::string& src, const std::string& stock)
    {
        static const std::regex clash(R"(\b(r28|b15|c250)\b)");
        if (src.find("vs_3_0") == std::string::npos || std::regex_search(stock, clash)) return false;
        // The colour write: lit, `mad_sat oK, <material>, rL, c29` (rL.w is 1); unlit, `add_sat oK, rM, c29` (rM the
        // material, c28). Its alpha is what the fade scales; exactly one, and v0 the model-space position.
        static const std::regex colour(R"(\n[ \t]*(?:mad_sat o\d+, [rv]\d+, (r\d+), c29|add_sat o\d+, (r\d+), c29)[ \t]*\n)");
        std::smatch m;
        if (!std::regex_search(src, m, colour) || src.find("dcl_position v0") == std::string::npos) return false;
        const std::string rest = m.suffix().str();
        if (std::regex_search(rest, colour)) return false;
        const std::string reg = m[1].matched ? m[1].str() : m[2].str();
        src.insert(static_cast<size_t>(m.position(0)),
                   "\n    if b15"
                   "\n    mad_sat r28.w, v0.x, c250.x, c250.y"
                   "\n    mul " + reg + ".w, " + reg + ".w, r28.w"
                   "\n    endif");
        ++g_fadeVariants;
        return true;
    }

    bool IsWaterfallHat(const char* path)
    {
        if (!path) return false;
        std::string p(path);
        for (char& c : p) c = c == '/' ? '\\' : static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        const size_t slash = p.rfind('\\');
        const std::string name = slash == std::string::npos ? p : p.substr(slash + 1);
        return name.find("waterfall") != std::string::npos && name.find("_hat") != std::string::npos;
    }

    void StyleConstants(Style style, float weight, float* look, float c179[4], float c190[4])
    {
        c179[0] = weight; c179[1] = 0.0f; c179[2] = 0.0f; c179[3] = 0.0f;
        c190[0] = 0.0f; c190[1] = 0.0f; c190[2] = 0.0f;
        // The detail's parallax and cracks fade from kDetailFadeStart to kDetailFadeEnd yards (r22.w in Block).
        constexpr float kDetailFadeStart = 60.0f, kDetailFadeEnd = 180.0f;
        c190[3] = -1.0f / (kDetailFadeEnd - kDetailFadeStart);
        look[39] = kDetailFadeEnd / (kDetailFadeEnd - kDetailFadeStart); // c189.w
        if (style == Style::Light || style == Style::SeaIce)
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
            if (style == Style::SeaIce)
            {
                // The floating floes: the detail repeating every 4-7 yd tiled small across a 15-25 yd floe ("some
                // frozen sea ice should have bigger tiling too imo", the user, 2026-10-11). 2.5x wider, and its
                // layers about twice as deep (the floes are 1-6 yd thick as placed) so it lies inside the ice.
                look[28] = 0.8f; look[29] = 2.4f;                      // c187.xy depths (yd)
                look[30] = 1.0f / 10.0f; look[31] = 1.0f / 18.0f;      // c187.zw detail repeats per yd
            }
            return;
        }
        if (style == Style::Floe)
        {
            // The flat shoreline floes (Kaskala, the Dragonblight and Borean coasts): near-white from above, where
            // the light style's pale crack highlight is darker than the floe (a grey lattice) and its cloudy depth
            // and fog-coloured sheen read as dirty blotches (the user, at Kaskala: "still ugly"). The floe's own
            // colour without the clouds, a quarter of the cracks, a faint sheen, and the glint.
            c190[0] = 1.0f; c190[1] = 0.0f; c190[2] = -0.75f;
            c179[2] = 0.05f;
            look[23] = 0.2f;                                           // c185.w Fresnel strength
            look[27] = 1.0f;                                           // c186.w clarity
            look[32] = 1.0f; look[33] = 1.0f; look[34] = 1.0f;         // c188.xyz crack/bubble highlight
            return;
        }
        if (style == Style::Glacier)
        {
            // Dragonblight's ice (the ice rocks, shards, cliffs and icebergs, and the Crystal Vice): 30-60 yd
            // crystals, where the detail repeating every 4-7 yd read as a small busy lattice on the surface and
            // the ice as milky (the user: "should have wider tiling and more transparency"). The detail 3x
            // wider and its layers 3x deeper, so it swims inside the ice as the view moves; the texture's own
            // colour as the body with lighter clouds and softer cracks (more of the deep body, or the light style's
            // clouds, made it murky); a glassier, brighter sky reflection at grazing angles.
            c190[0] = 1.0f; c190[1] = 0.2f; c190[2] = -0.4f;
            c179[2] = 0.08f;
            look[23] = 0.55f;                                           // c185.w Fresnel strength
            look[27] = 1.0f;                                           // c186.w clarity
            look[28] = 1.0f; look[29] = 3.2f;                          // c187.xy depths (yd)
            look[30] = 1.0f / 12.0f; look[31] = 1.0f / 22.0f;          // c187.zw detail repeats per yd
            look[32] = 0.9f; look[33] = 0.97f; look[34] = 1.0f;        // c188.xyz crack/bubble highlight
            look[35] = 1.3f;                                           // c188.w sky reflection brightness
            // A broader, brighter sun glint: at the default 350 a big flat face caught the sun only when
            // exactly aligned with it (the user: "maybe needs more sun glint in daylight").
            look[19] = 120.0f;                                         // c184.w glint exponent
            for (int k = 20; k < 23; ++k) look[k] *= 2.0f;            // c185.xyz glint colour
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

    bool EditPixelForTests(std::string& src, std::string& why) { return EditPixel(src, why, false); }
    bool EditPixelWmoForTests(std::string& src, std::string& why) { return EditPixel(src, why, true); }
    bool EditPixelWmoGlassForTests(std::string& src, std::string& why) { return EditPixel(src, why, true, true); }

    std::string RingBlockSelfTest(void* device)
    {
        namespace rp = cover::ripples;
        auto* dev = static_cast<IDirect3DDevice9*>(device);
        if (!dev) return "no device";
        // A boot turned 30 degrees, a paw turned the other way and mirrored, and an oval (overlapping the boot's), near
        // the world's origin (the pixels' positions come in as texture coordinates); strengths 1, 0.6, 0.8.
        const float at[2] = { 3.25f, -2.5f };
        const auto mapping = [](float turn, float acrossYd, float alongYd, bool mirrored, float map[6])
        {
            const float c = std::cos(turn), sn = std::sin(turn), m = mirrored ? -1.0f : 1.0f;
            map[0] = m * -sn / acrossYd; map[1] = m * c / acrossYd; map[2] = 0.5f;
            map[3] = -c / alongYd;       map[4] = -sn / alongYd;    map[5] = 0.5f;
        };
        rp::Shape shapes[3];
        float map[6];
        mapping(0.52f, 0.15f, 0.33f, false, map);
        bool ok = rp::ShapeFromPrint(1, map, at[0] - 0.25f, at[1] - 0.2f, shapes[0]);
        mapping(-1.1f, 0.3f, 0.3f, true, map);
        ok = rp::ShapeFromPrint(7, map, at[0] + 0.3f, at[1] - 0.15f, shapes[1]) && ok;
        const float axis[2] = { 0.8f, 0.6f };
        ok = rp::ShapeFromOval(at[0] - 0.05f, at[1] + 0.2f, axis, 0.165f, 0.075f, shapes[2]) && ok;
        if (!ok) return "shapes failed";
        const float strength[3] = { 1.0f, 0.6f, 0.8f };
        float rings[4 * rp::kRegistersPerRim * kWmoRings] = {};
        for (int k = 0; k < 3; ++k) rp::PackRim(shapes[k], 0.0f, 0.0f, strength[k], rings + k * 4 * rp::kRegistersPerRim);

        const std::string text = "ps_3_0\n    def c175, 1, 0, 0.5, 2\n    def c174, 1.3, 3.14159265, 0.000001, 0\n" + RingDefs() +
                                 "    defb b8, true\n    dcl_texcoord0 v0.xyz\n    mov r12.xyz, c175.x\n    mov r31.xyz, c175.y" + RingBlock("v0") +
                                 "\n    mov oC0.xyz, r31\n    mov oC0.w, c175.x\n";
        std::vector<uint8_t> code;
        std::string why;
        if (!shaderpatch::Assemble(text, "rims self-test", code, why)) return "FAIL the rims block doesn't assemble: " + why;
        constexpr int kSide = 120;
        constexpr float kFrom = -0.65f, kTo = 0.65f;
        IDirect3DPixelShader9* ps = nullptr;
        IDirect3DSurface9* target = nullptr;
        IDirect3DSurface9* readback = nullptr;
        IDirect3DSurface9* oldTarget = nullptr;
        IDirect3DStateBlock9* saved = nullptr;
        const auto cleanup = [&]()
        {
            if (ps) ps->Release();
            if (target) target->Release();
            if (readback) readback->Release();
            if (oldTarget) { dev->SetRenderTarget(0, oldTarget); oldTarget->Release(); }
            if (saved) { saved->Apply(); saved->Release(); }
        };
        if (FAILED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps)) ||
            FAILED(dev->CreateRenderTarget(kSide, kSide, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr)) ||
            FAILED(dev->CreateOffscreenPlainSurface(kSide, kSide, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &readback, nullptr)))
        { cleanup(); return "FAIL creating the rims shader or its target"; }
        dev->GetRenderTarget(0, &oldTarget);
        dev->CreateStateBlock(D3DSBT_ALL, &saved);
        struct V { float x, y, z, w, u, v, s; };
        const float e = static_cast<float>(kSide) - 0.5f;
        const V quad[4] = { { -0.5f, -0.5f, 0, 1, at[0] + kFrom, at[1] + kFrom, 0 }, { e, -0.5f, 0, 1, at[0] + kTo, at[1] + kFrom, 0 },
                            { -0.5f, e, 0, 1, at[0] + kFrom, at[1] + kTo, 0 },  { e, e, 0, 1, at[0] + kTo, at[1] + kTo, 0 } };
        const float view[12] = { 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0 };   // c180: no offset; c181, c182: x and y as they are
        // Three draws: the height (c191 = height gain 1), then the slope's x and y (the "sun" along each, slope gain 1).
        const float controls[3][4] = { { 0, 0, 0, 1 }, { 1, 0, 1, 0 }, { 0, 1, 1, 0 } };
        std::vector<float> out[3];
        for (int pass = 0; pass < 3; ++pass)
        {
            dev->SetRenderTarget(0, target);
            D3DVIEWPORT9 vp{ 0, 0, kSide, kSide, 0.0f, 1.0f };
            dev->SetViewport(&vp);
            dev->SetRenderState(D3DRS_ZENABLE, FALSE); dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE); dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
            dev->SetVertexShader(nullptr); dev->SetPixelShader(ps);
            dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0));
            dev->SetPixelShaderConstantF(kLookConstant, view, 3);
            dev->SetPixelShaderConstantF(kRingControl, controls[pass], 1);
            dev->SetPixelShaderConstantF(kRingConstant, rings, rp::kRegistersPerRim * kWmoRings);
            if (FAILED(dev->BeginScene())) { cleanup(); return "FAIL begin scene"; }
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(V));
            dev->EndScene();
            D3DLOCKED_RECT lr{};
            if (FAILED(dev->GetRenderTargetData(target, readback)) || FAILED(readback->LockRect(&lr, nullptr, D3DLOCK_READONLY)))
            { cleanup(); return "FAIL read back"; }
            out[pass].resize(static_cast<size_t>(kSide) * kSide);
            for (int row = 0; row < kSide; ++row)
                for (int col = 0; col < kSide; ++col)
                    out[pass][static_cast<size_t>(row) * kSide + col] =
                        reinterpret_cast<const float*>(static_cast<const uint8_t*>(lr.pBits) + row * lr.Pitch)[col * 4];
            readback->UnlockRect();
        }
        cleanup();
        // CoverRipples' own working, with the shapes as the shader got them (no fade for a coarse pixel: the buildings'
        // block has none).
        rp::Shape seen[3];
        float seenStrength[3];
        for (int k = 0; k < 3; ++k)
        {
            const float* o = rings + k * 4 * rp::kRegistersPerRim;
            seen[k] = rp::AsPacked(shapes[k]);
            seen[k].cx = o[0]; seen[k].cy = o[1]; seen[k].r0 = 1.0f / o[2];
            seenStrength[k] = (o[3] - std::floor(o[3])) / 0.99f;
        }
        float worstH = 0.0f, worstG = 0.0f, mostG = 0.0f, peak = 0.0f;
        int lit = 0;
        for (int j = 0; j < kSide; ++j)
            for (int i = 0; i < kSide; ++i)
            {
                const float x = at[0] + kFrom + (i + 0.5f) / kSide * (kTo - kFrom), y = at[1] + kFrom + (j + 0.5f) / kSide * (kTo - kFrom);
                float h = 0.0f, g[2] = { 0.0f, 0.0f };
                for (int k = 0; k < 3; ++k) rp::RimShade(seen[k], seenStrength[k], x, y, 1e-6f, h, g);
                const size_t p = static_cast<size_t>(j) * kSide + i;
                worstH = std::max(worstH, std::fabs(out[0][p] - h));
                worstG = std::max(worstG, std::hypot(out[1][p] - g[0], out[2][p] - g[1]));
                mostG = std::max(mostG, std::hypot(g[0], g[1]));
                peak = std::max(peak, out[0][p]);
                lit += out[0][p] > 0.05f ? 1 : 0;
            }
        char line[256];
        std::snprintf(line, sizeof(line), "buildings' rims: GPU against CoverRipples over %d x %d pixels: height off by up to %.4f (peak %.3f, %d pixels lit), slope by %.4f of %.3f",
                      kSide, kSide, worstH, peak, lit, worstG, mostG);
        if (worstH > 0.02f || worstG > 0.02f * mostG || peak < 0.9f || lit < 300) return std::string("FAIL ") + line;
        return line;
    }

    void Register()
    {
        shaderpatch::TableRule rule;
        rule.name = "model ice (M2 pixel)";
        rule.pixel = true;
        rule.contains = [](const void* wrapper) { return m2effects::IsPixelWrapper(wrapper) && !m2effects::IsWmoPixelWrapper(wrapper); };
        // Then the doodads' dissolve (features/DoodadFade): ShaderPatch applies one table rule per shader, and this one
        // owns the M2 pixel shaders. Its block goes in after the ice's edit (which refuses variants using any b register).
        rule.edit = [](std::string& src, std::string& why)
        {
            const std::string stock = src;
            const bool ice = EditPixelCounted(src, why);
            if (!ice) src = stock;
            std::string whyNot;
            const bool dissolve = doodadfade::EditPixelDissolve(src, whyNot);
            if (!ice && !dissolve) why += "; dissolve: " + whyNot;
            return ice || dissolve;
        };
        shaderpatch::Register(std::move(rule));
        // The buildings' (MapObj*) variants: the ice, and the footprint rims on their sand floors (features/WmoIce).
        shaderpatch::TableRule wmo;
        wmo.name = "model ice and footprint rims (WMO pixel)";
        wmo.pixel = true;
        wmo.contains = [](const void* wrapper) { return m2effects::IsWmoPixelWrapper(wrapper); };
        wmo.edit = &EditPixelWmoCounted;
        shaderpatch::Register(std::move(wmo));
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
    void SetHideHats(bool hide)   { g_hideHats = hide; }
    bool HideHats()               { return g_hideHats; }
    void SetFadeHats(bool fade)   { g_fadeHats = fade; }
    bool FadeHats()               { return g_fadeHats; }
    unsigned PatchedVariants()    { return g_patched; }

    const char* StatusLine()
    {
        char line[512];
        std::snprintf(line, sizeof(line), "ice models: %s; M2 pixel shader variants with ice %u, left stock %u%s%s%s; ice batches drawn so far %u",
                      g_enabled ? "on" : "off", g_patched, g_skipped, g_skipReasons.empty() ? "" : " (", g_skipReasons.c_str(), g_skipReasons.empty() ? "" : ")",
                      g_iceDraws);
        if (!g_hookedBatch || !g_hookedDoodad) std::strncat(line, "; batch draw hook missing", sizeof(line) - std::strlen(line) - 1);
        char hats[160];
        std::snprintf(hats, sizeof(hats), "; waterfall hats %s (%u batches skipped, %u faded at their upstream end; %u vertex variants with the fade)",
                      g_hideHats ? "hidden" : g_fadeHats ? "faded in" : "drawn", g_hatsHidden, g_hatsFaded, g_fadeVariants);
        std::strncat(line, hats, sizeof(line) - std::strlen(line) - 1);
        g_status = line;
        return g_status.c_str();
    }
}
