#include "WmoIce.hpp"

#include "Glass.hpp"
#include "ModelIce.hpp"
#include "SurfaceCover.hpp"
#include "TerrainIce.hpp"

#include "game/Gx.hpp"

#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <cmath>
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
        //  Wmo.RenderGroup (0x7ABF50): __thiscall(root, groupIndex, instance + 0xB0, list), ret 0Ch; the only caller of
        //   both loops' function pointers (0x7ABFF0, 0x7AC011), called once per placed instance's visible group by the
        //   scene's WMO pass (0x7967AA: `add edi, 0B0h; push edi` with edi the instance whose root is [edi+0F4h]).
        //   The instance's placement matrix is at +0x70 (row-major, the translation in its last row: +0xA0).
        //  MOMT +0x00 flags (0x10 SIDN: the client lights it at night; 0x1 unlit: drawn at its texture's colour, day
        //  and night), +0x10 its night colour (BGRA).
        //  Root +0x1C: its WMO's path, inline.
        constexpr size_t kGroupRoot = 0x18C;
        constexpr size_t kGroupFlags = 0x30;   // MOGP flags, copied into the group object (offsets/game/WMO.hpp)
        constexpr size_t kRootMotx = 0x124, kRootMotxSize = 0x164, kRootMaterials = 0x160, kRootMaterialCount = 0x19C;
        constexpr size_t kMomtStride = 0x40, kMomtTexture1 = 0x0C, kMobaMaterial = 0x17;
        constexpr size_t kMomtFlags = 0x00, kMomtSidn = 0x10;
        constexpr uint32_t kMomtFlagSidn = 0x10, kMomtFlagUnlit = 0x1;
        constexpr size_t kRootPath = 0x1C;
        constexpr size_t kInstanceArg = 0xB0, kInstanceOrigin = 0xA0;
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
            // cliffs, shards, bridges and icebergs built into buildings; the icy cliffs are Kaskala's coast in the Borean
            // Tundra (snowy ice on top, crystal faces below: "some borean tundra coastal ice still looks really flat",
            // the user, 2026-10-11) and Lake Indu'le's in Dragonblight
            "kaskala_icecliff_01", "kaskala_icecliff_01dirty", "nd_utgarde_iceshard_01", "ul_uldr_ice_02",
            "icycliff_02", "icycliff_03",
            "frozenbridge01", "icicles0106", "dragonblight_iceberg_01", "dragonblight_shoreline_ice_01",
        };
        // Of those, Dragonblight's (the Crystal Vice, Wyrmrest's cliffs, the icebergs; the dirty cliff is the
        // Storm Peaks' copy of it) and the icy cliffs (whose models, Dragonblight's ice cliffs, are glacier already)
        // take the glacier look, and the shoreline floe texture the floe one.
        const char* const kGlacierTextures[] = {
            "kaskala_icecliff_01", "kaskala_icecliff_01dirty", "dragonblight_iceberg_01", "icycliff_02", "icycliff_03",
        };
        constexpr const char* kFloeTexture = "dragonblight_shoreline_ice_01";

        using RenderLeafFn = void(__fastcall*)(void* root, void* edx, void* group, int flag);
        using CullBatchFn = char(__cdecl*)(void* record);
        using RenderGroupFn = void(__fastcall*)(void* root, void* edx, int group, void* instanceArg, void* list);
        RenderLeafFn g_origInt = nullptr, g_origExt = nullptr;
        CullBatchFn  g_origCull = nullptr;
        RenderGroupFn g_origGroup = nullptr;
        bool g_hookedInt = false, g_hookedExt = false, g_hookedCull = false, g_hookedGroup = false;

        bool g_enabled = true;
        int  g_depth = 0;                 // inside a batch loop
        bool g_on = false;                // the block is switched on for the current batch
        modelice::Style g_onStyle = modelice::Style::None;  // with this style's constants
        unsigned g_iceBatches = 0, g_roots = 0, g_iceRoots = 0;
        std::string g_status;

        struct RootIce
        {
            const void* motx = nullptr;
            const void* materials = nullptr;
            std::vector<uint8_t> ice;     // per material: its modelice::Style (0 = not ice)
            std::vector<uint8_t> sand;    // per material: its texture is a floor with footprint rims (1 sand, 2 snow)
            std::vector<uint16_t> glass;  // per material: its row in the glass table + 1 (features/Glass), 0 = not glass
            std::vector<uint8_t> selfLit; // per material: 1 the client lights it at night (MOMT SIDN with a colour), 2 unlit
                                          // (MOMT 0x1: at full day and night), 0 neither
            bool any = false, anySand = false, anyGlass = false;
            uint32_t glassGeneration = 0; // the glass table these were looked up in
            uint32_t path = 0;            // glass::PathHash of the root's WMO path (0 unread)
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

        // Whether the client lights material i at night (MOMT SIDN, with a night colour): 1; else whether it draws it
        // unlit (MOMT 0x1: its texture's colour day and night, a window that always glows): 2; else 0.
        uint8_t ReadSelfLit(const RootView& v, uint32_t i)
        {
            __try
            {
                const uint8_t* m = v.materials + i * kMomtStride;
                const uint32_t flags = *reinterpret_cast<const uint32_t*>(m + kMomtFlags);
                if ((flags & kMomtFlagSidn) && (*reinterpret_cast<const uint32_t*>(m + kMomtSidn) & 0xFFFFFFu)) return 1;
                return (flags & kMomtFlagUnlit) ? 2 : 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        }

        // The root's WMO path (inline at +0x1C), hashed as GlassBuildings.cdbc's paths are.
        uint32_t ReadPathHash(const uint8_t* root)
        {
            char path[kMaxName];
            __try
            {
                uint32_t n = 0;
                for (; n < kMaxName - 1 && root[kRootPath + n]; ++n) path[n] = static_cast<char>(root[kRootPath + n]);
                path[n] = 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
            return glass::PathHash(path);
        }

        // Where an instance stands: its placement matrix's translation (finite, within the world).
        bool ReadOrigin(const uint8_t* instance, float pos[3])
        {
            __try
            {
                for (int k = 0; k < 3; ++k) pos[k] = reinterpret_cast<const float*>(instance + kInstanceOrigin)[k];
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
            for (int k = 0; k < 3; ++k)
                if (!std::isfinite(pos[k]) || std::fabs(pos[k]) > 40000.0f) return false;
            return true;
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
            if (r.motx != v.motx || r.materials != v.materials || r.ice.size() != v.count || r.glassGeneration != glass::Generation())
            {
                r.motx = v.motx; r.materials = v.materials; r.ice.assign(v.count, 0); r.sand.assign(v.count, 0); r.glass.assign(v.count, 0);
                r.selfLit.assign(v.count, 0);
                r.any = r.anySand = r.anyGlass = false;
                r.glassGeneration = glass::Generation();
                r.path = ReadPathHash(root);
                char name[kMaxName];
                for (uint32_t i = 0; i < v.count; ++i)
                    if (ReadName(v, i, name))
                    {
                        if (const modelice::Style style = StyleOfTexture(name); style != modelice::Style::None)
                        { r.ice[i] = static_cast<uint8_t>(style); r.any = true; }
                        if (IsSandTexture(name)) { r.sand[i] = 1; r.anySand = true; }
                        else if (IsSnowFloorTexture(name)) { r.sand[i] = 2; r.anySand = true; }
                        if (const uint32_t g = glass::MaterialOf(name))
                        {
                            r.glass[i] = static_cast<uint16_t>(g);
                            r.anyGlass = true;
                            r.selfLit[i] = ReadSelfLit(v, i);
                        }
                    }
                ++g_roots;
                g_iceRoots += r.any ? 1 : 0;
            }
            return &r;
        }

        void SwitchOn(modelice::Style style)
        {
            auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice());
            IDirect3DTexture9* detail = dev ? terrainice::DetailTexture(dev) : nullptr;
            if (!detail) return;
            float look[4 * terrainice::kModelConstantCount];
            terrainice::FillModelConstants(look);
            float weight[4], more[4];
            modelice::StyleConstants(style, terrainice::Strength(), look, weight, more);
            dev->SetPixelShaderConstantF(modelice::kWeightConstant, weight, 1);
            dev->SetPixelShaderConstantF(modelice::kLookConstant, look, terrainice::kModelConstantCount);
            dev->SetPixelShaderConstantF(modelice::kStyleConstant, more, 1);
            glass::ViewTaken();        // c180..c183 are the floor light's too
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
            g_onStyle = style;
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

        // The footprint rims (ModelIce's b8 block) for a sand or snow batch: the view (c180..c189, as for the ice), the
        // sun's world direction and the gains (c191), and the live rims on building floors (c192..). The slope gain
        // darkens a rim's side away from the sun and lightens the side toward it; the height gain lightens its crest.
        // Both follow the cover panel's ring slope and brightness (at their old defaults, 0.5 and 0.1: -0.35 and 0.08).
        constexpr float kRingSlopeGainPerSlope = -0.7f;
        constexpr float kRingHeightGainPerBrightness = 0.8f;
        bool g_ringsOn = false;
        unsigned g_ringBatches = 0;

        void RingsOff()
        {
            if (!g_ringsOn) return;
            g_ringsOn = false;
            if (auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice()))
            {
                const BOOL off = FALSE;
                dev->SetPixelShaderConstantB(modelice::kRingSwitch, &off, 1);
            }
        }

        void RingsOn(bool snow)
        {
            auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice());
            if (!dev) return;
            float rings[4 * cover::kWmoRingRegisters * modelice::kWmoRings] = {};   // a rim not in use stays zeros
            if (!cover::WmoRings(rings, modelice::kWmoRings, snow)) { RingsOff(); return; }
            float look[4 * terrainice::kModelConstantCount];
            terrainice::FillModelConstants(look);
            // c184 is the sun's direction in view space; the rows c181..c183 turn it to world.
            const float* l = look + 16;
            float sun[2] = { l[0] * look[4] + l[1] * look[5] + l[2] * look[6], l[0] * look[8] + l[1] * look[9] + l[2] * look[10] };
            const float n = std::sqrt(sun[0] * sun[0] + sun[1] * sun[1]);
            if (n > 1.0f) { sun[0] /= n; sun[1] /= n; }
            float slope = 0.0f, brightness = 0.0f;
            cover::RingShading(slope, brightness);
            const float control[4] = { sun[0], sun[1], kRingSlopeGainPerSlope * slope, kRingHeightGainPerBrightness * brightness };
            dev->SetPixelShaderConstantF(modelice::kLookConstant, look, terrainice::kModelConstantCount);
            dev->SetPixelShaderConstantF(modelice::kRingControl, control, 1);
            dev->SetPixelShaderConstantF(modelice::kRingConstant, rings, cover::kWmoRingRegisters * modelice::kWmoRings);
            glass::PaneRangeTaken();   // the floor light's panes share c101..c172
            const BOOL on = TRUE;
            dev->SetPixelShaderConstantB(modelice::kRingSwitch, &on, 1);
            g_ringsOn = true;
            ++g_ringBatches;
        }

        bool g_iceLoop = false;   // the ice and the rims are on for the current loop (else only the glass)

        char __cdecl hkCull(void* record)
        {
            const char culled = g_origCull(record);
            if (!g_depth || culled) return culled;
            modelice::Style style = modelice::Style::None;
            uint8_t floor = 0;   // 1 sand, 2 snow
            uint16_t glassRow = 0;
            bool selfLit = false, unlit = false;
            const RootIce* r = g_rootIce;
            if (r)
            {
                uint8_t index = 0;
                if (ReadMaterialIndex(record, index) && index < r->ice.size())
                {
                    if (g_iceLoop)
                    {
                        style = static_cast<modelice::Style>(r->ice[index]);
                        floor = r->sand[index];
                    }
                    glassRow = r->glass[index];
                    selfLit = r->selfLit[index] == 1;
                    unlit = r->selfLit[index] == 2;
                }
            }
            if (floor) RingsOn(floor == 2);
            else RingsOff();
            if (style != modelice::Style::None)
            {
                if (!g_on || g_onStyle != style) SwitchOn(style);
                g_iceBatches += g_on ? 1 : 0;
            }
            else SwitchOff();
            // The window glass and the floor light (features/Glass): its material, and whether the rims hold c101..
            if (r && r->anyGlass) glass::Batch(glassRow, g_ringsOn, selfLit, unlit);
            return culled;
        }

        // Around one placed instance's group (both loops run inside): which building it is, for the rooms behind its
        // glass (features/Glass). Only for roots with glass.
        void __fastcall hkRenderGroup(void* root, void* edx, int group, void* instanceArg, void* list)
        {
            bool began = false;
            if (glass::Config().enabled && glass::PatchedVariants() > 0 && g_depth == 0)
                if (const RootIce* r = IceOf(static_cast<const uint8_t*>(root)); r && r->anyGlass)
                {
                    const uint8_t* instance = instanceArg ? static_cast<const uint8_t*>(instanceArg) - kInstanceArg : nullptr;
                    float pos[3];
                    const bool have = instance && ReadOrigin(instance, pos);
                    const uintptr_t address = reinterpret_cast<uintptr_t>(instance);
                    glass::BeginInstance(have ? pos : nullptr, r->path, static_cast<uint32_t>(group),
                                         static_cast<uint32_t>(address ^ (address >> 16)) * 2654435761u);
                    began = true;
                }
            g_origGroup(root, edx, group, instanceArg, list);
            if (began) glass::EndInstance();
        }

        uint32_t ReadGroupFlags(const void* group)
        {
            __try { return group ? *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(group) + kGroupFlags) : 0; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        }

        // Around one group's batch loop: its root's ice and glass materials, looked up once (null when none).
        template <RenderLeafFn* Orig>
        void __fastcall hkLoop(void* root, void* edx, void* group, int flag)
        {
            const RootIce* saved = g_rootIce;
            const bool savedIce = g_iceLoop;
            g_rootIce = nullptr;
            g_iceLoop = g_enabled && modelice::Enabled() && modelice::PatchedVariants() > 0;
            const bool glassOn = glass::Config().enabled && glass::PatchedVariants() > 0;
            if (g_iceLoop || glassOn)
                if (const RootIce* r = IceOf(RootOf(group)); r && ((g_iceLoop && (r->any || r->anySand)) || (glassOn && r->anyGlass)))
                {
                    g_rootIce = r;
                    if (glassOn && r->anyGlass) glass::BeginGroup(ReadGroupFlags(group), true);
                }
            ++g_depth;
            (*Orig)(root, edx, group, flag);
            SwitchOff();
            RingsOff();
            if (g_rootIce && g_rootIce->anyGlass) glass::EndGroup();
            --g_depth;
            g_rootIce = saved;
            g_iceLoop = savedIce;
        }
    }

    bool IsIceTexture(const char* path) { return StyleOfTexture(path) != modelice::Style::None; }

    bool IsSandTexture(const char* path)
    {
        if (!path) return false;
        std::string p(path);
        for (char& c : p) c = c == '/' ? '\\' : static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        const size_t slash = p.rfind('\\');
        const std::string base = slash == std::string::npos ? p : p.substr(slash + 1);
        if (base.find("sand") == std::string::npos) return false;
        for (const char* no : { "stone", "rock", "wall", "brick", "trim", "pillar", "statue", "wet" })
            if (base.find(no) != std::string::npos) return false;
        return true;
    }

    bool IsSnowFloorTexture(const char* path)
    {
        if (!path) return false;
        std::string p(path);
        for (char& c : p) c = c == '/' ? '\\' : static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        const size_t slash = p.rfind('\\');
        const std::string base = slash == std::string::npos ? p : p.substr(slash + 1);
        for (const char* floor : { "jlo_mcaves_ground.blp", "jlo_mcaves_ground2.1.blp", "mcaves_ground2_desat.blp" })
            if (base == floor) return true;
        return false;
    }

    modelice::Style StyleOfTexture(const char* path)
    {
        if (!path) return modelice::Style::None;
        std::string p(path);
        for (char& c : p) c = c == '/' ? '\\' : static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        const size_t slash = p.rfind('\\');
        std::string base = slash == std::string::npos ? p : p.substr(slash + 1);
        if (base.size() > 4 && base.compare(base.size() - 4, 4, ".blp") == 0) base.resize(base.size() - 4);
        for (const char* t : kIceTextures)
            if (base == t)
            {
                if (base == kFloeTexture) return modelice::Style::Floe;
                for (const char* g : kGlacierTextures)
                    if (base == g) return modelice::Style::Glacier;
                return modelice::Style::Light;
            }
        return modelice::Style::None;
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
        // The rooms' buildings (without it every building's rooms are the default house's).
        g_hookedGroup = attach("Wmo.RenderGroup", reinterpret_cast<void*>(&hkRenderGroup), reinterpret_cast<void**>(&g_origGroup));
        if (!g_hookedInt || !g_hookedExt || !g_hookedCull || !g_hookedGroup)
            api->Log(WXL_LOG_WARN, "wxl-seyris-living-azeroth", "wmo ice: hook missing (interior loop %d, exterior loop %d, batch cull %d, group %d)",
                     g_hookedInt ? 1 : 0, g_hookedExt ? 1 : 0, g_hookedCull ? 1 : 0, g_hookedGroup ? 1 : 0);
    }

    void SetEnabled(bool enabled) { g_enabled = enabled; }
    bool Enabled()                { return g_enabled; }

    const char* StatusLine()
    {
        char line[256];
        std::snprintf(line, sizeof(line), "ice in buildings: %s; buildings seen %u, with ice %u; ice batches drawn so far %u; footprint rims on sand floors %u%s",
                      g_enabled ? "on" : "off", g_roots, g_iceRoots, g_iceBatches, g_ringBatches,
                      g_hookedInt && g_hookedExt && g_hookedCull ? (g_hookedGroup ? "" : "; the group hook is missing (rooms: the default house)") : "; a hook is missing");
        g_status = line;
        return g_status.c_str();
    }
}
