#include "Windows.hpp"

#include "../env/CdbcLoad.hpp"
#include "../env/Climate.hpp"
#include "../env/Lights.hpp"
#include "../wxl_seyris/CdbcApi.hpp"

#include "wxl/PluginApi.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

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

        Settings    g_settings;
        Stats       g_stats;
        Index       g_index;
        std::string g_status = "not loaded yet";
        uint32_t    g_generation = 1;

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
        g_index.Build(std::move(rows));
    }

    void Near(uint32_t map, const float center[3], float range, bool glowOnly, size_t max, std::vector<const Window*>& out)
    {
        g_index.Near(map, center, range, glowOnly, max, out);
    }

    void Update()
    {
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
        int test = s.nightTest >= 0.0f ? 1 : 0;
        if (api->UiCheckbox("test: set the night factor here (instead of the time of day)##wnt", &test)) s.nightTest = test ? 1.0f : -1.0f;
        if (s.nightTest >= 0.0f) api->UiSliderFloat("test: night factor (0 day .. 1 night)##wntv", &s.nightTest, 0.0f, 1.0f);

        char line[384];
        api->UiText(g_status.c_str());
        std::snprintf(line, sizeof(line), "night factor %.2f, from %s (the client's windows: 1 at 06:00, 0 from 07:00 to 20:30, 1 from 21:30)%s%s",
                      g_night, g_nightSource, g_nightGuards == 2 ? "; guard: " : "", g_nightGuards == 2 ? g_nightWhy : "");
        api->UiText(line);
        std::snprintf(line, sizeof(line), "the client's Full Screen Glow (ffxGlow): %s%s%s",
                      g_clientGlow == 1 ? "on" : (g_clientGlow == 0 ? "off" : "unknown"),
                      g_glowGuards == 2 ? " -- guard: " : "", g_glowGuards == 2 ? g_glowWhy : "");
        api->UiText(line);
        std::snprintf(line, sizeof(line), "window lights: %u near, %u drawn, %u kept out of full light cells", g_stats.inRange, g_stats.lights, g_stats.leftOutOfCells);
        api->UiText(line);
        std::snprintf(line, sizeof(line), "window glow: %u window(s) in range, %u drawn, %.2f ms CPU%s%s", g_stats.glowWindows, g_stats.glowDrawn, g_stats.glowMs,
                      g_stats.glowInactive ? " -- not drawing: " : "", g_stats.glowInactive ? g_stats.glowInactive : "");
        api->UiText(line);
    }
}
