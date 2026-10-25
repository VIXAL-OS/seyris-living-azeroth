#include "Lantern.hpp"

#include "../debug/ChatLine.hpp"
#include "../debug/Compare.hpp"
#include "../debug/KeyWatch.hpp"
#include "../debug/UiText.hpp"
#include "../env/Actors.hpp"
#include "../env/Climate.hpp"
#include "../env/Lights.hpp"
#include "../env/Storm.hpp"

#include "engine/events/Event.hpp"
#include "game/World.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace wxl_livingazeroth::lantern
{
    namespace
    {
        namespace ev = wxl::events;
        namespace gw = wxl::game::world;

        Settings       g_settings;
        State          g_state;
        Stats          g_stats;
        keys::KeyWatch g_watch;
        Light          g_light{};
        bool           g_have = false;
        std::string    g_status;

        void __cdecl OnInput(void* /*user*/, const void* args)
        {
            const auto* a = static_cast<const ev::InputArgs*>(args);
            if (a) g_watch.Message(g_settings.key, a->message, a->wparam);
        }

        // The active player's facing (radians, 0 = +x, counter-clockwise), read as the snapshot reads it.
        bool ReadFacing(float& facing)
        {
            __try
            {
                const unsigned long long guid = gw::ActivePlayerGuid();
                void* player = guid ? gw::ResolveObject(guid, gw::kTypeMaskPlayer) : nullptr;
                if (!player) return false;
                facing = gw::Facing(player);
                return std::isfinite(facing);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // The player's own unit is the nearest actor (env/Actors, refreshed round the player this frame).
        bool Mounted()
        {
            for (const actors::Actor& a : actors::Nearby())
            {
                if (a.distance > 0.5f) break;
                if (a.isPlayer && a.mounted) return true;
            }
            return false;
        }

        // The rules for one frame, then the light where it hangs.
        void Compute(float dt, const float feet[3], float facing, bool facingRead, bool mounted, float daylight, bool indoors, float storm, float around)
        {
            const float strength = Advance(g_state, g_settings, daylight, indoors, storm, around, dt);
            Stats& st = g_stats;
            st.strength = strength;
            st.darkness = g_state.darkness;
            st.around = g_state.around;
            st.daylight = daylight;
            st.lit = g_state.lit;
            st.autoWants = g_state.autoWants;
            st.held = g_state.held;
            st.indoors = indoors;
            st.mounted = mounted;
            st.facingRead = facingRead;
            if (!(strength > 0.002f))
            {
                g_have = false;
                st.why = g_state.lit ? (g_state.level < 1.0f ? "lighting" : "lit, but too light here to show") : (g_state.level > 0.0f ? "going out" : "out");
                return;
            }
            const float up = mounted ? kMountedHeight : kHipHeight;
            const float f[2] = { facingRead ? std::cos(facing) : 1.0f, facingRead ? std::sin(facing) : 0.0f };
            const float side = facingRead ? kSide : 0.0f, forward = facingRead ? kForward : 0.0f;
            // (the right-hand side of the facing: forward x up)
            g_light.pos[0] = feet[0] + f[1] * side + f[0] * forward;
            g_light.pos[1] = feet[1] - f[0] * side + f[1] * forward;
            g_light.pos[2] = feet[2] + up;
            const float k = kIntensity * strength;
            g_light.color[0] = ((kColor >> 16) & 0xFF) / 255.0f * k;
            g_light.color[1] = ((kColor >> 8) & 0xFF) / 255.0f * k;
            g_light.color[2] = (kColor & 0xFF) / 255.0f * k;
            g_light.radius = std::isfinite(g_settings.radius) ? std::clamp(g_settings.radius, 2.0f, 24.0f) : 8.0f;
            g_light.indoors = indoors;
            std::copy(g_light.pos, g_light.pos + 3, st.pos);
            g_have = true;
            st.why = "";
        }

        void OnPress()
        {
            ++g_stats.presses;
            const std::string key = keys::KeyName(g_settings.key);
            if (compare::EffectsOff())
            {
                chat::Say("all effects are off for the comparison, the lantern with them (" + keys::KeyName(compare::Config().key) + " turns them back on).");
                return;
            }
            if (!g_settings.enabled) { chat::Say("the lantern is switched off (the lights panel's Lantern section)."); return; }
            const bool lit = Press(g_state, g_settings);
            if (lit)
            {
                if (!lights::Config().enabled) chat::Say("lantern lit, but our point lights are off (the lights panel): it gives no light.");
                else if (g_state.darkness < 0.05f) chat::Say("lantern lit, but it's too light here to see it: it shows in the dark (" + key + " puts it out).");
                else chat::Say("lantern lit (" + key + " puts it out).");
            }
            else if (g_settings.automatic) chat::Say("lantern out: it lights itself again at the next dark place or nightfall (" + key + " lights it now).");
            else chat::Say("lantern out (" + key + " lights it).");
        }
    }

    Settings& Config() { return g_settings; }
    const Stats& GetStats() { return g_stats; }

    void Install(const WXL_Api* api)
    {
        if (api) api->Subscribe(static_cast<uint32_t>(ev::Event::OnInput), &OnInput, nullptr);
    }

    void Update(float dt, const world::Snapshot& snap)
    {
        g_watch.Poll(g_settings.key);
        const bool pressed = g_watch.Take();
        g_have = false;
        if (!snap.inWorld) { g_stats.why = "not in the world"; g_stats.strength = 0.0f; return; }
        if (pressed) OnPress();
        if (!g_settings.enabled || !lights::Config().enabled)
        {
            g_state.level = 0.0f;   // lit again: it fades in
            g_stats.strength = 0.0f;
            g_stats.why = !g_settings.enabled ? "switched off" : "our point lights are off (it needs them)";
            return;
        }
        float facing = 0.0f;
        const bool facingRead = ReadFacing(facing);
        Compute(dt, snap.playerPos, facing, facingRead, Mounted(), climate::SunNow().daylight, !snap.outdoors, storm::GetStatus().darkness,
                lights::LastAround());
    }

    bool Current(Light& out)
    {
        if (!g_have) return false;
        out = g_light;
        return true;
    }

    const char* StatusLine()
    {
        const Stats& s = g_stats;
        const char* state = s.held == 0 ? "out (put out by the key)" : s.held == 1 ? "lit (by the key)" : s.lit ? "lit (by itself: dark here)" : "out";
        char line[700];
        std::snprintf(line, sizeof(line),
                      "lantern: %s; %s; shining %.2f (the place's darkness %.2f: %s, daylight %.2f; the light round you %.2f), %s%s; key %s, pressed %u time(s)%s",
                      g_settings.enabled ? "on" : "switched off", state, s.strength, s.darkness, s.indoors ? "indoors" : "outdoors", s.daylight, s.around,
                      s.why[0] ? s.why : "shining", s.mounted ? ", mounted" : "", keys::KeyName(g_settings.key).c_str(), s.presses,
                      g_settings.automatic ? "; lights itself in the dark" : "; only by the key");
        g_status = line;
        return g_status.c_str();
    }

    void SnapshotLines(std::vector<std::string>& out)
    {
        const Settings& c = g_settings;
        char line[400];
        std::snprintf(line, sizeof(line), "lantern settings: Enabled %d, Key %d (%s), Automatic %d, Strength %.2f, Radius %.1f yd", c.enabled, c.key,
                      keys::KeyName(c.key).c_str(), c.automatic, c.strength, c.radius);
        out.emplace_back(line);
        out.emplace_back(StatusLine());
        if (g_have)
        {
            std::snprintf(line, sizeof(line), "  lantern light at %.2f %.2f %.2f (%s), colour %.2f %.2f %.2f, radius %.1f yd%s", g_light.pos[0], g_light.pos[1], g_light.pos[2],
                          g_stats.facingRead ? "at the hip" : "over the feet: facing not read", g_light.color[0], g_light.color[1], g_light.color[2], g_light.radius,
                          g_light.indoors ? ", indoors (off the terrain and the cover)" : "");
            out.emplace_back(line);
        }
    }

    void DrawPanelSection(const WXL_Api* api)
    {
        char header[160];
        std::snprintf(header, sizeof(header), "Lantern: a light at your belt in the dark (%s)##lantern", keys::KeyName(g_settings.key).c_str());
        if (!api->UiCollapsingHeader(header)) return;
        Settings& s = g_settings;
        api->UiCheckbox("lantern: a warm light at your hip, following you##ln", &s.enabled);
        api->UiCheckbox("lantern: lights itself at night and in dark places (caves, indoors without light)##lna", &s.automatic);
        api->UiSliderFloat("lantern: strength##lns", &s.strength, 0.0f, 2.0f);
        api->UiSliderFloat("lantern: reach (yd)##lnr", &s.radius, 2.0f, 16.0f);
        if (api->UiButton(g_state.lit ? "Put the lantern out##lnkey" : "Light the lantern##lnkey")) OnPress();
        ui::Text(api, StatusLine());
        if (!lights::Config().enabled) ui::Text(api, "(our point lights are switched off above: the lantern needs them)");
    }

    float StepForTests(float dt, const float feet[3], float facing, float daylight, bool indoors, float stormDarkness, float around)
    {
        g_have = false;
        if (!g_settings.enabled) return 0.0f;
        Compute(dt, feet, facing, true, false, daylight, indoors, stormDarkness, around);
        return g_stats.strength;
    }

    void PressForTests() { OnPress(); }

    void ResetForTests()
    {
        g_state = State{};
        g_stats = Stats{};
        g_have = false;
        g_watch = keys::KeyWatch{};
    }
}
