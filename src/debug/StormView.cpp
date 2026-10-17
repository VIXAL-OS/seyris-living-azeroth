#include "StormView.hpp"
#include "Pages.hpp"

#include "../env/ClientWeather.hpp"
#include "../env/Climate.hpp"
#include "../env/Storm.hpp"
#include "../env/StormModel.hpp"

#include <cstdio>

namespace wxl_livingazeroth::debug
{
    namespace
    {
        constexpr const char* kPanelTitle = "wxl-seyris-living-azeroth: storms";
        const WXL_Api* g_api = nullptr;
        float g_grade = 0.8f;
        float g_distance = 400.0f;

        void __cdecl Panel(void* /*user*/)
        {
            storm::Settings& s = storm::Config();
            const storm::Status& st = storm::GetStatus();
            char line[320];

            std::snprintf(line, sizeof(line), "%s: rain grade %.2f%s, storminess %.2f",
                          storm::PhaseName(static_cast<storm::Phase>(st.phase)), st.grade,
                          st.overridden ? " (the climate panel's override)" : "", st.storminess);
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
            g_api->UiSliderFloat("strike light##storm", &s.strikeLight, 0.0f, 3.0f);
            g_api->UiSliderFloat("storm gusts##storm", &s.wind, 0.0f, 2.0f);
            g_api->UiSliderFloat("storm haze##storm", &s.haze, 0.0f, 1.0f);
            if (g_api->UiCollapsingHeader("Experimental##storm"))
            {
                g_api->UiCheckbox("bolt models at strikes (our own models in the client's scene)##storm", &s.bolts);
                g_api->UiSliderFloat("bolt size (x Call Lightning's 11.5 yd)##storm", &s.boltSize, 1.0f, 15.0f);
                g_api->UiSliderFloat("bolts up to (yd)##storm", &s.boltRange, 100.0f, 2500.0f);
                g_api->UiCheckbox("scorch marks where near strikes land (terrain only)##storm", &s.scorch);
                g_api->UiSliderFloat("scorch fades over (s)##storm", &s.scorchSeconds, 5.0f, 120.0f);
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
            const char* sound = storm::ThunderSound(g_distance, s.soundDistance);
            std::snprintf(line, sizeof(line), "at %.0f yd: the crack comes %.1f s after the flash, as %s", g_distance,
                          storm::ThunderDelay(g_distance), sound ? sound : "nothing (beyond the silent distance)");
            g_api->UiText(line);
        }
    }

    void RegisterStormPanel(const WXL_Api* api)
    {
        g_api = api;
        pages::AddPanel(kPanelTitle, &Panel);
    }
}
