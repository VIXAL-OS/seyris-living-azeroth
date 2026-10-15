// Depth for the player's see-through parts.
//
// A mount's wing membranes are alpha-blended M2 batches (blend mode 2), which the client draws with depth writes off.
// The water's screen-space reflections (wxl-modern-water) march the depth buffer, so at a wing the depth was the
// scenery behind it while the colour was the wing's: gold patches in ponds far below a drake, and nothing of the
// wings in the water right under a low one, since the water could only guess where wings were from a region round the
// player ("now it also doesn't reflect my drakes' wings when they're *directly* over water lolsob", the user,
// 2026-10-15; "OH WAIT WATER REFLECTING DRAKES WINGS IS STILL BROKEN AT DISTANCE LOLSOB", the same day).
//
// Here, right after each alpha-blended batch of the player's own model family is drawn -- the root of its attachment
// chain (kInstParent) is the root of the player's body model: the rider, the mount, what they carry -- it is drawn
// again into the depth buffer only: colour writes off, depth writes on, alpha-tested at Settings::alphaRef so only the
// solid membrane counts. The pixel states are captured and put back around that draw, so the client's own state cache
// never sees it. The colour is untouched; things drawn after the wing and behind it (later see-through batches, the
// water) are hidden by it, as they would be by an opaque wing. Not while the player is faded (stealth, a ghost: the
// instance's own alpha below 1).
#include "PlayerDepth.hpp"

#include "../debug/Pages.hpp"

#include "game/Gx.hpp"
#include "game/World.hpp"

#include <windows.h>
#include <d3d9.h>

#include <cstdint>
#include <cstdio>

namespace wxl_livingazeroth::playerdepth
{
    namespace
    {
        namespace gx = wxl::game::gx;
        namespace gw = wxl::game::world;
        constexpr const char* kTag = "wxl-seyris-living-azeroth";

        // Client fields (wxl-core offsets: engine/Gx.hpp, game/M2.hpp, game/Unit.hpp).
        constexpr uintptr_t kCtxInstance = 0x60;   // M2 draw context -> the instance drawing (kDrawBatchCtxModelField)
        constexpr uintptr_t kCtxMaterial = 0x98;   // -> the batch's live material (kOffDrawCtxMaterial)
        constexpr uintptr_t kMaterialBlend = 0x02; // material -> blend mode, uint16: 0 opaque, 1 alpha key, 2 alpha, 3+ add/mod
        constexpr uintptr_t kInstParent = 0x48;    // instance -> parent instance, null at the root (kOffInstParent)
        constexpr uintptr_t kInstAlpha = 0x17C;    // instance -> its own alpha, float (kOffInstAlphaBase)
        constexpr uintptr_t kUnitModel = 0xB4;     // unit -> body model (kUnitModelField)
        constexpr uint16_t kBlendAlpha = 2;

        using DrawBatchFn = void(__fastcall*)(void* ctx, void* edx);
        using DeviceDrawFn = void(__fastcall*)(void* device, void* edx, uint32_t* batch, int indexed);
        DrawBatchFn g_origBatch = nullptr;
        DeviceDrawFn g_origDraw = nullptr;

        const WXL_Api* g_api = nullptr;
        Settings g_settings;
        bool g_hooked = false;
        void* g_playerRoot = nullptr; // this frame's root of the player's model family (compared, never read through)
        void* g_drawCtx = nullptr;    // the draw context of the player's batch drawing now (inside M2.DrawBatch)

        // The player's batches by blend mode (0..3: opaque, alpha key, alpha, additive and the rest) and the depth
        // draws added, counted over a frame; the last whole frame's for the panel.
        struct Counts { unsigned blend[4] = {}; unsigned depthDraws = 0; unsigned faded = 0; };
        Counts g_counting, g_last;
        char g_status[160] = "player depth: not started";

        bool ReadPtr(uintptr_t at, void*& out)
        {
            __try { out = *reinterpret_cast<void* const*>(at); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = nullptr; return false; }
        }

        bool ReadU16(uintptr_t at, uint16_t& out)
        {
            __try { out = *reinterpret_cast<const uint16_t*>(at); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; return false; }
        }

        bool ReadFloat(uintptr_t at, float& out)
        {
            __try { out = *reinterpret_cast<const float*>(at); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = 0.0f; return false; }
        }

        void* Root(void* instance)
        {
            for (int i = 0; instance && i < 8; ++i)
            {
                void* parent = nullptr;
                if (!ReadPtr(reinterpret_cast<uintptr_t>(instance) + kInstParent, parent) || !parent) return instance;
                instance = parent;
            }
            return instance;
        }

        void __fastcall hkBatch(void* ctx, void* edx)
        {
            void* previous = g_drawCtx;
            void* instance = nullptr;
            g_drawCtx = g_playerRoot && ctx && ReadPtr(reinterpret_cast<uintptr_t>(ctx) + kCtxInstance, instance) && instance &&
                        Root(instance) == g_playerRoot ? ctx : nullptr;
            g_origBatch(ctx, edx);
            g_drawCtx = previous;
        }

        void __fastcall hkDraw(void* device, void* edx, uint32_t* batch, int indexed)
        {
            g_origDraw(device, edx, batch, indexed);
            if (!g_drawCtx) return;
            const uintptr_t ctx = reinterpret_cast<uintptr_t>(g_drawCtx);
            void* material = nullptr;
            uint16_t blend = 0;
            if (!ReadPtr(ctx + kCtxMaterial, material) || !material || !ReadU16(reinterpret_cast<uintptr_t>(material) + kMaterialBlend, blend))
                return;
            ++g_counting.blend[blend < 3 ? blend : 3];
            if (!g_settings.enabled || blend != kBlendAlpha) return;
            void* instance = nullptr;
            float alpha = 1.0f;
            if (ReadPtr(ctx + kCtxInstance, instance) && instance && ReadFloat(reinterpret_cast<uintptr_t>(instance) + kInstAlpha, alpha) &&
                !(alpha >= 0.99f))
            {
                ++g_counting.faded;
                return;
            }
            auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice());
            if (!dev) return;
            IDirect3DStateBlock9* saved = nullptr;
            if (FAILED(dev->CreateStateBlock(D3DSBT_PIXELSTATE, &saved)) || !saved) return;
            dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
            dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
            dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
            dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
            dev->SetRenderState(D3DRS_ALPHAREF, static_cast<DWORD>(g_settings.alphaRef < 1 ? 1 : (g_settings.alphaRef > 255 ? 255 : g_settings.alphaRef)));
            g_origDraw(device, edx, batch, indexed);
            saved->Apply();
            saved->Release();
            ++g_counting.depthDraws;
        }

        void __cdecl Panel(void* /*user*/)
        {
            if (!g_api) return;
            g_api->UiCheckbox("Your mount's see-through parts (wings) write depth: the water reflects them, fog and clouds sit right",
                              &g_settings.enabled);
            g_api->UiSliderInt("Which of their pixels count as solid (alpha test, 0..255)", &g_settings.alphaRef, 16, 250);
            g_api->UiText(g_status);
            char line[200];
            std::snprintf(line, sizeof(line), "Your batches last frame: opaque %u, alpha key %u, alpha (see-through) %u, additive/other %u",
                          g_last.blend[0], g_last.blend[1], g_last.blend[2], g_last.blend[3]);
            g_api->UiText(line);
            std::snprintf(line, sizeof(line), "Depth added for %u of them%s", g_last.depthDraws,
                          g_last.faded ? " (some skipped: you're faded -- stealth or a ghost)" : "");
            g_api->UiText(line);
        }
    }

    Settings& Config() { return g_settings; }

    void Install(const WXL_Api* api)
    {
        g_api = api;
        if (!api || !api->HookAttachByName)
        {
            std::snprintf(g_status, sizeof(g_status), "player depth: off (no hook interface)");
            return;
        }
        const bool batch = api->HookAttachByName("M2.DrawBatch", reinterpret_cast<void*>(&hkBatch), reinterpret_cast<void**>(&g_origBatch),
                                                 WXL_HOOK_DEFAULT_PRIORITY) != 0 ||
                           api->HookAttachByName("M2.DrawBatch", reinterpret_cast<void*>(&hkBatch), reinterpret_cast<void**>(&g_origBatch), 1000) != 0;
        const bool draw = batch && (api->HookAttachByName("Gx.DeviceDraw", reinterpret_cast<void*>(&hkDraw), reinterpret_cast<void**>(&g_origDraw),
                                                          WXL_HOOK_DEFAULT_PRIORITY) != 0 ||
                                    api->HookAttachByName("Gx.DeviceDraw", reinterpret_cast<void*>(&hkDraw), reinterpret_cast<void**>(&g_origDraw), 1000) != 0);
        g_hooked = batch && draw;
        if (!g_hooked)
        {
            // A batch hook without its draw hook would only ever mark: harmless, but say so.
            std::snprintf(g_status, sizeof(g_status), "player depth: off (the %s hook failed)", batch ? "device draw" : "M2 batch draw");
            api->Log(WXL_LOG_WARN, kTag, "%s", g_status);
        }
        else
            std::snprintf(g_status, sizeof(g_status), "player depth: hooks in");
        pages::AddPanel("wxl-seyris-living-azeroth: player depth", &Panel);
    }

    void Update(bool inWorld)
    {
        g_last = g_counting;
        g_counting = Counts{};
        g_playerRoot = nullptr;
        if (!g_hooked || !inWorld) return;
        const unsigned long long guid = gw::ActivePlayerGuid();
        void* unit = guid ? gw::ResolveObject(guid, gw::kTypeMaskUnit) : nullptr;
        void* model = nullptr;
        if (!unit || !ReadPtr(reinterpret_cast<uintptr_t>(unit) + kUnitModel, model) || !model) return;
        g_playerRoot = Root(model);
    }

    const char* StatusLine() { return g_status; }
}
