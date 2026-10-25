#include "Glass.hpp"

#include "../env/CdbcLoad.hpp"
#include "../env/Climate.hpp"
#include "../env/Fog.hpp"
#include "../env/Storm.hpp"
#include "../env/WorldQuery.hpp"
#include "../render/BlpTexture.hpp"
#include "../wxl_seyris/CdbcApi.hpp"
#include "GlassShafts.hpp"
#include "SurfaceCover.hpp"
#include "Windows.hpp"

#include "game/Camera.hpp"
#include "game/Gfx.hpp"
#include "game/Gx.hpp"
#include "../debug/UiText.hpp"

#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <chrono>
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
            // titan glass lit from behind (2026-10-18)
            {"Backlit", 16, WXL_CDBC_FIELD_VALUE}, {"Pulse", 17, WXL_CDBC_FIELD_VALUE},   {"PulseSeconds", 18, WXL_CDBC_FIELD_VALUE},
            {"PulseR", 19, WXL_CDBC_FIELD_VALUE},  {"PulseG", 20, WXL_CDBC_FIELD_VALUE},  {"PulseB", 21, WXL_CDBC_FIELD_VALUE},
            // the corner of the texture that isn't glass (2026-10-20: the forge's fire slots)
            {"NotGlassU", 22, WXL_CDBC_FIELD_VALUE}, {"NotGlassV", 23, WXL_CDBC_FIELD_VALUE},
        };
        constexpr WXL_SeyrisCdbcDefinition kMaterialDef = { "GlassMaterials", kMaterialsFile, kMaterialFields, 24 };
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
            {"Flags", 24, WXL_CDBC_FIELD_VALUE},     {"Comment", 25, WXL_CDBC_FIELD_STRING},  {"Material", 26, WXL_CDBC_FIELD_VALUE},
        };
        constexpr WXL_SeyrisCdbcDefinition kPaneDef = { "GlassPanes", kPanesFile, kPaneFields, 27 };
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
        // One room per window (tools/gen_glass.py GLASS_WINDOWS; env/GlassRooms' WindowTable).
        constexpr const char* kWindowsFile = "DBFilesClient\\GlassWindows.cdbc";
        constexpr WXL_SeyrisCdbcField kWindowFields[] = {
            {"ID", 0, WXL_CDBC_FIELD_VALUE},         {"Wmo", 1, WXL_CDBC_FIELD_STRING},       {"Group", 2, WXL_CDBC_FIELD_VALUE},
            {"Ordinal", 3, WXL_CDBC_FIELD_VALUE},    {"Kind", 4, WXL_CDBC_FIELD_VALUE},
            {"CenterX", 5, WXL_CDBC_FIELD_VALUE},    {"CenterY", 6, WXL_CDBC_FIELD_VALUE},    {"CenterZ", 7, WXL_CDBC_FIELD_VALUE},
            {"NormalX", 8, WXL_CDBC_FIELD_VALUE},    {"NormalY", 9, WXL_CDBC_FIELD_VALUE},    {"NormalZ", 10, WXL_CDBC_FIELD_VALUE},
            {"HalfWidth", 11, WXL_CDBC_FIELD_VALUE}, {"HalfHeight", 12, WXL_CDBC_FIELD_VALUE}, {"Flags", 13, WXL_CDBC_FIELD_VALUE},
            {"Panes", 14, WXL_CDBC_FIELD_VALUE},     {"Comment", 15, WXL_CDBC_FIELD_STRING},  {"Thickness", 16, WXL_CDBC_FIELD_VALUE},
            {"Room", 17, WXL_CDBC_FIELD_VALUE},      // (2026-10-24: the room it shows, -1 none; a file without it: none)
        };
        constexpr WXL_SeyrisCdbcDefinition kWindowDef = { "GlassWindows", kWindowsFile, kWindowFields, 18 };
        // Where a glass texture's panes are, for the see-through redraw's lead mask (tools/gen_glass.py GLASS_LEAD,
        // LEAD_LAYOUTS; 2026-10-21): one row per rectangle. Optional: without it the redraw keeps the light's mask.
        constexpr const char* kLeadFile = "DBFilesClient\\GlassLead.cdbc";
        constexpr WXL_SeyrisCdbcField kLeadFields[] = {
            {"ID", 0, WXL_CDBC_FIELD_VALUE}, {"Texture", 1, WXL_CDBC_FIELD_STRING}, {"U0", 2, WXL_CDBC_FIELD_VALUE},
            {"V0", 3, WXL_CDBC_FIELD_VALUE}, {"U1", 4, WXL_CDBC_FIELD_VALUE},       {"V1", 5, WXL_CDBC_FIELD_VALUE},
            {"Dark", 6, WXL_CDBC_FIELD_VALUE}, {"Line", 7, WXL_CDBC_FIELD_VALUE},   {"Comment", 8, WXL_CDBC_FIELD_STRING},
        };
        constexpr WXL_SeyrisCdbcDefinition kLeadDef = { "GlassLead", kLeadFile, kLeadFields, 9 };
        // The buildings whose windows go see-through (features/SeeThrough; tools/see_through_survey.py --table, its
        // SEE_THROUGH list; 2026-10-22): one row per WMO, its Flags (0x1 from outside, 0x2 from inside, 0x4 between rooms),
        // or (Group, 2026-10-23: -1 or missing the whole WMO) one per group of it; or (Window, 2026-10-24: -1 or missing none)
        // one window of it, by its ordinal, its Flags the ways it may open (0 none: a mask over its room's). Optional: without
        // it no building's windows open.
        constexpr const char* kSeeThroughFile = "DBFilesClient\\SeeThroughBuildings.cdbc";
        constexpr WXL_SeyrisCdbcField kSeeThroughFields[] = {
            {"ID", 0, WXL_CDBC_FIELD_VALUE}, {"Wmo", 1, WXL_CDBC_FIELD_STRING}, {"Flags", 2, WXL_CDBC_FIELD_VALUE},
            {"Comment", 3, WXL_CDBC_FIELD_STRING}, {"Group", 4, WXL_CDBC_FIELD_VALUE}, {"Window", 5, WXL_CDBC_FIELD_VALUE},
        };
        constexpr WXL_SeyrisCdbcDefinition kSeeThroughDef = { "SeeThroughBuildings", kSeeThroughFile, kSeeThroughFields, 6 };

        // Each kind's look (features/GlassShader's constants): the room behind it (x the settings' shares by day and
        // night), the glass's thickness (yd), saturation seen from inside, the reflection at grazing (Fresnel's
        // strength), darkening outside by day and F0 (x the settings'), glint, diffusion (frosted: the sun's direction
        // matters less), brightness seen from inside, and how much of the sun gets through to the floor.
        // Seen from outside the client's own window texture is the artist's painted glass, warm and bright, as if lamplit:
        // by day it moves part of the way to glass (clear glass darker than leaded, stained and frosted barely; cooler,
        // toward the sky's hue: `cool` x DayCoolness, stained barely), with the sky in it (F0: `f0` x room::kNightF0 at
        // night, `dayF0` x ReflectionF0 by day); at dusk and night its warmth again.
        struct Look { float room, thickness, saturation, fresnel, darken, glint, f0, diffusion, inside, through, dayF0, cool; };
        // F0 is above real glass's 0.04: without the eye's range, the sky's 4% beside a sunlit wall reads as no reflection.
        constexpr Look kClear = { 1.0f, 0.0f, 1.1f, 1.0f, 1.0f, 1.0f, 1.2f, 0.1f, 1.5f, 1.0f, 1.2f, 1.0f };
        constexpr Look kLeadedLook = { kLeadedRoom, 0.012f, 1.15f, 0.9f, 0.6f, 0.8f, 1.0f, 0.2f, 1.45f, 0.85f, 1.0f, 1.0f };
        constexpr Look kStainedLook = { 0.0f, 0.035f, 1.35f, 0.6f, 0.2f, 0.6f, 0.8f, 0.3f, 1.9f, 0.8f, 0.5f, 0.25f };
        constexpr Look kFrostedLook = { 0.0f, 0.0f, 1.0f, 0.35f, 0.2f, 0.25f, 0.5f, 0.85f, 1.05f, 0.55f, 0.5f, 0.6f };

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

        // The room behind clear glass: one per window (GlassWindows.cdbc), a warm tint, by day about half as bright as the
        // glass in front (room::kDayRoom; a building's style changes the tint and the light: RoomBatchConstants).
        constexpr float kRoomTint[3] = { 0.85f, 0.72f, 0.55f };
        constexpr float kGlowHidden = 0.75f;   // at night a dark room hides this much of the client's lit window in front of it (all of the window)
        // The floor light: the reach along the sun's ray behind a pane, the soft edges (1 / the share of the half
        // sizes), the far end's softness, and the gain.
        constexpr float kReach = 30.0f, kFloorGain = 1.2f;   // (the soft edges: GlassShader's kPaneSoft, kPaneFarSoft)
        constexpr float kRoomMargin = 0.5f;   // yd the room's box is widened by (walls sit on its faces)

        Settings g_settings;
        std::vector<Material> g_materials;
        std::vector<std::string> g_texturePaths;   // each material's texture, as GlassMaterials has it
        std::vector<LeadRule> g_lead;              // each material's lead rule (GlassLead.cdbc; no rectangles: none)
        std::unordered_map<uint32_t, uint32_t> g_seeThrough;   // SeeThroughBuildings.cdbc: a WMO's PathHash -> its Flags (any group's too)
        std::unordered_map<uint32_t, uint32_t> g_seeThroughWhole;   // ... its whole-WMO rows' alone
        std::unordered_map<uint64_t, uint32_t> g_seeThroughGroup;   // (PathHash << 32 | group) -> a group's row's
        std::unordered_map<uint64_t, uint32_t> g_seeThroughWindow;  // (PathHash << 32 | ordinal) -> a window's row's (a mask, 0 too)
        std::unordered_map<std::string, uint32_t> g_byTexture;
        std::unordered_map<uint32_t, uint32_t> g_materialById;   // GlassMaterials ID -> index + 1
        PaneIndex g_panes;
        std::string g_status = "not loaded yet", g_line;
        bool g_loadProblem = false;              // the last Load missed a table, or kept no windows or no placements
        uint32_t g_generation = 1;

        // This frame.
        Light g_light;
        float g_sun[3] = { 0, 0, 1 }, g_sunColour[3] = { 1, 0.95f, 0.85f }, g_sky[3] = { 0.6f, 0.7f, 0.8f };
        float g_indoors = 0.0f;
        float g_view[16] = {};                   // c180..c183
        bool  g_inWorld = false;
        std::vector<PaneLight> g_near;           // the panes in range that light the room (sunlit or backlit), nearest first
        std::vector<PaneLight> g_floorList;      // the floor light's, this frame
        std::vector<uint32_t> g_floorKept, g_shaftKept;   // the floor light's and the shafts' panes last time (IDs: ChoosePanes)
        PaneFader g_floorFader, g_shaftFader;    // their panes fading in and out (2026-10-24: "lots of pop-in when i move")
        std::chrono::steady_clock::time_point g_floorStamp{}, g_shaftStamp{};   // their last steps (wall time)

        // Seconds since `stamp` (wall time; 0 the first time), and `stamp` now.
        float SecondsSince(std::chrono::steady_clock::time_point& stamp)
        {
            const auto now = std::chrono::steady_clock::now();
            const bool first = stamp == std::chrono::steady_clock::time_point{};
            const float dt = first ? 0.0f : std::chrono::duration<float>(now - stamp).count();
            stamp = now;
            return dt;
        }
        float g_floorFacing[3] = { 0, 0, 1 };    // its batches' c195: their lights' direction (LightFacing)
        double g_pulseSeconds = 0.0;             // the backlit glass's pulse clock (steady seconds)
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
        float g_origin[3] = {};                  // the current instance's origin (world)
        bool g_inInstance = false, g_hasOrigin = false;
        unsigned g_instances = 0, g_matched = 0, g_byWmo = 0, g_defaulted = 0;   // instance groups with glass: placed, by WMO, neither
        float g_hour = 12.0f, g_night = 0.0f;    // this frame's
        RoomClock g_clock;                       // the rooms' lights out: the hour and night as they show them
        HearthClock g_hearth;                    // the hearths' flicker (frosted glass; the rooms' lamps)
        std::string g_roomLine, g_frostLine;
        // Lit frosted glass drawn: batches this frame and last (of them Ironforge's, underground ones).
        unsigned g_frostBatches = 0, g_frostIronforge = 0, g_underground = 0;
        unsigned g_lastFrost = 0, g_lastIronforge = 0, g_lastUnderground = 0;
        // Titan glass lit from behind: panes loaded, near this frame; on the floor and batches drawn this frame and last.
        unsigned g_backlitPanes = 0, g_backlitNear = 0, g_backlitFloor = 0, g_lastBacklitFloor = 0, g_backlitBatches = 0, g_lastBacklitBatches = 0;

        // One room per window: the table (GlassWindows.cdbc), the current instance's frame and group, the device's
        // textures (the table, the furniture atlas, a white texel for RoomTextures off, each palette's set: loaded the
        // first time a batch wants it, a set a frame, in Update).
        WindowTable g_windows;
        std::string g_windowStatus = "GlassWindows.cdbc: not loaded yet";
        float g_axes[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        bool g_hasAxes = false;
        uint32_t g_path = 0, g_group = 0;
        IDirect3DTexture9* g_tableTex = nullptr;
        IDirect3DTexture9* g_atlasTex = nullptr;
        IDirect3DTexture9* g_whiteTex = nullptr;
        bool g_tableDirty = true;
        struct SetTextures { IDirect3DTexture9* tex[4] = {}; float gain[4] = { 1, 1, 1, 1 }; int state = 0; };   // 0 none, 1 wanted, 2 loaded, 3 failed
        SetTextures g_sets[kTextureSets];
        std::string g_textureError;
        uint32_t g_samplerFrame = ~0u;
        unsigned g_roomBatches = 0, g_lastRoomBatches = 0, g_noFrame = 0, g_lastNoFrame = 0, g_noGrid = 0, g_lastNoGrid = 0;
        // Of the batches with rooms: behind frosted and stained glass, this frame and last.
        unsigned g_frostRooms = 0, g_lastFrostRooms = 0, g_stainRooms = 0, g_lastStainRooms = 0;
        unsigned g_stainedWindows = 0, g_frostedWindows = 0;   // GlassWindows.cdbc as loaded

        float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : (v == v ? v : 0.0f)); }
        float Lum(const float c[3]) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }

        // c198.w, the day's light in the room: the shader takes it x its tint x the client's colour's luminance (the room
        // seen through the glass), so with the glass's own darkening (1 - darken), over the room's mean light
        // (room::kDayMean) and its tints' luminance it makes the room about room::kDayRoom of the glass in front (x the
        // style's brightness, the settings' strength), while the client's windows are unlit (`day`: Light::rooms).
        float DayRoomLight(float day, float darken, const float tintA[3], const float tintB[3], float brightness, const Settings& s)
        {
            const float tintLum = std::max(0.05f, 0.5f * (Lum(tintA) + Lum(tintB)));
            return room::kDayRoom / room::kDayMean * std::max(0.0f, s.interiorStrength) * Clamp01(day) * (1.0f - Clamp01(darken)) *
                   std::max(0.0f, brightness) / tintLum;
        }

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

        // The panes past the first twelve (c34..c57): other shaders write those registers between draws, so every floor batch.
        void UploadMorePanes(IDirect3DDevice9* dev)
        {
            dev->SetPixelShaderConstantF(kPaneConstantMore, g_paneConstants + 4 * kPaneRegisters * kPanesAt101, kPaneRegisters * (kFloorPanes - kPanesAt101));
        }

        void UploadPanes(IDirect3DDevice9* dev)
        {
            dev->SetPixelShaderConstantF(kPaneConstant, g_paneConstants, kPaneRegisters * kPanesAt101);
            UploadMorePanes(dev);
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

        // This frame's inputs for a room: the night, the hour, and the band's ends from the rooms' clock.
        RoomInputs FrameInputs()
        {
            RoomInputs in;
            in.night = g_night; in.hour = g_hour;
            in.clock = &g_clock;
            return in;
        }

        // One glass batch's constants: its material's look, inside or outside, and the room of the building drawn; its
        // day look (c85, c86); the room's detail and its light lit and dark (c218); the room's registers (c15..c27) and the
        // texture set it wants (-1: none, white).
        void GlassConstants(const Material& m, bool selfLit, bool unlit, float out[4 * kConstantCount], float seed[4 * kSeedRegisters],
                            float day[4 * kDayRegisters], float look[4 * kLookRegisters], float rooms[4 * kRoomRegisters], int& set)
        {
            LookConstants(g_light, look);
            BatchConstants(m, InsideWeight(m.flags, g_groupFlags, g_indoors), g_light, g_sun, g_sunColour, g_sky, g_settings, out);
            const Building* b = g_inInstance ? g_building : nullptr;
            const RoomStyle* style = StyleOf(b);
            DayConstants(m, g_light, g_settings, style && b ? b->flags : 0, day);
            RoomInputs in = FrameInputs();
            in.selfLit = selfLit; in.fallbackSeed = g_fallbackSeed;
            in.origin = g_inInstance && g_hasOrigin ? g_origin : nullptr;
            in.kind = m.kind; in.unlit = unlit; in.hearthClock = g_hearth.Now();
            RoomBatchConstants(style ? *style : DefaultStyle(), style ? b : nullptr, g_light, in, g_settings, out, seed, look);
            // Titan glass with no sky behind it: lit from behind, the same at any hour.
            if (BacklitBatch(m, b ? b->flags : 0, g_settings)) { BacklitConstants(m, g_pulseSeconds, g_settings, out, day); ++g_backlitBatches; }
            if (out[39] > 0.0f) { ++g_frostBatches; g_frostIronforge += IsIronforge(b) ? 1 : 0; }
            g_underground += b && (b->flags & kBuildingUnderground) ? 1 : 0;
            // The room's: the group's grid and the instance's frame (none without either: no room, lit; stained glass's own
            // grid), the class, the palette's textures (with RoomTextures, once loaded; until then and without it white, a
            // gain of 1).
            const WindowTable::Grid* grid = g_inInstance && g_hasAxes && g_hasOrigin ? g_windows.Find(g_path, g_group, m.kind == kStained) : nullptr;
            const bool wanted = out[6] > 0.0f || out[39] > 0.0f;
            if (wanted && !grid) { if (!g_hasAxes) ++g_noFrame; else ++g_noGrid; }
            const uint32_t cls = style ? style->cls : DefaultStyle().cls;
            set = g_settings.roomTextures && out[6] > 0.0f ? TextureSetOf(style ? style->palette : 0, cls) : -1;
            float scales[4] = { 1, 1, 1, 1 }, gains[4] = { 1, 1, 1, 1 };
            if (set >= 0)
            {
                const TextureSet& ts = TextureSetAt(set);
                for (int k = 0; k < 4; ++k) scales[k] = ts.scale[k];
                if (g_sets[set].state == 2) for (int k = 0; k < 4; ++k) gains[k] = g_sets[set].gain[k];
                else { if (g_sets[set].state == 0) g_sets[set].state = 1; set = -1; }
            }
            RoomRegisters(g_axes, g_origin, g_view, grid, g_windows.Height(), cls, scales, gains, g_settings, rooms, m.kind, m.notGlass);
            const unsigned withRoom = grid && out[6] > 0.0f ? 1 : 0;
            g_roomBatches += withRoom;
            g_frostRooms += m.kind == kFrosted ? withRoom : 0;
            g_stainRooms += m.kind == kStained ? withRoom : 0;
        }

        // The room's samplers: the floor, walls, ceiling and wood (a set's, or white), the atlas, the table; their states
        // once a frame (another pass may have used s8, s9: the fog's).
        void BindRoomTextures(IDirect3DDevice9* dev, int set)
        {
            for (int k = 0; k < 4; ++k)
                dev->SetTexture(kFirstSampler + k, set >= 0 && g_sets[set].state == 2 && g_sets[set].tex[k] ? g_sets[set].tex[k] : g_whiteTex);
            dev->SetTexture(kFirstSampler + 4, g_atlasTex);
            dev->SetTexture(kTableSampler, g_tableTex);
            if (g_samplerFrame == g_frame) return;
            g_samplerFrame = g_frame;
            for (int k = 0; k < 6; ++k)
            {
                const DWORD s = static_cast<DWORD>(kFirstSampler + k);
                const bool table = s == static_cast<DWORD>(kTableSampler);
                dev->SetSamplerState(s, D3DSAMP_MINFILTER, table ? D3DTEXF_POINT : (k < 4 ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR));
                dev->SetSamplerState(s, D3DSAMP_MAGFILTER, table ? D3DTEXF_POINT : D3DTEXF_LINEAR);
                dev->SetSamplerState(s, D3DSAMP_MIPFILTER, table ? D3DTEXF_NONE : D3DTEXF_LINEAR);
                dev->SetSamplerState(s, D3DSAMP_MAXANISOTROPY, 8);
                const DWORD address = table || k == 4 ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP;
                dev->SetSamplerState(s, D3DSAMP_ADDRESSU, address);
                dev->SetSamplerState(s, D3DSAMP_ADDRESSV, address);
                dev->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
                dev->SetSamplerState(s, D3DSAMP_MAXMIPLEVEL, 0);
                dev->SetSamplerState(s, D3DSAMP_MIPMAPLODBIAS, 0);
            }
        }

        // A texture of A8R8G8B8 texels (0xAARRGGBB) with its mips (box filtered), managed.
        IDirect3DTexture9* TextureOf(IDirect3DDevice9* dev, const std::vector<uint32_t>& texels, int width, int height)
        {
            IDirect3DTexture9* tex = nullptr;
            if (FAILED(dev->CreateTexture(width, height, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) || !tex) return nullptr;
            std::vector<uint32_t> level = texels;
            int w = width, h = height;
            for (DWORD m = 0; m < tex->GetLevelCount(); ++m)
            {
                if (m)
                {
                    const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
                    std::vector<uint32_t> next(static_cast<size_t>(nw) * nh);
                    for (int y = 0; y < nh; ++y)
                        for (int x = 0; x < nw; ++x)
                        {
                            uint32_t sum[4] = {};
                            for (int dy = 0; dy < 2; ++dy)
                                for (int dx = 0; dx < 2; ++dx)
                                {
                                    const uint32_t t = level[static_cast<size_t>(std::min(h - 1, y * 2 + dy)) * w + std::min(w - 1, x * 2 + dx)];
                                    for (int c = 0; c < 4; ++c) sum[c] += (t >> (8 * c)) & 0xFF;
                                }
                            uint32_t v = 0;
                            for (int c = 0; c < 4; ++c) v |= ((sum[c] + 2) / 4) << (8 * c);
                            next[static_cast<size_t>(y) * nw + x] = v;
                        }
                    level.swap(next);
                    w = nw; h = nh;
                }
                D3DLOCKED_RECT lr{};
                if (FAILED(tex->LockRect(m, &lr, nullptr, 0))) { tex->Release(); return nullptr; }
                for (int y = 0; y < h; ++y) std::memcpy(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch, &level[static_cast<size_t>(y) * w], w * 4);
                tex->UnlockRect(m);
            }
            return tex;
        }

        void Release(IDirect3DTexture9*& t) { if (t) { t->Release(); t = nullptr; } }

        // The device's textures, made on the main thread (Update): the table after a load, the atlas and the white texel
        // once, a wanted texture set a frame.
        void RoomTexturesUpdate()
        {
            auto* dev = Device();
            if (!dev) return;
            if (g_tableDirty)
            {
                g_tableDirty = false;
                Release(g_tableTex);
                if (g_windows.Windows())
                {
                    IDirect3DTexture9* tex = nullptr;
                    const int h = g_windows.Height();
                    if (SUCCEEDED(dev->CreateTexture(rooms::kWidth, h, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED, &tex, nullptr)) && tex)
                    {
                        D3DLOCKED_RECT lr{};
                        if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0)))
                        {
                            const std::vector<float>& t = g_windows.Texels();
                            for (int y = 0; y < h; ++y)
                                std::memcpy(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch, &t[static_cast<size_t>(y) * rooms::kWidth * 4], rooms::kWidth * 16);
                            tex->UnlockRect(0);
                            g_tableTex = tex;
                        }
                        else tex->Release();
                    }
                    if (!g_tableTex) g_textureError = "the window table's texture (RGBA 32-bit floats) couldn't be made";
                }
            }
            if (!g_atlasTex)
            {
                std::vector<uint32_t> atlas;
                FurnitureAtlas(atlas);
                g_atlasTex = TextureOf(dev, atlas, rooms::kAtlasWidth, rooms::kAtlasHeight);
            }
            if (!g_whiteTex) g_whiteTex = TextureOf(dev, std::vector<uint32_t>(16, 0xFFFFFFFFu), 4, 4);
            for (int set = 0; set < kTextureSets; ++set)
            {
                SetTextures& st = g_sets[set];
                if (st.state != 1) continue;
                const TextureSet& ts = TextureSetAt(set);
                const char* paths[4] = { ts.floor, ts.wall, ts.ceiling, ts.wood };
                bool ok = true;
                for (int k = 0; k < 4; ++k)
                {
                    std::string error;
                    st.tex[k] = blp::Load(dev, paths[k], error);
                    float rgb[3] = { 0.5f, 0.5f, 0.5f };
                    const bool mean = blp::AverageColour(paths[k], rgb);
                    st.gain[k] = 1.0f / std::max(0.05f, Lum(rgb));
                    if (!st.tex[k] || !mean) { ok = false; g_textureError = std::string(paths[k]) + ": " + (error.empty() ? "no average colour" : error); }
                }
                st.state = ok ? 2 : 3;
                if (!ok) for (IDirect3DTexture9*& t : st.tex) Release(t);
                break;   // one set a frame
            }
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

    RoomToggles Toggles(const Settings& s)
    {
        RoomToggles t;
        t.variety = s.roomVariety != 0; t.lightsOut = s.lightsOut != 0;
        t.varietyStrength = s.varietyStrength; t.lightsOutStrength = s.lightsOutStrength;
        return t;
    }

    DarkBand RoomBand(const RoomStyle& style, uint32_t flags, const RoomInputs& in, const Settings& s)
    {
        const RoomToggles t = Toggles(s);
        if (in.clock) return BandOf(*in.clock, style, flags, t);
        const float now = DarkShare(style, flags, t, in.night, in.hour);
        return Band(now, now);
    }

    uint32_t FallbackSeed(uint32_t path, uint32_t hookSeed) { return path ? path : hookSeed; }

    bool FrostedLit(uint32_t kind, bool selfLit, bool unlit, uint32_t buildingFlags, const Settings& s)
    {
        return kind == kFrosted && (selfLit || unlit) && !(buildingFlags & kBuildingDungeon) &&
               (s.frostedLightsOut || s.hearthFlicker > 0.0f);
    }

    RoomToggles FrostedToggles(const Settings& s)
    {
        RoomToggles t;
        t.variety = true; t.lightsOut = s.frostedLightsOut != 0;
        t.varietyStrength = 1.0f; t.lightsOutStrength = s.lightsOutStrength;
        return t;
    }

    bool IsIronforge(const Building* b)
    {
        static const uint32_t ironforge = PathHash("world\\wmo\\khazmodan\\cities\\ironforge\\ironforge.wmo");
        return b && b->path == ironforge;
    }

    DarkBand FrostedBand(const RoomStyle& style, const Building* b, const RoomInputs& in, const Settings& s)
    {
        if (!s.frostedLightsOut || (s.ironforgeAlwaysLit && IsIronforge(b))) return Band(0.0f, 0.0f);   // every window lit
        const RoomToggles t = FrostedToggles(s);
        const uint32_t flags = b ? b->flags : 0;
        if (in.clock) return BandOf(*in.clock, style, flags, t);
        const float now = DarkShare(style, flags, t, in.night, in.hour);
        return Band(now, now);
    }

    float FlickerAmplitude(float amount, float night)
    {
        return std::isfinite(amount) ? Clamp01(amount) * Clamp01(night) : 0.0f;
    }

    bool BacklitBatch(const Material& m, uint32_t buildingFlags, const Settings& s)
    {
        return s.titanBacklit && m.backlit > 0.0f && (buildingFlags & kBuildingBacklit);
    }

    float PulseScale(const Material& m, const Settings& s)
    {
        if (!(m.flags & kBacklitPulse)) return 1.0f;
        return std::isfinite(s.vezaxPulse) ? std::max(0.0f, s.vezaxPulse) : 0.0f;
    }

    void BacklitConstants(const Material& m, double seconds, const Settings& s, float out[4 * kConstantCount], float day[4 * kDayRegisters])
    {
        // The inside look (the texel, more saturated, x the light behind it; never below the client's own colour), with a
        // steady light behind the glass in place of the sky and the sun: nothing in it depends on the hour or the weather.
        float glow[3];
        BacklitGlow(m, seconds, s.backlitStrength, PulseScale(m, s), glow);
        out[4] = 1.0f;                                  // c193.x: seen from inside, whatever the camera's indoors blend
        out[6] = 0.0f;                                  // c193.z: no room behind it
        for (int k = 0; k < 3; ++k) out[8 + k] = glow[k];   // c194.rgb: the light behind it
        out[11] = 0.0f;                                 // c194.w: no sun through it
        out[19] = 0.0f;                                 // c196.w: no sky in it
        out[20] = 0.0f; out[21] = 0.0f;                 // c197.xy: no darkening by day, no glint
        out[31] = 1.0f;                                 // c199.w: the glow is c194's
        out[39] = 0.0f;                                 // c201.w: not lit frosted glass
        day[0] = 0.0f;                                  // c85.x: not cooler by day
        day[4] = 0.0f;                                  // c86.x: no sun's patch
    }

    bool LightOfPane(const Pane& p, const Material* m, const Light& light, const float sun[3], const float sunColour[3], double seconds,
                     const Settings& s, PaneLight& out)
    {
        out = PaneLight{};
        out.pane = &p;
        const float through = LookOf(p.kind).through;
        if ((p.flags & kPaneBacklit) && m && BacklitBatch(*m, kBuildingBacklit, s))
        {
            float glow[3];
            BacklitGlow(*m, seconds, s.backlitStrength, PulseScale(*m, s), glow);
            BacklitDirection(p, out.dir);
            for (int k = 0; k < 3; ++k) out.colour[k] = p.light[k] * glow[k] * backlit::kFloorShare * through;
            out.backlit = true;
            return Lum(out.colour) > 0.0f;
        }
        if (light.sunGain <= 0.01f || (p.flags & kPaneNoSky) || p.out[0] * sun[0] + p.out[1] * sun[1] + p.out[2] * sun[2] <= 0.05f) return false;
        for (int k = 0; k < 3; ++k) { out.dir[k] = sun[k]; out.colour[k] = p.light[k] * sunColour[k] * through * light.sunGain; }
        return true;
    }

    void LightFacing(const PaneLight* lights, size_t n, const float sun[3], float out[3])
    {
        bool any = false;
        for (size_t i = 0; i < n; ++i) any = any || lights[i].backlit;
        if (!any) { for (int k = 0; k < 3; ++k) out[k] = sun[k]; return; }   // sunlit panes alone: the sun's, as before
        static thread_local std::vector<float> dirs, weights;
        dirs.assign(3 * n, 0.0f);
        weights.assign(n, 0.0f);
        for (size_t i = 0; i < n; ++i)
        {
            for (int k = 0; k < 3; ++k) dirs[3 * i + k] = lights[i].dir[k];
            weights[i] = Lum(lights[i].colour);
        }
        const float up[3] = { 0, 0, 1 };   // (lights that cancel: from overhead)
        MeanDirection(dirs.data(), weights.data(), n, up, out);
    }

    void ChoosePanes(const std::vector<PaneLight>& panes, const float room[3], const float focus[3], float slack, bool roomOnly, size_t max,
                     std::vector<uint32_t>& last, std::vector<PaneLight>& out)
    {
        out.clear();
        struct Ranked { int tier; float score; size_t at; };
        static thread_local std::vector<Ranked> ranked;
        ranked.clear();
        const bool haveRoom = room && std::isfinite(room[0]) && std::isfinite(room[1]) && std::isfinite(room[2]);
        const bool haveFocus = focus && std::isfinite(focus[0]) && std::isfinite(focus[1]) && std::isfinite(focus[2]);
        for (size_t i = 0; i < panes.size(); ++i)
        {
            const Pane* p = panes[i].pane;
            if (!p) continue;
            bool inRoom = haveRoom;
            for (int k = 0; k < 3 && inRoom; ++k) inRoom = room[k] >= p->roomMin[k] - slack && room[k] <= p->roomMax[k] + slack;
            if (roomOnly && !inRoom) continue;
            const float* d = panes[i].dir;
            const float cosine = std::max(0.0f, d[0] * p->out[0] + d[1] * p->out[1] + d[2] * p->out[2]);
            float score = 4.0f * std::max(0.0f, p->halfWidth) * std::max(0.0f, p->halfHeight) * cosine * std::max(0.0f, Lum(panes[i].colour));
            if (haveFocus)
            {
                // the nearest point to the focus of the light's middle ray, from the glass into the room (along -dir)
                const float rel[3] = { p->pos[0] - focus[0], p->pos[1] - focus[1], p->pos[2] - focus[2] };
                const float t = std::clamp(rel[0] * d[0] + rel[1] * d[1] + rel[2] * d[2], 0.0f, kPaneReach);
                const float q[3] = { rel[0] - d[0] * t, rel[1] - d[1] * t, rel[2] - d[2] * t };
                const float s = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]) / kPaneFocusFall;
                score /= 1.0f + s * s;
            }
            if (std::binary_search(last.begin(), last.end(), p->id)) score *= kPaneKeep;
            if (!std::isfinite(score)) score = 0.0f;
            ranked.push_back({ inRoom ? 0 : 1, score, i });
        }
        std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
            return a.tier != b.tier ? a.tier < b.tier : a.score > b.score;
        });
        for (const Ranked& r : ranked)
        {
            if (out.size() >= max) break;
            out.push_back(panes[r.at]);
        }
        last.clear();
        for (const PaneLight& pl : out) last.push_back(pl.pane->id);
        std::sort(last.begin(), last.end());
    }

    float FadeShare(float weight)
    {
        const float w = Clamp01(weight);
        return w * w * (3.0f - 2.0f * w);
    }

    void PaneFader::Step(const std::vector<PaneLight>& chosen, const std::vector<PaneLight>& lit, float dt, size_t max,
                         std::vector<PaneLight>& out, std::vector<uint32_t>& kept)
    {
        out.clear();
        const float step = (std::isfinite(dt) ? std::clamp(dt, 0.0f, 1.0f) : 0.0f) / kPaneFade;
        auto litOf = [&lit](uint32_t id) -> const PaneLight* {
            for (const PaneLight& pl : lit)
                if (pl.pane && pl.pane->id == id) return &pl;
            return nullptr;
        };
        auto isChosen = [&chosen](uint32_t id) {
            for (const PaneLight& pl : chosen)
                if (pl.pane && pl.pane->id == id) return true;
            return false;
        };
        // the slots: toward 1 while chosen, toward 0 when not (or no longer lit: with the light it had); free at 0
        size_t n = 0;
        for (size_t i = 0; i < slots.size(); ++i)
        {
            Slot s = slots[i];
            const PaneLight* now = litOf(s.id);
            if (now) s.light = *now;
            s.wanted = now && isChosen(s.id);
            s.weight = std::clamp(s.weight + (s.wanted ? step : -step), 0.0f, 1.0f);
            if (!s.wanted && s.weight <= 0.0f) continue;
            slots[n++] = s;
        }
        slots.resize(n);
        // (fewer slots than before: the faintest of those not chosen go first, then the faintest)
        if (slots.size() > max)
        {
            std::stable_sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) {
                return a.wanted != b.wanted ? a.wanted : a.weight > b.weight;
            });
            slots.resize(max);
        }
        // a pane newly chosen takes a free slot, in the choice's order, from its first frame's share
        for (const PaneLight& pl : chosen)
        {
            if (slots.size() >= max) break;
            if (!pl.pane) continue;
            bool have = false;
            for (const Slot& s : slots) have = have || s.id == pl.pane->id;
            if (!have) slots.push_back({ pl.pane->id, std::clamp(step, 0.0f, 1.0f), true, pl });
        }
        for (const Slot& s : slots)
        {
            const float share = FadeShare(s.weight);
            if (!s.light.pane || !(share > 0.0f)) continue;
            PaneLight f = s.light;
            for (float& c : f.colour) c *= share;
            out.push_back(f);
        }
        kept.clear();
        for (const Slot& s : slots) kept.push_back(s.id);
        std::sort(kept.begin(), kept.end());
    }

    void RoomBatchConstants(const RoomStyle& style, const Building* b, const Light& light, const RoomInputs& in, const Settings& s,
                            float out[4 * kConstantCount], float seed[4 * kSeedRegisters], float look[4 * kLookRegisters])
    {
        float* c = out;
        const bool variety = s.roomVariety != 0;
        const float v = variety ? std::max(0.0f, s.varietyStrength) : 0.0f;
        const float night = Clamp01(in.night);
        const uint32_t flags = b ? b->flags : 0;
        const bool empty = variety && (flags & kBuildingEmpty);
        const bool darkPlace = (flags & kBuildingDarkPlace) != 0;
        const bool underground = (flags & kBuildingUnderground) != 0;
        // Frosted and stained glass's rooms (FrostedRooms, StainedRooms: BatchConstants gave them their share): none in a
        // dungeon (its frosted glass keeps the client's look; its stained glass is architecture, or titan glass).
        if ((in.kind == kFrosted || in.kind == kStained) && !KindHasRoom(in.kind, flags, s)) c[6] = 0.0f;
        const bool frostedRoom = in.kind == kFrosted && c[6] > 0.0f;
        // (underground, behind frosted glass: no daylight, its lamps lit and its dark rooms' ambient at any hour)
        const float roomNight = frostedRoom && underground ? 1.0f : night;
        // c198.w the day's light in the room (x the style's brightness; about the glass's own: DayRoomLight, with the
        // darkening BatchConstants put in c197.x); c199.rgb and c202.rgb the building's tints (its hash picks between
        // them), c202.w how far buildings differ in brightness.
        // By day the buildings (in a city each 14-yd block) differ less (room::kDayVariety of it): the room is only a
        // hint behind the glass, and a block's edge across a facade is no edge in it.
        const float* tint = variety ? style.tint : kRoomTint;
        const float dayVariety = 1.0f + (room::kDayVariety - 1.0f) * Clamp01(light.rooms);
        for (int k = 0; k < 3; ++k)
        {
            c[28 + k] = tint[k];
            c[40 + k] = variety ? tint[k] + (style.tint2[k] - tint[k]) * std::min(1.0f, v) * dayVariety : tint[k];
        }
        // (with the rooms' textures, the tints only part of the way from their grey: the textures keep their own colours)
        if (s.roomTextures)
            for (int base : { 28, 40 })
            {
                const float l = Lum(c + base);
                for (int k = 0; k < 3; ++k) c[base + k] = l + (c[base + k] - l) * rooms::kTintWithTextures;
            }
        c[27] = frostedRoom && underground ? 0.0f : DayRoomLight(light.rooms, c[20], c + 28, c + 40, variety ? style.brightness : 1.0f, s);
        c[43] = variety ? Clamp01(style.spread * v * dayVariety) : 0.0f;
        // The dark band (c203.w lo, c200.w hi; c212.w its slope, c213.w its base): the share of rooms dark (DarkShare:
        // by day the style's few, at night by the hour, inns late, shops early; every room in an empty building, and at
        // night in a dark place) by the rooms' clock, so a room the threshold crosses, or a jump of the time, fades.
        // Lit frosted glass: its own band (FrostedBand), the cell and hash run for it (c201.w); a frosted room's the same.
        const bool frosted = in.kind == kFrosted && FrostedLit(in.kind, in.selfLit, in.unlit, flags, s);
        const DarkBand band = frosted || frostedRoom ? FrostedBand(style, b, in, s) : RoomBand(style, flags, in, s);
        c[47] = band.lo;
        c[35] = band.hi;
        // c203.rgb the lamp at night, behind the windows the client lights (SIDN; frosted glass it draws at full too): its
        // colour at luminance 1 over the room's mean light, so a lit room is about as bright as the client's window (the
        // shader x its luminance).
        const bool lamp = s.litRooms && (in.selfLit || (frostedRoom && in.unlit)) && !darkPlace && !empty;
        float lampN[3];
        const float ll = std::max(1e-3f, 0.2126f * style.lamp[0] + 0.7152f * style.lamp[1] + 0.0722f * style.lamp[2]);
        for (int k = 0; k < 3; ++k) lampN[k] = std::max(0.0f, style.lamp[k]) / ll;
        const float lit = std::max(1e-3f, 0.2126f * c[28] * lampN[0] + 0.7152f * c[29] * lampN[1] + 0.0722f * c[30] * lampN[2]);
        const float gain = lamp ? roomNight * std::max(0.0f, s.litRoomStrength) / (lit * room::kNightMean) : 0.0f;
        for (int k = 0; k < 3; ++k) c[44 + k] = lampN[k] * gain;
        // c204 the curtains' band in the room's hash (the style's shelves' weight .. + its curtains': the layouts' thresholds)
        // and the detail's strength; c205 the curtains, and how much of the client's lit window a dark room hides at night.
        float t[3] = { 0, 0, 0 };
        if (s.roomLayouts) LayoutThresholds(style.weights, t);
        for (int k = 0; k < 3; ++k) c[48 + k] = t[k];
        c[51] = s.roomLayouts ? std::max(0.0f, s.layoutStrength) : 0.0f;
        // (by day the curtains part of the way to their grey: seen through the cooler glass a red cloth reads pink)
        const float clothGrey = room::kCurtainDayGrey * Clamp01(light.rooms);
        const float clothLum = Lum(style.curtain);
        for (int k = 0; k < 3; ++k) c[52 + k] = style.curtain[k] + (clothLum - style.curtain[k]) * clothGrey;
        c[55] = s.litRooms && in.selfLit ? kGlowHidden * night : 0.0f;
        // c201: the hearth clock (.y), the flicker's amplitude at night (.z: a lit frosted window's hearth, HearthFlicker;
        // a lamp-lit room's lamp, LampFlicker), lit frosted glass (.w). A dark frosted window hides kFrostedHidden of its
        // glow at night (c205.w).
        c[37] = in.hearthClock;
        c[38] = lamp ? FlickerAmplitude(s.lampFlicker, night) : 0.0f;
        c[39] = 0.0f;
        if (frosted)
        {
            if (!frostedRoom) for (int k = 44; k < 47; ++k) c[k] = 0.0f;   // (no room: no lamp; a frosted room's lamp flickers with the hearth)
            c[55] = s.frostedLightsOut ? hearth::kFrostedHidden * night : 0.0f;
            c[38] = FlickerAmplitude(s.hearthFlicker, night);
            c[39] = 1.0f;
        }
        else if (frostedRoom) { c[55] = 0.0f; c[38] = 0.0f; }   // frosted glass the client doesn't light, or neither lights out nor flicker: steady
        // A dark room at night isn't a black void (DarkRoomLight): the moon's and the night's light at its window (the day's
        // light's shading: brighter by the window, the corners darker, the furniture dim shapes), in Light::moon's colour
        // (underground the city's warm ambient), mixed into c198 by its share; only dark rooms take it (c218.zw), every room
        // where there's no lamp. Without it c198 and c218.zw are exactly the day's.
        const float dayLight = c[27];
        const float moon = MoonRoomLight(roomNight, c + 28, c + 40, s);
        if (moon > 0.0f)
        {
            float hue[3];
            const float ul = Lum(room::kUndergroundHue);
            for (int k = 0; k < 3; ++k) hue[k] = underground ? room::kUndergroundHue[k] / ul : light.moon[k];
            const float w = moon / (dayLight + moon);
            for (int k = 0; k < 3; ++k) c[24 + k] += (hue[k] - c[24 + k]) * w;
            c[27] = dayLight + moon;
        }
        if (look)
        {
            look[2] = 1.0f - room::kDarkDay;
            look[3] = room::kDarkDay;
            if (moon > 0.0f)
            {
                const float sum = dayLight + moon;
                look[2] = ((1.0f - room::kDarkDay) * dayLight - (lamp ? moon : 0.0f)) / sum;
                look[3] = (room::kDarkDay * dayLight + moon) / sum;
            }
        }
        // Underground (Ironforge's streets are its interior groups: what's behind its glass is more of the city, not the
        // sky): the street's look whatever the camera's indoors blend, with no sky, darkening or glint in it.
        if (flags & kBuildingUnderground) { c[4] = 0.0f; c[19] = 0.0f; c[20] = 0.0f; c[21] = 0.0f; }
        // c212 the seed (two numbers 0..1023), the block (a city's buildings), the band's slope; c213 the building's
        // origin: a placement's from the table, else the instance's (a WMO's default row has none), else the world's;
        // the band's base.
        const uint32_t sd = b ? b->seed : in.fallbackSeed;
        seed[0] = static_cast<float>(sd & 1023u);
        seed[1] = static_cast<float>((sd >> 10) & 1023u);
        seed[2] = b && variety && (flags & kBuildingCity) ? 1.0f / room::kBlock : 0.0f;
        seed[3] = band.slope;
        const float* origin = b && b->uniqueId ? b->pos : in.origin;
        for (int k = 0; k < 3; ++k) seed[4 + k] = origin ? origin[k] : 0.0f;
        seed[7] = band.base;
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
        // c193 inside weight, strength, the room's share (by day and at dusk the day's, at night the lamp-lit room's:
        // the settings' shares x the kind's), thickness
        c[4] = inside; c[5] = 1.0f;
        const float share = s.roomShareNight + (s.roomShareDay - s.roomShareNight) * Clamp01(light.rooms);
        const float room = m.kind == kFrosted ? (s.frostedRooms ? kFrostedRoom : 0.0f) : m.kind == kStained ? (s.stainedRooms ? kStainedRoom : 0.0f) : look.room;
        c[6] = s.fakeInteriors && s.outsideReflection ? Clamp01(share * room) : 0.0f;
        c[7] = look.thickness;
        // c194 the sky through the glass, the sun's gain
        for (int k = 0; k < 3; ++k) c[8 + k] = light.sky[k];
        c[11] = light.sunGain;
        // c195 toward the sun, saturation
        for (int k = 0; k < 3; ++k) c[12 + k] = sun[k];
        c[15] = look.saturation;
        // c196 the reflected sky, the reflection at grazing (a little at night: the lit windows stay lit)
        const float reflect = s.outsideReflection && !matte ? s.reflectionStrength : 0.0f;
        for (int k = 0; k < 3; ++k) c[16 + k] = sky[k] * (0.15f + 0.85f * day) * 1.1f;
        c[19] = look.fresnel * reflect * (0.3f + 0.7f * day);
        // c197 darkening outside by day (the settings' x the kind's: a few tens of percent at most), glint, F0 (the
        // shader's Schlick x c196.w: head on by day the settings' F0 x the kind's, at night room::kNightF0 x the kind's
        // as before), diffusion
        c[20] = s.outsideReflection ? std::min(0.9f, std::max(0.0f, s.outsideDarkening) * look.darken) * Clamp01(day) : 0.0f;
        c[21] = look.glint * reflect * light.sunGain * 3.0f;
        const float nightF0 = room::kNightF0 * look.f0;
        const float f0 = nightF0 + (std::max(0.0f, s.reflectionF0) * look.dayF0 - nightF0) * Clamp01(day);
        c[22] = Clamp01(f0 / std::max(0.05f, look.fresnel));
        c[23] = look.diffusion;
        // c198 the day's light in the room: its colour at luminance 1 (the sky's and the sun's through a pane, room::kDayHue
        // of the way from white; white with neither), its strength (DayRoomLight); c199 its tint, the inside's brightness
        {
            float dl[3];
            for (int k = 0; k < 3; ++k) dl[k] = std::max(0.0f, sky[k]) + std::max(0.0f, sunColour[k]) * std::max(0.0f, light.sunGain);
            const float dlum = Lum(dl);
            for (int k = 0; k < 3; ++k)
                c[24 + k] = dlum > 1e-3f && std::isfinite(dlum) ? 1.0f + (std::min(3.0f, dl[k] / dlum) - 1.0f) * room::kDayHue : 1.0f;
        }
        c[27] = DayRoomLight(light.rooms, c[20], kRoomTint, kRoomTint, 1.0f, s);
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

    void LookConstants(const Light& light, float out[4 * kLookRegisters])
    {
        (void)light;
        RoomLook(out);
        out[2] = 1.0f - room::kDarkDay;   // the room's light lit x .z + .w: by day a dark room keeps kDarkDay of it
        out[3] = room::kDarkDay;
    }

    float MoonRoomLight(float night, const float tintA[3], const float tintB[3], const Settings& s)
    {
        if (!std::isfinite(s.darkRoomLight) || !(s.darkRoomLight > 0.0f)) return 0.0f;
        const float tintLum = std::max(0.05f, 0.5f * (Lum(tintA) + Lum(tintB)));
        return std::min(1.0f, s.darkRoomLight) * Clamp01(night) / (room::kMoonMean * tintLum);
    }

    bool KindHasRoom(uint32_t kind, uint32_t buildingFlags, const Settings& s)
    {
        if (kind == kFrosted) return s.frostedRooms && !(buildingFlags & kBuildingDungeon);
        if (kind == kStained) return s.stainedRooms && !(buildingFlags & kBuildingDungeon);
        return kind == Kind::kClear || kind == kLeaded;
    }

    void DayConstants(const Material& m, const Light& light, const Settings& s, uint32_t buildingFlags, float out[4 * kDayRegisters])
    {
        const Look& look = LookOf(m.kind);
        // c85: the glass cooler by day (DayCoolness x the kind's x the day; none matte, underground or without the outside
        // look), more toward the top of the pane and at a slant, the sky's hue's share (the rest grey).
        const bool sky = s.outsideReflection && !(m.flags & kMatte) && !(buildingFlags & kBuildingUnderground);
        out[0] = sky && std::isfinite(s.dayCoolness) ? Clamp01(s.dayCoolness * look.cool * Clamp01(light.day)) : 0.0f;
        out[1] = room::kCoolUp; out[2] = room::kCoolGraze; out[3] = room::kCoolTint;
        // c86: the room by day: the sun's patch on its floor (x the sun through: none at dusk, in cloud, underground), and
        // while the client's windows are unlit farther darker and lighter low (the light comes in at the window and falls).
        out[4] = buildingFlags & kBuildingUnderground ? 0.0f : room::kPatch * Clamp01(light.sunGain);
        out[5] = room::kFarDay * Clamp01(light.rooms);
        out[6] = room::kFallDay * Clamp01(light.rooms);
        out[7] = 0.0f;
    }

    void RoomRegisters(const float axes[9], const float origin[3], const float camera[3], const WindowTable::Grid* grid, int tableHeight,
                       uint32_t cls, const float scales[4], const float gains[4], const Settings& s, float out[4 * kRoomRegisters],
                       uint32_t glassKind, const float notGlass[2])
    {
        std::memset(out, 0, sizeof(float) * 4 * kRoomRegisters);
        // c15..c17: the point in its WMO's space = the rows . (P about the camera, 1): row i its axis i in the world, the
        // offset that axis . (the camera - the origin), in doubles (a city's -8800 yd never reach the result)
        for (int i = 0; i < 3; ++i)
        {
            double w = 0.0;
            for (int k = 0; k < 3; ++k)
            {
                out[4 * i + k] = axes[3 * i + k];
                w += static_cast<double>(axes[3 * i + k]) * (static_cast<double>(camera[k]) - static_cast<double>(origin[k]));
            }
            out[4 * i + 3] = static_cast<float>(w);
        }
        // c18..c20: the group's grid (its corner, its first cell's texel; 1 / a cell, x, x y cells, 1 / the table's height;
        // the last cell; 1: a grid)
        if (grid && tableHeight > 0)
        {
            for (int k = 0; k < 3; ++k) { out[12 + k] = grid->min[k]; out[20 + k] = static_cast<float>(grid->n[k] - 1); }
            out[15] = static_cast<float>(grid->base);
            out[16] = 1.0f / grid->cell;
            out[17] = static_cast<float>(grid->n[0]);
            out[18] = static_cast<float>(grid->n[0] * grid->n[1]);
            out[19] = 1.0f / static_cast<float>(tableHeight);
            out[23] = 1.0f;
        }
        // c21: the room's depth (the class's), its spread, the furniture (RoomDetail x LayoutStrength, at most 1)
        const RoomKind& kind = RoomKindOf(cls);
        out[24] = kind.depth;
        out[25] = kDepthSpread;
        out[26] = s.roomDetail && std::isfinite(s.layoutStrength) ? std::min(1.0f, std::max(0.0f, s.layoutStrength)) : 0.0f;
        // c22, c23: the textures (floor, walls, ceiling, wood): 1 / the yards each repeats over, each one's gain to a mean of 1
        for (int k = 0; k < 4; ++k)
        {
            out[28 + k] = 1.0f / std::max(0.1f, scales[k]);
            out[32 + k] = std::isfinite(gains[k]) ? std::min(8.0f, std::max(0.0f, gains[k])) : 1.0f;
        }
        // c24, c25: the furniture's tiles, the middle's and the back's
        for (int k = 0; k < 4; ++k) { out[36 + k] = static_cast<float>(kind.mid[k]); out[40 + k] = static_cast<float>(kind.back[k]); }
        // c26: behind frosted glass (FrostedRooms) the footprint at least kFrostedBlur yd, its windows' rooms allowed, the
        // faces' shades kFrostedFlatten of the way to kFaceMean; c27: behind stained glass (StainedRooms) the room x
        // lerp(1, the glass's hue, kStainedTint). Any other kind the room as it was: (0, 0, 0, kFaceMean), (1, 0, 0, 0).
        const bool frosted = glassKind == kFrosted && s.frostedRooms, stained = glassKind == kStained && s.stainedRooms;
        out[44] = frosted ? kFrostedBlur : 0.0f;
        out[45] = frosted ? 1.0f : 0.0f;
        out[46] = frosted ? kFrostedFlatten : 0.0f;
        out[47] = kFaceMean;
        out[48] = stained ? 1.0f - kStainedTint : 1.0f;
        out[49] = stained ? kStainedTint : 0.0f;
        // c27.zw: the texture's corner that isn't glass (Material::notGlass), as 1 - where it starts; 0: none (the
        // shader's frac(uv) - 1 + this is never >= 0)
        if (notGlass && notGlass[0] < 1.0f && notGlass[1] < 1.0f)
        {
            out[50] = 1.0f - notGlass[0];
            out[51] = 1.0f - notGlass[1];
        }
    }

    bool IsRotation(const float a[9])
    {
        if (!a) return false;
        for (int i = 0; i < 9; ++i)
            if (!std::isfinite(a[i])) return false;
        for (int i = 0; i < 3; ++i)
            for (int j = i; j < 3; ++j)
            {
                const float d = a[3 * i] * a[3 * j] + a[3 * i + 1] * a[3 * j + 1] + a[3 * i + 2] * a[3 * j + 2];
                if (std::fabs(d - (i == j ? 1.0f : 0.0f)) > 0.02f) return false;
            }
        return true;
    }

    const WindowTable& Windows() { return g_windows; }

    namespace
    {
        void FloorFrameConstants(float out[4 * kConstantCount])
        {
            std::memset(out, 0, sizeof(float) * 4 * kConstantCount);
            // c195: the surfaces' facing to the panes' light (the sun's; with backlit panes their lights': LightFacing)
            for (int k = 0; k < 3; ++k) { out[12 + k] = g_floorFacing[k]; out[32 + k] = g_sunColour[k]; }
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
        g_texturePaths.clear();
        g_lead.clear();
        g_byTexture.clear();
        g_materialById.clear();
        g_styles.clear();
        g_styleById.clear();
        g_building = nullptr;
        g_loadProblem = false;
        if (!cdbc || !cdbc->HasFeature("cdbc-load"))
        {
            g_status = "wxl-seyris-tools (cdbc) not available";
            g_windowStatus = "GlassWindows.cdbc: not read, wxl-seyris-tools (cdbc) not available (no rooms behind the glass)";
            g_loadProblem = true;
            g_panes.Build({}); g_buildings.Build({}); g_windows.Build({}); g_tableDirty = true;
            g_near.clear(); g_floorList.clear(); g_floorPanes = 0;
            g_floorFader.Clear(); g_shaftFader.Clear(); g_floorKept.clear(); g_shaftKept.clear();
            return;
        }
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
                // titan glass lit from behind (none in an older file)
                m.backlit = std::max(0.0f, F(row, "Backlit", 0.0f));
                m.pulse = std::max(0.0f, F(row, "Pulse", 0.0f));
                m.pulseSeconds = F(row, "PulseSeconds", 0.0f);
                const char* pulse[3] = { "PulseR", "PulseG", "PulseB" };
                for (int k = 0; k < 3; ++k) m.pulseColour[k] = std::max(0.0f, F(row, pulse[k], 1.0f));
                // the corner that isn't glass (none in an older file; anything but 0 .. 1: none)
                const float ng[2] = { F(row, "NotGlassU", 1.0f), F(row, "NotGlassV", 1.0f) };
                for (int k = 0; k < 2; ++k) m.notGlass[k] = std::isfinite(ng[k]) && ng[k] >= 0.0f && ng[k] < 1.0f ? ng[k] : 1.0f;
                if (m.notGlass[0] >= 1.0f || m.notGlass[1] >= 1.0f) m.notGlass[0] = m.notGlass[1] = 1.0f;
                g_materials.push_back(m);
                g_texturePaths.push_back(texture);
                g_byTexture[Key(texture)] = static_cast<uint32_t>(g_materials.size());
                g_materialById[cdbc->Value(t, row, "ID", 0)] = static_cast<uint32_t>(g_materials.size());
            }
            cdbc->Release(t);
            char line[160];
            unsigned backlitTextures = 0, cornered = 0;
            for (const Material& mm : g_materials)
            {
                backlitTextures += mm.backlit > 0.0f ? 1 : 0;
                cornered += mm.notGlass[0] < 1.0f ? 1 : 0;
            }
            std::snprintf(line, sizeof(line), "GlassMaterials.cdbc: %u glass textures (%u titan glass that can be lit from behind, %u with a corner that isn't glass)",
                          static_cast<unsigned>(g_materials.size()), backlitTextures, cornered);
            status = line;
        }
        else { status = std::string("GlassMaterials.cdbc: ") + (err[0] ? err : "not found") + " (no glass)"; g_loadProblem = true; }

        // The see-through redraw's lead mask: where each texture's panes are (optional: its light mask without them).
        g_lead.assign(g_materials.size(), LeadRule{});
        err[0] = 0;
        if (void* t = g_materials.empty() ? nullptr : cdbcload::LoadAppendOnly(cdbc, kLeadDef, err, sizeof(err)))
        {
            auto F = [&](const void* row, const char* f, float missing) { return cdbcload::Float(cdbc, t, row, f, missing); };
            unsigned rects = 0, unknown = 0, textures = 0;
            for (uint32_t i = 0, n = cdbc->RowCount(t); i < n; ++i)
            {
                const void* row = cdbc->RowAt(t, i);
                if (!row) continue;
                const uint32_t m = MaterialOf(cdbc->GetString(t, row, "Texture"));
                LeadRect r;
                r.u0 = F(row, "U0", 0.0f); r.v0 = F(row, "V0", 0.0f); r.u1 = F(row, "U1", 0.0f); r.v1 = F(row, "V1", 0.0f);
                const bool sane = std::isfinite(r.u0) && std::isfinite(r.v0) && std::isfinite(r.u1) && std::isfinite(r.v1) &&
                                  r.u0 >= 0.0f && r.v0 >= 0.0f && r.u1 <= 1.0f && r.v1 <= 1.0f && r.u0 < r.u1 && r.v0 < r.v1;
                if (!m || !sane) { ++unknown; continue; }
                LeadRule& rule = g_lead[m - 1];
                textures += rule.rects.empty() ? 1 : 0;
                rule.rects.push_back(r);
                const float dark = F(row, "Dark", kLeadDark), line = F(row, "Line", kLeadLine);
                rule.dark = std::isfinite(dark) && dark >= 0.0f && dark < 1.0f ? dark : kLeadDark;
                rule.line = std::isfinite(line) && line > 0.0f ? line : kLeadLine;
                ++rects;
            }
            cdbc->Release(t);
            char line[160];
            std::snprintf(line, sizeof(line), "; GlassLead.cdbc: %u rectangles over %u textures%s", rects, textures,
                          unknown ? " (some rows left out: no glass texture, or out of 0 .. 1)" : "");
            status += line;
        }
        else if (!g_materials.empty()) status += std::string("; GlassLead.cdbc: ") + (err[0] ? err : "not found") + " (the see-through redraw keeps the glass mask)";

        // The see-through windows' buildings (their WMOs' paths, from outside and from inside).
        g_seeThrough.clear();
        g_seeThroughWhole.clear();
        g_seeThroughGroup.clear();
        g_seeThroughWindow.clear();
        err[0] = 0;
        if (void* t = cdbcload::LoadAppendOnly(cdbc, kSeeThroughDef, err, sizeof(err)))
        {
            unsigned unnamed = 0;
            const bool groups = cdbcload::Has(cdbc, t, "Group");   // (a file before 2026-10-23: every row the whole WMO's)
            const bool windows = cdbcload::Has(cdbc, t, "Window");   // (before 2026-10-24: no window rows)
            for (uint32_t i = 0, n = cdbc->RowCount(t); i < n; ++i)
            {
                const void* row = cdbc->RowAt(t, i);
                if (!row) continue;
                const uint32_t path = PathHash(cdbc->GetString(t, row, "Wmo"));
                const uint32_t flags = cdbc->Value(t, row, "Flags", 0);
                const uint32_t group = groups ? cdbc->Value(t, row, "Group", 0) : 0xFFFFFFFFu;
                const uint32_t window = windows ? cdbc->Value(t, row, "Window", 0) : 0xFFFFFFFFu;
                if (!path) { ++unnamed; continue; }
                // (a window's row: a mask over its room's, kept with Flags 0, its never opening; it names no building)
                if (window != 0xFFFFFFFFu) { g_seeThroughWindow[(static_cast<uint64_t>(path) << 32) | window] = flags; continue; }
                if (!flags) continue;
                g_seeThrough[path] |= flags;
                if (group == 0xFFFFFFFFu) g_seeThroughWhole[path] |= flags;
                else g_seeThroughGroup[(static_cast<uint64_t>(path) << 32) | group] |= flags;
            }
            cdbc->Release(t);
            char line[160];
            std::snprintf(line, sizeof(line), "; SeeThroughBuildings.cdbc: %u buildings, %u window rows%s", static_cast<unsigned>(g_seeThrough.size()),
                          static_cast<unsigned>(g_seeThroughWindow.size()), unnamed ? " (some rows left out: no WMO)" : "");
            status += line;
        }
        else status += std::string("; SeeThroughBuildings.cdbc: ") + (err[0] ? err : "not found") + " (no see-through windows)";

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
                p.flags = cdbcload::Has(cdbc, t, "Flags") ? cdbc->Value(t, row, "Flags", 0) : 0;
                p.material = cdbcload::Has(cdbc, t, "Material") ? cdbc->Value(t, row, "Material", 0) : 0;
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
            g_backlitPanes = 0;
            for (const Pane& pp : panes) g_backlitPanes += (pp.flags & kPaneBacklit) ? 1 : 0;
            char line[128];
            std::snprintf(line, sizeof(line), "; GlassPanes.cdbc: %u panes (%u titan panes lit from behind)", static_cast<unsigned>(panes.size()), g_backlitPanes);
            status += line;
        }
        else { status += std::string("; GlassPanes.cdbc: ") + (err[0] ? err : "not found") + " (no floor light, no coloured shafts)"; g_loadProblem = true; }

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
                s.cls = cdbcload::Uint(cdbc, t, row, "Class", kClassHouse);
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
            bool seen[kClassCount + 1] = {};
            unsigned classes = 0;
            for (const RoomStyle& s : g_styles) { bool& b = seen[s.cls < kClassCount ? s.cls : kClassCount]; classes += b ? 0 : 1; b = true; }
            char line[96];
            std::snprintf(line, sizeof(line), "; RoomStyles.cdbc: %u styles (%u classes)", static_cast<unsigned>(g_styles.size()), classes);
            status += line;
        }
        else { status += std::string("; RoomStyles.cdbc: ") + (err[0] ? err : "not found") + " (every room the same)"; g_loadProblem = true; }
        std::vector<Building> buildings;
        bool buildingsRead = false;
        unsigned wmoDefaults = 0;
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
                b.group = cdbcload::Uint(cdbc, t, row, "Group", kWholePlacement);
                b.style = cdbc->Value(t, row, "Style", 0);
                b.seed = cdbc->Value(t, row, "Seed", 0);
                b.flags = cdbc->Value(t, row, "Flags", 0);
                b.area = cdbc->Value(t, row, "AreaID", 0);
                b.id = cdbc->Value(t, row, "ID", 0);
                b.path = PathHash(cdbc->GetString(t, row, "Wmo"));
                wmoDefaults += b.uniqueId ? 0 : 1;
                buildings.push_back(b);
            }
            cdbc->Release(t);
            buildingsRead = true;
        }
        else if (!g_styles.empty()) { status += std::string("; GlassBuildings.cdbc: ") + (err[0] ? err : "not found") + " (every building the default house)"; g_loadProblem = true; }
        g_buildings.Build(std::move(buildings));
        g_building = nullptr;
        if (buildingsRead)
        {
            // The rows as the index uses them: placements (found where an instance stands), a city's groups under them,
            // a WMO's default (instances not placed in the table). No placements from rows that have unique IDs: the
            // rooms fall back to their WMO's default everywhere.
            char line[160];
            std::snprintf(line, sizeof(line), "; GlassBuildings.cdbc: %u rows: %u placements, %u city groups, %u WMO defaults",
                          static_cast<unsigned>(g_buildings.Size()), static_cast<unsigned>(g_buildings.Placements()),
                          static_cast<unsigned>(g_buildings.Groups()), wmoDefaults);
            status += line;
            if (g_buildings.Placements() == 0 && g_buildings.Size() > wmoDefaults) { status += " (none placed: every building by its WMO)"; g_loadProblem = true; }
        }

        // One room per window: the windows (their WMO's space), the lookup table the shader reads.
        std::vector<Window> windows;
        unsigned windowRows = 0, noWmo = 0, otherKind = 0;
        g_stainedWindows = g_frostedWindows = 0;
        bool windowsRead = false;
        err[0] = 0;
        if (void* t = cdbcload::LoadAppendOnly(cdbc, kWindowDef, err, sizeof(err)))
        {
            auto F = [&](const void* row, const char* f, float missing) { return cdbcload::Float(cdbc, t, row, f, missing); };
            windowsRead = true;
            windowRows = cdbc->RowCount(t);
            const bool haveRoom = cdbcload::Has(cdbc, t, "Room");
            for (uint32_t i = 0, n = windowRows; i < n; ++i)
            {
                const void* row = cdbc->RowAt(t, i);
                if (!row) continue;
                Window w;
                w.path = PathHash(cdbc->GetString(t, row, "Wmo"));
                w.group = cdbc->Value(t, row, "Group", 0);
                w.ordinal = cdbc->Value(t, row, "Ordinal", 0);
                w.kind = cdbcload::Uint(cdbc, t, row, "Kind", Kind::kClear);
                w.flags = cdbc->Value(t, row, "Flags", 0);
                const char* c[3] = { "CenterX", "CenterY", "CenterZ" };
                const char* nn[3] = { "NormalX", "NormalY", "NormalZ" };
                for (int k = 0; k < 3; ++k) { w.center[k] = F(row, c[k], 0.0f); w.normal[k] = F(row, nn[k], 0.0f); }
                w.halfWidth = F(row, "HalfWidth", 0.0f);
                w.halfHeight = F(row, "HalfHeight", 0.0f);
                w.thickness = F(row, "Thickness", 0.0f);
                w.room = haveRoom ? static_cast<int>(cdbc->Value(t, row, "Room", 0)) : -1;
                const bool kind = w.kind == Kind::kClear || w.kind == kLeaded || w.kind == kFrosted || w.kind == kStained;
                noWmo += w.path ? 0 : 1;
                otherKind += w.path && !kind ? 1 : 0;
                if (w.path && kind)
                {
                    g_stainedWindows += w.kind == kStained ? 1 : 0;
                    g_frostedWindows += w.kind == kFrosted ? 1 : 0;
                    windows.push_back(w);
                }
            }
            cdbc->Release(t);
        }
        const bool haveWindows = !windows.empty();
        g_windows.Build(std::move(windows), g_settings.wholeRooms != 0);
        g_tableDirty = true;
        {
            char line[400];
            if (haveWindows)
                std::snprintf(line, sizeof(line), "GlassWindows.cdbc: %u windows in %u WMO groups (a table of %d x %d; %u frosted, %u stained in grids of their own)",
                              static_cast<unsigned>(g_windows.Windows()), static_cast<unsigned>(g_windows.Grids()), rooms::kWidth, g_windows.Height(),
                              g_frostedWindows, g_stainedWindows);
            else if (windowsRead)
                std::snprintf(line, sizeof(line), "GlassWindows.cdbc: %u rows, no window kept (no rooms behind the glass)", windowRows);
            else
                std::snprintf(line, sizeof(line), "GlassWindows.cdbc: %s (no rooms behind the glass)", err[0] ? err : "not found");
            std::string s = line;
            // Rows left out: no WMO path (a WMO's windows are found by it) or no kind of glass.
            if (noWmo || otherKind)
            {
                std::snprintf(line, sizeof(line), "; %u rows left out: %u without a WMO, %u not clear, leaded, frosted or stained glass", noWmo + otherKind, noWmo, otherKind);
                s += line;
            }
            g_loadProblem = g_loadProblem || !haveWindows;
            g_windowStatus = s;
            status += "; " + s;
        }
        g_status = status;
        g_panes.Build(std::move(panes));
        g_near.clear();
        g_floorList.clear();
        g_floorPanes = 0;
        // (the faders hold the panes' addresses)
        g_floorFader.Clear(); g_shaftFader.Clear(); g_floorKept.clear(); g_shaftKept.clear();
    }

    void LogLoad(const WXL_Api* api)
    {
        if (!api || !api->Log) return;
        api->Log(g_loadProblem ? WXL_LOG_WARN : WXL_LOG_INFO, "wxl-seyris-living-azeroth", "glass: tables loaded: %s", g_status.c_str());
    }

    bool LoadProblem() { return g_loadProblem; }

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

    uint32_t SeeThroughFlags(uint32_t pathHash)
    {
        const auto it = pathHash ? g_seeThrough.find(pathHash) : g_seeThrough.end();
        return it == g_seeThrough.end() ? 0u : it->second;
    }

    size_t SeeThroughBuildings() { return g_seeThrough.size(); }

    uint32_t SeeThroughGroupFlags(uint32_t pathHash, uint32_t group)
    {
        if (!pathHash) return 0;
        const auto whole = g_seeThroughWhole.find(pathHash);
        const auto one = g_seeThroughGroup.find((static_cast<uint64_t>(pathHash) << 32) | group);
        return (whole == g_seeThroughWhole.end() ? 0u : whole->second) | (one == g_seeThroughGroup.end() ? 0u : one->second);
    }

    size_t SeeThroughGroupRows() { return g_seeThroughGroup.size(); }

    uint32_t SeeThroughWindowMask(uint32_t pathHash, uint32_t ordinal)
    {
        if (!pathHash || g_seeThroughWindow.empty()) return ~0u;
        const auto it = g_seeThroughWindow.find((static_cast<uint64_t>(pathHash) << 32) | ordinal);
        return it == g_seeThroughWindow.end() ? ~0u : it->second;
    }

    size_t SeeThroughWindowRows() { return g_seeThroughWindow.size(); }

    const LeadRule* LeadOf(uint32_t indexPlusOne)
    {
        return indexPlusOne && indexPlusOne <= g_lead.size() && !g_lead[indexPlusOne - 1].rects.empty() ? &g_lead[indexPlusOne - 1] : nullptr;
    }

    const void* TableTexture() { return g_tableTex; }

    const char* TextureOf(uint32_t indexPlusOne)
    {
        return indexPlusOne && indexPlusOne <= g_texturePaths.size() ? g_texturePaths[indexPlusOne - 1].c_str() : nullptr;
    }

    const Material* MaterialById(uint32_t id)
    {
        auto it = g_materialById.find(id);
        return it == g_materialById.end() ? nullptr : MaterialAt(it->second);
    }

    void Update()
    {
        ++g_frame;
        g_panesUp = false;
        // WholeRooms changed (the panel, a reload): the table's keys again, and everything keyed by them (the window lights)
        if ((g_settings.wholeRooms != 0) != g_windows.WholeRooms() && g_windows.Windows())
        {
            std::vector<Window> all = g_windows.All();
            g_windows.Build(std::move(all), g_settings.wholeRooms != 0);
            g_tableDirty = true;
            ++g_generation;
        }
        const world::Snapshot& snap = world::Current();
        g_inWorld = snap.inWorld && snap.mapId >= 0;
        // The rooms' clock: the hour and the night as the rooms (and the window lights with them) show them, steady
        // seconds (a loading screen's gap snaps it).
        g_hour = HourNow();
        g_night = Clamp01(windows::Night());
        g_lastFrost = g_frostBatches; g_lastIronforge = g_frostIronforge; g_lastUnderground = g_underground;
        g_frostBatches = g_frostIronforge = g_underground = 0;
        g_lastBacklitBatches = g_backlitBatches; g_backlitBatches = 0;
        g_lastBacklitFloor = g_backlitFloor; g_backlitFloor = 0;
        g_lastRoomBatches = g_roomBatches; g_lastNoFrame = g_noFrame; g_lastNoGrid = g_noGrid;
        g_roomBatches = g_noFrame = g_noGrid = 0;
        g_lastFrostRooms = g_frostRooms; g_lastStainRooms = g_stainRooms;
        g_frostRooms = g_stainRooms = 0;
        if (g_settings.enabled && g_patched > 0) RoomTexturesUpdate();
        if (g_inWorld)
        {
            const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            g_clock.Advance(now, g_hour, g_night);
            g_hearth.Advance(now, g_settings.flickerSpeed);
            g_pulseSeconds = now;
        }
        else { g_clock.Reset(); g_hearth.Reset(); }
        g_backlitNear = 0;
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
            for (int k = 0; k < 3; ++k) in.ambient[k] = ambient[k] + diffuse[k];   // (at night the moon's and the night's: the dark rooms' hue)
        }
        g_light = Evaluate(in);
        for (int k = 0; k < 3; ++k) { g_sun[k] = in.sun[k]; g_sunColour[k] = in.sunColour[k]; g_sky[k] = in.sky[k]; }
        g_indoors = Clamp01(ReadFloat(kInteriorBlend));

        // The panes near the camera that light the room behind them: the sun's through them, or a titan pane's light
        // behind it at any hour (LightOfPane). The floor light's and the shafts' (GlassShafts picks those whose room holds
        // the camera). Their rows about the camera are made at the frame's first batch (RenderView), with the camera the
        // shaders get.
        g_near.clear();
        g_floorList.clear();
        g_floorPanes = 0;
        g_viewFrame = 0;
        const float floorDt = SecondsSince(g_floorStamp);
        const bool backlitNear = g_settings.titanBacklit && g_backlitPanes > 0;
        if ((g_light.sunGain <= 0.01f && !backlitNear) || g_panes.Size() == 0)
        {
            // (no light through any pane: nothing to fade, the next fade in from nothing)
            g_floorFader.Clear(); g_shaftFader.Clear(); g_floorKept.clear(); g_shaftKept.clear();
            return;
        }
        float cam[3];
        wxl::game::camera::GetPosition(cam);
        static std::vector<const Pane*> candidates;
        g_panes.Near(static_cast<uint32_t>(snap.mapId), cam, g_settings.range, 256, candidates);
        // (the sun's: not an underground pane, Ironforge's; not one the sun is behind)
        for (const Pane* p : candidates)
        {
            PaneLight pl;
            if (!LightOfPane(*p, MaterialById(p->material), g_light, g_sun, g_sunColour, g_pulseSeconds, g_settings, pl)) continue;
            g_backlitNear += pl.backlit ? 1 : 0;
            g_near.push_back(pl);
        }
        // The floor light: the panes whose room holds the player first (the room you stand in), then the rest, the ones that
        // light the most where the player stands (ChoosePanes; before 2026-10-24 the nearest the camera, so a third-person
        // camera swinging round the player swapped them).
        if (!g_settings.floorLight) { g_floorFader.Clear(); g_floorKept.clear(); return; }
        const float* focus = snap.playerPos;
        const bool player = std::isfinite(focus[0]) && std::isfinite(focus[1]) && std::isfinite(focus[2]) &&
                            (focus[0] != 0.0f || focus[1] != 0.0f || focus[2] != 0.0f);
        // (2026-10-24, "lots of pop-in when i move": the slots kept, fading or not, are the choice's `last`; a pane joining or
        // leaving fades over kPaneFade s: PaneFader)
        static std::vector<PaneLight> chosen;
        std::vector<uint32_t> last = g_floorKept;
        ChoosePanes(g_near, player ? focus : cam, player ? focus : cam, 0.0f, false, static_cast<size_t>(kFloorPanes), last, chosen);
        g_floorFader.Step(chosen, g_near, floorDt, static_cast<size_t>(kFloorPanes), g_floorList, g_floorKept);
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
            static std::vector<PaneLight> drawn;
            drawn.clear();
            for (const PaneLight& pl : g_floorList)
            {
                if (n >= static_cast<unsigned>(kFloorPanes)) break;
                const Pane* p = pl.pane;
                float* c = g_paneConstants + 4 * kPaneRegisters * n;
                // the rows along its light (the sun's, or the light behind a titan pane), its colour (faded: PaneFader), its
                // room's middle and half size (the soft edges: the shader's c58, kPaneSoft and kPaneFarSoft); packed from the
                // first (FloorBlock takes them two at a time)
                if (!ProjectionRows(*p, pl.dir, cam, kReach, c)) continue;
                for (int k = 0; k < 3; ++k) c[12 + k] = pl.colour[k];
                c[15] = 1.0f;
                PaneRoom(p->roomMin, p->roomMax, cam, c);
                drawn.push_back(pl);
                g_backlitFloor += pl.backlit ? 1 : 0;
                ++n;
            }
            g_floorPanes = n;
            LightFacing(drawn.data(), drawn.size(), g_sun, g_floorFacing);
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

    void BeginInstance(const float pos[3], const float axes[9], uint32_t path, uint32_t group, uint32_t fallbackSeed)
    {
        g_inInstance = true;
        g_hasAxes = IsRotation(axes);
        for (int k = 0; k < 9; ++k) g_axes[k] = g_hasAxes ? axes[k] : (k % 4 == 0 ? 1.0f : 0.0f);
        g_path = path;
        g_group = group;
        g_fallbackSeed = FallbackSeed(path, fallbackSeed);
        g_hasOrigin = pos && std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2]);
        for (int k = 0; k < 3; ++k) g_origin[k] = g_hasOrigin ? pos[k] : 0.0f;
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
        g_hasOrigin = false;
        g_hasAxes = false;
        g_building = nullptr;
    }

    bool RoomsHideGlow()
    {
        const Settings& s = g_settings;
        return s.enabled && g_patched > 0 && s.fakeInteriors && s.outsideReflection && s.litRooms;
    }

    DarkBand BandNow(const RoomStyle& style, uint32_t buildingFlags)
    {
        return RoomBand(style, buildingFlags, FrameInputs(), g_settings);
    }

    bool FrostedHideGlow()
    {
        const Settings& s = g_settings;
        return s.enabled && g_patched > 0 && s.frostedLightsOut;
    }

    bool StainedRoomsOn()
    {
        const Settings& s = g_settings;
        return s.enabled && g_patched > 0 && s.fakeInteriors && s.outsideReflection && s.stainedRooms;
    }

    bool StainedHideGlow() { return StainedRoomsOn() && RoomsHideGlow(); }

    DarkBand FrostedBandNow(const RoomStyle& style, const Building* building)
    {
        return FrostedBand(style, building, FrameInputs(), g_settings);
    }

    float HearthClockNow() { return g_hearth.Now(); }

    float HearthAmplitudeNow()
    {
        const Settings& s = g_settings;
        return s.enabled && g_patched > 0 ? FlickerAmplitude(s.hearthFlicker, g_night) : 0.0f;
    }

    float LampAmplitudeNow()
    {
        return RoomsHideGlow() ? FlickerAmplitude(g_settings.lampFlicker, g_night) : 0.0f;
    }

    const RoomClock& Clock() { return g_clock; }

    void Batch(uint32_t glassMaterial, bool rimsOn, bool selfLit, bool unlit)
    {
        auto* dev = Device();
        if (!dev) return;
        const Material* m = g_settings.enabled && g_patched > 0 ? MaterialAt(glassMaterial) : nullptr;
        if (m || g_loopFloor) RenderView();
        if (m)
        {
            float c[4 * kConstantCount], seed[4 * kSeedRegisters], look[4 * kLookRegisters], day[4 * kDayRegisters], rooms[4 * kRoomRegisters];
            int set = -1;
            GlassConstants(*m, selfLit, unlit, c, seed, day, look, rooms, set);
            UploadFrame(dev);
            dev->SetPixelShaderConstantF(kFirstConstant, c, kConstantCount);
            dev->SetPixelShaderConstantF(kSeedConstant, seed, kSeedRegisters);
            dev->SetPixelShaderConstantF(kLookConstant, look, kLookRegisters);
            dev->SetPixelShaderConstantF(kDayConstant, day, kDayRegisters);
            // (no table or atlas on the device yet: no window found, so no room and every one lit)
            if (!g_tableTex || !g_atlasTex || !g_whiteTex) rooms[23] = 0.0f;
            dev->SetPixelShaderConstantF(kRoomConstant, rooms, kRoomRegisters);
            BindRoomTextures(dev, set);
            g_floorConstsUp = false;   // c192..c201 are this batch's now
            if (!g_glassOn) { SetBool(kSwitch, true); g_glassOn = true; }
            ++g_glassBatches;
        }
        else GlassOff();
        // The floor light: the room's surfaces, not its glass, and not a floor whose rims hold the panes' registers.
        if (g_loopFloor && !m && !rimsOn)
        {
            if (!g_panesUp || g_uploadedFrame != g_frame) UploadPanes(dev);
            else UploadMorePanes(dev);
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

    bool ShaftPanes(std::vector<PaneLight>& out, size_t max)
    {
        out.clear();
        const float dt = SecondsSince(g_shaftStamp);
        if (!g_settings.enabled || !g_settings.colouredShafts || g_near.empty()) { g_shaftFader.Clear(); g_shaftKept.clear(); return false; }
        float cam[3];
        wxl::game::camera::GetPosition(cam);
        // the room the camera is in (a little slack: a doorway), its panes that light the most where the player stands
        // (ChoosePanes, as the floor light's since 2026-10-24; before, the nearest the camera), each faded in and out as it
        // joins and leaves (PaneFader, as the floor light's)
        const float* focus = world::Current().playerPos;
        const bool player = std::isfinite(focus[0]) && std::isfinite(focus[1]) && std::isfinite(focus[2]) &&
                            (focus[0] != 0.0f || focus[1] != 0.0f || focus[2] != 0.0f);
        static std::vector<PaneLight> chosen;
        std::vector<uint32_t> last = g_shaftKept;
        ChoosePanes(g_near, cam, player ? focus : cam, 2.0f, true, max, last, chosen);
        g_shaftFader.Step(chosen, g_near, dt, max, out, g_shaftKept);
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
        api->UiCheckbox("seen from outside: the sky in it, darker and cooler by day##glout", &s.outsideReflection);
        api->UiSliderFloat("outside: darker by day (clear; leaded 0.6x, stained 0.2x)##gldark", &s.outsideDarkening, 0.0f, 0.8f);
        api->UiSliderFloat("outside: cooler by day, toward the sky's hue (stained 0.25x)##glcool", &s.dayCoolness, 0.0f, 1.0f);
        api->UiSliderFloat("outside: the sky in it head on by day, F0 (clear 1.2x; to 1 at grazing)##glf0", &s.reflectionF0, 0.0f, 0.4f);
        api->UiSliderFloat("outside: reflection strength##glouts", &s.reflectionStrength, 0.0f, 3.0f);
        api->UiCheckbox("fake rooms behind clear glass##glroom", &s.fakeInteriors);
        api->UiSliderFloat("fake rooms: share by day and dusk (clear; leaded 0.85x)##glshd", &s.roomShareDay, 0.0f, 1.0f);
        api->UiSliderFloat("fake rooms: share at night##glshn", &s.roomShareNight, 0.0f, 1.0f);
        api->UiSliderFloat("fake rooms: light (1 = by day a little darker than the glass)##glrooms", &s.interiorStrength, 0.0f, 3.0f);
        api->UiCheckbox("rooms: each building its own (tint, brightness, a few dark)##glvar", &s.roomVariety);
        api->UiSliderFloat("rooms: how far buildings differ##glvars", &s.varietyStrength, 0.0f, 2.0f);
        api->UiCheckbox("rooms: lamp-lit at night behind the lit windows##gllit", &s.litRooms);
        api->UiSliderFloat("rooms: lamp-lit brightness (1 = the client's window)##gllits", &s.litRoomStrength, 0.0f, 2.0f);
        api->UiCheckbox("rooms: lights go out as the night goes on##glout2", &s.lightsOut);
        api->UiSliderFloat("rooms: share dark at night##glouts2", &s.lightsOutStrength, 0.0f, 1.5f);
        api->UiCheckbox("frosted glass: lit windows go dark through the night (each building's hours)##glfro", &s.frostedLightsOut);
        api->UiSliderFloat("frosted glass: hearth flicker (how much a lit window's glow dips at night)##glfrf", &s.hearthFlicker, 0.0f, 0.3f);
        api->UiSliderFloat("hearth flicker speed (x)##glfrs", &s.flickerSpeed, 0.0f, 3.0f);
        api->UiSliderFloat("rooms: lamp flicker behind clear and leaded glass (0 = steady lamps)##gllpf", &s.lampFlicker, 0.0f, 0.3f);
        api->UiCheckbox("Ironforge: every window lit all night (they still flicker)##glifl", &s.ironforgeAlwaysLit);
        api->UiCheckbox("titan glass (Ulduar, Halls of Stone and Lightning): lit from behind, at any hour##glttn", &s.titanBacklit);
        api->UiSliderFloat("titan glass: its glow and the light it casts (x)##glttns", &s.backlitStrength, 0.0f, 3.0f);
        api->UiSliderFloat("titan glass: General Vezax's slow pulse (x; 0 = steady)##glvzx", &s.vezaxPulse, 0.0f, 3.0f);
        api->UiCheckbox("rooms behind frosted glass: heavily blurred, the lamp a soft glow##glfrr", &s.frostedRooms);
        api->UiCheckbox("rooms behind stained glass: faint, tinted by the glass##glstr", &s.stainedRooms);
        api->UiSliderFloat("rooms: a dark room's light at night (share of a lit one: moon and night)##gldrl", &s.darkRoomLight, 0.0f, 0.4f);
        api->UiCheckbox("rooms: furniture (tables, beds, shelves, hearths...) at its depth##gldet", &s.roomDetail);
        api->UiCheckbox("rooms: floors, walls and ceilings in the client's interior textures##gltex", &s.roomTextures);
        api->UiCheckbox("rooms: curtains half drawn in some##gllay", &s.roomLayouts);
        api->UiSliderFloat("rooms: the furniture's and curtains' detail##gllays", &s.layoutStrength, 0.0f, 2.0f);
        api->UiCheckbox("coloured shafts in the room's air##glsh", &s.colouredShafts);
        api->UiSliderFloat("coloured shafts: strength##glshs", &s.shaftStrength, 0.0f, 3.0f);
        api->UiCheckbox("light through the panes on the floor##glfl", &s.floorLight);
        api->UiSliderFloat("floor light: strength##glfls", &s.floorStrength, 0.0f, 3.0f);
        api->UiSliderFloat("panes: range from the camera (yd)##glr", &s.range, 20.0f, 150.0f);
        ui::Text(api, g_status.c_str());
        ui::Text(api, StatusLine());
        ui::Text(api, glassshafts::StatusLine());
        char line[256];
        std::snprintf(line, sizeof(line), "light: sun through %.2f, sky %.2f, day %.2f, dim %.2f; camera indoors %.2f",
                      g_light.sunGain, g_light.skyGain, g_light.day, g_light.dim, g_indoors);
        ui::Text(api, line);
        ui::Text(api, RoomLine());
        ui::Text(api, WindowLine());
        ui::Text(api, FrostLine());
        ui::Text(api, BacklitLine());
    }

    const char* BacklitLine()
    {
        static std::string line;
        // Vezax's pulse now (the first backlit material whose pulse VezaxPulse scales), as a share of its glow
        float pulse = -1.0f;
        for (const Material& m : g_materials)
            if ((m.flags & kBacklitPulse) && m.backlit > 0.0f)
            {
                float glow[3];
                BacklitGlow(m, g_pulseSeconds, 1.0f, PulseScale(m, g_settings), glow);
                pulse = 100.0f * (Lum(glow) / m.backlit - 1.0f);
                break;
            }
        char buf[400];
        std::snprintf(buf, sizeof(buf), "titan glass lit from behind: %s; %u backlit panes loaded, %u near the camera; last frame %u lighting the floor, "
                      "%u backlit glass batches drawn; Vezax's pulse now %.1f%% over its glow (-1: no such glass loaded)",
                      g_settings.titanBacklit ? "on" : "off (the sun's, as other glass)", g_backlitPanes, g_backlitNear, g_lastBacklitFloor,
                      g_lastBacklitBatches, pulse);
        line = buf;
        return line.c_str();
    }

    const char* FrostLine()
    {
        // Ironforge's parts (their styles' hours, RoomStyles.cdbc): the share of their lit windows dark now, at rest.
        auto dark = [](uint32_t cls) {
            for (const RoomStyle& st : g_styles)
                if (st.cls == cls)
                    return 100.0f * DarkShare(st, 0, FrostedToggles(g_settings), g_night, g_hour);
            return -1.0f;
        };
        char line[480];
        std::snprintf(line, sizeof(line), "frosted glass: lights out %s, hearth flicker %.0f%% now (x%.1f); lit frosted batches drawn %u (Ironforge's %u), "
                      "underground batches %u; dark now in Ironforge (-1: no such style loaded): the forge %.0f%%, the Commons %.0f%%, the wards' homes "
                      "%.0f%%, the Forlorn Cavern %.0f%%%s",
                      g_settings.frostedLightsOut ? "on" : "off", 100.0f * HearthAmplitudeNow(), g_settings.flickerSpeed, g_lastFrost, g_lastIronforge,
                      g_lastUnderground, dark(kClassForge), dark(kClassCommons), dark(kClassQuarter), dark(kClassCavern),
                      g_settings.ironforgeAlwaysLit ? " (IronforgeAlwaysLit: all lit)" : "");
        g_frostLine = line;
        return g_frostLine.c_str();
    }

    const char* WindowLine()
    {
        static std::string line;
        // A set loads the first time a batch drawn wants it (its building's palette and class), one a frame: those
        // waiting are wanted and not loaded yet, the rest no batch has wanted (no building of theirs drawn).
        int loaded = 0, failed = 0, waiting = 0;
        for (const SetTextures& st : g_sets) { loaded += st.state == 2 ? 1 : 0; failed += st.state == 3 ? 1 : 0; waiting += st.state == 1 ? 1 : 0; }
        char buf[1024];
        std::snprintf(buf, sizeof(buf), "%s; on the device: table %s, atlas %s; texture sets loaded %d of %d (%d waiting, %d failed%s%s); last frame %u glass batches "
                      "with rooms (%u behind frosted glass, %u behind stained), %u without a placement frame, %u whose group has no windows",
                      g_windowStatus.c_str(), g_tableTex ? "yes" : "no", g_atlasTex ? "yes" : "no", loaded, kTextureSets, waiting, failed,
                      g_textureError.empty() ? "" : ": ", g_textureError.c_str(), g_lastRoomBatches, g_lastFrostRooms, g_lastStainRooms, g_lastNoFrame,
                      g_lastNoGrid);
        line = buf;
        return line.c_str();
    }

    const char* RoomLine()
    {
        char line[400];
        const int hh = static_cast<int>(g_hour) % 24, mm = static_cast<int>((g_hour - std::floor(g_hour)) * 60.0f);
        const float ch = g_clock.Hour();
        const int chh = static_cast<int>(ch) % 24, cmm = static_cast<int>((ch - std::floor(ch)) * 60.0f);
        std::snprintf(line, sizeof(line), "rooms: %u styles, %zu placements (%zu rows); instances drawn with glass %u: placed %u, by WMO %u, default %u; "
                      "%02d:%02d night %.2f, dark at night: house %.0f%%, inn %.0f%%; the rooms' clock %02d:%02d night %.2f%s",
                      static_cast<unsigned>(g_styles.size()), g_buildings.Placements(), g_buildings.Size(), g_instances, g_matched, g_byWmo,
                      g_defaulted, hh, mm, g_night, 100.0f * LightsOutShare(g_hour, 0.0f, 1.0f), 100.0f * LightsOutShare(g_hour, 2.5f, 0.55f),
                      chh, cmm, g_clock.Night(), g_clock.Fading() ? " (the time jumped: the rooms cross-fade to it)" : "");
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
