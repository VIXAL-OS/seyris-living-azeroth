#include "Glass.hpp"

#include "../env/CdbcLoad.hpp"
#include "../env/Climate.hpp"
#include "../env/Fog.hpp"
#include "../env/Storm.hpp"
#include "../env/WorldQuery.hpp"
#include "../wxl_seyris/CdbcApi.hpp"
#include "GlassShafts.hpp"
#include "SurfaceCover.hpp"
#include "Windows.hpp"

#include "game/Camera.hpp"
#include "game/Gfx.hpp"
#include "game/Gx.hpp"

#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace wxl_livingazeroth::glass
{
    namespace
    {
        namespace gx = wxl::game::gx;

        // Client globals (as GroundFog and SunShafts read them).
        constexpr uintptr_t kInteriorBlend = 0x00D38B9C;
        constexpr uintptr_t kFogColor = 0x00D38BF4;

        constexpr const char* kMaterialsFile = "DBFilesClient\\GlassMaterials.cdbc";
        constexpr const char* kPanesFile = "DBFilesClient\\GlassPanes.cdbc";
        // Column layouts (append-only, docs/cdbc-tables.md; tools/gen_glass.py GLASS_MATERIALS, GLASS_PANES).
        constexpr WXL_SeyrisCdbcField kMaterialFields[] = {
            {"ID", 0, WXL_CDBC_FIELD_VALUE},     {"Texture", 1, WXL_CDBC_FIELD_STRING}, {"Kind", 2, WXL_CDBC_FIELD_VALUE},
            {"Flags", 3, WXL_CDBC_FIELD_VALUE},  {"ColorR", 4, WXL_CDBC_FIELD_VALUE},   {"ColorG", 5, WXL_CDBC_FIELD_VALUE},
            {"ColorB", 6, WXL_CDBC_FIELD_VALUE}, {"LightR", 7, WXL_CDBC_FIELD_VALUE},   {"LightG", 8, WXL_CDBC_FIELD_VALUE},
            {"LightB", 9, WXL_CDBC_FIELD_VALUE}, {"MaskR", 10, WXL_CDBC_FIELD_VALUE},   {"MaskG", 11, WXL_CDBC_FIELD_VALUE},
            {"MaskB", 12, WXL_CDBC_FIELD_VALUE}, {"MaskBias", 13, WXL_CDBC_FIELD_VALUE}, {"Share", 14, WXL_CDBC_FIELD_VALUE},
            {"Comment", 15, WXL_CDBC_FIELD_STRING},
        };
        constexpr WXL_SeyrisCdbcDefinition kMaterialDef = { "GlassMaterials", kMaterialsFile, kMaterialFields, 16 };
        constexpr WXL_SeyrisCdbcField kPaneFields[] = {
            {"ID", 0, WXL_CDBC_FIELD_VALUE},         {"MapID", 1, WXL_CDBC_FIELD_VALUE},     {"WmoID", 2, WXL_CDBC_FIELD_VALUE},
            {"PosX", 3, WXL_CDBC_FIELD_VALUE},       {"PosY", 4, WXL_CDBC_FIELD_VALUE},      {"PosZ", 5, WXL_CDBC_FIELD_VALUE},
            {"OutX", 6, WXL_CDBC_FIELD_VALUE},       {"OutY", 7, WXL_CDBC_FIELD_VALUE},      {"OutZ", 8, WXL_CDBC_FIELD_VALUE},
            {"AlongX", 9, WXL_CDBC_FIELD_VALUE},     {"AlongY", 10, WXL_CDBC_FIELD_VALUE},   {"AlongZ", 11, WXL_CDBC_FIELD_VALUE},
            {"HalfWidth", 12, WXL_CDBC_FIELD_VALUE}, {"HalfHeight", 13, WXL_CDBC_FIELD_VALUE},
            {"LightR", 14, WXL_CDBC_FIELD_VALUE},    {"LightG", 15, WXL_CDBC_FIELD_VALUE},   {"LightB", 16, WXL_CDBC_FIELD_VALUE},
            {"Kind", 17, WXL_CDBC_FIELD_VALUE},
            {"RoomMinX", 18, WXL_CDBC_FIELD_VALUE},  {"RoomMinY", 19, WXL_CDBC_FIELD_VALUE}, {"RoomMinZ", 20, WXL_CDBC_FIELD_VALUE},
            {"RoomMaxX", 21, WXL_CDBC_FIELD_VALUE},  {"RoomMaxY", 22, WXL_CDBC_FIELD_VALUE}, {"RoomMaxZ", 23, WXL_CDBC_FIELD_VALUE},
            {"Flags", 24, WXL_CDBC_FIELD_VALUE},     {"Comment", 25, WXL_CDBC_FIELD_STRING},
        };
        constexpr WXL_SeyrisCdbcDefinition kPaneDef = { "GlassPanes", kPanesFile, kPaneFields, 26 };
        // The rooms behind the glass (tools/gen_glass.py ROOM_STYLES, GLASS_BUILDINGS).
        constexpr const char* kStylesFile = "DBFilesClient\\RoomStyles.cdbc";
        constexpr const char* kBuildingsFile = "DBFilesClient\\GlassBuildings.cdbc";
        constexpr WXL_SeyrisCdbcField kStyleFields[] = {
            {"ID", 0, WXL_CDBC_FIELD_VALUE},          {"Class", 1, WXL_CDBC_FIELD_VALUE},       {"Palette", 2, WXL_CDBC_FIELD_VALUE},
            {"TintR", 3, WXL_CDBC_FIELD_VALUE},       {"TintG", 4, WXL_CDBC_FIELD_VALUE},       {"TintB", 5, WXL_CDBC_FIELD_VALUE},
            {"Tint2R", 6, WXL_CDBC_FIELD_VALUE},      {"Tint2G", 7, WXL_CDBC_FIELD_VALUE},      {"Tint2B", 8, WXL_CDBC_FIELD_VALUE},
            {"LampR", 9, WXL_CDBC_FIELD_VALUE},       {"LampG", 10, WXL_CDBC_FIELD_VALUE},      {"LampB", 11, WXL_CDBC_FIELD_VALUE},
            {"CurtainR", 12, WXL_CDBC_FIELD_VALUE},   {"CurtainG", 13, WXL_CDBC_FIELD_VALUE},   {"CurtainB", 14, WXL_CDBC_FIELD_VALUE},
            {"Brightness", 15, WXL_CDBC_FIELD_VALUE}, {"Spread", 16, WXL_CDBC_FIELD_VALUE},     {"DarkShare", 17, WXL_CDBC_FIELD_VALUE},
            {"Shelves", 18, WXL_CDBC_FIELD_VALUE},    {"Curtains", 19, WXL_CDBC_FIELD_VALUE},   {"Divider", 20, WXL_CDBC_FIELD_VALUE},
            {"Plain", 21, WXL_CDBC_FIELD_VALUE},      {"LateHours", 22, WXL_CDBC_FIELD_VALUE},  {"NightCap", 23, WXL_CDBC_FIELD_VALUE},
            {"Comment", 24, WXL_CDBC_FIELD_STRING},
        };
        constexpr WXL_SeyrisCdbcDefinition kStyleDef = { "RoomStyles", kStylesFile, kStyleFields, 25 };
        constexpr WXL_SeyrisCdbcField kBuildingFields[] = {
            {"ID", 0, WXL_CDBC_FIELD_VALUE},     {"MapID", 1, WXL_CDBC_FIELD_VALUE},  {"UniqueID", 2, WXL_CDBC_FIELD_VALUE},
            {"PosX", 3, WXL_CDBC_FIELD_VALUE},   {"PosY", 4, WXL_CDBC_FIELD_VALUE},   {"PosZ", 5, WXL_CDBC_FIELD_VALUE},
            {"Group", 6, WXL_CDBC_FIELD_VALUE},  {"Style", 7, WXL_CDBC_FIELD_VALUE},  {"Seed", 8, WXL_CDBC_FIELD_VALUE},
            {"Flags", 9, WXL_CDBC_FIELD_VALUE},  {"AreaID", 10, WXL_CDBC_FIELD_VALUE}, {"Wmo", 11, WXL_CDBC_FIELD_STRING},
            {"Comment", 12, WXL_CDBC_FIELD_STRING},
        };
        constexpr WXL_SeyrisCdbcDefinition kBuildingDef = { "GlassBuildings", kBuildingsFile, kBuildingFields, 13 };

        // Each kind's look (features/GlassShader's constants): the room behind it (its share), the glass's thickness
        // (yd), saturation seen from inside, Fresnel strength, darkening outside, glint, F0, diffusion (frosted: the
        // sun's direction matters less), brightness seen from inside, and how much of the sun gets through to the floor.
        struct Look { float room, thickness, saturation, fresnel, darken, glint, f0, diffusion, inside, through; };
        // F0 is above real glass's 0.04: without the eye's range, the sky's 4% beside a sunlit wall reads as no reflection.
        constexpr Look kClear = { 0.65f, 0.0f, 1.1f, 1.0f, 0.55f, 1.0f, 0.12f, 0.1f, 1.5f, 1.0f };
        constexpr Look kLeadedLook = { 0.5f, 0.012f, 1.15f, 0.9f, 0.5f, 0.8f, 0.1f, 0.2f, 1.45f, 0.85f };
        constexpr Look kStainedLook = { 0.0f, 0.035f, 1.35f, 0.6f, 0.45f, 0.6f, 0.08f, 0.3f, 1.9f, 0.8f };
        constexpr Look kFrostedLook = { 0.0f, 0.0f, 1.0f, 0.35f, 0.15f, 0.25f, 0.05f, 0.85f, 1.05f, 0.55f };

        const Look& LookOf(uint32_t kind)
        {
            switch (kind)
            {
            case kStained: return kStainedLook;
            case kFrosted: return kFrostedLook;
            case kLeaded: return kLeadedLook;
            default: return kClear;
            }
        }

        // The room behind clear glass: cells 3.5 x 3.2 yd, 4 yd deep, a warm tint, lit by the day (a building's style
        // changes the tint and the light: RoomBatchConstants).
        constexpr float kCellWidth = 3.5f, kCellHeight = 3.2f, kRoomDepth = 4.0f, kRoomLight = 0.32f;
        constexpr float kRoomTint[3] = { 0.85f, 0.72f, 0.55f };
        constexpr float kGlowHidden = 0.75f;   // at night a dark room hides this much of the client's lit window in front of it (all of the window)
        // The floor light: the reach along the sun's ray behind a pane, the soft edges (1 / the share of the half
        // sizes), the far end's softness, and the gain.
        constexpr float kReach = 30.0f, kSoft = 6.0f, kFarSoft = 4.0f, kFloorGain = 1.2f;
        constexpr float kRoomMargin = 0.5f;   // yd the room's box is widened by (walls sit on its faces)

        Settings g_settings;
        std::vector<Material> g_materials;
        std::unordered_map<std::string, uint32_t> g_byTexture;
        PaneIndex g_panes;
        std::string g_status = "not loaded yet", g_line;
        uint32_t g_generation = 1;

        // This frame.
        Light g_light;
        float g_sun[3] = { 0, 0, 1 }, g_sunColour[3] = { 1, 0.95f, 0.85f }, g_sky[3] = { 0.6f, 0.7f, 0.8f };
        float g_indoors = 0.0f;
        float g_view[16] = {};                   // c180..c183
        bool  g_inWorld = false;
        std::vector<const Pane*> g_near;         // the panes in range, nearest first
        std::vector<const Pane*> g_floorList;    // the floor light's, this frame
        uint32_t g_viewFrame = 0;                // the frame g_view and the panes' rows were made for
        float g_paneConstants[4 * kPaneRegisters * kFloorPanes] = {};
        unsigned g_floorPanes = 0;
        bool g_panesUp = false;                  // c101.. hold this frame's panes
        uint32_t g_frame = 0, g_uploadedFrame = ~0u;

        // The current group's loop.
        uint32_t g_groupFlags = 0;
        bool g_loopFloor = false, g_glassOn = false, g_floorOn = false;
        bool g_floorConstsUp = false;            // the floor light's c180..c183, c192..c201 are up for this loop
        unsigned g_patched = 0, g_glassBatches = 0, g_floorBatches = 0, g_paneUploads = 0;

        // The rooms: the styles, the buildings, the instance being drawn.
        std::vector<RoomStyle> g_styles;
        std::unordered_map<uint32_t, uint32_t> g_styleById;
        BuildingIndex g_buildings;
        const Building* g_building = nullptr;    // the current instance's (null: not in the tables)
        uint32_t g_fallbackSeed = 0;
        bool g_inInstance = false;
        unsigned g_instances = 0, g_matched = 0, g_byWmo = 0, g_defaulted = 0;   // instance groups with glass: placed, by WMO, neither
        float g_hour = 12.0f, g_night = 0.0f;    // this frame's
        std::string g_roomLine;

        float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : (v == v ? v : 0.0f)); }

        std::string Key(const char* path)
        {
            std::string p(path ? path : "");
            for (char& c : p) c = c == '/' ? '\\' : static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            return p;
        }

        IDirect3DDevice9* Device() { return static_cast<IDirect3DDevice9*>(gx::RawDevice()); }

        float ReadFloat(uintptr_t at)
        {
            __try { return *reinterpret_cast<const volatile float*>(at); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 0.0f; }
        }

        uint32_t ReadU32(uintptr_t at)
        {
            __try { return *reinterpret_cast<const volatile uint32_t*>(at); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        }

        void SetBool(int reg, bool on)
        {
            if (auto* dev = Device()) { const BOOL b = on ? TRUE : FALSE; dev->SetPixelShaderConstantB(reg, &b, 1); }
        }

        // The frame's constants every glass or floor batch reads: c180..c183 (the camera and the view's rows), c195
        // (the sun), c200 (its colour), c201 (the floor light's gain).
        void UploadFrame(IDirect3DDevice9* dev)
        {
            dev->SetPixelShaderConstantF(kViewConstant, g_view, 4);
        }

        void UploadPanes(IDirect3DDevice9* dev)
        {
            dev->SetPixelShaderConstantF(kPaneConstant, g_paneConstants, kPaneRegisters * kFloorPanes);
            g_panesUp = true;
            g_uploadedFrame = g_frame;
            ++g_paneUploads;
        }

        const RoomStyle* StyleOf(const Building* b)
        {
            if (!b) return nullptr;
            auto it = g_styleById.find(b->style);
            return it == g_styleById.end() ? nullptr : &g_styles[it->second];
        }

        // One glass batch's constants: its material's look, inside or outside, and the room of the building drawn.
        void GlassConstants(const Material& m, bool selfLit, float out[4 * kConstantCount], float seed[4])
        {
            BatchConstants(m, InsideWeight(m.flags, g_groupFlags, g_indoors), g_light, g_sun, g_sunColour, g_sky, g_settings, out);
            const Building* b = g_inInstance ? g_building : nullptr;
            const RoomStyle* style = StyleOf(b);
            RoomInputs in;
            in.night = g_night; in.hour = g_hour; in.selfLit = selfLit; in.fallbackSeed = g_fallbackSeed;
            RoomBatchConstants(style ? *style : DefaultStyle(), style ? b : nullptr, g_light, in, g_settings, out, seed);
        }
    }

    const RoomStyle& DefaultStyle()
    {
        static const RoomStyle s{};
        return s;
    }

    const BuildingIndex& Buildings() { return g_buildings; }

    const RoomStyle* StyleById(uint32_t id)
    {
        auto it = g_styleById.find(id);
        return it == g_styleById.end() ? nullptr : &g_styles[it->second];
    }

    float HourNow()
    {
        const float o = climate::TimeOverride();
        if (o >= 0.0f) return o * 24.0f;
        const climate::Clock& c = climate::Now();
        return c.valid ? c.dayFraction * 24.0f : 12.0f;
    }

    void RoomBatchConstants(const RoomStyle& style, const Building* b, const Light& light, const RoomInputs& in, const Settings& s,
                            float out[4 * kConstantCount], float seed[4])
    {
        float* c = out;
        const bool variety = s.roomVariety != 0;
        const float v = variety ? std::max(0.0f, s.varietyStrength) : 0.0f;
        const float night = Clamp01(in.night);
        const uint32_t flags = b ? b->flags : 0;
        const bool empty = variety && (flags & kBuildingEmpty);
        const bool darkPlace = (flags & kBuildingDarkPlace) != 0;
        // c198.w the day's light in the room (x the style's brightness); c199.rgb and c202.rgb the building's tints
        // (its hash picks between them), c202.w how far buildings differ in brightness.
        const float* tint = variety ? style.tint : kRoomTint;
        c[27] = kRoomLight * s.interiorStrength * light.day * light.dim * (variety ? std::max(0.0f, style.brightness) : 1.0f);
        for (int k = 0; k < 3; ++k)
        {
            c[28 + k] = tint[k];
            c[40 + k] = variety ? tint[k] + (style.tint2[k] - tint[k]) * std::min(1.0f, v) : tint[k];
        }
        c[43] = variety ? Clamp01(style.spread * v) : 0.0f;
        // c203.w the share of rooms dark: by day the style's few, at night by the hour (inns late, shops early);
        // every room in an empty building, and at night in a dark place.
        const float dayDark = empty ? 1.0f : (variety ? Clamp01(style.darkShare * v) : 0.0f);
        float nightDark = s.lightsOut ? Clamp01(LightsOutShare(in.hour, style.lateHours, style.nightCap) * std::max(0.0f, s.lightsOutStrength)) : 0.0f;
        if (darkPlace || empty) nightDark = 1.0f;
        c[47] = dayDark + (nightDark - dayDark) * night;
        // c203.rgb the lamp at night, behind the windows the client lights (SIDN): its colour at luminance 1 over the
        // room's mean light, so a lit room is about as bright as the client's window (the shader x its luminance).
        const bool lamp = s.litRooms && in.selfLit && !darkPlace && !empty;
        float lampN[3];
        const float ll = std::max(1e-3f, 0.2126f * style.lamp[0] + 0.7152f * style.lamp[1] + 0.0722f * style.lamp[2]);
        for (int k = 0; k < 3; ++k) lampN[k] = std::max(0.0f, style.lamp[k]) / ll;
        const float lit = std::max(1e-3f, 0.2126f * tint[0] * lampN[0] + 0.7152f * tint[1] * lampN[1] + 0.0722f * tint[2] * lampN[2]);
        const float gain = lamp ? night * std::max(0.0f, s.litRoomStrength) / (lit * room::kNightMean) : 0.0f;
        for (int k = 0; k < 3; ++k) c[44 + k] = lampN[k] * gain;
        // c204 the layouts' thresholds and the detail's strength; c205 the curtains, and how much of the client's lit
        // window a dark room hides at night.
        float t[3] = { 0, 0, 0 };
        if (s.roomLayouts) LayoutThresholds(style.weights, t);
        for (int k = 0; k < 3; ++k) c[48 + k] = t[k];
        c[51] = s.roomLayouts ? std::max(0.0f, s.layoutStrength) : 0.0f;
        for (int k = 0; k < 3; ++k) c[52 + k] = style.curtain[k];
        c[55] = s.litRooms && in.selfLit ? kGlowHidden * night : 0.0f;
        // c212 the seed (two numbers 0..1023) and the block (a city's buildings).
        const uint32_t sd = b ? b->seed : in.fallbackSeed;
        seed[0] = static_cast<float>(sd & 1023u);
        seed[1] = static_cast<float>((sd >> 10) & 1023u);
        seed[2] = b && variety && (flags & kBuildingCity) ? 1.0f / room::kBlock : 0.0f;
        seed[3] = 0.0f;
    }

    void BatchConstants(const Material& m, float inside, const Light& light, const float sun[3], const float sunColour[3], const float sky[3],
                        const Settings& s, float out[4 * kConstantCount])
    {
        const Look& look = LookOf(m.kind);
        const bool matte = (m.flags & kMatte) != 0;
        const float day = light.day;
        float* c = out;
        // c192 the mask
        for (int k = 0; k < 4; ++k) c[k] = m.mask[k];
        // c193 inside weight, strength, the room's share, thickness
        c[4] = inside; c[5] = 1.0f;
        c[6] = s.fakeInteriors && s.outsideReflection ? look.room : 0.0f;
        c[7] = look.thickness;
        // c194 the sky through the glass, the sun's gain
        for (int k = 0; k < 3; ++k) c[8 + k] = light.sky[k];
        c[11] = light.sunGain;
        // c195 toward the sun, saturation
        for (int k = 0; k < 3; ++k) c[12 + k] = sun[k];
        c[15] = look.saturation;
        // c196 the reflected sky, Fresnel strength (a little at night: the lit windows stay lit)
        const float reflect = s.outsideReflection && !matte ? s.reflectionStrength : 0.0f;
        for (int k = 0; k < 3; ++k) c[16 + k] = sky[k] * (0.15f + 0.85f * day) * 1.1f;
        c[19] = look.fresnel * reflect * (0.3f + 0.7f * day);
        // c197 darkening outside, glint, F0, diffusion
        c[20] = s.outsideReflection ? look.darken * day * std::min(1.0f, s.reflectionStrength) : 0.0f;
        c[21] = look.glint * reflect * light.sunGain * 3.0f;
        c[22] = look.f0;
        c[23] = look.diffusion;
        // c198 the room's cells and light; c199 its tint, the inside's brightness
        c[24] = 1.0f / kCellWidth; c[25] = 1.0f / kCellHeight; c[26] = 1.0f / kRoomDepth;
        c[27] = kRoomLight * s.interiorStrength * day * light.dim;
        for (int k = 0; k < 3; ++k) c[28 + k] = kRoomTint[k];
        c[31] = s.insideGlow ? look.inside * s.insideStrength : 0.0f;
        // c200 the sun's colour; c201 the floor light's gain
        for (int k = 0; k < 3; ++k) c[32 + k] = sunColour[k];
        c[35] = 0.0f;
        c[36] = kFloorGain * s.floorStrength; c[37] = c[38] = c[39] = 0.0f;
        // c202..c205 the plain room: one tint, no spread, none dark, no lamp, no layouts (RoomBatchConstants: a building's)
        for (int k = 0; k < 3; ++k) { c[40 + k] = kRoomTint[k]; c[52 + k] = DefaultStyle().curtain[k]; }
        c[43] = 0.0f;
        for (int k = 44; k < 52; ++k) c[k] = 0.0f;
        c[55] = 0.0f;
    }

    namespace
    {
        void FloorFrameConstants(float out[4 * kConstantCount])
        {
            std::memset(out, 0, sizeof(float) * 4 * kConstantCount);
            for (int k = 0; k < 3; ++k) { out[12 + k] = g_sun[k]; out[32 + k] = g_sunColour[k]; }
            out[36] = kFloorGain * g_settings.floorStrength;
        }

        void GlassOff()
        {
            if (!g_glassOn) return;
            g_glassOn = false;
            SetBool(kSwitch, false);
        }

        void FloorOff()
        {
            if (!g_floorOn) return;
            g_floorOn = false;
            SetBool(kFloorSwitch, false);
        }
    }

    Settings& Config() { return g_settings; }
    const char* Status() { return g_status.c_str(); }
    uint32_t Generation() { return g_generation; }
    const PaneIndex& Panes() { return g_panes; }
    void NotePatchedVariant() { ++g_patched; }
    unsigned PatchedVariants() { return g_patched; }

    void Load(const WXL_SeyrisCdbcApi* cdbc)
    {
        ++g_generation;
        g_materials.clear();
        g_byTexture.clear();
        g_styles.clear();
        g_styleById.clear();
        g_building = nullptr;
        if (!cdbc || !cdbc->HasFeature("cdbc-load")) { g_status = "wxl-seyris-tools (cdbc) not available"; g_panes.Build({}); g_buildings.Build({}); return; }
        char err[256] = {};
        std::string status;
        if (void* t = cdbcload::LoadAppendOnly(cdbc, kMaterialDef, err, sizeof(err)))
        {
            auto F = [&](const void* row, const char* f, float missing) { return cdbcload::Float(cdbc, t, row, f, missing); };
            for (uint32_t i = 0, n = cdbc->RowCount(t); i < n; ++i)
            {
                const void* row = cdbc->RowAt(t, i);
                if (!row) continue;
                const char* texture = cdbc->GetString(t, row, "Texture");
                Material m;
                m.kind = cdbc->Value(t, row, "Kind", 0);
                m.flags = cdbcload::Has(cdbc, t, "Flags") ? cdbc->Value(t, row, "Flags", 0) : 0;
                if (!texture || !texture[0] || m.kind == kNone) continue;
                const char* names[3][3] = { { "ColorR", "ColorG", "ColorB" }, { "LightR", "LightG", "LightB" }, { "MaskR", "MaskG", "MaskB" } };
                for (int k = 0; k < 3; ++k)
                {
                    m.colour[k] = F(row, names[0][k], 1.0f);
                    m.light[k] = F(row, names[1][k], 1.0f);
                    m.mask[k] = F(row, names[2][k], 0.0f);
                }
                m.mask[3] = F(row, "MaskBias", 1.0f);
                g_materials.push_back(m);
                g_byTexture[Key(texture)] = static_cast<uint32_t>(g_materials.size());
            }
            cdbc->Release(t);
            char line[96];
            std::snprintf(line, sizeof(line), "GlassMaterials.cdbc: %u glass textures", static_cast<unsigned>(g_materials.size()));
            status = line;
        }
        else status = std::string("GlassMaterials.cdbc: ") + (err[0] ? err : "not found") + " (no glass)";

        std::vector<Pane> panes;
        err[0] = 0;
        if (void* t = cdbcload::LoadAppendOnly(cdbc, kPaneDef, err, sizeof(err)))
        {
            auto F = [&](const void* row, const char* f, float missing) { return cdbcload::Float(cdbc, t, row, f, missing); };
            for (uint32_t i = 0, n = cdbc->RowCount(t); i < n; ++i)
            {
                const void* row = cdbc->RowAt(t, i);
                if (!row) continue;
                Pane p;
                p.id = cdbc->Value(t, row, "ID", 0);
                p.map = cdbc->Value(t, row, "MapID", 0);
                p.wmo = cdbc->Value(t, row, "WmoID", 0);
                const char* xyz[3] = { "X", "Y", "Z" };
                for (int k = 0; k < 3; ++k)
                {
                    p.pos[k] = F(row, (std::string("Pos") + xyz[k]).c_str(), 0.0f);
                    p.out[k] = F(row, (std::string("Out") + xyz[k]).c_str(), 0.0f);
                    p.along[k] = F(row, (std::string("Along") + xyz[k]).c_str(), 0.0f);
                    p.roomMin[k] = F(row, (std::string("RoomMin") + xyz[k]).c_str(), 0.0f) - kRoomMargin;
                    p.roomMax[k] = F(row, (std::string("RoomMax") + xyz[k]).c_str(), 0.0f) + kRoomMargin;
                }
                const char* rgb[3] = { "LightR", "LightG", "LightB" };
                for (int k = 0; k < 3; ++k) p.light[k] = F(row, rgb[k], 1.0f);
                p.halfWidth = F(row, "HalfWidth", 1.0f);
                p.halfHeight = F(row, "HalfHeight", 1.0f);
                p.kind = cdbc->Value(t, row, "Kind", 0);
                const float lo = std::sqrt(p.out[0] * p.out[0] + p.out[1] * p.out[1] + p.out[2] * p.out[2]);
                const float la = std::sqrt(p.along[0] * p.along[0] + p.along[1] * p.along[1] + p.along[2] * p.along[2]);
                if (!(lo > 0.5f) || !(la > 0.5f)) continue;
                for (int k = 0; k < 3; ++k) { p.out[k] /= lo; p.along[k] /= la; }
                p.up[0] = p.out[1] * p.along[2] - p.out[2] * p.along[1];
                p.up[1] = p.out[2] * p.along[0] - p.out[0] * p.along[2];
                p.up[2] = p.out[0] * p.along[1] - p.out[1] * p.along[0];
                panes.push_back(p);
            }
            cdbc->Release(t);
            char line[96];
            std::snprintf(line, sizeof(line), "; GlassPanes.cdbc: %u panes", static_cast<unsigned>(panes.size()));
            status += line;
        }
        else status += std::string("; GlassPanes.cdbc: ") + (err[0] ? err : "not found") + " (no floor light, no coloured shafts)";

        // The rooms: their styles and the buildings.
        g_styles.clear();
        g_styleById.clear();
        err[0] = 0;
        if (void* t = cdbcload::LoadAppendOnly(cdbc, kStyleDef, err, sizeof(err)))
        {
            auto F = [&](const void* row, const char* f, float missing) { return cdbcload::Float(cdbc, t, row, f, missing); };
            for (uint32_t i = 0, n = cdbc->RowCount(t); i < n; ++i)
            {
                const void* row = cdbc->RowAt(t, i);
                if (!row) continue;
                RoomStyle s;
                s.id = cdbc->Value(t, row, "ID", 0);
                s.cls = cdbc->Value(t, row, "Class", kClassHouse);
                s.palette = cdbc->Value(t, row, "Palette", 0);
                const char* cols[4][3] = { { "TintR", "TintG", "TintB" }, { "Tint2R", "Tint2G", "Tint2B" }, { "LampR", "LampG", "LampB" },
                                           { "CurtainR", "CurtainG", "CurtainB" } };
                float* dst[4] = { s.tint, s.tint2, s.lamp, s.curtain };
                for (int a = 0; a < 4; ++a)
                    for (int k = 0; k < 3; ++k) dst[a][k] = F(row, cols[a][k], dst[a][k]);
                s.brightness = F(row, "Brightness", s.brightness);
                s.spread = F(row, "Spread", s.spread);
                s.darkShare = F(row, "DarkShare", s.darkShare);
                const char* w[4] = { "Shelves", "Curtains", "Divider", "Plain" };
                for (int k = 0; k < 4; ++k) s.weights[k] = F(row, w[k], s.weights[k]);
                s.lateHours = F(row, "LateHours", s.lateHours);
                s.nightCap = F(row, "NightCap", s.nightCap);
                g_styleById[s.id] = static_cast<uint32_t>(g_styles.size());
                g_styles.push_back(s);
            }
            cdbc->Release(t);
            char line[96];
            std::snprintf(line, sizeof(line), "; RoomStyles.cdbc: %u styles", static_cast<unsigned>(g_styles.size()));
            status += line;
        }
        else status += std::string("; RoomStyles.cdbc: ") + (err[0] ? err : "not found") + " (every room the same)";
        std::vector<Building> buildings;
        err[0] = 0;
        if (void* t = g_styles.empty() ? nullptr : cdbcload::LoadAppendOnly(cdbc, kBuildingDef, err, sizeof(err)))
        {
            auto F = [&](const void* row, const char* f, float missing) { return cdbcload::Float(cdbc, t, row, f, missing); };
            for (uint32_t i = 0, n = cdbc->RowCount(t); i < n; ++i)
            {
                const void* row = cdbc->RowAt(t, i);
                if (!row) continue;
                Building b;
                b.map = cdbc->Value(t, row, "MapID", 0);
                b.uniqueId = cdbc->Value(t, row, "UniqueID", 0);
                b.pos[0] = F(row, "PosX", 0.0f); b.pos[1] = F(row, "PosY", 0.0f); b.pos[2] = F(row, "PosZ", 0.0f);
                b.group = cdbc->Value(t, row, "Group", kWholePlacement);
                b.style = cdbc->Value(t, row, "Style", 0);
                b.seed = cdbc->Value(t, row, "Seed", 0);
                b.flags = cdbc->Value(t, row, "Flags", 0);
                b.area = cdbc->Value(t, row, "AreaID", 0);
                b.path = PathHash(cdbc->GetString(t, row, "Wmo"));
                buildings.push_back(b);
            }
            cdbc->Release(t);
            char line[128];
            std::snprintf(line, sizeof(line), "; GlassBuildings.cdbc: %u rows", static_cast<unsigned>(buildings.size()));
            status += line;
        }
        else if (!g_styles.empty()) status += std::string("; GlassBuildings.cdbc: ") + (err[0] ? err : "not found") + " (every building the default house)";
        g_buildings.Build(std::move(buildings));
        g_building = nullptr;
        g_status = status;
        g_panes.Build(std::move(panes));
        g_near.clear();
        g_floorPanes = 0;
    }

    uint32_t MaterialOf(const char* texturePath)
    {
        if (g_byTexture.empty() || !texturePath) return 0;
        auto it = g_byTexture.find(Key(texturePath));
        return it == g_byTexture.end() ? 0 : it->second;
    }

    const Material* MaterialAt(uint32_t indexPlusOne)
    {
        return indexPlusOne && indexPlusOne <= g_materials.size() ? &g_materials[indexPlusOne - 1] : nullptr;
    }

    void Update()
    {
        ++g_frame;
        g_panesUp = false;
        const world::Snapshot& snap = world::Current();
        g_inWorld = snap.inWorld && snap.mapId >= 0;
        if (!g_settings.enabled || !g_inWorld) { g_near.clear(); g_floorPanes = 0; return; }

        // The light of the moment.
        Inputs in;
        const climate::Sun& sun = climate::SunNow();
        for (int k = 0; k < 3; ++k) in.sun[k] = sun.dir[k];
        in.daylight = sun.daylight;
        in.night = windows::Night();
        const fog::RulesInputs& fi = fog::Inputs();
        in.overcast = Clamp01(fi.valid ? fi.fogginess : climate::Here().fogginess);
        const climate::Weather& w = climate::CurrentWeather();
        in.precipitation = (w.type == 1 || w.type == 2) ? Clamp01(w.intensity) : (w.type == 3 ? 0.6f * Clamp01(w.intensity) : 0.0f);
        in.storm = Clamp01(storm::GetStatus().storminess);
        const uint32_t argb = ReadU32(kFogColor);
        const float fogc[3] = { ((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f };
        for (int k = 0; k < 3; ++k) in.sky[k] = fogc[k];
        float ambient[3], diffuse[3];
        if (cover::TerrainLight(ambient, diffuse))
        {
            const float peak = std::max(diffuse[0], std::max(diffuse[1], diffuse[2]));
            for (int k = 0; k < 3; ++k) in.sunColour[k] = peak > 0.05f ? std::min(1.5f, diffuse[k] / peak * 1.2f) : 1.0f;
        }
        g_light = Evaluate(in);
        for (int k = 0; k < 3; ++k) { g_sun[k] = in.sun[k]; g_sunColour[k] = in.sunColour[k]; g_sky[k] = in.sky[k]; }
        g_indoors = Clamp01(ReadFloat(kInteriorBlend));
        g_night = Clamp01(in.night);
        g_hour = HourNow();

        // The panes near the camera that the sun shines through: the floor light's and the shafts' (GlassShafts picks
        // those whose room holds the camera). Their rows about the camera are made at the frame's first batch
        // (RenderView), with the camera the shaders get.
        g_near.clear();
        g_floorList.clear();
        g_floorPanes = 0;
        g_viewFrame = 0;
        if (g_light.sunGain <= 0.01f || g_panes.Size() == 0) return;
        float cam[3];
        wxl::game::camera::GetPosition(cam);
        static std::vector<const Pane*> candidates;
        g_panes.Near(static_cast<uint32_t>(snap.mapId), cam, g_settings.range, 256, candidates);
        for (const Pane* p : candidates)
            if (p->out[0] * g_sun[0] + p->out[1] * g_sun[1] + p->out[2] * g_sun[2] > 0.05f) g_near.push_back(p);
        // The floor light: the panes whose room holds the camera first (the room you stand in), then the nearest.
        if (!g_settings.floorLight) return;
        for (int pass = 0; pass < 2; ++pass)
            for (const Pane* p : g_near)
            {
                if (g_floorList.size() >= static_cast<size_t>(kFloorPanes)) break;
                bool inRoom = true;
                for (int k = 0; k < 3; ++k) inRoom = inRoom && cam[k] >= p->roomMin[k] && cam[k] <= p->roomMax[k];
                if (inRoom == (pass == 0)) g_floorList.push_back(p);
            }
        g_floorPanes = static_cast<unsigned>(g_floorList.size());
    }

    namespace
    {
        // At the frame's first glass or floor batch: the camera and the view's rows as this frame's scene has them
        // (c180..c183, as the ice's), and the floor light's panes as rows about that camera.
        void RenderView()
        {
            if (g_viewFrame == g_frame) return;
            g_viewFrame = g_frame;
            float view[16], proj[16], cam[3];
            const bool have = wxl::game::gfx::SceneMatrices(view, proj);
            wxl::game::camera::GetPosition(cam);
            std::memset(g_view, 0, sizeof(g_view));
            for (int k = 0; k < 3; ++k) g_view[k] = cam[k];
            for (int r = 0; r < 3; ++r)
                for (int k = 0; k < 3; ++k) g_view[4 + r * 4 + k] = have ? view[r * 4 + k] : (r == k ? 1.0f : 0.0f);
            std::memset(g_paneConstants, 0, sizeof(g_paneConstants));
            unsigned n = 0;
            for (const Pane* p : g_floorList)
            {
                float* c = g_paneConstants + 4 * kPaneRegisters * n;
                if (!ProjectionRows(*p, g_sun, cam, kReach, c)) continue;
                const float t = LookOf(p->kind).through * g_light.sunGain;
                for (int k = 0; k < 3; ++k) c[12 + k] = p->light[k] * g_sunColour[k] * t;
                c[15] = 1.0f;
                for (int k = 0; k < 3; ++k) { c[16 + k] = p->roomMin[k] - cam[k]; c[20 + k] = p->roomMax[k] - cam[k]; }
                c[19] = kSoft;
                c[23] = kFarSoft;
                ++n;
            }
            g_floorPanes = n;
            g_panesUp = false;
        }
    }

    namespace
    {
        // Nested batch loops (a loop's segment drawn by the other loop): each restores the one around it.
        struct GroupState { uint32_t flags; bool floor; };
        GroupState g_groupStack[8];
        int g_groupDepth = 0;
    }

    void BeginGroup(uint32_t groupFlags, bool rootHasGlass)
    {
        if (g_groupDepth < 8) g_groupStack[g_groupDepth] = { g_groupFlags, g_loopFloor };
        ++g_groupDepth;
        g_groupFlags = groupFlags;
        g_floorConstsUp = false;
        const bool interior = (groupFlags & kGroupIndoor) && !(groupFlags & kGroupExterior);
        g_loopFloor = g_settings.enabled && g_settings.floorLight && g_patched > 0 && g_floorPanes > 0 && interior && rootHasGlass;
    }

    void BeginInstance(const float pos[3], uint32_t path, uint32_t group, uint32_t fallbackSeed)
    {
        g_inInstance = true;
        g_fallbackSeed = fallbackSeed;
        const world::Snapshot& snap = world::Current();
        g_building = g_buildings.Size() ? g_buildings.Find(static_cast<uint32_t>(snap.mapId), pos, path, group) : nullptr;
        ++g_instances;
        if (!g_building) ++g_defaulted;
        else if (g_building->uniqueId) ++g_matched;
        else ++g_byWmo;
    }

    void EndInstance()
    {
        g_inInstance = false;
        g_building = nullptr;
    }

    void Batch(uint32_t glassMaterial, bool rimsOn, bool selfLit)
    {
        auto* dev = Device();
        if (!dev) return;
        const Material* m = g_settings.enabled && g_patched > 0 ? MaterialAt(glassMaterial) : nullptr;
        if (m || g_loopFloor) RenderView();
        if (m)
        {
            float c[4 * kConstantCount], seed[4];
            GlassConstants(*m, selfLit, c, seed);
            UploadFrame(dev);
            dev->SetPixelShaderConstantF(kFirstConstant, c, kConstantCount);
            dev->SetPixelShaderConstantF(kSeedConstant, seed, 1);
            g_floorConstsUp = false;   // c192..c201 are this batch's now
            if (!g_glassOn) { SetBool(kSwitch, true); g_glassOn = true; }
            ++g_glassBatches;
        }
        else GlassOff();
        // The floor light: the room's surfaces, not its glass, and not a floor whose rims hold the panes' registers.
        if (g_loopFloor && !m && !rimsOn)
        {
            if (!g_panesUp || g_uploadedFrame != g_frame) UploadPanes(dev);
            // c180..c183 and c192..c201 once per loop (the ice and the rims leave c192.. alone), again after a glass batch.
            if (!g_floorConstsUp)
            {
                float c[4 * kConstantCount];
                FloorFrameConstants(c);
                UploadFrame(dev);
                dev->SetPixelShaderConstantF(kFirstConstant, c, kConstantCount);
                g_floorConstsUp = true;
            }
            if (!g_floorOn) { SetBool(kFloorSwitch, true); g_floorOn = true; }
            ++g_floorBatches;
        }
        else FloorOff();
    }

    void EndGroup()
    {
        GlassOff();
        FloorOff();
        if (g_groupDepth > 0) --g_groupDepth;
        if (g_groupDepth > 0 && g_groupDepth < 8) { g_groupFlags = g_groupStack[g_groupDepth].flags; g_loopFloor = g_groupStack[g_groupDepth].floor; }
        else { g_groupFlags = 0; g_loopFloor = false; }
    }

    void PaneRangeTaken() { g_panesUp = false; g_floorConstsUp = false; }
    void ViewTaken() { g_floorConstsUp = false; }

    bool ShaftPanes(std::vector<ShaftPane>& out, size_t max)
    {
        out.clear();
        if (!g_settings.enabled || !g_settings.colouredShafts || g_near.empty()) return false;
        float cam[3];
        wxl::game::camera::GetPosition(cam);
        for (const Pane* p : g_near)
        {
            if (out.size() >= max) break;
            // the room you stand in (a little slack: a doorway)
            bool inRoom = true;
            for (int k = 0; k < 3; ++k) inRoom = inRoom && cam[k] >= p->roomMin[k] - 2.0f && cam[k] <= p->roomMax[k] + 2.0f;
            if (!inRoom) continue;
            ShaftPane s{ p, {} };
            const float t = LookOf(p->kind).through * g_light.sunGain;
            for (int k = 0; k < 3; ++k) s.colour[k] = p->light[k] * g_sunColour[k] * t;
            out.push_back(s);
        }
        return !out.empty();
    }

    void SunNow(float dir[3], float colour[3])
    {
        for (int k = 0; k < 3; ++k) { dir[k] = g_sun[k]; colour[k] = g_sunColour[k] * g_light.sunGain; }
    }

    void DrawPanelSection(const WXL_Api* api)
    {
        if (!api->UiCollapsingHeader("Glass: window glass, the light through it##glass")) return;
        Settings& s = g_settings;
        api->UiCheckbox("glass##glon", &s.enabled);
        api->UiCheckbox("seen from inside: the light coming through##glin", &s.insideGlow);
        api->UiSliderFloat("inside: strength##glins", &s.insideStrength, 0.0f, 3.0f);
        api->UiCheckbox("seen from outside: the sky in it, darker by day##glout", &s.outsideReflection);
        api->UiSliderFloat("outside: reflection strength##glouts", &s.reflectionStrength, 0.0f, 3.0f);
        api->UiCheckbox("fake rooms behind clear glass##glroom", &s.fakeInteriors);
        api->UiSliderFloat("fake rooms: light##glrooms", &s.interiorStrength, 0.0f, 3.0f);
        api->UiCheckbox("rooms: each building its own (tint, brightness, a few dark)##glvar", &s.roomVariety);
        api->UiSliderFloat("rooms: how far buildings differ##glvars", &s.varietyStrength, 0.0f, 2.0f);
        api->UiCheckbox("rooms: lamp-lit at night behind the lit windows##gllit", &s.litRooms);
        api->UiSliderFloat("rooms: lamp-lit brightness (1 = the client's window)##gllits", &s.litRoomStrength, 0.0f, 2.0f);
        api->UiCheckbox("rooms: lights go out as the night goes on##glout2", &s.lightsOut);
        api->UiSliderFloat("rooms: share dark at night##glouts2", &s.lightsOutStrength, 0.0f, 1.5f);
        api->UiCheckbox("rooms: shelves, curtains, a dividing wall##gllay", &s.roomLayouts);
        api->UiSliderFloat("rooms: the furniture's detail##gllays", &s.layoutStrength, 0.0f, 2.0f);
        api->UiCheckbox("coloured shafts in the room's air##glsh", &s.colouredShafts);
        api->UiSliderFloat("coloured shafts: strength##glshs", &s.shaftStrength, 0.0f, 3.0f);
        api->UiCheckbox("light through the panes on the floor##glfl", &s.floorLight);
        api->UiSliderFloat("floor light: strength##glfls", &s.floorStrength, 0.0f, 3.0f);
        api->UiSliderFloat("panes: range from the camera (yd)##glr", &s.range, 20.0f, 150.0f);
        api->UiText(g_status.c_str());
        api->UiText(StatusLine());
        api->UiText(glassshafts::StatusLine());
        char line[256];
        std::snprintf(line, sizeof(line), "light: sun through %.2f, sky %.2f, day %.2f, dim %.2f; camera indoors %.2f",
                      g_light.sunGain, g_light.skyGain, g_light.day, g_light.dim, g_indoors);
        api->UiText(line);
        api->UiText(RoomLine());
    }

    const char* RoomLine()
    {
        char line[320];
        const int hh = static_cast<int>(g_hour) % 24, mm = static_cast<int>((g_hour - std::floor(g_hour)) * 60.0f);
        std::snprintf(line, sizeof(line), "rooms: %u styles, %zu placements (%zu rows); instances drawn with glass %u: placed %u, by WMO %u, default %u; "
                      "%02d:%02d night %.2f, dark at night: house %.0f%%, inn %.0f%%",
                      static_cast<unsigned>(g_styles.size()), g_buildings.Placements(), g_buildings.Size(), g_instances, g_matched, g_byWmo,
                      g_defaulted, hh, mm, g_night, 100.0f * LightsOutShare(g_hour, 0.0f, 1.0f), 100.0f * LightsOutShare(g_hour, 2.5f, 0.55f));
        g_roomLine = line;
        return g_roomLine.c_str();
    }

    const char* StatusLine()
    {
        char line[320];
        std::snprintf(line, sizeof(line), "glass: %s; WMO shader variants with the blocks %u; glass batches %u, floor-light batches %u (%u pane uploads); "
                      "sunlit panes near %u, on the floor %u",
                      g_settings.enabled ? "on" : "off", g_patched, g_glassBatches, g_floorBatches, g_paneUploads,
                      static_cast<unsigned>(g_near.size()), g_floorPanes);
        g_line = line;
        return g_line.c_str();
    }
}
