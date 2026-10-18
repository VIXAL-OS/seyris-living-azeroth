// Window glass (env/GlassModel, features/GlassShader, features/GlassShaftShaders, features/ModelIce's WMO edit, the
// shipped tables).
//  1. The rules: the light through glass by time and weather, Schlick's Fresnel, the sun through a pane, the inside or
//     outside decision, the inside colour, the floor light's projection and its leak guards, the fake room's faces.
//     The rooms: lights out by the hour (inns late, shops early), the layouts' thresholds and how often the hash picks
//     each, finding a building (by place and WMO, a city's groups, a WMO's default), a building's room constants
//     (variety, lamp, dark places, empty buildings, cities, everything off), the layouts' geometry (dividing wall and
//     doorway, curtains and gap, planks and things, the detail fading), a lit room as bright as the client's window, and
//     by day a room about kDayRoom of the glass in front (darker), easing to its own mean near the cells' edges, with
//     no step anywhere along the glass (the night's crisp look has its edges). 1b': the day's look (c85, c86) by kind,
//     at dusk and at night (none), the day's knobs changing no register at night, F0 by day and night, the room at
//     dusk seen by the day's light, DayCool (cooler, more up the pane and at a slant, the luminance kept, a fog's hue
//     bounded).
//  2. The WMO pixel edit with the glass blocks on every stock WMO pixel shader (the client's ps_MapObj*.bls, argv
//     folder): every shadow variant takes them, assembles, and stays within the slots (reported).
//  3. The coloured shafts' shaders compile (slots reported).
//  4. On WARP (the CPU rasterizer behind D3D9On12; no GPU), the blocks drawn: the inside colour, Fresnel outside and
//     the floor light against the rules above, the glass cooler by day against DayCool; the rooms behind the glass
//     pixel by pixel against RoomOutside (each
//     layout, a city's blocks, lamp-lit at night, a dark place); degenerate rays (along the normal, toward the sun,
//     edge-on, no gradients) give no NaN or Inf. --no-warp skips it (and 6).
//  5. The shipped tables (argv data folder): GlassMaterials.cdbc, GlassPanes.cdbc, RoomStyles.cdbc and
//     GlassBuildings.cdbc read back sane.
//  6. On WARP, Stormwind's leaded windows at night (rooms lit, dark, all shelves) and by day (outside, inside; outside
//     as clear and as stained glass too) from 5 to 50 yd, head on to grazing: no NaN or Inf, the glass's speckle and
//     shimmer against the stock colour's bounded; by day outside the glass keeps at least 70% of the stock's light
//     (leaded), 60% (clear), 75% (stained) and is cooler than the stock (leaded, clear; stained barely moved by the
//     cooling); at dusk (19:00) at least 75%, the warmth back. 6b: a facade of clear glass by day, 5 to 20 yd: no step
//     between neighbouring pixels above 4% of the glass's mean (the rooms' walls, doorways, shelves and cells' edges
//     all soft), and the room's contrast in the glass above a floor (visible).
//  7. Lit or dark, shared with the window lights (features/Windows): the exact hash against whole-number arithmetic,
//     the cells about the building's origin and its anchors, the dark band (the clock running, a jump's cross-fade),
//     a batch's band, origin and fallback seed (1c); and on WARP 96 windows (the origin, Goldshire, Stormwind,
//     Northrend; origins up to 600 yd away; at rest, fading, cross-fading, all dark, all lit) pixel by pixel against
//     the CPU's cell, hash and lit value, and each light's share from its cells cut exactly against the pixels'.
//  8. Frosted glass (lit by the client, no room): which batches, their constants (the band, the glow hidden, the
//     hearth's flicker; Ironforge always lit; underground no sky), the flicker (env/Flame's rates, its range and mean, a
//     frame's step, no seam at the clock's wrap, each window its own), the hearth clock; the shipped tables (Ironforge's
//     groups by sub-zone and their hours at 23:00 and 03:00, the underground and dungeon flags, the panes with no sky,
//     WindowRooms' frosted cells); and on WARP 48 frosted windows pixel by pixel against the CPU's lit value, dip and
//     glow hidden, and each light's share with its flicker against the pixels'.
// Run: glass_tests <folder with the WMO ps_MapObj*.bls> <repo data folder> [--no-warp]
#include "env/Flame.hpp"
#include "env/GlassModel.hpp"
#include "features/Glass.hpp"
#include "features/GlassShader.hpp"
#include "features/GlassShaftShaders.hpp"
#include "features/ModelIce.hpp"
#include "render/ShaderPatch.hpp"

#include <windows.h>
#include <d3d9.h>
#include <d3d9on12.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace
{
    namespace gl = wxl_livingazeroth::glass;
    int g_failures = 0;
    void Check(bool ok, const std::string& what)
    {
        std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++g_failures;
    }
    bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
    const float kZeroSeed[4 * gl::kSeedRegisters] = {};
    void Norm(float v[3]) { const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); for (int k = 0; k < 3; ++k) v[k] /= n; }

    // --- 1. the rules -----------------------------------------------------------------------------------------
    gl::Pane WallWindow()
    {
        // In the wall x = 0, facing out along +x; 2 yd wide (along y), 3 yd tall (z 1.5 .. 4.5); its room behind it.
        gl::Pane p;
        p.pos[0] = 0; p.pos[1] = 0; p.pos[2] = 3;
        p.out[0] = 1; p.out[1] = 0; p.out[2] = 0;
        p.along[0] = 0; p.along[1] = 1; p.along[2] = 0;    // horizontal, as gen_glass gives it (z x out); up = out x along
        p.up[0] = p.out[1] * p.along[2] - p.out[2] * p.along[1];
        p.up[1] = p.out[2] * p.along[0] - p.out[0] * p.along[2];
        p.up[2] = p.out[0] * p.along[1] - p.out[1] * p.along[0];
        p.halfWidth = 1; p.halfHeight = 1.5f;
        p.roomMin[0] = -10; p.roomMin[1] = -10; p.roomMin[2] = -0.5f;
        p.roomMax[0] = 0.3f; p.roomMax[1] = 10; p.roomMax[2] = 8;
        p.light[0] = 1; p.light[1] = 0.5f; p.light[2] = 0.25f;
        p.kind = gl::kStained;
        return p;
    }

    void RulesTests()
    {
        // The light through glass.
        gl::Inputs noon;
        noon.sun[0] = 0.3f; noon.sun[1] = 0.2f; noon.sun[2] = 0.93f; Norm(noon.sun);
        noon.daylight = 1; noon.night = 0;
        const gl::Light day = gl::Evaluate(noon);
        Check(day.sunGain > 0.95f && day.skyGain > 0.95f && day.day > 0.99f, "clear noon: the sun and the sky come through in full");
        gl::Inputs night = noon; night.night = 1; night.daylight = 0;
        const gl::Light dark = gl::Evaluate(night);
        Check(dark.sunGain == 0 && dark.skyGain == 0 && dark.day == 0, "night (the client's lit windows): no light through, no outside look (they keep their own)");
        gl::Inputs rain = noon; rain.precipitation = 1; rain.overcast = 0.8f;
        gl::Inputs storm = rain; storm.storm = 1;
        gl::Inputs cloud = noon; cloud.overcast = 1;
        const gl::Light r = gl::Evaluate(rain), s = gl::Evaluate(storm), c = gl::Evaluate(cloud);
        char line[256];
        std::snprintf(line, sizeof(line), "weather dims it: sun through overcast %.2f, rain %.2f, storm %.2f; sky overcast %.2f, rain %.2f, storm %.2f",
                      c.sunGain, r.sunGain, s.sunGain, c.skyGain, r.skyGain, s.skyGain);
        Check(c.sunGain < 0.2f && r.sunGain < 0.1f && s.sunGain == 0 && c.skyGain < day.skyGain && r.skyGain < c.skyGain && s.skyGain < r.skyGain && s.skyGain > 0, line);
        gl::Inputs low = noon; low.sun[2] = -0.1f;
        Check(gl::Evaluate(low).sunGain == 0, "the sun below the horizon: none of it through the glass");

        // Fresnel.
        Check(Near(gl::Fresnel(1, 0.04f), 0.04f, 1e-6f) && Near(gl::Fresnel(0, 0.04f), 1, 1e-6f) &&
              gl::Fresnel(0.3f, 0.04f) > gl::Fresnel(0.6f, 0.04f), "Fresnel: F0 face on, 1 at grazing, more at a slant");
        // The sun through a pane.
        const float out[3] = { 1, 0, 0 };
        const float beyond[3] = { 0.8f, 0, 0.6f }, behind[3] = { -0.8f, 0, 0.6f };
        Check(Near(gl::SunThrough(beyond, out, 0), 0.8f, 1e-6f) && gl::SunThrough(behind, out, 0) == 0 &&
              Near(gl::SunThrough(behind, out, 1), 0.1f, 1e-6f) && Near(gl::SunThrough(beyond, out, 1), 0.9f, 1e-6f),
              "the sun through a pane: its cosine beyond it, none behind; frosted spreads it (0.9 / 0.1)");
        // Inside or outside.
        Check(gl::InsideWeight(gl::kInsideFace, gl::kGroupExterior, 0) == 1 && gl::InsideWeight(gl::kOutsideFace, gl::kGroupIndoor, 1) == 0 &&
              gl::InsideWeight(0, gl::kGroupIndoor, 1) == 1 && gl::InsideWeight(0, gl::kGroupIndoor, 0) == 0 &&
              gl::InsideWeight(0, gl::kGroupExterior, 1) == 0 && Near(gl::InsideWeight(0, gl::kGroupIndoor, 0.4f), 0.4f, 1e-6f),
              "inside or outside: _int_ textures inside, _ext_ outside; else an interior group's face as far as the camera is indoors");
        // The inside colour.
        const float texel[3] = { 0.8f, 0.3f, 0.2f }, dim[3] = { 0.05f, 0.05f, 0.05f }, lit[3] = { 0.9f, 0.9f, 0.9f };
        const float bright[3] = { 2, 2, 2 }, none[3] = { 0, 0, 0 };
        float a[3], b[3], keep[3];
        gl::InsideColour(texel, dim, 1.0f, bright, a);
        gl::InsideColour(texel, dim, 1.5f, bright, b);
        gl::InsideColour(texel, lit, 1.0f, none, keep);
        Check(Near(a[0], 1.6f, 1e-5f) && (b[0] - b[1]) > (a[0] - a[1]) && keep[0] == 0.9f && keep[2] == 0.9f,
              "the inside colour: the texel x the light, more saturated with saturation > 1, never below the client's own (its lit windows)");

        // The floor light: the window in the wall x = 0, the sun from +x at 45 degrees: its patch on the floor z = 0
        // is x in -4.5 .. -1.5, y in -1 .. 1 (the ray toward the sun from (x, y, 0) crosses x = 0 at z = -x).
        const gl::Pane p = WallWindow();
        float sun[3] = { 1, 0, 1 }; Norm(sun);
        auto w = [&](float x, float y, float z) { const float q[3] = { x, y, z }; return gl::FloorWeightWorld(p, sun, q, 30.0f, 6.0f, 4.0f); };
        std::snprintf(line, sizeof(line), "floor light: the patch's middle %.2f, its corners inside %.2f; past its edges %.2f %.2f %.2f %.2f",
                      w(-3, 0, 0), w(-1.7f, 0.8f, 0), w(-1.2f, 0, 0), w(-4.8f, 0, 0), w(-3, 1.3f, 0), w(-3, -1.3f, 0));
        Check(Near(w(-3, 0, 0), 1, 1e-4f) && w(-1.7f, 0.8f, 0) > 0.4f && w(-1.2f, 0, 0) == 0 && w(-4.8f, 0, 0) == 0 && w(-3, 1.3f, 0) == 0 && w(-3, -1.3f, 0) == 0, line);
        // The leak guards: in front of the pane (outside), outside the room's box, beyond the reach, the sun behind.
        float behindSun[3] = { -1, 0, 1 }; Norm(behindSun);
        const float q[3] = { -3, 0, 0 };
        gl::Pane high = p; high.pos[2] = 40; high.roomMax[2] = 60;   // a window 37 yd above the floor: beyond the 30 yd reach
        const float qFar[3] = { -38.5f, 0, 0 };
        std::snprintf(line, sizeof(line), "leak guards: outside the building %.2f, outside the room's box %.2f, beyond the reach %.2f, the sun behind %.2f",
                      w(3, 0, -6), w(-11, 0, 8.5f), gl::FloorWeightWorld(high, sun, qFar, 30, 6, 4), gl::FloorWeightWorld(p, behindSun, q, 30, 6, 4));
        Check(w(3, 0, -6) == 0 && w(-11, 0, 8.5f) == 0 && gl::FloorWeightWorld(high, sun, qFar, 30, 6, 4) == 0 && gl::FloorWeightWorld(p, behindSun, q, 30, 6, 4) == 0, line);
        // The rows about a far origin (the camera) give the same.
        const float origin[3] = { 1234.5f, -987.25f, 55.0f };
        float rows[12];
        gl::ProjectionRows(p, sun, origin, 30, rows);
        const float rel[3] = { -3 - origin[0], 0 - origin[1], 0 - origin[2] };
        const float rmin[3] = { p.roomMin[0] - origin[0], p.roomMin[1] - origin[1], p.roomMin[2] - origin[2] };
        const float rmax[3] = { p.roomMax[0] - origin[0], p.roomMax[1] - origin[1], p.roomMax[2] - origin[2] };
        Check(Near(gl::FloorWeight(rows, rel, rmin, rmax, 6, 4), 1, 1e-3f), "floor light: the same about the camera (camera-relative rows and box)");

        // The fake room.
        float dist = 0;
        const float at[2] = { 0.5f, 1.6f };
        const float straight[3] = { 0, 0, 1 }, down[3] = { 0, -1.2f, 0.4f }, side[3] = { 1.5f, 0, 0.3f }, upward[3] = { 0, 1.5f, 0.3f };
        const gl::RoomFace f1 = gl::RoomHit(at, straight, 3.5f, 3.2f, 4, dist);
        const float d1 = dist;
        const gl::RoomFace f2 = gl::RoomHit(at, down, 3.5f, 3.2f, 4, dist);
        const gl::RoomFace f3 = gl::RoomHit(at, side, 3.5f, 3.2f, 4, dist);
        const gl::RoomFace f4 = gl::RoomHit(at, upward, 3.5f, 3.2f, 4, dist);
        Check(f1 == gl::kBackWall && Near(d1, 4, 1e-4f) && f2 == gl::kFloor && f3 == gl::kSideWall && f4 == gl::kCeiling,
              "the fake room: straight in the back wall (4 yd), down the floor, sideways a side wall, up the ceiling");
    }

    // --- 1b. the rooms behind the glass: who lives there ----------------------------------------------------------
    // A pane at x = 5 facing the eye at the origin (-x), the room behind it; the room's constants for a style.
    gl::RoomConstants RoomFor(const gl::RoomStyle& style, const gl::Building* b, float night, float hour, bool selfLit, const gl::Settings& s,
                              float dayLight = 1.0f, float seed[4 * gl::kSeedRegisters] = nullptr, uint32_t kind = gl::kClear, float clock = 0.0f)
    {
        gl::Material m;
        m.kind = kind;
        gl::Inputs in;
        in.sun[0] = -0.3f; in.sun[1] = 0.2f; in.sun[2] = 0.9f; Norm(in.sun);
        in.daylight = dayLight; in.night = night;
        const gl::Light light = gl::Evaluate(in);
        float c[4 * gl::kConstantCount], sd[4 * gl::kSeedRegisters], look[4 * gl::kLookRegisters];
        gl::BatchConstants(m, 0.0f, light, in.sun, in.sunColour, in.sky, s, c);
        gl::RoomInputs ri;
        ri.night = night; ri.hour = hour; ri.selfLit = selfLit; ri.fallbackSeed = 12345;
        ri.kind = kind; ri.hearthClock = clock;
        gl::RoomBatchConstants(style, b, light, ri, s, c, sd);
        gl::LookConstants(light, look);
        float day[4 * gl::kDayRegisters];
        gl::DayConstants(m, light, s, b ? b->flags : 0, day);
        gl::RoomConstants k;
        for (int i = 0; i < 4; ++i) k.c86[i] = day[4 + i];
        for (int i = 0; i < 3; ++i) k.sun[i] = in.sun[i];
        k.share = c[6];
        for (int i = 0; i < 4; ++i)
        {
            k.c198[i] = c[24 + i]; k.c199[i] = c[28 + i]; k.c202[i] = c[40 + i]; k.c203[i] = c[44 + i];
            k.c204[i] = c[48 + i]; k.c205[i] = c[52 + i]; k.c212[i] = sd[i]; k.c213[i] = sd[4 + i];
            k.c218[i] = look[i]; k.c219[i] = look[4 + i];
        }
        k.c200w = c[35];
        for (int i = 0; i < 4; ++i) k.c201[i] = c[36 + i];
        if (seed) for (int i = 0; i < 4 * gl::kSeedRegisters; ++i) seed[i] = sd[i];
        return k;
    }

    // The night's look (crisp edges, the detail to 70 yd, the cells' edges as they are): the layouts' geometry.
    gl::RoomConstants Crisp(gl::RoomConstants k)
    {
        const gl::RoomConstants night;
        for (int i = 0; i < 4; ++i) { k.c218[i] = night.c218[i]; k.c219[i] = night.c219[i]; }
        return k;
    }

    // The room's colour for the eye at the origin looking at (5, y, z) on the pane facing -x.
    float RoomAt(const gl::RoomConstants& k, float y, float z, const float r12[3], float out[3], gl::RoomSample* s = nullptr)
    {
        const float P[3] = { 5, y, z }, N[3] = { -1, 0, 0 };
        float V[3] = { -5, -y, -z }; Norm(V);
        gl::RoomOutside(P, V, N, k, r12, r12, out, s);
        return 0.2126f * out[0] + 0.7152f * out[1] + 0.0722f * out[2];
    }

    void RoomRulesTests()
    {
        char line[400];
        // The night: lights out by the hour.
        const float h20 = gl::LightsOutShare(20, 0, 1), h2230 = gl::LightsOutShare(22.5f, 0, 1), h0 = gl::LightsOutShare(0, 0, 1),
                    h2 = gl::LightsOutShare(2, 0, 1), h430 = gl::LightsOutShare(4.5f, 0, 1), h6 = gl::LightsOutShare(6, 0, 1);
        bool rising = true;
        for (float h = 21.0f; h < 28.5f; h += 0.25f) rising = rising && gl::LightsOutShare(std::fmod(h, 24.0f), 0, 1) <= gl::LightsOutShare(std::fmod(h + 0.25f, 24.0f), 0, 1) + 1e-6f;
        std::snprintf(line, sizeof(line), "lights out: dark at 20:00 %.2f, 22:30 %.2f, 00:00 %.2f, 02:00 %.2f, 04:30 %.2f, 06:00 %.2f (early risers); rising from 21:00 to 04:30",
                      h20, h2230, h0, h2, h430, h6);
        Check(Near(h20, 0.08f, 1e-5f) && Near(h2230, 0.29f, 1e-4f) && Near(h0, 0.5f, 1e-5f) && Near(h2, 0.72f, 1e-5f) && Near(h430, 0.78f, 1e-5f) &&
              Near(h6, 0.6f, 1e-5f) && rising, line);
        const float inn0 = gl::LightsOutShare(0, 2.5f, 0.55f), inn3 = gl::LightsOutShare(3, 2.5f, 0.55f), shop22 = gl::LightsOutShare(22, -1, 1);
        float innMax = 0;
        for (float h = 0; h < 24; h += 0.1f) innMax = std::max(innMax, gl::LightsOutShare(h, 2.5f, 0.55f));
        std::snprintf(line, sizeof(line), "lights out: an inn stays lit late (00:00 %.2f, 03:00 %.2f, never above %.2f); a shop closes early (22:00 %.2f against a house's %.2f)",
                      inn0, inn3, innMax, shop22, gl::LightsOutShare(22, 0, 1));
        Check(Near(inn0, 0.15f, 1e-4f) && Near(inn3, 0.55f, 1e-5f) && innMax <= 0.55f + 1e-6f && shop22 > gl::LightsOutShare(22, 0, 1) + 0.1f, line);

        // The layouts: thresholds from the weights, and the hash picking them as often as the weights say.
        const gl::RoomStyle house;
        float t[3];
        gl::LayoutThresholds(house.weights, t);
        const float none[4] = { 0, 0, 0, 0 };
        float tz[3];
        gl::LayoutThresholds(none, tz);
        int count[4] = {};
        constexpr int kCells = 40000;
        double mean[4] = {};
        for (int i = 0; i < kCells; ++i)
        {
            float h[4];
            gl::Hash42(static_cast<float>(i % 200) - 100.0f + 4321.0f, static_cast<float>(i / 200) + 377.0f, h);
            ++count[gl::PickLayout(h[0], t)];
            for (int k = 0; k < 4; ++k) mean[k] += h[k] / kCells;
        }
        std::snprintf(line, sizeof(line), "layouts: a house's thresholds %.2f %.2f %.2f; over %d rooms shelves %.3f, curtains %.3f, divider %.3f, plain %.3f "
                      "(weights 0.25 0.35 0.15 0.25); the hash's means %.3f %.3f %.3f %.3f",
                      t[0], t[1], t[2], kCells, count[0] / double(kCells), count[1] / double(kCells), count[2] / double(kCells), count[3] / double(kCells),
                      mean[0], mean[1], mean[2], mean[3]);
        bool even = true;
        for (int k = 0; k < 4; ++k) even = even && std::fabs(mean[k] - 0.5) < 0.01;
        Check(Near(t[0], 0.25f, 1e-5f) && Near(t[1], 0.6f, 1e-5f) && Near(t[2], 0.75f, 1e-5f) && tz[0] == 0 && tz[2] == 0 &&
              std::fabs(count[0] / double(kCells) - 0.25) < 0.015 && std::fabs(count[1] / double(kCells) - 0.35) < 0.015 &&
              std::fabs(count[2] / double(kCells) - 0.15) < 0.015 && std::fabs(count[3] / double(kCells) - 0.25) < 0.015 && even, line);

        // The buildings: by where they stand and their WMO, a city's groups, a default per WMO.
        std::vector<gl::Building> rows(5);
        rows[0].map = 0; rows[0].uniqueId = 10047; rows[0].pos[0] = -8913.2f; rows[0].pos[1] = 554.6f; rows[0].pos[2] = 93.1f; rows[0].style = 1;
        rows[0].path = gl::PathHash("World\\wmo\\Azeroth\\Buildings\\Stormwind\\Stormwind.wmo");
        rows[1] = rows[0]; rows[1].group = 93; rows[1].style = 2;
        rows[2] = rows[0]; rows[2].uniqueId = 555; rows[2].path = gl::PathHash("world\\wmo\\x\\nowall.wmo"); rows[2].style = 3;   // the same spot, another WMO
        rows[3].map = 0; rows[3].uniqueId = 0; rows[3].path = gl::PathHash("world/wmo/transports/ship.wmo"); rows[3].style = 4;
        rows[4].map = 1; rows[4].uniqueId = 77; rows[4].pos[0] = 100; rows[4].pos[1] = 7.9f; rows[4].style = 5;
        gl::BuildingIndex index;
        index.Build(rows);
        const float at[3] = { -8913.7f, 555.1f, 93.0f }, away[3] = { -8920.0f, 555.1f, 93.0f }, edge[3] = { 100.4f, 8.2f, 0 };
        const uint32_t sw = gl::PathHash("world/wmo/azeroth/buildings/stormwind/STORMWIND.WMO");
        const gl::Building* a = index.Find(0, at, sw, gl::kWholePlacement);
        const gl::Building* g93 = index.Find(0, at, sw, 93);
        const gl::Building* g94 = index.Find(0, at, sw, 94);
        const gl::Building* other = index.Find(0, at, gl::PathHash("world\\wmo\\x\\NoWall.wmo"), gl::kWholePlacement);
        const gl::Building* ship = index.Find(530, nullptr, gl::PathHash("World\\wmo\\Transports\\ship.wmo"), 0);
        const gl::Building* nothing = index.Find(0, away, sw, gl::kWholePlacement);
        const gl::Building* acrossCells = index.Find(1, edge, 0, gl::kWholePlacement);
        std::snprintf(line, sizeof(line), "buildings: a placement within 1.5 yd (style %u), its city group 93 (%u), group 94 the placement's (%u), another WMO at the "
                      "same spot (%u), a transport by its WMO on any map (%u), 6 yd off none (%s), across a grid line (%u); %zu placements",
                      a ? a->style : 0, g93 ? g93->style : 0, g94 ? g94->style : 0, other ? other->style : 0, ship ? ship->style : 0, nothing ? "found" : "none",
                      acrossCells ? acrossCells->style : 0, index.Placements());
        Check(gl::PathHash("World/WMO/Azeroth/Buildings/Stormwind/Stormwind.wmo") == 4036435193u && gl::PathHash("") == 0,
              "PathHash: any case or slashes, the same number as gen_glass.py's hash_path (4036435193)");
        Check(a && a->style == 1 && g93 && g93->style == 2 && g94 && g94->style == 1 && other && other->style == 3 && ship && ship->style == 4 && !nothing &&
              acrossCells && acrossCells->style == 5 && index.Placements() == 3, line);

        // The room's constants for a building.
        gl::Settings s;
        gl::RoomStyle elf;
        elf.tint[0] = 0.95f; elf.tint[1] = 0.78f; elf.tint[2] = 0.48f; elf.tint2[0] = 0.86f; elf.tint2[1] = 0.5f; elf.tint2[2] = 0.42f;
        elf.lamp[0] = 1.0f; elf.lamp[1] = 0.78f; elf.lamp[2] = 0.38f; elf.spread = 0.2f; elf.darkShare = 0.1f;
        gl::Building b;
        b.seed = (37u << 10) | 911u;
        const gl::RoomConstants day = RoomFor(elf, &b, 0, 13, true, s);
        const gl::RoomConstants nightLit = RoomFor(elf, &b, 1, 1, true, s, 0.0f);
        const gl::RoomConstants nightPlain = RoomFor(elf, &b, 1, 1, false, s, 0.0f);
        gl::Building dark = b; dark.flags = gl::kBuildingDarkPlace;
        gl::Building empty = b; empty.flags = gl::kBuildingEmpty;
        gl::Building city = b; city.flags = gl::kBuildingCity;
        const gl::RoomConstants nightDarkPlace = RoomFor(elf, &dark, 1, 1, true, s, 0.0f);
        const gl::RoomConstants dayEmpty = RoomFor(elf, &empty, 0, 13, true, s);
        const gl::RoomConstants inCity = RoomFor(elf, &city, 0, 13, true, s);
        gl::Settings off = s; off.roomVariety = 0; off.roomLayouts = 0; off.litRooms = 0;
        const gl::RoomConstants plainDay = RoomFor(elf, &b, 0, 13, true, off);
        const gl::RoomConstants plainNight = RoomFor(elf, &b, 1, 1, true, off, 0.0f);
        std::snprintf(line, sizeof(line), "room constants: by day tints %.2f / %.2f, spread %.2f, dark %.2f, no lamp %.2f, layouts %.2f %.2f %.2f, seed %.0f %.0f; "
                      "at 01:00 lit behind a lit window: lamp %.2f %.2f %.2f, dark %.2f, glow hidden %.2f; not lit there: lamp %.2f; a dark place: dark %.2f, lamp %.2f; "
                      "empty: dark %.2f; a city: block 1/%.0f; variety, layouts and lit rooms off: tints %.2f / %.2f, dark %.2f, layouts %.2f, lamp %.2f, glow hidden %.2f",
                      day.c199[0], day.c202[0], day.c202[3], day.c203[3], day.c203[0], day.c204[0], day.c204[1], day.c204[2], day.c212[0], day.c212[1],
                      nightLit.c203[0], nightLit.c203[1], nightLit.c203[2], nightLit.c203[3], nightLit.c205[3], nightPlain.c203[0], nightDarkPlace.c203[3],
                      nightDarkPlace.c203[0], dayEmpty.c203[3], inCity.c212[2] > 0 ? 1.0f / inCity.c212[2] : 0.0f, plainDay.c199[0], plainDay.c202[0],
                      plainDay.c203[3], plainDay.c204[2], plainNight.c203[0], plainNight.c205[3]);
        // (by day the buildings differ by kDayVariety of the night's: the tints and the spread)
        Check(Near(day.c199[0], 0.95f, 1e-5f) && Near(day.c202[0], 0.95f + (0.86f - 0.95f) * gl::room::kDayVariety, 1e-5f) &&
              Near(day.c202[3], 0.2f * gl::room::kDayVariety, 1e-5f) && Near(nightLit.c202[0], 0.86f, 1e-5f) && Near(nightLit.c202[3], 0.2f, 1e-5f) &&
              Near(day.c203[3], 0.1f, 1e-5f) &&
              day.c203[0] == 0 && Near(day.c204[2], 0.75f, 1e-5f) && day.c212[0] == 911 && day.c212[1] == 37 && day.c212[2] == 0 &&
              nightLit.c203[0] > 0.5f && nightLit.c203[2] < nightLit.c203[0] && Near(nightLit.c203[3], gl::LightsOutShare(1, 0, 1), 1e-5f) &&
              Near(nightLit.c205[3], 0.75f, 1e-5f) && nightPlain.c203[0] == 0 && nightDarkPlace.c203[3] == 1 && nightDarkPlace.c203[0] == 0 &&
              dayEmpty.c203[3] == 1 && Near(inCity.c212[2], 1.0f / gl::room::kBlock, 1e-6f) &&
              Near(plainDay.c199[0], 0.85f, 1e-5f) && Near(plainDay.c202[0], 0.85f, 1e-5f) && plainDay.c202[3] == 0 && plainDay.c203[3] == 0 &&
              plainDay.c204[2] == 0 && plainNight.c203[0] == 0 && plainNight.c205[3] == 0, line);

        // The room's geometry (RoomOutside, as the shader works it), by layout. Rooms over a stretch of glass.
        const float r12[3] = { 0.3f, 0.3f, 0.3f };
        gl::RoomStyle force = elf;
        auto only = [&](int layout) { gl::RoomStyle st = force; for (int k = 0; k < 4; ++k) st.weights[k] = k == layout ? 1.0f : 0.0f; return st; };
        int doors = 0, walls = 0, cloths = 0, gaps = 0, planks = 0, things = 0, backs = 0, shelvesFar = 0, n = 0;
        float out[3];
        for (int pass = 0; pass < 3; ++pass)
        {
            const gl::RoomStyle st = only(pass == 0 ? gl::kLayoutDivider : pass == 1 ? gl::kLayoutCurtains : gl::kLayoutShelves);
            const gl::RoomConstants k = Crisp(RoomFor(st, &b, 0, 13, false, s));
            for (float y = -20; y < 20; y += 0.173f)
                for (float z = -1; z < 3; z += 0.21f)
                {
                    gl::RoomSample sm;
                    RoomAt(k, y, z, r12, out, &sm);
                    if (pass == 0) { doors += sm.door; walls += sm.wall; }
                    if (pass == 1) { cloths += sm.cloth > 0.99f; gaps += sm.cloth < 0.01f; }
                    if (pass == 2 && sm.hit[2] > 0.999f)
                    {
                        ++backs;
                        planks += sm.shelf > 1.3f;
                        things += sm.shelf != 1.0f && sm.shelf != gl::room::kShelfBack && sm.shelf < 1.3f;
                    }
                    ++n;
                }
        }
        // Far away (100 yd) the shelves fade to the plain wall.
        {
            gl::RoomConstants k = Crisp(RoomFor(only(gl::kLayoutShelves), &b, 0, 13, false, s));
            k.cam[0] = -95;
            for (float y = -20; y < 20; y += 0.31f)
            {
                gl::RoomSample sm;
                RoomAt(k, y, 1.0f, r12, out, &sm);
                shelvesFar += sm.shelf != 1.0f;
            }
        }
        std::snprintf(line, sizeof(line), "room layouts: a dividing wall %d points and doorways %d; curtains %d cloth and %d gap; shelves on %d back-wall points: "
                      "planks %d, things %d; 100 yd away %d not plain", walls, doors, cloths, gaps, backs, planks, things, shelvesFar);
        Check(walls > 100 && doors > 20 && cloths > 100 && gaps > 50 && backs > 100 && planks > 5 && things > 20 && shelvesFar == 0, line);

        // At night a lit room is about as bright as the client's lit window it replaces; a dark one hides most of it.
        const float lamp12[3] = { 0.62f, 0.47f, 0.28f };
        const float lum12 = 0.2126f * lamp12[0] + 0.7152f * lamp12[1] + 0.0722f * lamp12[2];
        double sumLit = 0, sumRoom = 0;
        int lit = 0, darkRooms = 0;
        for (int st = 0; st < 2; ++st)
        {
            gl::Settings all = s;
            all.lightsOut = 0;   // every room lit
            gl::RoomStyle style = st ? elf : gl::RoomStyle{};
            for (uint32_t seed = 1; seed < 40; ++seed)
            {
                gl::Building bb; bb.seed = seed * 2654435761u;
                const gl::RoomConstants k = RoomFor(style, &bb, 1, 23, true, all, 0.0f);
                for (float y = -6; y < 6; y += 0.37f)
                    for (float z = -1; z < 4; z += 0.43f)
                    {
                        gl::RoomSample sm;
                        const float l = RoomAt(k, y, z, lamp12, out, &sm);
                        sumLit += l; ++lit;
                        // the room alone: out = share room + (1 - share) r12
                        sumRoom += (l - (1 - k.share) * lum12) / k.share;
                    }
            }
        }
        const gl::RoomConstants kd = RoomFor(elf, &dark, 1, 1, true, s, 0.0f);
        float worst = 0;
        for (float y = -6; y < 6; y += 0.5f)
        {
            const float l = RoomAt(kd, y, 1.0f, lamp12, out);
            worst = std::max(worst, l / lum12);
            darkRooms++;
        }
        // The share of rooms dark is the threshold's: over many rooms at three shares of the night.
        {
            float frac[3] = {};
            const float shares[3] = { 0.22f, 0.5f, 0.744f };
            for (int i = 0; i < 3; ++i)
            {
                gl::RoomConstants k = RoomFor(elf, &b, 1, 23, true, s, 0.0f);
                k.c203[3] = shares[i]; k.c200w = shares[i];
                int darkCount = 0, all = 0;
                for (uint32_t seed = 0; seed < 64; ++seed)
                {
                    k.c212[0] = static_cast<float>(seed * 13 % 1024); k.c212[1] = static_cast<float>(seed * 101 % 1024);
                    for (float y = -40; y < 40; y += 3.5f)
                        for (float z = -3; z < 9; z += 3.2f)
                        {
                            gl::RoomSample sm;
                            RoomAt(k, y + 0.3f, z + 0.4f, lamp12, out, &sm);
                            darkCount += sm.lit ? 0 : 1; ++all;
                        }
                }
                frac[i] = darkCount / static_cast<float>(all);
            }
            std::snprintf(line, sizeof(line), "rooms dark at night by the share: 0.22 -> %.3f, 0.50 -> %.3f, 0.744 -> %.3f", frac[0], frac[1], frac[2]);
            Check(std::fabs(frac[0] - 0.22f) < 0.03f && std::fabs(frac[1] - 0.5f) < 0.03f && std::fabs(frac[2] - 0.744f) < 0.03f, line);
        }
        std::snprintf(line, sizeof(line), "at night, lit rooms (%d points): the room's mean luminance %.3f against the client's lit window's %.3f (x %.2f; kNightMean %.2f), "
                      "the glass %.3f; dark rooms behind it keep at most %.0f%% of its glow",
                      lit, sumRoom / lit, lum12, sumRoom / lit / lum12, gl::room::kNightMean, sumLit / lit, worst * 100);
        Check(std::fabs(sumRoom / lit / lum12 - 1.0) < 0.15 && worst < 0.12f && darkRooms > 0, line);

        // By day the room is about kDayRoom of the glass in front of it (the client's colour after the darkening) over
        // many rooms, buildings and views from 4 to 30 yd, darker than the glass; near the cells' edges it eases to its
        // own mean there (kEdgeMean: no lighter or darker frame round each room); and it has no edges: along rows of
        // glass 2 mm apart (a pixel 2 yd away) the largest change of the room (over its mean) under 2.5% (1.36% before
        // glass-day2, with 0.6 x its contrast), where the night's crisp look has its walls', doorways', shelves' and the
        // cells' own.
        {
            gl::Material clear;
            clear.kind = gl::kClear;
            gl::Inputs din;
            din.daylight = 1; din.night = 0;
            const gl::Light dl = gl::Evaluate(din);
            float bc[4 * gl::kConstantCount];
            gl::BatchConstants(clear, 0.0f, dl, din.sun, din.sunColour, din.sky, s, bc);
            const float darken = bc[20];
            const float glass12[3] = { 0.62f, 0.5f, 0.3f };
            float outside[3];
            for (int i = 0; i < 3; ++i) outside[i] = glass12[i] * (1.0f - darken);
            const float lumGlass = 0.2126f * outside[0] + 0.7152f * outside[1] + 0.0722f * outside[2];
            auto lum = [](const float c[3]) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; };
            const float N[3] = { -1, 0, 0 };
            double sum = 0, band = 0;
            int count = 0, inBand = 0;
            float stepDay = 0, stepNight = 0;
            for (int st = 0; st < 2; ++st)
                for (uint32_t seed = 1; seed < 33; ++seed)
                {
                    gl::Building bb;
                    bb.seed = seed * 2654435761u;
                    const gl::RoomConstants k = RoomFor(st ? elf : gl::RoomStyle{}, &bb, 0, 13, false, s);
                    for (float dist = 4; dist < 30; dist += 6.5f)
                        for (float y = -8; y < 8; y += 0.53f)
                            for (float z = -2; z < 5; z += 0.61f)
                            {
                                const float P[3] = { dist, y, z };
                                float V[3] = { -P[0], -P[1], -P[2] }; Norm(V);
                                gl::RoomSample sm;
                                float o[3];
                                gl::RoomOutside(P, V, N, k, glass12, outside, o, &sm);
                                sum += lum(sm.room) / lumGlass;
                                ++count;
                                // the room's light before the ease where it eases (near a cell's edge on the glass)
                                if (sm.edge > 0.05f && sm.edge < 0.999f)
                                {
                                    band += (sm.light - (1.0f - sm.edge) * gl::room::kEdgeMean) / sm.edge;
                                    ++inBand;
                                }
                            }
                    if (seed > 8) continue;
                    for (int look = 0; look < 2; ++look)
                    {
                        const gl::RoomConstants kk = look ? Crisp(k) : k;
                        for (float z = -1.3f; z < 4.5f; z += 0.9f)
                        {
                            float prev = -1, worst = 0;
                            double mean = 0;
                            int m = 0;
                            for (float y = -9; y < 9; y += 0.002f)
                            {
                                const float P[3] = { 6, y, z };
                                float V[3] = { -P[0], -P[1], -P[2] }; Norm(V);
                                gl::RoomSample sm;
                                float o[3];
                                gl::RoomOutside(P, V, N, kk, glass12, outside, o, &sm);
                                const float l = lum(sm.room);
                                if (prev >= 0) worst = std::max(worst, std::fabs(l - prev));
                                prev = l; mean += l; ++m;
                            }
                            const float rel = m ? static_cast<float>(worst / (mean / m)) : 1.0f;
                            (look ? stepNight : stepDay) = std::max(look ? stepNight : stepDay, rel);
                        }
                    }
                }
            const double bandMean = inBand ? band / inBand : 0.0;
            std::snprintf(line, sizeof(line), "by day the room is %.2f of the glass in front (kDayRoom %.2f: darker than the glass; %d points 4..30 yd, 64 "
                          "buildings); near the cells' edges its light %.3f before it eases to kEdgeMean %.3f (%d points); along rows of glass 2 mm apart its "
                          "largest step %.2f%% of its mean (the night's crisp look %.0f%%)",
                          sum / count, gl::room::kDayRoom, count, bandMean, gl::room::kEdgeMean, inBand, stepDay * 100, stepNight * 100);
            Check(std::fabs(sum / count / gl::room::kDayRoom - 1.0) < 0.12 && gl::room::kDayRoom < 0.75f && inBand > 1000 &&
                  std::fabs(gl::room::kEdgeMean / bandMean - 1.0) < 0.15 && stepDay < 0.025f && stepNight > 0.2f, line);
        }
    }

    // --- 1b'. by day, at dusk and at night: the glass's day look (c85, c86), its F0, the room at dusk ---------------
    // "they read as lamplit at noon ... I also feel like the inside faint rooms are only barely visible" (the user,
    // Theramore at 16:01, 2026-10-18): by day the painted glass cooler (DayCool), more of the sky in it, the room behind
    // more of the window and darker than the glass; the warmth back at dusk; the night as it was.
    void DayRulesTests()
    {
        char line[480];
        gl::Settings s;
        gl::Inputs noon;
        noon.sun[0] = -0.3f; noon.sun[1] = 0.2f; noon.sun[2] = 0.9f; Norm(noon.sun);
        noon.daylight = 1; noon.night = 0;
        gl::Inputs dusk = noon;           // 19:00: the sun's day over (18:00), the client's windows not lit yet (20:30)
        dusk.daylight = 0; dusk.sun[2] = -0.1f; Norm(dusk.sun);
        gl::Inputs night = dusk;          // 22:40
        night.night = 1;
        const gl::Light ln = gl::Evaluate(noon), ld = gl::Evaluate(dusk), lt = gl::Evaluate(night);
        auto day = [&](uint32_t kind, uint32_t flags, const gl::Light& l, const gl::Settings& st, float out[8]) {
            gl::Material m; m.kind = kind; m.flags = flags & 0xFF;
            gl::DayConstants(m, l, st, flags >> 8, out);
        };
        float clear[8], leaded[8], stained[8], frosted[8], under[8], matte[8], atNight[8], atDusk[8];
        day(gl::kClear, 0, ln, s, clear); day(gl::kLeaded, 0, ln, s, leaded); day(gl::kStained, 0, ln, s, stained); day(gl::kFrosted, 0, ln, s, frosted);
        day(gl::kClear, gl::kBuildingUnderground << 8, ln, s, under); day(gl::kClear, gl::kMatte, ln, s, matte);
        day(gl::kLeaded, 0, lt, s, atNight); day(gl::kLeaded, 0, ld, s, atDusk);
        std::snprintf(line, sizeof(line), "the day's look (c85.x cooler; c86 the sun's patch, farther darker, the light falling): noon clear %.2f %.2f %.2f %.2f, leaded "
                      "%.2f, stained %.3f, frosted %.2f; underground %.2f %.2f, matte %.2f; dusk %.2f %.2f %.2f %.2f; night %.2f %.2f %.2f %.2f",
                      clear[0], clear[4], clear[5], clear[6], leaded[0], stained[0], frosted[0], under[0], under[4], matte[0], atDusk[0], atDusk[4], atDusk[5],
                      atDusk[6], atNight[0], atNight[4], atNight[5], atNight[6]);
        Check(Near(clear[0], s.dayCoolness, 1e-6f) && Near(leaded[0], s.dayCoolness, 1e-6f) && Near(stained[0], 0.25f * s.dayCoolness, 1e-6f) &&
              Near(frosted[0], 0.6f * s.dayCoolness, 1e-6f) && clear[4] > 0.5f * gl::room::kPatch && Near(clear[5], gl::room::kFarDay, 1e-6f) &&
              Near(clear[6], gl::room::kFallDay, 1e-6f) && under[0] == 0 && under[4] == 0 && matte[0] == 0 && atDusk[0] == 0 && atDusk[4] == 0 &&
              Near(atDusk[5], gl::room::kFarDay, 1e-6f) && Near(atDusk[6], gl::room::kFallDay, 1e-6f) &&
              atNight[0] == 0 && atNight[4] == 0 && atNight[5] == 0 && atNight[6] == 0, line);

        // At night none of the day's knobs changes a register: the batch's constants (c192..c205, c212, c213, c218, c219,
        // c85, c86) with the defaults and with DayCoolness, ReflectionF0, RoomShareDay and OutsideDarkening all changed are
        // the same bits; F0 at night is room::kNightF0 x the kind's, as before.
        gl::Settings other = s;
        other.dayCoolness = 1.0f; other.reflectionF0 = 0.4f; other.roomShareDay = 0.95f; other.outsideDarkening = 0.6f;
        gl::RoomStyle style;
        gl::Building b;
        b.seed = 977;
        bool same = true;
        float nightF0[2] = {};
        for (uint32_t kind : { gl::kClear, gl::kLeaded, gl::kStained, gl::kFrosted })
        {
            float regs[2][4 * gl::kConstantCount + 4 * gl::kSeedRegisters + 4 * gl::kLookRegisters + 4 * gl::kDayRegisters];
            for (int v = 0; v < 2; ++v)
            {
                const gl::Settings& st = v ? other : s;
                gl::Material m; m.kind = kind;
                float* c = regs[v];
                float* sd = c + 4 * gl::kConstantCount;
                float* look = sd + 4 * gl::kSeedRegisters;
                float* dy = look + 4 * gl::kLookRegisters;
                gl::BatchConstants(m, 0.0f, lt, night.sun, night.sunColour, night.sky, st, c);
                gl::RoomInputs ri; ri.night = 1; ri.hour = 22.67f; ri.selfLit = true; ri.kind = kind;
                gl::RoomBatchConstants(style, &b, lt, ri, st, c, sd);
                gl::LookConstants(lt, look);
                gl::DayConstants(m, lt, st, 0, dy);
                if (kind == gl::kClear) nightF0[0] = c[22] * 1.0f;   // clear: F0 / Fresnel's strength 1
            }
            same = same && std::memcmp(regs[0], regs[1], sizeof(regs[0])) == 0;
        }
        nightF0[1] = gl::room::kNightF0 * 1.2f;
        // By day F0 is the setting's x the kind's: clear 1.2, leaded 1, stained and frosted 0.5.
        float noonC[4 * gl::kConstantCount], noonS[4 * gl::kConstantCount];
        gl::Material mc; mc.kind = gl::kClear;
        gl::Material ms; ms.kind = gl::kStained;
        gl::BatchConstants(mc, 0.0f, ln, noon.sun, noon.sunColour, noon.sky, s, noonC);
        gl::BatchConstants(ms, 0.0f, ln, noon.sun, noon.sunColour, noon.sky, s, noonS);
        std::snprintf(line, sizeof(line), "at night the day's knobs (DayCoolness, ReflectionF0, RoomShareDay, OutsideDarkening) change no register: %s; F0 at night "
                      "%.3f (kNightF0 x 1.2: %.3f), by day clear %.3f, stained %.3f (x its Fresnel 0.6)",
                      same ? "the same bits" : "DIFFERENT", nightF0[0], nightF0[1], noonC[22], noonS[22] * 0.6f);
        Check(same && Near(nightF0[0], nightF0[1], 1e-6f) && Near(noonC[22], s.reflectionF0 * 1.2f, 1e-6f) && Near(noonS[22] * 0.6f, s.reflectionF0 * 0.5f, 1e-6f), line);

        // Dusk (19:00): no cooling or darkening (the painted glass's warmth again), the room still seen by the day's light
        // (not black: before, between 18:00 and the client's lit windows at 20:30 the room had no light at all), its day share.
        float duskC[4 * gl::kConstantCount], duskSeed[4 * gl::kSeedRegisters];
        gl::Material ml; ml.kind = gl::kLeaded;
        gl::BatchConstants(ml, 0.0f, ld, dusk.sun, dusk.sunColour, dusk.sky, s, duskC);
        gl::RoomInputs rd; rd.night = 0; rd.hour = 19; rd.selfLit = true; rd.kind = gl::kLeaded;
        gl::RoomBatchConstants(style, &b, ld, rd, s, duskC, duskSeed);
        float duskLook[8];
        gl::LookConstants(ld, duskLook);
        std::snprintf(line, sizeof(line), "dusk (day %.2f, rooms %.2f): darkening %.2f, the room's day light c198.w %.2f, share %.3f (RoomShareDay x 0.77 %.3f), "
                      "its soft walls %.2f (the day's)", ld.day, ld.rooms, duskC[20], duskC[27], duskC[6], s.roomShareDay * 0.5f / 0.65f, duskLook[0]);
        Check(ld.day == 0 && ld.rooms == 1 && duskC[20] == 0 && duskC[27] > 0.5f && Near(duskC[6], s.roomShareDay * 0.5f / 0.65f, 1e-5f) &&
              Near(duskLook[0], gl::room::kWallSoftDay, 1e-6f), line);

        // DayCool: warm painted glass under a blue sky keeps its luminance and goes cooler, more up the pane and at a slant;
        // a green fog's hue stays within kSkyHue.
        const float glassWarm[3] = { 0.6f, 0.45f, 0.2f }, blue[3] = { 0.5f, 0.6f, 0.8f }, green[3] = { 0.15f, 0.9f, 0.15f };
        const float c85[4] = { 0.5f, gl::room::kCoolUp, gl::room::kCoolGraze, gl::room::kCoolTint };
        float level[3], up[3], slant[3], fog[3];
        gl::DayCool(glassWarm, blue, 0.0f, 1.0f, c85, level);
        gl::DayCool(glassWarm, blue, 0.4f, 1.0f, c85, up);
        gl::DayCool(glassWarm, blue, 0.0f, 0.3f, c85, slant);
        gl::DayCool(glassWarm, green, 0.0f, 1.0f, c85, fog);
        auto warmth = [](const float* q) { return (q[0] - q[2]) / (q[0] + q[1] + q[2]); };
        auto lum = [](const float* q) { return 0.2126f * q[0] + 0.7152f * q[1] + 0.0722f * q[2]; };
        float fogHue = 0;
        for (int k = 0; k < 3; ++k) fogHue = std::max(fogHue, fog[k] / lum(glassWarm));
        std::snprintf(line, sizeof(line), "DayCool: warmth %.3f -> %.3f head on, %.3f up the pane, %.3f at a slant; luminance %.3f -> %.3f; a green fog's channel at most "
                      "%.2f x the luminance (kSkyHue %.1f)", warmth(glassWarm), warmth(level), warmth(up), warmth(slant), lum(glassWarm), lum(level), fogHue,
                      gl::room::kSkyHue[1]);
        Check(warmth(level) < warmth(glassWarm) - 0.08f && warmth(up) < warmth(level) && warmth(slant) < warmth(level) &&
              std::fabs(lum(level) / lum(glassWarm) - 1.0f) < 0.03f && fogHue < gl::room::kSkyHue[1], line);
    }

    // --- 1c. lit or dark: the exact hash, the cells, the dark band (shared with the window lights) -----------------
    uint32_t Bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

    void LitRulesTests()
    {
        char line[400];
        // The hash: the shader's float steps against whole-number arithmetic, over cells about an origin (negative
        // too), every seed corner, and a city's far cells.
        int off = 0, n = 0;
        for (int x = -400; x <= 400; x += 3)
            for (int y = -300; y <= 300; y += 7)
                for (int s = 0; s < 4; ++s)
                {
                    const float sx = s & 1 ? 1023.0f : static_cast<float>((x * 37 + 5) & 1023), sy = s & 2 ? 1023.0f : static_cast<float>((y * 11 + 3) & 1023);
                    const float h = gl::LitHash(static_cast<float>(x), static_cast<float>(y), sx, sy);
                    const uint32_t k = gl::LitHashIndex(x + static_cast<int64_t>(sx), y + static_cast<int64_t>(sy));
                    off += Bits(h) != Bits((static_cast<float>(k) + 0.5f) * gl::room::kLitInv) ? 1 : 0;
                    ++n;
                }
        for (int x = -2600; x <= 2600; x += 13) { off += Bits(gl::LitHash(static_cast<float>(x), -9.0f, 512, 77)) != Bits((gl::LitHashIndex(x + 512, 68) + 0.5f) * gl::room::kLitInv); ++n; }
        // a permutation: 289 cells in a row give every value once; the share below a threshold is the threshold
        bool perm = true;
        for (int y = -5; y < 5 && perm; ++y)
        {
            std::vector<int> seen(289, 0);
            for (int x = 1000; x < 1289; ++x) seen[gl::LitHashIndex(x, y)]++;
            for (int c : seen) perm = perm && c == 1;
        }
        int below = 0, all = 0;
        for (int x = -200; x < 200; ++x)
            for (int y = -60; y < 60; ++y) { below += gl::LitHash(static_cast<float>(x), static_cast<float>(y), 300, 700) < 0.37f; ++all; }
        std::snprintf(line, sizeof(line), "the lit hash: the shader's float steps give the whole-number hash's bits in %d of %d cells (offsets to +-2600 cells); "
                      "289 cells in a row each value once; below 0.37: %.3f of the rooms", n - off, n, below / double(all));
        Check(off == 0 && perm && std::fabs(below / double(all) - 0.37) < 0.01, line);

        // The cells: about the building's origin, in the face's frame (along (-N.y, N.x, 0), up N x along).
        const float inv[2] = { 1 / 3.5f, 1 / 3.2f };
        const float o[3] = { -8913.2f, 554.6f, 93.1f };
        const float nx[3] = { 1, 0, 0 }, ny[3] = { 0, 1, 0 }, sky[3] = { 0, 0, 1 };
        float c1[2], c2[2], c3[2];
        const float p1[3] = { o[0], o[1] + 7.1f, o[2] + 3.3f }, p2[3] = { o[0] - 4.0f, o[1], o[2] - 0.5f }, p3[3] = { o[0] + 3.6f, o[1] - 0.1f, o[2] };
        gl::RoomCell(p1, o, nx, inv, c1);
        gl::RoomCell(p2, o, ny, inv, c2);
        gl::RoomCell(p3, o, sky, inv, c3);
        std::snprintf(line, sizeof(line), "room cells about Stormwind's origin: facing +x 7.1 along, 3.3 up -> (%.0f, %.0f); facing +y 4 yd west, 0.5 down -> (%.0f, %.0f); "
                      "a skylight (along x) -> (%.0f, %.0f)", c1[0], c1[1], c2[0], c2[1], c3[0], c3[1]);
        Check(c1[0] == 2 && c1[1] == 1 && c2[0] == 1 && c2[1] == -1 && c3[0] == 1 && c3[1] == -1, line);
        // 40 yd east of it: about the anchor 64 yd east, its offset (211, 59) in the cell (the generator's test too)
        float c4[2];
        const float p4[3] = { o[0] + 40.0f, o[1] + 7.1f, o[2] + 3.3f };
        gl::RoomCell(p4, o, nx, inv, c4);
        std::snprintf(line, sizeof(line), "room cells 40 yd east of the origin: about the anchor 64 yd east, (2, 1) + (211, 59) -> (%.0f, %.0f)", c4[0], c4[1]);
        Check(c4[0] == 213 && c4[1] == 60, line);

        // The dark band: at rest a step; the clock running: a fade by when the threshold crossed each room, going dark
        // (a room near the share now lit, near the share then dark) or lit again; after a jump, every room it crossed
        // together.
        const gl::DarkBand rest = gl::Band(0.4f, 0.4f), dark = gl::Band(0.42f, 0.4f), light = gl::Band(0.4f, 0.42f);
        const gl::DarkBand outA = gl::Crossfade(0.1f, 0.7f, 0.25f), inA = gl::Crossfade(0.7f, 0.1f, 0.25f);
        std::snprintf(line, sizeof(line), "the dark band: at rest 0.4 + 1e-7 lit %.0f, 0.4 dark %.0f; going dark the room crossed 0.5 s ago (0.4133) %.2f, lit again %.2f; "
                      "a jump 10%% -> 70%% dark a quarter through: crossed rooms %.2f (the rest %.0f %.0f), back %.2f",
                      gl::RoomLit(0.4000001f, rest), gl::RoomLit(0.4f, rest), gl::RoomLit(0.41333f, dark), gl::RoomLit(0.41333f, light),
                      gl::RoomLit(0.4f, outA), gl::RoomLit(0.05f, outA), gl::RoomLit(0.75f, outA), gl::RoomLit(0.4f, inA));
        Check(gl::RoomLit(0.4000001f, rest) == 1 && gl::RoomLit(0.4f, rest) == 0 && gl::RoomLit(0.3999f, rest) == 0 &&
              Near(gl::RoomLit(0.41333f, dark), 0.6667f, 1e-3f) && Near(gl::RoomLit(0.41333f, light), 0.6667f, 1e-3f) &&
              gl::RoomLit(0.39f, dark) == 0 && gl::RoomLit(0.43f, dark) == 1 && gl::RoomLit(0.39f, light) == 0 && gl::RoomLit(0.43f, light) == 1 &&
              Near(gl::RoomLit(0.4f, outA), 0.75f, 1e-6f) && gl::RoomLit(0.05f, outA) == 0 && gl::RoomLit(0.75f, outA) == 1 &&
              Near(gl::RoomLit(0.4f, inA), 0.25f, 1e-6f) && gl::RoomLit(0.05f, inA) == 0 && gl::RoomLit(0.75f, inA) == 1, line);

        // A batch's constants: the band from the rooms' clock, the origin (a placement's from the table, else the
        // instance's), the fallback seed (the WMO's path).
        gl::Settings s;
        gl::RoomStyle house;
        gl::Building placed; placed.uniqueId = 99; placed.seed = 5; placed.pos[0] = -8913.2f; placed.pos[1] = 554.6f; placed.pos[2] = 93.1f;
        gl::Building byWmo = placed; byWmo.uniqueId = 0; byWmo.pos[0] = byWmo.pos[1] = byWmo.pos[2] = 0;
        const float instance[3] = { -8913.0f, 554.9f, 93.0f };
        gl::Inputs in; in.daylight = 0; in.night = 1;
        const gl::Light lt = gl::Evaluate(in);
        float c[4 * gl::kConstantCount], sdA[4 * gl::kSeedRegisters], sdB[4 * gl::kSeedRegisters], sdC[4 * gl::kSeedRegisters];
        gl::Material m; m.kind = gl::kClear;
        gl::BatchConstants(m, 0, lt, in.sun, in.sunColour, in.sky, s, c);
        // the clock running from 23:29:58 to 23:30:00 (game seconds as real ones), 60 frames a second
        gl::RoomClock clock;
        for (int f = 0; f <= 120; ++f) clock.Advance(50.0 + f / 60.0, 23.5f - (120 - f) / 60.0f / 3600.0f, 1.0f);
        gl::RoomInputs ri;
        ri.night = 1; ri.hour = 23.5f; ri.selfLit = true; ri.origin = instance; ri.clock = &clock;
        gl::RoomBatchConstants(house, &placed, lt, ri, s, c, sdA);
        const float lo = c[47], hi = c[35];
        gl::RoomBatchConstants(house, &byWmo, lt, ri, s, c, sdB);
        gl::RoomBatchConstants(house, nullptr, lt, ri, s, c, sdC);
        const float now = gl::LightsOutShare(clock.Hour(), 0, 1), past = gl::LightsOutShare(clock.HourPast(), 0, 1);
        std::snprintf(line, sizeof(line), "batch constants: at 23:30 (the clock running) the band %.6f .. %.6f, slope %.0f, base %.0f (the band's ends "
                      "%.2f game s apart), the origin a placement's %.1f %.1f, by WMO the instance's %.1f %.1f, none the instance's %.1f; the fallback seed "
                      "the path's (%u)", lo, hi, sdA[3], sdA[7], (clock.Hour() - clock.HourPast()) * 3600.0f, sdA[4], sdA[5], sdB[4], sdB[5], sdC[4],
                      gl::FallbackSeed(gl::PathHash("World\\wmo\\a.wmo"), 7));
        Check(Near(lo, past, 1e-6f) && Near(hi, now, 1e-6f) && Near((clock.Hour() - clock.HourPast()) * 3600.0f, 1.5f, 0.05f) && Near(1.0f / sdA[3], now - past, 1e-6f) &&
              sdA[7] == 0 && sdA[4] == placed.pos[0] && sdA[6] == placed.pos[2] && sdB[4] == instance[0] && sdB[5] == instance[1] && sdC[4] == instance[0] &&
              gl::FallbackSeed(gl::PathHash("World\\wmo\\a.wmo"), 7) == gl::PathHash("world/wmo/A.wmo") && gl::FallbackSeed(0, 7) == 7, line);
    }

    // --- 2. the WMO edit ----------------------------------------------------------------------------------------
    struct Blob { std::string name; std::vector<uint8_t> code; };
    std::vector<Blob> LoadBlobs(const std::filesystem::path& dir)
    {
        std::vector<Blob> out;
        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            if (entry.path().extension() != ".bls") continue;
            std::ifstream f(entry.path(), std::ios::binary);
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            int k = 0;
            for (size_t i = 0; i + 8 <= data.size(); i += 4)
            {
                uint32_t token;
                std::memcpy(&token, data.data() + i, 4);
                if (token != 0xFFFF0300u) continue;
                size_t j = i + 4;
                for (; j + 4 <= data.size(); j += 4)
                {
                    uint32_t t;
                    std::memcpy(&t, data.data() + j, 4);
                    if (t == 0x0000FFFFu) break;
                }
                if (j + 4 > data.size()) break;
                char name[96];
                std::snprintf(name, sizeof(name), "%s_%02d", entry.path().stem().string().c_str(), k++);
                out.push_back({ name, std::vector<uint8_t>(data.begin() + i, data.begin() + j + 4) });
                i = j;
            }
        }
        return out;
    }

    int Slots(const std::vector<uint8_t>& code)
    {
        std::string text, why;
        if (!wxl_livingazeroth::shaderpatch::Disassemble(code.data(), code.size(), text, why)) return -1;
        std::smatch m;
        static const std::regex slots(R"(approximately (\d+) instruction slots)");
        return std::regex_search(text, m, slots) ? std::stoi(m[1].str()) : -1;
    }

    void EditTests(const std::filesystem::path& folder)
    {
        namespace mi = wxl_livingazeroth::modelice;
        namespace sp = wxl_livingazeroth::shaderpatch;
        if (!std::filesystem::exists(folder)) { Check(false, "WMO shader folder " + folder.string()); return; }
        const std::vector<Blob> blobs = LoadBlobs(folder);
        static const std::regex position(R"(dp3 r\d+\.\w, (v\d+), c3\b)"), normal(R"(dp3 r\d+\.\w, c4, (v\d+))");
        int shadow = 0, patched = 0, assembled = 0, withBlocks = 0, maxSlots = 0, maxIceSlots = 0;
        std::string worst;
        std::map<std::string, int> reasons;
        for (const Blob& b : blobs)
        {
            std::string src, why;
            if (!sp::Disassemble(b.code.data(), b.code.size(), src, why)) { reasons["disassemble: " + why]++; continue; }
            const bool isShadow = std::regex_search(src, position) && std::regex_search(src, normal);
            shadow += isShadow ? 1 : 0;
            std::string ice = src;
            std::string glassSrc = src;
            if (!mi::EditPixelWmoGlassForTests(glassSrc, why)) { reasons[std::string(isShadow ? "SHADOW VARIANT skipped: " : "skipped: ") + why]++; continue; }
            ++patched;
            withBlocks += glassSrc.find("if b9") != std::string::npos && glassSrc.find("if b10") != std::string::npos ? 1 : 0;
            std::vector<uint8_t> code;
            if (!sp::Assemble(glassSrc, b.name.c_str(), code, why))
            {
                // The assembler's message names the line: show it.
                std::smatch at;
                static const std::regex where(R"(\((\d+),[\d-]+\): (error [^\r\n]*))");
                std::string shown = why.substr(0, 200);
                if (std::regex_search(why, at, where))
                {
                    int line = std::stoi(at[1].str());
                    size_t pos = 0;
                    for (int l = 1; l < line && pos != std::string::npos; ++l) pos = glassSrc.find('\n', pos + 1);
                    const std::string text = pos == std::string::npos ? "?" : glassSrc.substr(pos + 1, glassSrc.find('\n', pos + 1) - pos - 1);
                    shown = at[2].str() + " at `" + text + "`";
                }
                reasons["assemble: " + shown]++;
                continue;
            }
            ++assembled;
            const int slots = Slots(code);
            if (slots > maxSlots) { maxSlots = slots; worst = b.name; }
            std::vector<uint8_t> iceCode;
            if (mi::EditPixelWmoForTests(ice, why) && sp::Assemble(ice, b.name.c_str(), iceCode, why)) maxIceSlots = std::max(maxIceSlots, Slots(iceCode));
        }
        for (const auto& r : reasons) std::printf("      %3d x %s\n", r.second, r.first.c_str());
        Check(blobs.size() == 112, "stock WMO pixel shaders found: " + std::to_string(blobs.size()) + " (7 sets x 16)");
        Check(patched == shadow && shadow == 84 && withBlocks == patched, "every WMO shadow variant takes the glass and floor blocks: " +
              std::to_string(withBlocks) + " of " + std::to_string(shadow));
        Check(assembled == patched, "every one assembles: " + std::to_string(assembled));
        char line[256];
        std::snprintf(line, sizeof(line), "largest WMO variant with the ice, the rims, the glass and the floor light: %d instruction slots (%s; %d before the glass), "
                      "under kGlassMinSlots %u and the ~3,500 the GTX 1660 SUPER takes", maxSlots, worst.c_str(), maxIceSlots, mi::kGlassMinSlots);
        Check(maxSlots > 0 && maxSlots <= static_cast<int>(mi::kGlassMinSlots) && maxSlots < 3500, line);
        {
            // The glass and floor blocks on their own (BlockShader: the WARP checks' shader).
            std::vector<uint8_t> blocks;
            std::string err;
            const std::string text = "ps_3_0\n    def c175, 1, 0, 0.5, 2\n    def c176, 0.2126, 0.7152, 0.0722, 0.2\n" + gl::ShaderDefs() +
                                     "    dcl_texcoord0 v0.xyz\n    dcl_texcoord1 v1.xyz\n    dcl_texcoord2 v2.xy\n    dcl_texcoord3 v3.xyz\n    dcl_2d s0\n"
                                     "    mov r12.xyz, v3\n    mov r31.xyz, c175.y" + gl::GlassBlock("v0", "v1", "v2") + gl::FloorBlock("v0", "v1", "v2") +
                                     "\n    add oC0.xyz, r12, r31\n    mov oC0.w, c175.x\n";
            const bool ok = sp::Assemble(text, "blocks", blocks, err);
            std::printf("info  the glass and floor blocks on their own: %d instruction slots%s\n", ok ? Slots(blocks) : -1, ok ? "" : err.substr(0, 200).c_str());
        }

        // The models' edit shares the code (and the fix to its matches read after the text moved): the M2 pixel
        // shaders beside the WMO ones (m2shaders), if they're there, still take the ice and assemble.
        const std::filesystem::path m2 = folder.parent_path() / "m2shaders";
        if (!std::filesystem::exists(m2)) { std::printf("skip  M2 edit (no %s)\n", m2.string().c_str()); return; }
        int m2Shadow = 0, m2Patched = 0, m2Assembled = 0;
        for (const Blob& b : LoadBlobs(m2))
        {
            std::string src, why;
            if (!sp::Disassemble(b.code.data(), b.code.size(), src, why)) continue;
            m2Shadow += std::regex_search(src, position) && std::regex_search(src, normal) ? 1 : 0;
            if (!mi::EditPixelForTests(src, why)) continue;
            ++m2Patched;
            std::vector<uint8_t> code;
            m2Assembled += sp::Assemble(src, b.name.c_str(), code, why) ? 1 : 0;
        }
        Check(m2Patched == m2Shadow && m2Patched > 0 && m2Assembled == m2Patched,
              "the M2 ice edit (same code): " + std::to_string(m2Patched) + " of " + std::to_string(m2Shadow) + " shadow variants patched, " +
              std::to_string(m2Assembled) + " assemble");
    }

    // --- 3. the shafts' shaders ---------------------------------------------------------------------------------
    std::vector<uint8_t> CompileHlsl(const char* source, std::string& error)
    {
        ID3DBlob* code = nullptr;
        ID3DBlob* err = nullptr;
        const HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", "ps_3_0", 0, 0, &code, &err);
        if (err) { error = static_cast<const char*>(err->GetBufferPointer()); err->Release(); }
        std::vector<uint8_t> out;
        if (SUCCEEDED(hr) && code)
        {
            const auto* p = static_cast<const uint8_t*>(code->GetBufferPointer());
            out.assign(p, p + code->GetBufferSize());
            code->Release();
        }
        return out;
    }

    void ShaftShaderTests()
    {
        namespace gs = wxl_livingazeroth::glassshafts;
        std::string e1, e2;
        const std::vector<uint8_t> march = CompileHlsl(gs::kGlassShaftMarchHlsl, e1), composite = CompileHlsl(gs::kGlassShaftCompositeHlsl, e2);
        char line[512];
        std::snprintf(line, sizeof(line), "coloured shafts' shaders compile (ps_3_0): march %d, composite %d instruction slots%s%s",
                      march.empty() ? -1 : Slots(march), composite.empty() ? -1 : Slots(composite), e1.empty() ? "" : " -- ", e1.substr(0, 300).c_str());
        Check(!march.empty() && !composite.empty() && Slots(march) < 512, line);
    }

    // --- 4. on WARP -----------------------------------------------------------------------------------------------
    template <class T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

    struct Warp
    {
        ID3D12Device* d12 = nullptr;
        IDirect3D9* d3d = nullptr;
        IDirect3DDevice9* dev = nullptr;
        IDirect3DSurface9* target = nullptr;
        IDirect3DSurface9* readback = nullptr;
        IDirect3DTexture9* texel = nullptr;
        ~Warp() { Release(texel); Release(readback); Release(target); Release(dev); Release(d3d); Release(d12); }
    };
    constexpr int kSide = 32;

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
        wc.lpszClassName = L"GlassTests";
        RegisterClassW(&wc);
        HWND window = CreateWindowW(wc.lpszClassName, L"glass", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window;
        pp.BackBufferWidth = 64; pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        if (FAILED(w.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &w.dev))) { why = "CreateDevice failed"; return false; }
        if (FAILED(w.dev->CreateRenderTarget(kSide, kSide, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &w.target, nullptr)) ||
            FAILED(w.dev->CreateOffscreenPlainSurface(kSide, kSide, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &w.readback, nullptr)))
        { why = "no float target"; return false; }
        if (FAILED(w.dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &w.texel, nullptr))) { why = "no texture"; return false; }
        D3DLOCKED_RECT lr;
        w.texel->LockRect(0, &lr, nullptr, 0);
        *static_cast<uint32_t*>(lr.pBits) = 0xFFCC4D33u;   // (0.8, 0.302, 0.2)
        w.texel->UnlockRect(0);
        return true;
    }

    // The room's look (c218, c219) as a RoomConstants has it, or the runtime's for a light.
    void UploadLook(IDirect3DDevice9* dev, const gl::RoomConstants& k)
    {
        float look[4 * gl::kLookRegisters];
        for (int i = 0; i < 4; ++i) { look[i] = k.c218[i]; look[4 + i] = k.c219[i]; }
        dev->SetPixelShaderConstantF(gl::kLookConstant, look, gl::kLookRegisters);
    }
    void UploadLook(IDirect3DDevice9* dev, const gl::Light& light)
    {
        float look[4 * gl::kLookRegisters];
        gl::LookConstants(light, look);
        dev->SetPixelShaderConstantF(gl::kLookConstant, look, gl::kLookRegisters);
    }

    // The blocks on their own: v0 = the view position, v1 = the normal, v2 = the uv, v3 = the stock colour (r12).
    std::string BlockShader()
    {
        return "ps_3_0\n    def c175, 1, 0, 0.5, 2\n    def c176, 0.2126, 0.7152, 0.0722, 0.2\n" + gl::ShaderDefs() +
               "    dcl_texcoord0 v0.xyz\n    dcl_texcoord1 v1.xyz\n    dcl_texcoord2 v2.xy\n    dcl_texcoord3 v3.xyz\n    dcl_2d s0\n"
               "    mov r12.xyz, v3\n    mov r31.xyz, c175.y" + gl::GlassBlock("v0", "v1", "v2") + gl::FloorBlock("v0", "v1", "v2") +
               "\n    add oC0.xyz, r12, r31\n    mov oC0.w, c175.x\n";
    }

    struct V { float x, y, z, rhw, p[3], n[3], uv[2], base[3]; };

    // Draws the quad (corner values as given, interpolated) with the constants, returns the target's pixels (rgb).
    bool Draw(Warp& w, IDirect3DPixelShader9* ps, const V corners[4], const float* consts, int first, int count, bool glass, bool floor,
              std::vector<float>& rgb)
    {
        IDirect3DDevice9* d = w.dev;
        d->SetRenderTarget(0, w.target);
        D3DVIEWPORT9 vp{ 0, 0, kSide, kSide, 0.0f, 1.0f };
        d->SetViewport(&vp);
        d->SetRenderState(D3DRS_ZENABLE, FALSE); d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE); d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        d->SetVertexShader(nullptr); d->SetPixelShader(ps);
        d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE3(0) | D3DFVF_TEXCOORDSIZE3(1) | D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE3(3));
        d->SetTexture(0, w.texel);
        d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT); d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
        d->SetPixelShaderConstantF(first, consts, count);
        const BOOL b9 = glass ? TRUE : FALSE, b10 = floor ? TRUE : FALSE;
        d->SetPixelShaderConstantB(gl::kSwitch, &b9, 1);
        d->SetPixelShaderConstantB(gl::kFloorSwitch, &b10, 1);
        if (FAILED(d->BeginScene())) return false;
        d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, corners, sizeof(V));
        d->EndScene();
        D3DLOCKED_RECT lr{};
        if (FAILED(d->GetRenderTargetData(w.target, w.readback)) || FAILED(w.readback->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
        rgb.resize(static_cast<size_t>(kSide) * kSide * 3);
        for (int row = 0; row < kSide; ++row)
            for (int col = 0; col < kSide; ++col)
                for (int k = 0; k < 3; ++k)
                    rgb[(static_cast<size_t>(row) * kSide + col) * 3 + k] = reinterpret_cast<const float*>(static_cast<const uint8_t*>(lr.pBits) + row * lr.Pitch)[col * 4 + k];
        w.readback->UnlockRect();
        return true;
    }

    void WarpTests()
    {
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("skip  WARP checks: %s\n", why.c_str()); return; }
        std::vector<uint8_t> code;
        if (!wxl_livingazeroth::shaderpatch::Assemble(BlockShader(), "glass blocks", code, why)) { Check(false, "the blocks on their own assemble: " + why.substr(0, 300)); return; }
        IDirect3DPixelShader9* ps = nullptr;
        if (FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps))) { Check(false, "the blocks' shader on WARP"); return; }
        const float e = static_cast<float>(kSide) - 0.5f;
        auto quad = [&](const float p[4][3], const float n[3], const float base[3], V out[4])
        {
            const float xy[4][2] = { { -0.5f, -0.5f }, { e, -0.5f }, { -0.5f, e }, { e, e } };
            for (int c = 0; c < 4; ++c)
            {
                out[c] = { xy[c][0], xy[c][1], 0, 1, { p[c][0], p[c][1], p[c][2] }, { n[0], n[1], n[2] }, { 0.5f, 0.5f }, { base[0], base[1], base[2] } };
            }
        };
        const float texel[3] = { 0.8f, 77.0f / 255.0f, 0.2f };
        // The view's rows as the identity: view space = world axes, the camera at the origin (c180..c183); the room's
        // night look (c218, c219) unless a check sets its own.
        float view[16] = { 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
        w.dev->SetPixelShaderConstantF(gl::kViewConstant, view, 4);
        UploadLook(w.dev, gl::RoomConstants{});

        // Inside: a pane 5 yd ahead (+x), its face toward the eye; the sun beyond it.
        {
            const float p[4][3] = { { 5, 0, 0 }, { 5, 0, 0 }, { 5, 0, 0 }, { 5, 0, 0 } };
            const float n[3] = { -1, 0, 0 }, base[3] = { 0.1f, 0.1f, 0.1f };
            V v[4];
            quad(p, n, base, v);
            float sun[3] = { 1, 0, 0.4f }; Norm(sun);
            float c[4 * gl::kConstantCount] = {};
            c[0] = c[1] = c[2] = 0; c[3] = 1;            // mask: all glass
            c[4] = 1; c[5] = 1; c[6] = 0; c[7] = 0;      // inside, strength 1, no room, no thickness
            c[8] = 0.3f; c[9] = 0.35f; c[10] = 0.4f; c[11] = 0.9f;   // sky through, sun gain
            c[12] = sun[0]; c[13] = sun[1]; c[14] = sun[2]; c[15] = 1.3f;
            c[16] = c[17] = c[18] = 0.5f; c[19] = 1;
            c[20] = 0.5f; c[21] = 0; c[22] = 0.04f; c[23] = 0.2f;
            c[24] = 1 / 3.5f; c[25] = 1 / 3.2f; c[26] = 0.25f; c[27] = 0.3f;
            c[28] = 0.85f; c[29] = 0.72f; c[30] = 0.55f; c[31] = 1.6f;
            c[32] = 1.0f; c[33] = 0.95f; c[34] = 0.85f; c[36] = 1;
            std::vector<float> rgb;
            const bool drew = Draw(w, ps, v, c, gl::kFirstConstant, gl::kConstantCount, true, false, rgb);
            const float out[3] = { 1, 0, 0 };
            const float through = gl::SunThrough(sun, out, 0.2f);
            const float L[3] = { (0.3f + 1.0f * 0.9f * through) * 1.6f, (0.35f + 0.95f * 0.9f * through) * 1.6f, (0.4f + 0.85f * 0.9f * through) * 1.6f };
            float expect[3];
            gl::InsideColour(texel, base, 1.3f, L, expect);
            const size_t mid = (static_cast<size_t>(kSide / 2) * kSide + kSide / 2) * 3;
            char line[256];
            std::snprintf(line, sizeof(line), "WARP, inside: %.3f %.3f %.3f against the rules' %.3f %.3f %.3f", drew ? rgb[mid] : -1, drew ? rgb[mid + 1] : -1,
                          drew ? rgb[mid + 2] : -1, expect[0], expect[1], expect[2]);
            Check(drew && Near(rgb[mid], expect[0], 0.01f) && Near(rgb[mid + 1], expect[1], 0.01f) && Near(rgb[mid + 2], expect[2], 0.01f), line);

            // Outside, face on and at a slant (the sun behind the eye: no glint): (1 - darken) base, then Fresnel to the sky.
            c[4] = 0;
            float tilted[3] = { -1, 3, 0 }; Norm(tilted);   // cos 0.32: Fresnel ~0.18
            for (int pass = 0; pass < 2; ++pass)
            {
                const float* nn = pass ? tilted : n;
                quad(p, nn, base, v);
                const bool ok = Draw(w, ps, v, c, gl::kFirstConstant, gl::kConstantCount, true, false, rgb);
                const float cosine = -nn[0];
                const float f = gl::Fresnel(cosine, 0.04f);
                const float want = (0.1f * 0.5f) * (1 - f) + 0.5f * f;
                std::snprintf(line, sizeof(line), "WARP, outside %s (cos %.2f): %.4f against the rules' %.4f (Fresnel %.3f)", pass ? "at a slant" : "face on", cosine,
                              ok ? rgb[mid] : -1, want, f);
                Check(ok && Near(rgb[mid], want, 0.003f), line);
            }
            // By day cooler (c85): warm painted glass under a blue sky (its hue; no reflection), face on and at a slant, against
            // the rules (SkyAlong, DayCool); cooler than without, more so at the slant.
            {
                float cc[4 * gl::kConstantCount];
                std::memcpy(cc, c, sizeof(cc));
                cc[16] = 0.45f; cc[17] = 0.55f; cc[18] = 0.8f;
                cc[19] = 0;   // no reflection: the cooling alone (more at a slant)
                const float warm[3] = { 0.6f, 0.45f, 0.2f };
                const float c85[4] = { 0.5f, gl::room::kCoolUp, gl::room::kCoolGraze, gl::room::kCoolTint };
                const float noCool[8] = {}, cool[8] = { c85[0], c85[1], c85[2], c85[3], 0, 0, 0, 0 };
                float shift[2] = {}, err = 0;
                bool ok = true;
                for (int pass = 0; pass < 2; ++pass)
                {
                    const float* nn = pass ? tilted : n;
                    quad(p, nn, warm, v);
                    std::vector<float> plain, cooled;
                    w.dev->SetPixelShaderConstantF(gl::kDayConstant, noCool, gl::kDayRegisters);
                    ok = ok && Draw(w, ps, v, cc, gl::kFirstConstant, gl::kConstantCount, true, false, plain);
                    w.dev->SetPixelShaderConstantF(gl::kDayConstant, cool, gl::kDayRegisters);
                    ok = ok && Draw(w, ps, v, cc, gl::kFirstConstant, gl::kConstantCount, true, false, cooled);
                    if (!ok) break;
                    const float N[3] = { nn[0], nn[1], nn[2] }, V[3] = { -1, 0, 0 };
                    const float cosine = std::max(0.0f, N[0] * V[0] + N[1] * V[1] + N[2] * V[2]);
                    float sky[3], glass[3], cooledGlass[3], want[3];
                    const float height = gl::SkyAlong(N, V, cc + 16, sky);
                    for (int k = 0; k < 3; ++k) glass[k] = warm[k] * (1 - cc[20]);
                    gl::DayCool(glass, sky, height, cosine, c85, cooledGlass);
                    const float f = gl::Fresnel(cosine, cc[22]) * cc[19];
                    for (int k = 0; k < 3; ++k) { want[k] = sky[k] * f + cooledGlass[k] * (1 - f); err = std::max(err, std::fabs(cooled[mid + k] - want[k])); }
                    auto warmth = [](const float* q) { return (q[0] - q[2]) / (q[0] + q[1] + q[2]); };
                    shift[pass] = warmth(&plain[mid]) - warmth(&cooled[mid]);
                }
                w.dev->SetPixelShaderConstantF(gl::kDayConstant, noCool, gl::kDayRegisters);
                char cl[256];
                std::snprintf(cl, sizeof(cl), "WARP, outside by day cooler (c85.x 0.5): off from the rules by %.5f; warmth (R - B) / (R + G + B) down by %.3f face on, "
                              "%.3f at a slant", err, shift[0], shift[1]);
                Check(ok && err < 0.003f && shift[0] > 0.03f && shift[1] > shift[0], cl);
            }
            // The room behind clear glass changes it, stays finite and in range.
            c[6] = 0.75f;
            quad(p, tilted, base, v);
            std::vector<float> room;
            const bool ok = Draw(w, ps, v, c, gl::kFirstConstant, gl::kConstantCount, true, false, room);
            bool finite = ok;
            for (float x : room) finite = finite && std::isfinite(x) && x >= 0 && x <= 2;
            std::snprintf(line, sizeof(line), "WARP, the fake room behind the glass: %.3f %.3f %.3f (finite, in range, not the plain outside)", ok ? room[mid] : -1,
                          ok ? room[mid + 1] : -1, ok ? room[mid + 2] : -1);
            Check(finite && std::fabs(room[mid] - rgb[mid]) > 0.002f, line);
        }

        // The floor light: a floor (z = 0) from x -6 .. 0, y -2 .. 2 under the wall window, against the rules.
        {
            const gl::Pane pane = WallWindow();
            float sun[3] = { 1, 0, 1 }; Norm(sun);
            const float origin[3] = { 0, 0, 0 };
            float panes[4 * gl::kPaneRegisters * gl::kFloorPanes] = {};
            gl::ProjectionRows(pane, sun, origin, 30, panes);
            for (int k = 0; k < 3; ++k) { panes[12 + k] = pane.light[k]; panes[16 + k] = pane.roomMin[k]; panes[20 + k] = pane.roomMax[k]; }
            panes[15] = 1; panes[19] = 6; panes[23] = 4;
            float c[4 * gl::kConstantCount] = {};
            c[12] = sun[0]; c[13] = sun[1]; c[14] = sun[2];
            c[36] = 1;
            w.dev->SetPixelShaderConstantF(gl::kPaneConstant, panes, gl::kPaneRegisters * gl::kFloorPanes);
            const float p[4][3] = { { -6, -2, 0 }, { 0, -2, 0 }, { -6, 2, 0 }, { 0, 2, 0 } };
            const float n[3] = { 0, 0, 1 }, base[3] = { 0, 0, 0 };
            V v[4];
            quad(p, n, base, v);
            // the quad's x runs along screen x (left -6 .. right 0) and y along screen y
            std::vector<float> rgb;
            const bool drew = Draw(w, ps, v, c, gl::kFirstConstant, gl::kConstantCount, false, true, rgb);
            float worst = 0, peak = 0;
            int lit = 0;
            for (int row = 0; row < kSide && drew; ++row)
                for (int col = 0; col < kSide; ++col)
                {
                    const float x = -6 + (col + 0.5f) / kSide * 6, y = -2 + (row + 0.5f) / kSide * 4;
                    const float q[3] = { x, y, 0 };
                    const float weight = gl::FloorWeightWorld(pane, sun, q, 30, 6, 4);
                    const float want = texel[0] * pane.light[0] * weight * sun[2];
                    const float got = rgb[(static_cast<size_t>(row) * kSide + col) * 3];
                    worst = std::max(worst, std::fabs(got - want));
                    peak = std::max(peak, got);
                    lit += got > 0.05f ? 1 : 0;
                }
            char line[256];
            std::snprintf(line, sizeof(line), "WARP, the floor light over %d x %d floor points: off from the rules by up to %.4f (peak %.3f, %d lit)", kSide, kSide, worst, peak, lit);
            Check(drew && worst < 0.02f && peak > 0.3f && lit > 100 && lit < kSide * kSide / 2, line);
        }

        // The rooms behind the glass against RoomOutside, pixel by pixel: a pane at x = 5 facing the eye, 2.4 x 1.8 yd
        // (y -1.2 .. 1.2, z 0.2 .. 2: a pixel 0.075 yd, so the detail shows at 84%), with no reflection or glint so the
        // output is the room seen through the glass. Each case forces a layout (or night, dark, a city's blocks) over
        // eight buildings' seeds.
        {
            gl::Settings s;
            gl::RoomStyle elf;
            elf.tint[0] = 0.95f; elf.tint[1] = 0.78f; elf.tint[2] = 0.48f; elf.tint2[0] = 0.62f; elf.tint2[1] = 0.64f; elf.tint2[2] = 0.92f;
            elf.lamp[0] = 1.0f; elf.lamp[1] = 0.62f; elf.lamp[2] = 0.3f; elf.spread = 0.3f; elf.darkShare = 0.15f;
            const float p[4][3] = { { 5, 1.2f, 2.0f }, { 5, -1.2f, 2.0f }, { 5, 1.2f, 0.2f }, { 5, -1.2f, 0.2f } };
            // the pixel's footprint on the glass, squared, as the shader's screen gradients have it (an affine quad)
            const float footprint2 = std::max(2.4f * 2.4f, 1.8f * 1.8f) / (kSide * kSide);
            const float n[3] = { -1, 0, 0 };
            struct Case { const char* name; int layout; float night; bool selfLit; uint32_t flags; float r12[3]; uint32_t kind = gl::kClear; float lampFlicker = 0; };
            const Case cases[] = {
                { "plain, by day", gl::kLayoutPlain, 0, false, 0, { 0.2f, 0.2f, 0.2f } },
                { "shelves, by day", gl::kLayoutShelves, 0, false, 0, { 0.2f, 0.2f, 0.2f } },
                { "curtains, by day", gl::kLayoutCurtains, 0, false, 0, { 0.2f, 0.2f, 0.2f } },
                { "a dividing wall, by day", gl::kLayoutDivider, 0, false, 0, { 0.2f, 0.2f, 0.2f } },
                { "the house's mix, a city's blocks, by day", -1, 0, false, gl::kBuildingCity, { 0.2f, 0.2f, 0.2f } },
                { "the house's mix at 23:00, lamp-lit", -1, 1, true, 0, { 0.62f, 0.47f, 0.28f } },
                { "curtains at 23:00, backlit", gl::kLayoutCurtains, 1, true, 0, { 0.62f, 0.47f, 0.28f } },
                { "a dark place at night", -1, 1, true, gl::kBuildingDarkPlace, { 0.62f, 0.47f, 0.28f } },
                { "the house's mix at 23:00, lamp-lit, the lamps flickering (LampFlicker 0.12)", -1, 1, true, 0, { 0.62f, 0.47f, 0.28f }, gl::kClear, 0.12f },
                { "frosted glass at 02:00 (no room: dark ones hide their glow, lit ones a hearth's flicker)", -1, 1, true, 0, { 0.62f, 0.47f, 0.28f }, gl::kFrosted },
            };
            for (const Case& cs : cases)
            {
                float spread = 0;
                int off = 0, total = 0;
                std::vector<float> errors;
                double mean = 0;
                float lo = 1e9f, hi = -1e9f;
                bool drewAll = true;
                for (uint32_t seedNo = 0; seedNo < 8; ++seedNo)
                {
                    gl::RoomStyle st = elf;
                    if (cs.layout >= 0) for (int k = 0; k < 4; ++k) st.weights[k] = k == cs.layout ? 1.0f : 0.0f;
                    gl::Building b;
                    b.seed = (seedNo + 1) * 2654435761u;
                    b.flags = cs.flags;
                    float sd[4 * gl::kSeedRegisters];
                    gl::Settings si = s;
                    if (cs.night > 0 && cs.selfLit && !(cs.flags & gl::kBuildingDarkPlace)) si.lightsOut = 0;
                    si.lampFlicker = cs.lampFlicker;
                    const bool frosted = cs.kind == gl::kFrosted;
                    gl::RoomConstants k = RoomFor(st, &b, cs.night, frosted ? 2.0f : 23.0f, cs.selfLit, si, cs.night > 0 ? 0.0f : 1.0f, sd, cs.kind,
                                                  137.25f + 61.5f * seedNo);
                    k.footprint2 = footprint2;
                    float c[4 * gl::kConstantCount] = {};
                    c[3] = 1;                                     // mask: all glass
                    c[4] = 0; c[5] = 1; c[6] = k.share; c[7] = 0; // outside, no thickness
                    c[8] = 0.3f; c[9] = 0.35f; c[10] = 0.4f; c[11] = 0;
                    float sun[3] = { -0.3f, 0.2f, 0.9f }; Norm(sun);
                    c[12] = sun[0]; c[13] = sun[1]; c[14] = sun[2]; c[15] = 1;
                    c[16] = c[17] = c[18] = 0.5f; c[19] = 0;      // no reflection
                    c[20] = 0; c[21] = 0; c[22] = 0.04f; c[23] = 0.2f;   // no darkening, no glint
                    for (int i = 0; i < 4; ++i)
                    {
                        c[24 + i] = k.c198[i]; c[28 + i] = k.c199[i]; c[40 + i] = k.c202[i]; c[44 + i] = k.c203[i]; c[48 + i] = k.c204[i]; c[52 + i] = k.c205[i];
                    }
                    c[32] = 1; c[33] = 0.95f; c[34] = 0.85f; c[35] = k.c200w; c[36] = 1;
                    c[37] = k.c201[1]; c[38] = k.c201[2]; c[39] = k.c201[3];
                    w.dev->SetPixelShaderConstantF(gl::kSeedConstant, sd, gl::kSeedRegisters);
                    UploadLook(w.dev, k);
                    // the room by day (the sun's patch, farther darker), not the glass's cooling (RoomOutside is the room's)
                    float dayRegs[4 * gl::kDayRegisters] = {};
                    for (int i = 0; i < 4; ++i) dayRegs[4 + i] = k.c86[i];
                    w.dev->SetPixelShaderConstantF(gl::kDayConstant, dayRegs, gl::kDayRegisters);
                    V v[4];
                    quad(p, n, cs.r12, v);
                    std::vector<float> rgb;
                    const bool drew = Draw(w, ps, v, c, gl::kFirstConstant, gl::kConstantCount, true, false, rgb);
                    drewAll = drewAll && drew;
                    if (!drew) continue;
                    for (int row = 0; row < kSide; ++row)
                        for (int col = 0; col < kSide; ++col)
                        {
                            const float fu = (col + 0.5f) / kSide, fv = (row + 0.5f) / kSide;
                            float P[3];
                            for (int i = 0; i < 3; ++i) P[i] = p[0][i] + (p[1][i] - p[0][i]) * fu + (p[2][i] - p[0][i]) * fv;
                            float V3[3] = { -P[0], -P[1], -P[2] }; Norm(V3);
                            float want[3];
                            gl::RoomOutside(P, V3, n, k, cs.r12, cs.r12, want, nullptr);
                            const size_t at = (static_cast<size_t>(row) * kSide + col) * 3;
                            float e = 0;
                            for (int i = 0; i < 3; ++i) e = std::max(e, std::isfinite(rgb[at + i]) ? std::fabs(rgb[at + i] - want[i]) : 1e9f);
                            // a shelf's or a doorway's edge a hair away is off by a lot at a few points
                            if (e > 0.01f * std::max(1.0f, std::max(want[0], std::max(want[1], want[2])))) ++off;
                            errors.push_back(e);
                            const float l = 0.2126f * rgb[at] + 0.7152f * rgb[at + 1] + 0.0722f * rgb[at + 2];
                            mean += l; lo = std::min(lo, l); hi = std::max(hi, l);
                            ++total;
                        }
                }
                spread = hi - lo;
                std::sort(errors.begin(), errors.end());
                const float median = errors.empty() ? 1.0f : errors[errors.size() / 2], p95 = errors.empty() ? 1.0f : errors[errors.size() * 95 / 100];
                char line[320];
                std::snprintf(line, sizeof(line), "WARP, the room (%s): %d points against RoomOutside, off by %.5f (median), %.4f (95%%); %d by more than 1%% "
                              "(an edge a hair away); luminance %.3f (%.3f .. %.3f)", cs.name, total, median, p95, off, total ? mean / total : 0.0, lo, hi);
                Check(drewAll && total > 0 && off <= total / 25 && median < 0.001f && (spread > 0.005f || (cs.flags & gl::kBuildingDarkPlace)), line);
            }
            w.dev->SetPixelShaderConstantF(gl::kSeedConstant, kZeroSeed, gl::kSeedRegisters);
            const float noDay[4 * gl::kDayRegisters] = {};
            w.dev->SetPixelShaderConstantF(gl::kDayConstant, noDay, gl::kDayRegisters);
        }

        // Degenerate rays, the runtime's constants for leaded glass at night (rooms dark) and by day, the glint on:
        // the ray straight along the pane's normal (the room's steps along and up exactly 0, or -0), the eye straight
        // toward the sun (the glint's half vector 0), the pane edge-on (cos 0), a pixel with no screen gradients (the
        // thickness's determinant 0); about the origin and at Stormwind's camera. No NaN or Inf.
        {
            gl::Settings s;
            gl::Material m;
            m.kind = gl::kLeaded;
            int bad = 0, pixels = 0;
            bool drewAll = true;
            for (int night = 0; night < 2; ++night)
            {
                gl::Inputs in;
                in.sun[0] = 0.3f; in.sun[1] = -0.2f; in.sun[2] = night ? -0.93f : 0.93f; Norm(in.sun);
                in.daylight = night ? 0.0f : 1.0f; in.night = night ? 1.0f : 0.0f;
                const gl::Light light = gl::Evaluate(in);
                float c[4 * gl::kConstantCount], sd[4 * gl::kSeedRegisters];
                gl::BatchConstants(m, 0.0f, light, in.sun, in.sunColour, in.sky, s, c);
                gl::Building b;
                b.seed = 77; b.flags = gl::kBuildingCity;
                gl::RoomInputs ri;
                ri.night = night ? 1.0f : 0.0f; ri.hour = night ? 23.0f : 13.0f; ri.selfLit = true;
                gl::RoomBatchConstants(gl::DefaultStyle(), &b, light, ri, s, c, sd);
                if (night) c[47] = c[35] = 1.0f;   // every room dark
                c[7] = 0.012f;             // the thickness
                c[21] = 1.0f;              // the glint, even at night: a NaN in it would show
                w.dev->SetPixelShaderConstantF(gl::kSeedConstant, sd, gl::kSeedRegisters);
                UploadLook(w.dev, light);
                struct Probe { float p[3], n[3], sun[3]; };
                const Probe probes[] = {
                    { { 5, 0, 0 }, { -1, 0, 0 }, { 0.3f, -0.2f, 0.93f } },                // along the normal
                    { { 5, -0.0f, -0.0f }, { -1, -0.0f, -0.0f }, { 0.3f, -0.2f, 0.93f } }, // ... with -0
                    { { 5, 0, 0 }, { -1, 0, 0 }, { 1, 0, 0 } },                           // the eye toward the sun
                    { { 5, 0, 0 }, { 0, 1, 0 }, { 0.3f, -0.2f, 0.93f } },                 // edge-on
                    { { 0, 0, 5 }, { 0, 0, -1 }, { 0.3f, -0.2f, 0.93f } },                // a skylight from below
                };
                for (int where = 0; where < 2; ++where)
                {
                    float camView[16] = { 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
                    if (where) { camView[0] = -8838.6f; camView[1] = 627.4f; camView[2] = 99.3f; }
                    w.dev->SetPixelShaderConstantF(gl::kViewConstant, camView, 4);
                    for (const Probe& pr : probes)
                    {
                        float sun[3] = { pr.sun[0], pr.sun[1], pr.sun[2] }; Norm(sun);
                        c[12] = sun[0]; c[13] = sun[1]; c[14] = sun[2];
                        const float p[4][3] = { { pr.p[0], pr.p[1], pr.p[2] }, { pr.p[0], pr.p[1], pr.p[2] }, { pr.p[0], pr.p[1], pr.p[2] }, { pr.p[0], pr.p[1], pr.p[2] } };
                        const float base[3] = { 0.62f, 0.47f, 0.28f };
                        V v[4];
                        quad(p, pr.n, base, v);
                        std::vector<float> rgb;
                        const bool drew = Draw(w, ps, v, c, gl::kFirstConstant, gl::kConstantCount, true, false, rgb);
                        drewAll = drewAll && drew;
                        for (float x : rgb) bad += std::isfinite(x) ? 0 : 1;
                        pixels += static_cast<int>(rgb.size() / 3);
                    }
                }
            }
            w.dev->SetPixelShaderConstantF(gl::kViewConstant, view, 4);
            w.dev->SetPixelShaderConstantF(gl::kSeedConstant, kZeroSeed, gl::kSeedRegisters);
            UploadLook(w.dev, gl::RoomConstants{});
            char line[256];
            std::snprintf(line, sizeof(line), "WARP, degenerate rays (along the normal, +-0; the eye toward the sun; edge-on; no gradients; about the origin and "
                          "Stormwind's camera; night and day): %d NaN/Inf of %d pixels' channels", bad, pixels * 3);
            Check(drewAll && pixels > 0 && bad == 0, line);
        }
        ps->Release();

        // The coloured shafts' march: in the wall window's room, the camera at (-5, 0, 1) looking along +x (at the
        // window), the sun from +x at 45 degrees; no surfaces (the sky's depth). Along the middle ray the lit air is
        // x -3.5 .. -0.5 (the ray toward the sun from (x, 0, 1) leaves at z = 1 - x): the march against the rules'
        // integral of the same, in the pane's colour; a ray down into the floor's corner meets none of it.
        {
            namespace gs = wxl_livingazeroth::glassshafts;
            std::string err;
            const std::vector<uint8_t> marchCode = CompileHlsl(gs::kGlassShaftMarchHlsl, err);
            IDirect3DPixelShader9* march = nullptr;
            if (marchCode.empty() || FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(marchCode.data()), &march)))
            { Check(false, "the march on WARP: " + err.substr(0, 200)); return; }
            IDirect3DTexture9* depth = nullptr;
            w.dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &depth, nullptr);
            const gl::Pane pane = WallWindow();
            float sun[3] = { 1, 0, 1 }; Norm(sun);
            const float cam[3] = { -5, 0, 1 };
            float rows[12];
            gl::ProjectionRows(pane, sun, cam, 30, rows);
            constexpr int kN = gs::kMaxPanes;
            std::vector<float> panes(4 * gs::kPaneArrays * kN, 0.0f);
            auto reg = [&](int array) { return panes.data() + 4 * (array * kN); };
            std::memcpy(reg(0), rows, 16); std::memcpy(reg(1), rows + 4, 16); std::memcpy(reg(2), rows + 8, 16);
            for (int k = 0; k < 3; ++k) { reg(3)[k] = pane.light[k]; reg(4)[k] = pane.roomMin[k] - cam[k]; reg(5)[k] = pane.roomMax[k] - cam[k]; }
            reg(3)[3] = 1; reg(4)[3] = 5; reg(5)[3] = 4;
            const float density = 0.03f, forward = 0.6f;
            const float c[11 * 4] = {
                0, 1, -1, 1,                                  // every pixel the sky: the march runs to its reach
                1, 1, 0, 0,                                   // 90 degrees, centred
                0.5f, 0.5f, 45, 32,                           // the reach (yd) and 32 steps
                0, 0, 1, 1,
                1.0f / kSide, 1.0f / kSide, density, 1,       // one pane
                0, -1, 0, 0,                                  // view x -> world -y
                0, 0, 1, 0,                                   // view y -> world z
                1, 0, 0, 0,                                   // view z (ahead) -> world x
                sun[0], sun[1], sun[2], forward,
                reg(4)[0], reg(4)[1], reg(4)[2], 0,
                reg(5)[0], reg(5)[1], reg(5)[2], 0,
            };
            IDirect3DDevice9* d = w.dev;
            d->SetRenderTarget(0, w.target);
            D3DVIEWPORT9 vp{ 0, 0, kSide, kSide, 0.0f, 1.0f };
            d->SetViewport(&vp);
            d->SetPixelShader(march);
            d->SetPixelShaderConstantF(0, c, 11);
            d->SetPixelShaderConstantF(gs::kFirstPaneConstant, panes.data(), gs::kPaneArrays * kN);
            d->SetTexture(0, depth);
            d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
            struct Q { float x, y, z, rhw, u, v; };
            const Q q[4] = { { -0.5f, -0.5f, 0, 1, 0, 0 }, { e, -0.5f, 0, 1, 1, 0 }, { -0.5f, e, 0, 1, 0, 1 }, { e, e, 0, 1, 1, 1 } };
            std::vector<float> out;
            bool drew = SUCCEEDED(d->BeginScene());
            if (drew)
            {
                d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Q));
                d->EndScene();
                D3DLOCKED_RECT lr{};
                drew = SUCCEEDED(d->GetRenderTargetData(w.target, w.readback)) && SUCCEEDED(w.readback->LockRect(&lr, nullptr, D3DLOCK_READONLY));
                if (drew)
                {
                    out.resize(static_cast<size_t>(kSide) * kSide * 3);
                    for (int row = 0; row < kSide; ++row)
                        for (int col = 0; col < kSide; ++col)
                            for (int k = 0; k < 3; ++k)
                                out[(static_cast<size_t>(row) * kSide + col) * 3 + k] = reinterpret_cast<const float*>(static_cast<const uint8_t*>(lr.pBits) + row * lr.Pitch)[col * 4 + k];
                    w.readback->UnlockRect();
                }
            }
            // The rules' integral along a pixel's ray (fine steps), as the shader's phase and density weigh it.
            auto integral = [&](int col, int row)
            {
                const float u = (col + 0.5f) / kSide, v = (row + 0.5f) / kSide;
                float dir[3] = { 1, -(u * 2 - 1), (1 - v * 2) };
                Norm(dir);
                float sum = 0;
                const int steps = 4000;
                for (int i = 0; i < steps; ++i)
                {
                    const float s = 45.0f * (i + 0.5f) / steps;
                    const float pnt[3] = { cam[0] + dir[0] * s, cam[1] + dir[1] * s, cam[2] + dir[2] * s };
                    sum += gl::FloorWeightWorld(pane, sun, pnt, 30, 5, 4) * 45.0f / steps;
                }
                const float mu = std::clamp((dir[0] * sun[0] + dir[1] * sun[1] + dir[2] * sun[2]) * 0.5f + 0.5f, 0.0f, 1.0f);
                return sum * density * ((1 - forward) + forward * 3.5f * std::pow(mu, 6.0f));
            };
            float got = 0, want = 0;
            for (int row = 14; row < 18; ++row)
                for (int col = 14; col < 18; ++col)
                {
                    if (drew) got += out[(static_cast<size_t>(row) * kSide + col) * 3] / 16;
                    want += integral(col, row) / 16;
                }
            const float green = drew ? out[(static_cast<size_t>(16) * kSide + 16) * 3 + 1] / std::max(out[(static_cast<size_t>(16) * kSide + 16) * 3], 1e-6f) : 0;
            const float corner = drew ? out[(static_cast<size_t>(kSide - 1) * kSide + kSide - 1) * 3] : -1;
            char line[256];
            std::snprintf(line, sizeof(line), "WARP, the coloured shafts' march: the middle rays %.3f against the rules' integral %.3f; green / red %.2f (the pane's 0.50); "
                          "a ray into the floor's corner %.3f (rules %.3f)", got, want, green, corner, integral(kSide - 1, kSide - 1));
            Check(drew && want > 0.03f && std::fabs(got - want) < 0.2f * want && std::fabs(green - 0.5f) < 0.02f && corner < 0.25f * got, line);
            depth->Release();
            march->Release();
        }
    }

    // --- preview (GLASS_PREVIEW=<folder>: for each <name>.rgba there, 256 x 256 RGBA texels of a window texture, a
    // <name>.ppm of it as a tilted 2 x 3 yd pane 4 yd ahead: stock, outside by day, inside by day, outside at night) --
    void Preview(const char* folder)
    {
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("preview: %s\n", why.c_str()); return; }
        const std::string text = "ps_3_0\n    def c175, 1, 0, 0.5, 2\n    def c176, 0.2126, 0.7152, 0.0722, 0.2\n" + gl::ShaderDefs() +
                                 "    dcl_texcoord0 v0.xyz\n    dcl_texcoord1 v1.xyz\n    dcl_texcoord2 v2.xy\n    dcl_texcoord3 v3.xyz\n    dcl_2d s0\n"
                                 "    texld r0, v2, s0\n    mul r12.xyz, r0, v3\n    mov r31.xyz, c175.y" + gl::GlassBlock("v0", "v1", "v2") +
                                 "\n    add oC0.xyz, r12, r31\n    mov oC0.w, c175.x\n";
        std::vector<uint8_t> code;
        if (!wxl_livingazeroth::shaderpatch::Assemble(text, "preview", code, why)) { std::printf("preview: %s\n", why.c_str()); return; }
        IDirect3DPixelShader9* ps = nullptr;
        w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps);
        constexpr int kOut = 160;
        IDirect3DSurface9* target = nullptr;
        IDirect3DSurface9* readback = nullptr;
        w.dev->CreateRenderTarget(kOut, kOut, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr);
        w.dev->CreateOffscreenPlainSurface(kOut, kOut, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &readback, nullptr);
        const float view[16] = { 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
        w.dev->SetPixelShaderConstantF(gl::kViewConstant, view, 4);
        for (const auto& entry : std::filesystem::directory_iterator(folder))
        {
            if (entry.path().extension() != ".rgba") continue;
            std::ifstream f(entry.path(), std::ios::binary);
            std::vector<uint8_t> px((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (px.size() != 256 * 256 * 4) continue;
            IDirect3DTexture9* tex = nullptr;
            w.dev->CreateTexture(256, 256, 0, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr);
            D3DLOCKED_RECT lr;
            tex->LockRect(0, &lr, nullptr, 0);
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256; ++x)
                {
                    const uint8_t* s = &px[(static_cast<size_t>(y) * 256 + x) * 4];
                    reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch)[x] = (uint32_t(s[3]) << 24) | (uint32_t(s[0]) << 16) | (uint32_t(s[1]) << 8) | s[2];
                }
            tex->UnlockRect(0);
            const std::string kind = entry.path().stem().string();
            std::vector<uint8_t> image(static_cast<size_t>(kOut) * 4 * kOut * 3);
            for (int pass = 0; pass < 4; ++pass)
            {
                // the pane: x = 4 + 0.6 y (turned ~30 degrees), 2 yd wide, 3 yd tall; its face toward the eye
                float n[3] = { -1, 0.6f, 0 }; Norm(n);
                const float e = kOut - 0.5f;
                const float level = pass == 2 ? 0.35f : (pass == 3 ? 0.9f : 0.75f);   // the client's: dim inside, the lit window at night
                struct PV { float x, y, z, rhw, p[3], nn[3], uv[2], base[3]; };
                const PV v[4] = {
                    { -0.5f, -0.5f, 0, 1, { 4 + 0.6f, 1, 1.5f }, { n[0], n[1], n[2] }, { 0, 0 }, { level, level, level } },
                    { e, -0.5f, 0, 1, { 4 - 0.6f, -1, 1.5f }, { n[0], n[1], n[2] }, { 1, 0 }, { level, level, level } },
                    { -0.5f, e, 0, 1, { 4 + 0.6f, 1, -1.5f }, { n[0], n[1], n[2] }, { 0, 1 }, { level, level, level } },
                    { e, e, 0, 1, { 4 - 0.6f, -1, -1.5f }, { n[0], n[1], n[2] }, { 1, 1 }, { level, level, level } } };
                // The runtime's own constants (glass::BatchConstants) for the texture's row (<name>.mask: the mask plane,
                // kind, flags), at a clear noon (night for the last), the sun in front of the pane (behind it inside).
                gl::Material mat;
                mat.kind = gl::kClear;
                {
                    std::ifstream m(entry.path().parent_path() / (kind + ".mask"));
                    float plane[4];
                    uint32_t k = 0, fl = 0;
                    if (m >> plane[0] >> plane[1] >> plane[2] >> plane[3] >> k >> fl)
                    {
                        for (int i = 0; i < 4; ++i) mat.mask[i] = plane[i];
                        mat.kind = k; mat.flags = fl;
                    }
                }
                gl::Inputs in;
                float sun[3] = { pass == 2 ? 0.6f : -0.5f, 0.2f, 0.75f }; Norm(sun);
                for (int i = 0; i < 3; ++i) in.sun[i] = sun[i];
                in.daylight = pass == 3 ? 0.0f : 1.0f; in.night = pass == 3 ? 1.0f : 0.0f;
                in.sky[0] = 0.55f; in.sky[1] = 0.65f; in.sky[2] = 0.8f;
                const gl::Light light = gl::Evaluate(in);
                float c[4 * gl::kConstantCount] = {};
                gl::BatchConstants(mat, pass == 2 ? 1.0f : 0.0f, light, in.sun, in.sunColour, in.sky, gl::Settings{}, c);
                UploadLook(w.dev, light);
                const BOOL on = pass == 0 ? FALSE : TRUE, off = FALSE;
                w.dev->SetRenderTarget(0, target);
                D3DVIEWPORT9 vp{ 0, 0, kOut, kOut, 0.0f, 1.0f };
                w.dev->SetViewport(&vp);
                w.dev->SetRenderState(D3DRS_ZENABLE, FALSE); w.dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
                w.dev->SetVertexShader(nullptr); w.dev->SetPixelShader(ps);
                w.dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE3(0) | D3DFVF_TEXCOORDSIZE3(1) | D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE3(3));
                w.dev->SetTexture(0, tex);
                w.dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR); w.dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
                w.dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
                w.dev->SetPixelShaderConstantF(gl::kFirstConstant, c, gl::kConstantCount);
                w.dev->SetPixelShaderConstantB(gl::kSwitch, &on, 1);
                w.dev->SetPixelShaderConstantB(gl::kFloorSwitch, &off, 1);
                w.dev->BeginScene();
                w.dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(PV));
                w.dev->EndScene();
                D3DLOCKED_RECT rr{};
                if (FAILED(w.dev->GetRenderTargetData(target, readback)) || FAILED(readback->LockRect(&rr, nullptr, D3DLOCK_READONLY))) continue;
                for (int y = 0; y < kOut; ++y)
                    for (int x = 0; x < kOut; ++x)
                        for (int k = 0; k < 3; ++k)
                        {
                            const float val = reinterpret_cast<const float*>(static_cast<const uint8_t*>(rr.pBits) + y * rr.Pitch)[x * 4 + k];
                            image[(static_cast<size_t>(y) * kOut * 4 + pass * kOut + x) * 3 + k] = static_cast<uint8_t>(std::lround(std::clamp(val, 0.0f, 1.0f) * 255.0f));
                        }
                readback->UnlockRect();
            }
            tex->Release();
            std::ofstream out(entry.path().parent_path() / (kind + ".ppm"), std::ios::binary);
            out << "P6\n" << kOut * 4 << " " << kOut << "\n255\n";
            out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
            std::printf("preview: %s.ppm (stock, outside by day, inside by day, outside at night)\n", kind.c_str());
        }
        readback->Release();
        target->Release();
        ps->Release();
    }

    // --- room preview (ROOM_PREVIEW=<file.ppm>: a facade of glass 14 x 9.6 yd seen from 12 yd at an angle, through the
    // blocks with the runtime's constants, for a few styles and hours: a contact sheet to look at) --------------------
    void RoomPreview(const char* file)
    {
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("room preview: %s\n", why.c_str()); return; }
        std::vector<uint8_t> code;
        if (!wxl_livingazeroth::shaderpatch::Assemble(BlockShader(), "room preview", code, why)) { std::printf("room preview: %s\n", why.c_str()); return; }
        IDirect3DPixelShader9* ps = nullptr;
        w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps);
        constexpr int kW = 320, kH = 220, kCols = 4;
        IDirect3DSurface9* target = nullptr;
        IDirect3DSurface9* readback = nullptr;
        w.dev->CreateRenderTarget(kW, kH, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr);
        w.dev->CreateOffscreenPlainSurface(kW, kH, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &readback, nullptr);
        const float view[16] = { 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
        w.dev->SetPixelShaderConstantF(gl::kViewConstant, view, 4);
        struct Panel { const char* name; uint32_t cls; float tint[3], tint2[3], lamp[3], curtain[3]; float bright, weights[4], late, cap; float hour; bool lit; uint32_t seed; };
        const Panel panels[] = {
            { "house, human, 13:00", gl::kClassHouse, { 0.86f, 0.71f, 0.53f }, { 0.78f, 0.70f, 0.60f }, { 1, 0.64f, 0.35f }, { 0.55f, 0.2f, 0.16f }, 1, { 0.25f, 0.35f, 0.15f, 0.25f }, 0, 1, 13, true, 17 },
            { "house, human, 22:00", gl::kClassHouse, { 0.86f, 0.71f, 0.53f }, { 0.78f, 0.70f, 0.60f }, { 1, 0.64f, 0.35f }, { 0.55f, 0.2f, 0.16f }, 1, { 0.25f, 0.35f, 0.15f, 0.25f }, 0, 1, 22, true, 17 },
            { "house, human, 03:00", gl::kClassHouse, { 0.86f, 0.71f, 0.53f }, { 0.78f, 0.70f, 0.60f }, { 1, 0.64f, 0.35f }, { 0.55f, 0.2f, 0.16f }, 1, { 0.25f, 0.35f, 0.15f, 0.25f }, 0, 1, 3, true, 17 },
            { "inn, human, 01:00", gl::kClassInn, { 0.86f, 0.71f, 0.53f }, { 0.78f, 0.70f, 0.60f }, { 1, 0.64f, 0.35f }, { 0.55f, 0.2f, 0.16f }, 1.15f, { 0.2f, 0.3f, 0.25f, 0.25f }, 2.5f, 0.55f, 1, true, 23 },
            { "house, blood elf, 13:00", gl::kClassHouse, { 0.95f, 0.78f, 0.48f }, { 0.86f, 0.5f, 0.42f }, { 1, 0.78f, 0.38f }, { 0.7f, 0.16f, 0.14f }, 1.1f, { 0.25f, 0.63f, 0.15f, 0.2f }, 0, 1, 13, true, 31 },
            { "house, vrykul, 13:00", gl::kClassHouse, { 0.72f, 0.5f, 0.32f }, { 0.62f, 0.48f, 0.38f }, { 1, 0.5f, 0.2f }, { 0.48f, 0.38f, 0.28f }, 0.8f, { 0.125f, 0.56f, 0.15f, 0.25f }, 0, 1, 13, false, 41 },
            { "shop, goblin, 13:00", gl::kClassShop, { 0.92f, 0.72f, 0.44f }, { 0.82f, 0.76f, 0.56f }, { 1, 0.6f, 0.3f }, { 0.3f, 0.5f, 0.3f }, 1.21f, { 1.21f, 0.08f, 0.15f, 0.12f }, -1, 1, 13, true, 51 },
            { "tower, Dalaran, 23:00", gl::kClassTower, { 0.74f, 0.62f, 0.92f }, { 0.88f, 0.8f, 0.96f }, { 0.92f, 0.8f, 1 }, { 0.42f, 0.22f, 0.62f }, 1.1f, { 0.78f, 0.24f, 0.1f, 0.12f }, 2, 0.6f, 23, true, 61 },
        };
        constexpr int kPanels = static_cast<int>(sizeof(panels) / sizeof(panels[0]));
        const int rows = (kPanels + kCols - 1) / kCols;
        std::vector<uint8_t> image(static_cast<size_t>(kW) * kCols * kH * rows * 3, 0);
        for (int i = 0; i < kPanels; ++i)
        {
            const Panel& pn = panels[i];
            gl::RoomStyle st;
            st.cls = pn.cls;
            for (int k = 0; k < 3; ++k) { st.tint[k] = pn.tint[k]; st.tint2[k] = pn.tint2[k]; st.lamp[k] = pn.lamp[k]; st.curtain[k] = pn.curtain[k]; }
            st.brightness = pn.bright;
            for (int k = 0; k < 4; ++k) st.weights[k] = pn.weights[k];
            st.lateHours = pn.late; st.nightCap = pn.cap;
            gl::Building b;
            b.seed = pn.seed * 2654435761u;
            const bool night = pn.hour > 20.5f || pn.hour < 6.5f;
            gl::Inputs in;
            in.sun[0] = -0.4f; in.sun[1] = 0.5f; in.sun[2] = 0.75f; Norm(in.sun);
            in.daylight = night ? 0.0f : 1.0f; in.night = night ? 1.0f : 0.0f;
            in.sky[0] = 0.55f; in.sky[1] = 0.65f; in.sky[2] = 0.8f;
            const gl::Light light = gl::Evaluate(in);
            gl::Material m;
            m.kind = gl::kLeaded; m.mask[0] = m.mask[1] = m.mask[2] = 0; m.mask[3] = 1;
            float c[4 * gl::kConstantCount], sd[4 * gl::kSeedRegisters];
            gl::BatchConstants(m, 0.0f, light, in.sun, in.sunColour, in.sky, gl::Settings{}, c);
            gl::RoomInputs ri;
            ri.night = night ? 1.0f : 0.0f; ri.hour = pn.hour; ri.selfLit = pn.lit;
            gl::RoomBatchConstants(st, &b, light, ri, gl::Settings{}, c, sd);
            w.dev->SetPixelShaderConstantF(gl::kSeedConstant, sd, gl::kSeedRegisters);
            UploadLook(w.dev, light);
            // The facade: the plane through (12, 0, 0) turned 25 degrees, 14 yd along, z -3 .. 6.6; the client's colour
            // a dim glass by day, its lit window at night (where it lights it).
            const float yaw = 0.436f, ca = std::cos(yaw), sa = std::sin(yaw);
            const float base[3] = { night ? (pn.lit ? 0.62f : 0.04f) : 0.16f, night ? (pn.lit ? 0.47f : 0.045f) : 0.18f, night ? (pn.lit ? 0.28f : 0.06f) : 0.2f };
            const float n[3] = { -ca, sa, 0 };
            struct PV { float x, y, z, rhw, p[3], nn[3], uv[2], base[3]; };
            PV v[4];
            const float corner[4][2] = { { -7, 6.6f }, { 7, 6.6f }, { -7, -3 }, { 7, -3 } };
            for (int k = 0; k < 4; ++k)
            {
                const float along = corner[k][0], up = corner[k][1];
                const float P[3] = { 12 + sa * along, ca * along, up };
                const float f = 150.0f;
                v[k] = { kW * 0.5f - f * P[1] / P[0] - 0.5f, kH * 0.62f - f * P[2] / P[0] - 0.5f, 0.5f, 1.0f / P[0],
                         { P[0], P[1], P[2] }, { n[0], n[1], n[2] }, { 0.5f, 0.5f }, { base[0], base[1], base[2] } };
            }
            w.dev->SetRenderTarget(0, target);
            D3DVIEWPORT9 vp{ 0, 0, kW, kH, 0.0f, 1.0f };
            w.dev->SetViewport(&vp);
            w.dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(70, 66, 60), 1.0f, 0);
            w.dev->SetRenderState(D3DRS_ZENABLE, FALSE); w.dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            w.dev->SetVertexShader(nullptr); w.dev->SetPixelShader(ps);
            w.dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE3(0) | D3DFVF_TEXCOORDSIZE3(1) | D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE3(3));
            w.dev->SetTexture(0, w.texel);
            w.dev->SetPixelShaderConstantF(gl::kFirstConstant, c, gl::kConstantCount);
            const BOOL on = TRUE, off = FALSE;
            w.dev->SetPixelShaderConstantB(gl::kSwitch, &on, 1);
            w.dev->SetPixelShaderConstantB(gl::kFloorSwitch, &off, 1);
            w.dev->BeginScene();
            w.dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(PV));
            w.dev->EndScene();
            D3DLOCKED_RECT rr{};
            if (FAILED(w.dev->GetRenderTargetData(target, readback)) || FAILED(readback->LockRect(&rr, nullptr, D3DLOCK_READONLY))) continue;
            const int ox = (i % kCols) * kW, oy = (i / kCols) * kH;
            for (int y = 0; y < kH; ++y)
                for (int x = 0; x < kW; ++x)
                    for (int k = 0; k < 3; ++k)
                    {
                        const float val = reinterpret_cast<const float*>(static_cast<const uint8_t*>(rr.pBits) + y * rr.Pitch)[x * 4 + k];
                        image[(static_cast<size_t>(oy + y) * kW * kCols + ox + x) * 3 + k] =
                            static_cast<uint8_t>(std::lround(std::clamp(std::pow(std::max(val, 0.0f), 1.0f / 1.1f), 0.0f, 1.0f) * 255.0f));
                    }
            readback->UnlockRect();
            std::printf("room preview: panel %d %s\n", i + 1, pn.name);
        }
        std::ofstream out(file, std::ios::binary);
        out << "P6\n" << kW * kCols << " " << kH * rows << "\n255\n";
        out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
        w.dev->SetPixelShaderConstantF(gl::kSeedConstant, kZeroSeed, gl::kSeedRegisters);
        readback->Release();
        target->Release();
        ps->Release();
    }

    // --- 5. the shipped tables --------------------------------------------------------------------------------------
    struct Table { uint32_t fields = 0; std::vector<std::vector<uint32_t>> rows; std::vector<char> strings; };
    bool ReadTable(const std::filesystem::path& p, Table& t)
    {
        std::ifstream f(p, std::ios::binary);
        std::vector<char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (d.size() < 20 || std::memcmp(d.data(), "WDBC", 4) != 0) return false;
        uint32_t h[4];
        std::memcpy(h, d.data() + 4, 16);
        t.fields = h[1];
        if (h[2] != h[1] * 4 || d.size() < 20 + static_cast<size_t>(h[0]) * h[2] + h[3]) return false;
        for (uint32_t i = 0; i < h[0]; ++i)
        {
            std::vector<uint32_t> r(h[1]);
            std::memcpy(r.data(), d.data() + 20 + static_cast<size_t>(i) * h[2], h[2]);
            t.rows.push_back(std::move(r));
        }
        t.strings.assign(d.begin() + 20 + static_cast<size_t>(h[0]) * h[2], d.end());
        return true;
    }
    float F(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

    void TableTests(const std::filesystem::path& data)
    {
        Table m, p;
        if (!ReadTable(data / "GlassMaterials.cdbc", m) || !ReadTable(data / "GlassPanes.cdbc", p)) { Check(false, "the shipped glass tables read"); return; }
        std::map<uint32_t, int> kinds;
        bool sane = m.fields == 16;
        std::map<std::string, uint32_t> byName;
        for (const auto& r : m.rows)
        {
            kinds[r[2]]++;
            sane = sane && r[2] >= 1 && r[2] <= 4 && r[1] < m.strings.size();
            if (r[1] < m.strings.size())
            {
                std::string t = &m.strings[r[1]];
                byName[t.substr(t.find_last_of('\\') + 1)] = r[2];
            }
            for (int k = 7; k <= 9; ++k) sane = sane && F(r[k]) >= 0 && F(r[k]) <= 1.0001f;
        }
        char line[256];
        std::snprintf(line, sizeof(line), "GlassMaterials.cdbc: %zu glass textures (clear %d, stained %d, frosted %d, leaded %d), all sane",
                      m.rows.size(), kinds[1], kinds[2], kinds[3], kinds[4]);
        Check(sane && m.rows.size() > 80, line);
        Check(byName["mm_strmwnd_cwindows_02.blp"] == gl::kStained && byName["mm_stained_01.blp"] == gl::kStained &&
              byName["mm_redridge_stained_01.blp"] == gl::kStained && byName["mm_windows_11.blp"] == gl::kStained &&
              byName["mm_windows_16.blp"] == gl::kStained && byName["eb_dal_allianceglass.blp"] == gl::kStained &&
              byName["vrykul_window04.blp"] == gl::kFrosted && byName["mm_elwynn_wnd_ext__01.blp"] == gl::kLeaded &&
              byName.count("mm_strmwnd_wall_01.blp") == 0 && byName.count("jlo_bloodelf_windows_abndn.blp") == 0 &&
              byName.count("stormwindlampglass.blp") == 0,
              "the stained glass the user named is stained; the vrykul's icy panes frosted, Elwynn's leaded; no Stormwind walls, abandoned glass or lamps");
        std::map<uint32_t, int> maps;
        bool unit = p.fields == 26;
        for (const auto& r : p.rows)
        {
            maps[r[1]]++;
            const float o = std::sqrt(F(r[6]) * F(r[6]) + F(r[7]) * F(r[7]) + F(r[8]) * F(r[8]));
            const float a = std::sqrt(F(r[9]) * F(r[9]) + F(r[10]) * F(r[10]) + F(r[11]) * F(r[11]));
            unit = unit && std::fabs(o - 1) < 1e-3f && std::fabs(a - 1) < 1e-3f && F(r[12]) > 0 && F(r[13]) > 0 &&
                   F(r[18]) <= F(r[21]) && F(r[19]) <= F(r[22]) && F(r[20]) <= F(r[23]);
        }
        std::snprintf(line, sizeof(line), "GlassPanes.cdbc: %zu panes (Eastern Kingdoms %d, Kalimdor %d, Outland %d, Northrend %d, other maps %zu), unit facings, boxes sane",
                      p.rows.size(), maps[0], maps[1], maps[530], maps[571], p.rows.size() - maps[0] - maps[1] - maps[530] - maps[571]);
        Check(unit && maps[0] > 1000 && maps[1] > 500 && maps[530] > 500 && maps[571] > 1000, line);

        // The rooms' tables: every style sane, every building's style there, the places the user named in them.
        Table st, bu;
        if (!ReadTable(data / "RoomStyles.cdbc", st) || !ReadTable(data / "GlassBuildings.cdbc", bu)) { Check(false, "the shipped room tables read"); return; }
        std::map<uint32_t, uint32_t> styleClass;
        bool styles = st.fields == 25;
        for (const auto& r : st.rows)
        {
            styleClass[r[0]] = r[1];
            styles = styles && r[1] < gl::kClassCount;
            for (int k = 3; k <= 14; ++k) styles = styles && F(r[k]) >= 0 && F(r[k]) <= 1.0001f;
            float sum = 0;
            for (int k = 18; k <= 21; ++k) { styles = styles && F(r[k]) >= 0; sum += F(r[k]); }
            styles = styles && sum > 0.5f && F(r[15]) > 0.3f && F(r[15]) < 1.5f && F(r[17]) >= 0 && F(r[17]) < 0.5f && F(r[23]) > 0 && F(r[23]) <= 1;
        }
        std::map<uint32_t, int> byClass;
        int placements = 0, groups = 0, defaults = 0, missing = 0;
        std::map<std::string, int> named;
        const char* places[] = { "goldshireinn", "redridge_humantwostory", "stormwind group", "nd_dalaran group", "silvermoon_city_01 group",
                                 "nd_vrykul_dwelling", "bootybay" };
        for (const auto& r : bu.rows)
        {
            if (!styleClass.count(r[7])) ++missing;
            if (r[2] == 0) { ++defaults; continue; }
            if (r[6] != gl::kWholePlacement) ++groups;
            else { ++placements; byClass[styleClass[r[7]]]++; }
            const std::string comment = r[12] < bu.strings.size() ? std::string(&bu.strings[r[12]]) : std::string();
            for (const char* place : places) if (comment.find(place) != std::string::npos) named[place]++;
        }
        std::snprintf(line, sizeof(line), "RoomStyles.cdbc: %zu styles, sane; GlassBuildings.cdbc: %d placements (houses %d, inns %d, shops %d, keeps %d), %d city groups, "
                      "%d WMO defaults, %d without a style; Goldshire's inn %d, Lakeshire's houses %d, Stormwind's groups %d, Dalaran's %d, Silvermoon's %d, "
                      "vrykul dwellings %d, Booty Bay %d", st.rows.size(), placements, byClass[gl::kClassHouse], byClass[gl::kClassInn], byClass[gl::kClassShop],
                      byClass[gl::kClassKeep], groups, defaults, missing, named["goldshireinn"], named["redridge_humantwostory"], named["stormwind group"],
                      named["nd_dalaran group"], named["silvermoon_city_01 group"], named["nd_vrykul_dwelling"], named["bootybay"]);
        bool all = true;
        for (const char* place : places) all = all && named[place] > 0;
        Check(styles && bu.fields == 13 && st.rows.size() > 20 && placements > 800 && groups > 100 && defaults > 100 && missing == 0 && all, line);
    }

    // --- 6. night windows: no sparkle (on WARP) -------------------------------------------------------------------
    // "why do the nighttime windows look fucked up and sparkly" (the user, Stormwind at 22:38, 2026-10-17). A leaded
    // window (the shipped row of Stormwind's mm_strmwnd_windows_01: its mask; a synthetic texture of its kind, mottled
    // amber glass in a diamond lead lattice: its mask in the glass as often below 1 as the real one's) on one of
    // Stormwind's buildings (its city group's style and seed) at 22:40, the client's lit window as the stock colour,
    // its rooms all lit or all dark, seen from 5 to 50 yd, head on to grazing, level and from below, through the blocks
    // with the runtime's own constants. Per view: no NaN or Inf; inside the glass (away from the lead, where a pixel
    // is at most ~1.5 texels) the pixels' contrast with their neighbours over the stock colour's (speckle); and the
    // change between two cameras a third of a pixel apart over the stock's (shimmer). GLASS_NIGHT=<folder> adds the
    // real texture (<name>.rgba, 256 x 256, and <name>.mask, as GLASS_PREVIEW's) to the report;
    // GLASS_NIGHT_SHEET=<file.ppm> writes the views (stock above, glass below) to look at.
    struct NightTexture { std::string name; std::vector<uint8_t> rgba; float mask[4] = { 0, 0, 0, 1 }; float uv[4] = { 0, 0, 1, 1 }; };

    uint32_t NoiseHash(int ix, int iy)
    {
        uint32_t x = static_cast<uint32_t>(ix) * 374761393u + static_cast<uint32_t>(iy) * 668265263u;
        x = (x ^ (x >> 13)) * 1274126177u;
        return x ^ (x >> 16);
    }
    float ValueNoise(float u, float v, float cell)
    {
        const float x = u / cell, y = v / cell;
        const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
        const float fx = x - ix, fy = y - iy, sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
        auto h = [](int a, int b) { return (NoiseHash(a, b) & 0xFFFFFFu) / 16777216.0f; };
        const float a = h(ix, iy), b = h(ix + 1, iy), c = h(ix, iy + 1), d = h(ix + 1, iy + 1);
        const float top = a + (b - a) * sx, bottom = c + (d - c) * sx;
        return top + (bottom - top) * sy;
    }

    NightTexture SyntheticLeaded()
    {
        NightTexture t;
        t.name = "synthetic leaded";
        t.rgba.resize(256 * 256 * 4);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x)
            {
                const float u = x + 0.5f, v = y + 0.5f, p = 32.0f;
                const float d1 = std::fabs(std::fmod(u + v, p) - p / 2), d2 = std::fabs(std::fmod(u - v + 256.0f, p) - p / 2);
                const float lead = std::min(p / 2 - d1, p / 2 - d2) / 1.41421356f;          // texels to the nearest lead line
                const float frame = std::min(std::min(u, v), std::min(256 - u, 256 - v)) - 6.0f;
                const float w = std::clamp(std::min(lead - 1.7f, frame) + 0.5f, 0.0f, 1.0f); // 1 glass .. 0 lead
                const float n = 0.5f * ValueNoise(u, v, 7.0f) + 0.3f * ValueNoise(u + 97, v + 31, 3.0f) + 0.2f * ValueNoise(u + 13, v + 71, 1.6f);
                const float k = 0.3f + 0.95f * n, l = 0.85f + 0.3f * ValueNoise(u, v, 4.0f);
                const float glass[3] = { 0.88f * k, 0.71f * k, 0.34f * k }, came[3] = { 0.16f * l, 0.14f * l, 0.11f * l };
                for (int c = 0; c < 3; ++c)
                    t.rgba[(static_cast<size_t>(y) * 256 + x) * 4 + c] =
                        static_cast<uint8_t>(std::lround(std::clamp(glass[c] * w + came[c] * (1 - w), 0.0f, 1.0f) * 255.0f));
                t.rgba[(static_cast<size_t>(y) * 256 + x) * 4 + 3] = 255;
            }
        return t;
    }

    // The texture with its mips (box filtered, as the client's BLPs have them).
    IDirect3DTexture9* NightTextureOnWarp(IDirect3DDevice9* dev, const NightTexture& t)
    {
        IDirect3DTexture9* tex = nullptr;
        if (FAILED(dev->CreateTexture(256, 256, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr))) return nullptr;
        std::vector<float> level(256 * 256 * 4);
        for (size_t i = 0; i < level.size(); ++i) level[i] = t.rgba[i] / 255.0f;
        int side = 256;
        for (DWORD m = 0; m < tex->GetLevelCount(); ++m)
        {
            if (m)
            {
                const int half = std::max(1, side / 2);
                std::vector<float> next(static_cast<size_t>(half) * half * 4);
                for (int y = 0; y < half; ++y)
                    for (int x = 0; x < half; ++x)
                        for (int c = 0; c < 4; ++c)
                        {
                            float s = 0;
                            for (int dy = 0; dy < 2; ++dy)
                                for (int dx = 0; dx < 2; ++dx)
                                    s += level[(static_cast<size_t>(std::min(side - 1, y * 2 + dy)) * side + std::min(side - 1, x * 2 + dx)) * 4 + c];
                            next[(static_cast<size_t>(y) * half + x) * 4 + c] = s / 4;
                        }
                level.swap(next);
                side = half;
            }
            D3DLOCKED_RECT lr;
            if (FAILED(tex->LockRect(m, &lr, nullptr, 0))) continue;
            for (int y = 0; y < side; ++y)
                for (int x = 0; x < side; ++x)
                {
                    const float* p = &level[(static_cast<size_t>(y) * side + x) * 4];
                    auto b = [](float f) { return static_cast<uint32_t>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
                    reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch)[x] = (b(p[3]) << 24) | (b(p[0]) << 16) | (b(p[1]) << 8) | b(p[2]);
                }
            tex->UnlockRect(m);
        }
        return tex;
    }

    // A style and a building from the shipped tables: Stormwind's first city group.
    bool StormwindBuilding(const std::filesystem::path& data, gl::RoomStyle& style, gl::Building& b, float mask[4])
    {
        Table st, bu, m;
        if (!ReadTable(data / "RoomStyles.cdbc", st) || !ReadTable(data / "GlassBuildings.cdbc", bu) || !ReadTable(data / "GlassMaterials.cdbc", m)) return false;
        bool haveMask = false, haveBuilding = false;
        for (const auto& r : m.rows)
            if (r[1] < m.strings.size() && std::string(&m.strings[r[1]]).find("mm_strmwnd_windows_01.blp") != std::string::npos)
            {
                for (int k = 0; k < 4; ++k) mask[k] = F(r[10 + k]);
                haveMask = true;
            }
        for (const auto& r : bu.rows)
        {
            const std::string comment = r[12] < bu.strings.size() ? std::string(&bu.strings[r[12]]) : std::string();
            if (r[1] != 0 || r[6] == gl::kWholePlacement || comment.find("stormwind group") == std::string::npos) continue;
            for (const auto& s : st.rows)
            {
                if (s[0] != r[7]) continue;
                style.id = s[0]; style.cls = s[1]; style.palette = s[2];
                for (int k = 0; k < 3; ++k)
                {
                    style.tint[k] = F(s[3 + k]); style.tint2[k] = F(s[6 + k]); style.lamp[k] = F(s[9 + k]); style.curtain[k] = F(s[12 + k]);
                }
                style.brightness = F(s[15]); style.spread = F(s[16]); style.darkShare = F(s[17]);
                for (int k = 0; k < 4; ++k) style.weights[k] = F(s[18 + k]);
                style.lateHours = F(s[22]); style.nightCap = F(s[23]);
                b.map = 0; b.uniqueId = r[2]; b.group = r[6]; b.style = r[7]; b.seed = r[8]; b.flags = r[9];
                for (int k = 0; k < 3; ++k) b.pos[k] = F(r[3 + k]);
                haveBuilding = true;
                break;
            }
            if (haveBuilding) break;
        }
        return haveMask && haveBuilding;
    }

    struct NightResult { int views = 0, nonfinite = 0; float speckle = 0, shimmer = 0, worstSpeckle = 0, worstShimmer = 0; double light = 0; int lightViews = 0; std::string worstAt; };

    void NightTests(const std::filesystem::path& data)
    {
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("skip  night windows: %s\n", why.c_str()); return; }
        gl::RoomStyle style;
        gl::Building building;
        float mask[4];
        if (!StormwindBuilding(data, style, building, mask)) { Check(false, "night windows: Stormwind's window texture row and a city group in the shipped tables"); return; }
        const std::string text = "ps_3_0\n    def c175, 1, 0, 0.5, 2\n    def c176, 0.2126, 0.7152, 0.0722, 0.2\n" + gl::ShaderDefs() +
                                 "    dcl_texcoord0 v0.xyz\n    dcl_texcoord1 v1.xyz\n    dcl_texcoord2 v2.xy\n    dcl_texcoord3 v3.xyz\n    dcl_2d s0\n"
                                 "    texld r0, v2, s0\n    mul r12.xyz, r0, v3\n    mov r31.xyz, c175.y" + gl::GlassBlock("v0", "v1", "v2") +
                                 "\n    add oC0.xyz, r12, r31\n    mov oC0.w, c175.x\n";
        const std::string uvText = "ps_3_0\n    def c0, 0, 0, 0, 1\n    dcl_texcoord2 v2.xy\n    mov oC0.xy, v2\n    mov oC0.zw, c0\n";
        std::vector<uint8_t> code, uvCode;
        if (!wxl_livingazeroth::shaderpatch::Assemble(text, "night", code, why) || !wxl_livingazeroth::shaderpatch::Assemble(uvText, "night uv", uvCode, why))
        { Check(false, "night windows: the shaders assemble: " + why.substr(0, 200)); return; }
        IDirect3DPixelShader9* ps = nullptr;
        IDirect3DPixelShader9* uvps = nullptr;
        w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps);
        w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(uvCode.data()), &uvps);
        constexpr int kS = 320;
        constexpr float kFocal = 960.0f;   // px per unit of tan: a 90-degree view 1920 px wide
        IDirect3DSurface9* target = nullptr;
        IDirect3DSurface9* readback = nullptr;
        w.dev->CreateRenderTarget(kS, kS, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr);
        w.dev->CreateOffscreenPlainSurface(kS, kS, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &readback, nullptr);
        if (!ps || !uvps || !target || !readback) { Check(false, "night windows: WARP resources"); return; }
        const float view[16] = { 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };

        std::vector<NightTexture> textures;
        textures.push_back(SyntheticLeaded());
        for (int k = 0; k < 4; ++k) textures[0].mask[k] = mask[k];
        textures[0].uv[0] = textures[0].uv[1] = 0.25f; textures[0].uv[2] = textures[0].uv[3] = 0.75f;   // 128 texels across, as the real window's
        if (const char* folder = std::getenv("GLASS_NIGHT"))
        {
            std::ifstream f(std::filesystem::path(folder) / "mm_strmwnd_windows_01.rgba", std::ios::binary);
            std::ifstream mf(std::filesystem::path(folder) / "mm_strmwnd_windows_01.mask");
            std::vector<uint8_t> px((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            NightTexture real;
            real.name = "mm_strmwnd_windows_01";
            real.uv[0] = 0.0f; real.uv[1] = 0.5f; real.uv[2] = 0.5f; real.uv[3] = 1.0f;   // the diamond lattice and its side lights
            if (px.size() == 256 * 256 * 4 && (mf >> real.mask[0] >> real.mask[1] >> real.mask[2] >> real.mask[3]))
            {
                real.rgba = std::move(px);
                textures.push_back(std::move(real));
            }
        }
        const char* sheetFile = std::getenv("GLASS_NIGHT_SHEET");
        std::vector<std::vector<uint8_t>> sheetTiles;

        // 22:40 in Stormwind: the client's lit windows, the sun below the horizon; and a clear afternoon.
        gl::Inputs nightIn;
        nightIn.sun[0] = 0.3f; nightIn.sun[1] = -0.2f; nightIn.sun[2] = -0.93f; Norm(nightIn.sun);
        nightIn.daylight = 0; nightIn.night = 1;
        nightIn.sky[0] = 0.05f; nightIn.sky[1] = 0.07f; nightIn.sky[2] = 0.14f;
        gl::Inputs dayIn;
        dayIn.sun[0] = -0.4f; dayIn.sun[1] = 0.3f; dayIn.sun[2] = 0.86f; Norm(dayIn.sun);
        dayIn.daylight = 1; dayIn.night = 0;
        dayIn.sky[0] = 0.55f; dayIn.sky[1] = 0.65f; dayIn.sky[2] = 0.8f;
        // 19:00: the sun's day over, the client's windows not lit yet, an orange sky.
        gl::Inputs duskIn = dayIn;
        duskIn.sun[2] = -0.05f; Norm(duskIn.sun);
        duskIn.daylight = 0;
        duskIn.sky[0] = 0.66f; duskIn.sky[1] = 0.5f; duskIn.sky[2] = 0.42f;
        // The cases: the stock lighting (texel x this: the client's lit window at night, a sunlit wall by day, a dim
        // room inside), rooms all lit (0), all dark (1) or the hour's (-1), a forced layout (-1: the style's); the
        // bounds: speckle and shimmer over the views (mean), speckle at the worst view. Before the fix the dark rooms'
        // were 3.7 and 2.2, worst 4.1 (synthetic; the real texture's 2.4 and 1.3, worst 3.0): the client's glow kept
        // where the mask is below 1, the lead's edges and the glass's darker texels, bright on the darkened glass. The
        // lit rooms' worst is the room itself 5 yd away (its shelves); seen from inside by day the glass is twice as
        // bright as the stock against the same dark lead, so its edges change more.
        // By day seen from outside the glass keeps most of the client's painted window: at least `light` of the stock's
        // light in the glass (leaded 70%, clear 60%, stained 75%: "is all of the glass supposed to look dark in the
        // daytime lol", the user, 2026-10-18; before c83b430, 48% for leaded), the same views; and it no longer reads as
        // lamplit ("they read as lamplit at noon", the user, Theramore at 16:01, 2026-10-18): the glass's mean colour at
        // least `cooler` less warm than the stock's (warmth (R - B) / (R + G + B)); stained glass keeps its colours (the
        // day's cooling moves its warmth by at most `keep`). At dusk (19:00) the warmth is back (within `keep` of the
        // stock's) and the room behind is seen by the day's light: no darker than `light` (before, its rooms were black from
        // 18:00 until the client lit its windows at 20:30).
        struct NightCase { const char* name; bool night; int dark; float inside; int layout; float lit[3]; float speckle, shimmer, worst; uint32_t kind = gl::kLeaded;
                           float light = 0; float cooler = 0; float keep = -1; bool dusk = false; };
        const NightCase cases[] = {
            { "night, rooms lit", true, 0, 0, -1, { 1.05f, 1.0f, 0.75f }, 1.4f, 1.2f, 2.5f },
            { "night, rooms dark", true, 1, 0, -1, { 1.05f, 1.0f, 0.75f }, 1.3f, 1.2f, 1.5f },
            { "night, rooms lit, all shelves", true, 0, 0, gl::kLayoutShelves, { 1.05f, 1.0f, 0.75f }, 1.5f, 1.2f, 3.2f },
            { "day, outside", false, -1, 0, -1, { 0.95f, 0.92f, 0.86f }, 1.2f, 1.2f, 1.5f, gl::kLeaded, 0.70f, 0.08f },
            { "day, from inside", false, -1, 1, -1, { 0.5f, 0.48f, 0.45f }, 1.8f, 2.0f, 2.2f },
            { "day, outside, as clear glass", false, -1, 0, -1, { 0.95f, 0.92f, 0.86f }, 1.2f, 1.2f, 1.5f, gl::kClear, 0.6f, 0.08f },
            { "day, outside, as stained glass", false, -1, 0, -1, { 0.95f, 0.92f, 0.86f }, 1.2f, 1.2f, 1.5f, gl::kStained, 0.75f, 0.0f, 0.04f },
            { "dusk (19:00), outside", false, -1, 0, -1, { 0.62f, 0.5f, 0.45f }, 1.2f, 1.2f, 1.5f, gl::kLeaded, 0.75f, 0.0f, 0.08f, true },
        };
        const float cam0[3] = { -8838.6f, 627.4f, 99.3f };
        const float heading = 0.37f;
        const float dists[] = { 5, 10, 20, 35, 50 }, yaws[] = { 0, 35, 60, 75, 84 }, heights[] = { 0, 3 };
        const float hw = 0.7f, hh = 1.0f;   // the pane: 1.4 x 2 yd

        for (const NightTexture& tx : textures)
        {
            IDirect3DTexture9* tex = NightTextureOnWarp(w.dev, tx);
            if (!tex) { Check(false, "night windows: the texture on WARP"); continue; }
            // The glass interior (no lead within 2 texels; lead: luminance below 0.25) at full size.
            std::vector<uint8_t> leadMap(256 * 256), interior(256 * 256);
            for (size_t i = 0; i < leadMap.size(); ++i)
                leadMap[i] = 0.2126f * tx.rgba[i * 4] / 255.0f + 0.7152f * tx.rgba[i * 4 + 1] / 255.0f + 0.0722f * tx.rgba[i * 4 + 2] / 255.0f < 0.25f;
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256; ++x)
                {
                    bool ok = true;
                    for (int dy = -2; dy <= 2 && ok; ++dy)
                        for (int dx = -2; dx <= 2 && ok; ++dx)
                            ok = !leadMap[static_cast<size_t>(std::clamp(y + dy, 0, 255)) * 256 + std::clamp(x + dx, 0, 255)];
                    interior[static_cast<size_t>(y) * 256 + x] = ok;
                }
            gl::Material mat;
            mat.kind = gl::kLeaded;
            for (int k = 0; k < 4; ++k) mat.mask[k] = tx.mask[k];
            for (const NightCase& cs : cases)
            {
                NightResult res;
                double speckleSum = 0, shimmerSum = 0;
                int speckleViews = 0, shimmerViews = 0;
                float c[4 * gl::kConstantCount], seed[4 * gl::kSeedRegisters], day[4 * gl::kDayRegisters];
                gl::Settings s;
                const gl::Inputs& in = cs.night ? nightIn : (cs.dusk ? duskIn : dayIn);
                const gl::Light light = gl::Evaluate(in);
                const float* lit = cs.lit;
                gl::Material cm = mat;
                cm.kind = cs.kind;
                gl::BatchConstants(cm, cs.inside, light, in.sun, in.sunColour, in.sky, s, c);
                gl::RoomInputs ri;
                ri.night = cs.night ? 1.0f : 0.0f; ri.hour = cs.night ? 22.67f : (cs.dusk ? 19.0f : 14.0f); ri.selfLit = true;
                gl::RoomStyle st = style;
                if (cs.layout >= 0) for (int k = 0; k < 4; ++k) st.weights[k] = k == cs.layout ? 1.0f : 0.0f;
                gl::RoomBatchConstants(st, &building, light, ri, s, c, seed);
                if (cs.dark >= 0) c[47] = c[35] = cs.dark ? 1.0f : 0.0f;   // c203.w, c200.w: every room dark, or every room lit
                UploadLook(w.dev, light);
                gl::DayConstants(cm, light, s, building.flags, day);
                w.dev->SetPixelShaderConstantF(gl::kDayConstant, day, gl::kDayRegisters);
                double hue[3][3] = {};   // the glass's mean colour inside the glass: stock, glass, glass without the day's cooling
                for (float dist : dists)
                    for (float yawDeg : yaws)
                        for (float height : heights)
                        {
                            const float yaw = yawDeg * 3.14159265f / 180.0f;
                            const float C[3] = { cam0[0] + dist * std::cos(heading), cam0[1] + dist * std::sin(heading), cam0[2] + height };
                            const float back[3] = { -std::cos(heading), -std::sin(heading), 0 };
                            const float N[3] = { back[0] * std::cos(yaw) - back[1] * std::sin(yaw), back[0] * std::sin(yaw) + back[1] * std::cos(yaw), 0 };
                            const float A[3] = { -N[1], N[0], 0 };
                            // renders [stock, glass] x [camera, camera a third of a pixel to the right], then the uv
                            std::vector<float> img[6];
                            bool ok = true;
                            const int passes = cs.keep >= 0 && !cs.dusk ? 6 : 5;   // 5: the glass without the day's cooling
                            for (int pass = 0; pass < passes && ok; ++pass)
                            {
                                const bool glass = pass == 1 || pass == 3 || pass == 5, shifted = pass == 2 || pass == 3;
                                if (pass == 5)
                                {
                                    float warm[4 * gl::kDayRegisters];
                                    std::memcpy(warm, day, sizeof(warm));
                                    warm[0] = 0;
                                    w.dev->SetPixelShaderConstantF(gl::kDayConstant, warm, gl::kDayRegisters);
                                }
                                float F3[3] = { C[0] - cam0[0], C[1] - cam0[1], C[2] - cam0[2] }; Norm(F3);
                                float R[3] = { F3[1], -F3[0], 0 }; Norm(R);
                                const float U[3] = { R[1] * F3[2] - R[2] * F3[1], R[2] * F3[0] - R[0] * F3[2], R[0] * F3[1] - R[1] * F3[0] };
                                const float step = shifted ? dist / kFocal / 3.0f : 0.0f;
                                const float cam[3] = { cam0[0] + R[0] * step, cam0[1] + R[1] * step, cam0[2] + R[2] * step };
                                struct PV { float x, y, z, rhw, p[3], n[3], uv[2], base[3]; };
                                PV v[4];
                                const float corner[4][2] = { { -hw, hh }, { hw, hh }, { -hw, -hh }, { hw, -hh } };
                                for (int k = 0; k < 4; ++k)
                                {
                                    float P[3], q[3];
                                    for (int i = 0; i < 3; ++i) { P[i] = C[i] + A[i] * corner[k][0] + (i == 2 ? corner[k][1] : 0.0f); q[i] = P[i] - cam[i]; }
                                    const float zf = q[0] * F3[0] + q[1] * F3[1] + q[2] * F3[2];
                                    const float sx = kS * 0.5f + kFocal * (q[0] * R[0] + q[1] * R[1] + q[2] * R[2]) / zf;
                                    const float sy = kS * 0.5f - kFocal * (q[0] * U[0] + q[1] * U[1] + q[2] * U[2]) / zf;
                                    const float uu = tx.uv[0] + (tx.uv[2] - tx.uv[0]) * (k & 1 ? 1.0f : 0.0f), vv = tx.uv[1] + (tx.uv[3] - tx.uv[1]) * (k & 2 ? 1.0f : 0.0f);
                                    v[k] = { sx - 0.5f, sy - 0.5f, 0.5f, 1.0f / zf, { q[0], q[1], q[2] }, { N[0], N[1], N[2] }, { uu, vv }, { lit[0], lit[1], lit[2] } };
                                }
                                float cv[16];
                                std::memcpy(cv, view, sizeof(cv));
                                for (int i = 0; i < 3; ++i) cv[i] = cam[i];
                                w.dev->SetRenderTarget(0, target);
                                D3DVIEWPORT9 vp{ 0, 0, kS, kS, 0.0f, 1.0f };
                                w.dev->SetViewport(&vp);
                                w.dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
                                w.dev->SetRenderState(D3DRS_ZENABLE, FALSE); w.dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
                                w.dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
                                w.dev->SetVertexShader(nullptr); w.dev->SetPixelShader(pass == 4 ? uvps : ps);
                                w.dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE3(0) | D3DFVF_TEXCOORDSIZE3(1) | D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE3(3));
                                w.dev->SetTexture(0, tex);
                                w.dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR); w.dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
                                w.dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
                                w.dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP); w.dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
                                w.dev->SetPixelShaderConstantF(gl::kViewConstant, cv, 4);
                                w.dev->SetPixelShaderConstantF(gl::kFirstConstant, c, gl::kConstantCount);
                                w.dev->SetPixelShaderConstantF(gl::kSeedConstant, seed, gl::kSeedRegisters);
                                const BOOL on = glass ? TRUE : FALSE, off = FALSE;
                                w.dev->SetPixelShaderConstantB(gl::kSwitch, &on, 1);
                                w.dev->SetPixelShaderConstantB(gl::kFloorSwitch, &off, 1);
                                ok = SUCCEEDED(w.dev->BeginScene());
                                if (!ok) break;
                                w.dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(PV));
                                w.dev->EndScene();
                                D3DLOCKED_RECT rr{};
                                ok = SUCCEEDED(w.dev->GetRenderTargetData(target, readback)) && SUCCEEDED(readback->LockRect(&rr, nullptr, D3DLOCK_READONLY));
                                if (!ok) break;
                                img[pass].resize(static_cast<size_t>(kS) * kS * 4);
                                for (int y = 0; y < kS; ++y)
                                    std::memcpy(&img[pass][static_cast<size_t>(y) * kS * 4], static_cast<const uint8_t*>(rr.pBits) + y * rr.Pitch, kS * 16);
                                readback->UnlockRect();
                                if (pass == 5) w.dev->SetPixelShaderConstantF(gl::kDayConstant, day, gl::kDayRegisters);
                            }
                            if (!ok) { Check(false, "night windows: drawing on WARP"); break; }
                            auto L = [&](int i, size_t p) { const float* q = &img[i][p * 4]; return 0.2126f * q[0] + 0.7152f * q[1] + 0.0722f * q[2]; };
                            auto drawn = [&](int i, size_t p) { return img[i][p * 4 + 3] > 0.5f; };
                            // NaN / Inf
                            for (int i = 1; i < 4; i += 2)
                                for (size_t p = 0; p < static_cast<size_t>(kS) * kS; ++p)
                                    if (drawn(i, p) || !std::isfinite(img[i][p * 4 + 3]))
                                        for (int k = 0; k < 3; ++k) res.nonfinite += std::isfinite(img[i][p * 4 + k]) ? 0 : 1;
                            // Speckle: inside the glass, |l - its 3 x 3 mean| over l, glass over stock, where a pixel is <= 1.5 texels.
                            double num[2] = {}, den[2] = {};
                            int inside = 0;
                            for (int y = 1; y < kS - 1; ++y)
                                for (int x = 1; x < kS - 1; ++x)
                                {
                                    const size_t p = static_cast<size_t>(y) * kS + x;
                                    bool all = true;
                                    for (int dy = -1; dy <= 1 && all; ++dy)
                                        for (int dx = -1; dx <= 1 && all; ++dx) all = drawn(4, p + dy * kS + dx);
                                    if (!all) continue;
                                    const float* uv = &img[4][p * 4];
                                    const float du = std::max(std::fabs(img[4][(p + 1) * 4] - uv[0]), std::fabs(img[4][(p + kS) * 4] - uv[0])) * 256;
                                    const float dv = std::max(std::fabs(img[4][(p + 1) * 4 + 1] - uv[1]), std::fabs(img[4][(p + kS) * 4 + 1] - uv[1])) * 256;
                                    if (std::max(du, dv) > 2.5f) continue;
                                    const int tu = std::clamp(static_cast<int>(uv[0] * 256), 0, 255), tv = std::clamp(static_cast<int>(uv[1] * 256), 0, 255);
                                    if (!interior[static_cast<size_t>(tv) * 256 + tu]) continue;
                                    ++inside;
                                    for (int g = 0; g < 3; ++g)
                                        for (int k = 0; k < 3; ++k)
                                            if (!img[g == 2 ? 5 : g].empty() && std::isfinite(img[g == 2 ? 5 : g][p * 4 + k])) hue[g][k] += img[g == 2 ? 5 : g][p * 4 + k];
                                    for (int g = 0; g < 2; ++g)
                                    {
                                        const int i = g ? 1 : 0;
                                        float mean = 0;
                                        for (int dy = -1; dy <= 1; ++dy)
                                            for (int dx = -1; dx <= 1; ++dx) mean += L(i, p + dy * kS + dx) / 9;
                                        const float l = L(i, p);
                                        if (!std::isfinite(l) || !std::isfinite(mean)) continue;
                                        num[g] += std::fabs(l - mean); den[g] += l;
                                    }
                                }
                            char at[96];
                            std::snprintf(at, sizeof(at), "%.0f yd, %.0f deg%s", dist, yawDeg, height > 0 ? ", from below" : "");
                            if (inside > 200 && den[0] > 0 && den[1] > 0 && num[0] > 0)
                            {
                                const float r = static_cast<float>((num[1] / den[1]) / (num[0] / den[0]));
                                speckleSum += r; ++speckleViews;
                                if (r > res.worstSpeckle) { res.worstSpeckle = r; res.worstAt = at; }
                                res.light += den[1] / den[0]; ++res.lightViews;
                            }
                            // Shimmer: the change between the two cameras, glass over stock (every pixel drawn in all four).
                            double ch[2] = {}, lv[2] = {};
                            int both = 0;
                            for (size_t p = 0; p < static_cast<size_t>(kS) * kS; ++p)
                            {
                                if (!drawn(0, p) || !drawn(1, p) || !drawn(2, p) || !drawn(3, p)) continue;
                                const float a = L(0, p), b = L(2, p), g1 = L(1, p), g2 = L(3, p);
                                if (!std::isfinite(g1) || !std::isfinite(g2)) continue;
                                ch[0] += std::fabs(a - b); lv[0] += a; ch[1] += std::fabs(g1 - g2); lv[1] += g1;
                                ++both;
                            }
                            if (both > 400 && ch[0] > 0 && lv[0] > 0 && lv[1] > 0)
                            {
                                const float r = static_cast<float>((ch[1] / lv[1]) / (ch[0] / lv[0]));
                                shimmerSum += r; ++shimmerViews;
                                res.worstShimmer = std::max(res.worstShimmer, r);
                            }
                            ++res.views;
                            if (std::getenv("GLASS_NIGHT_VERBOSE"))
                                std::printf("      %s, %s: %-28s speckle %.2f (%d px), shimmer %.2f\n", tx.name.c_str(), cs.name, at,
                                            inside > 200 && num[0] > 0 ? (num[1] / den[1]) / (num[0] / den[0]) : 0.0, inside,
                                            both > 400 && ch[0] > 0 ? (ch[1] / lv[1]) / (ch[0] / lv[0]) : 0.0);
                            // the views to look at: 20 yd and 5 yd, head on and at 60 degrees, level (the night's lit and
                            // dark rooms, the day outside)
                            if (sheetFile && height == 0 && (dist == 5 || dist == 20) && (yawDeg == 0 || yawDeg == 60) && (&cs == &cases[0] || &cs == &cases[1] || &cs == &cases[3]))
                            {
                                std::vector<uint8_t> tile(static_cast<size_t>(kS) * kS * 2 * 3, 0);
                                for (int half = 0; half < 2; ++half)
                                    for (size_t p = 0; p < static_cast<size_t>(kS) * kS; ++p)
                                        for (int k = 0; k < 3; ++k)
                                        {
                                            const float val = drawn(half, p) ? img[half][p * 4 + k] : 0.08f;
                                            tile[(half * static_cast<size_t>(kS) * kS + p) * 3 + k] =
                                                static_cast<uint8_t>(std::lround(std::clamp(std::isfinite(val) ? val : 1.0f, 0.0f, 1.0f) * 255.0f));
                                        }
                                sheetTiles.push_back(std::move(tile));
                            }
                        }
                res.speckle = speckleViews ? static_cast<float>(speckleSum / speckleViews) : 0;
                res.shimmer = shimmerViews ? static_cast<float>(shimmerSum / shimmerViews) : 0;
                char line[560];
                const double glassLight = res.lightViews ? res.light / res.lightViews : 0.0;
                auto warmth = [](const double* q) { const double t = q[0] + q[1] + q[2]; return t > 0 ? (q[0] - q[2]) / t : 0.0; };
                const double warmStock = warmth(hue[0]), warmGlass = warmth(hue[1]), warmUncooled = warmth(hue[2]);
                // keep: by day the cooling's own shift (against the same glass without it), at dusk against the stock
                const double kept = cs.dusk ? warmStock : warmUncooled;
                const bool hueOk = (cs.cooler <= 0 || warmGlass <= warmStock - cs.cooler) && (cs.keep < 0 || std::fabs(warmGlass - kept) <= cs.keep);
                std::snprintf(line, sizeof(line), "WARP, Stormwind's windows (%s; %s; %d views 5..50 yd, 0..84 deg): NaN/Inf %d; speckle in the glass x %.2f the "
                              "stock's (mean; worst %.2f at %s), shimmer x %.2f (worst %.2f); the glass at %.2f of the stock's light%s; warmth %.3f against the "
                              "stock's %.3f%s%s",
                              tx.name.c_str(), cs.name, res.views, res.nonfinite, res.speckle, res.worstSpeckle, res.worstAt.c_str(), res.shimmer,
                              res.worstShimmer, glassLight, cs.light > 0 ? (glassLight >= cs.light ? " (at least the bound)" : " (BELOW the bound)") : "",
                              warmGlass, warmStock, cs.keep >= 0 && !cs.dusk ? (", without the day's cooling " + std::to_string(warmUncooled).substr(0, 5)).c_str() : "",
                              cs.cooler > 0 ? (hueOk ? " (cooler)" : " (NOT cooler enough)") : (cs.keep >= 0 ? (hueOk ? " (kept)" : " (NOT kept)") : ""));
                Check(res.views == 50 && res.nonfinite == 0 && speckleViews >= 10 && shimmerViews >= 30 && res.speckle <= cs.speckle &&
                      res.shimmer <= cs.shimmer && res.worstSpeckle <= cs.worst && (cs.light <= 0 || glassLight >= cs.light) && hueOk, line);
            }
            tex->Release();
        }
        if (sheetFile && !sheetTiles.empty())
        {
            const int cols = static_cast<int>(sheetTiles.size());
            std::vector<uint8_t> sheet(static_cast<size_t>(kS) * cols * kS * 2 * 3);
            for (int t = 0; t < cols; ++t)
                for (int y = 0; y < kS * 2; ++y)
                    std::memcpy(&sheet[(static_cast<size_t>(y) * kS * cols + static_cast<size_t>(t) * kS) * 3], &sheetTiles[t][static_cast<size_t>(y) * kS * 3], kS * 3);
            std::ofstream out(sheetFile, std::ios::binary);
            out << "P6\n" << kS * cols << " " << kS * 2 << "\n255\n";
            out.write(reinterpret_cast<const char*>(sheet.data()), static_cast<std::streamsize>(sheet.size()));
            std::printf("night windows: %s (%d views: stock above, glass below)\n", sheetFile, cols);
        }
        w.dev->SetPixelShaderConstantF(gl::kSeedConstant, kZeroSeed, gl::kSeedRegisters);
        readback->Release();
        target->Release();
        uvps->Release();
        ps->Release();
    }

    // --- 6b. by day: rooms without edges (on WARP) ---------------------------------------------------------------------
    // "fake rooms look blocky?" (the user, Stormwind's trade district at noon, 2026-10-18: the rooms' faces and the
    // things on their shelves as hard-edged boxes, a different block in each pane). A facade of clear glass 8 x 5.4 yd
    // (rooms across and up, their cells' edges in it) on one of Stormwind's buildings (its city group's style and seed)
    // at 13:00, the runtime's constants, one flat colour for the client's, so only the room (and the sky) change it;
    // seen from 5, 10 and 20 yd, head on, at 45 and 70 degrees, level and from below. Per view: no NaN or Inf, and the
    // largest change between neighbouring pixels in the glass over the glass's mean (an edge: a step from one pixel to
    // the next) below kDayEdge; the night's crisp look on the same rooms for comparison (its walls', doorways',
    // shelves' and cells' edges). And the room visible ("I also feel like the inside faint rooms are only barely
    // visible", the user, Theramore, 2026-10-18): with the sky's reflection off, its contrast in the glass ((p90 - p10)
    // over the mean) at least kDayContrast over the views and kDayContrastLeast in each (2e61b4d's measured the same on
    // the contact sheet's facade: 0.165, least 0.100).
    void DayRoomTests(const std::filesystem::path& data)
    {
        constexpr float kDayEdge = 0.04f;
        constexpr float kDayContrast = 0.2f, kDayContrastLeast = 0.1f;
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("skip  rooms by day: %s\n", why.c_str()); return; }
        gl::RoomStyle style;
        gl::Building building;
        float mask[4];
        if (!StormwindBuilding(data, style, building, mask)) { building.seed = 4242; std::printf("info  rooms by day: no Stormwind row, the default house\n"); }
        std::vector<uint8_t> code;
        if (!wxl_livingazeroth::shaderpatch::Assemble(BlockShader(), "day rooms", code, why)) { Check(false, "rooms by day: the shader assembles: " + why.substr(0, 200)); return; }
        IDirect3DPixelShader9* ps = nullptr;
        constexpr int kS = 256;
        constexpr float kFocal = 960.0f;
        IDirect3DSurface9 *target = nullptr, *readback = nullptr;
        if (FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps)) ||
            FAILED(w.dev->CreateRenderTarget(kS, kS, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr)) ||
            FAILED(w.dev->CreateOffscreenPlainSurface(kS, kS, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &readback, nullptr)))
        { Check(false, "rooms by day: WARP resources"); return; }
        gl::Inputs in;
        in.sun[0] = -0.4f; in.sun[1] = 0.3f; in.sun[2] = 0.86f; Norm(in.sun);
        in.daylight = 1; in.night = 0;
        in.sky[0] = 0.55f; in.sky[1] = 0.65f; in.sky[2] = 0.8f;
        const gl::Light light = gl::Evaluate(in);
        gl::Material m;
        m.kind = gl::kClear; m.mask[0] = m.mask[1] = m.mask[2] = 0; m.mask[3] = 1;
        gl::Settings s;
        float c[4 * gl::kConstantCount], seed[4 * gl::kSeedRegisters];
        gl::BatchConstants(m, 0.0f, light, in.sun, in.sunColour, in.sky, s, c);
        gl::RoomInputs ri;
        ri.night = 0; ri.hour = 13; ri.selfLit = true;
        gl::RoomBatchConstants(style, &building, light, ri, s, c, seed);
        float day[4 * gl::kDayRegisters];
        gl::DayConstants(m, light, s, building.flags, day);
        w.dev->SetPixelShaderConstantF(gl::kDayConstant, day, gl::kDayRegisters);
        // (pass 2: the same day look without the sky's reflection and glint, so only the room changes the flat glass)
        float noSky[4 * gl::kConstantCount];
        std::memcpy(noSky, c, sizeof(noSky));
        noSky[19] = 0; noSky[21] = 0;
        const float lit[3] = { 0.62f, 0.5f, 0.3f };   // the client's painted glass in the sun
        const float cam0[3] = { -8838.6f, 627.4f, 99.3f }, heading = 0.37f;
        const float dists[] = { 5, 10, 20 }, yaws[] = { 0, 45, 70 }, heights[] = { 0, 2.5f };
        const float hw = 4.0f, hh = 2.7f;
        float worst[2] = {}, meanLight = 0, leastContrast = 1e9f;
        double contrastSum = 0;
        int contrastViews = 0;
        std::string worstAt[2], leastAt;
        int bad = 0, views = 0;
        bool drewAll = true;
        for (int look = 0; look < 3; ++look)
        {
            if (look == 1) UploadLook(w.dev, gl::RoomConstants{});
            else UploadLook(w.dev, light);
            for (float dist : dists)
                for (float yawDeg : yaws)
                    for (float height : heights)
                    {
                        const float yaw = yawDeg * 3.14159265f / 180.0f;
                        const float C[3] = { cam0[0] + dist * std::cos(heading), cam0[1] + dist * std::sin(heading), cam0[2] + height };
                        const float back[3] = { -std::cos(heading), -std::sin(heading), 0 };
                        const float N[3] = { back[0] * std::cos(yaw) - back[1] * std::sin(yaw), back[0] * std::sin(yaw) + back[1] * std::cos(yaw), 0 };
                        const float A[3] = { -N[1], N[0], 0 };
                        float F3[3] = { C[0] - cam0[0], C[1] - cam0[1], C[2] - cam0[2] }; Norm(F3);
                        float R[3] = { F3[1], -F3[0], 0 }; Norm(R);
                        const float U[3] = { R[1] * F3[2] - R[2] * F3[1], R[2] * F3[0] - R[0] * F3[2], R[0] * F3[1] - R[1] * F3[0] };
                        struct PV { float x, y, z, rhw, p[3], n[3], uv[2], base[3]; };
                        PV v[4];
                        const float corner[4][2] = { { -hw, hh }, { hw, hh }, { -hw, -hh }, { hw, -hh } };
                        for (int k = 0; k < 4; ++k)
                        {
                            float q[3];
                            for (int i = 0; i < 3; ++i) q[i] = C[i] + A[i] * corner[k][0] + (i == 2 ? corner[k][1] : 0.0f) - cam0[i];
                            const float zf = q[0] * F3[0] + q[1] * F3[1] + q[2] * F3[2];
                            const float sx = kS * 0.5f + kFocal * (q[0] * R[0] + q[1] * R[1] + q[2] * R[2]) / zf;
                            const float sy = kS * 0.5f - kFocal * (q[0] * U[0] + q[1] * U[1] + q[2] * U[2]) / zf;
                            v[k] = { sx - 0.5f, sy - 0.5f, 0.5f, 1.0f / zf, { q[0], q[1], q[2] }, { N[0], N[1], N[2] }, { 0.5f, 0.5f }, { lit[0], lit[1], lit[2] } };
                        }
                        float cv[16] = { cam0[0], cam0[1], cam0[2], 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
                        IDirect3DDevice9* d = w.dev;
                        d->SetRenderTarget(0, target);
                        D3DVIEWPORT9 vp{ 0, 0, kS, kS, 0.0f, 1.0f };
                        d->SetViewport(&vp);
                        d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
                        d->SetRenderState(D3DRS_ZENABLE, FALSE); d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
                        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
                        d->SetVertexShader(nullptr); d->SetPixelShader(ps);
                        d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE3(0) | D3DFVF_TEXCOORDSIZE3(1) | D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE3(3));
                        d->SetTexture(0, w.texel);
                        d->SetPixelShaderConstantF(gl::kViewConstant, cv, 4);
                        d->SetPixelShaderConstantF(gl::kFirstConstant, look == 2 ? noSky : c, gl::kConstantCount);
                        d->SetPixelShaderConstantF(gl::kSeedConstant, seed, gl::kSeedRegisters);
                        const BOOL on = TRUE, off = FALSE;
                        d->SetPixelShaderConstantB(gl::kSwitch, &on, 1);
                        d->SetPixelShaderConstantB(gl::kFloorSwitch, &off, 1);
                        D3DLOCKED_RECT lr{};
                        bool ok = SUCCEEDED(d->BeginScene());
                        if (ok) { d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(PV)); d->EndScene(); }
                        ok = ok && SUCCEEDED(d->GetRenderTargetData(target, readback)) && SUCCEEDED(readback->LockRect(&lr, nullptr, D3DLOCK_READONLY));
                        drewAll = drewAll && ok;
                        if (!ok) continue;
                        std::vector<float> px(static_cast<size_t>(kS) * kS * 4);
                        for (int row = 0; row < kS; ++row)
                            std::memcpy(&px[static_cast<size_t>(row) * kS * 4], static_cast<const uint8_t*>(lr.pBits) + row * lr.Pitch, sizeof(float) * kS * 4);
                        readback->UnlockRect();
                        auto L = [&](size_t p) { return 0.2126f * px[p * 4] + 0.7152f * px[p * 4 + 1] + 0.0722f * px[p * 4 + 2]; };
                        auto drawn = [&](size_t p) { return px[p * 4 + 3] > 0.5f; };
                        double sum = 0;
                        int n = 0, stepX = 0, stepY = 0;
                        float step = 0;
                        for (int y = 1; y < kS - 1; ++y)
                            for (int x = 1; x < kS - 1; ++x)
                            {
                                const size_t p = static_cast<size_t>(y) * kS + x;
                                for (int k = 0; k < 3; ++k) bad += std::isfinite(px[p * 4 + k]) ? 0 : 1;
                                if (!drawn(p) || !drawn(p - 1) || !drawn(p + 1) || !drawn(p - kS) || !drawn(p + kS)) continue;
                                sum += L(p); ++n;
                                const float here = std::max(std::fabs(L(p) - L(p + 1)), std::fabs(L(p) - L(p + kS)));
                                if (here > step) { step = here; stepX = x; stepY = y; }
                            }
                        // GLASS_DAY_DUMP=<folder>: each view as a picture (day_<look>_<yd>_<deg>_<height>.ppm) and where its step is
                        if (const char* dump = std::getenv("GLASS_DAY_DUMP"))
                        {
                            char name[128];
                            std::snprintf(name, sizeof(name), "day_%s_%02.0f_%02.0f_%.0f.ppm", look == 2 ? "room" : look ? "crisp" : "day", dist, yawDeg, height);
                            std::ofstream f(std::filesystem::path(dump) / name, std::ios::binary);
                            f << "P6\n" << kS << " " << kS << "\n255\n";
                            for (size_t p = 0; p < static_cast<size_t>(kS) * kS; ++p)
                                for (int k = 0; k < 3; ++k)
                                {
                                    const char b = static_cast<char>(std::lround(std::clamp(drawn(p) ? px[p * 4 + k] : 0.1f, 0.0f, 1.0f) * 255.0f));
                                    f.write(&b, 1);
                                }
                            std::printf("      %s: the step %.4f at (%d, %d), the mean %.4f\n", name, step, stepX, stepY, n ? sum / n : 0.0);
                        }
                        if (n < 400) continue;
                        char at[96];
                        std::snprintf(at, sizeof(at), "%.0f yd, %.0f deg%s", dist, yawDeg, height > 0 ? ", from below" : "");
                        if (look == 2)
                        {
                            // the room's contrast in the glass: (p90 - p10) / the mean (no sky: only the room changes it)
                            std::vector<float> ls;
                            ls.reserve(static_cast<size_t>(n));
                            for (int y = 1; y < kS - 1; ++y)
                                for (int x = 1; x < kS - 1; ++x)
                                {
                                    const size_t p = static_cast<size_t>(y) * kS + x;
                                    if (drawn(p) && drawn(p - 1) && drawn(p + 1) && drawn(p - kS) && drawn(p + kS)) ls.push_back(L(p));
                                }
                            std::sort(ls.begin(), ls.end());
                            const float contrast = static_cast<float>((ls[ls.size() * 9 / 10] - ls[ls.size() / 10]) / (sum / n));
                            contrastSum += contrast; ++contrastViews;
                            if (contrast < leastContrast) { leastContrast = contrast; leastAt = at; }
                            continue;
                        }
                        const float rel = static_cast<float>(step / (sum / n));
                        if (!look) { meanLight += static_cast<float>(sum / n); ++views; }
                        if (rel > worst[look]) { worst[look] = rel; worstAt[look] = at; }
                    }
        }
        char line[600];
        std::snprintf(line, sizeof(line), "WARP, rooms by day (Stormwind, clear glass, %d views 5..20 yd, 0..70 deg): NaN/Inf %d; the largest step between "
                      "neighbouring pixels %.1f%% of the glass's mean (at %s; bound %.0f%%), with the night's crisp look %.1f%% (at %s); the glass's mean %.3f "
                      "against the client's %.3f", views, bad, worst[0] * 100, worstAt[0].c_str(), kDayEdge * 100, worst[1] * 100, worstAt[1].c_str(),
                      views ? meanLight / views : 0.0f, 0.2126f * lit[0] + 0.7152f * lit[1] + 0.0722f * lit[2]);
        Check(drewAll && views == 18 && bad == 0 && worst[0] < kDayEdge && worst[1] > 2 * worst[0], line);
        // The room visible: its contrast in the glass (p90 - p10 over the mean, the sky's reflection off so only the room
        // changes the flat glass) at least kDayContrast on average and kDayContrastLeast in every view ("the inside faint
        // rooms are only barely visible", the user, 2026-10-18), with the edges above under kDayEdge.
        const float meanContrast = contrastViews ? static_cast<float>(contrastSum / contrastViews) : 0.0f;
        std::snprintf(line, sizeof(line), "WARP, rooms by day visible: the room's contrast in the glass (p90 - p10) / mean %.3f over %d views (bound %.2f), the least "
                      "%.3f (at %s; bound %.2f)", meanContrast, contrastViews, kDayContrast, leastContrast, leastAt.c_str(), kDayContrastLeast);
        Check(contrastViews == 18 && meanContrast >= kDayContrast && leastContrast >= kDayContrastLeast, line);
        UploadLook(w.dev, gl::RoomConstants{});
        w.dev->SetPixelShaderConstantF(gl::kSeedConstant, kZeroSeed, gl::kSeedRegisters);
        readback->Release();
        target->Release();
        ps->Release();
    }

    // --- 7. lit or dark: the shader and the window lights agree (on WARP) -------------------------------------------
    // The exact share of a flat quad (corners in order, world) in each room cell about `origin` (and its anchors, cut
    // where they meet: room::kAnchor), in the frame of its normal N facing the eye: the quad cut by the cells, a cell
    // with its anchor's offset (the window lights' cells, tools/gen_window_lights.py room_cells).
    std::map<std::pair<int, int>, double> QuadCells(const double corner[4][3], const float N[3], const float origin[3])
    {
        namespace rm = gl::room;
        float a[3] = { -N[1], N[0], 0.0f };
        if (a[0] * a[0] + a[1] * a[1] < 0.0001f) { a[0] = 1; a[1] = 0; }
        const float al = std::sqrt(a[0] * a[0] + a[1] * a[1]);
        a[0] /= al; a[1] /= al;
        const float u[3] = { N[1] * a[2] - N[2] * a[1], N[2] * a[0] - N[0] * a[2], N[0] * a[1] - N[1] * a[0] };
        using P3 = std::vector<std::array<double, 3>>;
        using Poly = std::vector<std::pair<double, double>>;
        P3 quad;
        for (int k = 0; k < 4; ++k) quad.push_back({ corner[k][0] - origin[0], corner[k][1] - origin[1], corner[k][2] - origin[2] });
        auto clip3 = [](const P3& p, int axis, double bound, bool above) {
            P3 out;
            for (size_t i = 0; i < p.size(); ++i)
            {
                const auto& s = p[i];
                const auto& e = p[(i + 1) % p.size()];
                const bool si = above ? s[axis] >= bound : s[axis] <= bound, ei = above ? e[axis] >= bound : e[axis] <= bound;
                if (si) out.push_back(s);
                if (si != ei)
                {
                    const double t = (bound - s[axis]) / (e[axis] - s[axis]);
                    out.push_back({ s[0] + (e[0] - s[0]) * t, s[1] + (e[1] - s[1]) * t, s[2] + (e[2] - s[2]) * t });
                }
            }
            return out;
        };
        auto clip = [](const Poly& p, int axis, double bound, bool above) {
            Poly out;
            for (size_t i = 0; i < p.size(); ++i)
            {
                const auto& s = p[i];
                const auto& e = p[(i + 1) % p.size()];
                const double sv = axis ? s.second : s.first, ev = axis ? e.second : e.first;
                const bool si = above ? sv >= bound : sv <= bound, ei = above ? ev >= bound : ev <= bound;
                if (si) out.push_back(s);
                if (si != ei)
                {
                    const double t = (bound - sv) / (ev - sv);
                    out.push_back({ s.first + (e.first - s.first) * t, s.second + (e.second - s.second) * t });
                }
            }
            return out;
        };
        auto area = [](const Poly& p) {
            double s = 0;
            for (size_t i = 0; i < p.size(); ++i) s += p[i].first * p[(i + 1) % p.size()].second - p[(i + 1) % p.size()].first * p[i].second;
            return std::fabs(s) * 0.5;
        };
        auto project = [&](const P3& p, const int k[3]) {
            Poly st;
            for (const auto& q : p)
            {
                const double r[3] = { q[0] - k[0] * rm::kAnchor, q[1] - k[1] * rm::kAnchor, q[2] - k[2] * rm::kAnchor };
                st.push_back({ (r[0] * a[0] + r[1] * a[1] + r[2] * a[2]) / 3.5, (r[0] * u[0] + r[1] * u[1] + r[2] * u[2]) / 3.2 });
            }
            return st;
        };
        const int zero[3] = { 0, 0, 0 };
        const double whole = area(project(quad, zero));
        int lo[3], hi[3];
        for (int i = 0; i < 3; ++i)
        {
            double mn = 1e30, mx = -1e30;
            for (const auto& q : quad) { mn = std::min(mn, q[i]); mx = std::max(mx, q[i]); }
            lo[i] = static_cast<int>(std::floor(mn / rm::kAnchor + 0.5)); hi[i] = static_cast<int>(std::floor(mx / rm::kAnchor + 0.5));
        }
        std::map<std::pair<int, int>, double> out;
        int k[3];
        for (k[0] = lo[0]; k[0] <= hi[0]; ++k[0])
            for (k[1] = lo[1]; k[1] <= hi[1]; ++k[1])
                for (k[2] = lo[2]; k[2] <= hi[2]; ++k[2])
                {
                    P3 part = quad;
                    for (int i = 0; i < 3 && part.size() >= 3; ++i)
                        part = clip3(clip3(part, i, (k[i] - 0.5) * rm::kAnchor, true), i, (k[i] + 0.5) * rm::kAnchor, false);
                    if (part.size() < 3) continue;
                    const int ox = static_cast<int>(k[0] * rm::kAnchorKeyX[0] + k[1] * rm::kAnchorKeyX[1] + k[2] * rm::kAnchorKeyX[2]);
                    const int oy = static_cast<int>(k[0] * rm::kAnchorKeyY[0] + k[1] * rm::kAnchorKeyY[1] + k[2] * rm::kAnchorKeyY[2]);
                    const Poly st = project(part, k);
                    double x0 = 1e30, x1 = -1e30;
                    for (const auto& q : st) { x0 = std::min(x0, q.first); x1 = std::max(x1, q.first); }
                    for (int x = static_cast<int>(std::floor(x0)); x <= static_cast<int>(std::floor(x1)); ++x)
                    {
                        const Poly col = clip(clip(st, 0, x, true), 0, x + 1.0, false);
                        if (col.size() < 3) continue;
                        double y0 = 1e30, y1 = -1e30;
                        for (const auto& q : col) { y0 = std::min(y0, q.second); y1 = std::max(y1, q.second); }
                        for (int y = static_cast<int>(std::floor(y0)); y <= static_cast<int>(std::floor(y1)); ++y)
                        {
                            const Poly piece = clip(clip(col, 1, y, true), 1, y + 1.0, false);
                            if (piece.size() >= 3) out[{ x + ox, y + oy }] += area(piece) / whole;
                        }
                    }
                }
        return out;
    }

    void LitAgreementTests()
    {
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("skip  lit agreement: %s\n", why.c_str()); return; }
        // The blocks with a probe right after the room's lit value: r0 = (lit, the lit hash, the cell about the origin).
        const std::string probe = "\n        mov r0.x, r20.w\n        mov r0.y, r22.x\n        mov r0.zw, r29.xxxy";
        const std::string text = "ps_3_0\n    def c175, 1, 0, 0.5, 2\n    def c176, 0.2126, 0.7152, 0.0722, 0.2\n" + gl::ShaderDefs() +
                                 "    dcl_texcoord0 v0.xyz\n    dcl_texcoord1 v1.xyz\n    dcl_texcoord2 v2.xy\n    dcl_texcoord3 v3.xyz\n    dcl_2d s0\n"
                                 "    mov r0, c175.y\n    mov r12.xyz, v3\n    mov r31.xyz, c175.y" + gl::GlassBlock("v0", "v1", "v2", probe) +
                                 "\n    mov oC0, r0\n";
        std::vector<uint8_t> code;
        if (!wxl_livingazeroth::shaderpatch::Assemble(text, "lit probe", code, why)) { Check(false, "the lit probe assembles: " + why.substr(0, 300)); return; }
        IDirect3DPixelShader9* ps = nullptr;
        constexpr int kS = 128;
        IDirect3DSurface9 *target = nullptr, *readback = nullptr;
        if (FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps)) ||
            FAILED(w.dev->CreateRenderTarget(kS, kS, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr)) ||
            FAILED(w.dev->CreateOffscreenPlainSurface(kS, kS, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &readback, nullptr)))
        { Check(false, "the lit probe on WARP"); return; }
        UploadLook(w.dev, gl::RoomConstants{});

        // Places: about the world's origin, Goldshire, Stormwind (its WMO's origin hundreds of yards from a window),
        // Northrend's far corner. Each window: a random facing (a little tilt), 1.5 .. 5 yd wide, 1.2 .. 4 tall, 6 .. 45 yd
        // from the camera, its building's origin 0 .. 600 yd away, a random seed, and a band: at rest, a slow clock's
        // fade, a sweep's, everything dark or lit.
        struct Place { const char* name; float x, y, z; };
        const Place places[] = { { "the origin", 0, 0, 0 }, { "Goldshire", -9460, 62, 57 }, { "Stormwind", -8838.6f, 627.4f, 99.3f },
                                 { "Northrend's far side", 5800, 600, 640 } };
        uint32_t rng = 12345;
        auto rnd = [&rng]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };
        int windows = 0, hardCell = 0, edgeCell = 0, hashOff = 0, litOff = 0, partLit = 0, wobbleOff = 0;
        double worstEdge = 0;
        long long pixels = 0;
        float worstShare = 0, sumShare = 0;
        bool drewAll = true;
        std::string worstAt;
        for (const Place& pl : places)
            for (int win = 0; win < 24; ++win)
            {
                const float yaw = rnd() * 6.2831853f, pitch = (rnd() - 0.5f) * 0.3f;
                float N[3] = { std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch) };
                const float hw = 0.75f + rnd() * 1.75f, hh = 0.6f + rnd() * 1.4f, dist = 6.0f + rnd() * 39.0f;
                // the window's middle (world) and the camera in front of it, a little off its axis
                const float mid[3] = { pl.x + (rnd() - 0.5f) * 60.0f, pl.y + (rnd() - 0.5f) * 60.0f, pl.z + rnd() * 12.0f };
                const float side = (rnd() - 0.5f) * 0.6f, lift = (rnd() - 0.5f) * 0.4f;
                float a[3] = { -N[1], N[0], 0 }; Norm(a);
                const float u[3] = { N[1] * a[2] - N[2] * a[1], N[2] * a[0] - N[0] * a[2], N[0] * a[1] - N[1] * a[0] };
                const float cam[3] = { mid[0] + (N[0] + a[0] * side + u[0] * lift) * dist, mid[1] + (N[1] + a[1] * side + u[1] * lift) * dist,
                                       mid[2] + (N[2] + a[2] * side + u[2] * lift) * dist };
                const float originDist = (win % 3 == 0) ? 0.0f : rnd() * 600.0f, originYaw = rnd() * 6.2831853f;
                const float origin[3] = { mid[0] + std::cos(originYaw) * originDist, mid[1] + std::sin(originYaw) * originDist, mid[2] - rnd() * 20.0f };
                // the quad: corners about the camera (as the shader gets them: floats) in screen order (top-left,
                // top-right, bottom-left, bottom-right); its real normal from those (a float corner at -8800 yd is a
                // hair off the ideal plane), facing the eye; the corners in the world as a polygon
                float rel[4][3];
                const float cs[4][2] = { { -hw, hh }, { hw, hh }, { -hw, -hh }, { hw, -hh } };
                for (int k = 0; k < 4; ++k)
                    for (int i = 0; i < 3; ++i)
                        rel[k][i] = static_cast<float>(static_cast<double>(mid[i]) - cam[i] + static_cast<double>(a[i]) * cs[k][0] + static_cast<double>(u[i]) * cs[k][1]);
                {
                    double e1[3], e2[3], nn[3];
                    for (int i = 0; i < 3; ++i) { e1[i] = static_cast<double>(rel[1][i]) - rel[0][i]; e2[i] = static_cast<double>(rel[2][i]) - rel[0][i]; }
                    nn[0] = e1[1] * e2[2] - e1[2] * e2[1]; nn[1] = e1[2] * e2[0] - e1[0] * e2[2]; nn[2] = e1[0] * e2[1] - e1[1] * e2[0];
                    const double len = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
                    const double facing = -(nn[0] * rel[0][0] + nn[1] * rel[0][1] + nn[2] * rel[0][2]) >= 0 ? 1.0 : -1.0;
                    for (int i = 0; i < 3; ++i) N[i] = static_cast<float>(nn[i] / len * facing);
                }
                double poly[4][3];
                const int order[4] = { 0, 1, 3, 2 };
                for (int k = 0; k < 4; ++k)
                    for (int i = 0; i < 3; ++i) poly[k][i] = static_cast<double>(rel[order[k]][i]) + cam[i];
                // the seed and the band
                const float seed[2] = { std::floor(rnd() * 1024.0f), std::floor(rnd() * 1024.0f) };
                gl::DarkBand band;
                switch (win % 8)
                {
                case 0: { const float t = rnd(); band = gl::Band(t, t); break; }                            // at rest
                case 1: { const float t = rnd(); band = gl::Band(t + 6e-5f, t); break; }                    // the clock, going dark
                case 2: { const float t = rnd() * 0.7f; band = gl::Band(t, t + 0.25f); break; }            // a fast clock, lit again
                case 3: { const float t = rnd() * 0.5f; band = gl::Band(t + 0.4f, t); break; }             // ... going dark
                case 4: { const float t = rnd() * 0.3f; band = gl::Crossfade(t, t + 0.5f, rnd()); break; } // a jump, going dark
                case 5: { const float t = rnd() * 0.3f; band = gl::Crossfade(t + 0.5f, t, rnd()); break; } // a jump, lit again
                case 6: band = gl::Band(1.0f, 1.0f); break;                                                // a dark place
                default: band = gl::Band(0.0f, 0.0f); break;                                               // every room lit
                }
                // the constants: outside, the room on, nothing else that matters to it
                float c[4 * gl::kConstantCount] = {};
                c[3] = 1; c[5] = 1; c[6] = 0.65f;
                c[24] = 1 / 3.5f; c[25] = 1 / 3.2f; c[26] = 0.25f; c[27] = 0.3f;
                c[28] = 0.85f; c[29] = 0.72f; c[30] = 0.55f;
                c[40] = 0.85f; c[41] = 0.72f; c[42] = 0.55f;
                c[35] = band.hi;
                c[47] = band.lo;
                c[51] = 1;
                const float sd[4 * gl::kSeedRegisters] = { seed[0], seed[1], 0, band.slope, origin[0], origin[1], origin[2], band.base };
                float view[16] = { cam[0], cam[1], cam[2], 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
                w.dev->SetPixelShaderConstantF(gl::kViewConstant, view, 4);
                w.dev->SetPixelShaderConstantF(gl::kSeedConstant, sd, gl::kSeedRegisters);
                // Twice: the vertex normal flat, then wobbled 8 degrees at the corners (a smoothed mesh): the cells go
                // by the face's own normal, so the same.
                std::vector<float> first;
                for (int pass = 0; pass < 2; ++pass)
                {
                    struct PV { float x, y, z, rhw, p[3], n[3], uv[2], base[3]; };
                    PV v[4];
                    const float e = kS - 0.5f, xy[4][2] = { { -0.5f, -0.5f }, { e, -0.5f }, { -0.5f, e }, { e, e } };
                    for (int k = 0; k < 4; ++k)
                    {
                        float nn[3] = { N[0], N[1], N[2] };
                        if (pass) { nn[0] += 0.14f * (k & 1 ? 1 : -1); nn[2] += 0.14f * (k & 2 ? 1 : -1); Norm(nn); }
                        v[k] = { xy[k][0], xy[k][1], 0, 1, { rel[k][0], rel[k][1], rel[k][2] }, { nn[0], nn[1], nn[2] },
                                 { 0.5f, 0.5f }, { 0.5f, 0.4f, 0.3f } };
                    }
                    IDirect3DDevice9* d = w.dev;
                    d->SetRenderTarget(0, target);
                    D3DVIEWPORT9 vp{ 0, 0, kS, kS, 0.0f, 1.0f };
                    d->SetViewport(&vp);
                    d->SetRenderState(D3DRS_ZENABLE, FALSE); d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
                    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
                    d->SetVertexShader(nullptr); d->SetPixelShader(ps);
                    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE3(0) | D3DFVF_TEXCOORDSIZE3(1) | D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE3(3));
                    d->SetTexture(0, w.texel);
                    d->SetPixelShaderConstantF(gl::kFirstConstant, c, gl::kConstantCount);
                    const BOOL on = TRUE, off = FALSE;
                    d->SetPixelShaderConstantB(gl::kSwitch, &on, 1);
                    d->SetPixelShaderConstantB(gl::kFloorSwitch, &off, 1);
                    D3DLOCKED_RECT lr{};
                    bool ok = SUCCEEDED(d->BeginScene());
                    if (ok) { d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(PV)); d->EndScene(); }
                    ok = ok && SUCCEEDED(d->GetRenderTargetData(target, readback)) && SUCCEEDED(readback->LockRect(&lr, nullptr, D3DLOCK_READONLY));
                    drewAll = drewAll && ok;
                    if (!ok) break;
                    std::vector<float> px(static_cast<size_t>(kS) * kS * 4);
                    for (int row = 0; row < kS; ++row)
                        std::memcpy(&px[static_cast<size_t>(row) * kS * 4], static_cast<const uint8_t*>(lr.pBits) + row * lr.Pitch, sizeof(float) * kS * 4);
                    readback->UnlockRect();
                    if (pass == 0) { first = px; continue; }
                    // the wobbled normals: the same cells and lit values as the flat ones
                    for (size_t i = 0; i < px.size(); i += 4)
                        if (px[i] != first[i] || px[i + 2] != first[i + 2] || px[i + 3] != first[i + 3]) ++wobbleOff;
                }
                if (first.empty()) continue;
                ++windows;
                // Pixel by pixel against the CPU's rules: the cell from the pixel's point, the hash, the lit value.
                double gpuSum = 0;
                for (int row = 0; row < kS; ++row)
                    for (int col = 0; col < kS; ++col)
                    {
                        const float* g = &first[(static_cast<size_t>(row) * kS + col) * 4];
                        const float fu = (col + 0.5f) / kS, fv = (row + 0.5f) / kS;
                        float P[3];
                        for (int i = 0; i < 3; ++i)
                            P[i] = rel[0][i] + (rel[1][i] - rel[0][i]) * fu + (rel[2][i] - rel[0][i]) * fv + cam[i];
                        const float inv[2] = { 1 / 3.5f, 1 / 3.2f };
                        float cell[2];
                        gl::RoomCell(P, origin, N, inv, cell);
                        if (cell[0] != g[2] || cell[1] != g[3])
                        {
                            // A pixel a hair from a cell's edge may land either side: the face normal from the screen
                            // gradients is ~1e-4 rad off, which moves an edge by that x the point's distance to its
                            // anchor (R'). Allowed within 3e-4 R' + 5 mm of the edge; the worst seen is reported.
                            double R[3], len = 0;
                            for (int i = 0; i < 3; ++i)
                            {
                                const double r = static_cast<double>(P[i]) - origin[i];
                                R[i] = r - std::floor(r / gl::room::kAnchor + 0.5) * gl::room::kAnchor;
                                len += R[i] * R[i];
                            }
                            len = std::sqrt(len);
                            float fa[3] = { -N[1], N[0], 0 }; Norm(fa);
                            const float fu2[3] = { N[1] * fa[2] - N[2] * fa[1], N[2] * fa[0] - N[0] * fa[2], N[0] * fa[1] - N[1] * fa[0] };
                            const double s = (R[0] * fa[0] + R[1] * fa[1] + R[2] * fa[2]) / 3.5, t = (R[0] * fu2[0] + R[1] * fu2[1] + R[2] * fu2[2]) / 3.2;
                            const double ds = std::fabs(s - std::round(s)) * 3.5, dt = std::fabs(t - std::round(t)) * 3.2;
                            const double edge = std::max(cell[0] != g[2] ? ds : 0.0, cell[1] != g[3] ? dt : 0.0);
                            worstEdge = std::max(worstEdge, std::max(0.0, edge - 0.005) / std::max(len, 1.0));
                            if (edge > 3e-4 * len + 0.005) ++hardCell;
                            else ++edgeCell;
                        }
                        // the hash and the lit value of the GPU's own cell: the same bits
                        const float h = gl::LitHash(g[2], g[3], seed[0], seed[1]);
                        hashOff += Bits(h) != Bits(g[1]) ? 1 : 0;
                        litOff += Bits(gl::RoomLit(h, band)) != Bits(g[0]) ? 1 : 0;
                        partLit += g[0] > 0.0f && g[0] < 1.0f ? 1 : 0;
                        gpuSum += g[0];
                        ++pixels;
                    }
                // The window light's share (its cells cut exactly, each x its room's lit value) against the pixels'.
                std::vector<gl::LightCell> cells;
                for (const auto& kv : QuadCells(poly, N, origin))
                {
                    gl::LightCell lc;
                    lc.hash = gl::LitHash(static_cast<float>(kv.first.first), static_cast<float>(kv.first.second), seed[0], seed[1]);
                    lc.share = static_cast<float>(kv.second); lc.band = band;
                    cells.push_back(lc);
                }
                const float cpuShare = gl::LitShare(cells.data(), cells.size()), gpuShare = static_cast<float>(gpuSum / (kS * kS));
                sumShare += gpuShare;
                if (std::fabs(cpuShare - gpuShare) > worstShare)
                {
                    worstShare = std::fabs(cpuShare - gpuShare);
                    char at[160];
                    std::snprintf(at, sizeof(at), "%s, window %d: %zu cells, %.3f against %.3f", pl.name, win, cells.size(), cpuShare, gpuShare);
                    worstAt = at;
                }
            }
        char line[600];
        std::snprintf(line, sizeof(line), "WARP, lit or dark, the shader against the window lights' rules: %d windows (the origin, Goldshire, Stormwind, Northrend; "
                      "6 .. 45 yd; origins 0 .. 600 yd away; at rest, the clock fading, cross-fades after a jump, all dark, all lit), %lld pixels: the lit hash's "
                      "bits differ at %d, the lit value's at %d (%d part lit); the cell differs at %d pixels, all at a cell's edge (within %.1e x their "
                      "distance to the anchor + 5 mm; %d farther); vertex normals wobbled 8 degrees change %d; the light's share from its cells against the "
                      "pixels' worst %.4f (%s), mean lit %.2f",
                      windows, pixels, hashOff, litOff, partLit, edgeCell + hardCell, worstEdge, hardCell, wobbleOff, worstShare, worstAt.c_str(),
                      windows ? sumShare / windows : 0.0f);
        Check(drewAll && windows == 96 && hashOff == 0 && litOff == 0 && hardCell == 0 && edgeCell < pixels / 1000 && wobbleOff == 0 && partLit > 1000 &&
              worstShare < 0.01f, line);
        const float zero[16] = {};
        w.dev->SetPixelShaderConstantF(gl::kViewConstant, zero, 4);
        w.dev->SetPixelShaderConstantF(gl::kSeedConstant, kZeroSeed, gl::kSeedRegisters);
        readback->Release();
        target->Release();
        ps->Release();
    }
}

namespace
{
    // --- 8. frosted glass: lights out without a room, the hearth's flicker, Ironforge --------------------------------
    // "Give Living Azeroth's frosted window glass (dwarven, vrykul, draenei and similar) a night lights-out schedule and a
    // hearth-fire flicker" (the user, 2026-10-18).
    const char* const kIronforgePath = "world\\wmo\\khazmodan\\cities\\ironforge\\ironforge.wmo";

    // A batch's constants as the runtime makes them (BatchConstants, then RoomBatchConstants), no rooms' clock.
    void FrostBatch(const gl::Settings& s, const gl::RoomStyle& style, const gl::Building* b, float night, float hour, uint32_t kind, bool selfLit,
                    bool unlit, float inside, float c[4 * gl::kConstantCount], float sd[4 * gl::kSeedRegisters])
    {
        gl::Material m;
        m.kind = kind;
        gl::Inputs in;
        in.daylight = 1.0f - night; in.night = night;
        const gl::Light lt = gl::Evaluate(in);
        gl::BatchConstants(m, inside, lt, in.sun, in.sunColour, in.sky, s, c);
        gl::RoomInputs ri;
        ri.night = night; ri.hour = hour; ri.selfLit = selfLit; ri.unlit = unlit; ri.kind = kind; ri.hearthClock = 321.5f; ri.fallbackSeed = 9;
        gl::RoomBatchConstants(style, b, lt, ri, s, c, sd);
    }

    void FrostedRulesTests()
    {
        namespace hh = gl::hearth;
        namespace fl = wxl_livingazeroth::flame;
        char line[640];
        // The hearth's rates: env/Flame's (the torches') at half their pace, each a whole number of cycles in the clock's
        // wrap; its weights twice env/Flame's (f = 0.5 + sum w q (1 - |q|) runs 0..1).
        bool rates = true;
        for (int k = 0; k < 3; ++k)
        {
            const double want = fl::kRates[k] / fl::kTwoPi * hh::kPace;
            rates = rates && std::fabs(hh::kRate[k] - want) <= 0.5 / 1024.0 + 1e-7 &&
                    std::fabs(hh::kRate[k] * hh::kWrap - std::round(hh::kRate[k] * hh::kWrap)) < 1e-4 &&
                    std::fabs(hh::kWeight[k] - 2.0 * fl::kWeights[k]) < 1e-6;
        }
        // The dip over two minutes at 60 frames a second, for a spread of windows: within 0..A, about A / 2 on average,
        // a frame's step small (a gentle flicker), the same bits every time, no seam where the clock wraps.
        const float A = 0.08f;
        float lo = 1, hi = 0, step = 0, seam = 0;
        double sum = 0;
        long n = 0;
        bool same = true;
        for (int hk = 0; hk < 289; hk += 17)
        {
            const float h = (hk + 0.5f) / 289.0f;
            float last = gl::HearthDip(h, 0.0f, A);
            for (int f = 1; f <= 60 * 120; ++f)
            {
                const float t = f / 60.0f;
                const float d = gl::HearthDip(h, t, A);
                lo = std::min(lo, d); hi = std::max(hi, d); sum += d; ++n;
                step = std::max(step, std::fabs(d - last));
                last = d;
                same = same && Bits(d) == Bits(gl::HearthDip(h, t, A));
            }
            for (float t : { 0.0f, 3.7f, 500.25f, 1023.9f })
                seam = std::max(seam, std::fabs(gl::HearthDip(h, t, A) - gl::HearthDip(h, t + hh::kWrap, A)));
        }
        const double mean = sum / n;
        // Two windows' flickers over two minutes: each its own (their correlation low).
        double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
        const int m = 7200;
        for (int f = 0; f < m; ++f)
        {
            const float t = f / 60.0f;
            const double a = gl::HearthDip(10.5f / 289.0f, t, 1.0f), b = gl::HearthDip(200.5f / 289.0f, t, 1.0f);
            sa += a; sb += b; saa += a * a; sbb += b * b; sab += a * b;
        }
        const double corr = (sab / m - sa / m * sb / m) / std::sqrt((saa / m - sa * sa / m / m) * (sbb / m - sb * sb / m / m));
        std::snprintf(line, sizeof(line), "the hearth's flicker: %.4f, %.4f, %.4f Hz (env/Flame's rates x %.1f, whole cycles in %.0f s); the dip %.4f .. %.4f of %.2f, "
                      "mean %.4f, a frame's step at most %.4f, the same bits every time, %.1e across the wrap; two windows' correlation %.2f",
                      hh::kRate[0], hh::kRate[1], hh::kRate[2], hh::kPace, hh::kWrap, lo, hi, A, mean, step, seam, corr);
        Check(rates && lo >= 0.0f && hi <= A && hi > 0.8f * A && lo < 0.2f * A && std::fabs(mean - 0.5 * A) < 0.05 * A && step < 0.07f * A && same &&
              seam < 1e-3f * A && std::fabs(corr) < 0.5, line);

        // The hearth clock: steady seconds x the speed; a hitch counts a quarter second at most; it wraps.
        gl::HearthClock hc;
        hc.Advance(100.0, 1.0f);
        for (int f = 1; f <= 600; ++f) hc.Advance(100.0 + f / 60.0, 1.0f);
        const float t10 = hc.Now();
        for (int f = 1; f <= 600; ++f) hc.Advance(110.0 + f / 60.0, 2.0f);
        const float t30 = hc.Now();
        hc.Advance(125.0, 1.0f);
        const float hitch = hc.Now() - t30;
        gl::HearthClock wrap;
        wrap.Advance(0.0, 1.0f);
        for (int f = 1; f <= 4200; ++f) wrap.Advance(f * 0.25, 1.0f);
        std::snprintf(line, sizeof(line), "the hearth clock: 10 s at x1 %.3f, 10 more at x2 %.3f, a 5 s hitch counts %.3f s, 1050 s later it reads %.2f (wraps at %.0f)",
                      t10, t30, hitch, wrap.Now(), hh::kWrap);
        Check(Near(t10, 10.0f, 1e-3f) && Near(t30, 30.0f, 2e-3f) && Near(hitch, 0.25f, 1e-4f) && wrap.Now() >= 0.0f && wrap.Now() < hh::kWrap &&
              Near(wrap.Now(), 26.0f, 1e-2f), line);

        // Which batches: lit frosted glass only (the client lights it: SIDN, or unlit), never a dungeon's, with lights out
        // or the flicker on.
        gl::Settings s;
        gl::Settings off = s;
        off.frostedLightsOut = 0; off.hearthFlicker = 0;
        gl::Settings flicker = off;
        flicker.hearthFlicker = 0.08f;
        Check(gl::FrostedLit(gl::kFrosted, true, false, 0, s) && gl::FrostedLit(gl::kFrosted, false, true, 0, s) &&
              !gl::FrostedLit(gl::kFrosted, false, false, 0, s) && !gl::FrostedLit(gl::kClear, true, false, 0, s) &&
              !gl::FrostedLit(gl::kStained, true, true, 0, s) && !gl::FrostedLit(gl::kFrosted, true, false, gl::kBuildingDungeon, s) &&
              !gl::FrostedLit(gl::kFrosted, true, false, 0, off) && gl::FrostedLit(gl::kFrosted, true, false, 0, flicker),
              "lit frosted glass: frosted, lit by the client (SIDN or unlit), not a dungeon's, with FrostedLightsOut or HearthFlicker on");

        // A batch's constants.
        gl::RoomStyle house;
        gl::Building plain;
        plain.seed = 77; plain.uniqueId = 5;
        float c[4 * gl::kConstantCount], sd[4 * gl::kSeedRegisters];
        const float share3 = gl::LightsOutShare(3.0f, 0.0f, 1.0f);
        FrostBatch(s, house, &plain, 1, 3, gl::kFrosted, true, false, 0, c, sd);
        const bool night3 = c[39] == 1 && Near(c[55], hh::kFrostedHidden, 1e-6f) && Near(c[38], 0.08f, 1e-6f) && c[37] == 321.5f &&
                            Near(c[47], share3, 1e-6f) && Near(c[35], share3, 1e-6f) && c[6] == 0 && c[44] == 0 && sd[0] == 77;
        const float hidden3 = c[55], amp3 = c[38];
        FrostBatch(s, house, &plain, 0, 12, gl::kFrosted, true, false, 0, c, sd);
        const bool noon = c[39] == 1 && c[55] == 0 && c[38] == 0;
        FrostBatch(s, house, &plain, 1, 3, gl::kFrosted, false, true, 0, c, sd);
        const bool unlit = c[39] == 1 && Near(c[55], hh::kFrostedHidden, 1e-6f);
        FrostBatch(s, house, &plain, 1, 3, gl::kFrosted, false, false, 0, c, sd);
        const bool notLit = c[39] == 0 && c[38] == 0;
        gl::Building dungeon = plain;
        dungeon.flags = gl::kBuildingDungeon;
        FrostBatch(s, house, &dungeon, 1, 3, gl::kFrosted, true, false, 0, c, sd);
        const bool keeps = c[39] == 0;
        FrostBatch(flicker, house, &plain, 1, 3, gl::kFrosted, true, false, 0, c, sd);
        const bool flickerOnly = c[39] == 1 && c[55] == 0 && c[47] == 0 && c[35] == 0 && Near(c[38], 0.08f, 1e-6f);
        gl::Building dark = plain;
        dark.flags = gl::kBuildingDarkPlace;
        FrostBatch(s, house, &dark, 1, 3, gl::kFrosted, true, false, 0, c, sd);
        const bool darkPlace = c[39] == 1 && c[47] == 1 && c[35] == 1;
        // Ironforge: its own hours, or with IronforgeAlwaysLit every window lit; underground the street's look, no sky.
        gl::RoomStyle quarter;
        quarter.cls = gl::kClassQuarter; quarter.lateHours = 1.0f; quarter.nightCap = 0.45f;
        gl::Building ironforge = plain;
        ironforge.path = gl::PathHash(kIronforgePath);
        ironforge.flags = gl::kBuildingCity | gl::kBuildingUnderground;
        FrostBatch(s, quarter, &ironforge, 1, 3, gl::kFrosted, false, true, 1, c, sd);
        const bool ifHours = c[39] == 1 && Near(c[47], 0.45f, 1e-6f) && c[4] == 0 && c[19] == 0 && c[20] == 0 && c[21] == 0;
        gl::Settings always = s;
        always.ironforgeAlwaysLit = 1;
        FrostBatch(always, quarter, &ironforge, 1, 3, gl::kFrosted, false, true, 1, c, sd);
        const bool ifLit = c[39] == 1 && c[47] == 0 && c[35] == 0 && Near(c[38], 0.08f, 1e-6f);
        FrostBatch(always, quarter, &plain, 1, 3, gl::kFrosted, false, true, 1, c, sd);
        const bool othersNot = Near(c[47], 0.45f, 1e-6f) && c[4] == 1;   // not Ironforge, not underground: as before
        // Rooms: the lamps steady by default (c201.zw 0: the rooms exactly as before), flickering with LampFlicker.
        FrostBatch(s, house, &plain, 1, 23, gl::kClear, true, false, 0, c, sd);
        const bool steady = c[38] == 0 && c[39] == 0 && c[37] == 321.5f;
        gl::Settings lamps = s;
        lamps.lampFlicker = 0.03f;
        FrostBatch(lamps, house, &plain, 1, 23, gl::kClear, true, false, 0, c, sd);
        const bool lampFlicker = Near(c[38], 0.03f, 1e-6f) && c[39] == 0;
        FrostBatch(lamps, house, &plain, 1, 23, gl::kClear, false, false, 0, c, sd);
        const bool noLamp = c[38] == 0;
        std::snprintf(line, sizeof(line), "frosted batches: at 03:00 a house's %.2f dark, a dark one hides %.2f of the glow, the flicker %.2f; at noon none; unlit "
                      "glass too, not-lit glass not; a dungeon's keeps the client's look; FrostedLightsOut off: all lit, still flickering; a dark place all "
                      "dark; Ironforge's quarters 0.45 dark, underground (the street's look, no sky), all lit with IronforgeAlwaysLit (others not); rooms' "
                      "lamps steady by default, flickering with LampFlicker (not where there's no lamp)", share3, hidden3, amp3);
        Check(night3 && noon && unlit && notLit && keeps && flickerOnly && darkPlace && ifHours && ifLit && othersNot && steady && lampFlicker && noLamp, line);

        // The window light: each cell lit x (1 - its dip); glass with no cell stays lit.
        gl::LightCell cells[2];
        cells[0].hash = 0.9f; cells[0].share = 0.6f; cells[0].band = gl::Band(0.5f, 0.5f); cells[0].dip = 0.05f;
        cells[1].hash = 0.1f; cells[1].share = 0.3f; cells[1].band = cells[0].band; cells[1].dip = 0.05f;
        std::snprintf(line, sizeof(line), "a window light's share with the flicker: lit 60%% x 0.95, dark 30%%, no cell 10%% -> %.4f", gl::LitShare(cells, 2));
        Check(Near(gl::LitShare(cells, 2), 0.1f + 0.6f * 0.95f, 1e-6f), line);

        // RoomOutside for frosted glass (no room: k.share 0): the client's colour x (1 - hide), hide = (1 - lit) x 0.85 + lit x dip.
        gl::RoomConstants k;
        k.share = 0; k.c205[3] = hh::kFrostedHidden; k.c201[1] = 77.0f; k.c201[2] = 0.1f; k.c201[3] = 1;
        k.c203[3] = 0.5f; k.c200w = 0.5f; k.c212[0] = 3; k.c212[1] = 900;
        int frostOk = 0, frostN = 0, darkN = 0;
        for (int y = -20; y < 20; ++y)
        {
            const float r12[3] = { 0.8f, 0.5f, 0.3f };
            float out[3];
            gl::RoomSample sm;
            RoomAt(k, y * 0.9f, 1.7f, r12, out, &sm);
            const float hide = (1.0f - sm.litValue) * hh::kFrostedHidden + sm.litValue * sm.dip;
            frostOk += Near(out[0], r12[0] * (1 - hide), 1e-5f) && Near(out[2], r12[2] * (1 - hide), 1e-5f) &&
                       Near(sm.dip, gl::HearthDip(sm.litHash, 77.0f, 0.1f), 1e-7f) ? 1 : 0;
            darkN += sm.litValue < 0.5f ? 1 : 0;
            ++frostN;
        }
        std::snprintf(line, sizeof(line), "frosted glass seen from outside (RoomOutside, no room): the client's colour x (1 - hide) at %d of %d points (%d dark)",
                      frostOk, frostN, darkN);
        Check(frostOk == frostN && darkN > 5 && darkN < frostN - 5, line);
    }

    // The shipped tables: Ironforge's groups by sub-zone, their hours, the flags, the panes with no sky, WindowRooms' frosted
    // cells.
    void FrostedTableTests(const std::filesystem::path& data)
    {
        Table st, bu, pa, wr;
        if (!ReadTable(data / "RoomStyles.cdbc", st) || !ReadTable(data / "GlassBuildings.cdbc", bu) || !ReadTable(data / "GlassPanes.cdbc", pa) ||
            !ReadTable(data / "WindowRooms.cdbc", wr))
        { Check(false, "the shipped tables read (frosted)"); return; }
        std::map<uint32_t, gl::RoomStyle> styles;
        for (const auto& r : st.rows)
        {
            gl::RoomStyle s;
            s.id = r[0]; s.cls = r[1]; s.palette = r[2];
            s.darkShare = F(r[17]); s.lateHours = F(r[22]); s.nightCap = F(r[23]);
            styles[s.id] = s;
        }
        auto text = [](const Table& t, uint32_t at) { return at < t.strings.size() ? std::string(&t.strings[at]) : std::string(); };
        const uint32_t ironforge = gl::PathHash(kIronforgePath);
        std::map<uint32_t, uint32_t> groupClass, groupFlags;
        uint32_t placementFlags = 0;
        int dungeonRows = 0, tempestKeep = 0, tempestDungeon = 0, goldshireDungeon = 0;
        for (const auto& r : bu.rows)
        {
            const std::string wmo = text(bu, r[11]);
            if (r[9] & gl::kBuildingDungeon) ++dungeonRows;
            if (wmo.find("tk_exterior") != std::string::npos) { ++tempestKeep; tempestDungeon += (r[9] & gl::kBuildingDungeon) ? 1 : 0; }
            if (wmo.find("goldshire") != std::string::npos) goldshireDungeon += (r[9] & gl::kBuildingDungeon) ? 1 : 0;
            if (gl::PathHash(wmo.c_str()) != ironforge || r[2] == 0) continue;
            if (r[6] == gl::kWholePlacement) placementFlags = r[9];
            else { groupClass[r[6]] = styles.count(r[7]) ? styles[r[7]].cls : 99; groupFlags[r[6]] = r[9]; }
        }
        // the mapping (WMOAreaTable's sub-zones): the Great Forge's halls and the smithy forge, the ring road, the gates' halls,
        // the High Seat and the Hall of Explorers the Commons, the wards' homes quarters, the Forlorn Cavern a cavern
        struct Want { uint32_t group; uint32_t cls; const char* what; };
        const Want wants[] = {
            { 65, gl::kClassForge, "the Great Forge's hall" }, { 26, gl::kClassForge, "a Great Forge house" }, { 47, gl::kClassForge, "a smithy" },
            { 18, gl::kClassCommons, "the ring road" }, { 8, gl::kClassCommons, "a gate's hall" }, { 89, gl::kClassCommons, "the High Seat" },
            { 12, gl::kClassCommons, "the Hall of Explorers" }, { 15, gl::kClassQuarter, "the Military Ward" }, { 97, gl::kClassQuarter, "the Mystic Ward" },
            { 37, gl::kClassCavern, "the Forlorn Cavern" }, { 80, gl::kClassCavern, "the Forlorn Cavern's houses" },
        };
        int right = 0;
        std::string wrong;
        for (const Want& wnt : wants)
        {
            if (groupClass.count(wnt.group) && groupClass[wnt.group] == wnt.cls) ++right;
            else wrong += std::string(" ") + wnt.what;
        }
        std::map<uint32_t, int> perClass;
        int underground = 0;
        for (const auto& kv : groupClass) perClass[kv.second]++;
        for (const auto& kv : groupFlags) underground += (kv.second & gl::kBuildingUnderground) ? 1 : 0;
        char line[700];
        std::snprintf(line, sizeof(line), "Ironforge's %zu glass groups: forge %d, commons %d, quarter %d, cavern %d (%d of %zu named ones right%s); underground %d "
                      "(not the gate's outside, group 0: %s; not the placement: %s); dungeon rows %d (Tempest Keep's outside %d of %d, Goldshire's %d)",
                      groupClass.size(), perClass[gl::kClassForge], perClass[gl::kClassCommons], perClass[gl::kClassQuarter], perClass[gl::kClassCavern],
                      right, std::size(wants), wrong.c_str(), underground, groupFlags.count(0) && !(groupFlags[0] & gl::kBuildingUnderground) ? "yes" : "NO",
                      !(placementFlags & gl::kBuildingUnderground) ? "yes" : "NO", dungeonRows, tempestDungeon, tempestKeep, goldshireDungeon);
        Check(groupClass.size() > 60 && right == static_cast<int>(std::size(wants)) && perClass[gl::kClassForge] > 5 && perClass[gl::kClassCavern] > 2 &&
              underground == static_cast<int>(groupClass.size()) - 1 && groupFlags.count(0) && !(groupFlags[0] & gl::kBuildingUnderground) &&
              !(placementFlags & gl::kBuildingUnderground) && tempestKeep > 0 && tempestDungeon == tempestKeep && goldshireDungeon == 0, line);

        // Their hours: the share of lit frosted windows dark at 23:00 and 03:00 (at rest, night 1), and a house's for comparison.
        gl::Settings s;
        const gl::RoomToggles t = gl::FrostedToggles(s);
        auto styleOf = [&](uint32_t cls) { for (const auto& kv : styles) if (kv.second.cls == cls && kv.second.palette != 0xFFFF) return kv.second; return gl::RoomStyle{}; };
        const uint32_t classes[4] = { gl::kClassForge, gl::kClassCommons, gl::kClassQuarter, gl::kClassCavern };
        float at23[4], at03[4];
        for (int i = 0; i < 4; ++i)
        {
            const gl::RoomStyle sty = styleOf(classes[i]);
            at23[i] = gl::DarkShare(sty, 0, t, 1.0f, 23.0f);
            at03[i] = gl::DarkShare(sty, 0, t, 1.0f, 3.0f);
        }
        const float house03 = gl::DarkShare(gl::RoomStyle{}, 0, t, 1.0f, 3.0f), house23 = gl::DarkShare(gl::RoomStyle{}, 0, t, 1.0f, 23.0f);
        std::snprintf(line, sizeof(line), "Ironforge's hours (dark at 23:00 / 03:00): the forge %.2f / %.2f, the Commons %.2f / %.2f, the wards' homes %.2f / %.2f, "
                      "the Forlorn Cavern %.2f / %.2f (a house elsewhere %.2f / %.2f)", at23[0], at03[0], at23[1], at03[1], at23[2], at03[2], at23[3], at03[3],
                      house23, house03);
        Check(at23[0] <= 0.04f + 1e-6f && at03[0] <= 0.04f + 1e-6f && at23[1] <= 0.1f && at03[1] <= 0.25f + 1e-6f && at03[2] <= 0.45f + 1e-6f &&
              at03[2] < house03 && at03[0] < at03[1] && at03[1] < at03[2] && at03[2] < at03[3] && at03[3] >= 0.75f && at23[3] > house23, line);

        // The panes: Ironforge's (and the tram's) interior panes have no sky behind them, no other pane is flagged.
        int noSky = 0, ifPanes = 0, otherFlagged = 0;
        for (const auto& r : pa.rows)
        {
            const std::string comment = text(pa, r[25]);
            const bool under = comment.rfind("ironforge:", 0) == 0 || comment.rfind("subway:", 0) == 0;
            ifPanes += comment.rfind("ironforge:", 0) == 0 ? 1 : 0;
            noSky += (r[24] & gl::kPaneNoSky) ? 1 : 0;
            otherFlagged += (r[24] & gl::kPaneNoSky) && !under ? 1 : 0;
        }
        std::snprintf(line, sizeof(line), "GlassPanes.cdbc: %d panes with no sky behind them (Ironforge's %d, the tram's), none elsewhere (%d)", noSky, ifPanes, otherFlagged);
        Check(noSky >= ifPanes && ifPanes > 700 && otherFlagged == 0, line);

        // WindowRooms: the Flags column, frosted cells at the lights of dwarven, orcish and taunka towns, each light's cells at
        // most its glass.
        std::map<uint32_t, float> perLight;
        int frostCells = 0;
        std::set<uint32_t> frostLights;
        for (const auto& r : wr.rows)
        {
            if (wr.fields < 8) break;
            perLight[r[1]] += F(r[5]);
            if (r[7] & 1u) { ++frostCells; frostLights.insert(r[1]); }
        }
        float most = 0;
        for (const auto& kv : perLight) most = std::max(most, kv.second);
        std::snprintf(line, sizeof(line), "WindowRooms.cdbc: %u columns, %zu cells, %d of them lit frosted glass at %zu lights; a light's cells at most %.4f of its glass",
                      wr.fields, wr.rows.size(), frostCells, frostLights.size(), most);
        Check(wr.fields == 8 && frostCells > 500 && frostLights.size() > 100 && most <= 1.0005f, line);
    }

    // On WARP: frosted windows (no room) pixel by pixel against the CPU's rules, and each light's share with its flicker.
    void FrostedAgreementTests()
    {
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("skip  frosted agreement: %s\n", why.c_str()); return; }
        // The probe: r0 = (the flicker's dip, the glow hidden, lit, the lit hash).
        const std::string probe = "\n        mov r0.x, r21.w\n        mov r0.y, r14.y\n        mov r0.z, r20.w\n        mov r0.w, r22.x";
        const std::string text = "ps_3_0\n    def c175, 1, 0, 0.5, 2\n    def c176, 0.2126, 0.7152, 0.0722, 0.2\n" + gl::ShaderDefs() +
                                 "    dcl_texcoord0 v0.xyz\n    dcl_texcoord1 v1.xyz\n    dcl_texcoord2 v2.xy\n    dcl_texcoord3 v3.xyz\n    dcl_2d s0\n"
                                 "    mov r0, c175.y\n    mov r12.xyz, v3\n    mov r31.xyz, c175.y" + gl::GlassBlock("v0", "v1", "v2", probe) +
                                 "\n    mov oC0, r0\n";
        std::vector<uint8_t> code;
        if (!wxl_livingazeroth::shaderpatch::Assemble(text, "frosted probe", code, why)) { Check(false, "the frosted probe assembles: " + why.substr(0, 300)); return; }
        IDirect3DPixelShader9* ps = nullptr;
        constexpr int kS = 96;
        IDirect3DSurface9 *target = nullptr, *readback = nullptr;
        if (FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps)) ||
            FAILED(w.dev->CreateRenderTarget(kS, kS, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr)) ||
            FAILED(w.dev->CreateOffscreenPlainSurface(kS, kS, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &readback, nullptr)))
        { Check(false, "the frosted probe on WARP"); return; }
        UploadLook(w.dev, gl::RoomConstants{});

        struct Place { const char* name; float x, y, z; };
        const Place places[] = { { "the origin", 0, 0, 0 }, { "Kharanos", -5600, -480, 397 }, { "Ironforge", -4900, -950, 501 },
                                 { "the Storm Peaks", 7800, -2600, 1100 } };
        uint32_t rng = 777;
        auto rnd = [&rng]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };
        int windows = 0, cellOff = 0, litOff = 0, nonFinite = 0, repeatOff = 0;
        long long pixels = 0;
        float worstDip = 0, worstHide = 0, worstShare = 0, worstWrap = 0;
        bool drewAll = true;
        std::string worstAt;
        for (const Place& pl : places)
            for (int win = 0; win < 12; ++win)
            {
                const float yaw = rnd() * 6.2831853f, pitch = (rnd() - 0.5f) * 0.3f;
                float N[3] = { std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch) };
                const float hw = 0.75f + rnd() * 1.75f, hh = 0.6f + rnd() * 1.4f, dist = 6.0f + rnd() * 30.0f;
                const float mid[3] = { pl.x + (rnd() - 0.5f) * 60.0f, pl.y + (rnd() - 0.5f) * 60.0f, pl.z + rnd() * 12.0f };
                float a[3] = { -N[1], N[0], 0 }; Norm(a);
                const float u[3] = { N[1] * a[2] - N[2] * a[1], N[2] * a[0] - N[0] * a[2], N[0] * a[1] - N[1] * a[0] };
                const float cam[3] = { mid[0] + N[0] * dist, mid[1] + N[1] * dist, mid[2] + N[2] * dist };
                const float originDist = (win % 3 == 0) ? 0.0f : rnd() * 400.0f, originYaw = rnd() * 6.2831853f;
                const float origin[3] = { mid[0] + std::cos(originYaw) * originDist, mid[1] + std::sin(originYaw) * originDist, mid[2] - rnd() * 20.0f };
                float rel[4][3];
                const float cs[4][2] = { { -hw, hh }, { hw, hh }, { -hw, -hh }, { hw, -hh } };
                for (int k = 0; k < 4; ++k)
                    for (int i = 0; i < 3; ++i)
                        rel[k][i] = static_cast<float>(static_cast<double>(mid[i]) - cam[i] + static_cast<double>(a[i]) * cs[k][0] + static_cast<double>(u[i]) * cs[k][1]);
                {
                    double e1[3], e2[3], nn[3];
                    for (int i = 0; i < 3; ++i) { e1[i] = static_cast<double>(rel[1][i]) - rel[0][i]; e2[i] = static_cast<double>(rel[2][i]) - rel[0][i]; }
                    nn[0] = e1[1] * e2[2] - e1[2] * e2[1]; nn[1] = e1[2] * e2[0] - e1[0] * e2[2]; nn[2] = e1[0] * e2[1] - e1[1] * e2[0];
                    const double len = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
                    const double facing = -(nn[0] * rel[0][0] + nn[1] * rel[0][1] + nn[2] * rel[0][2]) >= 0 ? 1.0 : -1.0;
                    for (int i = 0; i < 3; ++i) N[i] = static_cast<float>(nn[i] / len * facing);
                }
                double poly[4][3];
                const int order[4] = { 0, 1, 3, 2 };
                for (int k = 0; k < 4; ++k)
                    for (int i = 0; i < 3; ++i) poly[k][i] = static_cast<double>(rel[order[k]][i]) + cam[i];
                const float seed[2] = { std::floor(rnd() * 1024.0f), std::floor(rnd() * 1024.0f) };
                gl::DarkBand band;
                switch (win % 6)
                {
                case 0: { const float t = rnd(); band = gl::Band(t, t); break; }
                case 1: { const float t = rnd() * 0.7f; band = gl::Band(t + 0.2f, t); break; }
                case 2: { const float t = rnd() * 0.3f; band = gl::Crossfade(t, t + 0.5f, rnd()); break; }
                case 3: band = gl::Band(1.0f, 1.0f); break;
                case 4: band = gl::Band(0.0f, 0.0f); break;
                default: { const float t = 0.3f + rnd() * 0.4f; band = gl::Band(t, t); break; }
                }
                const float clock = rnd() * gl::hearth::kWrap, amp = 0.05f + rnd() * 0.25f;
                // lit frosted glass, outside: no room (c193.z 0), c201.w 1, the hearth clock and amplitude, the glow a dark one hides
                float c[4 * gl::kConstantCount] = {};
                c[3] = 1; c[5] = 1; c[6] = 0;
                c[24] = 1 / 3.5f; c[25] = 1 / 3.2f; c[26] = 0.25f;
                c[35] = band.hi; c[47] = band.lo;
                c[37] = clock; c[38] = amp; c[39] = 1;
                c[55] = gl::hearth::kFrostedHidden;
                const float sd[4 * gl::kSeedRegisters] = { seed[0], seed[1], 0, band.slope, origin[0], origin[1], origin[2], band.base };
                float view[16] = { cam[0], cam[1], cam[2], 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
                w.dev->SetPixelShaderConstantF(gl::kViewConstant, view, 4);
                w.dev->SetPixelShaderConstantF(gl::kSeedConstant, sd, gl::kSeedRegisters);
                // Three draws: as given, again (the same bits), and the clock a wrap later (no seam).
                std::vector<float> px[3];
                for (int pass = 0; pass < 3; ++pass)
                {
                    struct PV { float x, y, z, rhw, p[3], n[3], uv[2], base[3]; };
                    PV v[4];
                    const float e = kS - 0.5f, xy[4][2] = { { -0.5f, -0.5f }, { e, -0.5f }, { -0.5f, e }, { e, e } };
                    for (int k = 0; k < 4; ++k)
                        v[k] = { xy[k][0], xy[k][1], 0, 1, { rel[k][0], rel[k][1], rel[k][2] }, { N[0], N[1], N[2] }, { 0.5f, 0.5f }, { 0.5f, 0.4f, 0.3f } };
                    c[37] = pass == 2 ? clock + gl::hearth::kWrap : clock;
                    IDirect3DDevice9* d = w.dev;
                    d->SetRenderTarget(0, target);
                    D3DVIEWPORT9 vp{ 0, 0, kS, kS, 0.0f, 1.0f };
                    d->SetViewport(&vp);
                    d->SetRenderState(D3DRS_ZENABLE, FALSE); d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
                    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
                    d->SetVertexShader(nullptr); d->SetPixelShader(ps);
                    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE3(0) | D3DFVF_TEXCOORDSIZE3(1) | D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE3(3));
                    d->SetTexture(0, w.texel);
                    d->SetPixelShaderConstantF(gl::kFirstConstant, c, gl::kConstantCount);
                    const BOOL on = TRUE, off = FALSE;
                    d->SetPixelShaderConstantB(gl::kSwitch, &on, 1);
                    d->SetPixelShaderConstantB(gl::kFloorSwitch, &off, 1);
                    D3DLOCKED_RECT lr{};
                    bool ok = SUCCEEDED(d->BeginScene());
                    if (ok) { d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(PV)); d->EndScene(); }
                    ok = ok && SUCCEEDED(d->GetRenderTargetData(target, readback)) && SUCCEEDED(readback->LockRect(&lr, nullptr, D3DLOCK_READONLY));
                    drewAll = drewAll && ok;
                    if (!ok) break;
                    px[pass].resize(static_cast<size_t>(kS) * kS * 4);
                    for (int row = 0; row < kS; ++row)
                        std::memcpy(&px[pass][static_cast<size_t>(row) * kS * 4], static_cast<const uint8_t*>(lr.pBits) + row * lr.Pitch, sizeof(float) * kS * 4);
                    readback->UnlockRect();
                }
                if (px[2].empty()) continue;
                ++windows;
                double gpuSum = 0;
                for (int row = 0; row < kS; ++row)
                    for (int col = 0; col < kS; ++col)
                    {
                        const size_t at = (static_cast<size_t>(row) * kS + col) * 4;
                        const float* g = &px[0][at];
                        for (int i = 0; i < 4; ++i)
                        {
                            nonFinite += std::isfinite(g[i]) ? 0 : 1;
                            repeatOff += Bits(g[i]) != Bits(px[1][at + i]) ? 1 : 0;
                        }
                        worstWrap = std::max(worstWrap, std::fabs(px[2][at] - g[0]) / amp);
                        // the CPU's cell and hash (a pixel a hair from a cell's edge may land either side: counted, allowed)
                        const float fu = (col + 0.5f) / kS, fv = (row + 0.5f) / kS;
                        float P[3];
                        for (int i = 0; i < 3; ++i) P[i] = rel[0][i] + (rel[1][i] - rel[0][i]) * fu + (rel[2][i] - rel[0][i]) * fv + cam[i];
                        const float inv[2] = { 1 / 3.5f, 1 / 3.2f };
                        float cell[2];
                        gl::RoomCell(P, origin, N, inv, cell);
                        cellOff += Bits(gl::LitHash(cell[0], cell[1], seed[0], seed[1])) != Bits(g[3]) ? 1 : 0;
                        // the lit value, the dip and the glow hidden from the GPU's own hash
                        const float lit = gl::RoomLit(g[3], band);
                        litOff += Bits(lit) != Bits(g[2]) ? 1 : 0;
                        const float dip = gl::HearthDip(g[3], clock, amp);
                        worstDip = std::max(worstDip, std::fabs(dip - g[0]) / amp);
                        const float hide = (1.0f - lit) * gl::hearth::kFrostedHidden + lit * dip;
                        worstHide = std::max(worstHide, std::fabs(hide - g[1]));
                        gpuSum += g[2] * (1.0f - g[0]);
                        ++pixels;
                    }
                // The window light: its cells cut exactly, each lit x (1 - its dip), against the pixels'.
                std::vector<gl::LightCell> cells;
                for (const auto& kv : QuadCells(poly, N, origin))
                {
                    gl::LightCell lc;
                    lc.hash = gl::LitHash(static_cast<float>(kv.first.first), static_cast<float>(kv.first.second), seed[0], seed[1]);
                    lc.share = static_cast<float>(kv.second); lc.band = band; lc.dip = gl::HearthDip(lc.hash, clock, amp);
                    cells.push_back(lc);
                }
                const float cpuShare = gl::LitShare(cells.data(), cells.size()), gpuShare = static_cast<float>(gpuSum / (kS * kS));
                if (std::fabs(cpuShare - gpuShare) > worstShare)
                {
                    worstShare = std::fabs(cpuShare - gpuShare);
                    char at[160];
                    std::snprintf(at, sizeof(at), "%s, window %d: %zu cells, %.3f against %.3f", pl.name, win, cells.size(), cpuShare, gpuShare);
                    worstAt = at;
                }
            }
        char line[700];
        std::snprintf(line, sizeof(line), "WARP, frosted windows (no room), the shader against the window lights' rules: %d windows (the origin, Kharanos, Ironforge, "
                      "the Storm Peaks; at rest, fading, cross-fading, all dark, all lit; the flicker 0.05 .. 0.30), %lld pixels: NaN/Inf %d; the hash off its "
                      "cell at %d (a cell's edge); the lit value's bits differ at %d; the dip off the CPU's by at most %.1e of its amplitude, the glow hidden by "
                      "%.1e; drawn again the same bits (%d differ); a wrap of the clock later the dip off by %.1e of it; the light's share with its flicker against "
                      "the pixels' worst %.4f (%s)", windows, pixels, nonFinite, cellOff, litOff, worstDip, worstHide, repeatOff, worstWrap, worstShare, worstAt.c_str());
        Check(drewAll && windows == 48 && nonFinite == 0 && cellOff < pixels / 1000 && litOff == 0 && worstDip < 2e-3f && worstHide < 1e-3f && repeatOff == 0 &&
              worstWrap < 2e-3f && worstShare < 0.01f, line);
        const float zero[16] = {};
        w.dev->SetPixelShaderConstantF(gl::kViewConstant, zero, 4);
        w.dev->SetPixelShaderConstantF(gl::kSeedConstant, kZeroSeed, gl::kSeedRegisters);
        readback->Release();
        target->Release();
        ps->Release();
    }
}

int main(int argc, char** argv)
{
    bool warp = true;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--no-warp") == 0) warp = false;
        else args.push_back(argv[i]);
    }
    if (args.size() < 2) { std::printf("usage: glass_tests <folder with the WMO ps_MapObj*.bls> <repo data folder> [--no-warp]\n"); return 2; }
    RulesTests();
    RoomRulesTests();
    DayRulesTests();
    LitRulesTests();
    EditTests(args[0]);
    ShaftShaderTests();
    if (warp) WarpTests();
    else std::printf("skip  WARP checks (--no-warp)\n");
    TableTests(args[1]);
    if (warp) NightTests(args[1]);
    if (warp) DayRoomTests(args[1]);
    if (warp) LitAgreementTests();
    FrostedRulesTests();
    FrostedTableTests(args[1]);
    if (warp) FrostedAgreementTests();
    if (const char* preview = std::getenv("GLASS_PREVIEW")) Preview(preview);
    if (const char* preview = std::getenv("ROOM_PREVIEW")) RoomPreview(preview);
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
