// Window glass (env/GlassModel, features/GlassShader, features/GlassShaftShaders, features/ModelIce's WMO edit, the
// shipped tables).
//  1. The rules: the light through glass by time and weather, Schlick's Fresnel, the sun through a pane, the inside or
//     outside decision, the inside colour, the floor light's projection and its leak guards, the fake room's faces.
//     The rooms: lights out by the hour (inns late, shops early), the layouts' thresholds and how often the hash picks
//     each, finding a building (by place and WMO, a city's groups, a WMO's default), a building's room constants
//     (variety, lamp, dark places, empty buildings, cities, everything off), the layouts' geometry (dividing wall and
//     doorway, curtains and gap, planks and things, the detail fading), and a lit room as bright as the client's window.
//  2. The WMO pixel edit with the glass blocks on every stock WMO pixel shader (the client's ps_MapObj*.bls, argv
//     folder): every shadow variant takes them, assembles, and stays within the slots (reported).
//  3. The coloured shafts' shaders compile (slots reported).
//  4. On WARP (the CPU rasterizer behind D3D9On12; no GPU), the blocks drawn: the inside colour, Fresnel outside and
//     the floor light against the rules above; the rooms behind the glass pixel by pixel against RoomOutside (each
//     layout, a city's blocks, lamp-lit at night, a dark place); degenerate rays (along the normal, toward the sun,
//     edge-on, no gradients) give no NaN or Inf. --no-warp skips it (and 6).
//  5. The shipped tables (argv data folder): GlassMaterials.cdbc, GlassPanes.cdbc, RoomStyles.cdbc and
//     GlassBuildings.cdbc read back sane.
//  6. On WARP, Stormwind's leaded windows at night (rooms lit, dark, all shelves) and by day (outside, inside) from 5
//     to 50 yd, head on to grazing: no NaN or Inf, the glass's speckle and shimmer against the stock colour's bounded.
// Run: glass_tests <folder with the WMO ps_MapObj*.bls> <repo data folder> [--no-warp]
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
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
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
                              float dayLight = 1.0f, float seed[4] = nullptr)
    {
        gl::Material m;
        m.kind = gl::kClear;
        gl::Inputs in;
        in.sun[0] = -0.3f; in.sun[1] = 0.2f; in.sun[2] = 0.9f; Norm(in.sun);
        in.daylight = dayLight; in.night = night;
        const gl::Light light = gl::Evaluate(in);
        float c[4 * gl::kConstantCount], sd[4];
        gl::BatchConstants(m, 0.0f, light, in.sun, in.sunColour, in.sky, s, c);
        gl::RoomInputs ri;
        ri.night = night; ri.hour = hour; ri.selfLit = selfLit; ri.fallbackSeed = 12345;
        gl::RoomBatchConstants(style, b, light, ri, s, c, sd);
        gl::RoomConstants k;
        k.share = c[6];
        for (int i = 0; i < 4; ++i)
        {
            k.c198[i] = c[24 + i]; k.c199[i] = c[28 + i]; k.c202[i] = c[40 + i]; k.c203[i] = c[44 + i];
            k.c204[i] = c[48 + i]; k.c205[i] = c[52 + i]; k.c212[i] = sd[i];
            if (seed) seed[i] = sd[i];
        }
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
        Check(Near(day.c199[0], 0.95f, 1e-5f) && Near(day.c202[0], 0.86f, 1e-5f) && Near(day.c202[3], 0.2f, 1e-5f) && Near(day.c203[3], 0.1f, 1e-5f) &&
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
            const gl::RoomConstants k = RoomFor(st, &b, 0, 13, false, s);
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
            gl::RoomConstants k = RoomFor(only(gl::kLayoutShelves), &b, 0, 13, false, s);
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
                k.c203[3] = shares[i];
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
        // The view's rows as the identity: view space = world axes, the camera at the origin (c180..c183).
        float view[16] = { 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
        w.dev->SetPixelShaderConstantF(gl::kViewConstant, view, 4);

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
            struct Case { const char* name; int layout; float night; bool selfLit; uint32_t flags; float r12[3]; };
            const Case cases[] = {
                { "plain, by day", gl::kLayoutPlain, 0, false, 0, { 0.2f, 0.2f, 0.2f } },
                { "shelves, by day", gl::kLayoutShelves, 0, false, 0, { 0.2f, 0.2f, 0.2f } },
                { "curtains, by day", gl::kLayoutCurtains, 0, false, 0, { 0.2f, 0.2f, 0.2f } },
                { "a dividing wall, by day", gl::kLayoutDivider, 0, false, 0, { 0.2f, 0.2f, 0.2f } },
                { "the house's mix, a city's blocks, by day", -1, 0, false, gl::kBuildingCity, { 0.2f, 0.2f, 0.2f } },
                { "the house's mix at 23:00, lamp-lit", -1, 1, true, 0, { 0.62f, 0.47f, 0.28f } },
                { "curtains at 23:00, backlit", gl::kLayoutCurtains, 1, true, 0, { 0.62f, 0.47f, 0.28f } },
                { "a dark place at night", -1, 1, true, gl::kBuildingDarkPlace, { 0.62f, 0.47f, 0.28f } },
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
                    float sd[4];
                    gl::Settings si = s;
                    if (cs.night > 0 && cs.selfLit && !(cs.flags & gl::kBuildingDarkPlace)) si.lightsOut = 0;
                    gl::RoomConstants k = RoomFor(st, &b, cs.night, 23, cs.selfLit, si, cs.night > 0 ? 0.0f : 1.0f, sd);
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
                    c[32] = 1; c[33] = 0.95f; c[34] = 0.85f; c[36] = 1;
                    w.dev->SetPixelShaderConstantF(gl::kSeedConstant, sd, 1);
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
                            for (int i = 0; i < 3; ++i) e = std::max(e, std::fabs(rgb[at + i] - want[i]));
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
            const float zero[4] = {};
            w.dev->SetPixelShaderConstantF(gl::kSeedConstant, zero, 1);
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
                float c[4 * gl::kConstantCount], sd[4];
                gl::BatchConstants(m, 0.0f, light, in.sun, in.sunColour, in.sky, s, c);
                gl::Building b;
                b.seed = 77; b.flags = gl::kBuildingCity;
                gl::RoomInputs ri;
                ri.night = night ? 1.0f : 0.0f; ri.hour = night ? 23.0f : 13.0f; ri.selfLit = true;
                gl::RoomBatchConstants(gl::DefaultStyle(), &b, light, ri, s, c, sd);
                if (night) c[47] = 1.0f;   // every room dark
                c[7] = 0.012f;             // the thickness
                c[21] = 1.0f;              // the glint, even at night: a NaN in it would show
                w.dev->SetPixelShaderConstantF(gl::kSeedConstant, sd, 1);
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
            const float zero[4] = {};
            w.dev->SetPixelShaderConstantF(gl::kSeedConstant, zero, 1);
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
            float c[4 * gl::kConstantCount], sd[4];
            gl::BatchConstants(m, 0.0f, light, in.sun, in.sunColour, in.sky, gl::Settings{}, c);
            gl::RoomInputs ri;
            ri.night = night ? 1.0f : 0.0f; ri.hour = pn.hour; ri.selfLit = pn.lit;
            gl::RoomBatchConstants(st, &b, light, ri, gl::Settings{}, c, sd);
            w.dev->SetPixelShaderConstantF(gl::kSeedConstant, sd, 1);
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
        const float zero[4] = {};
        w.dev->SetPixelShaderConstantF(gl::kSeedConstant, zero, 1);
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
        // The cases: the stock lighting (texel x this: the client's lit window at night, a sunlit wall by day, a dim
        // room inside), rooms all lit (0), all dark (1) or the hour's (-1), a forced layout (-1: the style's); the
        // bounds: speckle and shimmer over the views (mean), speckle at the worst view. Before the fix the dark rooms'
        // were 3.7 and 2.2, worst 4.1 (synthetic; the real texture's 2.4 and 1.3, worst 3.0): the client's glow kept
        // where the mask is below 1, the lead's edges and the glass's darker texels, bright on the darkened glass. The
        // lit rooms' worst is the room itself 5 yd away (its shelves); seen from inside by day the glass is twice as
        // bright as the stock against the same dark lead, so its edges change more.
        struct NightCase { const char* name; bool night; int dark; float inside; int layout; float lit[3]; float speckle, shimmer, worst; };
        const NightCase cases[] = {
            { "night, rooms lit", true, 0, 0, -1, { 1.05f, 1.0f, 0.75f }, 1.4f, 1.2f, 2.5f },
            { "night, rooms dark", true, 1, 0, -1, { 1.05f, 1.0f, 0.75f }, 1.3f, 1.2f, 1.5f },
            { "night, rooms lit, all shelves", true, 0, 0, gl::kLayoutShelves, { 1.05f, 1.0f, 0.75f }, 1.5f, 1.2f, 3.2f },
            { "day, outside", false, -1, 0, -1, { 0.95f, 0.92f, 0.86f }, 1.2f, 1.2f, 1.5f },
            { "day, from inside", false, -1, 1, -1, { 0.5f, 0.48f, 0.45f }, 1.8f, 2.0f, 2.2f },
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
                float c[4 * gl::kConstantCount], seed[4];
                gl::Settings s;
                const gl::Inputs& in = cs.night ? nightIn : dayIn;
                const gl::Light light = gl::Evaluate(in);
                const float* lit = cs.lit;
                gl::BatchConstants(mat, cs.inside, light, in.sun, in.sunColour, in.sky, s, c);
                gl::RoomInputs ri;
                ri.night = cs.night ? 1.0f : 0.0f; ri.hour = cs.night ? 22.67f : 14.0f; ri.selfLit = true;
                gl::RoomStyle st = style;
                if (cs.layout >= 0) for (int k = 0; k < 4; ++k) st.weights[k] = k == cs.layout ? 1.0f : 0.0f;
                gl::RoomBatchConstants(st, &building, light, ri, s, c, seed);
                if (cs.dark >= 0) c[47] = cs.dark ? 1.0f : 0.0f;   // c203.w: every room dark, or every room lit
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
                            std::vector<float> img[5];
                            bool ok = true;
                            for (int pass = 0; pass < 5 && ok; ++pass)
                            {
                                const bool glass = pass == 1 || pass == 3, shifted = pass == 2 || pass == 3;
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
                                w.dev->SetPixelShaderConstantF(gl::kSeedConstant, seed, 1);
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
                            if (sheetFile && height == 0 && (dist == 5 || dist == 20) && (yawDeg == 0 || yawDeg == 60) && &cs != &cases[2] && &cs != &cases[4])
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
                char line[400];
                std::snprintf(line, sizeof(line), "WARP, Stormwind's windows (%s; %s; %d views 5..50 yd, 0..84 deg): NaN/Inf %d; speckle in the glass x %.2f the "
                              "stock's (mean; worst %.2f at %s), shimmer x %.2f (worst %.2f); the glass at %.2f of the stock's light",
                              tx.name.c_str(), cs.name, res.views, res.nonfinite, res.speckle, res.worstSpeckle, res.worstAt.c_str(), res.shimmer,
                              res.worstShimmer, res.lightViews ? res.light / res.lightViews : 0.0);
                Check(res.views == 50 && res.nonfinite == 0 && speckleViews >= 10 && shimmerViews >= 30 && res.speckle <= cs.speckle &&
                      res.shimmer <= cs.shimmer && res.worstSpeckle <= cs.worst, line);
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
        const float zero[4] = {};
        w.dev->SetPixelShaderConstantF(gl::kSeedConstant, zero, 1);
        readback->Release();
        target->Release();
        uvps->Release();
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
    EditTests(args[0]);
    ShaftShaderTests();
    if (warp) WarpTests();
    else std::printf("skip  WARP checks (--no-warp)\n");
    TableTests(args[1]);
    if (warp) NightTests(args[1]);
    if (const char* preview = std::getenv("GLASS_PREVIEW")) Preview(preview);
    if (const char* preview = std::getenv("ROOM_PREVIEW")) RoomPreview(preview);
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
