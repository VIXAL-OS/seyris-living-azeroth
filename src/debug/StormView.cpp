#include "StormView.hpp"
#include "Pages.hpp"

#include "../env/ClientWeather.hpp"
#include "../env/Climate.hpp"
#include "../env/Storm.hpp"
#include "../env/StormModel.hpp"
#include "../env/ThunderDsp.hpp"

#include <cmath>
#include <cstdio>

namespace wxl_livingazeroth::debug
{
    namespace
    {
        constexpr const char* kPanelTitle = "wxl-seyris-living-azeroth: storms";
        const WXL_Api* g_api = nullptr;
        float g_grade = 0.8f;
        float g_distance = 400.0f;
        int   g_kit = 0;            // the recording to audition (storm::Kit)
        int   g_kitFilters = 1;     // ... through its distance filters (whatever ThunderFilters says)

        // A chain's settings in a few words.
        void Describe(const thunderdsp::Params& p, char* out, size_t size)
        {
            if (thunderdsp::Plain(p)) { std::snprintf(out, size, "none (crisp)"); return; }
            char lowpass[40] = "no low-pass", echo[64] = "no echo";
            if (p.lowpass) std::snprintf(lowpass, sizeof(lowpass), "low-pass %.1f kHz", p.cutoff / 1000.0f);
            if (p.echo) std::snprintf(echo, sizeof(echo), "echo %.0f ms x %.2f, wet %.2f (rings %.1f s)", p.delayMs, p.decay, p.wet, thunderdsp::TailSeconds(p));
            std::snprintf(out, size, "%s, pitch %.3f, %s", lowpass, p.pitch, echo);
        }

        void __cdecl Panel(void* /*user*/)
        {
            storm::Settings& s = storm::Config();
            const storm::Status& st = storm::GetStatus();
            char line[320];

            std::snprintf(line, sizeof(line), "%s: rain grade %.2f%s (rain %.2f), storminess %.2f",
                          storm::PhaseName(static_cast<storm::Phase>(st.phase)), st.grade,
                          st.overridden ? " (the climate panel's override)" : "", st.rain, st.storminess);
            g_api->UiText(line);
            char next[32] = "none coming";
            if (st.nextFlash >= 0.0f) std::snprintf(next, sizeof(next), "in %.0f s", st.nextFlash);
            std::snprintf(line, sizeof(line), "now: darkness %.2f, flash in the sky %.2f, on the ground %.2f; next flash %s",
                          st.darkness, st.skyFlash, st.sceneFlash, next);
            g_api->UiText(line);
            std::snprintf(line, sizeof(line), "this session: %u flashes in the clouds, %u strikes (last at %.0f yd), %u cracks played (%d on their way), %u bolts (%d up), %u frames of strike light; scorch mark: %.0f s left",
                          st.flashes, st.strikes, st.lastStrikeDistance, st.sounds, st.pendingSounds, st.bolts, st.liveBolts, st.lights, st.scorchLeft);
            g_api->UiText(line);
            std::snprintf(line, sizeof(line), "day-night colours: %s (%u frames shaded); bolt models: %s", storm::BandStatus(), st.bandFrames, storm::BoltStatus());
            g_api->UiText(line);

            g_api->UiSeparator();
            g_api->UiCheckbox("storms##storm", &s.enabled);
            g_api->UiSliderFloat("darkness in full storm##storm", &s.darkness, 0.0f, 1.0f);
            g_api->UiCheckbox("flashes in the clouds (light and medium rain too)##storm", &s.flashes);
            g_api->UiSliderFloat("flash strength##storm", &s.flashStrength, 0.0f, 2.0f);
            g_api->UiSliderFloat("how often (x)##storm", &s.frequency, 0.1f, 4.0f);
            g_api->UiCheckbox("heavy rain: strikes##storm", &s.thunder);
            g_api->UiCheckbox("thunder sounds##storm", &s.sounds);
            g_api->UiSliderFloat("strikes farther than this are silent (yd)##storm", &s.soundDistance, 0.0f, 2500.0f);
            g_api->UiSliderFloat("thunder volume (x; each recording at most full)##storm", &s.soundVolume, 0.0f, 3.0f);
            g_api->UiCheckbox("rolling rumble after the cracks (alone for very far strikes)##storm", &s.rumble);
            g_api->UiCheckbox("thunder through the client's sound kits (volume by distance, direction); off = Lua PlaySound##storm", &s.nativeSound);
            g_api->UiSliderFloat("strike light##storm", &s.strikeLight, 0.0f, 3.0f);
            g_api->UiSliderFloat("storm gusts##storm", &s.wind, 0.0f, 2.0f);
            g_api->UiSliderFloat("storm haze##storm", &s.haze, 0.0f, 1.0f);
            g_api->UiSliderFloat("storm fog brightness by day (x the client's fog; 3/4 of it at night)##storm", &s.fogBrightness, 0.5f, 2.0f);
            g_api->UiSliderFloat("storm fog cool tint (a pale blue-grey)##storm", &s.fogCool, 0.0f, 1.0f);
            g_api->UiSliderFloat("snow: grey fog, sky and clouds by day (x their brightness)##storm", &s.snowBrightness, 1.0f, 3.0f);
            g_api->UiSliderFloat("snow: their grey to a pale blue-white##storm", &s.snowWhite, 0.0f, 1.0f);
            g_api->UiSliderFloat("snow: the distance fog ends no nearer than (yd; 0 = the client's)##storm", &s.snowDistance, 0.0f, 800.0f);
            if (st.fogIn || st.fogOut)
            {
                std::snprintf(line, sizeof(line), "the fog (the distance fog; the ground fog and haze take it): the client's #%06X -> ours #%06X, day %.2f",
                              static_cast<unsigned>(st.fogIn & 0xFFFFFF), static_cast<unsigned>(st.fogOut & 0xFFFFFF), st.day);
                g_api->UiText(line);
            }
            std::snprintf(line, sizeof(line), "snow (%s): the weather set's share %.2f; the distance fog ends %.0f -> %.0f yd, starts x%.2f -> x%.2f",
                          storm::SnowStatus(), st.snow, st.fogEndIn, st.fogEndOut, st.fogStartIn, st.fogStartOut);
            g_api->UiText(line);
            if (g_api->UiCollapsingHeader("Experimental##storm"))
            {
                g_api->UiCheckbox("bolt models at strikes (our own models in the client's scene)##storm", &s.bolts);
                g_api->UiSliderFloat("bolt size (x Call Lightning's 11.5 yd)##storm", &s.boltSize, 1.0f, 15.0f);
                g_api->UiSliderFloat("bolts up to (yd)##storm", &s.boltRange, 100.0f, 2500.0f);
                g_api->UiCheckbox("scorch marks where near strikes land (terrain only)##storm", &s.scorch);
                g_api->UiSliderFloat("scorch fades over (s)##storm", &s.scorchSeconds, 5.0f, 120.0f);
                thunderdsp::Settings& d = thunderdsp::Config();
                g_api->UiCheckbox("thunder through distance filters: farther = muffled, lower, rolling (the client's own sound engine)##storm", &d.filters);
                g_api->UiSliderFloat("filter strength (x how far the low-pass and the pitch-down go)##storm", &d.strength, 0.0f, 2.0f);
                g_api->UiSliderFloat("echo amount (x its repeats)##storm", &d.echoAmount, 0.0f, 2.0f);
                const thunderdsp::Status& ft = thunderdsp::GetStatus();
                std::snprintf(line, sizeof(line), "  filters: %s", thunderdsp::StatusText());
                g_api->UiText(line);
                std::snprintf(line, sizeof(line), "  %u layers filtered, %u near enough to stay crisp, %u unfiltered while unavailable; chains: %d made, %d busy now",
                              ft.filtered, ft.plain, ft.unavailable, ft.chains, ft.busyChains);
                g_api->UiText(line);
                std::snprintf(line, sizeof(line), "  %u sounds joined a chain (%u sharing one), %u layers found no chain free, %u came too late for theirs, %u refused by FMOD",
                              ft.pool.joined, ft.pool.shared, ft.pool.busy, ft.pool.stale, ft.pool.failedJoins);
                g_api->UiText(line);
            }

            g_api->UiSeparator();
            g_api->UiText("Testing (not saved). Rain grades: light from 0.27, medium from 0.40, heavy (thunder) from 0.70.");
            g_api->UiSliderFloat("rain grade##stormtest", &g_grade, 0.0f, 1.0f);
            if (g_api->UiButton("Storm at this grade (ours only)##stormtest")) climate::SetWeatherOverride(1, g_grade);
            g_api->UiSameLine();
            // As the climate page does: the client shows it until the server sends weather again, and the live
            // weather follows it.
            if (g_api->UiButton("Rain at this grade in the client##stormtest"))
            {
                clientweather::Request(clientweather::StateFor(1, g_grade), g_grade, false);
                climate::SetWeatherOverride(-1, g_grade);
            }
            g_api->UiSameLine();
            if (g_api->UiButton("Live weather##stormtest")) climate::SetWeatherOverride(-1, g_grade);
            if (clientweather::Last().state || clientweather::Last().applied)
            {
                const clientweather::Result& r = clientweather::Last();
                std::snprintf(line, sizeof(line), "client weather: state %u, %s", r.state, r.note);
                g_api->UiText(line);
            }
            if (g_api->UiButton("Flash in the clouds##stormtest")) storm::TestFlash();
            g_api->UiSameLine();
            if (g_api->UiButton("Strike, random distance##stormtest")) storm::TestStrike(-1.0f);
            g_api->UiSliderFloat("distance (yd)##stormtest", &g_distance, storm::kNearestStrike, storm::kFarthestStrike);
            if (g_api->UiButton("Strike at this distance##stormtest")) storm::TestStrike(g_distance);
            g_api->UiSameLine();
            if (g_api->UiButton("Its crack, now##stormtest")) storm::TestSound(g_distance);
            const storm::ThunderPlan plan = storm::PlanThunder(g_distance, s.soundDistance, 0.0f, s.rumble != 0);
            char heard[200] = "nothing (beyond the silent distance)";
            int at = 0;
            for (int i = 0; i < plan.count && at >= 0 && at < static_cast<int>(sizeof(heard)); ++i)
            {
                const storm::KitInfo& k = storm::Info(plan.layer[i].kit);
                at += std::snprintf(heard + at, sizeof(heard) - at, "%s%s at %.2f%s", i ? ", then " : "", k.label,
                                    storm::LayerVolume(plan.layer[i].kit, plan.layer[i].gain, s.soundVolume), plan.layer[i].after > 0.0f ? " (rolling after it)" : "");
            }
            std::snprintf(line, sizeof(line), "at %.0f yd: the thunder comes %.1f s after the flash: %s%s", g_distance,
                          storm::ThunderDelay(g_distance), heard,
                          plan.count && g_distance < storm::kNearCrack ? " (near cracks vary: machine lightning half the time, Shaman thunder or Call Lightning)" : "");
            g_api->UiText(line);

            // Audition: each candidate recording at the slider's distance.
            const char* kits[static_cast<int>(storm::Kit::Count)];
            for (int i = 0; i < static_cast<int>(storm::Kit::Count); ++i) kits[i] = storm::kKits[i].label;
            g_api->UiCombo("recording##stormkit", &g_kit, kits, static_cast<int>(storm::Kit::Count));
            g_api->UiSameLine();
            if (g_api->UiButton("Play it at this distance##stormkit")) storm::TestKit(g_kit, g_distance, g_kitFilters ? 1 : 0);
            g_api->UiSameLine();
            g_api->UiCheckbox("as if that far: its distance filters##stormkit", &g_kitFilters);
            {
                const storm::KitInfo& k = storm::kKits[g_kit >= 0 && g_kit < static_cast<int>(storm::Kit::Count) ? g_kit : 0];
                const float volume = storm::LayerVolume(static_cast<storm::Kit>(g_kit), storm::ThunderGain(g_distance, 1.0e9f), s.soundVolume);
                const char* lua = storm::LuaRowFor(static_cast<storm::Kit>(g_kit), volume);
                std::snprintf(line, sizeof(line), "  kit %d \"%s\": volume %.2f here (its own %.2f, min distance %.0f yd%s); Lua fallback: %s",
                              k.soundKitId, k.name, volume, k.volume, k.minDistance, k.positional ? ", from the strike's direction" : ", not positional",
                              lua ? lua : "none quiet enough (left out)");
                g_api->UiText(line);
                char filters[160];
                const storm::Kit kit = static_cast<storm::Kit>(g_kit >= 0 && g_kit < static_cast<int>(storm::Kit::Count) ? g_kit : 0);
                Describe(thunderdsp::Preview(kit == storm::Kit::Rumble ? thunderdsp::Layer::Rumble : thunderdsp::Layer::Crack, g_distance), filters, sizeof(filters));
                std::snprintf(line, sizeof(line), "  its distance filters here: %s%s", filters, g_kitFilters ? "" : " -- off for this audition");
                g_api->UiText(line);
            }
            std::snprintf(line, sizeof(line), "thunder through %s; played %u through the kits, %u through Lua; declined by the client %u, left out %u; last kit result %d",
                          storm::SoundStatus(), st.nativeSounds, st.luaSounds, st.declinedSounds, st.droppedSounds, st.lastKitResult);
            g_api->UiText(line);
        }
    }

    void RegisterStormPanel(const WXL_Api* api)
    {
        g_api = api;
        pages::AddPanel(kPanelTitle, &Panel);
    }
}
