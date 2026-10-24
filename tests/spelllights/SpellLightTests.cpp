// Spell lights (env/SpellLightModel, features/SpellLights, their way through env/Lights and features/ModelLights).
//  1. The rules: which models are spell visuals (the real SpellVisualEffectName and ItemVisualEffects tables through the
//     loader the game uses), a model's colour (its own light, its particles, its geometry, its name), its light's size,
//     the fade, the merge and the cap.
//  2. The client side over a fake client: the real tracker (spelllights::FrameForTests) walking a fake M2 scene and
//     missile lists laid out as the client's (the offsets features/SpellLights reads), whose models carry real spell
//     files (extracted, their arrays made pointers as the client's loader does) and whose textures are read through the
//     same reader the game uses; then env/Lights' merge and grid (UpdateForTests) on its lights.
//  3. The guards against the client's exe files (Wow.exe, Wow-WarcraftXL.exe).
//  4. Cost: a busy fake scene (a city's 20,000 models, a raid's 300 spell visuals) per frame.
//  5. On WARP (the CPU rasterizer behind D3D9On12, no GPU): a floor and a sphere drawn with the stock lit M2 vertex shader
//     as features/ModelLights patches it, its light texture filled by ModelLights from env/Lights' grid, at night, lit
//     by fire, frost, holy, arcane, fel and an area spell's field; a contact sheet (--sheet).
//  6. Flicker: our light on the floor at one spot, frame by frame, for steady effects (their particles born and dying as
//     their files say), effects among torches and in a crowd over the cap, and a missile passing (--traces <folder>: CSV).
//  7. Objects that aren't spells: a tracked herb node's loot sparkle, chests, ore, portals, breath, blood, level-up.
// Run: spelllights_tests <spell data folder (m2\, blp\, dbc\)> <M2 shader folder> [<Wow.exe> [<Wow-WarcraftXL.exe>]]
//      [--no-warp] [--sheet <bmp>] [--traces <folder>]
#include "env/Lights.hpp"
#include "env/SpellLightModel.hpp"
#include "features/ModelLights.hpp"
#include "features/SpellLights.hpp"
#include "render/BlpColour.hpp"
#include "render/ShaderPatch.hpp"

#include <windows.h>
#include <d3d9.h>
#include <d3d9on12.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace sl = wxl_livingazeroth::spelllight;
namespace sp = wxl_livingazeroth::spelllights;
namespace li = wxl_livingazeroth::lights;
namespace ml = wxl_livingazeroth::modellights;
namespace fs = std::filesystem;

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
        char b[1024];
        va_list a;
        va_start(a, format);
        std::vsnprintf(b, sizeof(b), format, a);
        va_end(a);
        return b;
    }

    fs::path g_data;

    bool ReadAll(const fs::path& p, std::vector<uint8_t>& out)
    {
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }

    // The game's file reader's stand-in: the extracted tables (dbc\<name>) and files (blp\, m2\: '\' as '#').
    bool ReadData(const char* path, std::vector<uint8_t>& out)
    {
        std::string p = path;
        const std::string db = "DBFilesClient\\";
        if (p.compare(0, db.size(), db) == 0) return ReadAll(g_data / "dbc" / p.substr(db.size()), out);
        std::replace(p.begin(), p.end(), '\\', '#');
        const std::string low = sl::NormalizePath(path);
        if (low.size() > 4 && low.compare(low.size() - 4, 4, ".blp") == 0) return ReadAll(g_data / "blp" / p, out);
        return ReadAll(g_data / "m2" / p, out);
    }

    // --- 1. the rules ------------------------------------------------------------------------------------------------------
    void TestRules()
    {
        std::printf("-- the rules\n");
        Check(sl::NormalizePath("Spells/Fireball_Missile_High.MDX") == "spells\\fireball_missile_high.m2", "a path normalized as the client keeps it (lower case, \\, .m2)");

        sl::VisualSets sets;
        std::vector<uint8_t> bytes;
        const bool haveSpells = ReadAll(g_data / "dbc" / "SpellVisualEffectName.dbc", bytes);
        sets.spellRows = haveSpells ? sl::LoadVisualTable(bytes.data(), bytes.size(), 2, sets.spells) : -1;
        if (haveSpells) sl::LoadVisualTable(bytes.data(), bytes.size(), 2, sets.hardcoded, 1, "HARDCODED");
        const bool haveItems = ReadAll(g_data / "dbc" / "ItemVisualEffects.dbc", bytes);
        sets.itemRows = haveItems ? sl::LoadVisualTable(bytes.data(), bytes.size(), 1, sets.items) : -1;
        const bool haveObjects = ReadAll(g_data / "dbc" / "GameObjectDisplayInfo.dbc", bytes);
        sets.objectRows = haveObjects ? sl::LoadVisualTable(bytes.data(), bytes.size(), 1, sets.objects) : -1;
        Check(sets.spellRows == 3965 && sets.spells.size() > 1700, F("SpellVisualEffectName through the loader: %d rows, %zu model files", sets.spellRows, sets.spells.size()));
        Check(sets.itemRows == 93 && sets.items.size() > 80, F("ItemVisualEffects: %d rows, %zu model files", sets.itemRows, sets.items.size()));
        Check(sets.objectRows == 3790 && sets.objects.size() > 3500, F("GameObjectDisplayInfo: %d rows, %zu model files (game objects' own models)", sets.objectRows, sets.objects.size()));
        Check(sets.hardcoded.size() == 16 && sets.hardcoded.count("particles\\lootfx.m2") && sets.hardcoded.count("particles\\coldbreath.m2") &&
              sets.hardcoded.count("spells\\levelup\\levelup.m2"),
              F("the client's own effects (rows named HARDCODED): %zu files, the loot sparkle, breath and level-up among them", sets.hardcoded.size()));
        Check(sets.spells.count("spells\\fireball_missile_high.m2") && sets.spells.count("particles\\frostbolt_missle.m2"), "Fireball's and the old Frostbolt's missiles are in it");
        unsigned world = 0, creature = 0;
        for (const std::string& s : sets.spells) { world += s.compare(0, 6, "world\\") == 0; creature += s.compare(0, 9, "creature\\") == 0; }
        Info(F("%u of its files are under world\\ and %u under creature\\ (doodads and creatures a few spells reuse: they stay theirs)", world, creature));
        uint8_t junk[64] = { 'W', 'D', 'B', 'C' };
        std::unordered_set<std::string> none;
        Check(sl::LoadVisualTable(junk, sizeof(junk), 2, none) == -1 && none.empty(), "a cut-off table is refused");

        using K = sl::Kind;
        Check(sl::Classify("spells\\fireball_missile_high.m2", 0, false, sets) == K::Effect, "a spell's file made with flags 0: an effect");
        Check(sl::Classify("spells\\fireball_missile_high.m2", sl::kFlagMissile, false, sets) == K::Missile, "... made with 0x40 (Spell_C's missiles): a missile");
        Check(sl::Classify("spells\\holy_missile_high.m2", 0, true, sets) == K::Missile, "... found in the missile lists: a missile");
        Check(sl::Classify("spells\\fireball_missile_high.m2", sl::kFlagMapDoodad, false, sets) == K::None, "a map doodad (0x20) with a spell's file stays with the doodad lights");
        Check(sl::Classify("spells\\errorcube.m2", 0, true, sets) == K::None, "the client's stand-in ErrorCube gives no light");
        Check(sl::Classify("spells\\enchantments\\redglow_high.m2", 0, false, sets) == K::Enchant, "an enchant's glow");
        Check(sl::Classify("world\\azeroth\\elwynn\\passivedoodads\\anvil\\anvil.m2", 0, false, sets) == K::None, "an anvil: not a spell visual");
        Check(sl::Classify("creature\\human\\male\\humanmale.m2", 0, false, sets) == K::None, "a character: not a spell visual");
        // Not spells (2026-10-24, the Storm Peaks' Icethorn): what the client shows by itself, game objects, doodads and
        // creatures a spell reuses.
        struct Not { const char* path; bool attached; const char* what; };
        const Not notSpells[] = {
            { "particles\\lootfx.m2", true, "the loot sparkle on a tracked herb or ore node, a chest, a quest object (HARDCODED Loot Art)" },
            { "particles\\coldbreath.m2", true, "breath in cold air (HARDCODED Breath Cold)" },
            { "spells\\levelup\\levelup.m2", true, "a level-up (HARDCODED Unit Level Up, under spells\\)" },
            { "spells\\reputationlevelup.m2", true, "a reputation level (HARDCODED)" },
            { "particles\\bloodspurts\\bloodspurt.m2", true, "a combat blood spurt (particles\\)" },
            { "particles\\footstepspraysnow.m2", true, "a footstep spray in snow (particles\\)" },
            { "particles\\frostbolt_missle.m2", false, "an old Frostbolt no 3.3.5 spell uses (particles\\)" },
            { "world\\expansion02\\doodads\\azjol-nerub\\saronite\\saronite_node_01.m2", false, "a Saronite deposit (a game object; the table's Massive Saronite Rock)" },
            { "world\\skillactivated\\tradeskillnodes\\bush_icethorn.m2", false, "an Icethorn node" },
            { "world\\generic\\activedoodads\\spellportals\\mageportal_stormwind.m2", false, "a mage's portal (a game object the table lists)" },
            { "spells\\instancenewportal_blue.m2", false, "an instance portal: a game object's model under spells\\, placed alone" },
            { "world\\expansion02\\doodads\\generic\\human\\northrend human mailbox dodad\\final\\hu_mailbox.m2", false, "a mailbox" },
            { "world\\expansion02\\doodads\\dalaran\\dalaran_lamppost01.m2", false, "a Dalaran lamp post (a game object)" },
            { "creature\\wisp\\wisp.m2", false, "a wisp (a creature the table lists)" },
            { "item\\objectcomponents\\weapon\\sword_2h_frostmourne_d_02.m2", true, "Frostmourne in a hand (an item the table lists)" },
        };
        for (const Not& n : notSpells)
            Check(sl::Classify(n.path, 0, false, sets, n.attached) == K::None, F("not a spell light: %s", n.what));
        Check(sl::Classify("spells\\banish_chest_dark.m2", 0, false, sets, true) == K::Effect && sl::ObjectModel("spells\\banish_chest_dark.m2", sets),
              "a spell's file a game object also uses, hung on a unit (Banish): an effect");
        Check(sl::Classify("world\\expansion02\\doodads\\azjol-nerub\\saronite\\saronite_node_01.m2", sl::kFlagMissile, false, sets) == K::Missile &&
              sl::Classify("item\\objectcomponents\\ammo\\arrowfireflight_01.m2", 0, true, sets) == K::Missile &&
              sl::Classify("particles\\lootfx.m2", 0, true, sets) == K::None,
              "anything flying as a missile is one (a hurled rock, a fire arrow), never the client's own effects");

        float c[3];
        Check(sl::KeywordColour("spells\\fireball_blue_missile_high.m2", c) && c[2] > c[0], "names: blue fire is blue (colour words first)");
        Check(sl::KeywordColour("spells\\fel_fireball_missile_high.m2", c) && c[1] > c[0], "names: fel fire is green");
        Check(sl::KeywordColour("spells\\lightningbolt_missile.m2", c) && c[2] >= c[0], "names: lightning before light");
        Check(sl::KeywordColour("spells\\frostbolt.m2", c) && c[2] > c[0], "names: frost is blue");
        Check(!sl::KeywordColour("spells\\abc_impact.m2", c), "names: no colour word, none");

        // Particle tracks: the alpha-weighted mean colour over a particle's life.
        {
            const float colours[] = { 255, 0, 0, 0, 0, 255 };
            const int16_t alphas[] = { 32767, 0 };
            float out[3], alpha = 0;
            sl::EmitterTrackColour(nullptr, 0, colours, 2, nullptr, 0, alphas, 2, out, alpha);
            Check(out[0] > 0.6f && out[2] < 0.4f && std::fabs(alpha - 0.5f) < 0.02f,
                  F("a red-to-blue particle fading out glows mostly red (%.2f %.2f %.2f, alpha %.2f): the fading end counts less", out[0], out[1], out[2], alpha));
            const int16_t zero[] = { 0, 0 };
            sl::EmitterTrackColour(nullptr, 0, colours, 2, nullptr, 0, zero, 2, out, alpha);
            Check(alpha == 0.0f, "an invisible particle: alpha 0");
            const int16_t times[] = { 0, 3276, 32767 };
            const float three[] = { 255, 255, 255, 255, 0, 0, 255, 0, 0 };
            sl::EmitterTrackColour(times, 3, three, 3, nullptr, 0, nullptr, 0, out, alpha);
            Check(out[1] < 0.15f, F("keys at their times (white only for the first tenth): %.2f %.2f %.2f", out[0], out[1], out[2]));
        }
        Check(sl::BlendWeight(4) == 1.0f && sl::BlendWeight(2) < 0.5f && sl::BlendWeight(5) == 0.0f, "blend modes: additive is light, alpha-blended less, modulating none");

        // Shade: what a model gives off.
        {
            sl::Look look;
            look.hasLight = true;
            look.light[0] = look.light[1] = look.light[2] = 1.0f;          // Fireball_Missile_High's own light: white
            look.emitters[0] = { { 1.0f, 0.45f, 0.1f }, 1.0f };
            look.emitterCount = 1;
            float o[3];
            bool lit = sl::Shade(look, o);
            Check(lit && o[0] > 0.95f && o[1] < 0.6f && o[2] < 0.2f, F("a white own light takes its hue from the particles (fire: %.2f %.2f %.2f)", o[0], o[1], o[2]));
            look.light[0] = 0.72f * 16; look.light[1] = 0.09f * 16; look.light[2] = 0.07f * 16;  // Flamestrike's: red x 16
            lit = sl::Shade(look, o);
            Check(lit && std::fabs(o[0] - 1.0f) < 1e-4f && o[1] < 0.15f, F("a saturated own light keeps its colour, its brightest channel at most 1 (Flamestrike: %.2f %.2f %.2f)", o[0], o[1], o[2]));
            look.light[0] = look.light[1] = look.light[2] = 0.0f;           // its light off by its animation: the particles
            lit = sl::Shade(look, o);
            Check(lit && o[0] > 0.9f, F("its light off: the particles' glow (%.2f %.2f %.2f)", o[0], o[1], o[2]));
        }
        {
            sl::Look look;
            look.emitters[0] = { { 0.45f, 0.15f, 0.8f }, 0.4f };          // a dim purple glow (Shadow Bolt)
            look.emitterCount = 1;
            float o[3];
            bool lit = sl::Shade(look, o);
            Check(lit && sl::MaxChannel(o) < 0.35f && o[2] > o[0], F("a dim purple glow stays dim and purple (%.2f %.2f %.2f)", o[0], o[1], o[2]));
            look.emitters[0] = { { 0.0f, 0.0f, 0.0f }, 1.0f };             // BlackGlow
            Check(!sl::Shade(look, o), "a black glow gives no light");
            look.emitters[0] = { { 0.9f, 0.9f, 0.9f }, 1.0f };
            look.hasKeyword = sl::KeywordColour("spells\\frostbolt.m2", look.keyword);
            lit = sl::Shade(look, o);
            Check(lit && o[2] > o[0] + 0.2f, F("white particles (the colour is in a texture not read yet) take the name's hue (frost: %.2f %.2f %.2f)", o[0], o[1], o[2]));
            look.alpha = 0.0f;
            Check(!sl::Shade(look, o), "alpha 0 (the client faded the model out): nothing");
        }
        {
            sl::Look look;
            look.emitters[0] = { { 1.0f, 0.8f, 0.1f }, 1.0f };           // golden sparkles
            look.emitterCount = 1;
            look.hasKeyword = sl::KeywordColour("spells\\arcane_missile_lvl3.m2", look.keyword);
            float o[3];
            const bool lit = sl::Shade(look, o);
            Check(lit && std::fabs(sl::MaxChannel(o) - 1.0f) < 1e-4f && o[2] > 0.55f && o[2] < 0.75f,
                  F("a school's name leans the hue halfway, the brightness kept (golden sparkles named arcane: %.2f %.2f %.2f)", o[0], o[1], o[2]));
            look.light[0] = 0.72f; look.light[1] = 0.09f; look.light[2] = 0.07f;
            look.hasLight = true;
            const bool lit2 = sl::Shade(look, o);
            Check(lit2 && o[1] < 0.15f, "... but not the colour of the model's own (saturated) light");
        }
        {
            sl::Look look;
            look.geometry[0] = 1.0f; look.geometry[1] = 0.9f; look.geometry[2] = 0.5f;
            look.geometryWeight = 0.6f;
            float o[3];
            const bool lit = sl::Shade(look, o);
            Check(lit && std::fabs(o[0] - 0.6f) < 1e-4f, F("geometry alone (Holy_Precast_Low_Hand): its glow x 0.6 (%.2f %.2f %.2f)", o[0], o[1], o[2]));
            sl::Look empty;
            Check(!sl::Shade(empty, o), "nothing to go by: no light");
        }

        // Size.
        sl::Settings s;
        const float rTiny = sl::LightRadius(s, K::Effect, 0.3f), rOne = sl::LightRadius(s, K::Effect, 1.0f), rFire = sl::LightRadius(s, K::Missile, 4.7f),
                    rAoe = sl::LightRadius(s, K::Effect, 16.4f), rHuge = sl::LightRadius(s, K::Effect, 20.0f), rEnch = sl::LightRadius(s, K::Enchant, 1.0f);
        Check(rTiny < rOne && rOne < rFire && rFire < rAoe && rAoe <= rHuge, F("bigger effects reach further: %.1f (0.3 yd) < %.1f (1) < %.1f (Fireball, 4.7) < %.1f (Consecration, 16.4) <= %.1f", rTiny, rOne, rFire, rAoe, rHuge));
        Check(rTiny >= 0.7f * s.radius - 1e-3f && rHuge <= 2.4f * s.radius + 1e-3f, F("within 0.7 .. 2.4 of the setting (%.1f .. %.1f)", 0.7f * s.radius, 2.4f * s.radius));
        Check(std::fabs(rEnch - 0.5f * rOne) < 1e-4f, F("an enchant reaches half as far (%.1f)", rEnch));
        Check(sl::SizeOf(1000.0f, 1.0f) == 20.0f && sl::SizeOf(0.01f, 1.0f) == 0.3f && sl::SizeOf(2.0f, 3.0f) == 6.0f && sl::SizeOf(NAN, 1.0f) == 1.0f, "size: the file's radius x the model's scale, 0.3 .. 20 (a broken radius: 1)");
        Check(sl::SizeGain(0.0f) == 0.8f && sl::SizeGain(0.3f) < sl::SizeGain(1.0f) && sl::SizeGain(20.0f) == 1.4f && sl::SizeGain(1.0f) == 1.0f,
              F("brightness with size: %.2f (0.3 yd) .. 1 (1 yd) .. 1.4 (the most)", sl::SizeGain(0.3f)));
        Check(sl::Lift(0.3f) == 0.3f && sl::Lift(20.0f) == 1.5f, "the light sits 0.3 .. 1.5 yd above the model's origin");

        // Fade.
        {
            sl::Fade f;
            const float on[3] = { 1.0f, 0.5f, 0.1f }, off[3] = {};
            float t = 0.0f;
            while (f.level < 0.9f && t < 2.0f) { sl::StepFade(f, on, 1.0f / 60.0f); t += 1.0f / 60.0f; }
            Check(t <= 0.16f, F("it flares: 90%% within %.0f ms", t * 1000.0f));
            float c[3];
            sl::FadedColour(f, c);
            Check(std::fabs(c[1] / c[0] - 0.5f) < 0.02f, "in its own hue");
            t = 0.0f;
            while (f.level > 0.1f && t < 5.0f) { sl::StepFade(f, off, 1.0f / 60.0f); t += 1.0f / 60.0f; }
            Check(t > 0.3f && t < 0.6f, F("it dies away: to 10%% in %.0f ms", t * 1000.0f));
            while (f.level >= 0.004f && t < 5.0f) { sl::StepFade(f, off, 1.0f / 60.0f); t += 1.0f / 60.0f; }
            Check(t < 1.4f, F("gone (below 0.4%%: the tracker forgets it) after %.2f s", t));
            sl::Fade g;
            const float blue[3] = { 0.2f, 0.4f, 1.0f };
            for (int k = 0; k < 30; ++k) sl::StepFade(g, on, 1.0f / 60.0f);
            for (int k = 0; k < 60; ++k) sl::StepFade(g, blue, 1.0f / 60.0f);
            sl::FadedColour(g, c);
            Check(c[2] > c[0], "the hue follows a changing colour");
            // Steady: after its first moments a step in what it gives (a particle more of ten) shows gently.
            sl::Fade h;
            const float ten[3] = { 1.0f, 1.0f, 1.0f }, eleven[3] = { 1.1f, 1.1f, 1.1f };
            for (int k = 0; k < 60; ++k) sl::StepFade(h, ten, 1.0f / 60.0f);
            const float before = h.level;
            sl::StepFade(h, eleven, 1.0f / 60.0f);
            Check((h.level - before) / before < 0.02f, F("a steady light's 10%% step shows as %.1f%% the next frame (kRiseSeconds %.2f s after the first %.2f s)",
                                                     100.0f * (h.level - before) / before, sl::kRiseSeconds, sl::kYoungSeconds));
            // Wall time, not frames: 30 frames at 30 fps reach what 60 at 60 fps do.
            sl::Fade a, b;
            for (int k = 0; k < 30; ++k) sl::StepFade(a, on, 1.0f / 30.0f);
            for (int k = 0; k < 60; ++k) sl::StepFade(b, on, 1.0f / 60.0f);
            Check(std::fabs(a.level - b.level) < 0.01f, F("by wall time: a second at 30 fps %.3f, at 60 fps %.3f", a.level, b.level));
            // An emitter's live count: 9, 10, 9, 10 ... as particles are born and die, steady.
            float count = 10.0f, lo = 1e9f, hi = 0.0f;
            const float seconds = sl::CountSeconds(1.0f, false);
            for (int k = 0; k < 240; ++k)
            {
                sl::StepCount(count, (k / 6) % 2 ? 9.0f : 10.0f, seconds, 1.0f / 60.0f);
                if (k >= 120) { lo = std::min(lo, count); hi = std::max(hi, count); }
            }
            Check((hi - lo) / hi < 0.03f, F("a live count stepping 9, 10, 9 (10 a second) smoothed over %.2f s: within %.1f%%", seconds, 100.0f * (hi - lo) / hi));
            float gone = 10.0f;
            int frames = 0;
            while (gone > 0.0f && frames < 120) { sl::StepCount(gone, 0.0f, seconds, 1.0f / 60.0f); ++frames; }
            Check(frames <= 30, F("none alive: the count gone in %d frames (nothing of it is seen)", frames));
        }

        // Merge and cap.
        {
            sl::Settings st;
            const float eye[3] = { 0.0f, 0.0f, 10.0f };
            std::vector<sl::Light> field;
            // A Blizzard: 30 shards' impacts at once over an 8-yd disc (a golden-angle spiral), each a pale blue light.
            for (int k = 0; k < 30; ++k)
            {
                const float r = 8.0f * std::sqrt((k + 0.5f) / 30.0f), a = k * 2.39996f;
                sl::Light l;
                l.pos[0] = r * std::cos(a); l.pos[1] = r * std::sin(a); l.pos[2] = 0.5f;
                l.color[0] = 0.5f; l.color[1] = 0.75f; l.color[2] = 1.0f;
                l.radius = 10.0f;
                l.model = reinterpret_cast<const void*>(static_cast<uintptr_t>(0x1000 + k * 16));
                field.push_back(l);
            }
            std::vector<sl::Light> merged = field;
            sl::MergeStats ms;
            sl::MergeAndCap(merged, eye, st, ms);
            Check(ms.out == static_cast<unsigned>(st.maxLights) && ms.dropped == 30 - ms.out - ms.merged,
                  F("a Blizzard's 30 impacts (1.5 to 2.6 yd apart): %u lights, %u merged (closer than %.1f yd), %u over the cap", ms.out, ms.merged, st.mergeDistance, ms.dropped));
            // A unit's four auras at one spot (within half a yard): one light, sqrt(4) = 2 x one, not 4.
            {
                std::vector<sl::Light> auras;
                for (int k = 0; k < 4; ++k)
                {
                    sl::Light l;
                    l.pos[0] = 20.0f + 0.1f * k; l.pos[1] = 20.0f; l.pos[2] = 1.0f;
                    l.color[0] = 1.0f; l.color[1] = 0.8f; l.color[2] = 0.3f;
                    l.radius = 7.0f;
                    l.model = reinterpret_cast<const void*>(static_cast<uintptr_t>(0x2000 + k * 16));
                    auras.push_back(l);
                }
                sl::MergeStats ma;
                sl::MergeAndCap(auras, eye, st, ma);
                Check(auras.size() == 1 && auras[0].sources == 4 && std::fabs(auras[0].color[0] - 2.0f) < 1e-4f && std::fabs(sl::MergeKeep(7) - 1.0f / std::sqrt(7.0f)) < 1e-6f,
                      F("a unit's four auras at one spot merge into one light at sqrt(4) of their sum (%.2f x one; MergeKeep), as env/Lights' small flames", auras.empty() ? 0.0f : auras[0].color[0]));
            }
            // The light that reaches the floor: summed as the shaders sum it, through the knee, merged or not.
            // ... and the share of the floor round it at the knee's top (1.25 of 1.3 or more: the blown-out look).
            auto floorPeak = [&](const std::vector<sl::Light>& ls, float* topShare = nullptr)
            {
                float peak = 0.0f;
                unsigned top = 0, points = 0;
                for (float x = -10; x <= 10; x += 0.5f)
                    for (float y = -10; y <= 10; y += 0.5f)
                    {
                        float sum[3] = {};
                        for (const sl::Light& l : ls)
                        {
                            const float d2 = (l.pos[0] - x) * (l.pos[0] - x) + (l.pos[1] - y) * (l.pos[1] - y) + l.pos[2] * l.pos[2];
                            const float d = std::sqrt(d2);
                            if (d >= l.radius) continue;
                            const float t = d / l.radius, win = (1 - t * t) * (1 - t * t);
                            const float de = std::sqrt(d2 + 1.0f), a = win / (de * (0.7f + 0.03f * de));
                            const float wrap = std::clamp((l.pos[2] / std::max(d, 1e-3f) + 0.5f) / 1.5f, 0.0f, 1.0f);
                            for (int k = 0; k < 3; ++k) sum[k] += l.color[k] * a * wrap;
                        }
                        li::LampKnee(sum);
                        peak = std::max(peak, sl::MaxChannel(sum));
                        top += sl::MaxChannel(sum) >= 1.25f ? 1u : 0u;
                        ++points;
                    }
                if (topShare) *topShare = points ? static_cast<float>(top) / points : 0.0f;
                return peak;
            };
            std::vector<sl::Light> raw = field;
            sl::Settings noMerge = st;
            noMerge.mergeDistance = 0.0f;
            noMerge.maxLights = 64;
            sl::MergeStats ms2;
            sl::MergeAndCap(raw, eye, noMerge, ms2);
            float rawSum = 0.0f;
            {
                // Its raw sum before the knee, at the disc's middle.
                for (const sl::Light& l : field)
                {
                    const float d2 = l.pos[0] * l.pos[0] + l.pos[1] * l.pos[1] + l.pos[2] * l.pos[2], d = std::sqrt(d2);
                    if (d >= l.radius) continue;
                    const float t = d / l.radius, de = std::sqrt(d2 + 1.0f);
                    rawSum += l.color[2] * (1 - t * t) * (1 - t * t) / (de * (0.7f + 0.03f * de));
                }
            }
            float topMerged = 0.0f, topRaw = 0.0f;
            const float peakMerged = floorPeak(merged, &topMerged), peakRaw = floorPeak(field, &topRaw);
            Check(peakMerged < 1.25f && topMerged == 0.0f && topRaw > 0.1f,
                  F("on the floor (the shaders' sum, through the knee): at most %.2f merged and crowded, none of it at the knee's top; with every impact a light of its own, uncrowded, %.0f%% of the 20 x 20 yd at it (%.1f before the knee at the disc's middle)",
                    peakMerged, 100.0f * topRaw, rawSum));
            Check(ms.crowded > 0, F("%u of the field's lights crowded (each keeps 1 / sqrt of how many reach it)", ms.crowded));
            {
                std::vector<sl::Light> lone(1, field[0]);
                sl::MergeStats ml;
                sl::MergeAndCap(lone, eye, st, ml);
                Check(lone.size() == 1 && std::fabs(lone[0].color[2] - 1.0f) < 1e-5f && ml.crowded == 0, "a light alone keeps all of its colour");
            }

            // The cap: the nearest and brightest; the same choice whatever the input's order.
            std::vector<sl::Light> many;
            for (int k = 0; k < 100; ++k)
            {
                sl::Light l;
                l.pos[0] = 3.0f + (k % 10) * 6.0f; l.pos[1] = (k / 10) * 6.0f; l.pos[2] = 1.0f;
                l.color[0] = l.color[1] = l.color[2] = (k % 3 == 0) ? 1.0f : 0.3f;
                l.radius = 7.0f;
                l.model = reinterpret_cast<const void*>(static_cast<uintptr_t>(0x9000 + k * 16));
                many.push_back(l);
            }
            std::vector<sl::Light> a = many, b(many.rbegin(), many.rend());
            sl::MergeStats ma, mb;
            sl::MergeAndCap(a, eye, st, ma);
            sl::MergeAndCap(b, eye, st, mb);
            bool same = a.size() == b.size();
            for (size_t k = 0; same && k < a.size(); ++k) same = a[k].model == b[k].model;
            bool ordered = true;
            for (size_t k = 1; k < a.size(); ++k) ordered = ordered && a[k - 1].score >= a[k].score;
            Check(a.size() == static_cast<size_t>(st.maxLights) && ma.dropped == 100 - st.maxLights && same && ordered,
                  F("100 lights: the %d best kept (%u over the cap), by score, the same whatever the order they came in", st.maxLights, ma.dropped));
            const float nearBright = sl::Score(many[0], eye), nearDim = sl::Score(many[1], eye), distant = sl::Score(many[99], eye);
            Check(nearBright > nearDim && nearBright > distant && sl::Score(many[90], eye) > sl::Score(many[99], eye), "the score: brighter and nearer first");
            sl::Settings narrow = st;
            narrow.range = 20.0f;
            std::vector<sl::Light> c = many;
            sl::MergeStats mc;
            sl::MergeAndCap(c, eye, narrow, mc);
            bool inRange = true;
            for (const sl::Light& l : c) inRange = inRange && std::hypot(l.pos[0] - eye[0], l.pos[1] - eye[1], l.pos[2] - eye[2]) <= 20.0f;
            Check(inRange && mc.inRange < 100, F("beyond the range none (%u of 100 within 20 yd)", mc.inRange));
            sl::Settings zero = st;
            zero.maxLights = 0;
            std::vector<sl::Light> d = many;
            sl::MergeStats md;
            sl::MergeAndCap(d, eye, zero, md);
            Check(d.empty(), "MaxLights 0: none");
        }

        // Select, frame after frame (each model's state carried as SpellLights carries it).
        {
            sl::Settings st;
            const float eye[3] = { 0.0f, 0.0f, 10.0f };
            const float dt = 1.0f / 60.0f;
            auto light = [](float x, float y, float b, uintptr_t id)
            {
                sl::Light l;
                l.pos[0] = x; l.pos[1] = y; l.pos[2] = 1.0f;
                l.color[0] = b; l.color[1] = 0.8f * b; l.color[2] = 0.4f * b;
                l.radius = 7.0f;
                l.model = reinterpret_cast<const void*>(id);
                l.age = 10.0f;
                return l;
            };
            std::vector<sl::Light> out;
            sl::MergeStats ms;
            // Quadrature: a dim light at a bright one's spot barely changes it (the old rule took 1 / sqrt 2 of their sum).
            {
                std::vector<sl::Light> two = { light(0, 0, 1.0f, 0x100), light(0.2f, 0, 0.01f, 0x110) };
                for (sl::Light& l : two) l.age = 0.0f;
                for (int f = 0; f < 3; ++f) sl::Select(two, out, eye, st, sl::CellRoom{}, dt, ms);
                Check(out.size() == 1 && std::fabs(sl::MaxChannel(out[0].color) - 1.0f) < 0.001f,
                      F("a faint light joining a bright one: %.4f of it (summed in quadrature)", out.empty() ? 0.0f : sl::MaxChannel(out[0].color)));
            }
            // Merging holds: two merged at 1.4 yd stay merged at 1.8 (within kMergeHold x 1.5 = 1.95), part at 2.1.
            {
                std::vector<sl::Light> two = { light(0, 0, 1.0f, 0x200), light(1.4f, 0, 0.9f, 0x210) };
                sl::Select(two, out, eye, st, sl::CellRoom{}, dt, ms);
                const size_t at14 = out.size();
                two[1].pos[0] = 1.8f;
                sl::Select(two, out, eye, st, sl::CellRoom{}, dt, ms);
                const size_t at18 = out.size();
                two[1].pos[0] = 2.1f;
                sl::Select(two, out, eye, st, sl::CellRoom{}, dt, ms);
                const size_t at21 = out.size();
                std::vector<sl::Light> fresh = { light(0, 0, 1.0f, 0x300), light(1.8f, 0, 0.9f, 0x310) };
                sl::Select(fresh, out, eye, st, sl::CellRoom{}, dt, ms);
                Check(at14 == 1 && at18 == 1 && at21 == 2 && out.size() == 2,
                      F("merged at 1.4 yd: still one at 1.8 (kMergeHold %.1f x the distance), two at 2.1; two new ones 1.8 apart: two", sl::kMergeHold));
            }
            // The cap holds what it kept: 25 alike for 24 places, their scores wobbling 5%: the same 24 every frame; one
            // clearly better coming in: the one it displaces fades out over frames, still drawn meanwhile.
            {
                std::vector<sl::Light> set;
                for (int k = 0; k < 25; ++k) set.push_back(light(3.0f * (k % 5) - 6.0f, 3.0f * (k / 5) - 6.0f, 0.5f, 0x1000 + 16 * k));
                std::vector<const void*> first;
                bool same = true;
                for (int f = 0; f < 60; ++f)
                {
                    for (int k = 0; k < 25; ++k) for (int c = 0; c < 3; ++c) set[k].color[c] = (c == 0 ? 0.5f : c == 1 ? 0.4f : 0.2f) * (1.0f + 0.05f * std::sin(0.3f * f + k));
                    sl::Select(set, out, eye, st, sl::CellRoom{}, dt, ms);
                    std::vector<const void*> kept;
                    for (const sl::Light& l : out) if (l.kept) kept.push_back(l.model);
                    std::sort(kept.begin(), kept.end());
                    if (f == 10) first = kept;
                    if (f > 10) same = same && kept == first;
                }
                Check(same && first.size() == 24, F("25 alike for 24 places, scores wobbling 5%%: the same %zu kept every frame", first.size()));
                set.push_back(light(7.5f, -7.5f, 2.0f, 0x2000));
                set.back().age = 0.0f;   // new: comes in at once
                sl::Select(set, out, eye, st, sl::CellRoom{}, dt, ms);
                unsigned leaving = 0;
                float leavingLight = 0.0f;
                bool newcomer = false;
                for (const sl::Light& l : out)
                {
                    if (!l.kept) { ++leaving; leavingLight = sl::MaxChannel(l.color); }
                    newcomer = newcomer || (l.model == set.back().model && l.kept);
                }
                Check(newcomer && leaving == 1 && leavingLight > 0.1f, F("a brighter one new: in at once, the one it displaces still drawn (%u leaving, %.2f)", leaving, leavingLight));
                int frames = 1;
                while (frames < 120)
                {
                    sl::Select(set, out, eye, st, sl::CellRoom{}, dt, ms);
                    ++frames;
                    bool any = false;
                    for (const sl::Light& l : out) any = any || !l.kept;
                    if (!any) break;
                }
                Check(frames > 20 && frames < 70, F("... fading out over %d frames (%.2f s; kPresenceSeconds %.2f)", frames, frames / 60.0f, sl::kPresenceSeconds));
            }
            // The grid's room: 20 lights in one 16-yd cell, 16 places: 16 drawn there, 4 waiting; the next frame the same.
            {
                std::vector<sl::Light> crowd;
                for (int k = 0; k < 20; ++k) crowd.push_back(light(4.0f + 2.0f * (k % 5), 4.0f + 2.0f * (k / 5), 0.3f, 0x3000 + 16 * k));
                for (sl::Light& l : crowd) l.radius = 3.0f;
                sl::Settings wide = st;
                wide.maxLights = 32;
                wide.mergeDistance = 0.5f;
                const sl::CellRoom room{ 16.0f, 16 };
                sl::Select(crowd, out, eye, wide, room, dt, ms);
                const size_t first = out.size();
                const unsigned waiting = ms.waiting;
                sl::Select(crowd, out, eye, wide, room, dt, ms);
                Check(first == 16 && waiting == 4 && out.size() == 16 && ms.waiting == 4,
                      F("20 lights within one grid cell of 16 places: %zu drawn, %u waiting (and %zu, %u the next frame)", first, waiting, out.size(), ms.waiting));
            }
            // A missile flying into a full cell: eight kept lights fill its room; the brighter newcomer hurries the weakest
            // out (a few frames' fade, no pop) and comes in.
            {
                std::vector<sl::Light> cell;
                for (int k = 0; k < 8; ++k) cell.push_back(light(3.0f + 1.6f * k, 4.0f, 0.2f + 0.01f * k, 0x5000 + 16 * k));
                for (sl::Light& l : cell) l.radius = 3.0f;
                sl::Settings wide = st;
                wide.mergeDistance = 0.5f;
                const sl::CellRoom room{ 16.0f, 8 };
                for (int f = 0; f < 60; ++f) sl::Select(cell, out, eye, wide, room, dt, ms);
                cell.push_back(light(8.0f, 10.0f, 1.5f, 0x6000));
                cell.back().radius = 3.0f;
                cell.back().age = 0.0f;
                int frames = 0;
                bool in = false, smooth = true;
                std::map<const void*, float> last;   // each light's brightness the frame before: none may drop 3/4 or vanish from 0.05 up
                for (const sl::Light& l : out) last[l.model] = sl::MaxChannel(l.color);
                while (!in && frames < 60)
                {
                    sl::Select(cell, out, eye, wide, room, dt, ms);
                    cell.back().age += dt;
                    ++frames;
                    std::map<const void*, float> now;
                    for (const sl::Light& l : out)
                    {
                        in = in || (l.model == cell.back().model && l.kept);
                        now[l.model] = sl::MaxChannel(l.color);
                    }
                    for (const auto& [model, before] : last)
                    {
                        const auto it = now.find(model);
                        const float after = it == now.end() ? 0.0f : it->second;
                        if (before > 0.05f && after < 0.25f * before) smooth = false;
                    }
                    last = now;
                }
                Check(in && frames <= 20 && smooth && out.size() <= 9,
                      F("a full cell (8 places): a brighter newcomer is in after %d frames, the weakest there faded out meanwhile, not popped", frames));
            }
            // Out of range: a light going past the range fades out over its last fifth instead of popping.
            {
                sl::Settings narrow = st;
                narrow.range = 20.0f;
                std::vector<sl::Light> one = { light(0.0f, 12.0f, 1.0f, 0x4000) };
                one[0].age = 0.0f;
                sl::Select(one, out, eye, narrow, sl::CellRoom{}, dt, ms);
                const float atStart = out.empty() ? 0.0f : sl::MaxChannel(out[0].color);
                one[0].pos[1] = 16.5f;
                sl::Select(one, out, eye, narrow, sl::CellRoom{}, dt, ms);
                const float nearEnd = out.empty() ? 0.0f : sl::MaxChannel(out[0].color);
                Check(atStart > 0.99f && nearEnd > 0.1f && nearEnd < 0.6f, F("toward the range's end it fades (%.2f at 15 yd, %.2f at 18.8 yd of 20)", atStart, nearEnd));
            }
        }
    }

    // --- 2. the client side over a fake client ----------------------------------------------------------------------------
    struct Arena
    {
        std::vector<std::unique_ptr<uint8_t[]>> blocks;
        uint8_t* Alloc(size_t n)
        {
            blocks.emplace_back(new uint8_t[n + 16]());
            return blocks.back().get();
        }
    };
    template <class T> void Put(uint8_t* base, size_t off, const T& v) { std::memcpy(base + off, &v, sizeof(T)); }
    template <class T> T Take(const uint8_t* base, size_t off) { T v; std::memcpy(&v, base + off, sizeof(T)); return v; }
    uint32_t Ptr(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

    // A model file as the client holds it once loaded: the arrays features/SpellLights reads made pointers.
    // Each loaded file's bytes as on disk (the fake's own simulation reads the tracks there).
    std::map<const uint8_t*, std::vector<uint8_t>> g_originals;

    uint8_t* LoadM2(Arena& a, const std::string& name, size_t& size)
    {
        std::vector<uint8_t> bytes;
        if (!ReadAll(g_data / "m2" / name, bytes) || bytes.size() < 0x140) return nullptr;
        uint8_t* d = a.Alloc(bytes.size());
        std::memcpy(d, bytes.data(), bytes.size());
        size = bytes.size();
        g_originals[d] = bytes;
        auto rel = [&](size_t at)
        {
            const uint32_t count = Take<uint32_t>(d, at - 4), off = Take<uint32_t>(d, at);
            Put<uint32_t>(d, at, count && off < bytes.size() ? Ptr(d + off) : 0u);
            return count && off < bytes.size() ? d + off : nullptr;
        };
        const uint32_t nt = Take<uint32_t>(d, 0x50);
        if (uint8_t* t = rel(0x54))
            for (uint32_t k = 0; k < nt; ++k) rel(t - d + k * 0x10 + 0x0C);
        rel(0x10C);
        rel(0x74);   // materials
        // Ribbons: their texture and material indices, their colour and alpha tracks (each sequence's keys too).
        const uint32_t nr = Take<uint32_t>(d, 0x120);
        if (uint8_t* rb = rel(0x124))
            for (uint32_t k = 0; k < nr; ++k)
            {
                const size_t r = (rb - d) + k * 0xB0;
                rel(r + 0x18);
                rel(r + 0x20);
                for (size_t track : { 0x24u, 0x38u })
                {
                    const uint32_t sequences = Take<uint32_t>(d, r + track + 0x0C);
                    if (uint8_t* per = rel(r + track + 0x10))
                        for (uint32_t q = 0; q < sequences; ++q) rel((per - d) + q * 8 + 4);
                }
            }
        const uint32_t ne = Take<uint32_t>(d, 0x128);
        if (uint8_t* e = rel(0x12C))
            for (uint32_t k = 0; k < ne; ++k)
            {
                const size_t p = (e - d) + k * 0x1DC;
                for (size_t at : { 0x108u, 0x110u, 0x118u, 0x120u }) rel(p + at);
                // The lifespan track (M2Track at +0x98): its values' per-sequence arrays, each sequence's keys.
                const uint32_t sequences = Take<uint32_t>(d, p + 0x98 + 0x0C);
                if (uint8_t* per = rel(p + 0x98 + 0x10))
                    for (uint32_t q = 0; q < sequences; ++q) rel((per - d) + q * 8 + 4);
            }
        Put<uint32_t>(d, 0xF0, 0u);   // no attachments (a parent's attachment then is its origin)
        return d;
    }

    struct FakeModel
    {
        uint8_t* m = nullptr;
        uint8_t* states = nullptr;
        std::vector<uint8_t*> emitters;
        uint8_t* lights = nullptr;
        uint32_t lightCount = 0;
        uint8_t* colours = nullptr;
        uint8_t* weights = nullptr;
        uint32_t colourCount = 0, weightCount = 0;
        bool shown = true;
        // Particles born and dying (Simulate): per emitter its rate track (ms, per second), life and life variation.
        struct Sim
        {
            std::vector<uint32_t> times;
            std::vector<float> rates;
            float life = 1.0f, lifeVariation = 0.0f, acc = 0.0f;
            std::vector<float> deaths;   // the alive particles' ages at death, minus their age now
        };
        std::vector<Sim> sims;
        uint32_t sequence = 1000;        // ms: its sequence's length (the clock wraps)
        float clock = 0.0f;              // s since it was made
        int cadence = 1;                 // the client animates it every this many frames (its frame stamp lags between)
        // Its texture weights animated as the file says (Simulate), when asked: their tracks' keys (ms, 0 .. 32767).
        bool animateWeights = false;
        std::vector<std::vector<uint32_t>> weightTimes;
        std::vector<std::vector<int16_t>> weightValues;
    };

    // A track's keys of its first sequence, from the file's own offsets (`data`, `size` bytes: the file as on disk). None
    // where they lie outside it (a creature's sequence kept in its .anim file).
    template <class T>
    void TrackKeys(const uint8_t* data, size_t size, const uint8_t* track, std::vector<uint32_t>& times, std::vector<T>& values)
    {
        times.clear();
        values.clear();
        const uint32_t ts = Take<uint32_t>(track, 0x04), to = Take<uint32_t>(track, 0x08), vs = Take<uint32_t>(track, 0x0C), vo = Take<uint32_t>(track, 0x10);
        if (!vs || static_cast<uint64_t>(vo) + 8 > size) return;
        const uint32_t vn = std::min(Take<uint32_t>(data, vo), 64u), vp = Take<uint32_t>(data, vo + 4);
        if (static_cast<uint64_t>(vp) + static_cast<uint64_t>(vn) * sizeof(T) > size) return;
        for (uint32_t k = 0; k < vn; ++k) values.push_back(Take<T>(data, vp + k * sizeof(T)));
        if (ts && static_cast<uint64_t>(to) + 8 <= size)
        {
            const uint32_t tn = std::min(Take<uint32_t>(data, to), 64u), tp = Take<uint32_t>(data, to + 4);
            if (static_cast<uint64_t>(tp) + static_cast<uint64_t>(tn) * 4 > size) { values.clear(); return; }
            for (uint32_t k = 0; k < tn; ++k) times.push_back(Take<uint32_t>(data, tp + k * 4));
        }
    }

    uint32_t g_rng = 12345;
    float Rand01() { g_rng = g_rng * 1664525u + 1013904223u; return (g_rng >> 8) * (1.0f / 16777216.0f); }

    struct FakeClient
    {
        Arena arena;
        uint8_t* scene = nullptr;
        std::vector<FakeModel*> list;            // the scene's list, head first
        std::vector<std::unique_ptr<FakeModel>> models;
        std::vector<uint8_t*> inFlight, impact;
        std::map<std::string, uint8_t*> shared;
        uint32_t frame = 100, time = 50000;

        FakeClient() { scene = arena.Alloc(0x150); }

        uint8_t* Shared(const std::string& file)
        {
            auto it = shared.find(file);
            if (it != shared.end()) return it->second;
            uint8_t* s = arena.Alloc(0x1B0);
            std::string name = file;
            std::replace(name.begin(), name.end(), '#', '\\');
            name = sl::NormalizePath(name.c_str());
            std::memcpy(s + 0x3C, name.c_str(), std::min<size_t>(name.size(), 259));
            size_t size = 0;
            uint8_t* data = LoadM2(arena, file, size);
            Put<uint32_t>(s, 0x150, Ptr(data));
            shared[file] = s;
            return s;
        }

        // A model of `file` at `pos`, made as CM2Scene::CreateModel would (`flags`), put at the head of the scene's list.
        FakeModel* Make(const std::string& file, const float pos[3], uint32_t flags = 0, float scale = 1.0f, bool link = true)
        {
            auto fm = std::make_unique<FakeModel>();
            uint8_t* m = fm->m = arena.Alloc(0x300);
            uint8_t* sh = Shared(file);
            const uint8_t* data = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(Take<uint32_t>(sh, 0x150)));
            Put<uint32_t>(m, 0x04, flags);
            Put<uint32_t>(m, 0x10, 1u);
            Put<uint32_t>(m, 0x28, Ptr(scene));
            Put<uint32_t>(m, 0x2C, Ptr(sh));
            Put<uint32_t>(m, 0x74, time++);
            Put<float>(m, 0x198, 1.0f);
            float w[16] = { scale, 0, 0, 0, 0, scale, 0, 0, 0, 0, scale, 0, pos[0], pos[1], pos[2], 1 };
            std::memcpy(m + 0xB4, w, sizeof(w));
            if (data)
            {
                const uint32_t ne = std::min(Take<uint32_t>(data, 0x128), 32u);
                fm->states = arena.Alloc(0x88 * std::max(ne, 1u));
                uint8_t* ptrs = arena.Alloc(4 * std::max(ne, 1u));
                for (uint32_t e = 0; e < ne; ++e)
                {
                    Put<uint8_t>(fm->states, e * 0x88 + 0x85, 1);
                    uint8_t* em = arena.Alloc(0x60);
                    Put<uint32_t>(em, 0x50, 40u);
                    Put<uint32_t>(ptrs, e * 4, Ptr(em));
                    fm->emitters.push_back(em);
                }
                Put<uint32_t>(m, 0x2C0, Ptr(fm->states));
                Put<uint32_t>(m, 0x2C4, Ptr(ptrs));
                // Its lights as the client animates them: the colour track's first value x the intensity's (x alpha 1).
                const uint32_t nl = Take<uint32_t>(data, 0x108);
                const uint8_t* fl = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(Take<uint32_t>(data, 0x10C)));
                fm->lightCount = std::min(nl, 16u);
                fm->lights = arena.Alloc(0xD4 * std::max(fm->lightCount, 1u));
                for (uint32_t l = 0; l < fm->lightCount && fl; ++l)
                {
                    uint8_t* rec = fm->lights + l * 0xD4;
                    Put<uint32_t>(rec, 0x64, 1u);
                    Put<uint8_t>(rec, 0x60, 1);
                    float colour[3] = { 1, 1, 1 }, intensity = 1;
                    FirstValue(fl + l * 0x9C + 0x38, colour, 12, data);
                    FirstValue(fl + l * 0x9C + 0x4C, &intensity, 4, data);
                    float c[3] = { colour[0] * intensity, colour[1] * intensity, colour[2] * intensity };
                    std::memcpy(rec + 0x68 + 0x3C, c, sizeof(c));
                }
                Put<uint32_t>(m, 0x1D0, Ptr(fm->lights));
                // Colours (M2Color 0x28 in the file: colour and alpha tracks) -> M2ModelColor 0x20 (value +8, alpha +0x1C),
                // texture weights (M2TextureWeight 0x14) -> 0x0C each (value +8): their first values.
                const uint32_t nc = std::min(Take<uint32_t>(data, 0x48), 8u), nw = std::min(Take<uint32_t>(data, 0x58), 8u);
                const uint8_t* fc = data + Take<uint32_t>(data, 0x4C);
                const uint8_t* fw = data + Take<uint32_t>(data, 0x5C);
                fm->colours = arena.Alloc(0x20 * std::max(nc, 1u));
                fm->weights = arena.Alloc(0x0C * std::max(nw, 1u));
                fm->colourCount = nc;
                fm->weightCount = nw;
                for (uint32_t k = 0; k < nc; ++k)
                {
                    float colour[3] = { 1, 1, 1 };
                    int16_t alpha = 32767;
                    FirstValue(fc + k * 0x28, colour, 12, data);
                    FirstValue(fc + k * 0x28 + 0x14, &alpha, 2, data);
                    std::memcpy(fm->colours + k * 0x20 + 8, colour, 12);
                    Put<float>(fm->colours, k * 0x20 + 0x1C, alpha / 32767.0f);
                }
                for (uint32_t k = 0; k < nw; ++k)
                {
                    int16_t w = 32767;
                    FirstValue(fw + k * 0x14, &w, 2, data);
                    Put<float>(fm->weights, k * 0x0C + 8, w / 32767.0f);
                }
                Put<uint32_t>(m, 0xA0, Ptr(fm->colours));
                Put<uint32_t>(m, 0xA8, Ptr(fm->weights));
                // Ribbon states (M2ModelRibbon 0x50): visible.
                const uint32_t nr = std::min(Take<uint32_t>(data, 0x120), 8u);
                uint8_t* ribbons = arena.Alloc(0x50 * std::max(nr, 1u));
                for (uint32_t k = 0; k < nr; ++k) Put<uint8_t>(ribbons, k * 0x50 + 0x4C, 1);
                Put<uint32_t>(m, 0x2B8, Ptr(ribbons));
            }
            FakeModel* raw = fm.get();
            models.push_back(std::move(fm));
            list.insert(list.begin(), raw);
            if (link) Link();
            return raw;
        }

        // An M2Track's first value, from the file's own offsets (its nested arrays aren't relocated here; none where they lie
        // outside the file).
        void FirstValue(const uint8_t* track, void* out, size_t size, const uint8_t* data)
        {
            const size_t bytes = g_originals.count(data) ? g_originals[data].size() : 0;
            const uint32_t seqs = Take<uint32_t>(track, 0x0C), seqOff = Take<uint32_t>(track, 0x10);
            if (!seqs || static_cast<uint64_t>(seqOff) + 8 > bytes) return;
            const uint32_t n = Take<uint32_t>(data, seqOff), off = Take<uint32_t>(data, seqOff + 4);
            if (n && static_cast<uint64_t>(off) + size <= bytes) std::memcpy(out, data + off, size);
        }

        void Link()
        {
            Put<uint32_t>(scene, 0x08, list.empty() ? 0u : Ptr(list[0]->m));
            for (size_t k = 0; k < list.size(); ++k)
            {
                Put<uint32_t>(list[k]->m, 0x0C, k + 1 < list.size() ? Ptr(list[k + 1]->m) : 0u);
                Put<uint32_t>(list[k]->m, 0x08, Ptr(k ? list[k - 1]->m + 0x0C : scene + 0x08));
            }
        }

        void Unlink(FakeModel* m)
        {
            list.erase(std::remove(list.begin(), list.end(), m), list.end());
            Put<uint32_t>(m->m, 0x08, 0u);
            Put<uint32_t>(m->m, 0x0C, 0u);
            Link();
        }

        uint8_t* Missile(FakeModel* model, bool hit = false)
        {
            uint8_t* mi = arena.Alloc(0x170);
            Put<uint32_t>(mi, 0x18, Ptr(model->m));
            auto& l = hit ? impact : inFlight;
            l.push_back(mi);
            for (size_t k = 0; k < l.size(); ++k) Put<uint32_t>(l[k], 0x164, k + 1 < l.size() ? Ptr(l[k + 1]) : 0u);
            return mi;
        }

        void Move(FakeModel* m, const float pos[3]) { std::memcpy(m->m + 0xB4 + 48, pos, 12); }

        void Attach(FakeModel* child, FakeModel* parent)
        {
            Put<uint32_t>(child->m, 0x48, Ptr(parent->m));
            Put<uint32_t>(child->m, 0x54, 0xFFFFu);
        }

        // A character (its file named only: no data) holding a weapon, and `file` on the weapon: an enchant as the client
        // has it (ItemVisualEffects' model on the weapon's model, the weapon on the character's).
        FakeModel* Enchant(const std::string& file, const float at[3], FakeModel** unitOut = nullptr)
        {
            FakeModel* unit = Make("Character#Human#Male#HumanMale.m2", at);
            FakeModel* weapon = Make("Item#ObjectComponents#Weapon#Sword_1H_Long_A_01.m2", at);
            Attach(weapon, unit);
            const float zero[3] = {};
            FakeModel* glow = Make(file, zero);
            Attach(glow, weapon);
            if (unitOut) *unitOut = unit;
            return glow;
        }

        sp::ClientView View() const
        {
            sp::ClientView v;
            v.scene = scene;
            v.missiles[0] = inFlight.empty() ? nullptr : inFlight[0];
            v.missiles[1] = impact.empty() ? nullptr : impact[0];
            return v;
        }

        // A frame: the scene's clock, every shown model animated now.
        std::vector<li::FrameLight> Step(const float eye[3], float dt = 1.0f / 60.0f)
        {
            ++frame;
            Put<uint32_t>(scene, 0x14, frame);
            for (auto& m : models)
            {
                if (!m->sims.empty()) Simulate(*m, dt);
                if (m->shown && (frame % static_cast<uint32_t>(std::max(1, m->cadence))) == 0) Put<uint32_t>(m->m, 0x3C, frame);
            }
            std::vector<li::FrameLight> out;
            sp::FrameForTests(View(), dt, eye, out);
            return out;
        }

        // Its emitters' particles born at their rate tracks' rate (linear between keys, the sequence looping) and dying at
        // their life (+- its variation): the live counts the client's emitters hold, frame by frame.
        void StartSimulation(FakeModel* fm)
        {
            const uint8_t* sh = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(Take<uint32_t>(fm->m, 0x2C)));
            const uint8_t* loaded = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(Take<uint32_t>(sh, 0x150)));
            if (!loaded || !g_originals.count(loaded)) return;
            const uint8_t* data = g_originals[loaded].data();   // the file's own offsets
            const size_t size = g_originals[loaded].size();
            const uint8_t* seqs = data + Take<uint32_t>(data, 0x20);
            if (Take<uint32_t>(data, 0x1C)) fm->sequence = std::max(1u, Take<uint32_t>(seqs, 4));
            const uint32_t ne = std::min(Take<uint32_t>(data, 0x128), 32u);
            const uint8_t* em = data + Take<uint32_t>(data, 0x12C);
            const uint32_t nw = std::min(Take<uint32_t>(data, 0x58), 8u);
            fm->weightTimes.assign(nw, {});
            fm->weightValues.assign(nw, {});
            for (uint32_t k = 0; k < nw; ++k) TrackKeys<int16_t>(data, size, data + Take<uint32_t>(data, 0x5C) + k * 0x14, fm->weightTimes[k], fm->weightValues[k]);
            fm->sims.assign(ne, {});
            for (uint32_t e = 0; e < ne; ++e)
            {
                const uint8_t* p = em + e * 0x1DC;
                FakeModel::Sim& sim = fm->sims[e];
                TrackKeys<float>(data, size, p + 0xB0, sim.times, sim.rates);
                std::vector<uint32_t> lt;
                std::vector<float> lv;
                TrackKeys<float>(data, size, p + 0x98, lt, lv);
                sim.life = lv.empty() ? 1.0f : std::max(0.05f, lv[0]);
                sim.lifeVariation = Take<float>(p, 0xAC);
                Put<uint32_t>(fm->emitters[e], 0x50, 0u);
            }
        }

        // A key track's value at `t` ms (linear between keys; the last key's past them).
        template <class T>
        static float At(const std::vector<uint32_t>& times, const std::vector<T>& values, float t)
        {
            if (values.empty()) return 0.0f;
            if (values.size() == 1 || times.size() != values.size()) return static_cast<float>(values[0]);
            for (size_t k = 0; k + 1 < times.size(); ++k)
                if (t >= times[k] && t <= times[k + 1])
                {
                    const float span = static_cast<float>(times[k + 1] - times[k]);
                    const float f = span > 0 ? (t - times[k]) / span : 0.0f;
                    return static_cast<float>(values[k]) + (static_cast<float>(values[k + 1]) - static_cast<float>(values[k])) * f;
                }
            return static_cast<float>(values.back());
        }

        void Simulate(FakeModel& m, float dt)
        {
            m.clock += dt;
            const float t = std::fmod(m.clock * 1000.0f, static_cast<float>(m.sequence));
            if (m.animateWeights)
                for (size_t k = 0; k < m.weightValues.size() && k < m.weightCount; ++k)
                    Put<float>(m.weights, k * 0x0C + 8, std::clamp(At(m.weightTimes[k], m.weightValues[k], t) / 32767.0f, 0.0f, 1.0f));
            for (size_t e = 0; e < m.sims.size(); ++e)
            {
                FakeModel::Sim& sim = m.sims[e];
                float rate = 0.0f;
                if (sim.rates.empty()) rate = 0.0f;   // (its first sequence has no keys: kept in the creature's .anim)
                else if (sim.rates.size() == 1 || sim.times.size() != sim.rates.size()) rate = sim.rates[0];
                else
                {
                    rate = sim.rates.back();
                    for (size_t k = 0; k + 1 < sim.times.size(); ++k)
                        if (t >= sim.times[k] && t <= sim.times[k + 1])
                        {
                            const float span = static_cast<float>(sim.times[k + 1] - sim.times[k]);
                            const float f = span > 0 ? (t - sim.times[k]) / span : 0.0f;
                            rate = sim.rates[k] + (sim.rates[k + 1] - sim.rates[k]) * f;
                            break;
                        }
                }
                for (float& d : sim.deaths) d -= dt;
                sim.deaths.erase(std::remove_if(sim.deaths.begin(), sim.deaths.end(), [](float d) { return d <= 0.0f; }), sim.deaths.end());
                sim.acc += std::max(0.0f, rate) * dt;
                while (sim.acc >= 1.0f)
                {
                    sim.acc -= 1.0f;
                    sim.deaths.push_back(sim.life * (1.0f + sim.lifeVariation * (2.0f * Rand01() - 1.0f)));
                }
                Put<uint32_t>(m.emitters[e], 0x50, static_cast<uint32_t>(sim.deaths.size()));
            }
        }
    };

    const li::FrameLight* Nearest(const std::vector<li::FrameLight>& ls, const float p[3])
    {
        const li::FrameLight* best = nullptr;
        float bd = 1e30f;
        for (const li::FrameLight& l : ls)
        {
            const float d = std::hypot(l.pos[0] - p[0], l.pos[1] - p[1], l.pos[2] - p[2]);
            if (d < bd) { bd = d; best = &l; }
        }
        return best;
    }

    void ResetFeature()
    {
        sp::Reset();
        sp::Config() = sl::Settings{};
        sp::Config().enabled = 1;
    }

    void TestTracker()
    {
        std::printf("-- the tracker over a fake client\n");
        ResetFeature();
        const float eye[3] = { -8.0f, -8.0f, 6.0f };
        {
            FakeClient c;
            const float at[3] = { 0.0f, 0.0f, 1.5f };
            // A Fireball in flight: its model made as a missile (0x40), at the scene's head and in the missile list.
            std::vector<li::FrameLight> out = c.Step(eye);
            FakeModel* fb = c.Make("Spells#Fireball_Missile_High.m2", at, sl::kFlagMissile);
            c.Missile(fb);
            for (int f = 0; f < 30; ++f) out = c.Step(eye);
            const sp::Stats& st = sp::GetStats();
            Check(st.missiles == 1 && st.inFlight == 1, F("a Fireball in flight is followed as a missile (%u missile(s), %u in the list)", st.missiles, st.inFlight));
            const li::FrameLight* l = Nearest(out, at);
            Check(l && std::hypot(l->pos[0] - at[0], l->pos[1] - at[1]) < 0.01f && l->pos[2] > at[2], "its light is at the model (a little above its origin)");
            Check(l && l->color[0] > l->color[1] && l->color[1] > l->color[2] && l->color[0] > 0.5f,
                  F("in the fire's colour, though its own light is white (%.2f %.2f %.2f; from the particles' tracks and textures)", l ? l->color[0] : 0, l ? l->color[1] : 0, l ? l->color[2] : 0));
            Check(st.textures > 0 && st.texturesWaiting == 0, F("its textures' glow colours read through the game's reader (%u known)", st.textures));
            Check(l && l->model == fb->m && l->indoor == 0, "the light names its model; outdoors (no indoor test here)");
            // It flies on: the light follows.
            const float on[3] = { 6.0f, 2.0f, 1.5f };
            c.Move(fb, on);
            out = c.Step(eye);
            l = Nearest(out, on);
            Check(l && std::hypot(l->pos[0] - on[0], l->pos[1] - on[1]) < 0.01f, "it follows the model the next frame");
            // It hits: unlinked from the scene, freed. Its light fades where it was.
            const float r0 = l ? sl::MaxChannel(l->color) : 0.0f;
            c.inFlight.clear();
            c.Unlink(fb);
            out = c.Step(eye);
            l = Nearest(out, on);
            Check(l && sl::MaxChannel(l->color) < r0 && sl::MaxChannel(l->color) > 0.5f * r0, F("gone from the scene: its light fades where it was (%.2f, from %.2f)", l ? sl::MaxChannel(l->color) : 0, r0));
            int frames = 1;
            while (!out.empty() && frames < 200) { out = c.Step(eye); ++frames; }
            Check(out.empty() && frames < 90 && sp::GetStats().tracked == 0, F("and is forgotten after %d frames (%.2f s)", frames, frames / 60.0f));
        }
        ResetFeature();
        {
            FakeClient c;
            std::vector<li::FrameLight> out = c.Step(eye);
            // A frost effect made this frame: at the scene's head.
            const float at[3] = { 2.0f, 0.0f, 1.0f };
            FakeModel* frost = c.Make("Spells#Frostbolt.m2", at);
            for (int f = 0; f < 20; ++f) out = c.Step(eye);
            Check(sp::GetStats().effects == 1, "a new effect at the scene's head is found by the walk");
            const li::FrameLight* l = Nearest(out, at);
            Check(l && l->color[2] > l->color[0], F("frost is blue (%.2f %.2f %.2f)", l ? l->color[0] : 0, l ? l->color[1] : 0, l ? l->color[2] : 0));
            const float full = l ? sl::MaxChannel(l->color) : 0.0f;
            // A particle effect (the green Lava Burst: particles alone): its particles die out, the light with them.
            const float lavaAt[3] = { 12.0f, -4.0f, 1.0f };
            FakeModel* lava = c.Make("spells#shaman_lavaburst_green_missile.m2", lavaAt);
            for (int f = 0; f < 20; ++f) out = c.Step(eye);
            const li::FrameLight* lv = Nearest(out, lavaAt);
            const float lavaFull = lv ? sl::MaxChannel(lv->color) : 0.0f;
            for (uint8_t* em : lava->emitters) Put<uint32_t>(em, 0x50, 2u);
            for (int f = 0; f < 90; ++f) out = c.Step(eye);
            lv = Nearest(out, lavaAt);
            const float lavaFew = lv && std::hypot(lv->pos[0] - lavaAt[0], lv->pos[1] - lavaAt[1]) < 0.1f ? sl::MaxChannel(lv->color) : 0.0f;
            for (uint8_t* em : lava->emitters) Put<uint32_t>(em, 0x50, 0u);
            for (int f = 0; f < 90; ++f) out = c.Step(eye);
            lv = Nearest(out, lavaAt);
            const bool lavaDark = !lv || std::hypot(lv->pos[0] - lavaAt[0], lv->pos[1] - lavaAt[1]) > 0.1f;
            Check(lavaFew < 0.6f * lavaFull && lavaFew > 0.0f && lavaDark,
                  F("a particle effect's particles dying out dim it (%.2f of %.2f with a twentieth alive), none alive: dark", lavaFew, lavaFull));
            // The frost bolt's particles gone: its bolt (its geometry and ribbons) still glows.
            for (uint8_t* em : frost->emitters) Put<uint32_t>(em, 0x50, 0u);
            for (int f = 0; f < 60; ++f) out = c.Step(eye);
            l = Nearest(out, at);
            const float bolt = l ? sl::MaxChannel(l->color) : 0.0f;
            Check(bolt > 0.3f * full, F("the Frostbolt's particles gone, its bolt (geometry, ribbons) still glows (%.2f of %.2f)", bolt, full));
            for (uint32_t k = 0; k < frost->weightCount; ++k) Put<float>(frost->weights, k * 0x0C + 8, 0.0f);
            const uint8_t* ribbonStates = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(Take<uint32_t>(frost->m, 0x2B8)));
            for (uint32_t k = 0; k < 2; ++k) Put<uint8_t>(const_cast<uint8_t*>(ribbonStates), k * 0x50 + 0x4C, 0);
            for (int f = 0; f < 60; ++f) out = c.Step(eye);
            l = Nearest(out, at);
            Check(!l || sl::MaxChannel(l->color) < 0.01f, F("... until the client makes its textures transparent (texture weights 0) and its ribbons go: dark (%.3f)", l ? sl::MaxChannel(l->color) : 0.0f));
            for (uint32_t k = 0; k < frost->weightCount; ++k) Put<float>(frost->weights, k * 0x0C + 8, 1.0f);
            // Alive again, the client fading the model out (effective alpha).
            for (uint8_t* em : frost->emitters) Put<uint32_t>(em, 0x50, 40u);
            for (int f = 0; f < 30; ++f) out = c.Step(eye);
            Put<float>(frost->m, 0x198, 0.25f);
            for (int f = 0; f < 60; ++f) out = c.Step(eye);
            l = Nearest(out, at);
            Check(l && sl::MaxChannel(l->color) < 0.35f * full, F("its alpha at a quarter: a quarter of the light (%.2f)", l ? sl::MaxChannel(l->color) : 0));
            Put<float>(frost->m, 0x198, 1.0f);
            // Off the screen: the client doesn't animate it, it gives no light (after kHoldSeconds: a far model is
            // animated every few frames).
            frost->shown = false;
            for (int f = 0; f < 15; ++f) out = c.Step(eye);
            const li::FrameLight* held = Nearest(out, at);
            const bool stillThere = held && std::hypot(held->pos[0] - at[0], held->pos[1] - at[1]) < 0.1f && sl::MaxChannel(held->color) > 0.5f * full;
            for (int f = 0; f < 120; ++f) out = c.Step(eye);
            Check(stillThere && (!Nearest(out, at) || std::hypot(Nearest(out, at)->pos[0] - at[0], Nearest(out, at)->pos[1] - at[1]) > 0.1f),
                  F("not animated by the client for a quarter second: still lit; for two seconds (culled): no light (hold %.1f s)", sp::kHoldSeconds));
            const unsigned followed = sp::GetStats().tracked;
            frost->shown = true;
            // The slot handed out again: the same address, another model (a new loop origin).
            c.Unlink(frost);
            const float there[3] = { -3.0f, 4.0f, 1.0f };
            Put<uint32_t>(frost->m, 0x74, c.time++);
            float w[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, there[0], there[1], there[2], 1 };
            std::memcpy(frost->m + 0xB4, w, sizeof(w));
            c.list.insert(c.list.begin(), frost);
            c.Link();
            for (int f = 0; f < 20; ++f) out = c.Step(eye);
            l = Nearest(out, there);
            Check(l && std::hypot(l->pos[0] - there[0], l->pos[1] - there[1]) < 0.01f && sp::GetStats().tracked == followed,
                  F("a slot handed out again (same address, new loop origin) is a new model, found at the head (%u followed, as before)", sp::GetStats().tracked));
        }
        ResetFeature();
        {
            FakeClient c;
            std::vector<li::FrameLight> out = c.Step(eye);
            const float at[3] = { 0.0f, 3.0f, 1.0f };
            c.Make("Spells#Fireball_Missile_High.m2", at, sl::kFlagMapDoodad);       // a placed map doodad with a spell's file
            const float at2[3] = { 3.0f, 3.0f, 1.0f };
            c.Make("Spells#Holy_Missile_High.m2", at2);
            for (int f = 0; f < 20; ++f) out = c.Step(eye);
            Check(sp::GetStats().tracked == 1 && out.size() == 1, F("a map doodad with a spell's file is left to the doodad lights (%u followed)", sp::GetStats().tracked));
            const li::FrameLight* l = Nearest(out, at2);
            Check(l && l->color[0] >= l->color[2] && l->color[1] >= l->color[2], F("holy is warm white-gold (%.2f %.2f %.2f)", l ? l->color[0] : 0, l ? l->color[1] : 0, l ? l->color[2] : 0));
            // Offered by the light scan (an effect that was there before the walk's first frame).
            FakeModel* old = c.Make("spells#fel_fireball_missile_high.m2", at, 0);
            c.Unlink(old);       // not at the head: only the scan meets it
            c.list.push_back(old);
            c.Link();
            Check(sp::OfferFromScan(old->m), "the light scan's offer: a spell visual, its lights left to this feature");
            const float doodadAt[3] = { 9, 9, 0 };
            FakeModel* doodad = c.Make("Spells#Fireball_Missile_High.m2", doodadAt, sl::kFlagMapDoodad);
            Check(!sp::OfferFromScan(doodad->m), "... a map doodad's: kept by the scan");
            const float greenAt[3] = { 6.0f, -3.0f, 1.0f };
            c.Make("spells#shaman_lavaburst_green_missile.m2", greenAt, sl::kFlagMissile);
            for (int f = 0; f < 20; ++f) out = c.Step(eye);
            l = Nearest(out, at);
            Check(l && sl::Saturation(l->color) > 0.3f && l->color[1] >= l->color[0], F("Fel Fireball's own light is white: its light takes its particles' hue, leant to fel green (%.2f %.2f %.2f)",
                                                        l ? l->color[0] : 0, l ? l->color[1] : 0, l ? l->color[2] : 0));
            l = Nearest(out, greenAt);
            Check(l && l->color[1] > l->color[0] && l->color[1] > l->color[2], F("the green Lava Burst is green (%.2f %.2f %.2f)", l ? l->color[0] : 0, l ? l->color[1] : 0, l ? l->color[2] : 0));
            // Attached to a moving unit: follows its parent.
            const float unitAt[3] = { -4.0f, 0.0f, 0.0f };
            FakeModel* unit = nullptr;
            c.Enchant("spells#enchantments#greenglow_high.m2", unitAt, &unit);
            for (int f = 0; f < 20; ++f) out = c.Step(eye);
            l = Nearest(out, unitAt);
            Check(l && std::hypot(l->pos[0] - unitAt[0], l->pos[1] - unitAt[1]) < 0.01f, "an enchant's glow on a weapon follows its parent");
            const float unitOn[3] = { -4.0f, -5.0f, 0.0f };
            c.Move(unit, unitOn);
            out = c.Step(eye);
            l = Nearest(out, unitOn);
            Check(l && std::hypot(l->pos[0] - unitOn[0], l->pos[1] - unitOn[1]) < 0.01f, "... as it moves");
            Check(l && l->radius < 0.6f * sp::Config().radius && sl::MaxChannel(l->color) < 0.6f && l->color[1] > l->color[0],
                  F("an enchant: green, small (%.1f yd) and weak (%.2f)", l ? l->radius : 0, l ? sl::MaxChannel(l->color) : 0));
            sp::Config().enchants = 0;
            sp::Reset();
            for (int f = 0; f < 5; ++f) out = c.Step(eye);
            Check(Nearest(out, unitOn) == nullptr || std::hypot(Nearest(out, unitOn)->pos[0] - unitOn[0], Nearest(out, unitOn)->pos[1] - unitOn[1]) > 1.0f,
                  "Enchants 0: the enchant's glow gives none");
            // The feature off: nothing offered, nothing drawn.
            sp::Config().enabled = 0;
            Check(!sp::OfferFromScan(old->m), "off: the scan keeps every model's lights (as before)");
            std::vector<li::FrameLight> none;
            wxl_livingazeroth::world::Snapshot snap;
            snap.inWorld = true;
            sp::Update(1.0f / 60.0f, snap, eye, none);
            Check(none.empty() && sp::GetStats().tracked == 0, "off: Update adds no light and follows nothing");
        }
        ResetFeature();
    }

    // env/Lights (Finish), through UpdateForTests: the scene's lights by its rules (the candle ring's), spell lights (frame
    // lights) apart from them: no merge with a torch, their own room in a full cell of the grid.
    void TestLightsMerge()
    {
        std::printf("-- env/Lights' merge and grid: the scene's lights and spell lights\n");
        const float eye[3] = { 0.0f, 0.0f, 5.0f };
        std::vector<li::FrameLight> candles;
        for (int k = 0; k < 7; ++k)
        {
            li::FrameLight f{};
            f.pos[0] = 0.12f * k; f.pos[1] = 0.0f; f.pos[2] = 0.5f;
            f.color[0] = 0.76f; f.color[1] = 0.61f; f.color[2] = 0.24f;
            f.radius = 9.5f;
            f.model = reinterpret_cast<const void*>(static_cast<uintptr_t>(0x5000 + k * 16));
            candles.push_back(f);
        }
        li::UpdateForTests(eye, 1.0f / 60.0f, {}, candles);
        Check(li::Active().size() == 1 && std::fabs(li::Active()[0].color[0] - 0.76f * std::sqrt(7.0f)) < 0.01f,
              F("the scene's 7 small flames within a yard: one light at sqrt(7) of their sum (%.3f = %.2f x one), the candle ring's rule (c5ec800)",
                li::Active().empty() ? 0.0f : li::Active()[0].color[0], li::Active().empty() ? 0.0f : li::Active()[0].color[0] / 0.76f));
        for (auto& f : candles) f.radius = 18.0f;
        li::UpdateForTests(eye, 1.0f / 60.0f, {}, candles);
        Check(li::Active().size() == 1 && std::fabs(li::Active()[0].color[0] - 0.76f * 7.0f) < 0.01f, "the scene's torches (reach > 10 yd) within a yard: their sum, as before");
        // A spell light beside a torch: merged and capped by its own rules already, it stays apart (a missile passing
        // would otherwise pull the torch's light along with it).
        {
            std::vector<li::FrameLight> torch(1, candles[0]), spell(1, candles[1]);
            spell[0].pos[0] = 0.5f;
            li::UpdateForTests(eye, 1.0f / 60.0f, spell, torch);
            Check(li::Active().size() == 2 && std::fabs(li::Active()[0].pos[0] - candles[0].pos[0]) < 1e-4f,
                  F("a spell light half a yard from a torch: both drawn, the torch where it is (%zu lights)", li::Active().size()));
        }
        // A full cell: 20 torches in one 16-yd cell (16 places for the scene's) and 4 spell lights there: every spell light
        // listed in its own room, the same 16 torches as without them.
        {
            std::vector<li::FrameLight> torches, spells;
            for (int k = 0; k < 20; ++k)
            {
                li::FrameLight f{};
                f.pos[0] = 1.0f + 3.0f * (k % 5); f.pos[1] = 1.0f + 3.0f * (k / 5); f.pos[2] = 2.0f;
                f.color[0] = 0.4f + 0.05f * k; f.color[1] = 0.3f; f.color[2] = 0.1f;
                f.radius = 6.0f;
                f.model = reinterpret_cast<const void*>(static_cast<uintptr_t>(0x8000 + k * 16));
                torches.push_back(f);
            }
            for (int k = 0; k < 4; ++k)
            {
                li::FrameLight f{};
                f.pos[0] = 2.5f + 3.0f * k; f.pos[1] = 8.5f; f.pos[2] = 1.0f;
                f.color[0] = 0.05f; f.color[1] = 0.1f; f.color[2] = 0.3f;   // dim: a cell ranked by brightness would drop them
                f.radius = 4.0f;
                f.model = reinterpret_cast<const void*>(static_cast<uintptr_t>(0x9000 + k * 16));
                spells.push_back(f);
            }
            const float cellEye[3] = { 8.0f, 8.0f, 5.0f };
            auto listed = [&]()
            {
                const li::Grid& g = li::CellGrid();
                const int cx = static_cast<int>(std::floor((8.0f - g.originX) / g.cellSize)), cy = static_cast<int>(std::floor((8.0f - g.originY) / g.cellSize));
                const size_t cell = static_cast<size_t>(cy) * g.cells + cx;
                std::vector<const void*> models;
                for (int k = 0; k < g.count[cell]; ++k) models.push_back(li::Active()[g.index[cell * g.perCell + k]].model);
                std::sort(models.begin(), models.end());
                return models;
            };
            li::UpdateForTests(cellEye, 1.0f / 60.0f, {}, torches);
            const std::vector<const void*> alone = listed();
            li::UpdateForTests(cellEye, 1.0f / 60.0f, spells, torches);
            std::vector<const void*> both = listed(), torchesThere;
            unsigned spellsThere = 0;
            for (const void* m : both)
            {
                if (reinterpret_cast<uintptr_t>(m) >= 0x9000) ++spellsThere;
                else torchesThere.push_back(m);
            }
            Check(alone.size() == 16 && spellsThere == 4 && torchesThere == alone && li::CellGrid().perCell == 16 + li::SpellSlotsPerCell(),
                  F("a cell of 20 torches (16 listed) and 4 dim spell lights: all 4 listed in their own room (%d a cell), the same 16 torches", li::SpellSlotsPerCell()));
        }
        std::vector<li::FrameLight> apart;
        for (int k = 0; k < 4; ++k)
        {
            li::FrameLight f{};
            f.pos[0] = 6.0f * k; f.pos[2] = 1.0f;
            f.color[0] = 1.0f; f.color[1] = 0.5f; f.color[2] = 0.2f;
            f.radius = 8.0f;
            f.indoor = k == 3 ? 1 : 0;
            f.model = reinterpret_cast<const void*>(static_cast<uintptr_t>(0x6000 + k * 16));
            apart.push_back(f);
        }
        li::UpdateForTests(eye, 1.0f / 60.0f, apart);
        bool steady = li::Active().size() == 4;
        for (const li::ActiveLight& a : li::Active()) steady = steady && a.dip == 0.0f && a.occlusion == nullptr && std::fabs(a.color[0] - 1.0f) < 1e-4f;
        Check(steady, "spell lights apart: each drawn, never flickering, never baked");
        Check(li::Active().size() == 4 && li::Active()[3].indoor && !li::Active()[0].indoor, "their indoor verdict as given (no indoor test of the client's)");
        const li::Grid& g = li::CellGrid();
        Check(g.cells > 0 && li::GetStats().frameLights == 4, F("in the grid (%d x %d cells), counted (%u spell lights)", g.cells, g.cells, li::GetStats().frameLights));
        const float under[3] = { 0.0f, 0.0f, 0.0f }, up[3] = { 0.0f, 0.0f, 1.0f };
        const li::LightAt at = li::SumAt(under, up);
        Check(at.reaching >= 1 && at.sum[0] > at.sum[2], F("on the floor under the first: lit in its colour (%.2f %.2f %.2f)", at.sum[0], at.sum[1], at.sum[2]));
    }

    // --- 3. the guards ----------------------------------------------------------------------------------------------------
    struct Image
    {
        std::vector<uint8_t> file;
        struct Section { uint32_t va, vsize, raw, rsize; };
        std::vector<Section> sections;
        uint32_t base = 0;
        bool Load(const fs::path& p)
        {
            if (!ReadAll(p, file) || file.size() < 0x400) return false;
            const uint32_t pe = Take<uint32_t>(file.data(), 0x3C);
            const uint16_t n = Take<uint16_t>(file.data(), pe + 6), opt = Take<uint16_t>(file.data(), pe + 20);
            base = Take<uint32_t>(file.data(), pe + 24 + 28);
            for (uint16_t k = 0; k < n; ++k)
            {
                const size_t at = pe + 24 + opt + k * 40;
                sections.push_back({ base + Take<uint32_t>(file.data(), at + 12), Take<uint32_t>(file.data(), at + 8), Take<uint32_t>(file.data(), at + 20), Take<uint32_t>(file.data(), at + 16) });
            }
            return true;
        }
        static const uint8_t* At(uintptr_t va, size_t size, void* user)
        {
            const Image* im = static_cast<const Image*>(user);
            for (const Section& s : im->sections)
                if (va >= s.va && va + size <= s.va + std::max(s.vsize, s.rsize) && s.raw + (va - s.va) + size <= im->file.size())
                    return im->file.data() + s.raw + (va - s.va);
            return nullptr;
        }
    };

    void TestGuards(const std::vector<std::string>& exes)
    {
        std::printf("-- the guards against the client\n");
        size_t n = 0;
        sp::Guards(n);
        for (const std::string& e : exes)
        {
            Image im;
            if (!im.Load(e)) { Check(false, "read " + e); continue; }
            std::string why;
            Check(sp::GuardsHold(&Image::At, &im, why), F("%zu guards hold in %s%s", n, fs::path(e).filename().string().c_str(), why.empty() ? "" : (": " + why).c_str()));
            // A changed byte is noticed.
            Image bad = im;
            const sp::Guard* gs = sp::Guards(n);
            const uint8_t* p = Image::At(gs[4].va, 1, &bad);
            if (p) bad.file[p - bad.file.data()] ^= 0xFF;
            Check(!sp::GuardsHold(&Image::At, &bad, why) && why.find("0x00834550") != std::string::npos, "a changed instruction stops the feature: " + why);
        }
        if (exes.empty()) Info("no exe given: guards not checked");
    }

    // --- 4. cost --------------------------------------------------------------------------------------------------------------
    // One scene's cost: `visuals` spell visuals (a fifth of them missiles in flight, a tenth enchants) among `plain` map
    // doodads, over 240 frames with `churn` new effects and as many gone each frame. Times by the thread's own cycles
    // (QueryThreadCycleTime): on a core shared at Idle priority the wall clock also counts other threads' turns.
    struct Cost { double median = 0, p95 = 0, worst = 0, wallMedian = 0, lightsMedian = 0, offerNs = 0, findMedian = 0, modelsMedian = 0, mergeMedian = 0; sp::Stats st; };
    Cost RunCost(int plain, int visuals, int churn, double perMs)
    {
        ResetFeature();
        FakeClient c;
        const float eye[3] = { 0.0f, 0.0f, 3.0f };
        const std::vector<std::string> spells = { "Spells#Fireball_Missile_High.m2", "Spells#Frostbolt.m2", "Spells#Holy_Missile_High.m2",
                                                  "Spells#Arcane_Missile_Lvl3.m2", "spells#fel_fireball_missile_high.m2", "Spells#Shadowbolt_Missile.m2",
                                                  "Spells#Blizzard_Impact_Base.m2", "spells#consecration_impact_base.m2" };
        std::vector<FakeModel*> plains;
        for (int k = 0; k < plain; ++k)
        {
            const float p[3] = { static_cast<float>((k % 200) * 4 - 400), static_cast<float>((k / 200) * 4 - 200), 0.0f };
            plains.push_back(c.Make("Spells#Holy_Precast_Low_Hand.m2", p, sl::kFlagMapDoodad, 1.0f, false));
        }
        c.Link();
        std::vector<li::FrameLight> out = c.Step(eye);
        auto spot = [](int k, float p[3])
        {
            const float a = k * 2.39996f, r = 3.0f + 45.0f * std::sqrt((k % 997 + 0.5f) / 997.0f);
            p[0] = r * std::cos(a); p[1] = r * std::sin(a); p[2] = 1.0f + (k % 3);
        };
        for (int k = 0; k < visuals; ++k)
        {
            float p[3];
            spot(k, p);
            const bool missile = k % 5 == 0, enchant = k % 10 == 3;
            FakeModel* m = c.Make(enchant ? "spells#enchantments#redglow_high.m2" : spells[k % spells.size()], p, missile ? sl::kFlagMissile : 0);
            if (missile) c.Missile(m);
        }
        for (int f = 0; f < 30; ++f) out = c.Step(eye);   // the textures read, a couple a frame
        auto cycles = [] { ULONG64 v = 0; QueryThreadCycleTime(GetCurrentThread(), &v); return static_cast<double>(v); };
        std::vector<double> ms, wall, lightsMs, find, models, merge;
        int next = visuals;
        for (int f = 0; f < 240; ++f)
        {
            for (int k = 0; k < churn; ++k)
            {
                float p[3];
                spot(next, p);
                c.Make(spells[next % spells.size()], p);
                ++next;
                c.Unlink(c.list[c.list.size() - plain - 1]);   // the oldest spell visual goes
            }
            ++c.frame;
            Put<uint32_t>(c.scene, 0x14, c.frame);
            for (auto& m : c.models) if (m->shown) Put<uint32_t>(m->m, 0x3C, c.frame);
            out.clear();
            const auto w0 = std::chrono::steady_clock::now();
            const double c0 = cycles();
            sp::FrameForTests(c.View(), 1.0f / 60.0f, eye, out);
            const double c1 = cycles();
            li::UpdateForTests(eye, 1.0f / 60.0f, out);
            const double c2 = cycles();
            wall.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - w0).count());
            ms.push_back((c1 - c0) / perMs);
            lightsMs.push_back((c2 - c1) / perMs);
            find.push_back(sp::GetStats().msFind);
            models.push_back(sp::GetStats().msModels);
            merge.push_back(sp::GetStats().msMerge);
        }
        std::sort(ms.begin(), ms.end());
        std::sort(wall.begin(), wall.end());
        std::sort(lightsMs.begin(), lightsMs.end());
        Cost r;
        r.median = ms[ms.size() / 2]; r.p95 = ms[ms.size() * 95 / 100]; r.worst = ms.back();
        for (std::vector<double>* v : { &find, &models, &merge }) std::sort(v->begin(), v->end());
        r.findMedian = find[find.size() / 2]; r.modelsMedian = models[models.size() / 2]; r.mergeMedian = merge[merge.size() / 2];
        r.wallMedian = wall[wall.size() / 2]; r.lightsMedian = lightsMs[lightsMs.size() / 2];
        r.st = sp::GetStats();
        if (!plains.empty())
        {
            const double o0 = cycles();
            unsigned claimed = 0;
            for (FakeModel* m : plains) claimed += sp::OfferFromScan(m->m) ? 1 : 0;
            r.offerNs = (cycles() - o0) / perMs * 1e6 / plains.size();
            Check(claimed == 0, "none of the map's doodads claimed by the scan's offers");
        }
        ResetFeature();
        return r;
    }

    void TestCost()
    {
        std::printf("-- cost (a city's models, a raid's spells)\n");
        auto cycles = [] { ULONG64 v = 0; QueryThreadCycleTime(GetCurrentThread(), &v); return static_cast<double>(v); };
        double perMs = 0.0;
        for (int t = 0; t < 5; ++t)
        {
            const double c0 = cycles();
            const auto w0 = std::chrono::steady_clock::now();
            volatile double x = 0;
            while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - w0).count() < 20.0) x = x + 1.0;
            perMs = std::max(perMs, (cycles() - c0) / std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - w0).count());
        }
        const Cost raid = RunCost(20000, 300, 4, perMs);
        Info(F("a raid in a city (20,000 models, %u spell visuals followed, %u missiles in flight, 4 new and 4 gone a frame): spell lights %.3f ms a frame (median; 95th %.3f, worst %.3f; the thread's own time at %.2f GHz, the wall clock's median %.3f)",
               raid.st.tracked, raid.st.inFlight, raid.median, raid.p95, raid.worst, perMs / 1e6, raid.wallMedian));
        Info(F("   %u drawn by the client, the nearest %u looked at, %u in range, %u merged into %u, %u over the cap, %u drawn; the steps (medians, wall clock): finding %.3f, the models %.3f, merge and cap %.3f",
               raid.st.shown, raid.st.looked, raid.st.inRange, raid.st.merged, raid.st.clusters, raid.st.dropped, raid.st.drawn, raid.findMedian, raid.modelsMedian, raid.mergeMedian));
        Info(F("   env/Lights' merge, drawn list and grid over them %.3f ms; the light scan's offer %.0f ns a model (a city's 20,000: %.1f ms a pass, spread over its slices)",
               raid.lightsMedian, raid.offerNs, raid.offerNs * 20000 / 1e6));
        Check(raid.median < 0.75 && raid.st.drawn - raid.st.leaving <= static_cast<unsigned>(sp::Config().maxLights) &&
              raid.st.drawn <= 2u * static_cast<unsigned>(sp::Config().maxLights) && raid.st.drawn > 0,
              F("a raid's spells: %.2f ms a frame, the cap holding (%u drawn: %u kept, %u fading out of the set; %u waiting for room in the grid)",
                raid.median, raid.st.drawn, raid.st.drawn - raid.st.leaving, raid.st.leaving, raid.st.waiting));
        const Cost pull = RunCost(5000, 40, 1, perMs);
        Info(F("a dungeon pull (5,000 models, %u spell visuals, 1 new and 1 gone a frame): %.3f ms a frame (median; worst %.3f); %u drawn",
               pull.st.tracked, pull.median, pull.worst, pull.st.drawn));
        Check(pull.median < 0.3, F("a dungeon pull's spells: %.3f ms a frame", pull.median));
        Check(raid.offerNs < 300.0, F("the light scan's offer: %.0f ns a model", raid.offerNs));
    }

    // --- 6. flicker traces --------------------------------------------------------------------------------------------------
    // Our light on the floor at one spot, frame by frame (env/Lights SumAt: the grid cell's list there, the shaders' sum,
    // through the knee), with the particles born and dying as the files say: steady effects (their light should be
    // steady), effects whose glow breathes by design or a light moving a little (it should follow smoothly), a busy area
    // spell (no frame-to-frame zigzag), a missile passing (one smooth rise and fall).
    enum class TraceKind { Steady, Smooth, Busy, Moving };
    struct Trace { std::string name; std::vector<float> b; TraceKind kind = TraceKind::Steady; unsigned turns = 0; };
    struct TraceStats { float mean = 0, relStd = 0, maxJump = 0; unsigned jumps = 0, reversals = 0, zigzags = 0, frames = 0; };
    // jumps: frame-to-frame changes over 2% of the mean (of the rise above the trace's least, for a missile passing);
    // reversals: changes of direction (steps over 0.2%); zigzags: a step up and down again (or down and up) over 1% each,
    // within two frames: flicker.
    TraceStats Measure(const Trace& t)
    {
        TraceStats r;
        r.frames = static_cast<unsigned>(t.b.size());
        if (t.b.size() < 3) return r;
        double sum = 0, sq = 0;
        for (float v : t.b) { sum += v; sq += v * v; }
        r.mean = static_cast<float>(sum / t.b.size());
        r.relStd = r.mean > 1e-6f ? static_cast<float>(std::sqrt(std::max(0.0, sq / t.b.size() - (sum / t.b.size()) * (sum / t.b.size()))) / r.mean) : 0.0f;
        int lastSign = 0;
        const float peak = *std::max_element(t.b.begin(), t.b.end()), least = *std::min_element(t.b.begin(), t.b.end());
        const float scale = t.kind == TraceKind::Moving ? peak - least : r.mean;
        std::vector<float> d(t.b.size(), 0.0f);
        for (size_t k = 1; k < t.b.size(); ++k)
        {
            d[k] = t.b[k] - t.b[k - 1];
            if (scale > 1e-6f) r.maxJump = std::max(r.maxJump, std::fabs(d[k]) / scale);
            if (scale > 1e-6f && std::fabs(d[k]) > 0.02f * scale) ++r.jumps;
            const int sign = std::fabs(d[k]) > 0.002f * scale ? (d[k] > 0 ? 1 : -1) : 0;
            if (sign && lastSign && sign != lastSign) ++r.reversals;
            if (sign) lastSign = sign;
        }
        for (size_t k = 2; k < t.b.size(); ++k)
        {
            const float big = 0.01f * scale;
            const bool updown = std::fabs(d[k]) > big && std::fabs(d[k - 1]) > big && (d[k] > 0) != (d[k - 1] > 0);
            const bool across = k >= 3 && std::fabs(d[k]) > big && std::fabs(d[k - 2]) > big && (d[k] > 0) != (d[k - 2] > 0) && std::fabs(d[k - 1]) <= big;
            if (scale > 1e-6f && (updown || across)) ++r.zigzags;
        }
        return r;
    }

    float FloorLight(const float p[3])
    {
        const float up[3] = { 0.0f, 0.0f, 1.0f };
        const li::LightAt a = li::SumAt(p, up);
        return sl::MaxChannel(a.kneed);
    }

    // env/Lights' frame over this frame's spell lights and the scene's own (torches) where a scenario has them.
    void UpdateLights(const float eye[3], float dt, const std::vector<li::FrameLight>& spells, const std::vector<li::FrameLight>& scene)
    {
        li::UpdateForTests(eye, dt, spells, scene);
    }

    // One scenario: `setup` makes the models, `each` moves things a frame; `frames` recorded after a warm-up (1 s).
    template <class Setup, class Each>
    Trace RunTrace(const char* name, const float spot[3], TraceKind kind, int frames, Setup setup, Each each, int warm = 60,
                   const std::vector<li::FrameLight>& scene = {})
    {
        ResetFeature();
        g_rng = 12345;
        FakeClient c;
        const float eye[3] = { -10.0f, -12.0f, 8.0f };
        c.Step(eye);
        setup(c);
        Trace t{ name, {}, kind };
        const float dt = 1.0f / 60.0f;
        for (int f = 0; f < warm + frames; ++f)
        {
            each(c, f, dt);
            std::vector<li::FrameLight> out = c.Step(eye, dt);
            UpdateLights(eye, dt, out, scene);
            if (f >= warm) t.b.push_back(FloorLight(spot));
        }
        ResetFeature();
        return t;
    }

    // Twenty torches (the scene's own lights) over one 16-yd cell of the grid: more than its 16 places for them.
    std::vector<li::FrameLight> Torches()
    {
        std::vector<li::FrameLight> torches;
        for (int k = 0; k < 20; ++k)
        {
            li::FrameLight f{};
            f.pos[0] = 1.0f + 3.5f * (k % 5); f.pos[1] = 1.0f + 3.5f * (k / 5); f.pos[2] = 2.5f;
            f.color[0] = 0.9f + 0.08f * k; f.color[1] = 0.65f; f.color[2] = 0.3f;
            f.radius = 8.0f;
            f.model = reinterpret_cast<const void*>(static_cast<uintptr_t>(0xA000 + k * 16));
            torches.push_back(f);
        }
        return torches;
    }

    // Consecration's ground glow held at full (its texture weights at 1: the file breathes them 1 .. 0.4 by design).
    FakeModel* Consecration(FakeClient& c, const float at[3], int cadence = 1, bool breathing = false)
    {
        FakeModel* m = c.Make("spells#consecration_impact_base.m2", at);
        for (uint32_t k = 0; k < m->weightCount; ++k) Put<float>(m->weights, k * 0x0C + 8, 1.0f);
        m->cadence = cadence;
        m->animateWeights = breathing;
        c.StartSimulation(m);
        return m;
    }

    void TestFlicker(const fs::path& traceFolder)
    {
        std::printf("-- flicker: our light on the floor, frame by frame (particles born and dying as the files say)\n");
        std::vector<Trace> traces;
        const float origin[3] = { 0.0f, 0.0f, 0.0f };
        auto none = [](FakeClient&, int, float) {};
        const float spot[3] = { 2.0f, 1.0f, 0.0f };
        // Consecration: one ground effect, its glow held; the client animating it every frame, every 3rd, every 6th (a
        // distant or busy model).
        traces.push_back(RunTrace("Consecration, its glow held", spot, TraceKind::Steady, 240, [&](FakeClient& c) { Consecration(c, origin); }, none, 150));
        traces.push_back(RunTrace("... the client animating it every 3rd frame", spot, TraceKind::Steady, 240, [&](FakeClient& c) { Consecration(c, origin, 3); }, none, 150));
        traces.push_back(RunTrace("... every 6th frame (far off)", spot, TraceKind::Steady, 240, [&](FakeClient& c) { Consecration(c, origin, 6); }, none, 150));
        // Its glow breathing as its file says (1 .. 0.4 and back every 2 s): followed smoothly, turning where it turns.
        {
            Trace t = RunTrace("Consecration breathing as its file says (1 .. 0.4, 2 s)", spot, TraceKind::Smooth, 240,
                               [&](FakeClient& c) { Consecration(c, origin, 1, true); }, none, 150);
            t.turns = 4;
            traces.push_back(t);
        }
        // ... among twenty torches over one cell of the grid (the scene's 16 places full).
        {
            Trace t = RunTrace("... among 20 torches (a full cell of the grid)", spot, TraceKind::Smooth, 240,
                               [&](FakeClient& c) { Consecration(c, origin, 1, true); }, none, 150, Torches());
            t.turns = 4;
            traces.push_back(t);
        }
        // Rain of Fire: its rune on the ground and an impact every 0.3 s within 6 yd, each gone after its 3.3 s (busy by
        // design: meteors land).
        {
            const float at[3] = { 1.0f, 1.0f, 0.0f };
            std::vector<std::pair<FakeModel*, int>> impacts;
            traces.push_back(RunTrace("Rain of Fire (rune + impacts)", at, TraceKind::Busy, 300, [&](FakeClient& c)
            {
                FakeModel* rune = c.Make("Spells#SummonGround_RainofFire.m2", origin);
                rune->animateWeights = true;
                c.StartSimulation(rune);
            }, [&](FakeClient& c, int f, float)
            {
                if (f % 18 == 0)
                {
                    const float a = Rand01() * 6.2832f, r = 6.0f * std::sqrt(Rand01());
                    const float p[3] = { r * std::cos(a), r * std::sin(a), 0.0f };
                    FakeModel* m = c.Make("Spells#RainOfFire_Impact_Base.m2", p);
                    c.StartSimulation(m);
                    impacts.push_back({ m, f });
                }
                for (auto it = impacts.begin(); it != impacts.end();)
                    if (f - it->second > 198) { c.Unlink(it->first); it = impacts.erase(it); }
                    else ++it;
            }));
        }
        // Weapon enchants on a standing player: Crusader-like red glow (steady), Mongoose (its sparks in bursts about once a
        // second, by design: followed smoothly).
        for (const char* file : { "spells#enchantments#redglow_high.m2", "Spells#Enchantments#MongooseGlow_High.m2" })
        {
            const float at[3] = { 1.0f, 0.0f, 0.0f };
            const bool red = std::strstr(file, "red") != nullptr;
            Trace t = RunTrace(red ? "enchant RedGlow" : "enchant Mongoose (its sparks in bursts)", at, red ? TraceKind::Steady : TraceKind::Smooth, 240,
                               [&](FakeClient& c)
            {
                const float hand[3] = { 0.3f, 0.0f, 1.1f };
                c.StartSimulation(c.Enchant(file, hand));
            }, none);
            t.turns = 8;
            traces.push_back(t);
        }
        // Two auras on one unit, 1.4 .. 1.6 yd apart as it breathes (around the merge distance): the light moves a little.
        {
            const float at[3] = { 0.5f, 0.0f, 0.0f };
            FakeModel* b = nullptr;
            Trace t = RunTrace("two auras breathing round the merge distance", at, TraceKind::Smooth, 240, [&](FakeClient& c)
            {
                const float pa[3] = { 0.0f, 0.0f, 0.5f }, pb[3] = { 1.5f, 0.0f, 0.5f };
                FakeModel* a = c.Make("spells#enchantments#redglow_high.m2", pa);
                b = c.Make("Spells#Holy_Missile_High.m2", pb);
                c.StartSimulation(a);
                c.StartSimulation(b);
            }, [&](FakeClient& c, int f, float)
            {
                const float pb[3] = { 1.5f + 0.1f * std::sin(f * 0.21f), 0.0f, 0.5f };
                if (b) c.Move(b, pb);
            });
            t.turns = 16;   // its own sway: 8 periods
            traces.push_back(t);
        }
        // A crowd round a Consecration: 30 players, each with two enchants, swaying a little: well over the cap of 24.
        {
            std::vector<FakeModel*> units;
            Trace t = RunTrace("Consecration in a crowd of 30 enchanted players (over the cap)", spot, TraceKind::Smooth, 240, [&](FakeClient& c)
            {
                Consecration(c, origin);
                for (int k = 0; k < 30; ++k)
                {
                    const float a = k * 2.39996f, r = 3.0f + 9.0f * std::sqrt((k + 0.5f) / 30.0f);
                    const float p[3] = { r * std::cos(a), r * std::sin(a), 0.0f };
                    FakeModel* unit = nullptr;
                    c.StartSimulation(c.Enchant("spells#enchantments#redglow_high.m2", p, &unit));
                    units.push_back(unit);
                    FakeModel* offhand = c.Make("Item#ObjectComponents#Weapon#Sword_1H_Long_A_01.m2", p);
                    c.Attach(offhand, unit);
                    FakeModel* glow = c.Make("Spells#Enchantments#MongooseGlow_High.m2", origin);
                    c.Attach(glow, offhand);
                    c.StartSimulation(glow);
                }
            }, [&](FakeClient& c, int f, float)
            {
                for (size_t k = 0; k < units.size(); ++k)
                {
                    const float a = k * 2.39996f, r = 3.0f + 9.0f * std::sqrt((k + 0.5f) / 30.0f) + 0.4f * std::sin(f * 0.05f + k);
                    const float p[3] = { r * std::cos(a), r * std::sin(a), 0.0f };
                    c.Move(units[k], p);
                }
            }, 150);
            t.turns = 8;    // the crowd sways (2 s)
            traces.push_back(t);
        }
        // A Fireball flying past the spot at 25 yd/s (a smooth rise and fall: one reversal).
        auto fireball = [&](const char* name, float y, float z, const float at[3], const std::vector<li::FrameLight>& scene)
        {
            FakeModel* fb = nullptr;
            traces.push_back(RunTrace(name, at, TraceKind::Moving, 96, [&](FakeClient& c)
            {
                const float p[3] = { -20.0f, y, z };
                fb = c.Make("Spells#Fireball_Missile_High.m2", p, sl::kFlagMissile);
                c.Missile(fb);
                c.StartSimulation(fb);
            }, [&](FakeClient& c, int f, float)
            {
                const float x = -20.0f + 25.0f * std::max(0, f - 60) / 60.0f;
                const float p[3] = { x, y, z };
                c.Move(fb, p);
            }, 60, scene));
        };
        fireball("Fireball flying past (25 yd/s)", -1.0f, 1.5f, origin, {});
        {
            const float at[3] = { 9.0f, 6.0f, 0.0f };
            fireball("... through 20 torches, a yard from them (a full cell)", 4.5f, 2.5f, at, Torches());
        }
        std::printf("      %-62s %7s %7s %8s %6s %9s %8s\n", "", "mean", "spread", "max jump", "jumps", "reversals", "zigzags");
        for (const Trace& t : traces)
        {
            const TraceStats st = Measure(t);
            std::printf("trace %-62s %7.3f %6.1f%% %7.1f%% %6u %9u %8u\n", t.name.c_str(), st.mean, 100.0f * st.relStd, 100.0f * st.maxJump, st.jumps,
                        st.reversals, st.zigzags);
            switch (t.kind)
            {
                case TraceKind::Steady:
                    Check(st.mean > 0.01f && st.jumps == 0 && st.zigzags == 0 && st.relStd < 0.03f,
                          F("%s: steady (%.1f%% spread, %u frame-to-frame steps over 2%%, the biggest %.1f%%)", t.name.c_str(), 100.0f * st.relStd, st.jumps, 100.0f * st.maxJump));
                    break;
                case TraceKind::Smooth:
                    Check(st.mean > 0.01f && st.jumps == 0 && st.zigzags == 0 && st.reversals <= t.turns + 2,
                          F("%s: follows smoothly (the biggest step %.1f%%, %u turns for its own %u, no zigzag)", t.name.c_str(), 100.0f * st.maxJump, st.reversals, t.turns));
                    break;
                case TraceKind::Busy:
                    Check(st.mean > 0.02f && st.zigzags == 0 && st.maxJump < 0.1f,
                          F("%s: no zigzag frame to frame (%u), the biggest step %.1f%% (meteors land)", t.name.c_str(), st.zigzags, 100.0f * st.maxJump));
                    break;
                case TraceKind::Moving:
                    Check(st.reversals <= 2 && st.maxJump < 0.25f, F("%s: a smooth rise and fall (%u reversals, the biggest step %.0f%% of it)", t.name.c_str(), st.reversals, 100.0f * st.maxJump));
                    break;
            }
        }
        if (!traceFolder.empty())
        {
            std::ofstream csv(traceFolder / "spelllights_traces.csv");
            csv << "frame";
            for (const Trace& t : traces) csv << "," << '"' << t.name << '"';
            csv << "\n";
            size_t most = 0;
            for (const Trace& t : traces) most = std::max(most, t.b.size());
            for (size_t k = 0; k < most; ++k)
            {
                csv << k;
                for (const Trace& t : traces) { csv << ","; if (k < t.b.size()) csv << t.b[k]; }
                csv << "\n";
            }
            Info("traces: " + (traceFolder / "spelllights_traces.csv").string());
        }
    }

    // --- 7. objects that aren't spells -----------------------------------------------------------------------------------------
    // "icethorn is VERY bright" (the Storm Peaks 42.0, 60.2): a herb node a herbalist tracks (Find Herbs) gets the client's
    // loot sparkle (CGGameObject_C::CheckForPassiveHighlight 0x711210 -> ShowLootEffect: "HARDCODED Loot Art",
    // Particles\LootFX.m2, on the node's model, scaled by its height up to 5x). The same for chests and quest objects; the
    // client's other effects (breath, blood, level-up), game objects' models (an ore deposit, portals), creatures and
    // doodads a spell reuses. Our light at the object's foot and what was drawn; then spells that still give light.
    void TestObjects()
    {
        std::printf("-- objects that aren't spells: gatherable nodes, their sparkle, portals, breath, blood\n");
        auto run = [&](const char* name, const float spot[3], auto setup)
        {
            const Trace t = RunTrace(name, spot, TraceKind::Steady, 60, setup, [](FakeClient&, int, float) {});
            const TraceStats st = Measure(t);
            std::string drawn;
            ResetFeature();
            // (RunTrace resets after itself: run the setup once more to list what is drawn)
            {
                FakeClient c;
                const float eye[3] = { -10.0f, -12.0f, 8.0f };
                c.Step(eye);
                setup(c);
                for (int f = 0; f < 60; ++f) c.Step(eye);
                for (const sp::Drawn& d : sp::DrawnLights())
                    drawn += (drawn.empty() ? "" : ", ") + std::string(sl::KindName(d.kind)) + " " + d.path + F(" (%.2f)", sl::MaxChannel(d.color));
                ResetFeature();
            }
            Info(F("%-62s our light at its foot %.3f; drawn: %s", name, st.mean, drawn.empty() ? "none" : drawn.c_str()));
            return st.mean;
        };
        const float foot[3] = { 0.6f, 0.0f, 0.0f };
        const float origin[3] = { 0.0f, 0.0f, 0.0f };
        const float head[3] = { 0.0f, 0.0f, 1.7f };
        // A game object's sparkle: LootFX on its model (attachment: its origin here), as ShowLootEffect hangs it.
        auto sparkled = [&](const char* object)
        {
            return [object, origin](FakeClient& c)
            {
                FakeModel* node = c.Make(object, origin);
                c.StartSimulation(node);
                FakeModel* sparkle = c.Make("Particles#LootFX.m2", origin);
                c.Attach(sparkle, node);
                c.StartSimulation(sparkle);
            };
        };
        // One of the client's own effects on a character's model.
        auto onUnit = [&](const char* effect)
        {
            return [effect, head, origin](FakeClient& c)
            {
                FakeModel* unit = c.Make("Character#Human#Male#HumanMale.m2", head);
                FakeModel* e = c.Make(effect, origin);
                c.Attach(e, unit);
                c.StartSimulation(e);
            };
        };
        auto alone = [&](const char* model)
        {
            return [model, origin](FakeClient& c) { c.StartSimulation(c.Make(model, origin)); };
        };
        struct Case { const char* name; std::function<void(FakeClient&)> setup; };
        const Case notSpells[] = {
            { "Icethorn node, tracked (its loot sparkle)", sparkled("World#SkillActivated#TradeskillNodes#Bush_IceThorn.m2") },
            { "Frost Lotus node, tracked", sparkled("World#SkillActivated#TradeskillNodes#Bush_FrostLotus.m2") },
            { "a Saronite deposit (a game object the table lists), tracked", sparkled("world#expansion02#doodads#azjol-nerub#saronite#saronite_node_01.m2") },
            { "a chest's or quest object's sparkle (LootFX on a world model)", sparkled("world#generic#passivedoodads#furniture#containers#lichcontainer01.m2") },
            { "an instance portal (a game object's model under spells\\)", alone("spells#InstanceNewPortal_Blue.m2") },
            { "a mage's portal to Stormwind (a game object the table lists)", alone("world#generic#activedoodads#spellportals#MagePortal_Stormwind.m2") },
            { "a wisp (a creature the table lists)", alone("Creature#Wisp#Wisp.m2") },
            { "a character breathing in cold air", onUnit("particles#coldbreath.m2") },
            { "a blood spurt as a character is hit", onUnit("Particles#BloodSpurts#BloodSpurt.m2") },
            { "a character's level-up (under spells\\)", onUnit("Spells#LevelUp#LevelUp.m2") },
        };
        bool dark = true;
        std::string lit;
        for (const Case& k : notSpells)
        {
            const float v = run(k.name, foot, k.setup);
            if (!(v < 0.005f)) { dark = false; lit += F(" %s (%.3f)", k.name, v); }
        }
        Check(dark, "none of them gives a spell light" + (lit.empty() ? std::string() : ":" + lit));
        // Still spell lights: an enchant on a weapon, an effect on a unit (Banish, a file a game object also uses, hung
        // on a unit), a fire arrow flying (its file under item\, its flames' particles).
        const float enchant = run("an enchant on a character's weapon", foot, [&](FakeClient& c)
        {
            const float hand[3] = { 0.3f, 0.0f, 1.1f };
            c.StartSimulation(c.Enchant("spells#enchantments#redglow_high.m2", hand));
        });
        const float banish = run("Banish on a character (a file game objects use too)", foot, onUnit("spells#Banish_Chest_Dark.m2"));
        const float arrow = run("a fire arrow flying (item\\, a missile)", foot, [&](FakeClient& c)
        {
            const float at[3] = { 0.0f, 0.0f, 1.0f };
            FakeModel* m = c.Make("Item#ObjectComponents#Ammo#ArrowFireFlight_01.m2", at, sl::kFlagMissile);
            c.Missile(m);
            c.StartSimulation(m);
        });
        const float dagger = run("a thrown dagger flying (a missile with no glow of its own)", foot, [&](FakeClient& c)
        {
            const float at[3] = { 0.0f, 0.0f, 1.0f };
            FakeModel* m = c.Make("Item#ObjectComponents#Weapon#Thrown_1H_Dagger_A_01.m2", at, sl::kFlagMissile);
            c.Missile(m);
            c.StartSimulation(m);
        });
        Check(enchant > 0.02f && banish > 0.005f && arrow > 0.02f, F("spells still do: an enchant %.3f, Banish %.3f, a fire arrow %.3f", enchant, banish, arrow));
        Check(dagger < 0.005f, F("a thrown dagger (its steel's texture isn't a glow): %.3f", dagger));
    }

    // --- 5. WARP ------------------------------------------------------------------------------------------------------------
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
        wc.lpszClassName = L"SpellLightTests";
        RegisterClassW(&wc);
        HWND window = CreateWindowW(wc.lpszClassName, L"spell lights", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window;
        pp.BackBufferWidth = 64; pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        if (FAILED(w.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &w.dev))) { why = "CreateDevice failed"; return false; }
        return true;
    }

    struct Vtx { float p[3]; float n[3]; float uv[2]; };

    void Floor(std::vector<Vtx>& v, std::vector<uint16_t>& idx)
    {
        const int n = 49;
        const float half = 12.0f;
        const uint16_t base = static_cast<uint16_t>(v.size());
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                v.push_back({ { -half + 2 * half * x / (n - 1), -half + 2 * half * y / (n - 1), 0.0f }, { 0, 0, 1 }, { 0, 0 } });
        for (int y = 0; y + 1 < n; ++y)
            for (int x = 0; x + 1 < n; ++x)
            {
                const uint16_t a = static_cast<uint16_t>(base + y * n + x), b = static_cast<uint16_t>(a + 1), c = static_cast<uint16_t>(a + n), d = static_cast<uint16_t>(c + 1);
                idx.insert(idx.end(), { a, b, d, a, d, c });
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

    // The camera: D3D's view (x right, y up, z ahead), row vectors: view = (world - camera) x V.
    struct Camera { float pos[3]; float V[16]; float P[16]; };
    Camera LookAt(const float eye[3], const float at[3], float fovY, float aspect)
    {
        Camera c{};
        std::memcpy(c.pos, eye, sizeof(c.pos));
        float f[3] = { at[0] - eye[0], at[1] - eye[1], at[2] - eye[2] };
        float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        for (float& x : f) x /= fl;
        const float up[3] = { 0, 0, 1 };
        float r[3] = { f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0] };
        float rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        for (float& x : r) x /= rl;
        // The world is right-handed (z up): forward x up is to the viewer's right; D3D's view (x right, y up, z ahead).
        const float u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
        // V's columns: x = right, y = up, z = forward (row vectors: view_k = sum_i rel_i V[i][k]).
        for (int i = 0; i < 3; ++i) { c.V[i * 4 + 0] = r[i]; c.V[i * 4 + 1] = u[i]; c.V[i * 4 + 2] = f[i]; c.V[i * 4 + 3] = 0.0f; }
        c.V[15] = 1.0f;
        const float ys = 1.0f / std::tan(fovY * 0.5f), xs = ys / aspect, zn = 0.5f, zf = 200.0f;
        const float P[16] = { xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zf - zn), -zn * zf / (zf - zn), 0, 0, 1, 0 };   // rows (column vectors)
        std::memcpy(c.P, P, sizeof(P));
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

        bool Init(const fs::path& shaders, std::string& why)
        {
            if (!InitWarp(w, why)) return false;
            std::ifstream vf(shaders / "vs_Diffuse_T1_03.asm"), pf(shaders / "ps_Combiners_Opaque_00.asm");
            if (!vf || !pf) { why = "no vs_Diffuse_T1_03.asm / ps_Combiners_Opaque_00.asm in " + shaders.string(); return false; }
            std::string vsrc((std::istreambuf_iterator<char>(vf)), std::istreambuf_iterator<char>()), psrc((std::istreambuf_iterator<char>(pf)), std::istreambuf_iterator<char>());
            std::string editWhy;
            if (!ml::EditVertexForTests("Diffuse_T1", vsrc, editWhy)) { why = "the model-light edit: " + editWhy; return false; }
            std::vector<uint8_t> code;
            if (!wxl_livingazeroth::shaderpatch::Assemble(vsrc, "spell lights vs", code, why)) return false;
            if (FAILED(w.dev->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()), &vs))) { why = "CreateVertexShader"; return false; }
            if (!wxl_livingazeroth::shaderpatch::Assemble(psrc, "spell lights ps", code, why)) return false;
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
            *static_cast<uint32_t*>(lr.pBits) = 0xFFFFFFFFu;
            white->UnlockRect(0);
            if (FAILED(w.dev->CreateRenderTarget(width, height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, nullptr)) ||
                FAILED(w.dev->CreateDepthStencilSurface(width, height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &ds, nullptr)) ||
                FAILED(w.dev->CreateOffscreenPlainSurface(width, height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr)))
            { why = "targets"; return false; }
            Floor(verts, idx);
            const float ball[3] = { 2.2f, 1.6f, 1.2f };
            Sphere(verts, idx, ball, 1.2f);
            return true;
        }

        // The scene at night (the sun 0.05, the ambient 0.12): what the client's lit M2 shader does with our lights.
        Panel Draw(const std::string& name, const Camera& cam)
        {
            Panel out{ name, {} };
            IDirect3DDevice9* d = w.dev;
            const bool header = ml::PrepareForTests(d, cam.V, cam.pos);
            if (!header) Info(name + ": the light texture's header isn't valid (our lights off?)");
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
            set(10, 0.05f, 0.05f, 0.07f, 0); set(11, 0.12f, 0.12f, 0.15f, 0);
            // The sun's direction (toward it) in view space, from the world's (up and to one side).
            const float sunW[3] = { 0.3f, -0.4f, 0.87f };
            float sunV[3];
            for (int k = 0; k < 3; ++k) sunV[k] = sunW[0] * cam.V[k] + sunW[1] * cam.V[4 + k] + sunW[2] * cam.V[8 + k];
            set(12, -sunV[0], -sunV[1], -sunV[2], 0);
            set(25, 1, 0, 0, 0); set(26, 0, 0, 0, 0); set(27, 0, 0, 0, 0);
            set(28, 1, 1, 1, 1); set(29, 0, 0, 0, 0); set(30, 0, 1, 1, 0);
            // Model -> view rows (the model is the world): view_k = (p - camera) . column k.
            for (int k = 0; k < 3; ++k)
                set(31 + k, cam.V[0 * 4 + k], cam.V[1 * 4 + k], cam.V[2 * 4 + k],
                    -(cam.pos[0] * cam.V[0 * 4 + k] + cam.pos[1] * cam.V[1 * 4 + k] + cam.pos[2] * cam.V[2 * 4 + k]));
            d->SetVertexShaderConstantF(0, c, 34);
            const float fogColour[4] = { 0, 0, 0, 0 };
            d->SetPixelShaderConstantF(2, fogColour, 1);
            d->BeginScene();
            d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, static_cast<UINT>(verts.size()), static_cast<UINT>(idx.size() / 3), idx.data(), D3DFMT_INDEX16, verts.data(), sizeof(Vtx));
            d->EndScene();
            d->GetRenderTargetData(rt, sys);
            D3DLOCKED_RECT lr;
            if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY)))
            {
                out.px.resize(static_cast<size_t>(width) * height);
                for (int y = 0; y < height; ++y) std::memcpy(&out.px[static_cast<size_t>(y) * width], static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch, width * 4);
                sys->UnlockRect();
            }
            return out;
        }
    };

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

    void TestWarp(const fs::path& shaders, const fs::path& sheet)
    {
        std::printf("-- on WARP: the patched lit M2 shader, its light texture from env/Lights' grid\n");
        Renderer r;
        std::string why;
        if (!r.Init(shaders, why)) { Check(false, "WARP: " + why); return; }
        li::Config() = li::Settings{};
        const float eyePos[3] = { -9.0f, -10.0f, 7.5f }, target[3] = { 0.5f, 0.3f, 0.3f };
        const Camera cam = LookAt(eyePos, target, 0.9f, static_cast<float>(r.width) / r.height);
        std::vector<Panel> panels;
        li::UpdateForTests(cam.pos, 1.0f / 60.0f, {});
        panels.push_back(r.Draw("night, no spell", cam));

        struct Effect { const char* name; const char* file; uint32_t flags; float pos[3]; int hue; };   // hue: 0 red, 1 green, 2 blue, 3 gold, 4 purple
        const Effect effects[] = {
            { "fire (Fireball)", "Spells#Fireball_Missile_High.m2", sl::kFlagMissile, { 0.0f, 0.0f, 1.5f }, 0 },
            { "frost (Frostbolt)", "Spells#Frostbolt.m2", sl::kFlagMissile, { 0.0f, 0.0f, 1.5f }, 2 },
            { "holy (Holy missile)", "Spells#Holy_Missile_High.m2", sl::kFlagMissile, { 0.0f, 0.0f, 1.5f }, 3 },
            { "arcane (Arcane Missiles)", "Spells#Arcane_Missile_Lvl3.m2", sl::kFlagMissile, { 0.0f, 0.0f, 1.5f }, 4 },
            { "fel green (the green Lava Burst)", "spells#shaman_lavaburst_green_missile.m2", sl::kFlagMissile, { 0.0f, 0.0f, 1.5f }, 1 },
        };
        const int floorX = 0, floorY = 0;
        (void)floorX; (void)floorY;
        int bx = 0, by = 0, fx = 0, fy = 0;
        const float under[3] = { 0.0f, 0.0f, 0.0f }, farFloor[3] = { 10.0f, -6.0f, 0.0f };
        Project(cam, under, r.width, r.height, bx, by);
        Project(cam, farFloor, r.width, r.height, fx, fy);
        // On the sphere, among the points the camera sees well: the one facing the light most and the one facing it least.
        auto spherePoints = [&](const float light[3], int& sx, int& sy, int& tx, int& ty)
        {
            const float C[3] = { 2.2f, 1.6f, 1.2f }, R = 1.2f;
            float best = -2.0f, worst = 2.0f;
            for (int i = 1; i < 16; ++i)
                for (int j = 0; j < 32; ++j)
                {
                    const float th = 3.14159265f * i / 16, ph = 2 * 3.14159265f * j / 32;
                    const float n[3] = { std::sin(th) * std::cos(ph), std::sin(th) * std::sin(ph), std::cos(th) };
                    const float p[3] = { C[0] + R * n[0], C[1] + R * n[1], C[2] + R * n[2] };
                    float tc[3] = { cam.pos[0] - p[0], cam.pos[1] - p[1], cam.pos[2] - p[2] }, tl[3] = { light[0] - p[0], light[1] - p[1], light[2] - p[2] };
                    const float lc = std::sqrt(tc[0] * tc[0] + tc[1] * tc[1] + tc[2] * tc[2]), ll = std::sqrt(tl[0] * tl[0] + tl[1] * tl[1] + tl[2] * tl[2]);
                    if ((n[0] * tc[0] + n[1] * tc[1] + n[2] * tc[2]) / lc < 0.5f) continue;
                    const float facing = (n[0] * tl[0] + n[1] * tl[1] + n[2] * tl[2]) / ll;
                    int x = 0, y = 0;
                    if (!Project(cam, p, r.width, r.height, x, y)) continue;
                    if (facing > best) { best = facing; sx = x; sy = y; }
                    if (facing < worst) { worst = facing; tx = x; ty = y; }
                }
        };
        const Panel base = panels[0];   // a copy: the panels grow
        float b0[3];
        Rgb(base.px[by * r.width + bx], b0);
        Info(F("unlit floor under the spell: %.2f %.2f %.2f (the night's sun and ambient)", b0[0], b0[1], b0[2]));
        for (const Effect& e : effects)
        {
            ResetFeature();
            FakeClient c;
            c.Step(cam.pos);
            FakeModel* m = c.Make(e.file, e.pos, e.flags);
            if (e.flags & sl::kFlagMissile) c.Missile(m);
            std::vector<li::FrameLight> out;
            for (int f = 0; f < 40; ++f) out = c.Step(cam.pos);
            li::UpdateForTests(cam.pos, 1.0f / 60.0f, out);
            panels.push_back(r.Draw(e.name, cam));
            const Panel& p = panels.back();
            int sx = 0, sy = 0, tx = 0, ty = 0;
            const float lightAt[3] = { out.empty() ? 0.0f : out[0].pos[0], out.empty() ? 0.0f : out[0].pos[1], out.empty() ? 2.0f : out[0].pos[2] };
            spherePoints(lightAt, sx, sy, tx, ty);
            float a[3], s1[3], s0[3], s2[3], s3[3], fl[3], fb[3];
            Rgb(p.px[by * r.width + bx], a);
            Rgb(p.px[sy * r.width + sx], s1);
            Rgb(base.px[sy * r.width + sx], s0);
            Rgb(p.px[ty * r.width + tx], s2);
            Rgb(p.px[fy * r.width + fx], fl);
            Rgb(base.px[fy * r.width + fx], fb);
            const float d[3] = { a[0] - b0[0], a[1] - b0[1], a[2] - b0[2] };
            bool hue = false;
            switch (e.hue)
            {
                case 0: hue = d[0] > d[1] && d[1] >= d[2]; break;
                case 1: hue = d[1] > d[0] && d[1] > d[2]; break;
                case 2: hue = d[2] > d[0] && d[2] >= d[1] * 0.9f; break;
                case 3: hue = d[0] >= d[2] && d[1] >= d[2] && std::fabs(d[0] - d[1]) < 0.25f; break;
                case 4: hue = d[0] > d[1] && d[2] > d[1]; break;
            }
            const float lightRaise = std::max(d[0], std::max(d[1], d[2]));
            Check(lightRaise > 0.15f && hue, F("%s: the floor under it lit in its colour (+%.2f %.2f %.2f; light %s)", e.name, d[0], d[1], d[2],
                                              out.empty() ? "none" : F("%.2f %.2f %.2f, %.1f yd", out[0].color[0], out[0].color[1], out[0].color[2], out[0].radius).c_str()));
            Rgb(base.px[ty * r.width + tx], s3);
            const float nearRaise = sl::MaxChannel(s1) - sl::MaxChannel(s0), farRaise = sl::MaxChannel(s2) - sl::MaxChannel(s3);
            Check(nearRaise > 0.05f && nearRaise > farRaise + 0.03f, F("%s: the sphere lit on its side toward it (+%.2f), its side away from it less (+%.2f)", e.name, nearRaise, farRaise));
            Check(std::fabs(sl::MaxChannel(fl) - sl::MaxChannel(fb)) < 0.02f, F("%s: the floor 12 yd away as it was (%.2f vs %.2f)", e.name, sl::MaxChannel(fl), sl::MaxChannel(fb)));
        }

        // An area spell: a Blizzard's 30 impacts at once over an 8-yd disc, merged, and every one a light of its own.
        // `raw`: the impacts' lights as one impact's light (the tracker's, for a lone one) at each, without the spell
        // lights' merge, crowding and cap: what the rules keep from happening.
        auto field = [&](bool raw, const char* name)
        {
            ResetFeature();
            FakeClient c;
            c.Step(cam.pos);
            std::vector<std::array<float, 3>> spots;
            for (int k = 0; k < 30; ++k)
            {
                const float rr = 7.0f * std::sqrt((k + 0.5f) / 30.0f), aa = k * 2.39996f;
                spots.push_back({ rr * std::cos(aa), rr * std::sin(aa), 0.3f });
                if (!raw || k == 0) c.Make("Spells#Blizzard_Impact_Base.m2", spots.back().data(), 0, 0.35f);
            }
            std::vector<li::FrameLight> out;
            for (int f = 0; f < 40; ++f) out = c.Step(cam.pos);
            if (raw && !out.empty())
            {
                const li::FrameLight one = out[0];
                out.clear();
                for (size_t k = 0; k < spots.size(); ++k)
                {
                    li::FrameLight l = one;
                    l.pos[0] = spots[k][0]; l.pos[1] = spots[k][1]; l.pos[2] = one.pos[2];
                    l.model = reinterpret_cast<const void*>(static_cast<uintptr_t>(0x7000 + k * 16));
                    out.push_back(l);
                }
            }
            li::UpdateForTests(cam.pos, 1.0f / 60.0f, out);
            panels.push_back(r.Draw(name, cam));
            const Panel& p = panels.back();
            unsigned white = 0, lit = 0, floorPx = 0;
            float most = 0.0f;
            for (int y = 0; y < r.height; ++y)
                for (int x = 0; x < r.width; ++x)
                {
                    float v[3], b[3];
                    Rgb(p.px[y * r.width + x], v);
                    Rgb(base.px[y * r.width + x], b);
                    if (sl::MaxChannel(b) <= 0.0f) continue;
                    ++floorPx;
                    most = std::max(most, sl::MaxChannel(v));
                    if (v[0] > 0.97f && v[1] > 0.97f && v[2] > 0.97f) ++white;
                    if (sl::MaxChannel(v) > sl::MaxChannel(b) + 0.1f) ++lit;
                }
            return std::make_tuple(white, lit, floorPx, most, static_cast<unsigned>(out.size()));
        };
        const auto [w1, l1, f1, m1, n1] = field(false, "Blizzard: 30 impacts (merged, crowded, capped)");
        const auto [w2, l2, f2, m2, n2] = field(true, "Blizzard: 30 impacts, no spell-light rules");
        Check(w1 == 0 && l1 > f1 / 20 && m1 < m2, F("a Blizzard's 30 impacts: %u lights, %u of %u pixels lit, none white, the brightest channel %.2f (without the rules %u lights, %u pixels white, %.2f)",
                                                   n1, l1, f1, m1, n2, w2, m2));
        ResetFeature();

        if (!sheet.empty())
        {
            const int cols = 3, rows = static_cast<int>((panels.size() + cols - 1) / cols);
            std::vector<uint32_t> img(static_cast<size_t>(cols) * r.width * rows * r.height, 0xFF202020u);
            for (size_t k = 0; k < panels.size(); ++k)
            {
                const int ox = static_cast<int>(k % cols) * r.width, oy = static_cast<int>(k / cols) * r.height;
                for (int y = 0; y < r.height; ++y)
                    for (int x = 0; x < r.width; ++x)
                        if (!panels[k].px.empty()) img[static_cast<size_t>(oy + y) * cols * r.width + ox + x] = panels[k].px[static_cast<size_t>(y) * r.width + x];
                Info(F("sheet panel %zu: %s", k + 1, panels[k].name.c_str()));
            }
            WriteBmp(sheet, img, cols * r.width, rows * r.height);
            Info("contact sheet: " + sheet.string());
        }
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> pos;
    bool warp = true;
    fs::path sheet, traceFolder;
    for (int k = 1; k < argc; ++k)
    {
        const std::string a = argv[k];
        if (a == "--no-warp") warp = false;
        else if (a == "--sheet" && k + 1 < argc) sheet = argv[++k];
        else if (a == "--traces" && k + 1 < argc) traceFolder = argv[++k];
        else pos.push_back(a);
    }
    if (pos.size() < 2)
    {
        std::printf("usage: spelllights_tests <spell data folder (m2\\, blp\\, dbc\\)> <M2 shader folder> [<Wow.exe> [<Wow-WarcraftXL.exe>]] [--no-warp] [--sheet <bmp>]\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    g_data = pos[0];
    sp::SetFileReader(&ReadData);
    sp::SetIndoorTest(nullptr);
    TestRules();
    TestTracker();
    TestLightsMerge();
    TestGuards(std::vector<std::string>(pos.begin() + 2, pos.end()));
    TestCost();
    TestFlicker(traceFolder);
    TestObjects();
    if (warp) TestWarp(pos[1], sheet);
    else std::printf("(--no-warp: the WARP checks left out)\n");
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
