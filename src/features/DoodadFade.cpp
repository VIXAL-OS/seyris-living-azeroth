#include "DoodadFade.hpp"
#include "../debug/MemoryTag.hpp"

#include "game/Gx.hpp"
#include "../debug/UiText.hpp"

#include <windows.h>
#include <d3d9.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <regex>
#include <unordered_map>
#include <vector>

namespace wxl_livingazeroth::doodadfade
{
    namespace
    {
        namespace gx = wxl::game::gx;
        constexpr const char* kTag = "wxl-seyris-living-azeroth";

        // The client's view-distance table (traced 2026-10-12, memory wxl-objectfade-table-lead). Five
        // size categories each. Base rows, never written by the client after load:
        constexpr uintptr_t kBaseDistance  = 0x00ADF364; // 30 / 100 / 200 / 750 / 1250 yd
        constexpr uintptr_t kBaseFadeWidth = 0x00ADF38C; // 5 / 10 / 15 / 20 / 50 yd
        // Runtime rows (rebuilt by 0x78F570 from the base rows and environmentDetail; read by the fade):
        constexpr uintptr_t kDistance        = 0x00ADF3A0;
        constexpr uintptr_t kDistanceSq      = 0x00ADF3B4;
        constexpr uintptr_t kFadeStart       = 0x00ADF3C8;
        constexpr uintptr_t kFadeStartSq     = 0x00ADF3DC;
        constexpr int       kCategories = 5;

        // The dissolve (Wow.exe 3.3.5a 12340, static disassembly, 2026-10-15). The fade: __cdecl(doodad, ?), called
        // per doodad in view (0x7988B3, 0x799A2A); it works out alpha = 1 - (distance - fade start) / fade width from
        // the table above and stores it in the doodad's instance (+0x34) at +0x178, which the element build (its
        // helpers 0x82E140 / 0x82F0F0) and the instance's colours (0x831990: +0x19C = +0x17C x +0x178) multiply in.
        constexpr uintptr_t kDoodadFade     = 0x00791CB0;
        constexpr uintptr_t kDoodadInstance = 0x34;  // doodad -> its M2 instance
        constexpr uintptr_t kInstFade       = 0x178; // instance -> its fade alpha (float)
        constexpr uintptr_t kInstModel      = 0x2C;  // instance -> its shared model
        // The M2 batch draw context (core offsets/engine/Gx.hpp): the batch record drawing and its instance; the
        // batched doodad draw (0x820AE0) draws a head record's run of copies (+0x1C, read at its entry) in one call.
        constexpr uintptr_t kCtxElement = 0x50, kCtxInstance = 0x60, kElementRun = 0x1C;
        // Its records (0x820AE0 reads them as elements[index] for each index of the run): 0x44 bytes, +4 the copy's
        // instance (whose +0x98 bones it uploads).
        constexpr uintptr_t kElementSize = 0x44, kElementInstance = 0x04;
        constexpr uint32_t  kMaxRun = 4096;
        // The block's switch and constant: c100 = (alpha, the dither's two weights, its scale).
        constexpr int kDissolveBool = 12, kDissolveConst = 100;

        using FadeFn = void(__cdecl*)(void* doodad, void* arg);
        using DrawBatchFn = void(__fastcall*)(void* ctx, void* edx);
        using DrawDoodadFn = void(__fastcall*)(void* ctx, void* edx, void* elements, void* indices);
        FadeFn       g_origFade = nullptr;
        DrawBatchFn  g_origBatch = nullptr;
        DrawDoodadFn g_origDoodad = nullptr;
        bool g_hookedFade = false, g_hookedBatch = false, g_hookedDoodad = false;

        Settings g_settings;
        float    g_scale = 1.0f;
        // Instances dissolving: their fade alpha, as noted since the last Update and the frame before (the fade may
        // run after the draws in a frame).
        std::unordered_map<uintptr_t, float> g_fadeNow, g_fadePrev;
        unsigned g_variants = 0, g_variantsLeft = 0;
        std::string g_leftWhy;
        unsigned g_noted = 0, g_lastNoted = 0, g_split = 0, g_lastSplit = 0, g_draws = 0;

        volatile float* Row(uintptr_t at) { return reinterpret_cast<volatile float*>(at); }

        bool ReadPtr(uintptr_t at, uintptr_t& out)
        {
            __try { out = *reinterpret_cast<const uintptr_t*>(at); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; return false; }
        }
        bool ReadU32(uintptr_t at, uint32_t& out)
        {
            __try { out = *reinterpret_cast<const uint32_t*>(at); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; return false; }
        }
        bool ReadFloat(uintptr_t at, float& out)
        {
            __try { out = *reinterpret_cast<const float*>(at); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = 0.0f; return false; }
        }
        bool WriteFloat(uintptr_t at, float value)
        {
            __try { *reinterpret_cast<float*>(at) = value; return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool WriteU32(uintptr_t at, uint32_t value)
        {
            __try { *reinterpret_cast<uint32_t*>(at) = value; return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool CopyU32s(const uint32_t* from, uint32_t* to, uint32_t count)
        {
            __try { std::memcpy(to, from, count * sizeof(uint32_t)); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool Dissolving() { return g_settings.dissolve && g_hookedFade && g_hookedBatch && g_hookedDoodad && g_variants > 0; }

        void __cdecl hkFade(void* doodad, void* arg)
        {
            memuse::Scope memory(memuse::kFog);
            g_origFade(doodad, arg);
            if (!Dissolving() || !doodad) return;
            uintptr_t instance = 0;
            float alpha = 1.0f;
            if (!ReadPtr(reinterpret_cast<uintptr_t>(doodad) + kDoodadInstance, instance) || !instance) return;
            if (!ReadFloat(instance + kInstFade, alpha) || !(alpha > 0.0f && alpha < 0.99999f)) return;
            if (!WriteFloat(instance + kInstFade, 1.0f)) return; // opaque: the opaque pass, before the fog
            g_fadeNow[instance] = alpha;
            ++g_noted;
        }

        bool FadeOf(uintptr_t instance, float& alpha)
        {
            auto found = g_fadeNow.find(instance);
            if (found == g_fadeNow.end())
            {
                found = g_fadePrev.find(instance);
                if (found == g_fadePrev.end()) return false;
            }
            alpha = found->second;
            return true;
        }

        // The block on for the next draw, keeping `alpha` of its pixels.
        bool BeginAlpha(float alpha)
        {
            auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice());
            if (!dev) return false;
            // Interleaved gradient noise on the pixel's position: a fine, even dither (no visible tile).
            const float c[4] = { alpha, 0.06711056f, 0.00583715f, 52.9829189f };
            dev->SetPixelShaderConstantF(kDissolveConst, c, 1);
            const BOOL on = TRUE;
            dev->SetPixelShaderConstantB(kDissolveBool, &on, 1);
            ++g_draws;
            return true;
        }

        // The block on for one draw if its instance is dissolving.
        bool Begin(void* ctx)
        {
            if (g_fadeNow.empty() && g_fadePrev.empty()) return false;
            uintptr_t instance = 0;
            float alpha = 1.0f;
            if (!ReadPtr(reinterpret_cast<uintptr_t>(ctx) + kCtxInstance, instance) || !instance) return false;
            return FadeOf(instance, alpha) && BeginAlpha(alpha);
        }

        void End()
        {
            if (auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice()))
            {
                const BOOL off = FALSE;
                dev->SetPixelShaderConstantB(kDissolveBool, &off, 1);
            }
        }

        void __fastcall hkBatch(void* ctx, void* edx)
        {
            memuse::Scope memory(memuse::kFog);
            const bool on = Begin(ctx);
            g_origBatch(ctx, edx);
            if (on) End();
        }

        // A run of copies drawn together (per copy only its bones reach the shader, so they can't each dissolve by their
        // own fade) with dissolving copies in it: the run's index list reordered (the others first, then those), the
        // others drawn as one run, each dissolving copy as a run of one with its own alpha; the list and the run length
        // put back after (the caller's cursor reads the run length again when the call returns: core
        // offsets/engine/Gx.hpp). The copies of a run share the head record's batch and model, so any order draws them
        // alike. Stock draws a fading doodad on its own too (a see-through batch), so the draw count stays about stock.
        // False (nothing drawn): no copy dissolving, or the list couldn't be read or written.
        bool SplitRun(void* ctx, void* edx, void* elements, void* indices, uintptr_t element, uint32_t run)
        {
            if (run > kMaxRun || !elements || !indices) return false;
            auto* list = static_cast<uint32_t*>(indices);
            std::vector<uint32_t> saved(run), order, dissolving; // (locals: safe if a draw below ever came back here)
            std::vector<float> alphas;
            if (!CopyU32s(list, saved.data(), run)) return false;
            const uintptr_t records = reinterpret_cast<uintptr_t>(elements);
            for (uint32_t k = 0; k < run; ++k)
            {
                uintptr_t instance = 0;
                float alpha = 1.0f;
                if (ReadPtr(records + static_cast<uintptr_t>(saved[k]) * kElementSize + kElementInstance, instance) && instance &&
                    FadeOf(instance, alpha))
                {
                    dissolving.push_back(saved[k]);
                    alphas.push_back(alpha);
                }
                else
                    order.push_back(saved[k]);
            }
            if (dissolving.empty()) return false;
            const uint32_t kept = static_cast<uint32_t>(order.size());
            order.insert(order.end(), dissolving.begin(), dissolving.end());
            if (!CopyU32s(order.data(), list, run) || !WriteU32(element + kElementRun, kept > 0 ? kept : 1))
            {
                CopyU32s(saved.data(), list, run);
                WriteU32(element + kElementRun, run);
                return false;
            }
            if (kept > 0)
            {
                g_origDoodad(ctx, edx, elements, indices);
                WriteU32(element + kElementRun, 1);
            }
            for (size_t j = 0; j < dissolving.size(); ++j)
            {
                const bool on = BeginAlpha(alphas[j]);
                g_origDoodad(ctx, edx, elements, list + kept + j);
                if (on) End();
                WriteU32(element + kElementRun, 1); // (in case the draw rewrote it)
                ++g_split;
            }
            CopyU32s(saved.data(), list, run);
            WriteU32(element + kElementRun, run);
            return true;
        }

        void __fastcall hkDoodad(void* ctx, void* edx, void* elements, void* indices)
        {
            memuse::Scope memory(memuse::kFog);
            const bool any = Dissolving() && !(g_fadeNow.empty() && g_fadePrev.empty());
            uintptr_t element = 0;
            uint32_t run = 0;
            if (any && ReadPtr(reinterpret_cast<uintptr_t>(ctx) + kCtxElement, element) && element && ReadU32(element + kElementRun, run) && run >= 2)
            {
                if (!SplitRun(ctx, edx, elements, indices, element, run)) g_origDoodad(ctx, edx, elements, indices);
                return;
            }
            const bool on = any && Begin(ctx); // a doodad drawn on its own
            g_origDoodad(ctx, edx, elements, indices);
            if (on) End();
        }
    }

    Settings& Config() { return g_settings; }

    void Install(const WXL_Api* api)
    {
        g_hookedFade = api->HookAttach("LivingAzeroth.DoodadFade", kDoodadFade, reinterpret_cast<void*>(&hkFade),
                                       reinterpret_cast<void**>(&g_origFade), WXL_HOOK_DEFAULT_PRIORITY) != 0;
        g_hookedBatch = api->HookAttachByName("M2.DrawBatch", reinterpret_cast<void*>(&hkBatch), reinterpret_cast<void**>(&g_origBatch), WXL_HOOK_DEFAULT_PRIORITY) != 0 ||
                        api->HookAttachByName("M2.DrawBatch", reinterpret_cast<void*>(&hkBatch), reinterpret_cast<void**>(&g_origBatch), 1000) != 0;
        g_hookedDoodad = api->HookAttachByName("M2.DrawBatchDoodad", reinterpret_cast<void*>(&hkDoodad), reinterpret_cast<void**>(&g_origDoodad), WXL_HOOK_DEFAULT_PRIORITY) != 0 ||
                         api->HookAttachByName("M2.DrawBatchDoodad", reinterpret_cast<void*>(&hkDoodad), reinterpret_cast<void**>(&g_origDoodad), 1000) != 0;
        if (!g_hookedFade || !g_hookedBatch || !g_hookedDoodad)
            api->Log(WXL_LOG_WARN, kTag, "doodad dissolve: off, a hook failed (fade %d, batch %d, doodad %d)",
                     g_hookedFade ? 1 : 0, g_hookedBatch ? 1 : 0, g_hookedDoodad ? 1 : 0);
    }

    void Update()
    {
        // A new frame for the dissolve's notes.
        g_fadePrev.swap(g_fadeNow);
        g_fadeNow.clear();
        g_lastNoted = g_noted;
        g_lastSplit = g_split;
        g_noted = g_split = 0;

        volatile float* base = Row(kBaseDistance);
        volatile float* width = Row(kBaseFadeWidth);
        volatile float* dist = Row(kDistance);
        volatile float* distSq = Row(kDistanceSq);
        volatile float* fade = Row(kFadeStart);
        volatile float* fadeSq = Row(kFadeStartSq);

        // environmentDetail's scale, from category 1 (never changed here): runtime / base.
        if (base[1] > 0.0f && dist[1] > 0.0f) g_scale = dist[1] / base[1];

        // Dissolving, every category fades (the pop-in was only there to keep fading doodads fogged).
        const int fadeFrom = Dissolving() ? 0 : g_settings.fadeFrom;
        for (int k = 0; k < kCategories; ++k)
        {
            // Stock, as 0x78F570 builds it: category 0 unscaled, the fade width never scaled.
            float d = k == 0 ? base[0] : base[k] * g_scale;
            float w = width[k];
            if (k == 0 && g_settings.smallUseNext) { d = base[1] * g_scale; w = width[1]; }
            float start = d - w;
            if (k < fadeFrom) start = d; // no fade: full until the view distance, then gone
            if (dist[k] != d || distSq[k] != d * d || fade[k] != start || fadeSq[k] != start * start)
            {
                dist[k] = d;
                distSq[k] = d * d;
                fade[k] = start;
                fadeSq[k] = start * start;
            }
        }
    }

    bool EditPixelDissolve(std::string& src, std::string& why)
    {
        if (src.find("ps_3_0") == std::string::npos) { why = "not ps_3_0"; ++g_variantsLeft; return false; }
        static const std::regex clash(R"(\b(b12|c100)\b)");
        if (std::regex_search(src, clash))
        {
            why = "uses b12 or c100";
            ++g_variantsLeft;
            if (g_leftWhy.find(why) == std::string::npos) g_leftWhy += (g_leftWhy.empty() ? "" : "; ") + why;
            return false;
        }
        // Right after the last declaration: the block runs first, while no temporary holds anything yet (r0 is free).
        size_t insertAt = std::string::npos, pos = 0;
        while (pos < src.size())
        {
            size_t eol = src.find('\n', pos);
            if (eol == std::string::npos) eol = src.size();
            const size_t text = src.find_first_not_of(" \t", pos);
            if (text < eol && src.compare(text, 3, "dcl") == 0) insertAt = eol < src.size() ? eol + 1 : eol;
            pos = eol + 1;
        }
        if (insertAt == std::string::npos) { why = "no declarations"; ++g_variantsLeft; return false; }
        std::string block;
        if (src.find("vPos") == std::string::npos) block += "    dcl vPos.xy\n";
        // Keep the pixel if alpha > the dither's value there (0..1): texkill drops it where alpha - value < 0.
        block += "    if b12\n"
                 "      mul r0.xy, vPos, c100.yzzw\n"
                 "      add r0.x, r0.x, r0.y\n"
                 "      frc r0.x, r0.x\n"
                 "      mul r0.x, r0.x, c100.w\n"
                 "      frc r0.x, r0.x\n"
                 "      add r0, c100.x, -r0.x\n"
                 "      texkill r0\n"
                 "    endif\n";
        if (insertAt == src.size() && !src.empty() && src.back() != '\n')
        {
            src += '\n';
            insertAt = src.size();
        }
        src.insert(insertAt, block);
        ++g_variants;
        return true;
    }

    void DrawPanelSection(const WXL_Api* api)
    {
        if (!api->UiCollapsingHeader("Doodads fading in##ddfade")) return;
        api->UiCheckbox("dissolve in (drawn solid, so the fog covers them)##dddissolve", &g_settings.dissolve);
        api->UiCheckbox("small doodads (mugs, plates: fixed 30 yd) use the next size's view distance##ddsmall", &g_settings.smallUseNext);
        api->UiSliderInt(Dissolving() ? "fade only from size category (set aside while dissolving)##ddfrom"
                                      : "fade only from size category (0 stock, 5 none: doodads pop in, fogged)##ddfrom",
                         &g_settings.fadeFrom, 0, 5);
        char line[320];
        const volatile float* dist = Row(kDistance);
        const volatile float* fade = Row(kFadeStart);
        std::snprintf(line, sizeof(line), "view distance / fade start (yd), radius <1, <4, <15, <100, larger: %.0f/%.0f  %.0f/%.0f  %.0f/%.0f  %.0f/%.0f  %.0f/%.0f "
                                          "(environmentDetail scale %.2f)",
                      dist[0], fade[0], dist[1], fade[1], dist[2], fade[2], dist[3], fade[3], dist[4], fade[4], g_scale);
        ui::Text(api, line);
        if (!g_hookedFade || !g_hookedBatch || !g_hookedDoodad)
            std::snprintf(line, sizeof(line), "dissolve: off, a hook is missing (fade %d, batch %d, doodad %d)", g_hookedFade ? 1 : 0, g_hookedBatch ? 1 : 0, g_hookedDoodad ? 1 : 0);
        else
            std::snprintf(line, sizeof(line), "dissolve: %s; last frame %u doodads dissolving (%u of them drawn out of shared draws of copies); "
                                              "M2 pixel shader variants with it %u, without %u%s%s; dissolving draws so far %u",
                          Dissolving() ? "on" : (g_settings.dissolve ? "waiting for the shaders" : "off"), g_lastNoted, g_lastSplit, g_variants, g_variantsLeft, g_leftWhy.empty() ? "" : ": ", g_leftWhy.c_str(), g_draws);
        ui::Text(api, line);
    }
}
