#include "WmoIce.hpp"

#include "ModelIce.hpp"
#include "TerrainIce.hpp"

#include "game/Gx.hpp"

#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace wxl_livingazeroth::wmoice
{
    namespace
    {
        namespace gx = wxl::game::gx;

        // Core layout (offsets/game/WMO.hpp), and what the loops themselves read:
        //  Wmo.IntRender / Wmo.ExtRender: __thiscall(root, group, flag), ret 8; both read the root back as
        //   group+0x18C (and its MOHD at root+0x120) before choosing their path.
        //  Wmo.CullBatch: __cdecl(MOBA record), non-zero = culled; on the shader loop and on the 0x7A9380
        //   path alike, each batch passes it right before its material, effect bind and draw.
        //  MOBA +0x17: the material index (u8). MOMT: 0x40-byte records at root+0x160 (count root+0x19C);
        //   texture_1 at +0x0C is a byte offset into MOTX (root+0x124, byte size root+0x164).
        constexpr size_t kGroupRoot = 0x18C;
        constexpr size_t kRootMotx = 0x124, kRootMotxSize = 0x164, kRootMaterials = 0x160, kRootMaterialCount = 0x19C;
        constexpr size_t kMomtStride = 0x40, kMomtTexture1 = 0x0C, kMobaMaterial = 0x17;
        constexpr uint32_t kMaxMaterials = 4096, kMaxName = 260;
        constexpr DWORD kSampler = 14;

        // Textures that are ice through and through (waterfall-scan's WMO list, picked by hand). Left out:
        // stone, metal and trim with frost on them, reflection maps, and the icebreaker ship.
        const char* const kIceTextures[] = {
            // the Nexus and Icecrown
            "jlo_nex_ice_01", "jlo_nex_icewall_01", "jlo_nex_frozenwave", "jlo_nex_icicle_frozen", "nex_icefloor",
            "nex_rock_ice", "nex_rock_ice2", "nex_rock_icespike", "ic_nex_ice_01", "ic_rock01_ice", "ic_rock02_ice",
            "iceshard_noalpha", "iceshard_blue_noalpha", "iceshard2", "ce_iceshard",
            // caves (Storm Peaks, Winterspring, the ice caves)
            "jlo_mcaves_ice", "mcaves_ice_desat",
            // Azjol-Nerub and the Argent Coliseum
            "jlo_azjnub_ice", "jlo_azjnub_stalactite_ice_01", "ac_ice", "ac_icebottom",
            // Naxxramas's frost wyrm lair and the Scourge walls
            "jlo_udercity_ice", "jlo_udercity_icefloor", "jlo_udercity_icewall", "mm_udercity_icefloor", "mm_udercity_icewall",
            // cliffs, shards, bridges and icebergs built into buildings
            "kaskala_icecliff_01", "kaskala_icecliff_01dirty", "nd_utgarde_iceshard_01", "ul_uldr_ice_02",
            "frozenbridge01", "icicles0106", "dragonblight_iceberg_01", "dragonblight_shoreline_ice_01",
        };

        using RenderLeafFn = void(__fastcall*)(void* root, void* edx, void* group, int flag);
        using CullBatchFn = char(__cdecl*)(void* record);
        RenderLeafFn g_origInt = nullptr, g_origExt = nullptr;
        CullBatchFn  g_origCull = nullptr;
        bool g_hookedInt = false, g_hookedExt = false, g_hookedCull = false;

        bool g_enabled = true;
        int  g_depth = 0;                 // inside a batch loop
        bool g_on = false;                // the block is switched on for the current batch
        unsigned g_iceBatches = 0, g_roots = 0, g_iceRoots = 0;
        std::string g_status;

        struct RootIce
        {
            const void* motx = nullptr;
            const void* materials = nullptr;
            std::vector<uint8_t> ice;     // per material: 1 = ice
            bool any = false;
        };
        std::unordered_map<const void*, RootIce> g_cache;
        const RootIce* g_rootIce = nullptr;  // the current group's root, when it has ice materials

        // Raw reads, guarded: a root still loading, or freed under us, reads as "no ice".
        struct RootView { const char* motx; uint32_t motxSize; const uint8_t* materials; uint32_t count; };

        bool ReadRoot(const uint8_t* root, RootView& v)
        {
            __try
            {
                v.motx = *reinterpret_cast<const char* const*>(root + kRootMotx);
                v.motxSize = *reinterpret_cast<const uint32_t*>(root + kRootMotxSize);
                v.materials = *reinterpret_cast<const uint8_t* const*>(root + kRootMaterials);
                v.count = *reinterpret_cast<const uint32_t*>(root + kRootMaterialCount);
                return v.motx && v.materials && v.count > 0 && v.count <= kMaxMaterials;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // Material i's first texture name, copied out (bounded by MOTX's size).
        bool ReadName(const RootView& v, uint32_t i, char (&out)[kMaxName])
        {
            __try
            {
                const uint32_t at = *reinterpret_cast<const uint32_t*>(v.materials + i * kMomtStride + kMomtTexture1);
                if (at >= v.motxSize) return false;
                const uint32_t limit = std::min<uint32_t>(v.motxSize - at, kMaxName - 1);
                uint32_t n = 0;
                for (; n < limit && v.motx[at + n]; ++n) out[n] = v.motx[at + n];
                out[n] = 0;
                return n > 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool ReadMaterialIndex(const void* record, uint8_t& index)
        {
            __try { index = static_cast<const uint8_t*>(record)[kMobaMaterial]; return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        const uint8_t* RootOf(const void* group)
        {
            __try { return group ? *reinterpret_cast<const uint8_t* const*>(static_cast<const uint8_t*>(group) + kGroupRoot) : nullptr; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
        }

        // Which of the root's materials are ice, worked out once per root (again if its tables move).
        const RootIce* IceOf(const uint8_t* root)
        {
            RootView v{};
            if (!root || !ReadRoot(root, v)) return nullptr;
            if (g_cache.size() > 4096 && g_depth == 0) g_cache.clear(); // not under a loop that holds an entry
            RootIce& r = g_cache[root];
            if (r.motx != v.motx || r.materials != v.materials || r.ice.size() != v.count)
            {
                r.motx = v.motx; r.materials = v.materials; r.ice.assign(v.count, 0); r.any = false;
                char name[kMaxName];
                for (uint32_t i = 0; i < v.count; ++i)
                    if (ReadName(v, i, name) && IsIceTexture(name)) { r.ice[i] = 1; r.any = true; }
                ++g_roots;
                g_iceRoots += r.any ? 1 : 0;
            }
            return &r;
        }

        void SwitchOn()
        {
            auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice());
            IDirect3DTexture9* detail = dev ? terrainice::DetailTexture(dev) : nullptr;
            if (!detail) return;
            float look[4 * terrainice::kModelConstantCount];
            terrainice::FillModelConstants(look);
            float weight[4];
            modelice::StyleConstants(modelice::Style::Ice, terrainice::Strength(), look, weight);
            dev->SetPixelShaderConstantF(modelice::kWeightConstant, weight, 1);
            dev->SetPixelShaderConstantF(modelice::kLookConstant, look, terrainice::kModelConstantCount);
            dev->SetTexture(kSampler, detail);
            dev->SetSamplerState(kSampler, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(kSampler, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(kSampler, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(kSampler, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
            dev->SetSamplerState(kSampler, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
            dev->SetSamplerState(kSampler, D3DSAMP_SRGBTEXTURE, FALSE);
            const BOOL on = TRUE;
            dev->SetPixelShaderConstantB(modelice::kSwitch, &on, 1);
            g_on = true;
        }

        void SwitchOff()
        {
            if (!g_on) return;
            g_on = false;
            if (auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice()))
            {
                const BOOL off = FALSE;
                dev->SetPixelShaderConstantB(modelice::kSwitch, &off, 1);
            }
        }

        char __cdecl hkCull(void* record)
        {
            const char culled = g_origCull(record);
            if (!g_depth || culled) return culled;
            bool ice = false;
            if (const RootIce* r = g_rootIce)
            {
                uint8_t index = 0;
                ice = ReadMaterialIndex(record, index) && index < r->ice.size() && r->ice[index];
            }
            if (ice)
            {
                if (!g_on) SwitchOn();
                g_iceBatches += g_on ? 1 : 0;
            }
            else SwitchOff();
            return culled;
        }

        // Around one group's batch loop: its root's ice materials, looked up once (null when none).
        template <RenderLeafFn* Orig>
        void __fastcall hkLoop(void* root, void* edx, void* group, int flag)
        {
            const RootIce* saved = g_rootIce;
            g_rootIce = nullptr;
            if (g_enabled && modelice::Enabled() && modelice::PatchedVariants() > 0)
                if (const RootIce* r = IceOf(RootOf(group)); r && r->any) g_rootIce = r;
            ++g_depth;
            (*Orig)(root, edx, group, flag);
            SwitchOff();
            --g_depth;
            g_rootIce = saved;
        }
    }

    bool IsIceTexture(const char* path)
    {
        if (!path) return false;
        std::string p(path);
        for (char& c : p) c = c == '/' ? '\\' : static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        const size_t slash = p.rfind('\\');
        std::string base = slash == std::string::npos ? p : p.substr(slash + 1);
        if (base.size() > 4 && base.compare(base.size() - 4, 4, ".blp") == 0) base.resize(base.size() - 4);
        for (const char* t : kIceTextures)
            if (base == t) return true;
        return false;
    }

    void Install(const WXL_Api* api)
    {
        // Behind other detours on the same functions if they already hold the head (the bind probe, the core).
        auto attach = [api](const char* name, void* hook, void** orig) {
            return api->HookAttachByName(name, hook, orig, WXL_HOOK_DEFAULT_PRIORITY) != 0 ||
                   api->HookAttachByName(name, hook, orig, 1000) != 0;
        };
        g_hookedInt = attach("Wmo.IntRender", reinterpret_cast<void*>(&hkLoop<&g_origInt>), reinterpret_cast<void**>(&g_origInt));
        g_hookedExt = attach("Wmo.ExtRender", reinterpret_cast<void*>(&hkLoop<&g_origExt>), reinterpret_cast<void**>(&g_origExt));
        g_hookedCull = attach("Wmo.CullBatch", reinterpret_cast<void*>(&hkCull), reinterpret_cast<void**>(&g_origCull));
        if (!g_hookedInt || !g_hookedExt || !g_hookedCull)
            api->Log(WXL_LOG_WARN, "wxl-seyris-living-azeroth", "wmo ice: hook missing (interior loop %d, exterior loop %d, batch cull %d)",
                     g_hookedInt ? 1 : 0, g_hookedExt ? 1 : 0, g_hookedCull ? 1 : 0);
    }

    void SetEnabled(bool enabled) { g_enabled = enabled; }
    bool Enabled()                { return g_enabled; }

    const char* StatusLine()
    {
        char line[256];
        std::snprintf(line, sizeof(line), "ice in buildings: %s; buildings seen %u, with ice %u; ice batches drawn so far %u%s",
                      g_enabled ? "on" : "off", g_roots, g_iceRoots, g_iceBatches,
                      g_hookedInt && g_hookedExt && g_hookedCull ? "" : "; a hook is missing");
        g_status = line;
        return g_status.c_str();
    }
}
