#include "WmoIce.hpp"
#include "../debug/MemoryTag.hpp"

#include "Crystal.hpp"
#include "Glass.hpp"
#include "ModelIce.hpp"
#include "SeeThrough.hpp"
#include "CoverRooms.hpp"
#include "SurfaceCover.hpp"
#include "TerrainIce.hpp"
#include "WetSurfaces.hpp"
#include "../env/Shelter.hpp"
#include "../env/WetSurfaceModel.hpp"

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
        constexpr size_t kMomtFlags = 0x00, kMomtBlend = 0x08, kMomtSidn = 0x10;
        constexpr uint32_t kMomtFlagSidn = 0x10, kMomtFlagUnlit = 0x1;
        constexpr size_t kRootPath = 0x1C;
        constexpr size_t kInstanceArg = 0xB0, kInstanceOrigin = 0xA0, kInstanceMatrix = 0x70;
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
        uint16_t g_onCrystal = 0;         // ... or this crystal class's (features/Crystal), 0 = the ice's
        float g_onCrystalSize = 0.0f;     // ... for crystals this size
        bool g_crystalLoop = false;       // the crystal look is on for the current loop
        const void* g_group = nullptr;    // the group whose batch loop this is
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
            std::vector<uint16_t> crystal;// per material: its crystal class (features/Crystal: row + 1 | unlit), 0 = none
            bool anyCrystal = false;
            uint32_t crystalGeneration = 0; // the crystal table these were looked up in
            uint32_t path = 0;            // glass::PathHash of the root's WMO path (0 unread)
            std::vector<uint8_t> wet;     // per material: its wet shine (features/WetSurfaces) x 255, 0 = never wet-looking
            bool anyWet = false;
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

        // Material i's flags and blend mode (MOMT +0x00, +0x08: 0 opaque, 1 alpha-keyed, 2 alpha, 3.. additive and the rest).
        bool ReadBlend(const RootView& v, uint32_t i, uint32_t& flags, uint32_t& blend)
        {
            __try
            {
                const uint8_t* m = v.materials + i * kMomtStride;
                flags = *reinterpret_cast<const uint32_t*>(m + kMomtFlags);
                blend = *reinterpret_cast<const uint32_t*>(m + kMomtBlend);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
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

        // Its rotation: the placement matrix's first three rows (row-major, row vectors: world = (x, y, z, 1) M, so row i
        // is the WMO's axis i in the world); glass::IsRotation checks it is one.
        bool ReadAxes(const uint8_t* instance, float axes[9])
        {
            __try
            {
                for (int r = 0; r < 3; ++r)
                    for (int k = 0; k < 3; ++k) axes[3 * r + k] = reinterpret_cast<const float*>(instance + kInstanceMatrix)[4 * r + k];
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
            return glass::IsRotation(axes);
        }

        bool ReadMaterialIndex(const void* record, uint8_t& index)
        {
            __try { index = static_cast<const uint8_t*>(record)[kMobaMaterial]; return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // A crystal batch's crystals' size (features/Crystal's PieceRadius over its triangles), worked out the first time
        // it draws and kept by its MOBA record, checked against the record's range and material and its group (a group
        // unloaded and another loaded at the same place gets its own).
        constexpr size_t kMobaStartIndex = 0x0C, kMobaIndexCount = 0x10;
        struct CrystalSize { const void* group = nullptr; uint32_t start = 0; uint16_t count = 0; uint8_t material = 0; float size = 0.0f; };
        std::unordered_map<const void*, CrystalSize> g_crystalSizes;

        bool ReadBatchRange(const void* record, uint32_t& start, uint16_t& count)
        {
            __try
            {
                start = *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(record) + kMobaStartIndex);
                count = *reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(record) + kMobaIndexCount);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        float CrystalSizeOf(const void* record, uint8_t material)
        {
            uint32_t start = 0;
            uint16_t count = 0;
            if (!g_group || !ReadBatchRange(record, start, count)) return 0.0f;
            if (const auto it = g_crystalSizes.find(record); it != g_crystalSizes.end())
                if (const CrystalSize& c = it->second; c.group == g_group && c.start == start && c.count == count && c.material == material) return c.size;
            if (g_crystalSizes.size() > 16384) g_crystalSizes.clear();
            const float size = crystal::WmoBatchPieceRadius(g_group, record);
            g_crystalSizes[record] = { g_group, start, count, material, size };
            return size;
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
            if (r.motx != v.motx || r.materials != v.materials || r.ice.size() != v.count || r.glassGeneration != glass::Generation() ||
                r.crystalGeneration != crystal::Generation())
            {
                r.motx = v.motx; r.materials = v.materials; r.ice.assign(v.count, 0); r.sand.assign(v.count, 0); r.glass.assign(v.count, 0);
                r.selfLit.assign(v.count, 0);
                r.crystal.assign(v.count, 0);
                r.wet.assign(v.count, 0);
                r.any = r.anySand = r.anyGlass = r.anyCrystal = r.anyWet = false;
                r.glassGeneration = glass::Generation();
                r.crystalGeneration = crystal::Generation();
                r.path = ReadPathHash(root);
                char name[kMaxName];
                for (uint32_t i = 0; i < v.count; ++i)
                    if (ReadName(v, i, name))
                    {
                        if (const modelice::Style style = StyleOfTexture(name); style != modelice::Style::None)
                        { r.ice[i] = static_cast<uint8_t>(style); r.any = true; }
                        else if (uint32_t flags = 0, blend = 0; ReadBlend(v, i, flags, blend))
                            if (const uint16_t c = crystal::Classify(blend, flags, name)) { r.crystal[i] = c; r.anyCrystal = true; }
                        if (IsSandTexture(name)) { r.sand[i] = 1; r.anySand = true; }
                        else if (IsSnowFloorTexture(name)) { r.sand[i] = 2; r.anySand = true; }
                        if (const uint32_t g = glass::MaterialOf(name))
                        {
                            r.glass[i] = static_cast<uint16_t>(g);
                            r.anyGlass = true;
                            r.selfLit[i] = ReadSelfLit(v, i);
                        }
                        // Wet surfaces: what isn't ice, a crystal or glass, by its texture and blend mode.
                        else if (!r.ice[i] && !r.crystal[i])
                            if (uint32_t flags = 0, blend = 0; ReadBlend(v, i, flags, blend))
                                if (const float shine = wetsurfaces::BuildingShine(name, blend, flags); shine > 0.0f)
                                {
                                    r.wet[i] = static_cast<uint8_t>(std::lround(std::min(shine, 1.0f) * 255.0f));
                                    r.anyWet = r.anyWet || r.wet[i] > 0;
                                }
                    }
                ++g_roots;
                g_iceRoots += r.any ? 1 : 0;
            }
            return &r;
        }

        void BindDetail(IDirect3DDevice9* dev, IDirect3DTexture9* detail)
        {
            dev->SetTexture(kSampler, detail);
            dev->SetSamplerState(kSampler, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(kSampler, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(kSampler, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(kSampler, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
            dev->SetSamplerState(kSampler, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
            dev->SetSamplerState(kSampler, D3DSAMP_SRGBTEXTURE, FALSE);
        }

        // Wet surfaces (features/WetSurfaces): the block's facing constant (c77) is set for wet batches only, back to zeros
        // (the weight as it was) before anything else uses the block.
        bool    g_facingSet = false;
        uint8_t g_onWetShine = 0;
        bool    g_wetLoop = false;   // wet buildings for the current loop (an exterior group, wet weather)
        float   g_groupWet = -1.0f;  // the group being drawn: 1 exterior, its sky share for a room, -1 unknown (by its flags)
        float   g_loopWet = 0.0f;    // the batch loop's: g_groupWet, or by the group's own flags where that's unknown

        void FacingOff(IDirect3DDevice9* dev)
        {
            if (!g_facingSet || !dev) return;
            const float zero[4] = {};
            dev->SetPixelShaderConstantF(wetsurf::kFacingConstant, zero, 1);
            g_facingSet = false;
        }

        void SwitchOn(modelice::Style style)
        {
            auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice());
            IDirect3DTexture9* detail = dev ? terrainice::DetailTexture(dev) : nullptr;
            if (!detail) return;
            FacingOff(dev);
            float look[4 * terrainice::kModelConstantCount];
            terrainice::FillModelConstants(look);
            float weight[4], more[4];
            modelice::StyleConstants(style, terrainice::Strength(), look, weight, more);
            dev->SetPixelShaderConstantF(modelice::kWeightConstant, weight, 1);
            dev->SetPixelShaderConstantF(modelice::kLookConstant, look, terrainice::kModelConstantCount);
            dev->SetPixelShaderConstantF(modelice::kStyleConstant, more, 1);
            const float noCrystal[4 * crystal::kRegisters] = {};
            dev->SetPixelShaderConstantF(crystal::kConstant, noCrystal, crystal::kRegisters);
            glass::ViewTaken();        // c180..c183 are the floor light's too
            BindDetail(dev, detail);
            const BOOL on = TRUE;
            dev->SetPixelShaderConstantB(modelice::kSwitch, &on, 1);
            g_on = true;
            g_onStyle = style;
            g_onCrystal = 0;
        }

        void SwitchOff();

        // The same block for a crystal material (features/Crystal): its constants, for its crystals' size, and the crystal
        // detail texture.
        void SwitchOnCrystal(uint16_t cls, float size)
        {
            auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice());
            const crystal::Material* m = crystal::MaterialAt(cls & ~crystal::kUnlitBit);
            IDirect3DTexture9* detail = dev && m ? crystal::DetailTexture(dev) : nullptr;
            if (!detail) { SwitchOff(); return; }
            FacingOff(dev);
            float look[4 * terrainice::kModelConstantCount];
            terrainice::FillModelConstants(look);
            float weight[4], more[4], c[4 * crystal::kRegisters];
            crystal::Constants(*m, crystal::Config(), crystal::Night(), (cls & crystal::kUnlitBit) != 0, look, weight, more, c, size);
            dev->SetPixelShaderConstantF(modelice::kWeightConstant, weight, 1);
            dev->SetPixelShaderConstantF(modelice::kLookConstant, look, terrainice::kModelConstantCount);
            dev->SetPixelShaderConstantF(modelice::kStyleConstant, more, 1);
            dev->SetPixelShaderConstantF(crystal::kConstant, c, crystal::kRegisters);
            glass::ViewTaken();
            BindDetail(dev, detail);
            const BOOL on = TRUE;
            dev->SetPixelShaderConstantB(modelice::kSwitch, &on, 1);
            g_on = true;
            g_onStyle = modelice::Style::None;
            g_onCrystal = cls;
            g_onCrystalSize = size;
        }

        void SwitchOff()
        {
            if (!g_on) return;
            g_on = false;
            if (auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice()))
            {
                const BOOL off = FALSE;
                dev->SetPixelShaderConstantB(modelice::kSwitch, &off, 1);
                FacingOff(dev);
            }
        }

        // The same block for a wet batch (features/WetSurfaces): its style's constants for its texture's shine, the facing,
        // a 1 x 1 detail (the wet style reads none of it).
        void SwitchOnWet(uint8_t shine)
        {
            if (g_on && g_onStyle == modelice::Style::Wet && g_onWetShine == shine) return;
            auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice());
            IDirect3DTexture9* detail = dev ? wetsurfaces::BuildingDetail(dev) : nullptr;
            if (!detail) { SwitchOff(); return; }
            float look[4 * terrainice::kModelConstantCount];
            terrainice::FillModelConstants(look);
            float weight[4], more[4], facing[4];
            wetsurfaces::BuildingConstants(shine / 255.0f, look, weight, more, facing);
            dev->SetPixelShaderConstantF(modelice::kWeightConstant, weight, 1);
            dev->SetPixelShaderConstantF(modelice::kLookConstant, look, terrainice::kModelConstantCount);
            dev->SetPixelShaderConstantF(modelice::kStyleConstant, more, 1);
            const float noCrystal[4 * crystal::kRegisters] = {};
            dev->SetPixelShaderConstantF(crystal::kConstant, noCrystal, crystal::kRegisters);
            dev->SetPixelShaderConstantF(wetsurf::kFacingConstant, facing, 1);
            g_facingSet = true;
            glass::ViewTaken();        // c180..c183 are the floor light's too
            BindDetail(dev, detail);
            const BOOL on = TRUE;
            dev->SetPixelShaderConstantB(modelice::kSwitch, &on, 1);
            g_on = true;
            g_onStyle = modelice::Style::Wet;
            g_onCrystal = 0;
            g_onWetShine = shine;
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
            memuse::Scope memory(memuse::kIce);
            const char culled = g_origCull(record);
            if (!g_depth || culled) return culled;
            modelice::Style style = modelice::Style::None;
            uint8_t floor = 0;   // 1 sand, 2 snow
            uint16_t glassRow = 0, crystalClass = 0;
            uint8_t wet = 0;
            bool selfLit = false, unlit = false;
            const RootIce* r = g_rootIce;
            uint8_t index = 0;
            if (r)
            {
                if (ReadMaterialIndex(record, index) && index < r->ice.size())
                {
                    if (g_iceLoop)
                    {
                        style = static_cast<modelice::Style>(r->ice[index]);
                        floor = r->sand[index];
                    }
                    if (g_crystalLoop && style == modelice::Style::None) crystalClass = r->crystal[index];
                    glassRow = r->glass[index];
                    selfLit = r->selfLit[index] == 1;
                    unlit = r->selfLit[index] == 2;
                    if (g_wetLoop && !glassRow && !floor) wet = static_cast<uint8_t>(std::lround(r->wet[index] * std::min(g_loopWet, 1.0f)));
                }
            }
            if (floor) RingsOn(floor == 2);
            else RingsOff();
            if (style != modelice::Style::None)
            {
                if (!g_on || g_onStyle != style || g_onCrystal) SwitchOn(style);
                g_iceBatches += g_on ? 1 : 0;
            }
            else if (crystalClass)
            {
                // The facets grow with this batch's crystals (the Exodar's giant ones: 100-200 yd).
                const float size = CrystalSizeOf(record, index);
                if (!g_on || g_onCrystal != crystalClass || g_onCrystalSize != size) SwitchOnCrystal(crystalClass, size);
                if (g_on) crystal::NoteBatch(true);
            }
            else if (wet)
            {
                SwitchOnWet(wet);
                if (g_on) wetsurfaces::NoteBuildingBatch();
            }
            else SwitchOff();
            // See-through windows (features/SeeThrough): a glass batch of an opened group may be drawn later, over the room.
            seethrough::Batch(g_group, record, glassRow);
            // The window glass and the floor light (features/Glass): its material, and whether the rims hold c101..
            if (r && r->anyGlass) glass::Batch(glassRow, g_ringsOn, selfLit, unlit);
            return culled;
        }

        // Wet buildings in a room's group (features/WetSurfaces). Stormwind's streets, canals and squares are interior groups
        // (MOGI 0x2000) as its shops are, so a room is wet only as far as the roof checks (env/Shelter) see the sky over its
        // floor: nine points over its box (MOGI, cover::rooms' guarded layout), 1.5 yd above its bottom, in the world through
        // the instance's placement. Cached per instance and group for kRoomRecheck seconds; while the roof checks' budget is
        // spent (they'd answer "open"), not cached and dry. 0..1: its batches' wet weight x this.
        constexpr double kRoomRecheck = 30.0;
        struct RoomOpen { float share = 0.0f; double at = -1.0e9; };
        std::unordered_map<uint64_t, RoomOpen> g_roomOpen;
        int   g_roomGuards = 0;          // 0 unchecked, 1 hold, -1 differ (rooms stay dry)
        unsigned g_roomsChecked = 0, g_roomsOpen = 0;

        bool ReadRaw(const void* src, void* dst, size_t n)
        {
            __try { std::memcpy(dst, src, n); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        float GroupWet(const void* root, int group, const void* instanceArg)
        {
            namespace rm = cover::rooms;
            if (!root || group < 0 || !instanceArg) return -1.0f;
            const auto* r = static_cast<const uint8_t*>(root);
            const void* infos = nullptr;
            uint32_t count = 0;
            uint8_t info[rm::kGroupInfoStride];
            if (!ReadRaw(r + rm::kRootGroupInfo, &infos, sizeof(infos)) || !ReadRaw(r + rm::kRootGroupCount, &count, sizeof(count)) || !infos ||
                static_cast<uint32_t>(group) >= count || count > rm::kMaxGroups ||
                !ReadRaw(static_cast<const uint8_t*>(infos) + static_cast<size_t>(group) * rm::kGroupInfoStride, info, sizeof(info)))
                return -1.0f;
            uint32_t flags;
            float box[6];
            std::memcpy(&flags, info, 4);
            std::memcpy(box, info + 4, sizeof(box));
            if (!(flags & rm::kInteriorGroup)) return 1.0f;
            if (g_roomGuards == 0)
            {
                g_roomGuards = 1;
                for (const coverprints::Guard& gd : rm::kGuards)
                {
                    uint8_t bytes[24];
                    if (!ReadRaw(reinterpret_cast<const void*>(gd.address), bytes, gd.size) || std::memcmp(bytes, gd.bytes, gd.size) != 0) { g_roomGuards = -1; break; }
                }
            }
            if (g_roomGuards < 0) return 0.0f;
            const uint64_t key = (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(instanceArg)) << 12) ^ static_cast<uint64_t>(group);
            const double now = static_cast<double>(GetTickCount64()) / 1000.0;
            RoomOpen& cached = g_roomOpen[key];
            if (now - cached.at < kRoomRecheck) return cached.share;
            const uint8_t* instance = static_cast<const uint8_t*>(instanceArg) - kInstanceArg;
            float origin[3], axes[9];
            if (!ReadOrigin(instance, origin) || !ReadAxes(instance, axes)) return cached.share;
            for (int k = 0; k < 6; ++k) if (!std::isfinite(box[k])) return 0.0f;
            const unsigned deferred = shelter::GetStats().deferredThisFrame;
            float open = 0.0f;
            for (int j = 0; j < 3; ++j)
                for (int i = 0; i < 3; ++i)
                {
                    const float p[3] = { box[0] + (box[3] - box[0]) * (i + 0.5f) / 3.0f, box[1] + (box[4] - box[1]) * (j + 0.5f) / 3.0f, box[2] + 1.5f };
                    float w[3];
                    for (int k = 0; k < 3; ++k) w[k] = origin[k] + p[0] * axes[k] + p[1] * axes[3 + k] + p[2] * axes[6 + k];
                    open += shelter::Openness(w);
                }
            if (shelter::GetStats().deferredThisFrame != deferred) return cached.at > -1.0e8 ? cached.share : 0.0f;  // asked again later
            const float share = open / 9.0f;
            cached.share = std::clamp((share - 0.15f) / 0.35f, 0.0f, 1.0f);
            cached.at = now;
            ++g_roomsChecked;
            g_roomsOpen += cached.share > 0.0f ? 1u : 0u;
            if (g_roomOpen.size() > 8192) g_roomOpen.clear();
            return cached.share;
        }

        // Around one placed instance's group (both loops run inside): which building it is, for the rooms behind its
        // glass (features/Glass; only for roots with glass), the windows opened in it (features/SeeThrough) and the
        // rooms the surface cover keeps out of (features/SurfaceCoverRooms); for wet buildings, how open to the sky it is.
        void __fastcall hkRenderGroup(void* root, void* edx, int group, void* instanceArg, void* list)
        {
            memuse::Scope memory(memuse::kIce);
            const float savedGroupWet = g_groupWet;
            g_groupWet = wetsurfaces::BuildingsActive() ? GroupWet(root, group, instanceArg) : -1.0f;
            bool began = false;
            if (glass::Config().enabled && glass::PatchedVariants() > 0 && g_depth == 0)
                if (const RootIce* r = IceOf(static_cast<const uint8_t*>(root)); r && r->anyGlass)
                {
                    const uint8_t* instance = instanceArg ? static_cast<const uint8_t*>(instanceArg) - kInstanceArg : nullptr;
                    float pos[3], axes[9];
                    const bool have = instance && ReadOrigin(instance, pos);
                    const bool turned = have && ReadAxes(instance, axes);
                    const uintptr_t address = reinterpret_cast<uintptr_t>(instance);
                    glass::BeginInstance(have ? pos : nullptr, turned ? axes : nullptr, r->path, static_cast<uint32_t>(group),
                                         static_cast<uint32_t>(address ^ (address >> 16)) * 2654435761u);
                    began = true;
                }
            seethrough::BeginGroupDraw(root, group, instanceArg);   // which instance's group: its opened windows
            cover::NoteBuilding(root, instanceArg);                 // the surface cover keeps out of its rooms
            g_origGroup(root, edx, group, instanceArg, list);
            seethrough::EndGroupDraw();
            if (began) glass::EndInstance();
            g_groupWet = savedGroupWet;
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
            memuse::Scope memory(memuse::kIce);
            const RootIce* saved = g_rootIce;
            const void* savedGroup = g_group;
            const bool savedIce = g_iceLoop, savedCrystal = g_crystalLoop, savedWet = g_wetLoop;
            const float savedLoopWet = g_loopWet;
            g_rootIce = nullptr;
            g_group = group;
            g_iceLoop = g_enabled && modelice::Enabled() && modelice::PatchedVariants() > 0;
            g_crystalLoop = crystal::Active() && modelice::PatchedVariants() > 0;
            // Wet buildings: an exterior group's batches, a room's as far as it's open to the sky (GroupWet, from the group
            // draw around this loop: Stormwind's streets yes, its shops no).
            g_loopWet = g_groupWet >= 0.0f ? g_groupWet : ((ReadGroupFlags(group) & 0x2000u) ? 0.0f : 1.0f);
            g_wetLoop = wetsurfaces::BuildingsActive() && g_loopWet > 0.0f;
            const bool glassOn = glass::Config().enabled && glass::PatchedVariants() > 0;
            if (g_iceLoop || g_crystalLoop || glassOn || g_wetLoop)
                if (const RootIce* r = IceOf(RootOf(group));
                    r && ((g_iceLoop && (r->any || r->anySand)) || (g_crystalLoop && r->anyCrystal) || (glassOn && r->anyGlass) ||
                          (g_wetLoop && r->anyWet)))
                {
                    g_rootIce = r;
                    if (glassOn && r->anyGlass) glass::BeginGroup(ReadGroupFlags(group), true);
                }
            ++g_depth;
            (*Orig)(root, edx, group, flag);
            seethrough::EndLoop();
            SwitchOff();
            RingsOff();
            if (g_rootIce && g_rootIce->anyGlass) glass::EndGroup();
            --g_depth;
            g_rootIce = saved;
            g_group = savedGroup;
            g_iceLoop = savedIce;
            g_crystalLoop = savedCrystal;
            g_wetLoop = savedWet;
            g_loopWet = savedLoopWet;
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
        char line[384];
        std::snprintf(line, sizeof(line), "ice in buildings: %s; buildings seen %u, with ice %u; ice batches drawn so far %u; footprint rims on sand floors %u; "
                      "wet buildings: rooms open to the sky %u of %u checked%s%s",
                      g_enabled ? "on" : "off", g_roots, g_iceRoots, g_iceBatches, g_ringBatches, g_roomsOpen, g_roomsChecked,
                      g_roomGuards < 0 ? " (the rooms' layout differs: rooms stay dry)" : "",
                      g_hookedInt && g_hookedExt && g_hookedCull ? (g_hookedGroup ? "" : "; the group hook is missing (rooms: the default house)") : "; a hook is missing");
        g_status = line;
        return g_status.c_str();
    }
}
