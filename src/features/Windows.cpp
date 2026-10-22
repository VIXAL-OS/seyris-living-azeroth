#include "Windows.hpp"

#include "../env/CdbcLoad.hpp"
#include "../env/Climate.hpp"
#include "../env/Lights.hpp"
#include "../wxl_seyris/CdbcApi.hpp"
#include "Glass.hpp"

#include "wxl/PluginApi.h"
#include "../debug/UiText.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace wxl_livingazeroth::windows
{
    namespace
    {
        constexpr const char* kFile = "DBFilesClient\\WindowLights.cdbc";
        // Column layout (append-only, docs/cdbc-tables.md; tools/gen_window_lights.py WINDOW_LIGHTS).
        constexpr WXL_SeyrisCdbcField kFields[] = {
            {"ID", 0, WXL_CDBC_FIELD_VALUE},         {"MapID", 1, WXL_CDBC_FIELD_VALUE},
            {"AreaID", 2, WXL_CDBC_FIELD_VALUE},     {"PosX", 3, WXL_CDBC_FIELD_VALUE},
            {"PosY", 4, WXL_CDBC_FIELD_VALUE},       {"PosZ", 5, WXL_CDBC_FIELD_VALUE},
            {"FacingX", 6, WXL_CDBC_FIELD_VALUE},    {"FacingY", 7, WXL_CDBC_FIELD_VALUE},
            {"FacingZ", 8, WXL_CDBC_FIELD_VALUE},    {"Back", 9, WXL_CDBC_FIELD_VALUE},
            {"HalfWidth", 10, WXL_CDBC_FIELD_VALUE}, {"HalfHeight", 11, WXL_CDBC_FIELD_VALUE},
            {"Radius", 12, WXL_CDBC_FIELD_VALUE},    {"LightID", 13, WXL_CDBC_FIELD_VALUE},
            {"Strength", 14, WXL_CDBC_FIELD_VALUE},  {"Flags", 15, WXL_CDBC_FIELD_VALUE},
            {"Comment", 16, WXL_CDBC_FIELD_STRING},
        };
        constexpr WXL_SeyrisCdbcDefinition kDef = { "WindowLights", kFile, kFields, 17 };

        // The rooms behind each light's glass (tools/gen_window_lights.py WINDOW_ROOMS, docs/cdbc-tables.md).
        constexpr const char* kRoomsFile = "DBFilesClient\\WindowRooms.cdbc";
        constexpr WXL_SeyrisCdbcField kRoomFields[] = {
            {"ID", 0, WXL_CDBC_FIELD_VALUE},    {"WindowID", 1, WXL_CDBC_FIELD_VALUE}, {"BuildingID", 2, WXL_CDBC_FIELD_VALUE},
            {"CellX", 3, WXL_CDBC_FIELD_VALUE}, {"CellY", 4, WXL_CDBC_FIELD_VALUE},    {"Share", 5, WXL_CDBC_FIELD_VALUE},
            {"Seed", 6, WXL_CDBC_FIELD_VALUE},  {"Flags", 7, WXL_CDBC_FIELD_VALUE},
        };
        constexpr WXL_SeyrisCdbcDefinition kRoomDef = { "WindowRooms", kRoomsFile, kRoomFields, 8 };

        Settings    g_settings;
        Stats       g_stats;
        Index       g_index;
        std::string g_status = "not loaded yet", g_roomStatus = "WindowRooms.cdbc: not loaded yet";
        uint32_t    g_generation = 1;

        // A light's room cell as the table has it (Flags: kCellFrosted, lit frosted glass with no room), and resolved
        // against features/Glass's tables (its style, building, flags and lit hash: again whenever those reload).
        struct RoomRef { uint32_t building = 0; float cell[2] = {}; float share = 0.0f; uint32_t seed = 0, flags = 0; };
        struct RoomCell
        {
            const glass::RoomStyle* style = nullptr; const glass::Building* building = nullptr;
            uint32_t flags = 0; float hash = 0.5f, share = 0.0f; bool frosted = false, stained = false;
            uint32_t cell = 0;   // the table's Flags (kCellStained, kCellUnstained, kCellWithStained: which state reads it)
        };
        // Whether a cell is read with stained glass's rooms (`stainedOn`) or without them.
        bool CellRead(uint32_t cellFlags, bool stainedOn)
        {
            return !(cellFlags & (stainedOn ? kCellUnstained : (kCellStained | kCellWithStained)));
        }
        std::vector<RoomRef>  g_refs;
        std::vector<RoomCell> g_cells;
        uint32_t g_resolvedFor = 0;        // glass::Generation() g_cells were resolved for (0: never)
        unsigned g_unresolved = 0;         // cells whose building isn't in GlassBuildings: the default house
        // This frame's shares (each window once), and the counters behind Stats' room fields.
        std::vector<float>    g_share;
        std::vector<uint32_t> g_shareFrame;
        uint32_t g_frame = 1;
        unsigned g_roomWindows = 0, g_roomDimmed = 0, g_roomDark = 0;
        double   g_roomSum = 0.0;
        unsigned g_frostWindows = 0, g_frostDimmed = 0, g_frostDark = 0;   // windows with lit frosted glass (lights out alone)
        double   g_frostSum = 0.0;

        float       g_night = 0.0f;
        const char* g_nightSource = "not read yet";
        int         g_clientGlow = -1;

        // The guards, checked once: 0 not yet, 1 hold, 2 differ (the reason in g_*Why).
        int         g_nightGuards = 0, g_glowGuards = 0;
        const char* g_nightWhy = "";
        const char* g_glowWhy = "";

        template <size_t N> int CheckUnsafe(const Guard (&guards)[N], const char*& why)
        {
            const Guard* bad = FirstMismatch(guards, [](uintptr_t a, uint32_t) { return reinterpret_cast<const void*>(a); });
            if (bad) { why = bad->what; return 2; }
            return 1;
        }
        template <size_t N> int Check(const Guard (&guards)[N], const char*& why)
        {
            __try { return CheckUnsafe(guards, why); }
            __except (EXCEPTION_EXECUTE_HANDLER) { why = "a fault reading the client's code"; return 2; }
        }

        bool ReadFloat(uintptr_t address, float& out)
        {
            __try { std::memcpy(&out, reinterpret_cast<const void*>(address), sizeof(out)); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        int ReadClientGlow()
        {
            __try
            {
                const auto* effect = *reinterpret_cast<const uint8_t* const*>(kGlowEffect);
                if (!effect) return -1;
                const auto* cvar = *reinterpret_cast<const uint8_t* const*>(effect + kGlowCVar);
                if (!cvar) return -1;
                return *reinterpret_cast<const int32_t*>(cvar + kCVarInt) != 0 ? 1 : 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
        }

        float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

        // Reads WindowRooms.cdbc into g_refs, each window's run of cells into its roomFirst / roomCount.
        void LoadRooms(const WXL_SeyrisCdbcApi* cdbc, std::vector<Window>& rows)
        {
            g_refs.clear();
            g_cells.clear();
            g_resolvedFor = 0;
            char err[256] = {};
            void* t = cdbcload::LoadAppendOnly(cdbc, kRoomDef, err, sizeof(err));
            if (!t)
            {
                g_roomStatus = std::string("WindowRooms.cdbc: ") + (err[0] ? err : "not found") + " (the window lights don't follow the rooms)";
                return;
            }
            std::unordered_map<uint32_t, std::vector<RoomRef>> byWindow;
            unsigned cells = 0;
            for (uint32_t i = 0, n = cdbc->RowCount(t); i < n; ++i)
            {
                const void* row = cdbc->RowAt(t, i);
                if (!row) continue;
                RoomRef r;
                r.building = cdbc->Value(t, row, "BuildingID", 0);
                r.cell[0] = static_cast<float>(static_cast<int32_t>(cdbc->Value(t, row, "CellX", 0)));
                r.cell[1] = static_cast<float>(static_cast<int32_t>(cdbc->Value(t, row, "CellY", 0)));
                r.share = cdbcload::Float(cdbc, t, row, "Share", 0.0f);
                r.seed = cdbcload::Has(cdbc, t, "Seed") ? cdbc->Value(t, row, "Seed", 0) : 0;
                r.flags = cdbcload::Has(cdbc, t, "Flags") ? cdbc->Value(t, row, "Flags", 0) : 0;
                if (!(r.share > 0.0f) || !std::isfinite(r.share)) continue;
                byWindow[cdbc->Value(t, row, "WindowID", 0)].push_back(r);
                ++cells;
            }
            cdbc->Release(t);
            unsigned windows = 0, frostWindows = 0, frostCells = 0, stainWindows = 0, stainCells = 0, before = 0;
            for (Window& w : rows)
            {
                auto it = byWindow.find(w.id);
                if (it == byWindow.end()) continue;
                w.roomFirst = static_cast<uint32_t>(g_refs.size());
                w.roomCount = static_cast<uint32_t>(it->second.size());
                g_refs.insert(g_refs.end(), it->second.begin(), it->second.end());
                bool frost = false, stain = false;
                for (const RoomRef& r : it->second)
                {
                    before += (r.flags & kCellUnstained) ? 1u : 0u;
                    if (r.flags & kCellUnstained) continue;   // (counted as stained glass's rooms read them)
                    frost = frost || (r.flags & kCellFrosted); frostCells += (r.flags & kCellFrosted) ? 1u : 0u;
                    stain = stain || (r.flags & kCellStained); stainCells += (r.flags & kCellStained) ? 1u : 0u;
                }
                ++windows;
                frostWindows += frost ? 1u : 0u;
                stainWindows += stain ? 1u : 0u;
            }
            char line[384];
            std::snprintf(line, sizeof(line), "WindowRooms.cdbc: %u cells behind %u of the windows' glass (%u of them lit frosted glass, at %u windows; "
                          "%u stained glass, at %u windows; %u read only without stained glass's rooms)",
                          cells, windows, frostCells, frostWindows, stainCells, stainWindows, before);
            g_roomStatus = line;
        }

        // The cells against features/Glass's tables as they are now: the building's style, flags and seed (one not in
        // GlassBuildings, or without its style, is the shader's default: the built-in house, its WMO's seed), and the
        // lit hash of each cell with that seed.
        void ResolveRooms()
        {
            const uint32_t gen = glass::Generation();
            if (g_resolvedFor == gen && g_cells.size() == g_refs.size()) return;
            g_resolvedFor = gen;
            g_cells.resize(g_refs.size());
            g_unresolved = 0;
            const glass::BuildingIndex& buildings = glass::Buildings();
            for (size_t i = 0; i < g_refs.size(); ++i)
            {
                const RoomRef& r = g_refs[i];
                const glass::Building* b = r.building ? buildings.ById(r.building) : nullptr;
                const glass::RoomStyle* style = b ? glass::StyleById(b->style) : nullptr;
                RoomCell& c = g_cells[i];
                uint32_t seed = r.seed;
                if (style) { c.style = style; c.building = b; c.flags = b->flags; seed = b->seed; }
                else { c.style = &glass::DefaultStyle(); c.building = nullptr; c.flags = 0; ++g_unresolved; }
                c.hash = glass::LitHash(r.cell[0], r.cell[1], static_cast<float>(seed & 1023u), static_cast<float>((seed >> 10) & 1023u));
                c.share = r.share;
                c.frosted = (r.flags & kCellFrosted) != 0;
                c.stained = (r.flags & kCellStained) != 0;
                c.cell = r.flags;
            }
        }
    }

    float LitShare(const Window& w)
    {
        if (!g_settings.lightsFollowRooms || !w.roomCount) return 1.0f;
        // What follows the glass now: the rooms (dark ones, a lamp's flicker), lit frosted glass (dark, a hearth's flicker),
        // stained glass's rooms (with StainedRooms; titan glass lit from behind never dims).
        const bool rooms = glass::RoomsHideGlow(), frost = glass::FrostedHideGlow();
        const bool stainedOn = glass::StainedRoomsOn(), stainedHide = glass::StainedHideGlow();
        const bool titan = glass::Config().titanBacklit != 0;
        const float lampA = glass::LampAmplitudeNow(), hearthA = glass::HearthAmplitudeNow();
        if (!rooms && !frost && lampA <= 0.0f && hearthA <= 0.0f) return 1.0f;
        const std::vector<Window>& all = g_index.All();
        if (all.empty() || &w < all.data() || &w >= all.data() + all.size()) return 1.0f;
        const size_t at = static_cast<size_t>(&w - all.data());
        if (g_share.size() != all.size()) { g_share.assign(all.size(), 1.0f); g_shareFrame.assign(all.size(), 0); }
        if (g_shareFrame[at] == g_frame) return g_share[at];
        ResolveRooms();
        static std::vector<glass::LightCell> cells, frostCells;
        cells.clear();
        frostCells.clear();
        const float clock = glass::HearthClockNow();
        const glass::DarkBand allLit = glass::Band(0.0f, 0.0f);
        bool anyRoom = false;
        for (uint32_t i = w.roomFirst; i < w.roomFirst + w.roomCount && i < g_cells.size(); ++i)
        {
            const RoomCell& c = g_cells[i];
            if (!CellRead(c.cell, stainedOn)) continue;   // the other state's cell
            glass::LightCell lc;
            lc.hash = c.hash; lc.share = c.share;
            if (c.stained)
            {
                // stained glass's faint room, as its batch gets it: a room's band and lamp, none for titan glass lit from behind
                const bool follows = stainedHide && !(titan && (c.flags & glass::kBuildingBacklit));
                lc.band = follows ? glass::BandNow(*c.style, c.flags) : allLit;
                lc.dip = follows ? glass::HearthDip(c.hash, clock, lampA) : 0.0f;
                anyRoom = anyRoom || follows;
            }
            else if (c.frosted)
            {
                // lit frosted glass, as its batch (features/Glass: FrostedLit) gets it: a dungeon's keeps the client's look
                const bool dungeon = (c.flags & glass::kBuildingDungeon) != 0;
                lc.band = frost && !dungeon ? glass::FrostedBandNow(*c.style, c.building) : allLit;
                lc.dip = dungeon ? 0.0f : glass::HearthDip(c.hash, clock, hearthA);
                glass::LightCell still = lc;
                still.dip = 0.0f;
                frostCells.push_back(still);
            }
            else
            {
                lc.band = rooms ? glass::BandNow(*c.style, c.flags) : allLit;
                lc.dip = rooms ? glass::HearthDip(c.hash, clock, lampA) : 0.0f;
                anyRoom = true;
            }
            cells.push_back(lc);
        }
        const float share = glass::LitShare(cells.data(), cells.size());
        g_share[at] = share;
        g_shareFrame[at] = g_frame;
        if (anyRoom && rooms)
        {
            ++g_roomWindows;
            g_roomDimmed += share < 0.999f && share > 0.002f ? 1u : 0u;
            g_roomDark += share <= 0.002f ? 1u : 0u;
            g_roomSum += share;
        }
        if (!frostCells.empty())
        {
            // lit, dimmed or dark by the lights out alone (the flicker dims every lit one a little)
            float s = 0.0f, lit = 0.0f;
            for (const glass::LightCell& c : frostCells) { s += c.share; lit += c.share * glass::RoomLit(c.hash, c.band); }
            const float f = s > 0.0f ? lit / s : 1.0f;
            ++g_frostWindows;
            g_frostDimmed += f < 0.999f && f > 0.002f ? 1u : 0u;
            g_frostDark += f <= 0.002f ? 1u : 0u;
            g_frostSum += f;
        }
        return share;
    }

    bool SyncedFlicker(const Window& w)
    {
        if (!g_settings.lightsFollowRooms || !w.roomCount) return false;
        const float lampA = glass::LampAmplitudeNow(), hearthA = glass::HearthAmplitudeNow();
        if (lampA <= 0.0f && hearthA <= 0.0f) return false;
        ResolveRooms();
        const bool stainedOn = glass::StainedRoomsOn(), stainedHide = glass::StainedHideGlow();
        const bool titan = glass::Config().titanBacklit != 0;
        for (uint32_t i = w.roomFirst; i < w.roomFirst + w.roomCount && i < g_cells.size(); ++i)
        {
            const RoomCell& c = g_cells[i];
            if (!CellRead(c.cell, stainedOn)) continue;
            if (c.stained ? lampA > 0.0f && stainedHide && !(titan && (c.flags & glass::kBuildingBacklit))
                          : c.frosted ? hearthA > 0.0f && !(c.flags & glass::kBuildingDungeon) : lampA > 0.0f)
                return true;
        }
        return false;
    }

    Settings& Config() { return g_settings; }
    Stats& GetStats() { return g_stats; }
    const char* Status() { return g_status.c_str(); }
    uint32_t Generation() { return g_generation; }
    float Night() { return g_night; }
    const char* NightSource() { return g_nightSource; }
    int ClientGlow() { return g_clientGlow; }

    void Load(const WXL_SeyrisCdbcApi* cdbc)
    {
        std::vector<Window> rows;
        ++g_generation;
        g_refs.clear(); g_cells.clear(); g_resolvedFor = 0;
        g_share.clear(); g_shareFrame.clear();
        if (!cdbc || !cdbc->HasFeature("cdbc-load")) { g_status = "wxl-seyris-tools (cdbc) not available"; g_index.Build({}); return; }
        char err[256] = {};
        void* t = cdbcload::LoadAppendOnly(cdbc, kDef, err, sizeof(err));
        if (!t)
        {
            g_status = std::string("WindowLights.cdbc: ") + (err[0] ? err : "not found") + " (no window lights)";
            g_index.Build({});
            return;
        }
        auto F = [&](const void* row, const char* field, float missing) { return cdbcload::Float(cdbc, t, row, field, missing); };
        auto U = [&](const void* row, const char* field, uint32_t missing) { return cdbcload::Has(cdbc, t, field) ? cdbc->Value(t, row, field, 0) : missing; };
        unsigned glowOnly = 0;
        for (uint32_t i = 0, n = cdbc->RowCount(t); i < n; ++i)
        {
            const void* row = cdbc->RowAt(t, i);
            if (!row) continue;
            Window w;
            w.id = cdbc->Value(t, row, "ID", 0);
            w.map = U(row, "MapID", 0);
            w.area = U(row, "AreaID", 0);
            w.pos[0] = F(row, "PosX", 0.0f); w.pos[1] = F(row, "PosY", 0.0f); w.pos[2] = F(row, "PosZ", 0.0f);
            w.facing[0] = F(row, "FacingX", 0.0f); w.facing[1] = F(row, "FacingY", 0.0f); w.facing[2] = F(row, "FacingZ", 0.0f);
            w.back = F(row, "Back", 0.75f);
            w.halfWidth = F(row, "HalfWidth", 1.0f); w.halfHeight = F(row, "HalfHeight", 1.0f);
            w.radius = F(row, "Radius", 0.0f);
            w.lightId = U(row, "LightID", 0);
            w.strength = F(row, "Strength", 1.0f);
            w.flags = U(row, "Flags", 0);
            const float len = std::sqrt(w.facing[0] * w.facing[0] + w.facing[1] * w.facing[1] + w.facing[2] * w.facing[2]);
            if (!(len > 0.5f)) continue; // a window facing nowhere
            for (float& f : w.facing) f /= len;
            glowOnly += (w.flags & kGlowOnly) ? 1u : 0u;
            rows.push_back(w);
        }
        cdbc->Release(t);
        char line[160];
        std::snprintf(line, sizeof(line), "WindowLights.cdbc: %u lit window(s), %u of them only glow (their light cell was full)",
                      static_cast<unsigned>(rows.size()), glowOnly);
        g_status = line;
        LoadRooms(cdbc, rows);
        g_index.Build(std::move(rows));
    }

    void Near(uint32_t map, const float center[3], float range, bool glowOnly, size_t max, std::vector<const Window*>& out)
    {
        g_index.Near(map, center, range, glowOnly, max, out);
    }

    void Update()
    {
        // Last frame's rooms for the panel, then a new frame for LitShare.
        g_stats.roomWindows = g_roomWindows; g_stats.roomDimmed = g_roomDimmed; g_stats.roomDark = g_roomDark;
        g_stats.roomMeanLit = g_roomWindows ? static_cast<float>(g_roomSum / g_roomWindows) : 1.0f;
        g_roomWindows = g_roomDimmed = g_roomDark = 0;
        g_roomSum = 0.0;
        g_stats.frostWindows = g_frostWindows; g_stats.frostDimmed = g_frostDimmed; g_stats.frostDark = g_frostDark;
        g_stats.frostMeanLit = g_frostWindows ? static_cast<float>(g_frostSum / g_frostWindows) : 1.0f;
        g_frostWindows = g_frostDimmed = g_frostDark = 0;
        g_frostSum = 0.0;
        if (++g_frame == 0) g_frame = 1;

        if (!g_nightGuards) g_nightGuards = Check(kNightGuards, g_nightWhy);
        if (!g_glowGuards) g_glowGuards = Check(kGlowGuards, g_glowWhy);

        float night = 0.0f;
        const float timeOverride = climate::TimeOverride();
        if (g_settings.nightTest >= 0.0f)
        {
            night = Clamp01(g_settings.nightTest);
            g_nightSource = "the panel's test value";
        }
        else if (timeOverride >= 0.0f)
        {
            night = NightFade(timeOverride);
            g_nightSource = "the climate panel's time of day (the client's curve)";
        }
        else if (g_nightGuards == 1 && ReadFloat(kDayNight + kWindowNight, night) && night >= 0.0f && night <= 1.0f)
            g_nightSource = "the client's own window fade";
        else if (climate::Now().valid)
        {
            night = NightFade(climate::Now().dayFraction);
            g_nightSource = g_nightGuards == 2 ? "the clock (the client's fade can't be read here: a guard differs)" : "the clock";
        }
        else
        {
            night = 0.0f;
            g_nightSource = "no clock (not in the world)";
        }
        g_night = Clamp01(night);
        g_clientGlow = g_glowGuards == 1 ? ReadClientGlow() : -1;
    }

    void DrawPanelSection(const WXL_Api* api)
    {
        if (!api->UiCollapsingHeader("Windows: light and glow from lit windows at night##windows")) return;
        Settings& s = g_settings;
        api->UiCheckbox("window lights: warm light in front of the lit windows of towns and cities##wl", &s.lights);
        api->UiSliderFloat("window lights: strength##wls", &s.lightStrength, 0.0f, 3.0f);
        api->UiSliderFloat("window lights: range from the camera (yd)##wlr", &s.lightRange, 40.0f, 256.0f);
        api->UiSliderInt("window lights: at most this many at once##wlm", &s.maxLights, 0, 256);
        api->UiSliderInt("window lights: per light-grid cell (after torches and lanterns)##wlc", &s.perCell, 0, lights::kMaxPerCellCap);
        api->UiCheckbox("window glow: a soft bloom on the lit glass##wg", &s.glow);
        api->UiSliderFloat("window glow: strength##wgs", &s.glowStrength, 0.0f, 3.0f);
        api->UiSliderFloat("window glow: range from the camera (yd)##wgr", &s.glowRange, 50.0f, 500.0f);
        static const char* const kWith[] = { "full", "half (default)", "off" };
        api->UiCombo("window glow while the client's Full Screen Glow is on##wgc", &s.withClientGlow, kWith, 3);
        api->UiCheckbox("window lights and glow follow the rooms behind the glass (a dark room lights nothing)##wlfr", &s.lightsFollowRooms);
        int test = s.nightTest >= 0.0f ? 1 : 0;
        if (api->UiCheckbox("test: set the night factor here (instead of the time of day)##wnt", &test)) s.nightTest = test ? 1.0f : -1.0f;
        if (s.nightTest >= 0.0f) api->UiSliderFloat("test: night factor (0 day .. 1 night)##wntv", &s.nightTest, 0.0f, 1.0f);

        char line[384];
        ui::Text(api, g_status.c_str());
        ui::Text(api, g_roomStatus.c_str());
        {
            const float ch = glass::Clock().Hour();
            const int hh = static_cast<int>(ch) % 24, mm = static_cast<int>((ch - std::floor(ch)) * 60.0f);
            if (!s.lightsFollowRooms) std::snprintf(line, sizeof(line), "rooms: the lights don't follow them (switched off): all at full");
            else if (!glass::RoomsHideGlow())
                std::snprintf(line, sizeof(line), "rooms: none hide the client's windows now (glass, its rooms or lamp-lit rooms off, or no shader variants): lights at full");
            else if (!g_stats.roomWindows)
                std::snprintf(line, sizeof(line), "rooms: no window with rooms behind its glass near (the rooms' clock %02d:%02d)", hh, mm);
            else
                std::snprintf(line, sizeof(line), "rooms (the rooms' clock %02d:%02d): %u window(s) near with rooms: %u lit, %u dimmed, %u dark; lit %.0f%% on the whole%s%s",
                              hh, mm, g_stats.roomWindows, g_stats.roomWindows - g_stats.roomDimmed - g_stats.roomDark, g_stats.roomDimmed,
                              g_stats.roomDark, 100.0f * g_stats.roomMeanLit, glass::Clock().Fading() ? "; the time jumped: cross-fading" : "",
                              g_unresolved ? "; some rooms not in GlassBuildings (the default house)" : "");
            ui::Text(api, line);
            if (!s.lightsFollowRooms) std::snprintf(line, sizeof(line), "frosted windows: the lights don't follow the glass (switched off)");
            else if (!glass::FrostedHideGlow() && glass::HearthAmplitudeNow() <= 0.0f)
                std::snprintf(line, sizeof(line), "frosted windows: lights out and flicker off (FrostedLightsOut, HearthFlicker; or the glass off): lights at full");
            else if (!g_stats.frostWindows) std::snprintf(line, sizeof(line), "frosted windows: none near with lit frosted glass");
            else
                std::snprintf(line, sizeof(line), "frosted windows near (lit frosted glass): %u lit, %u dimmed, %u dark; lit %.0f%% on the whole; hearth flicker %.0f%% now",
                              g_stats.frostWindows - g_stats.frostDimmed - g_stats.frostDark, g_stats.frostDimmed, g_stats.frostDark,
                              100.0f * g_stats.frostMeanLit, 100.0f * glass::HearthAmplitudeNow());
            ui::Text(api, line);
        }
        std::snprintf(line, sizeof(line), "night factor %.2f, from %s (the client's windows: 1 at 06:00, 0 from 07:00 to 20:30, 1 from 21:30)%s%s",
                      g_night, g_nightSource, g_nightGuards == 2 ? "; guard: " : "", g_nightGuards == 2 ? g_nightWhy : "");
        ui::Text(api, line);
        std::snprintf(line, sizeof(line), "the client's Full Screen Glow (ffxGlow): %s%s%s",
                      g_clientGlow == 1 ? "on" : (g_clientGlow == 0 ? "off" : "unknown"),
                      g_glowGuards == 2 ? " -- guard: " : "", g_glowGuards == 2 ? g_glowWhy : "");
        ui::Text(api, line);
        std::snprintf(line, sizeof(line), "window lights: %u near, %u drawn, %u kept out of full light cells", g_stats.inRange, g_stats.lights, g_stats.leftOutOfCells);
        ui::Text(api, line);
        std::snprintf(line, sizeof(line), "window glow: %u window(s) in range, %u drawn, %.2f ms CPU%s%s", g_stats.glowWindows, g_stats.glowDrawn, g_stats.glowMs,
                      g_stats.glowInactive ? " -- not drawing: " : "", g_stats.glowInactive ? g_stats.glowInactive : "");
        ui::Text(api, line);
    }
}
