// Lumenveil's lights (2026-10-25, the ideas only): the belt lantern (env/LanternModel, features/Lantern, its way through
// env/Lights and features/ModelLights), distant night lights (env/DistantLightModel, features/DistantLights over the
// shipped WindowLights.cdbc through the game's own cdbc reader) and the compare key (debug/Compare).
//  1. The lantern's rules: how dark a place is, the automatic choice and its hysteresis, the key's choice holding until the
//     light changes, the fades, nothing by day outdoors however it was lit, a lit inn and a torch passed in a mine.
//  2. The lantern in env/Lights (UpdateForTests, the light taken as Update takes it: LanternLights): at the hip, never
//     merged with a torch beside it, first in a full grid cell (in the scene's places, not the spell lights'), no dip,
//     indoor as given, under the knee; LightAround leaves it out; our lights off: none.
//  3. The distant lights' rules: the fade-in band, the fog, the far clip, sizes on the screen, merging and the cap.
//  4. The windows and light tables through the real reader (wxl-seyris-tools' Cdbc.cpp, linked; copies of the tables in a
//     scratch DBFilesClient), Lakeshire's windows from 600 yd at night: glows, all past the fade-in band; none by day.
//  5. The compare key: every switch off through its own path and back as it was; a switch changed in a panel meanwhile
//     kept; its chat lines; the settings through config::Apply (the new keys, their defaults, described without bracketed
//     section names), and no setting read or written by a toggle.
//  5b. The light list other extensions read (LightList.hpp, "chromie.light-list" v1, for Modern Water): Active() bit for bit
//     in its order, after merging, with its flags (indoor, windows, the spells and the lantern moving, a strike plain); the
//     same array until the next update; empty with our lights off, by the compare key, leaving and out of the world.
//  6. On WARP (the CPU rasterizer behind D3D9On12, no GPU), contact sheets with --sheet <folder>:
//      a. the lantern in a dark cave: the stock lit M2 shader as features/ModelLights patches it, its light texture from
//         env/Lights' grid; without it, with it, with a torch beside it, and at noon outdoors (lit, but nothing);
//      b. a town's glow from 600 yd at night: Lakeshire's windows and lamps through features/DistantLights' own draw over a
//         night landscape with depth, a hill in front hiding part of the town, and in fog;
//      c. the compare key's off/on pair: both scenes on, off by the key, on again (the same pixels).
//  The pixel shader's slots (the distant glow's own, ps_2_0).
// Run: lumen_tests <M2 shader folder> <repo data folder> [--no-warp] [--sheet <folder>]
#include "../../src/Config.hpp"   // (the core has a Config.hpp of its own on the include path)
#include "debug/ChatLine.hpp"
#include "debug/Compare.hpp"
#include "env/DistantLightModel.hpp"
#include "env/LanternModel.hpp"
#include "env/Lights.hpp"
#include "env/Storm.hpp"
#include "features/DistantLights.hpp"
#include "features/DoodadLightTable.hpp"
#include "features/Glass.hpp"
#include "features/GroundFog.hpp"
#include "features/Lantern.hpp"
#include "features/ModelLights.hpp"
#include "features/SeeThrough.hpp"
#include "features/SpellLights.hpp"
#include "features/Windows.hpp"
#include "render/ShaderPatch.hpp"
#include "wxl/PluginApi.h"
#include "wxl_seyris/CdbcApi.hpp"
#include "wxl_seyris/SettingsApi.hpp"
#include "LightList.hpp"

#include <windows.h>
#include <d3d9.h>
#include <d3d9on12.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace la = wxl_livingazeroth;
namespace li = wxl_livingazeroth::lights;
namespace ln = wxl_livingazeroth::lantern;
namespace dl = wxl_livingazeroth::distantlight;
namespace ds = wxl_livingazeroth::distantlights;
namespace ml = wxl_livingazeroth::modellights;
namespace cmp = wxl_livingazeroth::compare;

// wxl-seyris-tools' reader (its src/Cdbc.cpp, linked in), as glass_load_tests links it.
namespace wxl_seyris::cdbc
{
    void*       Load(const WXL_SeyrisCdbcDefinition* def, char* errorBuf, size_t errorBufSize);
    void        Release(void* table);
    uint32_t    RowCount(void* table);
    const void* RowAt(void* table, uint32_t index);
    const void* FindRow(void* table, uint32_t id);
    uint32_t    Value(void* table, const void* row, const char* field, uint32_t element);
    const char* GetString(void* table, const void* row, const char* field);
    size_t      FieldIndex(void* table, const char* field);
    const void* FindRowByField(void* table, const char* field, uint32_t value, const void* after);
    void*       RowAdd(void* table);
    int         RowUpdateValue(void* table, void* row, const char* field, uint32_t value);
    int         RowUpdateString(void* table, void* row, const char* field, const char* value);
    int         RowDelete(void* table, void* row);
    int         Save(void* table, char* errorBuf, size_t errorBufSize);
    void*       Reload(void* table, char* errorBuf, size_t errorBufSize);
}

namespace
{
    int g_failures = 0;
    void Check(bool ok, const std::string& what)
    {
        std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++g_failures;
    }
    void Info(const std::string& what) { std::printf("info  %s\n", what.c_str()); }
    std::string F(const char* format, ...)
    {
        char b[2048];
        va_list a;
        va_start(a, format);
        std::vsnprintf(b, sizeof(b), format, a);
        va_end(a);
        return b;
    }
    bool Has(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }
    float MaxChannel(const float c[3]) { return std::max(c[0], std::max(c[1], c[2])); }

    std::vector<std::string> g_chat;
    void Capture(const std::string& line) { g_chat.push_back(line); }

    // --- 1. the lantern's rules ------------------------------------------------------------------------------------------
    void LanternRules()
    {
        std::printf("-- 1. the lantern's rules\n");
        Check(ln::PlaceDarkness(1.0f, false, 0.0f) == 0.0f && ln::PlaceDarkness(0.35f, false, 0.0f) == 0.0f, "outdoors by day (daylight 1, 0.35): not dark");
        Check(ln::PlaceDarkness(0.0f, false, 0.0f) == 1.0f, "outdoors at night: dark");
        const float dusk = ln::PlaceDarkness(0.2f, false, 0.0f);
        Check(dusk > 0.35f && dusk < 0.5f, F("dusk (daylight 0.2): %.2f", dusk));
        Check(ln::PlaceDarkness(1.0f, true, 0.0f) == 1.0f, "indoors (a cave, a mine) at noon: dark");
        Check(std::fabs(ln::PlaceDarkness(1.0f, false, 0.35f) - 0.35f) < 1e-5f, "a full storm by day: the storm's darkening (0.35), not enough to light it");
        Check(ln::LitShare(0.0f) == 1.0f && ln::LitShare(ln::kLitFull) == 0.0f && ln::LitShare(0.75f) > 0.2f && ln::LitShare(0.75f) < 0.8f,
              F("the light round you: none 1, a lit inn's %.1f 0, between %.2f", ln::kLitFull, ln::LitShare(0.75f)));

        ln::Settings c;
        auto run = [&](ln::State& s, float seconds, float daylight, bool indoors, float around, float storm = 0.0f)
        {
            float k = 0.0f;
            for (float t = 0.0f; t < seconds; t += 1.0f / 30.0f) k = ln::Advance(s, c, daylight, indoors, storm, around, 1.0f / 30.0f);
            return k;
        };
        ln::State s;
        Check(run(s, 2.0f, 1.0f, false, 0.0f) == 0.0f && !s.lit, "automatic, at noon outdoors: out");
        float k = run(s, 0.3f, 0.0f, false, 0.0f);
        Check(s.lit && k > 0.3f && k < 0.75f, F("night falls: it lights itself, fading in (%.2f after 0.3 s)", k));
        k = run(s, 1.0f, 0.0f, false, 0.0f);
        Check(std::fabs(k - 1.0f) < 1e-4f, F("at night, after a second: full (%.2f)", k));
        Check(!ln::Press(s, c) && run(s, 0.5f, 0.0f, false, 0.0f) == 0.0f, "the key at night: out (gone in under half a second)");
        Check(run(s, 30.0f, 0.0f, false, 0.0f) == 0.0f && !s.lit, "and it stays out all night");
        run(s, 2.0f, 1.0f, false, 0.0f);
        Check(s.held == -1, "daybreak (the automatic choice changes): the key's choice gives way");
        k = run(s, 2.0f, 0.0f, false, 0.0f);
        Check(s.lit && k > 0.99f, "the next night: it lights itself again");

        ln::State d;
        run(d, 1.0f, 1.0f, false, 0.0f);
        Check(ln::Press(d, c) && run(d, 2.0f, 1.0f, false, 0.0f) == 0.0f && d.lit, "lit by the key at noon outdoors: lit, but it gives nothing (bright daylight)");
        k = run(d, 1.0f, 1.0f, true, 0.0f);
        Check(k > 0.99f, F("lit, stepping into a cave at noon: full (%.2f)", k));

        ln::State inn;
        run(inn, 3.0f, 0.0f, true, 1.6f);
        Check(!inn.lit, F("a lit inn at night (indoors, a hearth and candles round you: %.1f): it stays out", 1.6f));
        ln::State mine;
        run(mine, 2.0f, 1.0f, true, 0.05f);
        Check(mine.lit, "a dark mine: lit");
        float least = 1.0f;
        for (float t = 0.0f; t < 0.6f; t += 1.0f / 30.0f) least = std::min(least, ln::Advance(mine, c, 1.0f, true, 0.0f, 0.9f, 1.0f / 30.0f));
        const bool still = mine.lit;
        k = run(mine, 3.0f, 1.0f, true, 0.05f);
        Check(still && least > 0.5f && k > 0.95f, F("walking past a torch in the mine (0.6 s at 0.9): still lit, at least %.2f, full again after (%.2f)", least, k));

        ln::Settings manual;
        manual.automatic = 0;
        ln::State m;
        float km = 0.0f;
        for (int f = 0; f < 60; ++f) km = ln::Advance(m, manual, 0.0f, false, 0.0f, 0.0f, 1.0f / 30.0f);
        Check(!m.lit && km == 0.0f, "Automatic 0, at night: it doesn't light itself");
        ln::Press(m, manual);
        for (int f = 0; f < 60; ++f) km = ln::Advance(m, manual, 0.0f, false, 0.0f, 0.0f, 1.0f / 30.0f);
        Check(m.lit && km > 0.99f, "Automatic 0: the key lights it");
        ln::Press(m, manual);
        for (int f = 0; f < 60; ++f) km = ln::Advance(m, manual, 0.0f, false, 0.0f, 0.0f, 1.0f / 30.0f);
        Check(!m.lit && km == 0.0f, "and puts it out");
        ln::State n;
        Check(ln::Advance(n, c, 0.0f, false, 0.0f, NAN, NAN) >= 0.0f && std::isfinite(n.around), "a NaN in: no NaN out");
    }

    // --- 2. the lantern in env/Lights -------------------------------------------------------------------------------------
    li::FrameLight Torch(float x, float y, float z, const void* model)
    {
        li::FrameLight f{};
        f.pos[0] = x; f.pos[1] = y; f.pos[2] = z;
        f.color[0] = 1.0f; f.color[1] = 0.63f; f.color[2] = 0.31f;
        f.radius = 20.0f;
        f.model = model;
        f.indoor = 0;
        return f;
    }

    // The lantern at night (or `daylight`), the player at `feet` facing +x: its rules stepped for a second and a half.
    std::vector<li::OwnLight> LanternAt(const float feet[3], float daylight, bool indoors, float around)
    {
        float s = 0.0f;
        for (int f = 0; f < 45; ++f) s = ln::StepForTests(1.0f / 30.0f, feet, 0.0f, daylight, indoors, 0.0f, around);
        (void)s;
        std::vector<li::OwnLight> own;
        li::LanternLights(own);
        return own;
    }

    void LanternInLights()
    {
        std::printf("-- 2. the lantern in env/Lights\n");
        li::Config() = li::Settings{};
        la::spelllights::Config().enabled = 0;
        ln::Config() = ln::Settings{};
        ln::ResetForTests();
        const float feet[3] = { 100.0f, 200.0f, 10.0f };
        std::vector<li::OwnLight> own = LanternAt(feet, 0.0f, false, 0.0f);
        Check(own.size() == 1, "at night outdoors: one light of the player's own");
        if (own.empty()) return;
        const li::OwnLight o = own[0];
        Check(std::fabs(o.pos[0] - (feet[0] + ln::kForward)) < 1e-3f && std::fabs(o.pos[1] - (feet[1] - ln::kSide)) < 1e-3f && std::fabs(o.pos[2] - (feet[2] + ln::kHipHeight)) < 1e-3f,
              F("at the right hip, facing +x: %.2f %.2f %.2f", o.pos[0] - feet[0], o.pos[1] - feet[1], o.pos[2] - feet[2]));
        Check(o.color[0] > o.color[1] && o.color[1] > o.color[2] && o.color[0] < 0.8f && o.color[0] > 0.5f && o.radius == 8.0f && !o.indoor,
              F("warm and modest: %.2f %.2f %.2f, %.0f yd, outdoors", o.color[0], o.color[1], o.color[2], o.radius));
        Check(LanternAt(feet, 1.0f, false, 0.0f).empty(), "at noon outdoors: none");
        ln::ResetForTests();
        own = LanternAt(feet, 1.0f, true, 0.0f);
        Check(own.size() == 1 && own[0].indoor, "in a cave at noon: one, indoor (off the terrain, the cover and the fog outside)");
        ln::ResetForTests();
        own = LanternAt(feet, 0.0f, false, 0.0f);

        // A torch half a yard off, and a cell crowded with torches.
        const float eye[3] = { feet[0] - 6.0f, feet[1] - 5.0f, feet[2] + 4.0f };
        std::vector<li::FrameLight> scene;
        scene.push_back(Torch(own[0].pos[0] + 0.5f, own[0].pos[1], own[0].pos[2], reinterpret_cast<const void*>(0x1000)));
        li::UpdateForTests(eye, 1.0f / 60.0f, {}, scene, own);
        const std::vector<li::ActiveLight>& a = li::Active();
        const li::ActiveLight* lamp = nullptr;
        const li::ActiveLight* torch = nullptr;
        for (const li::ActiveLight& l : a) (l.own ? lamp : torch) = &l;
        Check(a.size() == 2 && lamp && torch && std::fabs(torch->pos[0] - scene[0].pos[0]) < 1e-4f, F("a torch half a yard away: not merged (%zu lights, the torch where it was)", a.size()));
        Check(lamp && lamp->dip == 0.0f && !lamp->indoor, "its flicker reports no dip (the WMO interiors' baked light doesn't pulse with it)");
        const li::LightAt here = li::SumAt(own[0].pos, nullptr);
        Check(here.kneed[0] <= li::kLampKneeStart + li::kLampKneeRoom + 1e-4f && here.sum[0] > here.kneed[0],
              F("the lantern and the torch at the lantern: %.2f, after the knee %.2f (at most %.2f)", here.sum[0], here.kneed[0], li::kLampKneeStart + li::kLampKneeRoom));
        Check(!here.strongest.empty() && (here.strongest[0].model == "(your lantern)" || (here.strongest.size() > 1 && here.strongest[1].model == "(your lantern)")),
              "the snapshot names it: (your lantern)");
        Check(li::LightAround(own[0].pos, ln::kAroundReach) > 0.0f && li::LightAround(own[0].pos, ln::kAroundReach) < 1.0f,
              F("the light round it counts the torch, not itself: %.2f", li::LightAround(own[0].pos, ln::kAroundReach)));

        scene.clear();
        for (int k = 0; k < 30; ++k)
            scene.push_back(Torch(own[0].pos[0] + 3.0f + (k % 6) * 1.5f, own[0].pos[1] + 2.0f + (k / 6) * 1.5f, own[0].pos[2], reinterpret_cast<const void*>(static_cast<uintptr_t>(0x2000 + k * 16))));
        li::UpdateForTests(eye, 1.0f / 60.0f, {}, scene, own);
        const li::Grid& g = li::CellGrid();
        const int cx = static_cast<int>(std::floor((own[0].pos[0] - g.originX) / g.cellSize)), cy = static_cast<int>(std::floor((own[0].pos[1] - g.originY) / g.cellSize));
        const size_t cell = static_cast<size_t>(cy) * g.cells + cx;
        const unsigned listed = g.count[cell];
        bool first = false;
        for (size_t i = 0; i < li::Active().size(); ++i)
            if (li::Active()[i].own) first = g.index[cell * g.perCell] == i;
        Check(static_cast<int>(listed) == li::Config().maxPerCell && first, F("its cell full of torches (30 near): %u listed (%d places), the lantern first", listed, li::Config().maxPerCell));

        la::spelllights::Config().enabled = 1;
        std::vector<li::FrameLight> spells;
        for (int k = 0; k < 20; ++k)
        {
            li::FrameLight f = Torch(own[0].pos[0] + 1.0f + k * 0.3f, own[0].pos[1] - 1.0f, own[0].pos[2], reinterpret_cast<const void*>(static_cast<uintptr_t>(0x9000 + k * 16)));
            f.color[0] = 0.3f; f.color[1] = 0.5f; f.color[2] = 1.0f; f.radius = 7.0f;
            spells.push_back(f);
        }
        li::UpdateForTests(eye, 1.0f / 60.0f, spells, scene, own);
        const li::Grid& g2 = li::CellGrid();
        unsigned spellsListed = 0, scenes = 0;
        bool lampListed = false;
        for (unsigned k = 0; k < g2.count[cell]; ++k)
        {
            const li::ActiveLight& l = li::Active()[g2.index[cell * g2.perCell + k]];
            if (l.own) lampListed = true;
            else if (l.color[2] > l.color[0]) ++spellsListed;
            else ++scenes;
        }
        Check(lampListed && static_cast<int>(spellsListed) == li::kSpellSlotsPerCell && static_cast<int>(scenes + 1) == li::Config().maxPerCell,
              F("with spell lights on: the lantern in the scene's places (%u torches + it), the spells' %u places their own", scenes, spellsListed));
        la::spelllights::Config().enabled = 0;

        li::Config().enabled = 0;
        li::UpdateForTests(eye, 1.0f / 60.0f, {}, scene, own);
        Check(li::Active().empty() && li::CellGrid().cells == 0, "our lights off: nothing drawn, the grid empty (the client's own lights back)");
        li::Config().enabled = 1;

        // The key, and its chat lines.
        g_chat.clear();
        ln::PressForTests();
        Check(!g_chat.empty() && Has(g_chat.back(), "lantern out") && Has(g_chat.back(), "F7"), "the key at night: out, and chat says so (" + (g_chat.empty() ? std::string("nothing") : g_chat.back()) + ")");
        ln::PressForTests();
        Check(!g_chat.empty() && Has(g_chat.back(), "lantern lit"), "again: lit (" + (g_chat.empty() ? std::string("nothing") : g_chat.back()) + ")");
        ln::Config().enabled = 0;
        ln::PressForTests();
        Check(!g_chat.empty() && Has(g_chat.back(), "switched off"), "switched off: the key says so (" + (g_chat.empty() ? std::string("nothing") : g_chat.back()) + ")");
        ln::Config().enabled = 1;
        ln::ResetForTests();
    }

    // --- 3. the distant lights' rules -------------------------------------------------------------------------------------
    void DistantRules()
    {
        std::printf("-- 3. the distant lights' rules\n");
        Check(dl::NearFade(170.0f, 256.0f) == 0.0f && dl::NearFade(256.0f, 256.0f) == 1.0f && dl::NearFade(213.0f, 256.0f) > 0.4f && dl::NearFade(213.0f, 256.0f) < 0.6f,
              "they fade in over the last third of our lights' range, as the lights fade out (170 yd 0, 213 yd a half, 256 yd 1)");
        Check(dl::FogVisibility(100.0f, 122.0f, 490.0f, 1.5f) == 1.0f && std::fabs(dl::FogVisibility(490.0f, 122.0f, 490.0f, 1.5f) - std::exp(-1.0f / 1.5f)) < 1e-4f,
              F("in the clear default's fog (122 .. 490 yd): 1 before it, %.2f at its end", dl::FogVisibility(490.0f, 122.0f, 490.0f, 1.5f)));
        Check(dl::FogVisibility(600.0f, 122.0f, 490.0f, 1.5f) > 0.35f && dl::FogVisibility(600.0f, -139.0f, 278.0f, 1.5f) < dl::FogVisibility(600.0f, 122.0f, 490.0f, 1.5f),
              F("at 600 yd: %.2f in the clear night's fog, %.2f in the weather's (ends at 278)", dl::FogVisibility(600.0f, 122.0f, 490.0f, 1.5f), dl::FogVisibility(600.0f, -139.0f, 278.0f, 1.5f)));
        Check(dl::FogVisibility(600.0f, 0.0f, 0.0f, 1.5f) == 1.0f && dl::FarClipFade(700.0f, 1000.0f) == 1.0f && dl::FarClipFade(990.0f, 1000.0f) == 0.0f,
              "no fog: 1; the far clip at 1000 yd: 1 at 700, 0 at 990");
        const dl::Screen at600 = dl::OnScreen(1.0f, 600.0f, 540.0f), at30 = dl::OnScreen(1.0f, 30.0f, 540.0f);
        Check(at600.corePixels == dl::kMinCorePixels && at600.halfPixels == std::max(dl::kMinHaloPixels, dl::kMinCorePixels * dl::kHaloScale) && at30.corePixels == 18.0f &&
                  at30.halfPixels == 63.0f,
              F("on the screen: at 600 yd the least (core %.2f px, halo %.1f), at 30 yd %.0f px", at600.corePixels, at600.halfPixels, at30.corePixels));
        Check(dl::LampSize(9.45f) > 0.6f && dl::LampSize(9.45f) < 0.75f && dl::LampSize(20.0f) > 1.0f && dl::LampSize(20.0f) < 1.1f,
              F("a candle's core %.2f yd, a torch's %.2f", dl::LampSize(9.45f), dl::LampSize(20.0f)));

        const float eye[3] = { 0.0f, 0.0f, 0.0f };
        std::vector<dl::Source> in;
        for (int k = 0; k < 10; ++k) in.push_back({ { 600.0f + (k % 3) * 0.3f, 0.2f * (k / 3), 10.0f }, { 0.9f, 0.6f, 0.3f }, 1.0f, dl::kLamp });
        in.push_back({ { 600.0f, 40.0f, 10.0f }, { 0.9f, 0.6f, 0.3f }, 1.0f, dl::kWindow });
        std::vector<dl::Sprite> out;
        dl::Cluster(in, eye, out);
        const dl::Sprite* ten = nullptr;
        for (const dl::Sprite& s : out) if (s.count == 10) ten = &s;
        Check(out.size() == 2 && ten && std::fabs(ten->color[0] - 9.0f / std::sqrt(10.0f)) < 1e-3f && ten->size > 1.8f,
              F("ten lamps within a yard at 600 yd: one glow with sqrt(10) of one's light (%.2f), a little bigger (%.2f yd); one 40 yd off apart", ten ? ten->color[0] : 0.0f, ten ? ten->size : 0.0f));
        std::vector<dl::Sprite> many;
        for (int k = 0; k < 50; ++k) many.push_back({ { 300.0f + k * 20.0f, 0, 0 }, { 0.1f + 0.01f * k, 0.1f, 0.1f }, 1.0f, 300.0f + k * 20.0f, 1, dl::kLamp });
        dl::Cap(many, 10);
        bool sorted = many.size() == 10;
        for (size_t k = 1; k < many.size(); ++k) sorted = sorted && many[k - 1].distance <= many[k].distance;
        Check(sorted, "the cap keeps 10 of 50, nearest first after");
    }

    // --- 4. the tables through the real reader --------------------------------------------------------------------------
    int __cdecl LinkedHasFeature(const char* name)
    {
        return name && (std::strcmp(name, "cdbc-load") == 0 || std::strcmp(name, "cdbc-find-by-field") == 0 || std::strcmp(name, "cdbc-write") == 0);
    }
    WXL_SeyrisCdbcApi LinkedApi()
    {
        WXL_SeyrisCdbcApi api{};
        api.structSize = sizeof(WXL_SeyrisCdbcApi);
        api.major = 1; api.minor = 3; api.patch = 0;
        api.HasFeature = &LinkedHasFeature;
        api.Load = &wxl_seyris::cdbc::Load;
        api.Release = &wxl_seyris::cdbc::Release;
        api.RowCount = &wxl_seyris::cdbc::RowCount;
        api.RowAt = &wxl_seyris::cdbc::RowAt;
        api.FindRow = &wxl_seyris::cdbc::FindRow;
        api.Value = &wxl_seyris::cdbc::Value;
        api.GetString = &wxl_seyris::cdbc::GetString;
        api.FieldIndex = &wxl_seyris::cdbc::FieldIndex;
        api.FindRowByField = &wxl_seyris::cdbc::FindRowByField;
        api.RowAdd = &wxl_seyris::cdbc::RowAdd;
        api.RowUpdateValue = &wxl_seyris::cdbc::RowUpdateValue;
        api.RowUpdateString = &wxl_seyris::cdbc::RowUpdateString;
        api.RowDelete = &wxl_seyris::cdbc::RowDelete;
        api.Save = &wxl_seyris::cdbc::Save;
        api.Reload = &wxl_seyris::cdbc::Reload;
        return api;
    }

    // Lakeshire (Redridge, map 0): its windows' middle, and a camera 600 yd south of it, up the hillside, at night.
    float g_town[3] = {};
    unsigned g_townWindows = 0;
    const float kTownDistance = 600.0f;
    void TownEye(float eye[3]) { eye[0] = g_town[0]; eye[1] = g_town[1] - kTownDistance; eye[2] = g_town[2] + 45.0f; }

    // Lamps along the town's streets (the light scan's far list as it would be): a street lamp's colour, every 12 yd.
    std::vector<li::FarLight> TownLamps()
    {
        std::vector<li::FarLight> lamps;
        for (int k = 0; k < 14; ++k)
        {
            li::FarLight l{};
            l.pos[0] = g_town[0] - 40.0f + 12.0f * (k % 7);
            l.pos[1] = g_town[1] - 30.0f + 60.0f * (k / 7);
            l.pos[2] = g_town[2] - 2.0f;
            l.color[0] = 1.0f; l.color[1] = 0.72f; l.color[2] = 0.38f;
            l.radius = 16.0f;
            lamps.push_back(l);
        }
        return lamps;
    }

    bool TablesThroughTheReader(const fs::path& data)
    {
        std::printf("-- 4. the windows and the light tables through the real reader\n");
        char tmp[MAX_PATH] = {};
        GetTempPathA(MAX_PATH, tmp);
        const fs::path root = fs::path(tmp) / ("la-lumen-" + std::to_string(GetCurrentProcessId()));
        const fs::path db = root / "DBFilesClient";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(db, ec);
        const char* const tables[] = { "WindowLights.cdbc", "WindowRooms.cdbc", "DoodadLightProperties.cdbc", "DoodadLightAssignment.cdbc", "GlassMaterials.cdbc",
                                       "GlassPanes.cdbc", "RoomStyles.cdbc", "GlassBuildings.cdbc", "GlassWindows.cdbc", "GlassLead.cdbc", "SeeThroughBuildings.cdbc" };
        bool copied = !ec;
        for (const char* t : tables) copied = copied && fs::copy_file(data / t, db / t, fs::copy_options::overwrite_existing, ec);
        Check(copied, "the window, light and glass tables copied to " + db.string());
        if (!copied) return false;
        const fs::path cwd = fs::current_path();
        fs::current_path(root);
        static const WXL_SeyrisCdbcApi api = LinkedApi();
        la::lighttable::Load(&api);
        la::glass::Load(&api);
        la::windows::Load(&api);
        fs::current_path(cwd);
        Info(std::string("WindowLights: ") + la::windows::Status());
        Info(std::string("DoodadLightProperties / Assignment: ") + la::lighttable::Status());
        Check(la::lighttable::AllProperties().size() >= 30 && la::lighttable::AllAssignments().size() >= 100,
              F("the light tables loaded: %zu properties, %zu assignments", la::lighttable::AllProperties().size(), la::lighttable::AllAssignments().size()));

        // Lakeshire's windows' middle (all of map 0 within 150 yd of its first window, the town's rows).
        std::vector<const la::windows::Window*> all;
        const float probe[3] = { -9214.0f, -2138.0f, 78.0f };
        la::windows::Near(0, probe, 140.0f, true, 1000, all);
        double sum[3] = {};
        for (const la::windows::Window* w : all) for (int k = 0; k < 3; ++k) sum[k] += w->pos[k];
        g_townWindows = static_cast<unsigned>(all.size());
        for (int k = 0; k < 3; ++k) g_town[k] = all.empty() ? probe[k] : static_cast<float>(sum[k] / all.size());
        Check(g_townWindows >= 40, F("Lakeshire's window lights through the reader: %u, round %.0f %.0f %.0f", g_townWindows, g_town[0], g_town[1], g_town[2]));

        ds::Config() = dl::Settings{};
        float eye[3];
        TownEye(eye);
        ds::BuildForTests(eye, 0, 1.0f, {});
        const auto& sprites = ds::Sprites();
        float nearest = 1e9f;
        for (const dl::Sprite& s : sprites) nearest = std::min(nearest, s.distance);
        const ds::Stats& st = ds::GetStats();
        Check(!sprites.empty() && st.windows > 8 && nearest > 256.0f * dl::kFadeFrom,
              F("from 600 yd at night: %u glows from %u windows (of %u looked at), the nearest %.0f yd", st.sprites, st.windows, st.windowsNear, nearest));
        const size_t windowGlows = sprites.size();
        ds::BuildForTests(eye, 0, 0.0f, {});
        Check(ds::Sprites().empty(), "by day (the client's window fade 0): none");
        ds::BuildForTests(eye, 0, 1.0f, TownLamps());
        Check(ds::GetStats().lamps == 14 && ds::Sprites().size() > windowGlows,
              F("with the far lamps: %u lamps too, %zu glows (%zu from the windows alone)", ds::GetStats().lamps, ds::Sprites().size(), windowGlows));
        const float inTown[3] = { g_town[0], g_town[1] - 20.0f, g_town[2] + 2.0f };
        ds::BuildForTests(inTown, 0, 1.0f, TownLamps());
        unsigned ownTown = 0;
        for (const dl::Sprite& s : ds::Sprites())
        {
            const float dx = s.pos[0] - g_town[0], dy = s.pos[1] - g_town[1];
            ownTown += dx * dx + dy * dy < 140.0f * 140.0f ? 1u : 0u;
        }
        Check(ownTown == 0, F("standing in the town: none of its own (its lights are point lights there; %zu glows of farther places)", ds::Sprites().size()));
        ds::Config().enabled = 0;
        ds::BuildForTests(eye, 0, 1.0f, TownLamps());
        Check(ds::Sprites().empty(), "switched off: none");
        ds::Config().enabled = 1;

        // Cost: a frame's build on a hill over Stormwind (its 233 windows and its neighbours' out to 1500 yd) with 3,000 far
        // lamps (a city's light scan far list), as Update runs it every frame.
        std::vector<li::FarLight> lamps;
        uint32_t seed = 12345;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return static_cast<float>(seed >> 8) / 16777216.0f; };
        const float hill[3] = { -8700.0f, 300.0f, 160.0f };
        for (int k = 0; k < 3000; ++k)
        {
            li::FarLight l{};
            const float a = rnd() * 6.2831853f, r = 200.0f + rnd() * 1250.0f;
            l.pos[0] = hill[0] + r * std::cos(a); l.pos[1] = hill[1] + r * std::sin(a); l.pos[2] = 90.0f + rnd() * 20.0f;
            l.color[0] = 1.0f; l.color[1] = 0.7f; l.color[2] = 0.35f;
            l.radius = 16.0f;
            lamps.push_back(l);
        }
        std::vector<double> ms;
        for (int k = 0; k < 60; ++k)
        {
            ds::BuildForTests(hill, 0, 1.0f, lamps);
            ms.push_back(ds::GetStats().msBuild);
        }
        std::sort(ms.begin(), ms.end());
        const ds::Stats& cs = ds::GetStats();
        Info(F("cost over Stormwind: %u windows looked at, %u glows from windows, %u from lamps, %u after merging, %u over the cap; the last build's parts: "
               "windows %.3f ms, lamps %.3f ms, merging and the cap %.3f ms", cs.windowsNear, cs.windows, cs.lamps, cs.sprites, cs.capped, cs.msWindows, cs.msLamps, cs.msMerge));
        const double median = ms[ms.size() / 2];
        Check(median < 1.0, F("a build there: median %.3f ms, most %.3f ms (this machine, one core at idle priority, a game running); built again only when the "
                              "camera moves 4 yd or every quarter second: at 60 fps standing %.3f ms a frame, riding (7 yd/s) %.3f", median, ms.back(), median / 15.0,
                              median * std::max(1.0 / 15.0, 7.0 / 4.0 / 60.0)));
        return g_townWindows > 0;
    }

    // --- 5. the compare key ------------------------------------------------------------------------------------------------
    struct Key { std::string description; float value; };
    std::map<std::string, Key> g_keys;
    unsigned g_settingsCalls = 0;
    float __cdecl FakeGetFloat(const char* ns, const char* key, const char* description, float fallback)
    {
        ++g_settingsCalls;
        g_keys[std::string(ns) + "/" + key] = { description ? description : "", fallback };
        return fallback;
    }
    int __cdecl FakeGetBool(const char* ns, const char* key, const char* description, int fallback)
    {
        return FakeGetFloat(ns, key, description, static_cast<float>(fallback)) != 0.0f ? 1 : 0;
    }
    int __cdecl FakeHasFeature(const char* name) { return std::strcmp(name, "get-float") == 0; }
    WXL_SeyrisSettingsApi g_settingsApi = { sizeof(WXL_SeyrisSettingsApi), 1, 1, 0, &FakeHasFeature, &FakeGetBool, &FakeGetFloat };
    unsigned g_interfaceCalls = 0;
    std::vector<std::string> g_log;
    void __cdecl FakeLog(int level, const char* tag, const char* format, ...)
    {
        char line[4096];
        va_list a;
        va_start(a, format);
        std::vsnprintf(line, sizeof(line), format, a);
        va_end(a);
        g_log.push_back(F("%d %s: %s", level, tag ? tag : "", line));
    }
    void* __cdecl FakeGetInterface(const char* name, uint32_t)
    {
        ++g_interfaceCalls;
        return std::strcmp(name, WXL_SEYRIS_SETTINGS_INTERFACE_NAME) == 0 ? &g_settingsApi : nullptr;
    }
    void __cdecl FakeSubscribe(uint32_t, WXL_EventFn, void*) {}
    WXL_Api MakeApi()
    {
        WXL_Api a{};
        a.structSize = sizeof(WXL_Api);
        a.apiVersion = WXL_API_VERSION;
        a.Log = &FakeLog;
        a.Subscribe = &FakeSubscribe;
        a.GetInterface = &FakeGetInterface;
        return a;
    }

    void CompareKey()
    {
        std::printf("-- 5. the compare key\n");
        static WXL_Api api = MakeApi();
        la::config::Apply(&api);
        struct Want { const char* key; float value; };
        const Want wants[] = { { "LivingAzeroth/CompareKey", 119 }, { "LivingAzeroth.Lantern/Enabled", 1 }, { "LivingAzeroth.Lantern/Key", 118 },
                               { "LivingAzeroth.Lantern/Automatic", 1 }, { "LivingAzeroth.Lantern/Strength", 1 }, { "LivingAzeroth.Lantern/Radius", 8 },
                               { "LivingAzeroth.DistantLights/Enabled", 1 }, { "LivingAzeroth.DistantLights/Windows", 1 }, { "LivingAzeroth.DistantLights/Lamps", 1 },
                               { "LivingAzeroth.DistantLights/Strength", 1 }, { "LivingAzeroth.DistantLights/Size", 1 }, { "LivingAzeroth.DistantLights/MaxDistance", 1500 },
                               { "LivingAzeroth.DistantLights/MaxLights", 1024 }, { "LivingAzeroth.DistantLights/FogPierce", 1.5f } };
        std::string missing;
        for (const Want& w : wants)
        {
            const auto it = g_keys.find(w.key);
            if (it == g_keys.end() || it->second.value != w.value || it->second.description.size() < 40) missing += std::string(" ") + w.key;
        }
        Check(missing.empty(), "the new keys written with their defaults (all on; F7, F8) and described" + (missing.empty() ? std::string() : ":" + missing));
        std::string bracketed;
        for (const auto& k : g_keys)
            if (k.second.description.find('[') != std::string::npos) bracketed += " " + k.first;
        Check(bracketed.empty(), F("no setting's description names a bracketed section (%zu settings)%s", g_keys.size(), bracketed.c_str()));
        bool logged = false;
        for (const std::string& l : g_log) logged |= Has(l, "[LivingAzeroth.Lantern]") && Has(l, "[LivingAzeroth.DistantLights]");
        Check(logged, "the config line names the two new sections");

        cmp::Install(&api);
        cmp::ResetForTests();
        size_t n = 0;
        const cmp::Switch* sw = cmp::Switches(n);
        // Every switch "on" first: the defaults with the experimental ones on too (as the user's ini has them).
        la::seethrough::Config().enabled = 1;
        la::seethrough::Config().fromInside = 1;
        la::spelllights::Config().enabled = 1;
        std::vector<float> before(n);
        for (size_t i = 0; i < n; ++i) before[i] = sw[i].get();
        const unsigned calls = g_settingsCalls, ifaces = g_interfaceCalls;
        g_chat.clear();
        cmp::Toggle("the test");
        std::string notOff;
        for (size_t i = 0; i < n; ++i) if (sw[i].get() != sw[i].off) notOff += std::string(" ") + sw[i].name;
        Check(cmp::EffectsOff() && notOff.empty(), F("off: all %zu switches at their off values%s", n, notOff.c_str()));
        Check(!g_chat.empty() && Has(g_chat.back(), "all effects OFF") && Has(g_chat.back(), "F8"), "chat says so: " + (g_chat.empty() ? std::string("nothing") : g_chat.back()));
        std::vector<std::string> lines;
        cmp::SnapshotLines(lines);
        Check(lines.size() == 2 && Has(lines[0], "all effects OFF since") && Has(lines[1], "our point lights") && Has(lines[1], "darker nights"),
              "the snapshot records it: " + (lines.empty() ? std::string() : lines[0]));
        la::storm::Config().nightDarkness = 0.5f;   // a panel's change while off
        cmp::Toggle("the test");
        std::string notBack;
        for (size_t i = 0; i < n; ++i)
            if (std::string(sw[i].name) != "darker nights" && sw[i].get() != before[i]) notBack += std::string(" ") + sw[i].name;
        Check(!cmp::EffectsOff() && notBack.empty(), "on again: every switch as it was" + notBack);
        Check(la::storm::Config().nightDarkness == 0.5f && Has(g_chat.back(), "1 changed in a panel meanwhile kept"),
              "the one changed in a panel meanwhile (darker nights 0.5) kept: " + g_chat.back());
        Check(g_settingsCalls == calls && g_interfaceCalls == ifaces, "no setting read or written, no settings interface asked for, by either press");
        for (size_t i = 0; i < n; ++i)
            if (std::string(sw[i].name) == "darker nights") la::storm::Config().nightDarkness = before[i];
        la::seethrough::Config().enabled = 0;
        la::seethrough::Config().fromInside = 0;
        la::spelllights::Config().enabled = 0;
        cmp::ResetForTests();
    }

    // --- 5b. the light list other extensions read (LightList.hpp: Modern Water's lamps on the water) -----------------------
    bool SpellModel(const void* m) { const uintptr_t v = reinterpret_cast<uintptr_t>(m); return v >= 0x9000 && v < 0xA000; }

    // What differs between the published list and Active() as it stands (empty: nothing): the count, then each light in
    // Active()'s order, its numbers as drawn (bit for bit) and its flags (Moving: the spell lights, models 0x9000.., and the
    // lantern; Flash never).
    std::string ListDiffers()
    {
        const WxlLightV1* p = reinterpret_cast<const WxlLightV1*>(static_cast<uintptr_t>(0x10));
        const uint32_t n = li::LightList().Lights(&p);
        const std::vector<li::ActiveLight>& a = li::Active();
        if (n != a.size()) return F("%u published, %zu drawn", n, a.size());
        if (!n) return p ? "none, but the pointer isn't null" : "";
        if (!p) return "a null array";
        std::string out;
        for (uint32_t i = 0; i < n; ++i)
        {
            const li::ActiveLight& l = a[i];
            const WxlLightV1& q = *reinterpret_cast<const WxlLightV1*>(reinterpret_cast<const char*>(p) + static_cast<size_t>(i) * li::LightList().lightSize);
            const uint32_t want = (l.indoor ? kWxlLightIndoor : 0u) | (l.window ? kWxlLightWindow : 0u) | (l.own || SpellModel(l.model) ? kWxlLightMoving : 0u);
            if (std::memcmp(q.position, l.pos, sizeof(q.position)) || std::memcmp(q.colour, l.color, sizeof(q.colour)) || q.radius != l.radius ||
                q.falloff != l.falloff || q.flags != want)
                out += F(" light %u (flags %u, want %u)", i, q.flags, want);
        }
        return out;
    }

    void LightListForOthers()
    {
        std::printf("-- 5b. the light list other extensions read\n");
        WxlLightListV1& list = li::LightList();
        Check(std::strcmp(kWxlLightListName, "chromie.light-list") == 0 && kWxlLightListVersion == 1 && list.size == sizeof(WxlLightListV1) &&
              list.lightSize == sizeof(WxlLightV1) && sizeof(WxlLightV1) == 36 && list.Lights,
              F("\"%s\" v%u: %u bytes, a light every %u bytes, its Lights set", kWxlLightListName, kWxlLightListVersion, list.size, list.lightSize));
        li::Config() = li::Settings{};
        la::spelllights::Config().enabled = 1;
        ln::Config() = ln::Settings{};
        ln::ResetForTests();
        const float feet[3] = { 300.0f, 400.0f, 20.0f };
        const std::vector<li::OwnLight> own = LanternAt(feet, 0.0f, false, 0.0f);
        const float eye[3] = { feet[0] - 6.0f, feet[1] - 5.0f, feet[2] + 4.0f };
        li::Config().enabled = 0;
        li::UpdateForTests(eye, 1.0f / 60.0f, {}, {}, own);
        const WxlLightV1* p = nullptr;
        Check(list.Lights(&p) == 0 && !p && ListDiffers().empty(), "our lights off: none, the array null");
        li::Config().enabled = 1;

        // A night street: a torch, one inside a house (indoor), two a third of a yard apart (they merge: the list is after
        // merging), two lit windows, two spells cast, the lantern, a lightning strike.
        std::vector<li::FrameLight> scene, windows, spells;
        scene.push_back(Torch(feet[0] + 10.0f, feet[1], feet[2] + 2.0f, reinterpret_cast<const void*>(0x1000)));
        scene.push_back(Torch(feet[0] + 20.0f, feet[1] + 6.0f, feet[2] + 1.0f, reinterpret_cast<const void*>(0x1010)));
        scene.back().indoor = 1;
        scene.push_back(Torch(feet[0] - 8.0f, feet[1] + 9.0f, feet[2] + 2.0f, reinterpret_cast<const void*>(0x1020)));
        scene.push_back(Torch(feet[0] - 7.7f, feet[1] + 9.0f, feet[2] + 2.0f, reinterpret_cast<const void*>(0x1030)));
        for (int k = 0; k < 2; ++k)
        {
            li::FrameLight w = Torch(feet[0] + 4.0f + k * 6.0f, feet[1] + 14.0f, feet[2] + 3.0f, nullptr);
            w.color[0] = 0.9f; w.color[1] = 0.6f; w.color[2] = 0.3f; w.radius = 10.0f;
            windows.push_back(w);
            li::FrameLight s = Torch(feet[0] + 2.0f, feet[1] - 3.0f - k * 4.0f, feet[2] + 1.5f, reinterpret_cast<const void*>(static_cast<uintptr_t>(0x9000 + k * 16)));
            s.color[0] = 0.3f; s.color[1] = 0.5f; s.color[2] = 1.0f; s.radius = 7.0f;
            spells.push_back(s);
        }
        const float strike[3] = { feet[0] + 30.0f, feet[1] - 20.0f, feet[2] + 25.0f };
        const float white[3] = { 2.0f, 2.1f, 2.4f };
        li::AddTransient(strike, white, 60.0f);
        li::UpdateForTests(eye, 1.0f / 60.0f, spells, scene, own, windows);
        const uint32_t n = list.Lights(&p);
        unsigned indoor = 0, window = 0, moving = 0, flash = 0, strikes = 0;
        for (uint32_t i = 0; p && i < n; ++i)
        {
            indoor += (p[i].flags & kWxlLightIndoor) ? 1 : 0;
            window += (p[i].flags & kWxlLightWindow) ? 1 : 0;
            moving += (p[i].flags & kWxlLightMoving) ? 1 : 0;
            flash += (p[i].flags & kWxlLightFlash) ? 1 : 0;
            strikes += p[i].colour[2] == white[2] && p[i].radius == 60.0f && !p[i].flags ? 1 : 0;
        }
        const std::string diff = ListDiffers();
        Check(diff.empty() && n == 9, F("published as drawn: %u lights (4 torches, two merged, 2 windows, 2 spells, the lantern, a strike), "
                                         "each one's position, colour, radius, falloff and flags%s", n, diff.c_str()));
        Check(indoor == 1 && window == 2 && moving == 3 && flash == 0 && strikes == 1,
              F("flags: %u indoor (the house's torch), %u windows, %u moving (the spells and the lantern), the strike plain (%u), Flash on none (%u)",
                indoor, window, moving, strikes, flash));
        const WxlLightV1* again = nullptr;
        Check(list.Lights(&again) == n && again == p && list.Lights(nullptr) == n, "asked again before the next update: the same array (a null pointer only counts)");

        li::UpdateForTests(eye, 1.0f / 60.0f, spells, scene, own, windows);
        Check(ListDiffers().empty() && list.Lights(&p) == 8, F("the next frame: refilled, the strike gone (%u)", list.Lights(nullptr)));

        // The compare key: off with the rest (our lights' own switch), back on.
        static WXL_Api api = MakeApi();
        cmp::Install(&api);
        cmp::ResetForTests();
        cmp::Toggle("the test");
        li::UpdateForTests(eye, 1.0f / 60.0f, spells, scene, own, windows);
        Check(li::Config().enabled == 0 && list.Lights(&p) == 0 && !p, "the compare key off: none published");
        cmp::Toggle("the test");
        li::UpdateForTests(eye, 1.0f / 60.0f, spells, scene, own, windows);
        Check(li::Config().enabled == 1 && list.Lights(nullptr) == 8 && ListDiffers().empty(), F("on again: all back (%u)", list.Lights(nullptr)));
        cmp::ResetForTests();

        // Leaving the world: the loading screen's hook empties it at once, Update out of the world keeps it empty.
        li::OnWorldLeave();
        Check(list.Lights(&p) == 0 && !p, F("leaving the world (OnWorldLeave): none at once (%zu still drawn until the next update)", li::Active().size()));
        li::UpdateForTests(eye, 1.0f / 60.0f, spells, scene, own, windows);
        la::world::Snapshot out{};
        li::Update(1.0f / 60.0f, out);
        Check(!out.inWorld && list.Lights(&p) == 0 && !p && li::Active().empty(), "out of the world (Update): none, none drawn");
        la::spelllights::Config().enabled = 0;
        ln::ResetForTests();
    }

    // --- 6. WARP ------------------------------------------------------------------------------------------------------------
    template <class T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

    struct Warp
    {
        ID3D12Device* d12 = nullptr;
        IDirect3D9* d3d = nullptr;
        IDirect3DDevice9* dev = nullptr;
        ~Warp() { Release(dev); Release(d3d); Release(d12); }
    };

    bool InitWarp(Warp& w, std::string& why)
    {
        IDXGIFactory4* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory)))) { why = "no DXGI factory"; return false; }
        IDXGIAdapter* adapter = nullptr;
        const HRESULT ha = factory->EnumWarpAdapter(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&adapter));
        factory->Release();
        if (FAILED(ha) || !adapter) { why = "no WARP adapter"; return false; }
        const HRESULT hd = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&w.d12));
        adapter->Release();
        if (FAILED(hd) || !w.d12) { why = "no D3D12 device on WARP"; return false; }
        using Create9On12 = IDirect3D9*(WINAPI*)(UINT, D3D9ON12_ARGS*, UINT);
        HMODULE d9 = LoadLibraryW(L"d3d9.dll");
        auto create = d9 ? reinterpret_cast<Create9On12>(GetProcAddress(d9, "Direct3DCreate9On12")) : nullptr;
        if (!create) { why = "no Direct3DCreate9On12"; return false; }
        D3D9ON12_ARGS a{};
        a.Enable9On12 = TRUE;
        a.pD3D12Device = w.d12;
        w.d3d = create(D3D_SDK_VERSION, &a, 1);
        if (!w.d3d) { why = "D3D9On12 failed"; return false; }
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"LumenTests";
        RegisterClassW(&wc);
        HWND window = CreateWindowW(wc.lpszClassName, L"lumen", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window;
        pp.BackBufferWidth = 64; pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        if (FAILED(w.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &w.dev))) { why = "CreateDevice failed"; return false; }
        return true;
    }

    struct Vtx { float p[3]; float n[3]; float uv[2]; };

    // A quad of n x n vertices from `o` along `a` and `b` (yd), facing `normal`.
    void Sheet(std::vector<Vtx>& v, std::vector<uint16_t>& idx, const float o[3], const float a[3], const float b[3], const float normal[3], int n)
    {
        const uint16_t base = static_cast<uint16_t>(v.size());
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            {
                const float s = static_cast<float>(x) / (n - 1), t = static_cast<float>(y) / (n - 1);
                v.push_back({ { o[0] + a[0] * s + b[0] * t, o[1] + a[1] * s + b[1] * t, o[2] + a[2] * s + b[2] * t }, { normal[0], normal[1], normal[2] }, { 0, 0 } });
            }
        for (int y = 0; y + 1 < n; ++y)
            for (int x = 0; x + 1 < n; ++x)
            {
                const uint16_t p = static_cast<uint16_t>(base + y * n + x), q = static_cast<uint16_t>(p + 1), r = static_cast<uint16_t>(p + n), s = static_cast<uint16_t>(r + 1);
                idx.insert(idx.end(), { p, q, s, p, s, r });
            }
    }

    void Sphere(std::vector<Vtx>& v, std::vector<uint16_t>& idx, const float c[3], float r)
    {
        const int rings = 16, segs = 24;
        const uint16_t base = static_cast<uint16_t>(v.size());
        for (int i = 0; i <= rings; ++i)
            for (int j = 0; j <= segs; ++j)
            {
                const float th = 3.14159265f * i / rings, ph = 2 * 3.14159265f * j / segs;
                const float n[3] = { std::sin(th) * std::cos(ph), std::sin(th) * std::sin(ph), std::cos(th) };
                v.push_back({ { c[0] + r * n[0], c[1] + r * n[1], c[2] + r * n[2] }, { n[0], n[1], n[2] }, { 0, 0 } });
            }
        for (int i = 0; i < rings; ++i)
            for (int j = 0; j < segs; ++j)
            {
                const uint16_t a = static_cast<uint16_t>(base + i * (segs + 1) + j), b = static_cast<uint16_t>(a + 1), cc = static_cast<uint16_t>(a + segs + 1), d = static_cast<uint16_t>(cc + 1);
                idx.insert(idx.end(), { a, cc, d, a, d, b });
            }
    }

    // The camera: D3D's view (x right, y up, z ahead), row vectors: view = (world - camera) x V; P as the spell lights' test
    // keeps it (rows for column vectors), and R its row-vector form (as gfx::SceneMatrices hands the projection).
    struct Camera { float pos[3]; float V[16]; float P[16]; float R[16]; };
    Camera LookAt(const float eye[3], const float at[3], float fovY, float aspect, float zn, float zf)
    {
        Camera c{};
        std::memcpy(c.pos, eye, sizeof(c.pos));
        float f[3] = { at[0] - eye[0], at[1] - eye[1], at[2] - eye[2] };
        const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        for (float& x : f) x /= fl;
        const float up[3] = { 0, 0, 1 };
        float r[3] = { f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0] };
        const float rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        for (float& x : r) x /= rl;
        const float u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
        for (int i = 0; i < 3; ++i) { c.V[i * 4 + 0] = r[i]; c.V[i * 4 + 1] = u[i]; c.V[i * 4 + 2] = f[i]; c.V[i * 4 + 3] = 0.0f; }
        c.V[15] = 1.0f;
        const float ys = 1.0f / std::tan(fovY * 0.5f), xs = ys / aspect;
        const float P[16] = { xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zf - zn), -zn * zf / (zf - zn), 0, 0, 1, 0 };
        std::memcpy(c.P, P, sizeof(P));
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) c.R[i * 4 + j] = P[j * 4 + i];
        return c;
    }

    bool Project(const Camera& c, const float p[3], int w, int h, int& x, int& y)
    {
        const float rel[3] = { p[0] - c.pos[0], p[1] - c.pos[1], p[2] - c.pos[2] };
        float v[3];
        for (int k = 0; k < 3; ++k) v[k] = rel[0] * c.V[0 * 4 + k] + rel[1] * c.V[1 * 4 + k] + rel[2] * c.V[2 * 4 + k];
        if (v[2] <= 0.1f) return false;
        const float cx = v[0] * c.P[0] / v[2], cy = v[1] * c.P[5] / v[2];
        x = static_cast<int>((cx * 0.5f + 0.5f) * w);
        y = static_cast<int>((0.5f - cy * 0.5f) * h);
        return x >= 0 && y >= 0 && x < w && y < h;
    }

    struct Panel { std::string name; std::vector<uint32_t> px; };
    void Rgb(uint32_t p, float o[3]) { o[0] = ((p >> 16) & 0xFF) / 255.0f; o[1] = ((p >> 8) & 0xFF) / 255.0f; o[2] = (p & 0xFF) / 255.0f; }

    void WriteBmp(const fs::path& path, const std::vector<uint32_t>& image, int width, int height)
    {
        std::ofstream f(path, std::ios::binary);
        const uint32_t rowBytes = width * 3, pad = (4 - rowBytes % 4) % 4, size = 54 + (rowBytes + pad) * height;
        uint8_t h[54] = { 'B', 'M' };
        std::memcpy(h + 2, &size, 4);
        const uint32_t off = 54, dib = 40, planesBpp = 1 | (24u << 16), imgSize = (rowBytes + pad) * height;
        std::memcpy(h + 10, &off, 4); std::memcpy(h + 14, &dib, 4); std::memcpy(h + 18, &width, 4); std::memcpy(h + 22, &height, 4);
        std::memcpy(h + 26, &planesBpp, 4); std::memcpy(h + 34, &imgSize, 4);
        f.write(reinterpret_cast<const char*>(h), 54);
        const uint8_t zero[3] = {};
        for (int y = height - 1; y >= 0; --y)
        {
            for (int x = 0; x < width; ++x)
            {
                const uint32_t p = image[static_cast<size_t>(y) * width + x];
                const uint8_t bgr[3] = { static_cast<uint8_t>(p & 0xFF), static_cast<uint8_t>((p >> 8) & 0xFF), static_cast<uint8_t>((p >> 16) & 0xFF) };
                f.write(reinterpret_cast<const char*>(bgr), 3);
            }
            f.write(reinterpret_cast<const char*>(zero), pad);
        }
    }

    void Sheet(const fs::path& path, const std::vector<Panel>& panels, int w, int h, int cols)
    {
        const int rows = static_cast<int>((panels.size() + cols - 1) / cols);
        std::vector<uint32_t> img(static_cast<size_t>(cols) * w * rows * h, 0xFF202020u);
        for (size_t k = 0; k < panels.size(); ++k)
        {
            const int ox = static_cast<int>(k % cols) * w, oy = static_cast<int>(k / cols) * h;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    if (!panels[k].px.empty()) img[static_cast<size_t>(oy + y) * cols * w + ox + x] = panels[k].px[static_cast<size_t>(y) * w + x];
            Info(F("  panel %zu: %s", k + 1, panels[k].name.c_str()));
        }
        WriteBmp(path, img, cols * w, rows * h);
        Info("contact sheet: " + path.string());
    }

    // The lit M2 shader (vs_Diffuse_T1 as features/ModelLights patches it), as the spell lights' test draws with it.
    struct Renderer
    {
        Warp w;
        IDirect3DVertexShader9* vs = nullptr;
        IDirect3DPixelShader9* ps = nullptr;
        IDirect3DVertexDeclaration9* decl = nullptr;
        IDirect3DTexture9* white = nullptr;
        IDirect3DSurface9 *rt = nullptr, *ds = nullptr, *sys = nullptr;
        std::vector<Vtx> verts;
        std::vector<uint16_t> idx;
        int width = 320, height = 240;
        ~Renderer() { Release(sys); Release(ds); Release(rt); Release(white); Release(decl); Release(ps); Release(vs); }

        bool Targets(std::string& why)
        {
            if (FAILED(w.dev->CreateRenderTarget(width, height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, nullptr)) ||
                FAILED(w.dev->CreateDepthStencilSurface(width, height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &ds, nullptr)) ||
                FAILED(w.dev->CreateOffscreenPlainSurface(width, height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr)))
            { why = "targets"; return false; }
            return true;
        }

        bool Init(const fs::path& shaders, std::string& why)
        {
            if (!InitWarp(w, why)) return false;
            std::ifstream vf(shaders / "vs_Diffuse_T1_03.asm"), pf(shaders / "ps_Combiners_Opaque_00.asm");
            if (!vf || !pf) { why = "no vs_Diffuse_T1_03.asm / ps_Combiners_Opaque_00.asm in " + shaders.string(); return false; }
            std::string vsrc((std::istreambuf_iterator<char>(vf)), std::istreambuf_iterator<char>()), psrc((std::istreambuf_iterator<char>(pf)), std::istreambuf_iterator<char>());
            std::string editWhy;
            if (!ml::EditVertexForTests("Diffuse_T1", vsrc, editWhy)) { why = "the model-light edit: " + editWhy; return false; }
            std::vector<uint8_t> code;
            if (!la::shaderpatch::Assemble(vsrc, "lumen vs", code, why)) return false;
            if (FAILED(w.dev->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()), &vs))) { why = "CreateVertexShader"; return false; }
            if (!la::shaderpatch::Assemble(psrc, "lumen ps", code, why)) return false;
            if (FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps))) { why = "CreatePixelShader"; return false; }
            const D3DVERTEXELEMENT9 el[] = {
                { 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
                { 0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
                { 0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
                D3DDECL_END() };
            if (FAILED(w.dev->CreateVertexDeclaration(el, &decl))) { why = "vertex declaration"; return false; }
            if (FAILED(w.dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &white, nullptr))) { why = "texture"; return false; }
            D3DLOCKED_RECT lr;
            white->LockRect(0, &lr, nullptr, 0);
            *static_cast<uint32_t*>(lr.pBits) = 0xFFB0A090u;   // cave stone
            white->UnlockRect(0);
            return Targets(why);
        }

        std::vector<uint32_t> ReadBack()
        {
            std::vector<uint32_t> px;
            w.dev->GetRenderTargetData(rt, sys);
            D3DLOCKED_RECT lr;
            if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY)))
            {
                px.resize(static_cast<size_t>(width) * height);
                for (int y = 0; y < height; ++y) std::memcpy(&px[static_cast<size_t>(y) * width], static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch, width * 4);
                sys->UnlockRect();
            }
            return px;
        }

        // The cave: no sun, a faint ambient (`ambient`; noon outdoors: the sun `sun`).
        Panel Draw(const std::string& name, const Camera& cam, float ambient, float sun)
        {
            Panel out{ name, {} };
            IDirect3DDevice9* d = w.dev;
            ml::PrepareForTests(d, cam.V, cam.pos);
            d->SetRenderTarget(0, rt);
            d->SetDepthStencilSurface(ds);
            d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000u, 1.0f, 0);
            d->SetRenderState(D3DRS_ZENABLE, TRUE);
            d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
            d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            d->SetVertexDeclaration(decl);
            d->SetVertexShader(vs);
            d->SetPixelShader(ps);
            d->SetTexture(0, white);
            float c[34 * 4] = {};
            auto set = [&](int r, float x, float y, float z, float ww) { c[r * 4] = x; c[r * 4 + 1] = y; c[r * 4 + 2] = z; c[r * 4 + 3] = ww; };
            for (int r = 0; r < 4; ++r) set(2 + r, cam.P[r * 4], cam.P[r * 4 + 1], cam.P[r * 4 + 2], cam.P[r * 4 + 3]);
            set(6, 1, 0, 0, 0); set(7, 0, 1, 0, 0);
            set(10, sun, sun, sun * 0.95f, 0); set(11, ambient, ambient, ambient * 1.15f, 0);
            const float sunW[3] = { 0.3f, -0.4f, 0.87f };
            float sunV[3];
            for (int k = 0; k < 3; ++k) sunV[k] = sunW[0] * cam.V[k] + sunW[1] * cam.V[4 + k] + sunW[2] * cam.V[8 + k];
            set(12, -sunV[0], -sunV[1], -sunV[2], 0);
            set(25, 1, 0, 0, 0); set(26, 0, 0, 0, 0); set(27, 0, 0, 0, 0);
            set(28, 1, 1, 1, 1); set(29, 0, 0, 0, 0); set(30, 0, 1, 1, 0);
            for (int k = 0; k < 3; ++k)
                set(31 + k, cam.V[0 * 4 + k], cam.V[1 * 4 + k], cam.V[2 * 4 + k],
                    -(cam.pos[0] * cam.V[0 * 4 + k] + cam.pos[1] * cam.V[1 * 4 + k] + cam.pos[2] * cam.V[2 * 4 + k]));
            d->SetVertexShaderConstantF(0, c, 34);
            const float fogColour[4] = { 0, 0, 0, 0 };
            d->SetPixelShaderConstantF(2, fogColour, 1);
            d->BeginScene();
            d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, static_cast<UINT>(verts.size()), static_cast<UINT>(idx.size() / 3), idx.data(), D3DFMT_INDEX16, verts.data(), sizeof(Vtx));
            d->EndScene();
            out.px = ReadBack();
            return out;
        }
    };

    // The cave round the player (at the origin, facing +x): a floor 24 yd across, its walls, a low rock ceiling; the
    // player a body and a head.
    void Cave(Renderer& r)
    {
        const float h = 12.0f, top = 4.5f;
        auto sheet = [&](float ox, float oy, float oz, float ax, float ay, float az, float bx, float by, float bz, float nx, float ny, float nz)
        {
            const float o[3] = { ox, oy, oz }, a[3] = { ax, ay, az }, b[3] = { bx, by, bz }, n[3] = { nx, ny, nz };
            Sheet(r.verts, r.idx, o, a, b, n, 33);
        };
        sheet(-h, -h, 0, 2 * h, 0, 0, 0, 2 * h, 0, 0, 0, 1);         // floor
        sheet(-h, -h, top, 2 * h, 0, 0, 0, 2 * h, 0, 0, 0, -1);      // ceiling
        sheet(-h, h, 0, 2 * h, 0, 0, 0, 0, top, 0, -1, 0);           // far wall (+y)
        sheet(h, -h, 0, 0, 2 * h, 0, 0, 0, top, -1, 0, 0);           // the wall ahead (+x)
        // (a waist narrower than the lantern's 0.45 yd to the side, as a character's: the lantern hangs outside the body)
        const float body[3] = { 0.0f, 0.0f, 0.95f }, head[3] = { 0.0f, 0.0f, 1.7f };
        Sphere(r.verts, r.idx, body, 0.33f);
        Sphere(r.verts, r.idx, head, 0.25f);
    }

    struct WarpResults { std::vector<Panel> cave; Panel caveOff; bool caveRan = false; };

    void LanternInACave(Renderer& r, const fs::path& sheetFolder, WarpResults& res)
    {
        std::printf("-- 6a. on WARP: the lantern in a dark cave\n");
        li::Config() = li::Settings{};
        ln::Config() = ln::Settings{};
        ln::ResetForTests();
        const float eye[3] = { -7.0f, -6.0f, 3.6f }, at[3] = { 1.0f, 0.5f, 0.6f }, feet[3] = { 0.0f, 0.0f, 0.0f };
        const Camera cam = LookAt(eye, at, 0.95f, static_cast<float>(r.width) / r.height, 0.5f, 200.0f);
        const float caveAmbient = 0.03f;
        // Without the lantern.
        li::UpdateForTests(cam.pos, 1.0f / 60.0f, {}, {}, {});
        res.cave.push_back(r.Draw("a dark cave, no lantern", cam, caveAmbient, 0.0f));
        // With it: the cave is indoors (the client's verdict), the light round the player none.
        std::vector<li::OwnLight> own = LanternAt(feet, 1.0f, true, 0.0f);
        Check(own.size() == 1, "in the cave: the lantern lit by itself");
        li::UpdateForTests(cam.pos, 1.0f / 60.0f, {}, {}, own);
        res.cave.push_back(r.Draw("the lantern lit (by itself: a cave)", cam, caveAmbient, 0.0f));
        // A torch on the wall ahead, 8 yd off.
        std::vector<li::FrameLight> scene = { Torch(7.0f, 3.0f, 2.2f, reinterpret_cast<const void*>(0x5000)) };
        li::UpdateForTests(cam.pos, 1.0f / 60.0f, {}, scene, own);
        res.cave.push_back(r.Draw("the lantern and a torch 8 yd ahead", cam, caveAmbient, 0.0f));
        li::UpdateForTests(cam.pos, 1.0f / 60.0f, {}, scene, {});
        res.cave.push_back(r.Draw("the torch alone", cam, caveAmbient, 0.0f));
        // Noon outdoors, lit by the key: nothing.
        ln::ResetForTests();
        for (int f = 0; f < 10; ++f) ln::StepForTests(1.0f / 30.0f, feet, 0.0f, 1.0f, false, 0.0f, 0.0f);
        ln::PressForTests();
        std::vector<li::OwnLight> noon = LanternAt(feet, 1.0f, false, 0.0f);
        li::UpdateForTests(cam.pos, 1.0f / 60.0f, {}, {}, noon);
        res.cave.push_back(r.Draw("noon outdoors, lit by the key (gives nothing)", cam, 0.35f, 0.8f));
        li::UpdateForTests(cam.pos, 1.0f / 60.0f, {}, {}, {});
        res.cave.push_back(r.Draw("noon outdoors, no lantern", cam, 0.35f, 0.8f));
        Check(noon.empty(), "noon outdoors, lit by the key: no light");
        res.caveRan = true;

        const Panel& dark = res.cave[0];
        const Panel& lit = res.cave[1];
        const Panel& both = res.cave[2];
        const Panel& torch = res.cave[3];
        int fx = 0, fy = 0, wx = 0, wy = 0, gx = 0, gy = 0;
        const float floorNear[3] = { 1.0f, -1.5f, 0.0f }, farFloor[3] = { 10.0f, 10.0f, 0.0f }, farWall[3] = { 3.0f, 11.99f, 2.0f };
        const bool pn = Project(cam, floorNear, r.width, r.height, fx, fy), pf = Project(cam, farFloor, r.width, r.height, wx, wy), pw = Project(cam, farWall, r.width, r.height, gx, gy);
        if (!pn || !pf || !pw) { Check(false, "the cave's probes on the screen"); return; }
        float d0[3], d1[3], f0[3], f1[3];
        Rgb(dark.px[fy * r.width + fx], d0);
        Rgb(lit.px[fy * r.width + fx], d1);
        Rgb(dark.px[wy * r.width + wx], f0);
        Rgb(lit.px[wy * r.width + wx], f1);
        const float raise[3] = { d1[0] - d0[0], d1[1] - d0[1], d1[2] - d0[2] };
        Check(MaxChannel(d0) < 0.06f && raise[0] > 0.12f && raise[0] > raise[1] && raise[1] > raise[2],
              F("the floor beside the player: %.2f dark, +%.2f %.2f %.2f with the lantern (warm)", MaxChannel(d0), raise[0], raise[1], raise[2]));
        Check(std::fabs(MaxChannel(f1) - MaxChannel(f0)) < 0.02f, F("the floor 14 yd away as it was (%.2f, %.2f): it reaches 8 yd", MaxChannel(f0), MaxChannel(f1)));
        unsigned white = 0, litPx = 0;
        float most = 0.0f;
        for (size_t k = 0; k < both.px.size(); ++k)
        {
            float v[3], b[3];
            Rgb(both.px[k], v);
            Rgb(torch.px[k], b);
            most = std::max(most, MaxChannel(v));
            if (v[0] > 0.98f && v[1] > 0.98f && v[2] > 0.98f) ++white;
            if (MaxChannel(v) > MaxChannel(b) + 0.03f) ++litPx;
        }
        Check(white == 0 && litPx > both.px.size() / 30, F("with a torch near: the lantern still adds round you (%u pixels), nothing blown out to white (the brightest channel %.2f)", litPx, most));
        float n0[3], n1[3];
        Rgb(res.cave[4].px[fy * r.width + fx], n0);
        Rgb(res.cave[5].px[fy * r.width + fx], n1);
        Check(res.cave[4].px == res.cave[5].px, F("noon outdoors: lit by the key, the same pixels as without (%.2f)", MaxChannel(n0)));
        (void)n1;
        if (!sheetFolder.empty()) Sheet(sheetFolder / "lumen_lantern.bmp", res.cave, r.width, r.height, 3);
    }

    // --- 6b. a town's glow from 600 yd --------------------------------------------------------------------------------------
    // A night landscape with depth: the ground round the town and up to the camera, the town's houses as boxes, and a hill
    // between the camera and the town's west half. Its own little shaders (the world's aren't what's tested here).
    const char* const kGroundVs = R"(
float4 r0 : register(c0); float4 r1 : register(c1); float4 r2 : register(c2); float4 r3 : register(c3);
struct Out { float4 p : POSITION; float4 c : COLOR0; };
Out main(float3 p : POSITION, float4 c : COLOR0)
{
    Out o;
    o.p = p.x * r0 + p.y * r1 + p.z * r2 + r3;
    o.c = c;
    return o;
}
)";
    const char* const kGroundPs = R"(
float4 main(float4 c : COLOR0) : COLOR { return c; }
)";

    struct GroundVtx { float p[3]; uint32_t c; };

    struct Night
    {
        Renderer* r = nullptr;
        IDirect3DVertexShader9* vs = nullptr;
        IDirect3DPixelShader9* ps = nullptr;
        IDirect3DVertexDeclaration9* decl = nullptr;
        std::vector<GroundVtx> verts;
        std::vector<uint16_t> idx;
        ~Night() { Release(decl); Release(ps); Release(vs); }

        bool Init(Renderer& renderer, std::string& why)
        {
            r = &renderer;
            HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
            const auto compile = m ? reinterpret_cast<pD3DCompile>(GetProcAddress(m, "D3DCompile")) : nullptr;
            if (!compile) { why = "no d3dcompiler_47"; return false; }
            ID3DBlob* code = nullptr;
            ID3DBlob* err = nullptr;
            if (FAILED(compile(kGroundVs, std::strlen(kGroundVs), nullptr, nullptr, nullptr, "main", "vs_3_0", 0, 0, &code, &err)) || !code ||
                FAILED(r->w.dev->CreateVertexShader(static_cast<const DWORD*>(code->GetBufferPointer()), &vs)))
            { why = err ? static_cast<const char*>(err->GetBufferPointer()) : "ground vs"; return false; }
            code->Release();
            if (FAILED(compile(kGroundPs, std::strlen(kGroundPs), nullptr, nullptr, nullptr, "main", "ps_3_0", 0, 0, &code, &err)) || !code ||
                FAILED(r->w.dev->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()), &ps)))
            { why = err ? static_cast<const char*>(err->GetBufferPointer()) : "ground ps"; return false; }
            code->Release();
            const D3DVERTEXELEMENT9 el[] = { { 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
                                             { 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 }, D3DDECL_END() };
            return SUCCEEDED(r->w.dev->CreateVertexDeclaration(el, &decl));
        }

        // Positions camera-relative (the scene view's translation 0, as the client's).
        void Quad(const float a[3], const float b[3], const float c[3], const float d[3], uint32_t colour, const float cam[3])
        {
            const uint16_t base = static_cast<uint16_t>(verts.size());
            for (const float* p : { a, b, c, d }) verts.push_back({ { p[0] - cam[0], p[1] - cam[1], p[2] - cam[2] }, colour });
            idx.insert(idx.end(), { base, static_cast<uint16_t>(base + 1), static_cast<uint16_t>(base + 2), base, static_cast<uint16_t>(base + 2), static_cast<uint16_t>(base + 3) });
        }
        void Box(const float lo[3], const float hi[3], uint32_t colour, const float cam[3])
        {
            const float p[8][3] = { { lo[0], lo[1], lo[2] }, { hi[0], lo[1], lo[2] }, { hi[0], hi[1], lo[2] }, { lo[0], hi[1], lo[2] },
                                    { lo[0], lo[1], hi[2] }, { hi[0], lo[1], hi[2] }, { hi[0], hi[1], hi[2] }, { lo[0], hi[1], hi[2] } };
            const int f[6][4] = { { 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 }, { 1, 2, 6, 5 }, { 0, 3, 7, 4 } };
            for (const auto& q : f) Quad(p[q[0]], p[q[1]], p[q[2]], p[q[3]], colour, cam);
        }

        Panel Draw(const std::string& name, const Camera& cam, float fogStart, float fogEnd, bool glows)
        {
            Panel out{ name, {} };
            IDirect3DDevice9* d = r->w.dev;
            d->SetRenderTarget(0, r->rt);
            d->SetDepthStencilSurface(r->ds);
            d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF0A0E1Cu, 1.0f, 0);   // the night sky
            d->SetRenderState(D3DRS_ZENABLE, TRUE);
            d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
            d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
            d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            d->SetVertexDeclaration(decl);
            d->SetVertexShader(vs);
            d->SetPixelShader(ps);
            // view x projection, as rows for the shader (camera-relative: the view's translation 0).
            float M[16];
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                {
                    float s = 0.0f;
                    for (int k = 0; k < 4; ++k) s += (i < 3 && k < 3 ? cam.V[i * 4 + k] : (i == 3 && k == 3 ? 1.0f : 0.0f)) * cam.R[k * 4 + j];
                    M[i * 4 + j] = s;
                }
            d->SetVertexShaderConstantF(0, M, 4);
            d->BeginScene();
            d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, static_cast<UINT>(verts.size()), static_cast<UINT>(idx.size() / 3), idx.data(), D3DFMT_INDEX16, verts.data(), sizeof(GroundVtx));
            if (glows)
            {
                float view[16] = {};
                for (int i = 0; i < 3; ++i) for (int k = 0; k < 4; ++k) view[i * 4 + k] = k < 3 ? cam.V[i * 4 + k] : 0.0f;
                view[15] = 1.0f;
                const D3DVIEWPORT9 vp = { 0, 0, static_cast<DWORD>(r->width), static_cast<DWORD>(r->height), 0.0f, 1.0f };
                d->SetViewport(&vp);
                ds::DrawForTests(d, view, cam.R, cam.pos, fogStart, fogEnd);
            }
            d->EndScene();
            out.px = r->ReadBack();
            return out;
        }
    };

    struct TownScene { std::vector<Panel> panels; bool ran = false; };

    void TownFromAfar(Renderer& r, const fs::path& sheetFolder, TownScene& res)
    {
        std::printf("-- 6b. on WARP: a town's glow from 600 yd at night\n");
        if (!g_townWindows) { Check(false, "no town (the tables didn't load)"); return; }
        Night n;
        std::string why;
        if (!n.Init(r, why)) { Check(false, "the night scene's shaders: " + why); return; }
        float eye[3];
        TownEye(eye);
        const float at[3] = { g_town[0], g_town[1], g_town[2] - 4.0f };
        const Camera cam = LookAt(eye, at, 0.45f, static_cast<float>(r.width) / r.height, 0.3f, 1200.0f);
        const float groundZ = g_town[2] - 9.0f;
        // The ground: the valley round the town, and the slope up to the camera.
        const float g0[3] = { g_town[0] - 600.0f, g_town[1] - 80.0f, groundZ }, g1[3] = { g_town[0] + 600.0f, g_town[1] - 80.0f, groundZ },
                    g2[3] = { g_town[0] + 600.0f, g_town[1] + 900.0f, groundZ }, g3[3] = { g_town[0] - 600.0f, g_town[1] + 900.0f, groundZ };
        n.Quad(g0, g1, g2, g3, 0xFF0C140Cu, cam.pos);
        const float s0[3] = { g_town[0] - 600.0f, eye[1] - 50.0f, eye[2] - 6.0f }, s1[3] = { g_town[0] + 600.0f, eye[1] - 50.0f, eye[2] - 6.0f };
        n.Quad(s0, s1, g1, g0, 0xFF0B120Bu, cam.pos);
        // The houses: boxes round each window a little behind its glass (their walls must not hide their own windows).
        std::vector<const la::windows::Window*> ws;
        la::windows::Near(0, g_town, 140.0f, true, 1000, ws);
        for (const la::windows::Window* w : ws)
        {
            const float mid[3] = { w->pos[0] - w->facing[0] * w->back, w->pos[1] - w->facing[1] * w->back, w->pos[2] - w->facing[2] * w->back };
            const float c[3] = { mid[0] - w->facing[0] * 3.0f, mid[1] - w->facing[1] * 3.0f, mid[2] };
            const float lo[3] = { c[0] - 2.9f, c[1] - 2.9f, groundZ }, hi[3] = { c[0] + 2.9f, c[1] + 2.9f, mid[2] + 3.0f };
            n.Box(lo, hi, 0xFF15151Au, cam.pos);
        }
        // A hill between the camera and the town's west half.
        const float hl[3] = { g_town[0] - 160.0f, g_town[1] - 300.0f, groundZ }, hh[3] = { g_town[0] - 8.0f, g_town[1] - 260.0f, g_town[2] + 30.0f };
        n.Box(hl, hh, 0xFF0E180Eu, cam.pos);

        ds::Config() = dl::Settings{};
        ds::BuildForTests(eye, 0, 1.0f, TownLamps());
        const unsigned built = static_cast<unsigned>(ds::Sprites().size());
        res.panels.push_back(n.Draw("Lakeshire from 600 yd at night, no distant lights", cam, 0.0f, 0.0f, false));
        res.panels.push_back(n.Draw("with the distant lights (the clear night's fog: 122 .. 490 yd)", cam, 122.0f, 490.0f, true));
        const unsigned drawn = ds::GetStats().drawn;
        res.panels.push_back(n.Draw("in the weather's fog (ends at 278 yd)", cam, -139.0f, 278.0f, true));
        res.panels.push_back(n.Draw("no fog", cam, 0.0f, 0.0f, true));
        res.ran = true;

        const Panel& off = res.panels[0];
        const Panel& on = res.panels[1];
        const Panel& fog = res.panels[2];
        // Each glow's centre: lit on the screen where nothing stands in front, as dark as before where the hill does.
        unsigned seen = 0, hidden = 0, shownBehindHill = 0, behindHill = 0;
        float warmest = 0.0f, onSum = 0.0f, fogSum = 0.0f;
        for (const dl::Sprite& s : ds::Sprites())
        {
            int x = 0, y = 0;
            if (!Project(cam, s.pos, r.width, r.height, x, y)) continue;
            float a[3], b[3], f[3];
            Rgb(on.px[y * r.width + x], a);
            Rgb(off.px[y * r.width + x], b);
            Rgb(fog.px[y * r.width + x], f);
            const bool westHalf = s.pos[0] < g_town[0] - 8.0f;
            // Behind the hill: the ray to it passes the hill's box (it stands 260..300 yd short of the town, up to 30 yd high).
            const float t = (hh[1] - eye[1]) / (s.pos[1] - eye[1]);
            const float zAt = eye[2] + (s.pos[2] - eye[2]) * t, xAt = eye[0] + (s.pos[0] - eye[0]) * t;
            const bool hill = westHalf && zAt < hh[2] && xAt > hl[0] && xAt < hh[0];
            const float raise = a[0] - b[0];
            if (hill) { ++behindHill; if (raise > 0.03f) ++shownBehindHill; }
            else if (raise > 0.08f && a[0] > a[2]) { ++seen; warmest = std::max(warmest, raise); onSum += a[0]; fogSum += f[0]; }
            else ++hidden;
        }
        Info(F("%u glows built, %u drawn; of those on the screen %u lit where seen, %u not (in front of a house or out of the frame), %u behind the hill", built, drawn, seen, hidden, behindHill));
        Check(seen >= 8 && warmest > 0.25f, F("Lakeshire's windows and lamps glow from 600 yd at night: %u glows seen, the brightest +%.2f, warm", seen, warmest));
        Check(behindHill > 0 && shownBehindHill == 0, F("the hill in front hides the town's west half: %u behind it, none shows", behindHill));
        Check(fogSum < onSum, F("in the weather's thicker fog they're dimmer (%.2f vs %.2f summed)", fogSum, onSum));
        if (!sheetFolder.empty()) Sheet(sheetFolder / "lumen_distant.bmp", res.panels, r.width, r.height, 2);
    }

    // --- 6c. the compare key's off/on pair ---------------------------------------------------------------------------------
    void ComparePair(Renderer& r, const fs::path& shaders, const fs::path& sheetFolder)
    {
        std::printf("-- 6c. on WARP: the compare key's off/on pair\n");
        (void)shaders;
        static WXL_Api api = MakeApi();
        cmp::Install(&api);
        cmp::ResetForTests();
        li::Config() = li::Settings{};
        ln::Config() = ln::Settings{};
        ds::Config() = dl::Settings{};
        Night n;
        std::string why;
        const bool night = n.Init(r, why);
        const float feet[3] = { 0.0f, 0.0f, 0.0f };
        const float eye[3] = { -7.0f, -6.0f, 3.6f }, at[3] = { 1.0f, 0.5f, 0.6f };
        const Camera cave = LookAt(eye, at, 0.95f, static_cast<float>(r.width) / r.height, 0.5f, 200.0f);
        float townEye[3];
        TownEye(townEye);
        const float townAt[3] = { g_town[0], g_town[1], g_town[2] - 4.0f };
        const Camera town = LookAt(townEye, townAt, 0.6f, static_cast<float>(r.width) / r.height, 0.3f, 1200.0f);
        if (night)
        {
            const float groundZ = g_town[2] - 9.0f;
            const float g0[3] = { g_town[0] - 600.0f, g_town[1] - 80.0f, groundZ }, g1[3] = { g_town[0] + 600.0f, g_town[1] - 80.0f, groundZ },
                        g2[3] = { g_town[0] + 600.0f, g_town[1] + 900.0f, groundZ }, g3[3] = { g_town[0] - 600.0f, g_town[1] + 900.0f, groundZ };
            n.Quad(g0, g1, g2, g3, 0xFF0C140Cu, town.pos);
        }
        // One frame of the game, as OnUpdate runs it for these: the lantern, our lights with it, the distant glows; then drawn.
        // (dt 0 for our lights: the lantern's flicker at the same moment each time, so on and on again can match exactly.)
        auto frame = [&](const std::string& name, std::vector<Panel>& out)
        {
            ln::ResetForTests();
            std::vector<li::OwnLight> own = LanternAt(feet, 1.0f, true, 0.0f);
            li::UpdateForTests(cave.pos, 0.0f, {}, {}, own);
            out.push_back(r.Draw(name + ": the cave", cave, 0.03f, 0.0f));
            ds::BuildForTests(townEye, 0, 1.0f, TownLamps());
            if (night) out.push_back(n.Draw(name + ": the town from 600 yd", town, 122.0f, 490.0f, true));
        };
        std::vector<Panel> panels;
        frame("effects on", panels);
        g_chat.clear();
        cmp::Toggle("the key");
        const std::string offLine = g_chat.empty() ? std::string() : g_chat.back();
        frame("off by the compare key", panels);
        cmp::Toggle("the key");
        const std::string onLine = g_chat.empty() ? std::string() : g_chat.back();
        frame("on again", panels);
        const size_t per = night ? 2 : 1;
        bool differs = true, same = true;
        for (size_t k = 0; k < per; ++k)
        {
            differs = differs && panels[k].px != panels[per + k].px;
            same = same && panels[k].px == panels[2 * per + k].px;
        }
        Check(differs, "off by the key: the cave without the lantern's light, the town without its glows");
        Check(same, "on again: the same pixels as before it was pressed");
        Check(Has(offLine, "all effects OFF") && Has(onLine, "back ON"), "the chat lines: \"" + offLine + "\", \"" + onLine + "\"");
        // Off: the cave as dark as the client's own (no light of ours on it), the town's glows gone.
        float lit[3], dark[3];
        int fx = 0, fy = 0;
        const float floorNear[3] = { 1.0f, -1.5f, 0.0f };
        Project(cave, floorNear, r.width, r.height, fx, fy);
        Rgb(panels[0].px[fy * r.width + fx], lit);
        Rgb(panels[per].px[fy * r.width + fx], dark);
        Check(MaxChannel(dark) < 0.06f && MaxChannel(lit) > MaxChannel(dark) + 0.2f, F("the floor beside the player: %.2f on, %.2f off", MaxChannel(lit), MaxChannel(dark)));
        if (!sheetFolder.empty()) Sheet(sheetFolder / "lumen_compare.bmp", panels, r.width, r.height, static_cast<int>(per));
        cmp::ResetForTests();
    }

    // --- the distant glow's pixel shader: its slots ------------------------------------------------------------------------
    void ShaderSlots()
    {
        std::printf("-- the distant glow's pixel shader\n");
        HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
        const auto compile = m ? reinterpret_cast<pD3DCompile>(GetProcAddress(m, "D3DCompile")) : nullptr;
        const auto disassemble = m ? reinterpret_cast<pD3DDisassemble>(GetProcAddress(m, "D3DDisassemble")) : nullptr;
        if (!compile || !disassemble) { Check(false, "d3dcompiler_47"); return; }
        const char* src = ds::PixelShaderSource();
        ID3DBlob* code = nullptr;
        ID3DBlob* err = nullptr;
        const HRESULT hr = compile(src, std::strlen(src), nullptr, nullptr, nullptr, "main", "ps_2_0", 0, 0, &code, &err);
        if (FAILED(hr) || !code) { Check(false, std::string("ps_2_0 compile: ") + (err ? static_cast<const char*>(err->GetBufferPointer()) : "?")); return; }
        ID3DBlob* text = nullptr;
        disassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr, &text);
        const std::string asmText = text ? std::string(static_cast<const char*>(text->GetBufferPointer()), text->GetBufferSize()) : std::string();
        const size_t at = asmText.find("approximately ");
        const int slots = at != std::string::npos ? std::atoi(asmText.c_str() + at + 14) : -1;
        Check(slots > 0 && slots <= 64, F("it compiles as ps_2_0: %d instruction slots (ps_2_0 allows 64 arithmetic)", slots));
        if (text) text->Release();
        code->Release();
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> pos;
    bool warp = true;
    fs::path sheet;
    for (int k = 1; k < argc; ++k)
    {
        const std::string a = argv[k];
        if (a == "--no-warp") warp = false;
        else if (a == "--sheet" && k + 1 < argc) sheet = argv[++k];
        else pos.push_back(a);
    }
    if (pos.size() < 2)
    {
        std::printf("usage: lumen_tests <M2 shader folder> <repo data folder> [--no-warp] [--sheet <folder>]\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    la::chat::SetCaptureForTests(&Capture);
    la::spelllights::SetIndoorTest(nullptr);
    LanternRules();
    LanternInLights();
    DistantRules();
    TablesThroughTheReader(pos[1]);
    CompareKey();
    LightListForOthers();
    ShaderSlots();
    if (warp)
    {
        Renderer r;
        std::string why;
        if (!r.Init(pos[0], why)) Check(false, "WARP: " + why);
        else
        {
            Cave(r);
            WarpResults res;
            LanternInACave(r, sheet, res);
            Renderer wide;   // the town at 640 x 360
            wide.width = 640;
            wide.height = 360;
            TownScene town;
            if (!wide.Init(pos[0], why)) Check(false, "WARP (640 x 360): " + why);
            else TownFromAfar(wide, sheet, town);
            ComparePair(r, pos[0], sheet);
        }
    }
    else std::printf("(--no-warp: the WARP checks left out)\n");
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
