// Window glass (env/GlassModel, env/GlassRooms, features/GlassShader, features/GlassShaftShaders, features/ModelIce's WMO
// edit, the shipped tables).
//  1. The rules: the light through glass by time and weather, Schlick's Fresnel, the sun through a pane, the inside or
//     outside decision, the inside colour, the floor light's projection and its leak guards. One room per window
//     (env/GlassRooms): the window's frame, its room's box, the window table (a grid per WMO and group; every window found
//     at its own points, none between them or in another group, never more than the loop's bound in a cell, a big city
//     group too), the room's ray (CastRoom: the back wall, the side walls, the floor and the ceiling, where it meets
//     them), the classes' rooms and furniture, the palettes' textures, the furniture atlas, an instance's frame.
//     The rooms: lights out by the hour (inns late, shops early), the curtains' band, finding a building (by place and
//     WMO, a city's groups, a WMO's default), a building's room constants (variety, lamp, dark places, empty buildings,
//     cities, the textures' share of the tint, everything off), rooms dark by the threshold over the windows' keys.
//     1b': the day's look (c85, c86) by kind, at dusk and at night (none), the day's knobs changing no register at night,
//     F0 by day and night, the room at dusk seen by the day's light, DayCool (cooler, more up the pane and at a slant, the
//     luminance kept, a fog's hue bounded). 1b'': the rooms at night and behind frosted and stained glass: the moon's
//     colour; FrostedRooms, StainedRooms and DarkRoomLight off leave every kind's constants as they were (144 batches),
//     on: frosted and stained glass's shares (none in a dungeon), their looks (c26, c27), Ironforge's lamp at noon and no
//     daylight, the moon's light in dark rooms only (c218.zw), none by day.
//  2. The WMO pixel edit with the glass blocks on every stock WMO pixel shader (the client's ps_MapObj*.bls, argv
//     folder): every shadow variant takes them, assembles, and stays within the slots (reported).
//  3. The coloured shafts' shaders compile (slots reported).
//  4. On WARP (the CPU rasterizer behind D3D9On12; no GPU), the blocks drawn: the inside colour, Fresnel outside and
//     the floor light against the rules above, the glass cooler by day against DayCool; the room's ray pixel by pixel
//     against CastRoom (which face, how far, where) from many eyes; degenerate rays (along the normal, toward the sun,
//     edge-on, no gradients) give no NaN or Inf. --no-warp skips it (and 6, 7, 8's WARP parts).
//  5. The shipped tables (argv data folder): GlassMaterials.cdbc, GlassPanes.cdbc, RoomStyles.cdbc,
//     GlassBuildings.cdbc and GlassWindows.cdbc read back sane; the window table built from them (each window found at
//     its middle unless one lower overlapping it at a slant holds it: 98% of the clear, leaded and frosted ones, 99% of
//     clear and leaded, 80% of stained; no cell over the loop's bound; stained windows numbered after the others and in
//     grids of their own); the windows the user named (Ironforge's, Dalaran's, Kharanos', none of a dungeon's stained);
//     WindowRooms.cdbc's keys all windows of their buildings' WMOs, of the cell's kind.
//  6. On WARP, Stormwind's leaded windows at night (rooms lit, dark, all furnished) and by day (outside, inside; outside
//     as clear and as stained glass too) from 5 to 50 yd, head on to grazing, each the window of its own room: no NaN
//     or Inf, the glass's speckle and shimmer against the stock colour's bounded; by day outside the glass keeps at least
//     70% of the stock's light (leaded), 60% (clear), 75% (stained) and is cooler than the stock (leaded, clear; stained
//     barely moved by the cooling); at dusk (19:00) at least 75%, the warmth back. 6b: a facade of six clear windows by
//     day, 5 to 20 yd: no NaN, no shimmer (a third of a pixel's move changes the glass by little), the rooms visible
//     (their contrast in the glass), in depth (the box's faces as a view sees them: head on the back wall, at a slant the
//     side wall, near from below the ceiling, from above the floor), and about room::kDayRoom of the glass's light.
//     6c: at night, a dark room's light about DarkRoomLight of a lit one's (0.08 .. 0.15), readable, cooler; with 0
//     black as before, a lit room's bits the same either way; frosted glass's room there and blurred, stained glass's
//     there and tinted by the glass.
//  7. Lit or dark, shared with the window lights (features/Windows): the exact hash against whole-number arithmetic,
//     the dark band (the clock running, a jump's cross-fade), a batch's band, origin and fallback seed (1c); and on WARP
//     96 windows (the origin, Goldshire, Stormwind, Northrend; at rest, fading, cross-fading, all dark, all lit) of clear,
//     stained (their own grid) and frosted glass (its rooms) pixel by pixel: the window found, its key, the lit hash's
//     and the lit value's bits against the CPU's; a light over two windows and the mullion between them its share
//     against the pixels'.
//  8. Frosted glass (lit by the client, no room): which batches, their constants (the band, the glow hidden, the
//     hearth's flicker; Ironforge always lit; underground no sky), the flicker (env/Flame's rates, its range and mean, a
//     frame's step, no seam at the clock's wrap, each window its own), the hearth clock; the shipped tables (Ironforge's
//     groups by sub-zone and their hours at 23:00 and 03:00, the underground and dungeon flags, the panes with no sky,
//     WindowRooms' frosted windows); and on WARP 48 frosted windows pixel by pixel against the CPU's lit value, dip and
//     glow hidden, and each light's share with its flicker against the pixels'.
//  9. Titan glass lit from behind (Ulduar, the Halls of Stone and of Lightning): the light behind a pane (out tilted 30
//     degrees up), the floor projection along it against the crossing over a floor grid and its leak guards, the pulse
//     (a pure function of the time, periodic, bounded, a gentle step; no period: none), Vezax's glow (a few percent,
//     purple, VezaxPulse; NaN in, finite out), which batches and their constants (the registers the inside look reads the
//     same at noon and at midnight), a pane's light (backlit at any hour, sunlit by day, TitanBacklit off: the sun's), the
//     facing; the shipped tables (the 7 textures, Vezax's pulse, the backlit panes per map, the buildings, none
//     elsewhere); and on WARP the glass and its light on the floor at noon and at midnight: the same pixels, the rules'.
// 10. On WARP, see-through windows' redraw (features/SeeThrough: b11, c219) and the corner that isn't glass (c27.zw): with
//     b11 off and no corner every pixel and its alpha the block's as it was, bit for bit (by day, at night lit and dark,
//     two masks, two stock alphas); in the redraw an open window has no fake room and hides no glow, its alpha 1 - mask x
//     (1 - the opacity), its lead opaque, a shut one exactly as it was and opaque, the bits from the first ordinal, the
//     batch's alpha test kept (texkill); the corner (the forge's fire slots) the client's colour exactly, the rest glass.
//     And (2) every patched variant writes its alpha once, last, from r12.w, with the redraw in it; no stock shader has it.
// Run: glass_tests <folder with the WMO ps_MapObj*.bls> <repo data folder> [--no-warp]
// GLASS_TEXTURES=<folder>: the client's interior BLPs (waterscan --extract-list's names) for the rooms on WARP, else
// synthetic ones. GLASS_ONLY=<parts>: only those (rules, edit, shafts, warp, rays, tables, night, dayrooms, nightrooms,
// lit, frosted, titan, replay).
#include "env/Flame.hpp"
#include "env/GlassModel.hpp"
#include "env/GlassRooms.hpp"
#include "env/SeeThroughModel.hpp"
#include "features/Glass.hpp"
#include "features/GlassShader.hpp"
#include "features/GlassShaftShaders.hpp"
#include "features/ModelIce.hpp"
#include "features/SeeThroughDraw.hpp"
#include "render/ShaderPatch.hpp"
#include "RoomHarness.hpp"

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
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    namespace gl = wxl_livingazeroth::glass;
    namespace rh = roomharness;
    int g_failures = 0;
    void Check(bool ok, const std::string& what)
    {
        std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++g_failures;
    }
    bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
    const float kZeroSeed[4 * gl::kSeedRegisters] = {};
    void Norm(float v[3]) { const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); for (int k = 0; k < 3; ++k) v[k] /= n; }
    uint32_t Bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
    const char* TexturesFolder() { return std::getenv("GLASS_TEXTURES"); }

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

    }

    // --- 1a. one room per window (env/GlassRooms) ---------------------------------------------------------------------
    const uint32_t kFacadePath = 0x12345u, kSecondPath = 0x777u;

    // A facade of windows facing +x at x = 10 (6 across, 3 up: 1.2 x 1.6 yd, 1 yd apart), group 4; a second group's
    // window at the same spot as the first; a skylight; a frosted window.
    std::vector<gl::Window> Facade()
    {
        std::vector<gl::Window> v;
        const float out[3] = { 1, 0, 0 };
        uint32_t ordinal = 0;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 6; ++col)
            {
                const float c[3] = { 10.0f, -12.0f + col * 4.2f, 2.0f + row * 3.6f };
                v.push_back(rh::MakeWindow(kFacadePath, 4, ordinal++, c, out, 1.2f, 1.6f));
            }
        const float first[3] = { 10.0f, -12.0f, 2.0f };
        v.push_back(rh::MakeWindow(kFacadePath, 5, ordinal++, first, out, 1.2f, 1.6f));
        const float up[3] = { 0, 0, 1 }, skyMid[3] = { 4.0f, 3.0f, 14.0f };
        v.push_back(rh::MakeWindow(kFacadePath, 4, ordinal++, skyMid, up, 1.5f, 1.0f));
        const float frostMid[3] = { 10.0f, 15.0f, 2.0f };
        v.push_back(rh::MakeWindow(kFacadePath, 4, ordinal++, frostMid, out, 0.8f, 1.0f, gl::kFrosted));
        return v;
    }

    // The window's key found at a point (WMO space) of a group, -1 none.
    int KeyAt(const gl::WindowTable& t, uint32_t path, uint32_t group, float x, float y, float z)
    {
        const gl::WindowTable::Grid* g = t.Find(path, group);
        if (!g) return -1;
        const float p[3] = { x, y, z };
        const int e = t.Lookup(*g, p);
        return e < 0 ? -1 : static_cast<int>(t.Texel(e + 3)[3]);
    }

    void WindowRulesTests()
    {
        char line[640];
        // The frame: along (-n.y, n.x, 0), x for a skylight; up n x along.
        float a[3], u[3], a2[3], u2[3], a3[3], u3[3];
        const float east[3] = { 1, 0, 0 }, sky[3] = { 0.0f, 0.005f, 0.99999f }, slope[3] = { 0.6f, 0.0f, 0.8f };
        gl::WindowFrame(east, a, u);
        gl::WindowFrame(sky, a2, u2);
        gl::WindowFrame(slope, a3, u3);
        Check(Near(a[1], 1, 1e-6f) && Near(u[2], 1, 1e-6f) && Near(a2[0], 1, 1e-6f) && Near(u2[1], 1, 1e-4f) && Near(a3[1], 1, 1e-6f) &&
              Near(u3[0], -0.8f, 1e-6f) && Near(u3[2], 0.6f, 1e-6f),
              "the window's frame: along (-n.y, n.x, 0) (x for a skylight), up n x along: facing east along +y, up +z; a skylight along +x; "
              "a sloped pane's up along its slope");

        // The room's box: the opening extruded, a little wider than the window, the floor below the sill, never a cupboard.
        const gl::RoomBox little = gl::RoomBoxOf(0.3f, 0.4f), shop = gl::RoomBoxOf(1.3f, 0.8f), tall = gl::RoomBoxOf(0.7f, 3.0f);
        bool boxes = true;
        for (const auto& [hw, hh, b] : { std::tuple{ 0.3f, 0.4f, little }, std::tuple{ 1.3f, 0.8f, shop }, std::tuple{ 0.7f, 3.0f, tall } })
            boxes = boxes && b.halfWidth >= hw + 0.2f && b.halfWidth >= 0.5f * gl::rooms::kMinWidth && b.floor <= -hh - gl::rooms::kSillMin + 1e-6f &&
                    b.ceiling >= hh + gl::rooms::kHeadMin - 1e-6f && b.ceiling - b.floor >= gl::rooms::kMinHeight - 1e-5f;
        std::snprintf(line, sizeof(line), "the room's box: a small window's %.2f wide, %.2f .. %.2f up; a shop window's %.2f wide, %.2f .. %.2f; a tall one's "
                      "%.2f, %.2f .. %.2f (wider than the window, the floor below the sill, at least %.1f x %.1f yd)",
                      2 * little.halfWidth, little.floor, little.ceiling, 2 * shop.halfWidth, shop.floor, shop.ceiling, 2 * tall.halfWidth, tall.floor, tall.ceiling,
                      gl::rooms::kMinWidth, gl::rooms::kMinHeight);
        Check(boxes, line);

        // The table: each window found at its own points (its middle, near its corners), none between them, none in another
        // group's grid, the second group's window at its own spot; the skylight and the frosted window too.
        gl::WindowTable t;
        t.Build(Facade());
        int found = 0, wrong = 0, between = 0, points = 0;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 6; ++col)
            {
                const int want = row * 6 + col;
                const float y = -12.0f + col * 4.2f, z = 2.0f + row * 3.6f;
                for (const auto& [dy, dz] : { std::pair{ 0.0f, 0.0f }, std::pair{ 1.18f, 1.58f }, std::pair{ -1.18f, -1.58f }, std::pair{ 0.5f, -1.0f } })
                    for (float dx : { 0.0f, 0.2f, -0.2f })
                    {
                        const int k = KeyAt(t, kFacadePath, 4, 10.0f + dx, y + dy, z + dz);
                        found += k == want ? 1 : 0; wrong += k >= 0 && k != want ? 1 : 0; ++points;
                    }
                between += KeyAt(t, kFacadePath, 4, 10.0f, y + 2.1f, z) >= 0 ? 1 : 0;      // the wall between windows
                between += KeyAt(t, kFacadePath, 4, 10.6f, y, z) >= 0 ? 1 : 0;             // off the glass's plane
            }
        const int second = KeyAt(t, kFacadePath, 5, 10.0f, -12.0f, 2.0f), noGroup = KeyAt(t, kFacadePath, 6, 10.0f, -12.0f, 2.0f);
        const int skylight = KeyAt(t, kFacadePath, 4, 4.8f, 3.5f, 14.0f), frost = KeyAt(t, kFacadePath, 4, 10.0f, 15.5f, 2.5f);
        const gl::WindowTable::Grid* g4 = t.Find(kFacadePath, 4);
        std::snprintf(line, sizeof(line), "the window table: %d of %d points in the facade's windows find their own (%d another), %d of 36 between them or off "
                      "the glass find one; the second group's window %d (18), a group with none %d, the skylight %d (19), the frosted window %d (20); the "
                      "grid %u x %u x %u cells of %.1f yd, %u windows, at most %u a cell",
                      found, points, wrong, between, second, noGroup, skylight, frost, g4 ? g4->n[0] : 0, g4 ? g4->n[1] : 0, g4 ? g4->n[2] : 0,
                      g4 ? g4->cell : 0.0f, g4 ? g4->windows : 0, g4 ? g4->worst : 0);
        Check(found == points && wrong == 0 && between == 0 && second == 18 && noGroup == -1 && skylight == 19 && frost == 20 &&
              g4 && g4->worst <= static_cast<uint32_t>(gl::rooms::kMaxPerCell), line);

        // A big city group: 1,500 windows in a 300 x 300 x 60 yd box, crowded along streets: each found at its middle, no
        // cell over the loop's bound.
        {
            std::vector<gl::Window> many;
            uint32_t rng = 99;
            auto rnd = [&rng]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };
            for (uint32_t i = 0; i < 1500; ++i)
            {
                const float street = std::floor(rnd() * 12.0f) * 25.0f;
                const bool alongX = rnd() < 0.5f;
                const float c[3] = { alongX ? rnd() * 300.0f : street, alongX ? street : rnd() * 300.0f, rnd() * 60.0f };
                float n[3] = { alongX ? 0.0f : 1.0f, alongX ? 1.0f : 0.0f, 0.0f };
                if (rnd() < 0.5f) for (float& x : n) x = -x;
                many.push_back(rh::MakeWindow(kSecondPath, 2, i, c, n, 0.5f + rnd(), 0.6f + rnd()));
            }
            gl::WindowTable big;
            big.Build(many);
            const gl::WindowTable::Grid* g = big.Find(kSecondPath, 2);
            int self = 0, other = 0;
            for (const gl::Window& w : many)
            {
                const int k = KeyAt(big, kSecondPath, 2, w.center[0], w.center[1], w.center[2]);
                self += k == static_cast<int>(w.ordinal) ? 1 : 0; other += k >= 0 && k != static_cast<int>(w.ordinal) ? 1 : 0;
            }
            std::snprintf(line, sizeof(line), "a big city group: 1500 windows, %d found at their middles (%d where windows overlap find another), cells of %.1f yd, "
                          "at most %u windows a cell (the loop's bound %d), a table of %d x %d texels",
                          self, other, g ? g->cell : 0.0f, g ? g->worst : 0, gl::rooms::kMaxPerCell, gl::rooms::kWidth, big.Height());
            Check(g && g->worst <= static_cast<uint32_t>(gl::rooms::kMaxPerCell) && self + other == 1500 && self >= 1450, line);
        }

        // The ray into the room (CastRoom): straight in the back wall at the room's depth; down the floor, up the ceiling,
        // sideways a side wall; where it meets a face lies on it.
        {
            const float mid[3] = { 10.0f, -12.0f, 2.0f };
            const gl::WindowTable::Grid* g = t.Find(kFacadePath, 4);
            const int e = g ? t.Lookup(*g, mid) : -1;
            const gl::RoomBox box = gl::RoomBoxOf(1.2f, 1.6f);
            auto cast = [&](float vx, float vy, float vz, gl::RoomRay& r) { float v[3] = { vx, vy, vz }; Norm(v); return gl::CastRoom(t, e, mid, v, 4.0f, r); };
            gl::RoomRay straight, down, up, side;
            const bool ok = cast(1, 0, 0, straight) && cast(1, 0, 0.8f, down) && cast(1, 0, -0.8f, up) && cast(1, -0.9f, 0, side);
            int onFace = 0, rays = 0;
            uint32_t rng = 5;
            auto rnd = [&rng]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };
            for (int i = 0; i < 2000; ++i)
            {
                float v[3] = { 0.2f + rnd(), rnd() * 2 - 1, rnd() * 2 - 1 }; Norm(v);
                const float p[3] = { 10.0f, -12.0f + (rnd() * 2 - 1) * 1.1f, 2.0f + (rnd() * 2 - 1) * 1.5f };
                gl::RoomRay r;
                if (!gl::CastRoom(t, t.Lookup(*g, p), p, v, 3.0f + rnd(), r)) continue;
                const float depth = r.face == gl::kBackWall ? r.hit[2] : -1.0f;
                bool on = false;
                switch (r.face)
                {
                case gl::kBackWall: on = depth > 2.9f; break;
                case gl::kSideWall: on = Near(std::fabs(r.hit[0]), box.halfWidth, 1e-3f); break;
                case gl::kFloor: on = Near(r.hit[1], box.floor, 1e-3f); break;
                case gl::kCeiling: on = Near(r.hit[1], box.ceiling, 1e-3f); break;
                }
                onFace += on ? 1 : 0; ++rays;
            }
            std::snprintf(line, sizeof(line), "the room's ray (CastRoom): straight in the back wall at %.2f yd (4), looking down the floor (%d), up the ceiling (%d), "
                          "sideways a side wall (%d); %d of %d random rays meet a face where it is",
                          straight.t, down.face, up.face, side.face, onFace, rays);
            Check(ok && straight.face == gl::kBackWall && Near(straight.t, 4.0f, 1e-4f) && down.face == gl::kFloor && up.face == gl::kCeiling &&
                  side.face == gl::kSideWall && onFace == rays && rays > 1900, line);
        }

        // The classes' rooms and furniture; the palettes' textures.
        bool kinds = true;
        for (uint32_t c = 0; c < gl::kClassCount; ++c)
        {
            const gl::RoomKind& k = gl::RoomKindOf(c);
            kinds = kinds && k.depth >= 2.5f && k.depth <= 6.0f;
            for (int i = 0; i < 4; ++i) kinds = kinds && k.mid[i] < gl::rooms::kTiles && k.back[i] < gl::rooms::kTiles;
        }
        const gl::RoomKind& shopKind = gl::RoomKindOf(gl::kClassShop);
        const gl::RoomKind& innKind = gl::RoomKindOf(gl::kClassInn);
        const gl::RoomKind& keepKind = gl::RoomKindOf(gl::kClassKeep);
        const gl::RoomKind& houseKind = gl::RoomKindOf(gl::kClassHouse);
        auto has = [](const uint8_t* m, int p) { for (int i = 0; i < 4; ++i) if (m[i] == p) return true; return false; };
        Check(kinds && has(shopKind.back, gl::kPropShelves) && has(shopKind.mid, gl::kPropCounter) && has(innKind.mid, gl::kPropRoundTable) &&
              has(innKind.mid, gl::kPropCounter) && has(keepKind.mid, gl::kPropRack) && has(keepKind.back, gl::kPropBanner) &&
              has(houseKind.mid, gl::kPropBed) && has(houseKind.mid, gl::kPropTable) && has(houseKind.back, gl::kPropFireplace),
              "the classes' rooms: 2.5 .. 6 yd deep; a shop's shelves and counter, an inn's tables and bar, a keep's racks and banners, a house's bed, "
              "table and hearth");
        const int human = gl::TextureSetOf(0, gl::kClassHouse), farm = gl::TextureSetOf(0, gl::kClassFarm), dwarf = gl::TextureSetOf(12, gl::kClassHouse),
                  forge = gl::TextureSetOf(0, gl::kClassForge), vrykul = gl::TextureSetOf(8, gl::kClassHouse), goblin = gl::TextureSetOf(9, gl::kClassShop),
                  forsaken = gl::TextureSetOf(10, gl::kClassHouse), draenei = gl::TextureSetOf(7, gl::kClassHouse), elf = gl::TextureSetOf(4, gl::kClassHouse),
                  orc = gl::TextureSetOf(11, gl::kClassHouse), unknown = gl::TextureSetOf(999, gl::kClassHouse);
        std::snprintf(line, sizeof(line), "the palettes' interiors: human %s (%s), a human farm %s, dwarf %s, Ironforge's forge %s, vrykul %s, goblin %s, Forsaken %s, "
                      "draenei %s, elven %s, orc %s, an unknown palette %s",
                      gl::TextureSetAt(human).name, gl::TextureSetAt(human).floor, gl::TextureSetAt(farm).name, gl::TextureSetAt(dwarf).name,
                      gl::TextureSetAt(forge).name, gl::TextureSetAt(vrykul).name, gl::TextureSetAt(goblin).name, gl::TextureSetAt(forsaken).name,
                      gl::TextureSetAt(draenei).name, gl::TextureSetAt(elf).name, gl::TextureSetAt(orc).name, gl::TextureSetAt(unknown).name);
        Check(std::string(gl::TextureSetAt(human).floor).find("mm_strmwnd_int_floor") != std::string::npos && farm != human && dwarf == forge &&
              std::string(gl::TextureSetAt(vrykul).wall).find("vrykul") != std::string::npos && std::string(gl::TextureSetAt(forsaken).wall).find("fskn") != std::string::npos &&
              std::string(gl::TextureSetAt(draenei).floor).find("draenei") != std::string::npos && std::string(gl::TextureSetAt(elf).wall).find("bloodelf") != std::string::npos &&
              std::string(gl::TextureSetAt(goblin).wall).find("bbay") != std::string::npos && orc != human && unknown == human, line);

        // The furniture atlas: every prop has its cut-out (a share of its tile), none reaches its tile's border (no
        // neighbour bleeds in), the empty tile none, the hearth and the candles a glow.
        {
            std::vector<uint32_t> atlas;
            gl::FurnitureAtlas(atlas);
            const int T = gl::rooms::kTileTexels;
            int filled = 0, bordered = 0, glowing = 0;
            float empty = 0;
            for (int tile = 0; tile < gl::rooms::kTiles; ++tile)
            {
                int inside = 0, border = 0, glow = 0;
                for (int j = 0; j < T; ++j)
                    for (int i = 0; i < T; ++i)
                    {
                        const uint32_t texel = atlas[static_cast<size_t>((tile / 8) * T + j) * gl::rooms::kAtlasWidth + (tile % 8) * T + i];
                        const bool in = (texel >> 24) > 128;
                        inside += in ? 1 : 0;
                        border += in && (i < 2 || j < 2 || i >= T - 2 || j >= T - 2) ? 1 : 0;
                        glow += in && ((texel >> 8) & 0xFF) > 212 ? 1 : 0;
                    }
                if (tile == gl::kPropNone) { empty = inside / float(T * T); continue; }
                filled += inside > T * T / 33 ? 1 : 0;
                bordered += border;
                glowing += glow > 0 && (tile == gl::kPropFireplace || tile == gl::kPropCandles || tile == gl::kPropDesk) ? 1 : 0;
            }
            int stuff = -1;
            const float table = gl::PropDistance(gl::kPropTable, 0.0f, 0.8f, &stuff);
            std::snprintf(line, sizeof(line), "the furniture atlas: %d of 15 props cut out, %d texels at a tile's border, the empty tile %.3f, %d of 3 alight "
                          "(the hearth, the candles, the desk's candle); the table's top at 0.8 yd up %.3f yd inside (wood %d)",
                          filled, bordered, empty, glowing, -table, stuff);
            Check(filled == 15 && bordered == 0 && empty == 0 && glowing == 3 && table < -0.01f && stuff == 0, line);
        }

        // An instance's frame (RoomRegisters' c15..c17): its WMO turned 90 degrees about z and moved; a world point (about
        // the camera, the shader's) to its WMO-space point.
        {
            const float axes[9] = { 0, 1, 0, -1, 0, 0, 0, 0, 1 };   // its x along the world's y, its y along -x
            const float origin[3] = { -8913.2f, 554.6f, 93.1f }, cam[3] = { -8900.0f, 560.0f, 99.0f };
            const float local[3] = { 3.5f, -2.0f, 1.25f };
            float world[3];
            for (int k = 0; k < 3; ++k) world[k] = origin[k] + local[0] * axes[k] + local[1] * axes[3 + k] + local[2] * axes[6 + k];
            float regs[4 * gl::kRoomRegisters];
            const float scales[4] = { 2, 2.6f, 2.4f, 1.6f }, gains[4] = { 1, 1, 1, 1 };
            gl::RoomRegisters(axes, origin, cam, nullptr, 1, gl::kClassHouse, scales, gains, gl::Settings{}, regs);
            float back[3];
            for (int i = 0; i < 3; ++i)
                back[i] = regs[4 * i] * (world[0] - cam[0]) + regs[4 * i + 1] * (world[1] - cam[1]) + regs[4 * i + 2] * (world[2] - cam[2]) + regs[4 * i + 3];
            const float notTurned[9] = { 1, 0, 0, 0, 2, 0, 0, 0, 1 };
            std::snprintf(line, sizeof(line), "an instance's frame: a world point about the camera back in its WMO's space (%.4f, %.4f, %.4f) for (3.5, -2, 1.25); "
                          "no grid: c20.w %.0f; a scaled frame isn't a rotation (%s); the house's depth %.1f, furniture %.0f",
                          back[0], back[1], back[2], regs[23], gl::IsRotation(notTurned) ? "is" : "isn't", regs[24], regs[26]);
            Check(Near(back[0], local[0], 2e-3f) && Near(back[1], local[1], 2e-3f) && Near(back[2], local[2], 2e-3f) && regs[23] == 0 &&
                  gl::IsRotation(axes) && !gl::IsRotation(notTurned) && Near(regs[24], gl::RoomKindOf(gl::kClassHouse).depth, 1e-6f) && regs[26] == 1, line);
        }
    }

    // --- 1b. the rooms behind the glass: who lives there ----------------------------------------------------------
    // The batch's constants (c192..c205) and seed (c212, c213) for a style, a building, the night, the hour.
    void RoomFor(const gl::RoomStyle& style, const gl::Building* b, float night, float hour, bool selfLit, const gl::Settings& s,
                 float c[4 * gl::kConstantCount], float sd[4 * gl::kSeedRegisters], float dayLight = 1.0f, uint32_t kind = gl::kClear)
    {
        gl::Material m;
        m.kind = kind;
        gl::Inputs in;
        in.sun[0] = -0.3f; in.sun[1] = 0.2f; in.sun[2] = 0.9f; Norm(in.sun);
        in.daylight = dayLight; in.night = night;
        const gl::Light light = gl::Evaluate(in);
        gl::BatchConstants(m, 0.0f, light, in.sun, in.sunColour, in.sky, s, c);
        gl::RoomInputs ri;
        ri.night = night; ri.hour = hour; ri.selfLit = selfLit; ri.fallbackSeed = 12345;
        ri.kind = kind;
        gl::RoomBatchConstants(style, b, light, ri, s, c, sd);
    }

    void RoomRulesTests()
    {
        char line[640];
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

        // The curtains' band: the layouts' thresholds from the weights (the curtains between the first two), and the room's
        // hash picking them as often as the weights say.
        const gl::RoomStyle house;
        float t[3];
        gl::LayoutThresholds(house.weights, t);
        const float none[4] = { 0, 0, 0, 0 };
        float tz[3];
        gl::LayoutThresholds(none, tz);
        int curtains = 0;
        constexpr int kRooms = 40000;
        double mean[4] = {};
        for (int i = 0; i < kRooms; ++i)
        {
            float h[4];
            gl::Hash42(static_cast<float>(i % 400) + 4321.0f, static_cast<float>(i / 400) + 377.0f, h);
            curtains += h[0] >= t[0] && h[0] < t[1] ? 1 : 0;
            for (int k = 0; k < 4; ++k) mean[k] += h[k] / kRooms;
        }
        std::snprintf(line, sizeof(line), "the curtains' band: a house's %.2f .. %.2f; over %d rooms (windows' keys) %.3f curtained (weight 0.35); the hash's means %.3f %.3f %.3f %.3f",
                      t[0], t[1], kRooms, curtains / double(kRooms), mean[0], mean[1], mean[2], mean[3]);
        bool even = true;
        for (int k = 0; k < 4; ++k) even = even && std::fabs(mean[k] - 0.5) < 0.01;
        Check(Near(t[0], 0.25f, 1e-5f) && Near(t[1], 0.6f, 1e-5f) && tz[0] == 0 && tz[1] == 0 && std::fabs(curtains / double(kRooms) - 0.35) < 0.015 && even, line);

        // The buildings: by where they stand and their WMO, a city's groups, a default per WMO.
        std::vector<gl::Building> rows(5);
        rows[0].map = 0; rows[0].uniqueId = 10047; rows[0].pos[0] = -8913.2f; rows[0].pos[1] = 554.6f; rows[0].pos[2] = 93.1f; rows[0].style = 1;
        rows[0].path = gl::PathHash("World\\wmo\\Azeroth\\Buildings\\Stormwind\\Stormwind.wmo");
        rows[1] = rows[0]; rows[1].group = 93; rows[1].style = 2;
        rows[2] = rows[0]; rows[2].uniqueId = 555; rows[2].path = gl::PathHash("world\\wmo\\x\\nowall.wmo"); rows[2].style = 3;
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

        // The room's constants for a building (RoomTextures off: the tints as the style has them; on: part of the way
        // from their grey, the same luminance).
        gl::Settings s;
        s.roomTextures = 0;
        gl::RoomStyle elf;
        elf.tint[0] = 0.95f; elf.tint[1] = 0.78f; elf.tint[2] = 0.48f; elf.tint2[0] = 0.86f; elf.tint2[1] = 0.5f; elf.tint2[2] = 0.42f;
        elf.lamp[0] = 1.0f; elf.lamp[1] = 0.78f; elf.lamp[2] = 0.38f; elf.spread = 0.2f; elf.darkShare = 0.1f;
        gl::Building b;
        b.seed = (37u << 10) | 911u;
        float day[4 * gl::kConstantCount], daySd[8], nightLit[4 * gl::kConstantCount], nlSd[8], nightPlain[4 * gl::kConstantCount], npSd[8];
        RoomFor(elf, &b, 0, 13, true, s, day, daySd);
        RoomFor(elf, &b, 1, 1, true, s, nightLit, nlSd, 0.0f);
        RoomFor(elf, &b, 1, 1, false, s, nightPlain, npSd, 0.0f);
        gl::Building dark = b; dark.flags = gl::kBuildingDarkPlace;
        gl::Building empty = b; empty.flags = gl::kBuildingEmpty;
        gl::Building city = b; city.flags = gl::kBuildingCity;
        float nightDark[4 * gl::kConstantCount], ndSd[8], dayEmpty[4 * gl::kConstantCount], deSd[8], inCity[4 * gl::kConstantCount], icSd[8];
        RoomFor(elf, &dark, 1, 1, true, s, nightDark, ndSd, 0.0f);
        RoomFor(elf, &empty, 0, 13, true, s, dayEmpty, deSd);
        RoomFor(elf, &city, 0, 13, true, s, inCity, icSd);
        gl::Settings off = s; off.roomVariety = 0; off.roomLayouts = 0; off.litRooms = 0;
        float plainDay[4 * gl::kConstantCount], pdSd[8], plainNight[4 * gl::kConstantCount], pnSd[8];
        RoomFor(elf, &b, 0, 13, true, off, plainDay, pdSd);
        RoomFor(elf, &b, 1, 1, true, off, plainNight, pnSd, 0.0f);
        gl::Settings textured = s; textured.roomTextures = 1;
        float tex[4 * gl::kConstantCount], txSd[8];
        RoomFor(elf, &b, 0, 13, true, textured, tex, txSd);
        auto lum = [](const float* c) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; };
        std::snprintf(line, sizeof(line), "room constants: by day tints %.2f / %.2f, spread %.2f, dark %.2f, no lamp %.2f, curtains %.2f .. %.2f, seed %.0f %.0f; "
                      "at 01:00 lit behind a lit window: lamp %.2f %.2f %.2f, dark %.2f, glow hidden %.2f; not lit there: lamp %.2f; a dark place: dark %.2f, lamp %.2f; "
                      "empty: dark %.2f; a city: block 1/%.0f; variety, layouts and lit rooms off: tints %.2f / %.2f, dark %.2f, curtains %.2f, lamp %.2f, glow "
                      "hidden %.2f; with the textures the tint %.3f (its luminance %.3f, %.3f without)",
                      day[28], day[40], day[43], day[47], day[44], day[48], day[49], daySd[0], daySd[1], nightLit[44], nightLit[45], nightLit[46], nightLit[47],
                      nightLit[55], nightPlain[44], nightDark[47], nightDark[44], dayEmpty[47], icSd[2] > 0 ? 1.0f / icSd[2] : 0.0f, plainDay[28], plainDay[40],
                      plainDay[47], plainDay[49], plainNight[44], plainNight[55], tex[28], lum(tex + 28), lum(day + 28));
        Check(Near(day[28], 0.95f, 1e-5f) && Near(day[40], 0.95f + (0.86f - 0.95f) * gl::room::kDayVariety, 1e-5f) &&
              Near(day[43], 0.2f * gl::room::kDayVariety, 1e-5f) && Near(nightLit[40], 0.86f, 1e-5f) && Near(nightLit[43], 0.2f, 1e-5f) &&
              Near(day[47], 0.1f, 1e-5f) && day[44] == 0 && Near(day[48], 0.25f, 1e-5f) && Near(day[49], 0.6f, 1e-5f) && daySd[0] == 911 && daySd[1] == 37 &&
              daySd[2] == 0 && nightLit[44] > 0.5f && nightLit[46] < nightLit[44] && Near(nightLit[47], gl::LightsOutShare(1, 0, 1), 1e-5f) &&
              Near(nightLit[55], 0.75f, 1e-5f) && nightPlain[44] == 0 && nightDark[47] == 1 && nightDark[44] == 0 && dayEmpty[47] == 1 &&
              Near(icSd[2], 1.0f / gl::room::kBlock, 1e-6f) && Near(plainDay[28], 0.85f, 1e-5f) && Near(plainDay[40], 0.85f, 1e-5f) && plainDay[43] == 0 &&
              plainDay[47] == 0 && plainDay[49] == 0 && plainNight[44] == 0 && plainNight[55] == 0 &&
              Near(lum(tex + 28), lum(day + 28), 1e-4f) && tex[28] < day[28] - 0.05f, line);

        // The share of rooms dark is the threshold's: the windows' keys (ordinals in a group, many seeds) under three shares.
        {
            float frac[3] = {};
            const float shares[3] = { 0.22f, 0.5f, 0.744f };
            for (int i = 0; i < 3; ++i)
            {
                int darkCount = 0, all = 0;
                for (uint32_t seed = 0; seed < 64; ++seed)
                    for (int ordinal = 0; ordinal < 120; ++ordinal)
                    {
                        const float h = gl::LitHash(static_cast<float>(ordinal), static_cast<float>(seed % 7), static_cast<float>(seed * 13 % 1024),
                                                    static_cast<float>(seed * 101 % 1024));
                        darkCount += gl::RoomLit(h, gl::Band(shares[i], shares[i])) < 0.5f ? 1 : 0; ++all;
                    }
                frac[i] = darkCount / static_cast<float>(all);
            }
            std::snprintf(line, sizeof(line), "rooms dark at night by the share (the windows' keys): 0.22 -> %.3f, 0.50 -> %.3f, 0.744 -> %.3f", frac[0], frac[1], frac[2]);
            Check(std::fabs(frac[0] - 0.22f) < 0.03f && std::fabs(frac[1] - 0.5f) < 0.03f && std::fabs(frac[2] - 0.744f) < 0.03f, line);
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
        float duskLook[4];
        gl::LookConstants(ld, duskLook);
        std::snprintf(line, sizeof(line), "dusk (day %.2f, rooms %.2f): darkening %.2f, the room's day light c198.w %.2f, share %.3f (RoomShareDay x kLeadedRoom %.3f), "
                      "its detail's fade over the distance %.2g (the day's and the night's)", ld.day, ld.rooms, duskC[20], duskC[27], duskC[6], s.roomShareDay * gl::kLeadedRoom, duskLook[1]);
        Check(ld.day == 0 && ld.rooms == 1 && duskC[20] == 0 && duskC[27] > 0.5f && Near(duskC[6], s.roomShareDay * gl::kLeadedRoom, 1e-5f) &&
              duskLook[1] < 0.0f, line);

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

    // --- 1c. lit or dark: the exact hash, the dark band (shared with the window lights) ------------------------------

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
        static const std::regex alphaOut(R"(\n[ \t]*\w+ oC0\.w, )");
        int shadow = 0, patched = 0, assembled = 0, withBlocks = 0, maxSlots = 0, maxIceSlots = 0;
        int alphaLast = 0, withReplay = 0, stockReplay = 0;
        std::string worst;
        std::map<std::string, int> reasons;
        for (const Blob& b : blobs)
        {
            std::string src, why;
            if (!sp::Disassemble(b.code.data(), b.code.size(), src, why)) { reasons["disassemble: " + why]++; continue; }
            const bool isShadow = std::regex_search(src, position) && std::regex_search(src, normal);
            shadow += isShadow ? 1 : 0;
            stockReplay += gl::HasReplaySwitch(reinterpret_cast<const uint32_t*>(b.code.data()), b.code.size() / 4) ? 1 : 0;
            std::string ice = src;
            std::string glassSrc = src;
            if (!mi::EditPixelWmoGlassForTests(glassSrc, why)) { reasons[std::string(isShadow ? "SHADOW VARIANT skipped: " : "skipped: ") + why]++; continue; }
            ++patched;
            withBlocks += glassSrc.find("if b9") != std::string::npos && glassSrc.find("if b10") != std::string::npos ? 1 : 0;
            // its alpha written once, last, from r12.w (the stock write moved there, the redraw's change after it)
            const auto writes = std::distance(std::sregex_iterator(glassSrc.begin(), glassSrc.end(), alphaOut), std::sregex_iterator());
            const size_t last = glassSrc.find_last_not_of(" \t\r\n");
            const size_t lineStart = glassSrc.rfind('\n', last);
            alphaLast += writes == 1 && glassSrc.compare(lineStart + 1, std::string::npos, "    mov oC0.w, r12.w\n") == 0 &&
                         glassSrc.find("if b11") != std::string::npos ? 1 : 0;
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
            withReplay += gl::HasReplaySwitch(reinterpret_cast<const uint32_t*>(code.data()), code.size() / 4) ? 1 : 0;
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
        Check(alphaLast == patched && withReplay == assembled && stockReplay == 0,
              "every one writes its alpha once, last, from r12.w after the redraw's (b11) change: " + std::to_string(alphaLast) +
              "; the redraw found in " + std::to_string(withReplay) + " assembled, in " + std::to_string(stockReplay) + " stock shaders");
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

    // The room's detail by the distance (c218) for a light.
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
        UploadLook(w.dev, gl::Light{});

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
            c[24] = c[25] = c[26] = 1.0f; c[27] = 0.3f;
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
            // The room behind clear glass changes it, stays finite and in range (the pane its own window's: the camera at the
            // origin, so the quad's point is the world's).
            rh::Rooms rooms;
            rooms.Textures(w.dev);
            const float wn[3] = { -1, 0, 0 }, wc[3] = { 5, 0, 0 }, origin[3] = { 0, 0, 0 };
            rooms.Table(w.dev, { rh::MakeWindow(kFacadePath, 0, 1, wc, wn, 1.0f, 1.0f) });
            float regs[4 * gl::kRoomRegisters];
            rooms.Registers(origin, kFacadePath, 0, gl::kClassHouse, gl::Settings{}, regs);
            w.dev->SetPixelShaderConstantF(gl::kRoomConstant, regs, gl::kRoomRegisters);
            rooms.Bind(w.dev);
            c[6] = 0.75f;
            quad(p, tilted, base, v);
            std::vector<float> room;
            const bool ok = Draw(w, ps, v, c, gl::kFirstConstant, gl::kConstantCount, true, false, room);
            bool finite = ok;
            for (float x : room) finite = finite && std::isfinite(x) && x >= 0 && x <= 2;
            const float noRooms[4 * gl::kRoomRegisters] = {};
            w.dev->SetPixelShaderConstantF(gl::kRoomConstant, noRooms, gl::kRoomRegisters);
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
            rh::Rooms degenerate;
            degenerate.Textures(w.dev);
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
                    {
                        // the probes' panes their own windows (facing the eye, and a skylight above it)
                        const float cam[3] = { camView[0], camView[1], camView[2] };
                        const float n1[3] = { -1, 0, 0 }, n2[3] = { 0, 0, -1 };
                        const float c1[3] = { cam[0] + 5, cam[1], cam[2] }, c2[3] = { cam[0], cam[1], cam[2] + 5 };
                        degenerate.Table(w.dev, { rh::MakeWindow(kFacadePath, 1, 0, c1, n1, 1, 1), rh::MakeWindow(kFacadePath, 1, 1, c2, n2, 1, 1) });
                        float regs[4 * gl::kRoomRegisters];
                        degenerate.Registers(cam, kFacadePath, 1, gl::kClassInn, s, regs);
                        w.dev->SetPixelShaderConstantF(gl::kRoomConstant, regs, gl::kRoomRegisters);
                        degenerate.Bind(w.dev);
                    }
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
            UploadLook(w.dev, gl::Light{});
            const float noRooms[4 * gl::kRoomRegisters] = {};
            w.dev->SetPixelShaderConstantF(gl::kRoomConstant, noRooms, gl::kRoomRegisters);
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

    // --- 4b. the room's ray on WARP against CastRoom ------------------------------------------------------------------
    // The blocks with a probe right after the ray meets the room: r0 = (t, the face (0 back, 1 side, 2 floor, 3 ceiling),
    // where across, where up).
    std::string RoomProbeShader()
    {
        const std::string probeRoom =
            "\n            mov r0.x, r13.w"
            "\n            mul r0.y, r25.y, c175.x"
            "\n            mad r0.y, r25.z, c175.w, r0.y"
            "\n            add r0.y, r0.y, r25.w"
            "\n            mov r0.zw, r26.xxxy";
        return "ps_3_0\n    def c175, 1, 0, 0.5, 2\n    def c176, 0.2126, 0.7152, 0.0722, 0.2\n" + gl::ShaderDefs() +
               "    dcl_texcoord0 v0.xyz\n    dcl_texcoord1 v1.xyz\n    dcl_texcoord2 v2.xy\n    dcl_texcoord3 v3.xyz\n    dcl_2d s0\n"
               "    mov r0, c175.y\n    mov r0.y, -c175.x\n    mov r12.xyz, v3\n    mov r31.xyz, c175.y" + gl::GlassBlock("v0", "v1", "v2", std::string(), probeRoom) +
               "\n    mov oC0, r0\n";
    }

    // Renders a quad (corners about the camera, screen order) into `px` (rgba floats, kS x kS) with the shader and
    // whatever constants are up; the stock colour `base`.
    bool DrawQuad(IDirect3DDevice9* d, IDirect3DPixelShader9* ps, IDirect3DSurface9* target, IDirect3DSurface9* readback, int kS, const float rel[4][3],
                  const float N[3], const float base[3], std::vector<float>& px, IDirect3DTexture9* texel)
    {
        struct PV { float x, y, z, rhw, p[3], n[3], uv[2], base[3]; };
        PV v[4];
        const float e = kS - 0.5f, xy[4][2] = { { -0.5f, -0.5f }, { e, -0.5f }, { -0.5f, e }, { e, e } };
        for (int k = 0; k < 4; ++k)
            v[k] = { xy[k][0], xy[k][1], 0, 1, { rel[k][0], rel[k][1], rel[k][2] }, { N[0], N[1], N[2] }, { 0.5f, 0.5f }, { base[0], base[1], base[2] } };
        d->SetRenderTarget(0, target);
        D3DVIEWPORT9 vp{ 0, 0, static_cast<DWORD>(kS), static_cast<DWORD>(kS), 0.0f, 1.0f };
        d->SetViewport(&vp);
        d->SetRenderState(D3DRS_ZENABLE, FALSE); d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d->SetVertexShader(nullptr); d->SetPixelShader(ps);
        d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE3(0) | D3DFVF_TEXCOORDSIZE3(1) | D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE3(3));
        d->SetTexture(0, texel);
        const BOOL on = TRUE, off = FALSE;
        d->SetPixelShaderConstantB(gl::kSwitch, &on, 1);
        d->SetPixelShaderConstantB(gl::kFloorSwitch, &off, 1);
        D3DLOCKED_RECT lr{};
        bool ok = SUCCEEDED(d->BeginScene());
        if (ok) { d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(PV)); d->EndScene(); }
        ok = ok && SUCCEEDED(d->GetRenderTargetData(target, readback)) && SUCCEEDED(readback->LockRect(&lr, nullptr, D3DLOCK_READONLY));
        if (!ok) return false;
        px.resize(static_cast<size_t>(kS) * kS * 4);
        for (int row = 0; row < kS; ++row)
            std::memcpy(&px[static_cast<size_t>(row) * kS * 4], static_cast<const uint8_t*>(lr.pBits) + row * lr.Pitch, sizeof(float) * kS * 4);
        readback->UnlockRect();
        return true;
    }

    // A window `w` (its WMO space the world's) as a quad about the camera `cam`: its corners (screen order: top-left,
    // top-right, bottom-left, bottom-right as the eye sees it), and the normal facing the eye.
    void WindowQuad(const gl::Window& w, const float cam[3], float rel[4][3], float N[3], float shrink = 1.0f)
    {
        float a[3], u[3];
        gl::WindowFrame(w.normal, a, u);
        const float toEye = (cam[0] - w.center[0]) * w.normal[0] + (cam[1] - w.center[1]) * w.normal[1] + (cam[2] - w.center[2]) * w.normal[2];
        const float s = toEye >= 0 ? 1.0f : -1.0f;   // seen from behind: mirrored across
        const float cs[4][2] = { { -s, 1 }, { s, 1 }, { -s, -1 }, { s, -1 } };
        for (int k = 0; k < 4; ++k)
            for (int i = 0; i < 3; ++i)
                rel[k][i] = static_cast<float>(static_cast<double>(w.center[i]) - cam[i] + static_cast<double>(a[i]) * cs[k][0] * w.halfWidth * shrink +
                                               static_cast<double>(u[i]) * cs[k][1] * w.halfHeight * shrink);
        for (int i = 0; i < 3; ++i) N[i] = w.normal[i] * s;
    }

    void RoomRayTests()
    {
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("skip  room rays: %s\n", why.c_str()); return; }
        std::vector<uint8_t> code;
        if (!wxl_livingazeroth::shaderpatch::Assemble(RoomProbeShader(), "room probe", code, why)) { Check(false, "the room probe assembles: " + why.substr(0, 300)); return; }
        IDirect3DPixelShader9* ps = nullptr;
        constexpr int kS = 96;
        IDirect3DSurface9 *target = nullptr, *readback = nullptr;
        if (FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps)) ||
            FAILED(w.dev->CreateRenderTarget(kS, kS, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr)) ||
            FAILED(w.dev->CreateOffscreenPlainSurface(kS, kS, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &readback, nullptr)))
        { Check(false, "the room probe on WARP"); return; }
        rh::Rooms rooms;
        rooms.Textures(w.dev);
        // The batch: clear glass outside by day, the room on (its share), a house's class.
        gl::Settings s;
        gl::Material m; m.kind = gl::kClear;
        gl::Inputs in; in.daylight = 1; in.night = 0;
        const gl::Light light = gl::Evaluate(in);
        float c[4 * gl::kConstantCount], sd[4 * gl::kSeedRegisters], look[4], day[8];
        gl::BatchConstants(m, 0.0f, light, in.sun, in.sunColour, in.sky, s, c);
        gl::RoomInputs ri; ri.night = 0; ri.hour = 13; ri.selfLit = true; ri.kind = gl::kClear;
        gl::Building b; b.seed = 4242;
        gl::RoomBatchConstants(gl::DefaultStyle(), &b, light, ri, s, c, sd);
        gl::LookConstants(light, look);
        gl::DayConstants(m, light, s, 0, day);
        w.dev->SetPixelShaderConstantF(gl::kFirstConstant, c, gl::kConstantCount);
        w.dev->SetPixelShaderConstantF(gl::kSeedConstant, sd, gl::kSeedRegisters);
        w.dev->SetPixelShaderConstantF(gl::kLookConstant, look, gl::kLookRegisters);
        w.dev->SetPixelShaderConstantF(gl::kDayConstant, day, gl::kDayRegisters);
        // Windows about the origin and at Stormwind, facing every way (a few tilted, a skylight), eyes 3 .. 25 yd off, at
        // a slant up to 70 degrees; each pixel's ray against CastRoom: the same face (but where two faces are within 0.2%
        // of each other's t), t within 0.1% (and 1 mm), no NaN.
        uint32_t rng = 31337;
        auto rnd = [&rng]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };
        int windows = 0, missing = 0, faceOff = 0, faceEdge = 0, tOff = 0, nonFinite = 0;
        long long pixels = 0;
        int faces[4] = {};
        float worstT = 0;
        bool drewAll = true;
        for (int place = 0; place < 2; ++place)
            for (int k = 0; k < 24; ++k)
            {
                const float base[3] = { place ? -8838.6f : 0.0f, place ? 627.4f : 0.0f, place ? 99.3f : 0.0f };
                const float yaw = rnd() * 6.2831853f, pitch = k == 5 ? 1.5f : (rnd() - 0.5f) * 0.4f;
                float n[3] = { std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch) };
                Norm(n);
                const float mid[3] = { base[0] + (rnd() - 0.5f) * 40, base[1] + (rnd() - 0.5f) * 40, base[2] + rnd() * 10 };
                const gl::Window win = rh::MakeWindow(kFacadePath, 3, static_cast<uint32_t>(k), mid, n, 0.5f + rnd() * 1.5f, 0.5f + rnd() * 1.8f);
                rooms.Table(w.dev, { win });
                // the eye: in front, off its axis
                float a[3], u[3];
                gl::WindowFrame(n, a, u);
                const float dist = 3.0f + rnd() * 22.0f, side = (rnd() - 0.5f) * 3.0f, lift = (rnd() - 0.5f) * 2.0f;
                const float cam[3] = { mid[0] + (n[0] + a[0] * side + u[0] * lift) * dist, mid[1] + (n[1] + a[1] * side + u[1] * lift) * dist,
                                       mid[2] + (n[2] + a[2] * side + u[2] * lift) * dist };
                float view[16] = { cam[0], cam[1], cam[2], 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
                w.dev->SetPixelShaderConstantF(gl::kViewConstant, view, 4);
                float regs[4 * gl::kRoomRegisters];
                rooms.Registers(cam, kFacadePath, 3, gl::kClassHouse, s, regs);
                w.dev->SetPixelShaderConstantF(gl::kRoomConstant, regs, gl::kRoomRegisters);
                rooms.Bind(w.dev);
                float rel[4][3], N[3];
                WindowQuad(win, cam, rel, N, 0.98f);
                std::vector<float> px;
                const float stock[3] = { 0.5f, 0.45f, 0.4f };
                if (!DrawQuad(w.dev, ps, target, readback, kS, rel, N, stock, px, w.texel)) { drewAll = false; continue; }
                ++windows;
                // the room's depth as the shader has it: the class's x (1 + spread (h.w - 0.5)) of the window's key and the seed
                float h[4];
                gl::Hash42(static_cast<float>(win.ordinal) + sd[0], static_cast<float>(win.group) + sd[1], h);
                const float depth = gl::RoomDepthOf(gl::kClassHouse, h[3]);
                const gl::WindowTable::Grid* g = rooms.table.Find(kFacadePath, 3);
                for (int row = 0; row < kS; ++row)
                    for (int col = 0; col < kS; ++col)
                    {
                        const float* q = &px[(static_cast<size_t>(row) * kS + col) * 4];
                        for (int i = 0; i < 4; ++i) nonFinite += std::isfinite(q[i]) ? 0 : 1;
                        ++pixels;
                        if (q[1] < 0) { ++missing; continue; }
                        const float fu = (col + 0.5f) / kS, fv = (row + 0.5f) / kS;
                        float P[3], V[3];
                        for (int i = 0; i < 3; ++i)
                        {
                            const float r = rel[0][i] + (rel[1][i] - rel[0][i]) * fu + (rel[2][i] - rel[0][i]) * fv;
                            P[i] = r + cam[i];
                            V[i] = -r;
                        }
                        Norm(V);
                        gl::RoomRay ray;
                        const int e = g ? rooms.table.Lookup(*g, P) : -1;
                        if (!gl::CastRoom(rooms.table, e, P, V, depth, ray)) { ++missing; continue; }
                        const int face = static_cast<int>(std::lround(q[1]));
                        faces[std::clamp(face, 0, 3)]++;
                        if (face != static_cast<int>(ray.face))
                        {
                            // a pixel where two faces meet: their t's within 0.2%
                            gl::RoomRay alt = ray;
                            const float tGpu = q[0];
                            if (std::fabs(tGpu - ray.t) <= 2e-3f * ray.t + 1e-3f) ++faceEdge; else ++faceOff;
                            (void)alt;
                            continue;
                        }
                        const float dt = std::fabs(q[0] - ray.t);
                        worstT = std::max(worstT, dt / std::max(ray.t, 1.0f));
                        tOff += dt > 1e-3f * ray.t + 1e-3f ? 1 : 0;
                    }
            }
        char line[480];
        std::snprintf(line, sizeof(line), "WARP, the room's ray against CastRoom: %d windows (about the origin and Stormwind, any facing, a skylight; eyes 3 .. 25 yd off "
                      "at a slant), %lld pixels: no window found %d, NaN/Inf %d; the face differs at %d (%d more where two meet), t off by more than 0.1%% "
                      "at %d (worst %.1e); faces seen: back %d, side %d, floor %d, ceiling %d",
                      windows, pixels, missing, nonFinite, faceOff, faceEdge, tOff, worstT, faces[0], faces[1], faces[2], faces[3]);
        Check(drewAll && windows == 48 && missing == 0 && nonFinite == 0 && faceOff == 0 && faceEdge < pixels / 500 && tOff == 0 &&
              faces[0] > 1000 && faces[1] > 1000 && faces[2] > 300 && faces[3] > 300, line);
        const float zero[16] = {};
        w.dev->SetPixelShaderConstantF(gl::kViewConstant, zero, 4);
        w.dev->SetPixelShaderConstantF(gl::kSeedConstant, kZeroSeed, gl::kSeedRegisters);
        readback->Release();
        target->Release();
        ps->Release();
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
        bool sane = m.fields == 24;
        std::map<std::string, uint32_t> byName;
        std::map<std::string, std::pair<float, float>> corner;
        for (const auto& r : m.rows)
        {
            kinds[r[2]]++;
            sane = sane && r[2] >= 1 && r[2] <= 4 && r[1] < m.strings.size();
            if (r[1] < m.strings.size())
            {
                std::string t = &m.strings[r[1]];
                byName[t.substr(t.find_last_of('\\') + 1)] = r[2];
                if (m.fields >= 24) corner[t.substr(t.find_last_of('\\') + 1)] = { F(r[22]), F(r[23]) };
            }
            for (int k = 7; k <= 9; ++k) sane = sane && F(r[k]) >= 0 && F(r[k]) <= 1.0001f;
        }
        // the corner that isn't glass (NotGlassU, NotGlassV): the eight human window atlases' fourth quadrant (a forge's fire
        // slots), nothing else
        int cornered = 0;
        for (const auto& [name, uv] : corner) cornered += uv.first != 1.0f || uv.second != 1.0f ? 1 : 0;
        bool atlases = true;
        for (const char* a : { "mm_elwynn_wnd_ext__01.blp", "mm_elwynn_wnd_int__01.blp", "mm_duskwood_wnd_ext__01.blp", "mm_duskwood_wnd_int__01.blp",
                               "mm_rr_wnd_ext__01.blp", "mm_rr_wnd_int__01.blp", "mm_westfall_wnd_ext__01.blp", "mm_westfall_wnd_int__01.blp" })
            atlases = atlases && corner.count(a) && corner[a].first == 0.5f && corner[a].second == 0.5f;
        Check(cornered == 8 && atlases, "GlassMaterials.cdbc: the corner that isn't glass on the eight human window atlases (0.5, 0.5: the forge's fire slots), " +
              std::to_string(cornered) + " textures with one");
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
        bool unit = p.fields == 27;
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

    // The shipped windows (GlassWindows.cdbc): sane rows, ordinals unique in each WMO; the table the runtime builds from
    // them (every window found at its middle, no cell over the loop's bound); WindowRooms.cdbc's keys all windows of their
    // buildings' WMOs.
    void WindowTableTests(const std::filesystem::path& data)
    {
        Table wt, bu, wr;
        if (!ReadTable(data / "GlassWindows.cdbc", wt) || !ReadTable(data / "GlassBuildings.cdbc", bu) || !ReadTable(data / "WindowRooms.cdbc", wr))
        { Check(false, "the shipped window tables read (GlassWindows, GlassBuildings, WindowRooms)"); return; }
        auto text = [](const Table& t, uint32_t at) { return at < t.strings.size() ? std::string(&t.strings[at]) : std::string(); };
        std::vector<gl::Window> windows;
        std::map<uint32_t, std::set<uint32_t>> ordinals;
        std::map<std::pair<uint32_t, uint64_t>, int> byKey;   // (path, ordinal << 32 | group)
        int kinds[5] = {}, insane = 0, duplicate = 0;
        float thinnest = 1e9f, thickest = 0.0f;
        for (const auto& r : wt.rows)
        {
            if (wt.fields < 17) break;
            gl::Window w;
            w.path = gl::PathHash(text(wt, r[1]).c_str());
            w.group = r[2]; w.ordinal = r[3]; w.kind = r[4];
            for (int k = 0; k < 3; ++k) { w.center[k] = F(r[5 + k]); w.normal[k] = F(r[8 + k]); }
            w.halfWidth = F(r[11]); w.halfHeight = F(r[12]);
            w.thickness = wt.fields > 16 ? F(r[16]) : 0.0f;
            thinnest = std::min(thinnest, w.thickness);
            thickest = std::max(thickest, w.thickness);
            const float nl = std::sqrt(w.normal[0] * w.normal[0] + w.normal[1] * w.normal[1] + w.normal[2] * w.normal[2]);
            const bool ok = w.path && std::fabs(nl - 1) < 1e-3f && w.halfWidth > 0 && w.halfHeight > 0 && w.halfWidth < 20 && w.halfHeight < 20 &&
                            w.thickness >= 0.08f && w.thickness <= 1.0001f &&
                            (w.kind == gl::kClear || w.kind == gl::kLeaded || w.kind == gl::kFrosted || w.kind == gl::kStained);
            insane += ok ? 0 : 1;
            if (w.kind < 5) kinds[w.kind]++;
            duplicate += ordinals[w.path].insert(w.ordinal).second ? 0 : 1;
            byKey[{ w.path, (static_cast<uint64_t>(w.ordinal) << 32) | w.group }] = static_cast<int>(w.kind);
            windows.push_back(w);
        }
        // Stained windows numbered after every other window of their WMO (so the others keep their ordinals: the rooms'
        // keys and the window lights' cells as they were before stained glass had rooms).
        std::map<uint32_t, uint32_t> lastOther, firstStained;
        for (const gl::Window& w : windows)
        {
            if (w.kind == gl::kStained) { auto it = firstStained.find(w.path); if (it == firstStained.end() || w.ordinal < it->second) firstStained[w.path] = w.ordinal; }
            else lastOther[w.path] = std::max(lastOther[w.path], w.ordinal + 1);
        }
        int stainedBefore = 0;
        for (const auto& kv : firstStained) stainedBefore += lastOther.count(kv.first) && kv.second < lastOther[kv.first] ? 1 : 0;
        size_t wmos = ordinals.size();
        bool dense = true;
        for (const auto& kv : ordinals) dense = dense && *kv.second.rbegin() + 1 == kv.second.size();
        gl::WindowTable table;
        table.Build(windows);
        // At its middle a window is found unless another, lower in ordinal, holds that point too (overlapping windows
        // at a slant the generator can't join): the window lights take the same one (gen_window_lights' Windows.at).
        int found = 0, other = 0, none = 0, rooms = 0, roomsFound = 0;
        uint32_t worst = 0, cells = 0;
        float coarsest = 0;
        int stainedFound = 0, stainedInOther = 0;
        for (const gl::Window& w : table.All())
        {
            const bool room = w.kind == gl::kClear || w.kind == gl::kLeaded;
            rooms += room ? 1 : 0;
            // (a stained window is in its group's stained grid alone: never found by another kind's batch)
            if (w.kind == gl::kStained)
                if (const gl::WindowTable::Grid* og = table.Find(w.path, w.group))
                {
                    const int oe = table.Lookup(*og, w.center);
                    stainedInOther += oe >= 0 && static_cast<uint32_t>(table.Texel(oe + 3)[3]) == w.ordinal ? 1 : 0;
                }
            const gl::WindowTable::Grid* g = table.Find(w.path, w.group, w.kind == gl::kStained);
            if (!g) { ++none; continue; }
            worst = std::max(worst, g->worst);
            const int e = table.Lookup(*g, w.center);
            if (e < 0) ++none;
            else if (static_cast<uint32_t>(table.Texel(e + 3)[3]) == w.ordinal) { ++found; roomsFound += room ? 1 : 0; stainedFound += w.kind == gl::kStained ? 1 : 0; }
            else ++other;
        }
        for (const gl::Window& w : table.All())
            if (const gl::WindowTable::Grid* g = table.Find(w.path, w.group, w.kind == gl::kStained)) { cells = std::max(cells, g->n[0] * g->n[1] * g->n[2]); coarsest = std::max(coarsest, g->cell); }
        char line[900];
        std::snprintf(line, sizeof(line), "GlassWindows.cdbc: %zu windows (clear %d, leaded %d, frosted %d, stained %d) in %zu WMOs, %d insane, %d ordinals repeated, "
                      "ordinals 0..n-1 in each: %s (stained after the others in each: %s), the lookup %.2f .. %.2f yd off the plane; the table: %zu groups, %d x %d "
                      "texels (%.1f MB), the most windows in a cell %u (bound %d), the most cells in a grid %u, the coarsest cell %.1f yd; at its middle each "
                      "window found %d (%.1f%%; clear and leaded %d of %d, %.1f%%; stained %d of %d), one lower overlapping it %d, none %d; stained windows a "
                      "clear, leaded or frosted batch would find %d",
                      windows.size(), kinds[gl::kClear], kinds[gl::kLeaded], kinds[gl::kFrosted], kinds[gl::kStained], wmos, insane, duplicate,
                      dense ? "yes" : "NO", stainedBefore ? "NO" : "yes", thinnest, thickest, table.Grids(), gl::rooms::kWidth, table.Height(),
                      table.Height() * gl::rooms::kWidth * 16.0 / (1 << 20), worst, gl::rooms::kMaxPerCell, cells, coarsest, found,
                      100.0 * found / std::max<size_t>(1, windows.size()), roomsFound, rooms, 100.0 * roomsFound / std::max(1, rooms), stainedFound,
                      kinds[gl::kStained], other, none, stainedInOther);
        // (the clear, leaded and frosted windows as before stained glass had rooms: 98% at their middle, 99% of those of clear
        // and leaded glass; stained glass's 84%: Dalaran's and Silvermoon's curved and stacked panes at a slant over a
        // lower-numbered stained window, whose room the pixel takes, as the window lights do)
        const int otherFound = found - stainedFound, others = static_cast<int>(windows.size()) - kinds[gl::kStained];
        Check(wt.fields == 17 && windows.size() > 5000 && insane == 0 && duplicate == 0 && dense && stainedBefore == 0 &&
              worst <= static_cast<uint32_t>(gl::rooms::kMaxPerCell) && none == 0 && otherFound >= others * 98 / 100 &&
              roomsFound >= rooms * 99 / 100 && stainedFound >= kinds[gl::kStained] * 80 / 100 && stainedInOther == 0 && table.Height() <= 4096, line);
        // The windows the user named: Ironforge's and Dalaran's frosted, Dalaran's and Stormwind's (its cathedral's) stained, a
        // dwarven frosted window in Kharanos (the Thunderbrew Distillery), and no dungeon's stained glass (Tempest Keep's).
        {
            std::map<std::string, int> named;
            for (const auto& r : wt.rows)
            {
                const std::string path = text(wt, r[1]);
                const uint32_t kind = r[4];
                if (path.find("ironforge.wmo") != std::string::npos && kind == gl::kFrosted) named["Ironforge frosted"]++;
                if (path.find("nd_dalaran.wmo") != std::string::npos) named[kind == gl::kStained ? "Dalaran stained" : kind == gl::kFrosted ? "Dalaran frosted" : "Dalaran other"]++;
                if (path.find("snow_inn.wmo") != std::string::npos && kind == gl::kFrosted) named["Kharanos frosted"]++;
                if (path.find("stormwind.wmo") != std::string::npos && kind == gl::kStained) named["Stormwind stained"]++;
                if (path.find("\\dungeon\\") != std::string::npos && kind == gl::kStained) named["dungeon stained"]++;
            }
            std::snprintf(line, sizeof(line), "GlassWindows.cdbc: the rooms behind frosted and stained glass: Ironforge %d frosted; Dalaran %d frosted, %d stained; "
                          "Kharanos' distillery %d frosted; Stormwind %d stained; a dungeon's stained %d", named["Ironforge frosted"], named["Dalaran frosted"],
                          named["Dalaran stained"], named["Kharanos frosted"], named["Stormwind stained"], named["dungeon stained"]);
            Check(named["Ironforge frosted"] > 500 && named["Dalaran frosted"] > 500 && named["Dalaran stained"] > 1000 && named["Kharanos frosted"] > 0 &&
                  named["Stormwind stained"] > 100 && named["dungeon stained"] == 0, line);
        }

        // WindowRooms: each row's key is a window of its building's WMO (its GlassBuildings row's path), of the row's kind:
        // a stained cell a stained window's, a frosted cell a frosted one's, a room's clear or leaded (a cell read only
        // without stained glass's rooms is as it was: its stained glass in the frosted or clear window beside it).
        std::map<uint32_t, uint32_t> buildingPath;
        for (const auto& r : bu.rows) buildingPath[r[0]] = gl::PathHash(text(bu, r[11]).c_str());
        int keyed = 0, unknownBuilding = 0, noWindow = 0, wrongKind = 0, stainedCells = 0, unstainedCells = 0;
        for (const auto& r : wr.rows)
        {
            if (wr.fields < 8) break;
            auto b = buildingPath.find(r[2]);
            if (b == buildingPath.end()) { ++unknownBuilding; continue; }
            const uint32_t ordinal = r[3], group = r[4], flags = r[7];
            auto k = byKey.find({ b->second, (static_cast<uint64_t>(ordinal) << 32) | group });
            if (k == byKey.end()) { ++noWindow; continue; }
            ++keyed;
            stainedCells += (flags & 0x2) ? 1 : 0;
            unstainedCells += (flags & 0x4) ? 1 : 0;
            const int want = (flags & 0x2) ? gl::kStained : (flags & 0x1) ? gl::kFrosted : -1;
            wrongKind += want >= 0 ? (k->second != want ? 1 : 0) : (k->second != gl::kClear && k->second != gl::kLeaded ? 1 : 0);
        }
        std::snprintf(line, sizeof(line), "WindowRooms.cdbc: %zu rows, %d keyed to a window of their building's WMO (its ordinal, its group), %d with no such window, "
                      "%d with no building row, %d of another kind than the cell's; %d stained cells, %d read only without stained glass's rooms",
                      wr.rows.size(), keyed, noWindow, unknownBuilding, wrongKind, stainedCells, unstainedCells);
        Check(wr.fields == 8 && keyed > 900 && noWindow == 0 && wrongKind == 0 && stainedCells > 1000, line);
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
        // bright as the stock against the same dark lead, so its edges change more. With one textured 3D room per window
        // (2026-10-18) the rooms' own detail 5 yd away (the walls' textures, the furniture's edges) is most of the worst
        // view's speckle where the room is bright behind the glass: at night lit 2.34 (plain walls 1.16), at dusk 1.62
        // (no darkening: the room weighs more than by day, 1.05); a sparkle raises the shimmer as well, which stays below
        // the stock's (worst 1.14 and 1.07). The dark rooms' 0.99: the lead and the frame hidden as much as the glass.
        // By day seen from outside the glass keeps most of the client's painted window: at least `light` of the stock's
        // light in the glass (leaded 70%, clear 60%, stained 75%: "is all of the glass supposed to look dark in the
        // daytime lol", the user, 2026-10-18; before c83b430, 48% for leaded), the same views; and it no longer reads as
        // lamplit ("they read as lamplit at noon", the user, Theramore at 16:01, 2026-10-18): the glass's mean colour at
        // least `cooler` less warm than the stock's (warmth (R - B) / (R + G + B)); stained glass keeps its colours (the
        // day's cooling moves its warmth by at most `keep`). At dusk (19:00) the warmth is back (within `keep` of the
        // stock's) and the room behind is seen by the day's light: no darker than `light` (before, its rooms were black from
        // 18:00 until the client lit its windows at 20:30).
        // A dark room shows its furniture and corners by the moon's light (DarkRoomLight 0.12, 2026-10-20): its detail 5 yd
        // away is the worst view's speckle as a lit room's is (1.89; its shimmer below the stock's); with DarkRoomLight 0 the
        // dark rooms as before (0.99).
        struct NightCase { const char* name; bool night; int dark; float inside; int layout; float lit[3]; float speckle, shimmer, worst; uint32_t kind = gl::kLeaded;
                           float light = 0; float cooler = 0; float keep = -1; bool dusk = false; float darkRoom = -1; };
        const NightCase cases[] = {
            { "night, rooms lit", true, 0, 0, -1, { 1.05f, 1.0f, 0.75f }, 1.4f, 1.2f, 2.5f },
            { "night, rooms dark", true, 1, 0, -1, { 1.05f, 1.0f, 0.75f }, 1.3f, 1.2f, 2.0f },
            { "night, rooms dark, DarkRoomLight 0", true, 1, 0, -1, { 1.05f, 1.0f, 0.75f }, 1.3f, 1.2f, 1.5f, gl::kLeaded, 0, 0, -1, false, 0.0f },
            { "night, rooms lit, plain walls (RoomTextures 0)", true, 0, 0, -2, { 1.05f, 1.0f, 0.75f }, 1.5f, 1.2f, 3.2f },
            { "day, outside", false, -1, 0, -1, { 0.95f, 0.92f, 0.86f }, 1.2f, 1.2f, 1.5f, gl::kLeaded, 0.70f, 0.08f },
            { "day, from inside", false, -1, 1, -1, { 0.5f, 0.48f, 0.45f }, 1.8f, 2.0f, 2.2f },
            { "day, outside, as clear glass", false, -1, 0, -1, { 0.95f, 0.92f, 0.86f }, 1.2f, 1.2f, 1.5f, gl::kClear, 0.6f, 0.08f },
            { "day, outside, as stained glass", false, -1, 0, -1, { 0.95f, 0.92f, 0.86f }, 1.2f, 1.2f, 1.5f, gl::kStained, 0.75f, 0.0f, 0.04f },
            { "dusk (19:00), outside", false, -1, 0, -1, { 0.62f, 0.5f, 0.45f }, 1.2f, 1.2f, 1.8f, gl::kLeaded, 0.75f, 0.0f, 0.08f, true },
        };
        const float cam0[3] = { -8838.6f, 627.4f, 99.3f };
        const float heading = 0.37f;
        const float dists[] = { 5, 10, 20, 35, 50 }, yaws[] = { 0, 35, 60, 75, 84 }, heights[] = { 0, 3 };
        const float hw = 0.7f, hh = 1.0f;   // the pane: 1.4 x 2 yd

        // the rooms: each pane its own window (its room's textures the style's palette's)
        rh::Rooms rooms;
        rooms.Textures(w.dev, TexturesFolder(), gl::TextureSetOf(style.palette, style.cls));
        std::printf("info  night windows: the rooms' textures %s\n", rooms.source.c_str());
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
                float c[4 * gl::kConstantCount], seed[4 * gl::kSeedRegisters], day[4 * gl::kDayRegisters], look[4];
                gl::Settings s;
                if (cs.darkRoom >= 0) s.darkRoomLight = cs.darkRoom;
                const gl::Inputs& in = cs.night ? nightIn : (cs.dusk ? duskIn : dayIn);
                const gl::Light light = gl::Evaluate(in);
                const float* lit = cs.lit;
                gl::Material cm = mat;
                cm.kind = cs.kind;
                gl::BatchConstants(cm, cs.inside, light, in.sun, in.sunColour, in.sky, s, c);
                gl::RoomInputs ri;
                ri.night = cs.night ? 1.0f : 0.0f; ri.hour = cs.night ? 22.67f : (cs.dusk ? 19.0f : 14.0f); ri.selfLit = true;
                gl::RoomStyle st = style;
                if (cs.layout == -2) s.roomTextures = 0;
                gl::LookConstants(light, look);
                gl::RoomBatchConstants(st, &building, light, ri, s, c, seed, look);
                if (cs.dark >= 0) c[47] = c[35] = cs.dark ? 1.0f : 0.0f;   // c203.w, c200.w: every room dark, or every room lit
                w.dev->SetPixelShaderConstantF(gl::kLookConstant, look, gl::kLookRegisters);
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
                            float A[3], Up[3];
                            gl::WindowFrame(N, A, Up);
                            rooms.Table(w.dev, { rh::MakeWindow(kSecondPath, 2, 7, C, N, hw, hh) });
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
                                {
                                    float regs[4 * gl::kRoomRegisters];
                                    rooms.Registers(cam, kSecondPath, 2, st.cls, s, regs, cs.layout == -2);
                                    w.dev->SetPixelShaderConstantF(gl::kRoomConstant, regs, gl::kRoomRegisters);
                                    rooms.Bind(w.dev, cs.layout == -2);
                                }
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
                            if (sheetFile && height == 0 && (dist == 5 || dist == 20) && (yawDeg == 0 || yawDeg == 60) && (&cs == &cases[0] || &cs == &cases[1] || &cs == &cases[4]))
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

    // --- 6b. by day: rooms you can see, in depth (on WARP) ----------------------------------------------------------------
    // "Rebuild the fake rooms ... as convincing 3D interior-mapped rooms" (the user, 2026-10-18; before, "fake rooms look
    // blocky?" and "only barely visible"). A facade of six clear windows (3 x 2, 1.4 x 1.8 yd, 0.9 yd of wall between them),
    // each the window of its own room, on one of Stormwind's buildings (its city group's style and seed) at 13:00, the
    // runtime's constants, one flat colour for the client's (so only the room and the sky change the glass); seen from 5, 10
    // and 20 yd, head on, at 45 and 70 degrees, level, from below and from above. Per view: no NaN or Inf; no shimmer (the
    // change between two cameras a third of a pixel apart, over the glass's mean, below kShimmer: the edges in the rooms
    // are a pixel's, never sparkling); the rooms visible (with the sky's reflection off their contrast in the glass, (p90 -
    // p10) over the mean, at least kContrast over the views and kContrastLeast in each); in depth (the room probe's faces: at
    // a slant a side wall and the back wall, from below the ceiling, from above the floor, each at least kFaceShare of the
    // glass); and about room::kDayRoom of the glass's light (the room alone over the client's light x kGlassAlbedo x (1 - the
    // darkening), all the views: within 20%).
    void DayRoomTests(const std::filesystem::path& data)
    {
        constexpr float kShimmer = 0.06f, kContrast = 0.25f, kContrastLeast = 0.1f, kFaceShare = 0.03f;
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("skip  rooms by day: %s\n", why.c_str()); return; }
        gl::RoomStyle style;
        gl::Building building;
        float mask[4];
        if (!StormwindBuilding(data, style, building, mask)) { building.seed = 4242; std::printf("info  rooms by day: no Stormwind row, the default house\n"); }
        std::vector<uint8_t> code, probeCode;
        if (!wxl_livingazeroth::shaderpatch::Assemble(BlockShader(), "day rooms", code, why) ||
            !wxl_livingazeroth::shaderpatch::Assemble(RoomProbeShader(), "day room probe", probeCode, why))
        { Check(false, "rooms by day: the shaders assemble: " + why.substr(0, 200)); return; }
        IDirect3DPixelShader9 *ps = nullptr, *probe = nullptr;
        constexpr int kS = 256;
        constexpr float kFocal = 960.0f;
        IDirect3DSurface9 *target = nullptr, *readback = nullptr;
        if (FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps)) ||
            FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(probeCode.data()), &probe)) ||
            FAILED(w.dev->CreateRenderTarget(kS, kS, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr)) ||
            FAILED(w.dev->CreateOffscreenPlainSurface(kS, kS, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &readback, nullptr)))
        { Check(false, "rooms by day: WARP resources"); return; }
        rh::Rooms rooms;
        rooms.Textures(w.dev, TexturesFolder(), gl::TextureSetOf(style.palette, style.cls));
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
        c[47] = c[35] = 0.0f;   // every room lit (a dark one by day keeps kDarkDay of its light: the measure is the lit rooms')
        float day[4 * gl::kDayRegisters], look[4];
        gl::DayConstants(m, light, s, building.flags, day);
        gl::LookConstants(light, look);
        w.dev->SetPixelShaderConstantF(gl::kDayConstant, day, gl::kDayRegisters);
        w.dev->SetPixelShaderConstantF(gl::kLookConstant, look, gl::kLookRegisters);
        // (the room alone: the sky's reflection and glint off, all of the glass the room (share 1), no darkening, no cooling)
        float noSky[4 * gl::kConstantCount], alone[4 * gl::kConstantCount];
        std::memcpy(noSky, c, sizeof(noSky));
        noSky[19] = 0; noSky[21] = 0;
        std::memcpy(alone, noSky, sizeof(alone));
        alone[6] = 1.0f;
        const float lit[3] = { 0.62f, 0.5f, 0.3f };   // the client's painted glass in the sun (the stock colour; the texel w.texel's)
        const float texelLum = 0.2126f * 0.8f + 0.7152f * (77.0f / 255.0f) + 0.0722f * 0.2f;
        const float cam0[3] = { -8838.6f, 627.4f, 99.3f }, heading = 0.37f;
        const float dists[] = { 5, 10, 20 }, yaws[] = { 0, 45, 70 }, heights[] = { 0, 2.5f, -2.5f };
        const float hw = 0.7f, hh = 0.9f, gapX = 2 * hw + 0.9f, gapZ = 2 * hh + 1.2f;
        double contrastSum = 0, roomRatio = 0;
        float worstShimmer = 0, leastContrast = 1e9f;
        int contrastViews = 0, bad = 0, views = 0, ratioViews = 0, depthOk = 0, depthViews = 0;
        float seen[4] = {};
        std::string worstAt, leastAt, depthMiss;
        bool drewAll = true;
        for (float dist : dists)
            for (float yawDeg : yaws)
                for (float height : heights)
                {
                    const float yaw = yawDeg * 3.14159265f / 180.0f;
                    const float C[3] = { cam0[0] + dist * std::cos(heading), cam0[1] + dist * std::sin(heading), cam0[2] + height };
                    const float back[3] = { -std::cos(heading), -std::sin(heading), 0 };
                    const float N[3] = { back[0] * std::cos(yaw) - back[1] * std::sin(yaw), back[0] * std::sin(yaw) + back[1] * std::cos(yaw), 0 };
                    float A[3], U[3];
                    gl::WindowFrame(N, A, U);
                    // the six windows of the facade, each its own room (its WMO space the world's)
                    std::vector<gl::Window> ws;
                    for (int r = 0; r < 2; ++r)
                        for (int k = 0; k < 3; ++k)
                        {
                            const float m3[3] = { C[0] + A[0] * gapX * (k - 1), C[1] + A[1] * gapX * (k - 1), C[2] + gapZ * (r - 0.5f) };
                            ws.push_back(rh::MakeWindow(kFacadePath, 0, static_cast<uint32_t>(40 + r * 3 + k), m3, N, hw, hh));
                        }
                    rooms.Table(w.dev, ws);
                    float F3[3] = { C[0] - cam0[0], C[1] - cam0[1], C[2] - cam0[2] }; Norm(F3);
                    const float fl = std::sqrt(F3[0] * F3[0] + F3[1] * F3[1]);
                    const float R[3] = { F3[1] / fl, -F3[0] / fl, 0 };
                    const float Uv[3] = { R[1] * F3[2] - R[2] * F3[1], R[2] * F3[0] - R[0] * F3[2], R[0] * F3[1] - R[1] * F3[0] };
                    // passes: the look, it a third of a pixel to the right, the look without the sky, the room alone, the probe
                    std::vector<float> img[5];
                    bool ok = true;
                    for (int pass = 0; pass < 5 && ok; ++pass)
                    {
                        const float step = pass == 1 ? dist / kFocal / 3.0f : 0.0f;
                        const float cam[3] = { cam0[0] + R[0] * step, cam0[1] + R[1] * step, cam0[2] + R[2] * step };
                        float regs[4 * gl::kRoomRegisters];
                        rooms.Registers(cam, kFacadePath, 0, style.cls, s, regs);
                        IDirect3DDevice9* d = w.dev;
                        d->SetPixelShaderConstantF(gl::kRoomConstant, regs, gl::kRoomRegisters);
                        rooms.Bind(d);
                        float cv[16] = { cam[0], cam[1], cam[2], 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
                        d->SetPixelShaderConstantF(gl::kViewConstant, cv, 4);
                        d->SetPixelShaderConstantF(gl::kFirstConstant, pass == 2 ? noSky : (pass == 3 ? alone : c), gl::kConstantCount);
                        d->SetPixelShaderConstantF(gl::kSeedConstant, seed, gl::kSeedRegisters);
                        d->SetRenderTarget(0, target);
                        D3DVIEWPORT9 vp{ 0, 0, kS, kS, 0.0f, 1.0f };
                        d->SetViewport(&vp);
                        d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
                        d->SetRenderState(D3DRS_ZENABLE, FALSE); d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
                        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
                        d->SetVertexShader(nullptr); d->SetPixelShader(pass == 4 ? probe : ps);
                        d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE3(0) | D3DFVF_TEXCOORDSIZE3(1) | D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE3(3));
                        d->SetTexture(0, w.texel);
                        const BOOL on = TRUE, off = FALSE;
                        d->SetPixelShaderConstantB(gl::kSwitch, &on, 1);
                        d->SetPixelShaderConstantB(gl::kFloorSwitch, &off, 1);
                        ok = SUCCEEDED(d->BeginScene());
                        if (!ok) break;
                        struct PV { float x, y, z, rhw, p[3], n[3], uv[2], base[3]; };
                        for (const gl::Window& win : ws)
                        {
                            PV v[4];
                            const float corner[4][2] = { { -hw, hh }, { hw, hh }, { -hw, -hh }, { hw, -hh } };
                            for (int k = 0; k < 4; ++k)
                            {
                                float q[3];
                                for (int i = 0; i < 3; ++i) q[i] = win.center[i] + A[i] * corner[k][0] * 0.995f + U[i] * corner[k][1] * 0.995f - cam[i];
                                const float zf = q[0] * F3[0] + q[1] * F3[1] + q[2] * F3[2];
                                const float sx = kS * 0.5f + kFocal * (q[0] * R[0] + q[1] * R[1] + q[2] * R[2]) / zf;
                                const float sy = kS * 0.5f - kFocal * (q[0] * Uv[0] + q[1] * Uv[1] + q[2] * Uv[2]) / zf;
                                v[k] = { sx - 0.5f, sy - 0.5f, 0.5f, 1.0f / zf, { q[0], q[1], q[2] }, { N[0], N[1], N[2] }, { 0.5f, 0.5f }, { lit[0], lit[1], lit[2] } };
                            }
                            d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(PV));
                        }
                        d->EndScene();
                        D3DLOCKED_RECT lr{};
                        ok = SUCCEEDED(d->GetRenderTargetData(target, readback)) && SUCCEEDED(readback->LockRect(&lr, nullptr, D3DLOCK_READONLY));
                        if (!ok) break;
                        img[pass].resize(static_cast<size_t>(kS) * kS * 4);
                        for (int row = 0; row < kS; ++row)
                            std::memcpy(&img[pass][static_cast<size_t>(row) * kS * 4], static_cast<const uint8_t*>(lr.pBits) + row * lr.Pitch, sizeof(float) * kS * 4);
                        readback->UnlockRect();
                    }
                    drewAll = drewAll && ok;
                    if (!ok) continue;
                    ++views;
                    auto L = [&](int i, size_t p) { const float* q = &img[i][p * 4]; return 0.2126f * q[0] + 0.7152f * q[1] + 0.0722f * q[2]; };
                    auto drawn = [&](int i, size_t p) { return img[i][p * 4 + 3] > 0.5f; };
                    char at[96];
                    std::snprintf(at, sizeof(at), "%.0f yd, %.0f deg%s", dist, yawDeg, height > 0 ? ", from below" : (height < 0 ? ", from above" : ""));
                    // NaN; shimmer; contrast without the sky; the room alone's light; the faces
                    double ch = 0, lv = 0, sum = 0, alone = 0;
                    std::vector<float> ls;
                    int faces[4] = {}, glass = 0;
                    for (size_t p = 0; p < static_cast<size_t>(kS) * kS; ++p)
                    {
                        for (int i = 0; i < 4; ++i)
                            for (int k = 0; k < 3; ++k) bad += drawn(i, p) && !std::isfinite(img[i][p * 4 + k]) ? 1 : 0;
                        if (!drawn(0, p) || !drawn(1, p)) continue;
                        ch += std::fabs(L(0, p) - L(1, p)); lv += L(0, p);
                        ls.push_back(L(2, p)); sum += L(2, p); alone += L(3, p);
                        ++glass;
                        const float face = img[4][p * 4 + 1];
                        if (face >= 0) faces[std::clamp(static_cast<int>(std::lround(face)), 0, 3)]++;
                    }
                    if (glass < 300) continue;
                    const float shimmer = static_cast<float>(ch / lv);
                    if (shimmer > worstShimmer) { worstShimmer = shimmer; worstAt = at; }
                    std::sort(ls.begin(), ls.end());
                    const float contrast = static_cast<float>((ls[ls.size() * 9 / 10] - ls[ls.size() / 10]) / (sum / ls.size()));
                    contrastSum += contrast; ++contrastViews;
                    if (contrast < leastContrast) { leastContrast = contrast; leastAt = at; }
                    // the room alone over the glass it stands for: the client's light (its colour over its texel) x
                    // kGlassAlbedo x (1 - the darkening)
                    const float lum12 = 0.2126f * lit[0] + 0.7152f * lit[1] + 0.0722f * lit[2];   // (the blocks' shader: r12 the vertex colour)
                    const float glassRef = lum12 / texelLum * gl::rooms::kGlassAlbedo * (1.0f - c[20]);
                    roomRatio += alone / glass / glassRef; ++ratioViews;
                    // in depth
                    const float share[4] = { faces[0] / float(glass), faces[1] / float(glass), faces[2] / float(glass), faces[3] / float(glass) };
                    // (the room is about the window's size, so what a view sees is the box's: head on and level mostly the
                    // back wall; at a slant the far side wall, which hides the back wall from 45 degrees; from below the
                    // ceiling, from above the floor, near (5 yd) and not at a grazing slant; RoomRayTests checks every face pixel by
                    // pixel against CastRoom)
                    bool deep = true;
                    if (yawDeg == 0 && height == 0) deep = share[0] > 0.5f;
                    if (yawDeg >= 45) deep = deep && share[1] > kFaceShare;
                    if (height > 0 && dist <= 5 && yawDeg <= 45) deep = deep && share[3] > kFaceShare;
                    if (height < 0 && dist <= 5 && yawDeg <= 45) deep = deep && share[2] > kFaceShare;
                    for (int f = 0; f < 4; ++f) seen[f] = std::max(seen[f], share[f]);
                    depthOk += deep ? 1 : 0; ++depthViews;
                    if (!deep)
                    {
                        char miss[160];
                        std::snprintf(miss, sizeof(miss), " [%s: back %.2f side %.2f floor %.2f ceiling %.2f]", at, share[0], share[1], share[2], share[3]);
                        depthMiss += miss;
                    }
                    if (const char* dump = std::getenv("GLASS_DAY_DUMP"))
                    {
                        char name[128];
                        std::snprintf(name, sizeof(name), "day_%02.0f_%02.0f_%+.0f.ppm", dist, yawDeg, height);
                        std::ofstream f(std::filesystem::path(dump) / name, std::ios::binary);
                        f << "P6\n" << kS << " " << kS << "\n255\n";
                        for (size_t p = 0; p < static_cast<size_t>(kS) * kS; ++p)
                            for (int k = 0; k < 3; ++k)
                            {
                                const char bt = static_cast<char>(std::lround(std::clamp(drawn(0, p) ? img[0][p * 4 + k] : 0.1f, 0.0f, 1.0f) * 255.0f));
                                f.write(&bt, 1);
                            }
                    }
                }
        char line[3000];
        const float meanContrast = contrastViews ? static_cast<float>(contrastSum / contrastViews) : 0.0f;
        const float ratio = ratioViews ? static_cast<float>(roomRatio / ratioViews) : 0.0f;
        std::snprintf(line, sizeof(line), "WARP, rooms by day (Stormwind's style, six clear windows, %d views 5..20 yd, 0..70 deg, level, from below and above): NaN/Inf "
                      "%d; shimmer (a third of a pixel's move) at most %.3f of the glass's mean (at %s; bound %.2f); the rooms' contrast without the sky %.3f "
                      "over the views (bound %.2f), the least %.3f (at %s; bound %.2f); the room alone %.2f of the glass it stands for (room::kDayRoom %.2f: "
                      "within 20%%); in depth (the box's faces as a view sees them) in %d of %d views, the most of the glass each face fills in a view: back "
                      "%.2f, side %.2f, floor %.2f, ceiling %.2f%s",
                      views, bad, worstShimmer, worstAt.c_str(), kShimmer, meanContrast, kContrast, leastContrast, leastAt.c_str(), kContrastLeast, ratio,
                      gl::room::kDayRoom, depthOk, depthViews, seen[0], seen[1], seen[2], seen[3], depthMiss.c_str());
        Check(drewAll && views == 27 && bad == 0 && worstShimmer < kShimmer && meanContrast >= kContrast && leastContrast >= kContrastLeast &&
              depthOk == depthViews && std::fabs(ratio / gl::room::kDayRoom - 1.0f) < 0.2f, line);
        const float zero[16] = {};
        w.dev->SetPixelShaderConstantF(gl::kViewConstant, zero, 4);
        w.dev->SetPixelShaderConstantF(gl::kSeedConstant, kZeroSeed, gl::kSeedRegisters);
        readback->Release();
        target->Release();
        probe->Release();
        ps->Release();
    }

    // --- 1b''. the rooms at night, frosted and stained glass's rooms: the constants -------------------------------------
    // "the rooms behind the windows look gorgeous but when lights in the windows are turned off I can't see the rooms at all.
    // It also seems like rooms behind Dalaran and Ironforge windows don't work?" (the user, 2026-10-20). A setting that is
    // off leaves the constants as they were: FrostedRooms and StainedRooms off give frosted and stained glass no room (c193.z
    // 0) and every kind the room's look as it was (c26 (0, 0, 0, kFaceMean), c27 (1, 0, 0, 0): the shader's max, lrp and
    // mad are then exactly its old values); DarkRoomLight 0 leaves c198 and c218.zw exactly the day's (1 - kDarkDay, kDarkDay).
    void NightRoomRulesTests()
    {
        char line[700];
        auto lum = [](const float* c) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; };
        // The moon's colour: cool, luminance 1; the client's warm light moves it part of the way; none, NaN: the default.
        float moon[3], warm[3], nan[3];
        const float none[3] = { 0, 0, 0 }, sodium[3] = { 0.9f, 0.6f, 0.3f }, bad[3] = { std::nanf(""), 1, 1 };
        gl::MoonHue(none, moon); gl::MoonHue(sodium, warm); gl::MoonHue(bad, nan);
        std::snprintf(line, sizeof(line), "the dark rooms' light: the moon's %.2f %.2f %.2f (luminance %.3f), under a warm client light %.2f %.2f %.2f, a NaN light %.2f %.2f %.2f",
                      moon[0], moon[1], moon[2], lum(moon), warm[0], warm[1], warm[2], nan[0], nan[1], nan[2]);
        Check(moon[2] > moon[1] && moon[1] > moon[0] && Near(lum(moon), 1.0f, 1e-4f) && warm[0] > moon[0] && warm[2] < moon[2] && Near(lum(warm), 1.0f, 1e-4f) &&
              Bits(nan[0]) == Bits(moon[0]) && Bits(nan[2]) == Bits(moon[2]), line);

        // Every kind, by day, at dusk and at night, lit by the client or not, in a house, a dungeon, underground: the constants
        // with the new settings off against the same with them on where they don't apply (clear and leaded glass with
        // FrostedRooms and StainedRooms, any glass by day with DarkRoomLight), and the off values themselves.
        gl::Settings off;
        off.frostedRooms = off.stainedRooms = 0;
        off.darkRoomLight = 0.0f;
        gl::Settings on;
        const gl::RoomStyle style;
        int checked = 0, differ = 0, offRoom = 0, offLook = 0, offRegs = 0;
        for (uint32_t kind : { static_cast<uint32_t>(gl::kClear), static_cast<uint32_t>(gl::kLeaded), static_cast<uint32_t>(gl::kFrosted),
                               static_cast<uint32_t>(gl::kStained) })
            for (float night : { 0.0f, 0.4f, 1.0f })
                for (int lit = 0; lit < 4; ++lit)
                    for (uint32_t flags : { 0u, static_cast<uint32_t>(gl::kBuildingDungeon), static_cast<uint32_t>(gl::kBuildingUnderground) })
                    {
                        gl::Inputs in;
                        in.night = night; in.daylight = 1.0f - night; in.sun[2] = night > 0.5f ? -0.3f : 0.6f; Norm(in.sun);
                        in.ambient[0] = 0.1f; in.ambient[1] = 0.12f; in.ambient[2] = 0.2f;
                        const gl::Light light = gl::Evaluate(in);
                        gl::Material m;
                        m.kind = kind;
                        gl::Building b;
                        b.flags = flags; b.seed = 77; b.uniqueId = 5;
                        gl::RoomInputs ri;
                        ri.night = night; ri.hour = 23.0f; ri.kind = kind; ri.selfLit = lit & 1; ri.unlit = (lit & 2) != 0;
                        float c0[4 * gl::kConstantCount], s0[4 * gl::kSeedRegisters], l0[4], r0[4 * gl::kRoomRegisters];
                        float c1[4 * gl::kConstantCount], s1[4 * gl::kSeedRegisters], l1[4], r1[4 * gl::kRoomRegisters];
                        const float axes[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, origin[3] = {}, cam[3] = { 1, 2, 3 }, ones[4] = { 1, 1, 1, 1 };
                        gl::BatchConstants(m, 0.0f, light, in.sun, in.sunColour, in.sky, off, c0);
                        gl::LookConstants(light, l0);
                        gl::RoomBatchConstants(style, &b, light, ri, off, c0, s0, l0);
                        gl::RoomRegisters(axes, origin, cam, nullptr, 1, gl::kClassHouse, ones, ones, off, r0, kind);
                        // the off values: no room behind frosted or stained glass, the room's look and light as they were
                        offRoom += (kind == gl::kFrosted || kind == gl::kStained) && c0[6] != 0.0f ? 1 : 0;
                        offLook += Bits(l0[2]) != Bits(1.0f - gl::room::kDarkDay) || Bits(l0[3]) != Bits(gl::room::kDarkDay) ? 1 : 0;
                        offRegs += r0[44] != 0.0f || r0[45] != 0.0f || r0[46] != 0.0f || r0[48] != 1.0f || r0[49] != 0.0f ? 1 : 0;
                        // where the new settings don't apply, on and off give the same bits
                        gl::Settings same = on;
                        if (kind == gl::kFrosted || kind == gl::kStained) { same.frostedRooms = same.stainedRooms = 0; }
                        if (night > 0.0f) same.darkRoomLight = 0.0f;
                        gl::BatchConstants(m, 0.0f, light, in.sun, in.sunColour, in.sky, same, c1);
                        gl::LookConstants(light, l1);
                        gl::RoomBatchConstants(style, &b, light, ri, same, c1, s1, l1);
                        gl::RoomRegisters(axes, origin, cam, nullptr, 1, gl::kClassHouse, ones, ones, same, r1, kind);
                        const bool equal = std::memcmp(c0, c1, sizeof(c0)) == 0 && std::memcmp(s0, s1, sizeof(s0)) == 0 && std::memcmp(l0, l1, sizeof(l0)) == 0 &&
                                           std::memcmp(r0, r1, sizeof(r0)) == 0;
                        differ += equal ? 0 : 1;
                        ++checked;
                    }
        std::snprintf(line, sizeof(line), "the settings off leave the constants as they were: %d batches (4 kinds x day, dusk, night x lit or not x a house, a "
                      "dungeon, underground): differ from the settings on where those don't apply %d; frosted or stained glass with a room %d; the room's light "
                      "lit and dark not the day's %d; the kind's look not the old room's %d", checked, differ, offRoom, offLook, offRegs);
        Check(checked == 144 && differ == 0 && offRoom == 0 && offLook == 0 && offRegs == 0, line);

        // On: frosted and stained glass's rooms, not a dungeon's; their looks; the moon's light in dark rooms only.
        auto batch = [&](uint32_t kind, uint32_t flags, float night, bool selfLit, bool unlit, const gl::Settings& s, float c[4 * gl::kConstantCount],
                         float look[4], float regs[4 * gl::kRoomRegisters])
        {
            gl::Inputs in;
            in.night = night; in.daylight = 1.0f - night; in.sun[2] = night > 0.5f ? -0.3f : 0.6f; Norm(in.sun);
            const gl::Light light = gl::Evaluate(in);
            gl::Material m;
            m.kind = kind;
            gl::Building b;
            b.flags = flags; b.seed = 77; b.uniqueId = 5;
            gl::RoomInputs ri;
            ri.night = night; ri.hour = 12.0f + 11.0f * night; ri.kind = kind; ri.selfLit = selfLit; ri.unlit = unlit;
            float seed[4 * gl::kSeedRegisters];
            const float axes[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, origin[3] = {}, cam[3] = { 1, 2, 3 }, ones[4] = { 1, 1, 1, 1 };
            gl::BatchConstants(m, 0.0f, light, in.sun, in.sunColour, in.sky, s, c);
            gl::LookConstants(light, look);
            gl::RoomBatchConstants(style, &b, light, ri, s, c, seed, look);
            gl::RoomRegisters(axes, origin, cam, nullptr, 1, gl::kClassHouse, ones, ones, s, regs, kind);
        };
        float c[4 * gl::kConstantCount], look[4], regs[4 * gl::kRoomRegisters];
        batch(gl::kFrosted, 0, 1.0f, true, false, on, c, look, regs);
        const float frostShare = c[6], frostLamp = lum(c + 44);
        const bool frostLook = Near(regs[44], gl::kFrostedBlur, 0) && regs[45] == 1.0f && Near(regs[46], gl::kFrostedFlatten, 0) && regs[48] == 1.0f && regs[49] == 0.0f;
        batch(gl::kFrosted, gl::kBuildingDungeon, 1.0f, true, false, on, c, look, regs);
        const float frostDungeon = c[6];
        batch(gl::kStained, 0, 1.0f, true, false, on, c, look, regs);
        const float stainShare = c[6];
        const bool stainLook = regs[44] == 0.0f && regs[45] == 0.0f && Near(regs[49], gl::kStainedTint, 0) && Near(regs[48], 1.0f - gl::kStainedTint, 1e-7f);
        batch(gl::kStained, gl::kBuildingDungeon, 1.0f, true, false, on, c, look, regs);
        const float stainDungeon = c[6];
        // underground (Ironforge) behind frosted glass at noon: no daylight, the lamp lit, a dark room's ambient warm
        batch(gl::kFrosted, gl::kBuildingUnderground, 0.0f, false, true, on, c, look, regs);
        const float ugLamp = lum(c + 44), ugLight = c[27], ugMoon = gl::MoonRoomLight(1.0f, c + 28, c + 40, on), ugLit = look[2] + look[3], ugDark = look[3],
                    ugWarm = c[24] - c[26];
        // at night: a lamp-lit window's dark rooms take the moon (lit x z + w: 0 lit, 1 dark); a window with no lamp, every room
        batch(gl::kClear, 0, 1.0f, true, false, on, c, look, regs);
        const float nightLit = look[2] + look[3], nightDark = look[3], moonLight = c[27], moonBlue = c[26] - c[24];
        batch(gl::kClear, 0, 1.0f, false, false, on, c, look, regs);
        const float noLampLit = look[2] + look[3], noLampDark = look[3];
        // by day nothing of it: the day's exactly
        batch(gl::kClear, 0, 0.0f, true, false, on, c, look, regs);
        const bool dayLook = Bits(look[2]) == Bits(1.0f - gl::room::kDarkDay) && Bits(look[3]) == Bits(gl::room::kDarkDay);
        std::snprintf(line, sizeof(line), "on: frosted glass's room share %.3f (a dungeon's %.3f), its lamp at night %.2f, its look (blur, frosted windows, flatten) %s; "
                      "stained glass's %.3f (a dungeon's %.3f), its tint %s; underground frosted at noon: the room's light %.3f (the ambient's %.3f: no daylight), the lamp %.2f, lit rooms' light "
                      "%.2f, dark ones' %.2f (warmer: r - b %.2f); at night a lamp-lit window's lit rooms %.2f of the moon, dark %.2f (c198.w %.3f, blue - red "
                      "%.2f); a window with no lamp lit %.2f, dark %.2f; by day the day's look %s",
                      frostShare, frostDungeon, frostLamp, frostLook ? "yes" : "NO", stainShare, stainDungeon, stainLook ? "yes" : "NO", ugLight, ugMoon, ugLamp, ugLit, ugDark,
                      ugWarm, nightLit, nightDark, moonLight, moonBlue, noLampLit, noLampDark, dayLook ? "yes" : "NO");
        Check(Near(frostShare, on.roomShareNight * gl::kFrostedRoom, 1e-6f) && frostDungeon == 0.0f && frostLamp > 0.5f && frostLook &&
              Near(stainShare, on.roomShareNight * gl::kStainedRoom, 1e-6f) && stainDungeon == 0.0f && stainLook && ugMoon > 0.0f && Near(ugLight, ugMoon, 1e-6f) && ugLamp > 0.5f &&
              Near(ugLit, 0.0f, 1e-6f) && Near(ugDark, 1.0f, 1e-6f) && ugWarm > 0.0f && Near(nightLit, 0.0f, 1e-6f) && Near(nightDark, 1.0f, 1e-6f) &&
              moonLight > 0.0f && moonBlue > 0.0f && Near(noLampLit, 1.0f, 1e-6f) && Near(noLampDark, 1.0f, 1e-6f) && dayLook, line);
    }

    // --- 6c. at night: dark rooms not black voids; frosted and stained glass's rooms (on WARP) -----------------------------
    // Six windows of Stormwind's style (its city group's), each its own room, at 23:00 under a night sky, the client's lit
    // window the stock colour (a flat colour: only the room changes the glass), seen from 5, 10 and 20 yd, head on and at
    // 45 degrees, level and from below, the room alone (all of the glass the room, no sky or glint in it):
    //  - every room lit and every room dark, DarkRoomLight 0.12: a dark room's mean light over a lit one's within 0.08 ..
    //    0.15, readable (its contrast, (p90 - p10) over its mean, at least kReadable: the furniture's shapes, the corners
    //    darker), cooler than the lamp-lit rooms (blue over red); with 0 every dark room's pixel black, as before; a lit
    //    room the same bits with 0.12 and 0 (its lamp alone);
    //  - frosted glass (FrostedRooms): its room there behind the glass (the room probe), blurred: the change between
    //    neighbouring pixels over the mean at most kBlur of the clear glass's room's in the same view;
    //  - stained glass (StainedRooms, its windows in their own grid): its room there, tinted: under a blue glass the room's
    //    blue over its red at least 1.5 x the clear glass's room's;
    //  - no NaN or Inf in any of it.
    void NightRoomTests(const std::filesystem::path& data)
    {
        constexpr float kReadable = 0.25f, kBlur = 0.5f;
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("skip  rooms at night: %s\n", why.c_str()); return; }
        gl::RoomStyle style;
        gl::Building building;
        float mask[4];
        if (!StormwindBuilding(data, style, building, mask)) { building.seed = 4242; std::printf("info  rooms at night: no Stormwind row, the default house\n"); }
        std::vector<uint8_t> code, probeCode;
        if (!wxl_livingazeroth::shaderpatch::Assemble(BlockShader(), "night rooms", code, why) ||
            !wxl_livingazeroth::shaderpatch::Assemble(RoomProbeShader(), "night room probe", probeCode, why))
        { Check(false, "rooms at night: the shaders assemble: " + why.substr(0, 200)); return; }
        IDirect3DPixelShader9 *ps = nullptr, *probe = nullptr;
        constexpr int kS = 192;
        constexpr float kFocal = 720.0f;
        IDirect3DSurface9 *target = nullptr, *readback = nullptr;
        if (FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps)) ||
            FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(probeCode.data()), &probe)) ||
            FAILED(w.dev->CreateRenderTarget(kS, kS, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr)) ||
            FAILED(w.dev->CreateOffscreenPlainSurface(kS, kS, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &readback, nullptr)))
        { Check(false, "rooms at night: WARP resources"); return; }
        rh::Rooms rooms;
        rooms.Textures(w.dev, TexturesFolder(), gl::TextureSetOf(style.palette, style.cls));
        gl::Inputs in;
        in.sun[0] = 0.3f; in.sun[1] = -0.2f; in.sun[2] = -0.5f; Norm(in.sun);
        in.daylight = 0; in.night = 1;
        in.sky[0] = 0.04f; in.sky[1] = 0.05f; in.sky[2] = 0.09f;
        in.ambient[0] = 0.1f; in.ambient[1] = 0.13f; in.ambient[2] = 0.24f;
        const gl::Light light = gl::Evaluate(in);
        const float warm[3] = { 0.9f, 0.7f, 0.42f }, blue[3] = { 0.15f, 0.25f, 0.9f };   // the client's lit windows: leaded, a blue stained one
        const float cam0[3] = { -8838.6f, 627.4f, 99.3f }, heading = 0.37f;
        const float hw = 0.7f, hh = 0.9f, gapX = 2 * hw + 0.9f, gapZ = 2 * hh + 1.2f;
        // One draw of the facade: the glass's kind (its windows, its grid, its look), the settings, every room dark or lit, the
        // stock colour; the room alone or the probe. Returns the pixels (rgba).
        auto draw = [&](const float C[3], const float N[3], uint32_t kind, const gl::Settings& s, bool dark, const float stock[3], bool useProbe,
                        std::vector<float>& img) -> bool
        {
            float A[3], U[3];
            gl::WindowFrame(N, A, U);
            std::vector<gl::Window> ws;
            for (int r = 0; r < 2; ++r)
                for (int k = 0; k < 3; ++k)
                {
                    const float m3[3] = { C[0] + A[0] * gapX * (k - 1), C[1] + A[1] * gapX * (k - 1), C[2] + gapZ * (r - 0.5f) };
                    ws.push_back(rh::MakeWindow(kFacadePath, 0, static_cast<uint32_t>(40 + r * 3 + k), m3, N, hw, hh, kind));
                }
            rooms.Table(w.dev, ws);
            gl::Material m;
            m.kind = kind; m.mask[0] = m.mask[1] = m.mask[2] = 0; m.mask[3] = 1;
            float c[4 * gl::kConstantCount], seed[4 * gl::kSeedRegisters], look[4];
            gl::BatchConstants(m, 0.0f, light, in.sun, in.sunColour, in.sky, s, c);
            gl::LookConstants(light, look);
            gl::RoomInputs ri;
            ri.night = 1; ri.hour = 23; ri.selfLit = true; ri.kind = kind;
            gl::RoomBatchConstants(style, &building, light, ri, s, c, seed, look);
            c[47] = c[35] = dark ? 1.0f : 0.0f;          // every room dark, or every room lit (at rest)
            c[6] = c[6] > 0.0f ? 1.0f : 0.0f;            // the room alone (none where the kind has none)
            c[19] = 0; c[21] = 0;                        // no sky, no glint
            float F3[3] = { C[0] - cam0[0], C[1] - cam0[1], C[2] - cam0[2] }; Norm(F3);
            const float fl = std::sqrt(F3[0] * F3[0] + F3[1] * F3[1]);
            const float R[3] = { F3[1] / fl, -F3[0] / fl, 0 };
            const float Uv[3] = { R[1] * F3[2] - R[2] * F3[1], R[2] * F3[0] - R[0] * F3[2], R[0] * F3[1] - R[1] * F3[0] };
            float regs[4 * gl::kRoomRegisters];
            rooms.Registers(cam0, kFacadePath, 0, style.cls, s, regs, false, kind);
            IDirect3DDevice9* d = w.dev;
            d->SetPixelShaderConstantF(gl::kRoomConstant, regs, gl::kRoomRegisters);
            rooms.Bind(d);
            float cv[16] = { cam0[0], cam0[1], cam0[2], 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
            d->SetPixelShaderConstantF(gl::kViewConstant, cv, 4);
            d->SetPixelShaderConstantF(gl::kFirstConstant, c, gl::kConstantCount);
            d->SetPixelShaderConstantF(gl::kSeedConstant, seed, gl::kSeedRegisters);
            d->SetPixelShaderConstantF(gl::kLookConstant, look, gl::kLookRegisters);
            const float noDay[4 * gl::kDayRegisters] = { 0, gl::room::kCoolUp, gl::room::kCoolGraze, gl::room::kCoolTint, 0, 0, 0, 0 };
            d->SetPixelShaderConstantF(gl::kDayConstant, noDay, gl::kDayRegisters);
            d->SetRenderTarget(0, target);
            D3DVIEWPORT9 vp{ 0, 0, kS, kS, 0.0f, 1.0f };
            d->SetViewport(&vp);
            d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
            d->SetRenderState(D3DRS_ZENABLE, FALSE); d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            d->SetVertexShader(nullptr); d->SetPixelShader(useProbe ? probe : ps);
            d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE3(0) | D3DFVF_TEXCOORDSIZE3(1) | D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE3(3));
            d->SetTexture(0, w.texel);
            const BOOL on = TRUE, off = FALSE;
            d->SetPixelShaderConstantB(gl::kSwitch, &on, 1);
            d->SetPixelShaderConstantB(gl::kFloorSwitch, &off, 1);
            if (FAILED(d->BeginScene())) return false;
            struct PV { float x, y, z, rhw, p[3], n[3], uv[2], base[3]; };
            for (const gl::Window& win : ws)
            {
                PV v[4];
                const float corner[4][2] = { { -hw, hh }, { hw, hh }, { -hw, -hh }, { hw, -hh } };
                for (int k = 0; k < 4; ++k)
                {
                    float q[3];
                    for (int i = 0; i < 3; ++i) q[i] = win.center[i] + A[i] * corner[k][0] * 0.995f + U[i] * corner[k][1] * 0.995f - cam0[i];
                    const float zf = q[0] * F3[0] + q[1] * F3[1] + q[2] * F3[2];
                    const float sx = kS * 0.5f + kFocal * (q[0] * R[0] + q[1] * R[1] + q[2] * R[2]) / zf;
                    const float sy = kS * 0.5f - kFocal * (q[0] * Uv[0] + q[1] * Uv[1] + q[2] * Uv[2]) / zf;
                    v[k] = { sx - 0.5f, sy - 0.5f, 0.5f, 1.0f / zf, { q[0], q[1], q[2] }, { N[0], N[1], N[2] }, { 0.5f, 0.5f }, { stock[0], stock[1], stock[2] } };
                }
                d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(PV));
            }
            d->EndScene();
            D3DLOCKED_RECT lr{};
            if (FAILED(d->GetRenderTargetData(target, readback)) || FAILED(readback->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
            img.resize(static_cast<size_t>(kS) * kS * 4);
            for (int row = 0; row < kS; ++row)
                std::memcpy(&img[static_cast<size_t>(row) * kS * 4], static_cast<const uint8_t*>(lr.pBits) + row * lr.Pitch, sizeof(float) * kS * 4);
            readback->UnlockRect();
            return true;
        };
        gl::Settings s, none = s;
        none.darkRoomLight = 0.0f;
        auto L = [](const std::vector<float>& im, size_t p) { const float* q = &im[p * 4]; return 0.2126f * q[0] + 0.7152f * q[1] + 0.0722f * q[2]; };
        auto drawn = [](const std::vector<float>& im, size_t p) { return im[p * 4 + 3] > 0.5f; };
        double litSum = 0, darkSum = 0, darkBlue = 0, darkRed = 0, litBlue = 0, litRed = 0;
        long long glassPx = 0;
        int bad = 0, views = 0, litChanged = 0, darkNotBlack = 0, frostThere = 0, frostViews = 0, stainThere = 0, blurViews = 0;
        float leastReadable = 1e9f, worstBlur = 0, leastTint = 1e9f;
        std::string leastAt, blurAt, tintAt;
        bool drewAll = true;
        for (float dist : { 5.0f, 10.0f, 20.0f })
            for (float yawDeg : { 0.0f, 45.0f })
                for (float height : { 0.0f, 2.5f })
                {
                    const float yaw = yawDeg * 3.14159265f / 180.0f;
                    const float C[3] = { cam0[0] + dist * std::cos(heading), cam0[1] + dist * std::sin(heading), cam0[2] + height };
                    const float back[3] = { -std::cos(heading), -std::sin(heading), 0 };
                    const float N[3] = { back[0] * std::cos(yaw) - back[1] * std::sin(yaw), back[0] * std::sin(yaw) + back[1] * std::cos(yaw), 0 };
                    char at[96];
                    std::snprintf(at, sizeof(at), "%.0f yd, %.0f deg%s", dist, yawDeg, height > 0 ? ", from below" : "");
                    std::vector<float> lit, dark, lit0, dark0, frost, frostProbe, stainClear, stain, stainProbe;
                    bool ok = draw(C, N, gl::kClear, s, false, warm, false, lit) && draw(C, N, gl::kClear, s, true, warm, false, dark) &&
                              draw(C, N, gl::kClear, none, false, warm, false, lit0) && draw(C, N, gl::kClear, none, true, warm, false, dark0) &&
                              draw(C, N, gl::kFrosted, s, false, warm, false, frost) && draw(C, N, gl::kFrosted, s, false, warm, true, frostProbe) &&
                              draw(C, N, gl::kClear, s, false, blue, false, stainClear) && draw(C, N, gl::kStained, s, false, blue, false, stain) &&
                              draw(C, N, gl::kStained, s, false, blue, true, stainProbe);
                    drewAll = drewAll && ok;
                    if (!ok) continue;
                    ++views;
                    double vl = 0, vd = 0;
                    std::vector<float> dl;
                    int n = 0, frostRoom = 0, stainRoom = 0;
                    double clearStep = 0, frostStep = 0, clearMean = 0, frostMean = 0;
                    double sb = 0, sr = 0, cb = 0, cr = 0;
                    for (size_t p = 0; p < static_cast<size_t>(kS) * kS; ++p)
                    {
                        for (const std::vector<float>* im : { &lit, &dark, &lit0, &dark0, &frost, &stainClear, &stain })
                            for (int k = 0; k < 3; ++k) bad += drawn(*im, p) && !std::isfinite((*im)[p * 4 + k]) ? 1 : 0;
                        if (!drawn(lit, p)) continue;
                        ++n;
                        vl += L(lit, p); vd += L(dark, p); dl.push_back(L(dark, p));
                        darkBlue += dark[p * 4 + 2]; darkRed += dark[p * 4]; litBlue += lit[p * 4 + 2]; litRed += lit[p * 4];
                        for (int k = 0; k < 3; ++k) litChanged += Bits(lit[p * 4 + k]) != Bits(lit0[p * 4 + k]) ? 1 : 0;
                        darkNotBlack += dark0[p * 4] != 0.0f || dark0[p * 4 + 1] != 0.0f || dark0[p * 4 + 2] != 0.0f ? 1 : 0;
                        frostRoom += frostProbe[p * 4] >= 0.0f && frostProbe[p * 4 + 1] >= 0.0f ? 1 : 0;
                        stainRoom += stainProbe[p * 4] >= 0.0f && stainProbe[p * 4 + 1] >= 0.0f ? 1 : 0;
                        sb += stain[p * 4 + 2]; sr += stain[p * 4]; cb += stainClear[p * 4 + 2]; cr += stainClear[p * 4];
                        const size_t q = p + 1;
                        if (p % kS + 1 < static_cast<size_t>(kS) && drawn(lit, q))
                        {
                            clearStep += std::fabs(L(lit, p) - L(lit, q)); clearMean += L(lit, p);
                            frostStep += std::fabs(L(frost, p) - L(frost, q)); frostMean += L(frost, p);
                        }
                    }
                    if (n < 300) continue;
                    litSum += vl; darkSum += vd; glassPx += n;
                    std::sort(dl.begin(), dl.end());
                    const float readable = static_cast<float>((dl[dl.size() * 9 / 10] - dl[dl.size() / 10]) / std::max(1e-6, vd / n));
                    if (readable < leastReadable) { leastReadable = readable; leastAt = at; }
                    frostThere += frostRoom * 100 >= n * 95 ? 1 : 0; ++frostViews;
                    stainThere += stainRoom * 100 >= n * 95 ? 1 : 0;
                    const float blur = static_cast<float>((frostStep / std::max(1e-6, frostMean)) / std::max(1e-6, clearStep / std::max(1e-6, clearMean)));
                    if (blur > worstBlur) { worstBlur = blur; blurAt = at; }
                    ++blurViews;
                    const float tint = static_cast<float>((sb / std::max(1e-6, sr)) / std::max(1e-6, cb / std::max(1e-6, cr)));
                    if (tint < leastTint) { leastTint = tint; tintAt = at; }
                }
        const float ratio = litSum > 0 ? static_cast<float>(darkSum / litSum) : 0.0f;
        const float darkCool = static_cast<float>((darkBlue / std::max(1e-6, darkRed)) / std::max(1e-6, litBlue / std::max(1e-6, litRed)));
        char line[1600];
        std::snprintf(line, sizeof(line), "WARP, rooms at night (Stormwind's style, six windows, %d views 5..20 yd, head on and 45 deg, level and from below; %lld pixels): "
                      "NaN/Inf %d; DarkRoomLight %.2f: a dark room's light %.3f of a lit one's (bounds 0.08 .. 0.15), readable (its contrast at least %.2f, at %s; "
                      "bound %.2f), cooler than the lamp-lit (blue over red %.2f x); with 0: dark pixels not black %d, lit rooms' bits changed by it %d; frosted "
                      "glass's room there in %d of %d views, blurred (its pixel steps %.2f of the clear room's at worst, at %s; bound %.2f); stained glass's "
                      "room there in %d of %d views, tinted by a blue glass (blue over red %.2f x the clear room's at least, at %s; bound 1.5)",
                      views, glassPx, bad, s.darkRoomLight, ratio, leastReadable, leastAt.c_str(), kReadable, darkCool, darkNotBlack, litChanged, frostThere,
                      frostViews, worstBlur, blurAt.c_str(), kBlur, stainThere, frostViews, leastTint, tintAt.c_str());
        Check(drewAll && views == 12 && bad == 0 && ratio >= 0.08f && ratio <= 0.15f && leastReadable >= kReadable && darkCool > 1.0f && darkNotBlack == 0 &&
              litChanged == 0 && frostThere == views && worstBlur <= kBlur && stainThere == views && leastTint >= 1.5f, line);
        const float zero[16] = {};
        w.dev->SetPixelShaderConstantF(gl::kViewConstant, zero, 4);
        w.dev->SetPixelShaderConstantF(gl::kSeedConstant, kZeroSeed, gl::kSeedRegisters);
        float look[4];
        gl::LookConstants(gl::Light{}, look);
        w.dev->SetPixelShaderConstantF(gl::kLookConstant, look, gl::kLookRegisters);
        readback->Release();
        target->Release();
        probe->Release();
        ps->Release();
    }

    // --- 7. lit or dark: the shader and the window lights agree (on WARP) -------------------------------------------
    // The blocks with a probe right after the window's lit value: r0 = (lit, the lit hash, its key: ordinal, group).
    // --- 10. the redraw (features/SeeThrough: b11, c219) and the corner that isn't glass (c27.zw) --------------------------
    // The blocks with the stock alpha in c0.x (r12.w), as the WMO edit gives it: `parts` of GlassBlock's, and the redraw's
    // alpha (ReplayAlpha) or the stock one as it was.
    std::string ReplayShader(int parts, bool replayAlpha)
    {
        return "ps_3_0\n    def c175, 1, 0, 0.5, 2\n    def c176, 0.2126, 0.7152, 0.0722, 0.2\n" + gl::ShaderDefs() +
               "    dcl_texcoord0 v0.xyz\n    dcl_texcoord1 v1.xyz\n    dcl_texcoord2 v2.xy\n    dcl_texcoord3 v3.xyz\n    dcl_2d s0\n"
               "    mov r12.xyz, v3\n    mov r12.w, c0.x\n    mov r31.xyz, c175.y" +
               gl::GlassBlock("v0", "v1", "v2", std::string(), std::string(), parts) + gl::FloorBlock("v0", "v1", "v2") +
               "\n    add oC0.xyz, r12, r31" + (replayAlpha ? gl::ReplayAlpha() : std::string()) + "\n    mov oC0.w, r12.w\n";
    }

    void ReplayTests(const std::filesystem::path& data)
    {
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("skip  the redraw: %s\n", why.c_str()); return; }
        gl::RoomStyle style;
        gl::Building building;
        float stormMask[4];
        if (!StormwindBuilding(data, style, building, stormMask)) building.seed = 4242;
        std::vector<uint8_t> oldCode, newCode, uvCode;
        const std::string uvText = "ps_3_0\n    def c0, 0, 0, 0, 1\n    dcl_texcoord2 v2.xy\n    mov oC0.xy, v2\n    mov oC0.zw, c0\n";
        if (!wxl_livingazeroth::shaderpatch::Assemble(ReplayShader(0, false), "as it was", oldCode, why) ||
            !wxl_livingazeroth::shaderpatch::Assemble(ReplayShader(gl::kAllParts, true), "the redraw", newCode, why) ||
            !wxl_livingazeroth::shaderpatch::Assemble(uvText, "uv", uvCode, why))
        { Check(false, "the redraw: the shaders assemble: " + why.substr(0, 300)); return; }
        IDirect3DPixelShader9 *oldPs = nullptr, *newPs = nullptr, *uvPs = nullptr;
        constexpr int kS = 96;
        constexpr float kFocal = 200.0f;   // (both windows whole on the target)
        IDirect3DSurface9 *target = nullptr, *readback = nullptr;
        if (FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(oldCode.data()), &oldPs)) ||
            FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(newCode.data()), &newPs)) ||
            FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(uvCode.data()), &uvPs)) ||
            FAILED(w.dev->CreateRenderTarget(kS, kS, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr)) ||
            FAILED(w.dev->CreateOffscreenPlainSurface(kS, kS, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &readback, nullptr)))
        { Check(false, "the redraw: WARP resources"); return; }
        rh::Rooms rooms;
        rooms.Textures(w.dev, TexturesFolder(), gl::TextureSetOf(style.palette, style.cls));
        // two windows side by side (ordinals 40 and 41, the facade's group 0), each its own room, facing the eye
        const float cam[3] = { -8838.6f, 627.4f, 99.3f }, heading = 0.37f, dist = 9.0f;
        const float C[3] = { cam[0] + dist * std::cos(heading), cam[1] + dist * std::sin(heading), cam[2] };
        const float N[3] = { -std::cos(heading), -std::sin(heading), 0 };
        float A[3], U[3];
        gl::WindowFrame(N, A, U);
        const float hw = 0.8f, hh = 1.0f;
        std::vector<gl::Window> ws;
        for (int k = 0; k < 2; ++k)
        {
            const float m3[3] = { C[0] + A[0] * 1.2f * (k ? 1 : -1), C[1] + A[1] * 1.2f * (k ? 1 : -1), C[2] };
            ws.push_back(rh::MakeWindow(kFacadePath, 0, static_cast<uint32_t>(40 + k), m3, N, hw, hh, gl::kLeaded));
        }
        rooms.Table(w.dev, ws);
        float F3[3] = { C[0] - cam[0], C[1] - cam[1], C[2] - cam[2] }; Norm(F3);
        const float fl = std::sqrt(F3[0] * F3[0] + F3[1] * F3[1]);
        const float R[3] = { F3[1] / fl, -F3[0] / fl, 0 };
        const float Uv[3] = { R[1] * F3[2] - R[2] * F3[1], R[2] * F3[0] - R[0] * F3[2], R[0] * F3[1] - R[1] * F3[0] };
        // The batch's constants at an hour (by day, at night with every room lit or dark), a glass mask (all, a half), the
        // texture's corner (c27.zw)
        struct Look { float c[4 * gl::kConstantCount], seed[4 * gl::kSeedRegisters], look[4], day[4 * gl::kDayRegisters]; };
        auto constants = [&](int hour, bool dark, float mask) {
            gl::Inputs in;
            if (hour == 0) { in.sun[0] = 0.3f; in.sun[1] = -0.2f; in.sun[2] = -0.5f; in.daylight = 0; in.night = 1; in.sky[0] = 0.04f; in.sky[1] = 0.05f; in.sky[2] = 0.09f; }
            else { in.sun[0] = -0.4f; in.sun[1] = 0.3f; in.sun[2] = 0.86f; in.daylight = 1; in.night = 0; in.sky[0] = 0.55f; in.sky[1] = 0.65f; in.sky[2] = 0.8f; }
            Norm(in.sun);
            const gl::Light light = gl::Evaluate(in);
            gl::Material m;
            m.kind = gl::kLeaded; m.mask[0] = m.mask[1] = m.mask[2] = 0; m.mask[3] = mask;
            gl::Settings s;
            Look l;
            gl::BatchConstants(m, 0.0f, light, in.sun, in.sunColour, in.sky, s, l.c);
            gl::LookConstants(light, l.look);
            gl::RoomInputs ri;
            ri.night = hour == 0 ? 1.0f : 0.0f; ri.hour = hour == 0 ? 23.0f : 13.0f; ri.selfLit = true; ri.kind = gl::kLeaded;
            gl::RoomBatchConstants(style, &building, light, ri, s, l.c, l.seed, l.look);
            l.c[47] = l.c[35] = dark ? 1.0f : 0.0f;
            gl::DayConstants(m, light, s, building.flags, l.day);
            return l;
        };
        // One draw of one window (0, 1; or both: -1) with a shader, the constants, b11 and c219, c27.zw, the stock alpha, the
        // uv across the quad (0..1) or 0.5 everywhere, b9; the pixels (rgba), the target cleared to 0 first. c28 (the redraw's
        // debug view and split) and whether the batch has no grid are the next two.
        float c28[4] = { 0, 0, 0.999f, 0 };
        bool noGrid = false;
        auto draw = [&](IDirect3DPixelShader9* ps, int which, const Look& l, bool b11, const float c219[4], const float corner[2], float stockAlpha,
                        bool uvAcross, bool glassOn, std::vector<float>& img) -> bool
        {
            IDirect3DDevice9* d = w.dev;
            float regs[4 * gl::kRoomRegisters];
            rooms.Registers(cam, kFacadePath, 0, style.cls, gl::Settings{}, regs, false, gl::kLeaded);
            regs[50] = corner[0]; regs[51] = corner[1];
            if (noGrid) regs[23] = 0.0f;
            d->SetPixelShaderConstantF(gl::kRoomConstant, regs, gl::kRoomRegisters);
            d->SetPixelShaderConstantF(gl::kReplayDrawConstant, c28, 1);
            const float lookupAlone[4] = { -1, 0, 0, 0 };   // c33: the lookup's alone (not split by window)
            d->SetPixelShaderConstantF(gl::kReplayWindowConstant, lookupAlone, 1);
            rooms.Bind(d);
            float cv[16] = { cam[0], cam[1], cam[2], 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
            d->SetPixelShaderConstantF(gl::kViewConstant, cv, 4);
            d->SetPixelShaderConstantF(gl::kFirstConstant, l.c, gl::kConstantCount);
            d->SetPixelShaderConstantF(gl::kSeedConstant, l.seed, gl::kSeedRegisters);
            d->SetPixelShaderConstantF(gl::kLookConstant, l.look, gl::kLookRegisters);
            d->SetPixelShaderConstantF(gl::kDayConstant, l.day, gl::kDayRegisters);
            const float stock[4] = { stockAlpha, 0, 0, 0 };
            d->SetPixelShaderConstantF(0, stock, 1);
            d->SetPixelShaderConstantF(gl::kReplayConstant, c219, 1);
            d->SetRenderTarget(0, target);
            D3DVIEWPORT9 vp{ 0, 0, kS, kS, 0.0f, 1.0f };
            d->SetViewport(&vp);
            d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
            d->SetRenderState(D3DRS_ZENABLE, FALSE); d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE); d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE); d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
            d->SetVertexShader(nullptr); d->SetPixelShader(ps);
            d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE3(0) | D3DFVF_TEXCOORDSIZE3(1) | D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE3(3));
            d->SetTexture(0, w.texel);
            const BOOL on9 = glassOn ? TRUE : FALSE, off = FALSE, on11 = b11 ? TRUE : FALSE;
            d->SetPixelShaderConstantB(gl::kSwitch, &on9, 1);
            d->SetPixelShaderConstantB(gl::kFloorSwitch, &off, 1);
            d->SetPixelShaderConstantB(gl::kReplaySwitch, &on11, 1);
            if (FAILED(d->BeginScene())) return false;
            struct PV { float x, y, z, rhw, p[3], n[3], uv[2], base[3]; };
            const float lit[3] = { 0.9f, 0.7f, 0.42f };
            for (int k = 0; k < 2; ++k)
            {
                if (which >= 0 && which != k) continue;
                PV v[4];
                const float cornerXY[4][2] = { { -hw, hh }, { hw, hh }, { -hw, -hh }, { hw, -hh } };
                const float uvs[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
                for (int c = 0; c < 4; ++c)
                {
                    float q[3];
                    for (int i = 0; i < 3; ++i) q[i] = ws[k].center[i] + A[i] * cornerXY[c][0] * 0.995f + U[i] * cornerXY[c][1] * 0.995f - cam[i];
                    const float zf = q[0] * F3[0] + q[1] * F3[1] + q[2] * F3[2];
                    const float sx = kS * 0.5f + kFocal * (q[0] * R[0] + q[1] * R[1] + q[2] * R[2]) / zf;
                    const float sy = kS * 0.5f - kFocal * (q[0] * Uv[0] + q[1] * Uv[1] + q[2] * Uv[2]) / zf;
                    v[c] = { sx - 0.5f, sy - 0.5f, 0.5f, 1.0f / zf, { q[0], q[1], q[2] }, { N[0], N[1], N[2] },
                             { uvAcross ? uvs[c][0] : 0.5f, uvAcross ? uvs[c][1] : 0.5f }, { lit[0], lit[1], lit[2] } };
                }
                d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(PV));
            }
            d->EndScene();
            D3DLOCKED_RECT lr{};
            if (FAILED(d->GetRenderTargetData(target, readback)) || FAILED(readback->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
            img.resize(static_cast<size_t>(kS) * kS * 4);
            for (int row = 0; row < kS; ++row)
                std::memcpy(&img[static_cast<size_t>(row) * kS * 4], static_cast<const uint8_t*>(lr.pBits) + row * lr.Pitch, sizeof(float) * kS * 4);
            readback->UnlockRect();
            return true;
        };
        const float none[4] = { 0, 0, 0, 0 }, noCorner[2] = { 0, 0 };
        auto drawn = [](const std::vector<float>& im, size_t p) { return im[p * 4 + 3] != 0.0f || im[p * 4] != 0.0f; };
        char line[400];

        // (a) b11 off and no corner: every pixel and its alpha the block's as it was, bit for bit (by day, at night lit and
        // dark, the glass all of the texel and half of it, the stock alpha 1 and 0.6)
        {
            int draws = 0, pixels = 0, differ = 0;
            bool ok = true;
            for (const int hour : { 13, 0 })
                for (const bool dark : { false, true })
                    for (const float mask : { 1.0f, 0.5f })
                        for (const float alpha : { 1.0f, 0.6f })
                        {
                            if (hour && dark) continue;
                            const Look l = constants(hour, dark, mask);
                            std::vector<float> a, b;
                            ok = ok && draw(oldPs, -1, l, false, none, noCorner, alpha, false, true, a) && draw(newPs, -1, l, false, none, noCorner, alpha, false, true, b);
                            if (!ok) break;
                            ++draws;
                            for (size_t i = 0; i < a.size(); ++i) differ += Bits(a[i]) != Bits(b[i]) ? 1 : 0;
                            for (size_t p = 0; p < a.size() / 4; ++p) pixels += drawn(a, p) ? 1 : 0;
                        }
            std::snprintf(line, sizeof(line), "the redraw's and the corner's parts inert (b11 off, c27.zw 0): %d draws, %d pixels, %d channels differ from the block as it was",
                          draws, pixels, differ);
            Check(ok && draws == 12 && pixels > 1000 && differ == 0, line);
        }

        // (b) the redraw: window 40 open (bit 0 from 40), 41 shut. The open one: no fake room and no glow hidden (the block as it
        // was with no room and no lookup), its alpha 1 - mask x (1 - 0.35); the shut one: exactly as it was, alpha 1.
        {
            const float replay[4] = { 0.65f, -1.0f, 40.0f, 1.5f };   // (c219.w: the bits + 0.5)
            int checked = 0, openRgb = 0, openAlpha = 0, shutRgb = 0, shutAlpha = 0;
            float worstAlpha = 0;
            bool ok = true;
            for (const int hour : { 13, 0 })
                for (const bool dark : { false, true })
                    for (const float mask : { 1.0f, 0.5f })
                    {
                        if (hour && dark) continue;
                        const Look l = constants(hour, dark, mask);
                        Look plain = l;
                        plain.c[6] = 0.0f;    // c193.z: no room
                        plain.c[39] = 0.0f;   // c201.w: no lit frosted lookup
                        std::vector<float> open, openWant, shut, shutWant;
                        ok = ok && draw(newPs, 0, l, true, replay, noCorner, 1.0f, false, true, open) && draw(oldPs, 0, plain, false, none, noCorner, 1.0f, false, true, openWant) &&
                             draw(newPs, 1, l, true, replay, noCorner, 1.0f, false, true, shut) && draw(oldPs, 1, l, false, none, noCorner, 1.0f, false, true, shutWant);
                        if (!ok) break;
                        const float want = 1.0f - mask * 0.65f;
                        for (size_t p = 0; p < open.size() / 4; ++p)
                        {
                            if (drawn(openWant, p))
                            {
                                ++checked;
                                for (int k = 0; k < 3; ++k) openRgb += Bits(open[p * 4 + k]) != Bits(openWant[p * 4 + k]) ? 1 : 0;
                                worstAlpha = std::max(worstAlpha, std::fabs(open[p * 4 + 3] - want));
                                openAlpha += std::fabs(open[p * 4 + 3] - want) > 1e-6f ? 1 : 0;
                            }
                            if (drawn(shutWant, p))
                            {
                                ++checked;
                                for (int k = 0; k < 3; ++k) shutRgb += Bits(shut[p * 4 + k]) != Bits(shutWant[p * 4 + k]) ? 1 : 0;
                                shutAlpha += shut[p * 4 + 3] != 1.0f ? 1 : 0;
                            }
                        }
                    }
            std::snprintf(line, sizeof(line), "the redraw (b11): %d pixels; the open window as the glass with no fake room and no glow hidden (%d channels differ), its alpha "
                          "1 - mask x 0.65 (%d off, worst %.2g); the shut one as it was (%d channels differ), opaque (%d not)", checked, openRgb, openAlpha, worstAlpha,
                          shutRgb, shutAlpha);
            Check(ok && checked > 1000 && openRgb == 0 && openAlpha == 0 && shutRgb == 0 && shutAlpha == 0, line);
            // the lead (mask 0): opaque in the open window too
            const Look lead = constants(13, false, 0.0f);
            std::vector<float> img;
            int notOpaque = 0, px = 0;
            ok = draw(newPs, 0, lead, true, replay, noCorner, 1.0f, false, true, img);
            for (size_t p = 0; ok && p < img.size() / 4; ++p)
                if (drawn(img, p)) { ++px; notOpaque += img[p * 4 + 3] != 1.0f ? 1 : 0; }
            std::snprintf(line, sizeof(line), "the redraw: the open window's lead and frame (mask 0) opaque: %d of %d pixels not", notOpaque, px);
            Check(ok && px > 100 && notOpaque == 0, line);
            // which windows: the bits from the first ordinal (41 alone open: from 41, bit 0; 40 then shut); none open
            const float only41[4] = { 0.65f, -1.0f, 41.0f, 1.5f }, noneOpen[4] = { 0.65f, -1.0f, 40.0f, 0.5f }, both[4] = { 0.65f, -1.0f, 40.0f, 3.5f };
            const Look l = constants(13, false, 1.0f);
            std::vector<float> a40, a41, n40, b41;
            ok = draw(newPs, 0, l, true, only41, noCorner, 1.0f, false, true, a40) && draw(newPs, 1, l, true, only41, noCorner, 1.0f, false, true, a41) &&
                 draw(newPs, 0, l, true, noneOpen, noCorner, 1.0f, false, true, n40) && draw(newPs, 1, l, true, both, noCorner, 1.0f, false, true, b41);
            const size_t mid0 = 0, mid = static_cast<size_t>(kS / 2) * kS;   // (the rows' middles: each window's own draw)
            auto alphaOf = [&](const std::vector<float>& im) { float a = -1; for (size_t p = mid0; p < im.size() / 4; ++p) if (drawn(im, p)) { a = im[p * 4 + 3]; break; } return a; };
            (void)mid;
            std::snprintf(line, sizeof(line), "the redraw's bits: from 41, 40 shut (alpha %.2f) and 41 open (%.2f); none open (%.2f); 40 and 41 open: 41 (%.2f)",
                          alphaOf(a40), alphaOf(a41), alphaOf(n40), alphaOf(b41));
            Check(ok && alphaOf(a40) == 1.0f && Near(alphaOf(a41), 0.35f, 1e-6f) && alphaOf(n40) == 1.0f && Near(alphaOf(b41), 0.35f, 1e-6f), line);
        }

        // (c) the batch's own alpha test on the stock alpha (c219.y): below it nothing drawn, at or above it drawn
        {
            const Look l = constants(13, false, 1.0f);
            const float keyed[4] = { 0.65f, 0.878f, 40.0f, 1.5f };
            std::vector<float> below, above;
            const bool ok = draw(newPs, 0, l, true, keyed, noCorner, 0.5f, false, true, below) && draw(newPs, 0, l, true, keyed, noCorner, 0.95f, false, true, above);
            int belowDrawn = 0, aboveDrawn = 0;
            for (size_t p = 0; ok && p < below.size() / 4; ++p) { belowDrawn += drawn(below, p) ? 1 : 0; aboveDrawn += drawn(above, p) ? 1 : 0; }
            std::snprintf(line, sizeof(line), "the redraw keeps the batch's alpha test (texkill on the stock alpha): %d pixels below its reference, %d at or above", belowDrawn, aboveDrawn);
            Check(ok && belowDrawn == 0 && aboveDrawn > 100, line);
        }

        // (d) the corner that isn't glass: c27.zw (0.5, 0.5) and the uv across the window: its fourth quadrant (u, v >= 0.5)
        // the client's colour exactly (as with the glass off), the rest the glass as without the corner
        {
            const Look l = constants(0, false, 1.0f);
            const float corner[2] = { 0.5f, 0.5f };
            std::vector<float> cornered, plain, off, uv;
            const bool ok = draw(newPs, 0, l, false, none, corner, 1.0f, true, true, cornered) && draw(newPs, 0, l, false, none, noCorner, 1.0f, true, true, plain) &&
                            draw(newPs, 0, l, false, none, noCorner, 1.0f, true, false, off) && draw(uvPs, 0, l, false, none, noCorner, 1.0f, true, true, uv);
            int inCorner = 0, rest = 0, wrong = 0, glassy = 0, uHigh = 0, vHigh = 0;
            for (size_t p = 0; ok && p < cornered.size() / 4; ++p)
            {
                if (!drawn(off, p)) continue;
                const float u = uv[p * 4], v = uv[p * 4 + 1];
                uHigh += u >= 0.5f ? 1 : 0;
                vHigh += v >= 0.5f ? 1 : 0;
                if (std::fabs(u - 0.5f) < 0.02f || std::fabs(v - 0.5f) < 0.02f) continue;   // (the quadrant's edge: either)
                const bool in = u >= 0.5f && v >= 0.5f;
                const std::vector<float>& want = in ? off : plain;
                bool same = true;
                for (int k = 0; k < 3; ++k) same = same && Bits(cornered[p * 4 + k]) == Bits(want[p * 4 + k]);
                wrong += same ? 0 : 1;
                (in ? inCorner : rest) += 1;
                glassy += !in && Bits(plain[p * 4]) != Bits(off[p * 4]) ? 1 : 0;
            }
            std::snprintf(line, sizeof(line), "the corner that isn't glass: %d pixels in it the client's colour, %d outside it the glass (%d unlike the client's); %d wrong "
                          "(u >= 0.5 at %d, v >= 0.5 at %d)", inCorner, rest, glassy, wrong, uHigh, vHigh);
            Check(ok && inCorner > 200 && rest > 600 && glassy > rest / 2 && wrong == 0, line);
        }

        // (e) the redraw's two draws, split in the shader (c28.y +1: what's opaque, alpha at least c28.z; -1: the rest), and
        // its debug view (c28.x): an open window's glass magenta, its lead and frame green, part glass between them by its mask,
        // a shut window cyan, a batch with no window found (no grid) yellow; the alpha as without it
        {
            const float replay[4] = { 0.65f, -1.0f, 40.0f, 1.5f };
            const Look glassy = constants(13, false, 1.0f), lead = constants(13, false, 0.0f);
            struct Case { const char* what; const Look* l; int which; float split; bool grid; int drawn; };   // drawn: 1 all, 0 none
            const Case cases[] = { { "the open glass, the opaque draw", &glassy, 0, 1.0f, true, 0 }, { "the open glass, the see-through draw", &glassy, 0, -1.0f, true, 1 },
                                   { "the lead, the opaque draw", &lead, 0, 1.0f, true, 1 }, { "the lead, the see-through draw", &lead, 0, -1.0f, true, 0 },
                                   { "the shut window, the opaque draw", &glassy, 1, 1.0f, true, 1 }, { "the shut window, the see-through draw", &glassy, 1, -1.0f, true, 0 } };
            int wrong = 0, px = 0;
            std::string which;
            for (const Case& k : cases)
            {
                std::vector<float> all, part;
                c28[1] = 0.0f;
                bool ok = draw(newPs, k.which, *k.l, true, replay, noCorner, 1.0f, false, true, all);
                c28[1] = k.split;
                ok = ok && draw(newPs, k.which, *k.l, true, replay, noCorner, 1.0f, false, true, part);
                c28[1] = 0.0f;
                int n = 0, kept = 0;
                for (size_t p = 0; ok && p < all.size() / 4; ++p)
                    if (drawn(all, p)) { ++n; kept += drawn(part, p) ? 1 : 0; }
                px += n;
                const bool right = ok && n > 100 && kept == (k.drawn ? n : 0);
                if (!right) { ++wrong; which += std::string(" ") + k.what; }
            }
            std::snprintf(line, sizeof(line), "the redraw's split in the shader (c28.y): %d pixels over 6 cases, %d wrong%s", px, wrong, which.c_str());
            Check(wrong == 0 && px > 600, line);
            // the debug view
            const Look half = constants(13, false, 0.3f);
            struct Tint { const char* what; const Look* l; int which; bool grid; float rgb[3]; };
            const Tint tints[] = { { "magenta", &glassy, 0, true, { 1, 0, 1 } }, { "green", &lead, 0, true, { 0, 1, 0 } },
                                   { "cyan", &glassy, 1, true, { 0, 1, 1 } }, { "yellow", &glassy, 0, false, { 1, 1, 0 } } };
            int tinted = 0, off = 0, alphaOff = 0, partOff = 0, partPx = 0;
            for (const Tint& t : tints)
            {
                std::vector<float> plain, dbg;
                noGrid = !t.grid;
                bool ok = draw(newPs, t.which, *t.l, true, replay, noCorner, 1.0f, false, true, plain);
                c28[0] = 1.0f;
                ok = ok && draw(newPs, t.which, *t.l, true, replay, noCorner, 1.0f, false, true, dbg);
                c28[0] = 0.0f;
                noGrid = false;
                for (size_t p = 0; ok && p < dbg.size() / 4; ++p)
                {
                    if (!drawn(plain, p)) continue;
                    ++tinted;
                    bool same = true;
                    for (int k = 0; k < 3; ++k) same = same && std::fabs(dbg[p * 4 + k] - t.rgb[k]) < 1e-5f;
                    off += same ? 0 : 1;
                    alphaOff += Bits(dbg[p * 4 + 3]) != Bits(plain[p * 4 + 3]) ? 1 : 0;
                }
                if (!ok) ++off;
            }
            // a texel part glass (mask 0.3): 0.3 of the way from green to magenta
            {
                std::vector<float> dbg;
                c28[0] = 1.0f;
                const bool ok = draw(newPs, 0, half, true, replay, noCorner, 1.0f, false, true, dbg);
                c28[0] = 0.0f;
                for (size_t p = 0; ok && p < dbg.size() / 4; ++p)
                    if (drawn(dbg, p))
                    {
                        ++partPx;
                        partOff += std::fabs(dbg[p * 4] - 0.3f) < 1e-5f && std::fabs(dbg[p * 4 + 1] - 0.7f) < 1e-5f && std::fabs(dbg[p * 4 + 2] - 0.3f) < 1e-5f ? 0 : 1;
                    }
            }
            std::snprintf(line, sizeof(line), "the redraw's debug view (c28.x): %d pixels magenta, green, cyan and yellow as meant (%d not), the alpha as without it (%d "
                          "differ); a glass mask of 0.3 between green and magenta by it (%d of %d not)", tinted, off, alphaOff, partOff, partPx);
            Check(off == 0 && alphaOff == 0 && tinted > 400 && partOff == 0 && partPx > 100, line);
        }
        for (IUnknown* u : std::initializer_list<IUnknown*>{ oldPs, newPs, uvPs, target, readback }) if (u) u->Release();
    }

    // --- 12. the see-through redraw's lead mask (env/GlassModel LeadMask, GlassLead.cdbc) ----------------------------------
    // "yeah build the lead mask pls :D" (the user, 2026-10-21): every pane's inside glass, lit and shaded alike; the lead,
    // the frames and the shutters opaque. GLASS_DUMP=<folder>: the masks (and the inn's views below) as PGM/PPM files.
    void WritePnm(const std::filesystem::path& p, uint32_t w, uint32_t h, int channels, const std::vector<uint8_t>& bytes)
    {
        std::ofstream f(p, std::ios::binary);
        f << (channels == 1 ? "P5\n" : "P6\n") << w << " " << h << "\n255\n";
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    // A texture's GlassMaterials row as features/Glass reads it, and its GlassLead rule (the shipped tables).
    bool MaterialRow(const Table& t, const std::string& texture, gl::Material& m)
    {
        for (const auto& r : t.rows)
            if (r[1] < t.strings.size() && std::string(&t.strings[r[1]]) == texture)
            {
                m.kind = r[2]; m.flags = r[3];
                for (int k = 0; k < 3; ++k) { m.colour[k] = F(r[4 + k]); m.light[k] = F(r[7 + k]); m.mask[k] = F(r[10 + k]); }
                m.mask[3] = F(r[13]);
                for (int k = 0; t.fields >= 24 && k < 2; ++k)
                    m.notGlass[k] = std::isfinite(F(r[22 + k])) && F(r[22 + k]) >= 0.0f && F(r[22 + k]) < 1.0f ? F(r[22 + k]) : 1.0f;
                if (m.notGlass[0] >= 1.0f || m.notGlass[1] >= 1.0f) m.notGlass[0] = m.notGlass[1] = 1.0f;
                return true;
            }
        return false;
    }

    gl::LeadRule LeadRuleOf(const Table& t, const std::string& texture)
    {
        gl::LeadRule rule;
        for (const auto& r : t.rows)
            if (t.fields >= 8 && r[1] < t.strings.size() && std::string(&t.strings[r[1]]) == texture)
            {
                rule.rects.push_back({ F(r[2]), F(r[3]), F(r[4]), F(r[5]) });
                rule.dark = F(r[6]);
                rule.line = F(r[7]);
            }
        return rule;
    }

    std::vector<uint8_t> ReadBytes(const std::filesystem::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }

    void LeadTests(const std::filesystem::path& data)
    {
        char line[400];
        // (a) a pane in a 32 x 32 texture: its top lit, its bottom shaded (the light's mask: 0.35), a lead line down its middle,
        // wood all round; its rectangle (8 .. 24)
        {
            constexpr uint32_t S = 32;
            std::vector<float> rgb(S * S * 3);
            auto put = [&](uint32_t x, uint32_t y, float r, float g, float b) { float* p = &rgb[(y * S + x) * 3]; p[0] = r; p[1] = g; p[2] = b; };
            for (uint32_t y = 0; y < S; ++y)
                for (uint32_t x = 0; x < S; ++x)
                {
                    const bool pane = x >= 8 && x < 24 && y >= 8 && y < 24;
                    if (!pane) put(x, y, 0.35f, 0.25f, 0.15f);
                    else if (x == 15 || x == 16) put(x, y, 0.05f, 0.04f, 0.03f);
                    else if (y < 16) put(x, y, 0.9f, 0.85f, 0.4f);
                    else put(x, y, 0.45f, 0.38f, 0.12f);
                }
            gl::Material m;
            m.mask[0] = 0.726f; m.mask[1] = 2.442f; m.mask[2] = 0.247f; m.mask[3] = -0.944f;   // mm_elwynn_wnd_ext__01's
            gl::LeadRule rule;
            rule.rects.push_back({ 8.0f / S, 8.0f / S, 24.0f / S, 24.0f / S });
            std::vector<uint8_t> mask;
            const bool ok = gl::LeadMask(rgb.data(), S, S, m, rule, mask);
            int lit = 0, shaded = 0, litGlass = 0, shadedGlass = 0, lead = 0, leadGlass = 0, wood = 0, woodGlass = 0, shadedLight = 0;
            for (uint32_t y = 0; ok && y < S; ++y)
                for (uint32_t x = 0; x < S; ++x)
                {
                    const bool g = mask[y * S + x] == 255, pane = x >= 8 && x < 24 && y >= 8 && y < 24;
                    if (!pane) { ++wood; woodGlass += g ? 1 : 0; }
                    else if (x == 15 || x == 16) { ++lead; leadGlass += g ? 1 : 0; }
                    else if (y < 16) { ++lit; litGlass += g ? 1 : 0; }
                    else
                    {
                        ++shaded; shadedGlass += g ? 1 : 0;
                        const float* p = &rgb[(y * S + x) * 3];
                        shadedLight += m.mask[0] * p[0] + m.mask[1] * p[1] + m.mask[2] * p[2] + m.mask[3] > 0.5f ? 1 : 0;
                    }
                }
            std::snprintf(line, sizeof(line), "the lead mask: a pane's lit half glass (%d of %d), its shaded half too (%d of %d; the light's mask: %d), its lead "
                          "line not (%d of %d), the wood round it not (%d of %d)", litGlass, lit, shadedGlass, shaded, shadedLight, leadGlass, lead, woodGlass, wood);
            Check(ok && litGlass == lit && shadedGlass == shaded && shadedLight == 0 && leadGlass == 0 && woodGlass == 0, line);
            std::vector<uint8_t> none;
            uint32_t w = 4, h = 4;
            std::vector<uint8_t> level = { 0, 255, 255, 255, 0, 0, 255, 255, 0, 0, 0, 0, 255, 255, 0, 0 }, next;
            gl::LeadMip(level, w, h, next);
            const bool mipOk = w == 2 && h == 2 && next == std::vector<uint8_t>{ 64, 255, 128, 0 };
            uint32_t one = 1, oneH = 1;
            gl::LeadMip(std::vector<uint8_t>{ 200 }, one, oneH, level);
            Check(!gl::LeadMask(rgb.data(), S, S, m, gl::LeadRule{}, none) && none.empty() && mipOk && one == 1 && oneH == 1 && level == std::vector<uint8_t>{ 200 },
                  "the lead mask: none without rectangles; its mips the 2 x 2 means (a 1 x 1 stays)");
        }
        // (b) the shipped table: the human window atlases' panes, each a glass texture's, in its uv, none in the forge's corner;
        // since 2026-10-22 stained glass's and Northrend's arched windows' (the stained sheets seven, the arched windows the
        // whole texture), each with its own Dark
        Table lt, mt;
        if (!ReadTable(data / "GlassLead.cdbc", lt) || !ReadTable(data / "GlassMaterials.cdbc", mt)) { Check(false, "GlassLead.cdbc and GlassMaterials.cdbc read"); return; }
        std::map<std::string, int> perTexture;
        int insane = 0, unknown = 0, corner = 0;
        for (const auto& r : lt.rows)
        {
            const std::string t = r[1] < lt.strings.size() ? std::string(&lt.strings[r[1]]) : std::string();
            gl::Material m;
            unknown += MaterialRow(mt, t, m) ? 0 : 1;
            const float u0 = F(r[2]), v0 = F(r[3]), u1 = F(r[4]), v1 = F(r[5]);
            const bool human = m.notGlass[0] < 1.0f;   // (the atlases with the forge's corner)
            const bool dark = human ? Near(F(r[6]), gl::kLeadDark, 1e-6f) : F(r[6]) >= 0.3f && F(r[6]) <= 0.6f;
            insane += u0 >= 0 && v0 >= 0 && u1 <= 1 && v1 <= 1 && u0 < u1 && v0 < v1 && dark && Near(F(r[7]), gl::kLeadLine, 1e-6f) ? 0 : 1;
            corner += u1 > m.notGlass[0] && v1 > m.notGlass[1] ? 1 : 0;
            ++perTexture[t];
        }
        int humans = 0, sheets = 0, arched = 0, odd = 0;
        for (const auto& kv : perTexture) ++(kv.second == 19 ? humans : kv.second == 7 ? sheets : kv.second == 1 ? arched : odd);
        const bool even = perTexture.size() == 13 && humans == 8 && sheets == 2 && arched == 3 && odd == 0;
        std::snprintf(line, sizeof(line), "GlassLead.cdbc: %zu rectangles over %zu textures (19 each on %d human atlases, 7 on %d stained sheets, the whole on %d "
                      "arched windows), %d out of range, %d not a glass texture's, %d in the forge's corner", lt.rows.size(), perTexture.size(), humans, sheets, arched,
                      insane, unknown, corner);
        Check(lt.fields == 9 && lt.rows.size() == 169 && even && insane == 0 && unknown == 0 && corner == 0, line);
        // (c) the textures themselves (GLASS_TEXTURES): decoded as the runtime does, each cell's glass share by the lead mask
        // against the light's, the shutters and the post opaque; GLASS_DUMP: each mask
        const char* textures = TexturesFolder();
        if (!textures) { std::printf("skip  the lead masks of the real textures (GLASS_TEXTURES not set)\n"); return; }
        const char* dump = std::getenv("GLASS_DUMP");
        int cells = 0, wrong = 0;
        std::string worst;
        for (const auto& kv : perTexture)
        {
            const std::string& t = kv.first;
            if (kv.second != 19) continue;   // (the human atlases here; the others below)
            std::string flat = t;
            std::replace(flat.begin(), flat.end(), '\\', '#');
            const std::vector<uint8_t> file = ReadBytes(std::filesystem::path(textures) / flat);
            std::vector<float> rgb;
            uint32_t w = 0, h = 0;
            gl::Material m;
            std::vector<uint8_t> mask;
            float mean[3] = {};
            if (!wxl_livingazeroth::blp::DecodeTop(file.data(), file.size(), rgb, w, h) || !MaterialRow(mt, t, m) ||
                !gl::LeadMask(rgb.data(), w, h, m, LeadRuleOf(lt, t), mask) || !wxl_livingazeroth::blp::AverageColour(file.data(), file.size(), mean))
            { ++wrong; worst += " " + flat + " (unread)"; continue; }
            double sum[3] = {};
            for (size_t i = 0; i < rgb.size(); ++i) sum[i % 3] += rgb[i];
            for (int k = 0; k < 3; ++k)
                if (!Near(static_cast<float>(sum[k] / (rgb.size() / 3)), mean[k], 0.03f)) { ++wrong; worst += " " + flat + " (its colours)"; break; }
            auto share = [&](uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, bool light) {
                double n = 0, g = 0;
                for (uint32_t y = y0 * h / 256; y < y1 * h / 256; ++y)
                    for (uint32_t x = x0 * w / 256; x < x1 * w / 256; ++x)
                    {
                        const float* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
                        g += light ? std::clamp(m.mask[0] * p[0] + m.mask[1] * p[1] + m.mask[2] * p[2] + m.mask[3], 0.0f, 1.0f) : mask[static_cast<size_t>(y) * w + x] / 255.0;
                        n += 1;
                    }
                return n ? g / n : 0.0;
            };
            // the lattice, the tall panes, the arched window: their glass by the lead mask within these bounds, the forge none,
            // the shutters (either side of the lattice) and the arched window's post none
            const double lattice = share(0, 0, 128, 128, false), tall = share(128, 0, 256, 128, false), arched = share(0, 128, 128, 256, false);
            const double forge = share(128, 128, 256, 256, false), shutters = share(0, 0, 28, 128, false) + share(100, 0, 128, 128, false);
            const double post = share(58, 190, 71, 248, false);
            const bool ok = lattice > 0.15 && lattice < 0.35 && tall > 0.4 && tall < 0.65 && arched > 0.45 && arched < 0.7 && forge == 0.0 && shutters == 0.0 && post == 0.0;
            cells += 3;
            if (!ok)
            {
                ++wrong;
                char b[200];
                std::snprintf(b, sizeof(b), " %s (lattice %.2f, tall %.2f, arched %.2f, forge %.2f, shutters %.2f, post %.2f)", flat.c_str(), lattice, tall, arched, forge, shutters, post);
                worst += b;
            }
            if (t == "dungeons\\textures\\windows\\mm_elwynn_wnd_ext__01.blp")
            {
                std::snprintf(line, sizeof(line), "the inn's texture (mm_elwynn_wnd_ext__01): glass by the lead mask: the lattice cell %.0f%%, the tall panes %.0f%%, the "
                              "arched one %.0f%% (by the light's mask %.0f%%, %.0f%%, %.0f%%)", 100 * lattice, 100 * tall, 100 * arched, 100 * share(0, 0, 128, 128, true),
                              100 * share(128, 0, 256, 128, true), 100 * share(0, 128, 128, 256, true));
                std::printf("info  %s\n", line);
            }
            if (dump) WritePnm(std::filesystem::path(dump) / ("lead_" + flat + ".pgm"), w, h, 1, mask);
        }
        std::snprintf(line, sizeof(line), "the lead masks of the eight human window atlases (decoded as the runtime does, its colours their files' mean): %d cells as "
                      "meant, the forge, the shutters and the post opaque; %d not%s", cells, wrong, worst.substr(0, 200).c_str());
        Check(wrong == 0 && cells == 24, line);
        // (d) stained glass and Northrend's arched windows (2026-10-22: "derive the lead mask from the texture itself (the dark
        // lead lines, thin dark features, the frame)"): their masks from their textures as the runtime builds them. All: the
        // light's lit glass in their rectangles nearly all glass. The stained sheets: the lattice window's lights glass, the
        // stone cross between them not (no rectangle there: as bright and as grey as the sheet's white glass). The arched
        // windows (the whole texture: glass only where lit or coloured): the flat grey round them not, a pane glass.
        struct Own { const char* texture; double lo, hi; };
        const Own own[] = { { "dungeons\\textures\\windows\\mm_stained_01.blp", 0.45, 0.7 },
                            { "dungeons\\textures\\windows\\mm_redridge_stained_01.blp", 0.35, 0.6 },
                            { "dungeons\\textures\\northrend\\humans\\nr_hu_window.blp", 0.25, 0.45 },
                            { "dungeons\\textures\\northrend\\humans\\nr_hu_window_half.blp", 0.18, 0.35 },
                            { "dungeons\\textures\\northrend\\humans\\nr_hu_interwindow.blp", 0.2, 0.4 } };
        int asMeant = 0;
        std::string notes;
        for (const Own& o : own)
        {
            const std::string t = o.texture;
            std::string flat = t;
            std::replace(flat.begin(), flat.end(), '\\', '#');
            const std::vector<uint8_t> file = ReadBytes(std::filesystem::path(textures) / flat);
            std::vector<float> rgb;
            uint32_t w = 0, h = 0;
            gl::Material m;
            std::vector<uint8_t> mask;
            const gl::LeadRule rule = LeadRuleOf(lt, t);
            if (!wxl_livingazeroth::blp::DecodeTop(file.data(), file.size(), rgb, w, h) || !MaterialRow(mt, t, m) || !gl::LeadMask(rgb.data(), w, h, m, rule, mask))
            { notes += " " + flat + " (unread)"; continue; }
            auto share = [&](uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
                double n = 0, g = 0;
                for (uint32_t y = y0 * h / 256; y < y1 * h / 256; ++y)
                    for (uint32_t x = x0 * w / 256; x < x1 * w / 256; ++x) { g += mask[static_cast<size_t>(y) * w + x] / 255.0; n += 1; }
                return n ? g / n : 0.0;
            };
            // the light's lit glass in the rectangles: glass by this mask
            double litN = 0, litG = 0;
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < w; ++x)
                {
                    const float u = (x + 0.5f) / w, v = (y + 0.5f) / h;
                    bool in = false;
                    for (const gl::LeadRect& r : rule.rects) in = in || (u >= r.u0 && u < r.u1 && v >= r.v0 && v < r.v1);
                    const float* p = &rgb[(static_cast<size_t>(y) * w + x) * 3];
                    if (!in || m.mask[0] * p[0] + m.mask[1] * p[1] + m.mask[2] * p[2] + m.mask[3] <= 0.5f) continue;
                    litN += 1;
                    litG += mask[static_cast<size_t>(y) * w + x] / 255.0;
                }
            const double all = share(0, 0, 256, 256), lit = litN ? litG / litN : 0.0;
            bool ok = all >= o.lo && all <= o.hi && lit >= 0.85;
            char b[240];
            if (rule.Whole())
            {
                const double corners = std::max(share(0, 0, 24, 24), share(232, 0, 256, 24));
                ok = ok && corners <= 0.05 && (t.find("nr_hu_window.blp") == std::string::npos || share(40, 120, 60, 160) >= 0.6);
                std::snprintf(b, sizeof(b), " %s: %.0f%% glass, the lit glass %.0f%%, the grey round it %.0f%%;", flat.c_str(), 100 * all, 100 * lit, 100 * corners);
            }
            else
            {
                const double cross = std::max(share(56, 8, 70, 50), share(8, 58, 50, 67)), lights = share(10, 10, 50, 50);
                ok = ok && cross == 0.0 && lights >= 0.3 && lights <= 0.6;
                std::snprintf(b, sizeof(b), " %s: %.0f%% glass, the lit glass %.0f%%, the lattice's lights %.0f%%, the stone cross %.0f%%;", flat.c_str(),
                              100 * all, 100 * lit, 100 * lights, 100 * cross);
            }
            notes += b;
            asMeant += ok ? 1 : 0;
            if (dump) WritePnm(std::filesystem::path(dump) / ("lead_" + flat + ".pgm"), w, h, 1, mask);
        }
        std::snprintf(line, sizeof(line), "the lead masks of stained glass and Northrend's arched windows (2026-10-22): %d of 5 as meant;%s", asMeant,
                      notes.substr(0, 300).c_str());
        Check(asMeant == 5, line);
    }

    // --- 11. the inn's own glass through the client's own patched WMO shader (WARP) -----------------------------------
    // "see-through windows don't seem to work anymore now" (the user, 2026-10-21, the panel saying windows 3, 4, 6, 9 and 14
    // open and the batch redrawn). Everything real but the GPU: the stock ps_MapObjDiffuse_02 (a shadow variant) patched by
    // the WMO edit, the Lion's Pride Inn's exterior glass triangles (its group 4's glass batch from the map scan:
    // GLASS_SCAN, the wmo-windows folder) with their own texture coordinates, its texture (mm_elwynn_wnd_ext__01 from
    // GLASS_TEXTURES), the shipped tables (the data folder: GlassMaterials' row, GlassWindows' every window in the table),
    // seen from where the user stood (the inn's west front, its space's (24, 7, 2)), the redraw's constants as the runtime
    // makes them for the bits the panel showed (from ordinal 3, 0x084B). The debug view says, per pixel, what the shader
    // decided; the alpha what the blend gets. Each window with the light's mask (as fix3 shipped) and with the lead mask
    // (section 12; s15, c28.w 1), the room's share of what's seen against the whole glass's 65%. GLASS_DUMP: each window's
    // views (the debug view; the glass over a stand-in room by day and night, with each mask and as the whole glass).
    struct InnTri { float p[3][3]; float uv[3][2]; };
    bool ReadInnGlass(const std::filesystem::path& scan, std::vector<InnTri>& out)
    {
        std::ifstream f(scan / "world#wmo#azeroth#buildings#goldshireinn#goldshireinn_004.wmo", std::ios::binary);
        const std::vector<uint8_t> g((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (g.size() < 0x60) return false;
        auto chunks = [&](size_t start, size_t end, auto fn) {
            for (size_t at = start; at + 8 <= end;)
            {
                const std::string tag = { static_cast<char>(g[at + 3]), static_cast<char>(g[at + 2]), static_cast<char>(g[at + 1]), static_cast<char>(g[at]) };
                uint32_t size;
                std::memcpy(&size, &g[at + 4], 4);
                if (at + 8 + size > end) break;
                fn(tag, at + 8, static_cast<size_t>(size));
                at += 8 + size;
            }
        };
        std::vector<uint16_t> idx;
        std::vector<float> v, uv;
        std::vector<std::pair<uint32_t, uint32_t>> glass;
        chunks(0, g.size(), [&](const std::string& tag, size_t at, size_t size) {
            if (tag != "MOGP") return;
            chunks(at + 0x44, at + size, [&](const std::string& t, size_t a, size_t s) {
                if (t == "MOVI") { idx.resize(s / 2); std::memcpy(idx.data(), &g[a], idx.size() * 2); }
                else if (t == "MOVT") { v.resize(s / 4); std::memcpy(v.data(), &g[a], v.size() * 4); }
                else if (t == "MOTV" && uv.empty()) { uv.resize(s / 4); std::memcpy(uv.data(), &g[a], uv.size() * 4); }
                else if (t == "MOBA")
                    for (size_t b = a; b + 24 <= a + s; b += 24)
                    {
                        uint32_t start;
                        uint16_t count;
                        std::memcpy(&start, &g[b + 12], 4);
                        std::memcpy(&count, &g[b + 16], 2);
                        if (g[b + 23] == 5) glass.emplace_back(start, count);   // mm_elwynn_wnd_ext__01
                    }
            });
        });
        for (const auto& [start, count] : glass)
            for (uint32_t k = start; k + 2 < start + count; k += 3)
            {
                InnTri t;
                for (int c = 0; c < 3; ++c)
                {
                    const uint16_t i = idx.at(k + c);
                    for (int j = 0; j < 3; ++j) t.p[c][j] = v.at(3 * i + j);
                    for (int j = 0; j < 2; ++j) t.uv[c][j] = uv.at(2 * i + j);
                }
                out.push_back(t);
            }
        return !out.empty();
    }

    void InnRedrawTests(const std::filesystem::path& shaders, const std::filesystem::path& data)
    {
        namespace mi = wxl_livingazeroth::modelice;
        namespace sp = wxl_livingazeroth::shaderpatch;
        namespace st = wxl_livingazeroth::seethrough;
        const char* scan = std::getenv("GLASS_SCAN");
        const char* textures = TexturesFolder();
        if (!scan || !textures) { std::printf("skip  the inn's glass through the client's patched shader (GLASS_SCAN and GLASS_TEXTURES not both set)\n"); return; }
        // the stock variant, patched as the client gets it; its inputs
        std::string src, why;
        for (const Blob& b : LoadBlobs(shaders))
            if (b.name == "ps_MapObjDiffuse_02" && !sp::Disassemble(b.code.data(), b.code.size(), src, why)) src.clear();
        if (src.empty()) { Check(false, "the inn's glass: ps_MapObjDiffuse_02 from " + shaders.string()); return; }
        std::map<int, std::string> usage;   // v register -> "color", "texcoordN", "fog"
        static const std::regex dcl(R"(dcl_(color|texcoord\d?|fog) v(\d+))");
        for (std::sregex_iterator it(src.begin(), src.end(), dcl), end; it != end; ++it)
            usage[std::stoi((*it)[2].str())] = (*it)[1].str() == "texcoord" ? "texcoord0" : (*it)[1].str();
        std::smatch mp, mn, mt;
        static const std::regex position(R"(dp3 r\d+\.\w, v(\d+), c3\b)"), normal(R"(dp3 r\d+\.\w, c4, v(\d+))"), texcoord(R"(texld r\d+, v(\d+), s0\b)");
        if (!std::regex_search(src, mp, position) || !std::regex_search(src, mn, normal) || !std::regex_search(src, mt, texcoord))
        { Check(false, "the inn's glass: the variant's position, normal and uv"); return; }
        const int vP = std::stoi(mp[1].str()), vN = std::stoi(mn[1].str()), vT = std::stoi(mt[1].str());
        std::string patched = src;
        std::vector<uint8_t> code;
        if (!mi::EditPixelWmoGlassForTests(patched, why) || !sp::Assemble(patched, "ps_MapObjDiffuse_02 patched", code, why))
        { Check(false, "the inn's glass: the variant patched and assembled: " + why.substr(0, 200)); return; }
        // the inn's glass, its texture, the tables
        std::vector<InnTri> tris;
        if (!ReadInnGlass(scan, tris)) { Check(false, std::string("the inn's glass batch from ") + scan); return; }
        Table mt0, wt;
        if (!ReadTable(data / "GlassMaterials.cdbc", mt0) || !ReadTable(data / "GlassWindows.cdbc", wt)) { Check(false, "the inn's glass: the tables read"); return; }
        auto text = [](const Table& t, uint32_t at) { return at < t.strings.size() ? std::string(&t.strings[at]) : std::string(); };
        gl::Material m;
        bool found = false;
        for (const auto& r : mt0.rows)
            if (text(mt0, r[1]) == "dungeons\\textures\\windows\\mm_elwynn_wnd_ext__01.blp")
            {
                found = true;
                m.kind = r[2]; m.flags = r[3];
                for (int k = 0; k < 3; ++k) { m.colour[k] = F(r[4 + k]); m.light[k] = F(r[7 + k]); m.mask[k] = F(r[10 + k]); }
                m.mask[3] = F(r[13]);
                // (the corner as features/Glass reads it: anything but 0 .. 1 none)
                for (int k = 0; mt0.fields >= 24 && k < 2; ++k)
                    m.notGlass[k] = std::isfinite(F(r[22 + k])) && F(r[22 + k]) >= 0.0f && F(r[22 + k]) < 1.0f ? F(r[22 + k]) : 1.0f;
                if (m.notGlass[0] >= 1.0f || m.notGlass[1] >= 1.0f) m.notGlass[0] = m.notGlass[1] = 1.0f;
            }
        std::vector<gl::Window> all;
        const uint32_t innPath = gl::PathHash("world\\wmo\\azeroth\\buildings\\goldshireinn\\goldshireinn.wmo");
        std::map<uint32_t, gl::Window> inn;
        for (const auto& r : wt.rows)
        {
            gl::Window w;
            w.path = gl::PathHash(text(wt, r[1]).c_str());
            w.group = r[2]; w.ordinal = r[3]; w.kind = r[4];
            for (int k = 0; k < 3; ++k) { w.center[k] = F(r[5 + k]); w.normal[k] = F(r[8 + k]); }
            w.halfWidth = F(r[11]); w.halfHeight = F(r[12]);
            w.thickness = wt.fields > 16 ? F(r[16]) : 0.0f;
            all.push_back(w);
            if (w.path == innPath && w.group == 4) inn[w.ordinal] = w;
        }
        if (!found || inn.size() != 17) { Check(false, "the inn's glass: Elwynn's glass row and the inn's 17 windows in the tables"); return; }
        std::ifstream bf(std::filesystem::path(textures) / "dungeons#textures#windows#mm_elwynn_wnd_ext__01.blp", std::ios::binary);
        const std::vector<uint8_t> blp((std::istreambuf_iterator<char>(bf)), std::istreambuf_iterator<char>());
        // the lead mask as the runtime builds it: the texture decoded, GlassLead.cdbc's rectangles for it
        Table lt;
        std::vector<float> texRgb;
        std::vector<uint8_t> leadMask;
        uint32_t tw = 0, th = 0;
        if (!ReadTable(data / "GlassLead.cdbc", lt) || !wxl_livingazeroth::blp::DecodeTop(blp.data(), blp.size(), texRgb, tw, th) ||
            !gl::LeadMask(texRgb.data(), tw, th, m, LeadRuleOf(lt, "dungeons\\textures\\windows\\mm_elwynn_wnd_ext__01.blp"), leadMask))
        { Check(false, "the inn's glass: its lead mask (GlassLead.cdbc's rectangles, the texture decoded)"); return; }

        Warp w;
        if (!InitWarp(w, why)) { std::printf("skip  the inn's glass: %s\n", why.c_str()); return; }
        IDirect3DDevice9* d = w.dev;
        IDirect3DPixelShader9* ps = nullptr;
        constexpr int kS = 160;
        IDirect3DSurface9 *target = nullptr, *readback = nullptr;
        std::string blpError;
        IDirect3DTexture9* glassTex = blp.empty() ? nullptr : wxl_livingazeroth::blp::LoadFromMemory(d, blp.data(), blp.size(), blpError);
        IDirect3DTexture9* white = rh::Texture(d, std::vector<uint32_t>(16, 0xFFFFFFFFu), 4, 4);
        if (FAILED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps)) || !glassTex || !white ||
            FAILED(d->CreateRenderTarget(kS, kS, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr)) ||
            FAILED(d->CreateOffscreenPlainSurface(kS, kS, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &readback, nullptr)))
        { Check(false, "the inn's glass: WARP resources (the texture: " + blpError + ")"); return; }
        // the lead mask's texture (L8, its mips: as features/SeeThrough uploads it)
        IDirect3DTexture9* leadTex = nullptr;
        {
            UINT levels = 1;
            for (uint32_t sz = std::max(tw, th); sz > 1; sz >>= 1) ++levels;
            std::vector<uint8_t> level = leadMask, next;
            uint32_t lw = tw, lh = th;
            if (SUCCEEDED(d->CreateTexture(tw, th, levels, 0, D3DFMT_L8, D3DPOOL_MANAGED, &leadTex, nullptr)) && leadTex)
                for (UINT k = 0; k < levels; ++k)
                {
                    D3DLOCKED_RECT lr{};
                    if (FAILED(leadTex->LockRect(k, &lr, nullptr, 0))) { leadTex->Release(); leadTex = nullptr; break; }
                    for (uint32_t y = 0; y < lh; ++y) std::memcpy(static_cast<uint8_t*>(lr.pBits) + static_cast<size_t>(y) * lr.Pitch, &level[static_cast<size_t>(y) * lw], lw);
                    leadTex->UnlockRect(k);
                    if (k + 1 < levels) { gl::LeadMip(level, lw, lh, next); level.swap(next); }
                }
        }
        if (!leadTex) { Check(false, "the inn's glass: the lead mask's texture"); return; }
        // the vertex: pretransformed, a colour, six texture coordinate sets, the fog
        struct Vtx { float pos[4]; float col[4]; float tc[6][4]; float fog; };
        D3DVERTEXELEMENT9 el[10] = {
            { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITIONT, 0 },
            { 0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
            { 0, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
            { 0, 48, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },
            { 0, 64, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2 },
            { 0, 80, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3 },
            { 0, 96, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 4 },
            { 0, 112, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 5 },
            { 0, 128, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_FOG, 0 },
            D3DDECL_END() };
        IDirect3DVertexDeclaration9* decl = nullptr;
        if (FAILED(d->CreateVertexDeclaration(el, &decl))) { Check(false, "the inn's glass: the vertex declaration"); return; }
        // the room's: the table of every window, the textures (synthetic unless the folder has the set)
        rh::Rooms rooms;
        rooms.Textures(d, textures, 0);
        rooms.Table(d, all);
        // the camera where the user stood (the shut window 5, a dormer edge-on from there: 8 yd in front of it), looking at
        // the window drawn (aim, before its room's registers: place); the inn's space as the world's
        float eye[3] = { 24, 7, 2 };
        float F3[3], R[3], Uv[3];
        auto aim = [&](const gl::Window& at) {
            for (int k = 0; k < 3; ++k) F3[k] = at.center[k] - eye[k];
            Norm(F3);
            const float fl = std::sqrt(F3[0] * F3[0] + F3[1] * F3[1]);
            R[0] = F3[1] / fl; R[1] = -F3[0] / fl; R[2] = 0;
            Uv[0] = R[1] * F3[2] - R[2] * F3[1]; Uv[1] = R[2] * F3[0] - R[0] * F3[2]; Uv[2] = R[0] * F3[1] - R[1] * F3[0];
        };
        float focal = 70.0f;   // (the checks' wide view; the dumps' each window filling its frame)
        // the constants: by day overcast (rain), or at night; the batch's look as the runtime makes it, the inn's room
        gl::Settings s;
        float c[4 * gl::kConstantCount], seed[4 * gl::kSeedRegisters], look[4], day[4 * gl::kDayRegisters], regs[4 * gl::kRoomRegisters];
        const gl::RoomStyle style;
        auto setTime = [&](bool night) {
            gl::Inputs in;
            if (night) { in.sun[0] = 0.3f; in.sun[1] = -0.2f; in.sun[2] = -0.5f; in.daylight = 0; in.night = 1; in.sky[0] = 0.04f; in.sky[1] = 0.05f; in.sky[2] = 0.09f; }
            else
            {
                in.sun[0] = -0.4f; in.sun[1] = 0.3f; in.sun[2] = 0.86f;
                in.daylight = 1; in.night = 0; in.overcast = 1; in.precipitation = 1;
                in.sky[0] = 0.5f; in.sky[1] = 0.52f; in.sky[2] = 0.55f;
            }
            Norm(in.sun);
            const gl::Light light = gl::Evaluate(in);
            gl::BatchConstants(m, 0.0f, light, in.sun, in.sunColour, in.sky, s, c);
            gl::RoomInputs ri;
            ri.night = night ? 1.0f : 0.0f; ri.hour = night ? 23.0f : 13.0f; ri.selfLit = night; ri.unlit = true; ri.kind = m.kind;
            gl::RoomBatchConstants(style, nullptr, light, ri, s, c, seed, look);
            gl::DayConstants(m, light, s, 0, day);
        };
        setTime(false);
        auto place = [&] {
            rooms.Registers(eye, innPath, 4, style.cls, s, regs, false, m.kind);
            regs[50] = m.notGlass[0] < 1.0f ? 1.0f - m.notGlass[0] : 0.0f;
            regs[51] = m.notGlass[1] < 1.0f ? 1.0f - m.notGlass[1] : 0.0f;
        };
        float replay[4];
        st::ReplayConstants(0.35f, false, 0, 0, 3.0f, 0x084Bu, replay);
        float leadOn = 0.0f;       // c28.w: the lead mask at s15 (1) or the light's (0)
        bool wholeGlass = false;   // the 2026-10-20 redraw: b11 off, the fake room off (c193.z 0), the whole glass blended
        auto draw = [&](uint32_t ordinal, bool b11, float debug, float split, std::vector<float>& img) -> int {
            d->SetRenderTarget(0, target);
            D3DVIEWPORT9 vp{ 0, 0, kS, kS, 0.0f, 1.0f };
            d->SetViewport(&vp);
            d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
            d->SetRenderState(D3DRS_ZENABLE, FALSE); d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE); d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE); d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF); d->SetRenderState(D3DRS_FOGENABLE, FALSE);
            d->SetVertexShader(nullptr); d->SetPixelShader(ps); d->SetVertexDeclaration(decl);
            d->SetTexture(0, glassTex);
            d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR); d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR); d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
            for (DWORD k = 4; k <= 7; ++k) d->SetTexture(k, white);
            d->SetTexture(gl::kLeadSampler, leadTex);
            d->SetSamplerState(gl::kLeadSampler, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP); d->SetSamplerState(gl::kLeadSampler, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
            d->SetSamplerState(gl::kLeadSampler, D3DSAMP_MINFILTER, D3DTEXF_LINEAR); d->SetSamplerState(gl::kLeadSampler, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            d->SetSamplerState(gl::kLeadSampler, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR); d->SetSamplerState(gl::kLeadSampler, D3DSAMP_SRGBTEXTURE, FALSE);
            rooms.Bind(d);
            // the stock part's constants (its lights, the shadow's): a middling grey, the fog colour black (the fog factor 1)
            float stock[4 * 13];
            for (float& x : stock) x = 0.5f;
            stock[8] = stock[9] = stock[10] = stock[11] = 0.0f;   // c2: the fog colour
            d->SetPixelShaderConstantF(0, stock, 13);
            float cv[16] = { eye[0], eye[1], eye[2], 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
            d->SetPixelShaderConstantF(gl::kViewConstant, cv, 4);
            float cc[4 * gl::kConstantCount];
            std::memcpy(cc, c, sizeof(cc));
            if (wholeGlass) cc[6] = 0.0f;
            d->SetPixelShaderConstantF(gl::kFirstConstant, cc, gl::kConstantCount);
            d->SetPixelShaderConstantF(gl::kSeedConstant, seed, gl::kSeedRegisters);
            d->SetPixelShaderConstantF(gl::kLookConstant, look, gl::kLookRegisters);
            d->SetPixelShaderConstantF(gl::kDayConstant, day, gl::kDayRegisters);
            d->SetPixelShaderConstantF(gl::kRoomConstant, regs, gl::kRoomRegisters);
            d->SetPixelShaderConstantF(gl::kReplayConstant, replay, 1);
            const float c28[4] = { debug, split, 0.999f, leadOn };
            d->SetPixelShaderConstantF(gl::kReplayDrawConstant, c28, 1);
            const float lookupAlone[4] = { -1, 0, 0, 0 };   // c33: the lookup's alone (not split by window)
            d->SetPixelShaderConstantF(gl::kReplayWindowConstant, lookupAlone, 1);
            const BOOL on = TRUE, off = FALSE, b11on = b11 && !wholeGlass ? TRUE : FALSE;
            d->SetPixelShaderConstantB(7, &off, 1);
            d->SetPixelShaderConstantB(8, &off, 1);
            d->SetPixelShaderConstantB(gl::kSwitch, &on, 1);
            d->SetPixelShaderConstantB(gl::kFloorSwitch, &off, 1);
            d->SetPixelShaderConstantB(gl::kReplaySwitch, &b11on, 1);
            if (FAILED(d->BeginScene())) return -1;
            const gl::Window& win = inn.at(ordinal);
            int drawnTris = 0;
            for (const InnTri& t : tris)
            {
                float mid[3];
                for (int j = 0; j < 3; ++j) mid[j] = (t.p[0][j] + t.p[1][j] + t.p[2][j]) / 3.0f;
                float a[3], u[3];
                gl::WindowFrame(win.normal, a, u);
                const float dd[3] = { mid[0] - win.center[0], mid[1] - win.center[1], mid[2] - win.center[2] };
                if (std::fabs(dd[0] * a[0] + dd[1] * a[1] + dd[2] * a[2]) > win.halfWidth + 0.05f ||
                    std::fabs(dd[0] * u[0] + dd[1] * u[1] + dd[2] * u[2]) > win.halfHeight + 0.05f ||
                    std::fabs(dd[0] * win.normal[0] + dd[1] * win.normal[1] + dd[2] * win.normal[2]) > 0.4f)
                    continue;
                Vtx v[3] = {};
                bool ok = true;
                for (int k = 0; k < 3; ++k)
                {
                    const float q[3] = { t.p[k][0] - eye[0], t.p[k][1] - eye[1], t.p[k][2] - eye[2] };
                    const float zf = q[0] * F3[0] + q[1] * F3[1] + q[2] * F3[2];
                    ok = ok && zf > 0.1f;
                    v[k].pos[0] = kS * 0.5f + focal * (q[0] * R[0] + q[1] * R[1] + q[2] * R[2]) / zf - 0.5f;
                    v[k].pos[1] = kS * 0.5f - focal * (q[0] * Uv[0] + q[1] * Uv[1] + q[2] * Uv[2]) / zf - 0.5f;
                    v[k].pos[2] = 0.5f; v[k].pos[3] = 1.0f / std::max(zf, 0.1f);
                    for (int j = 0; j < 4; ++j) v[k].col[j] = 1.0f;
                    v[k].fog = 1.0f;
                    for (const auto& [reg, use] : usage)
                    {
                        if (use.rfind("texcoord", 0) != 0 || use[8] - '0' > 5) continue;
                        float* tc = v[k].tc[use[8] - '0'];
                        if (reg == vP) { for (int j = 0; j < 3; ++j) tc[j] = q[j]; tc[3] = 1; }
                        else if (reg == vN) { for (int j = 0; j < 3; ++j) tc[j] = win.normal[j]; tc[3] = 0; }
                        else if (reg == vT) { tc[0] = t.uv[k][0]; tc[1] = t.uv[k][1]; }
                        else { tc[0] = tc[1] = 2.0f; tc[2] = 0.5f; tc[3] = 1.0f; }   // the shadow map's: outside it
                    }
                }
                if (!ok) continue;
                d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, v, sizeof(Vtx));
                ++drawnTris;
            }
            d->EndScene();
            D3DLOCKED_RECT lr{};
            if (FAILED(d->GetRenderTargetData(target, readback)) || FAILED(readback->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return -1;
            img.resize(static_cast<size_t>(kS) * kS * 4);
            for (int row = 0; row < kS; ++row)
                std::memcpy(&img[static_cast<size_t>(row) * kS * 4], static_cast<const uint8_t*>(lr.pBits) + row * lr.Pitch, sizeof(float) * kS * 4);
            readback->UnlockRect();
            return drawnTris;
        };
        auto drawnPx = [](const std::vector<float>& im, size_t p) { return im[p * 4 + 3] != 0.0f || im[p * 4] != 0.0f || im[p * 4 + 1] != 0.0f || im[p * 4 + 2] != 0.0f; };
        auto is = [](const float* px, float r, float g, float b) { return std::fabs(px[0] - r) < 1e-3f && std::fabs(px[1] - g) < 1e-3f && std::fabs(px[2] - b) < 1e-3f; };
        // an open window's glass in the debug view: mask m of the way from green to magenta, m > 0
        auto glassTint = [](const float* px) { return px[0] > 1e-3f && std::fabs(px[0] - px[2]) < 1e-3f && std::fabs(px[0] + px[1] - 1.0f) < 1e-3f; };
        char line[600];
        const char* dump = std::getenv("GLASS_DUMP");
        // a stand-in room behind the glass (a dim checker by day, lamp-lit at night), the sky round the window
        auto composite = [&](const std::vector<float>& img, bool whole, bool night, std::vector<uint8_t>& out) {
            out.assign(static_cast<size_t>(kS) * kS * 3, 0);
            for (int y = 0; y < kS; ++y)
                for (int x = 0; x < kS; ++x)
                {
                    const size_t p = static_cast<size_t>(y) * kS + x;
                    const bool odd = ((x / 10) + (y / 10)) % 2 != 0;
                    const float roomDay[2][3] = { { 0.16f, 0.12f, 0.08f }, { 0.34f, 0.25f, 0.15f } }, roomNight[2][3] = { { 0.35f, 0.22f, 0.1f }, { 0.6f, 0.42f, 0.2f } };
                    const float* room = night ? roomNight[odd] : roomDay[odd];
                    const bool drawn = img[p * 4 + 3] != 0.0f || img[p * 4] != 0.0f || img[p * 4 + 1] != 0.0f || img[p * 4 + 2] != 0.0f;
                    const float a = whole ? 0.35f : img[p * 4 + 3];
                    for (int k = 0; k < 3; ++k)
                    {
                        const float v = drawn ? img[p * 4 + k] * a + room[k] * (1.0f - a) : (night ? 0.05f : 0.42f);
                        out[p * 3 + k] = static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
                    }
                }
        };
        auto save = [&](const std::string& name, const std::vector<uint8_t>& rgb) {
            if (dump) WritePnm(std::filesystem::path(dump) / name, kS, kS, 3, rgb);
        };
        double share[2][18] = {};
        for (const uint32_t ordinal : { 3u, 9u, 14u, 5u })
        {
            const gl::Window& win = inn.at(ordinal);
            const float user[3] = { 24, 7, 2 };
            const float out = win.center[0] * win.normal[0] + win.center[1] * win.normal[1] < 0.0f ? -8.0f : 8.0f;   // (outside the inn)
            for (int k = 0; k < 3; ++k) eye[k] = ordinal == 5 ? win.center[k] + win.normal[k] * out : user[k];
            aim(win);
            place();
            const bool open = ordinal != 5;
            for (const float mask : { 0.0f, 1.0f })
            {
                leadOn = mask;
                setTime(false);
                std::vector<float> dbg, alpha, opaque, glassy;
                const int t1 = draw(ordinal, true, 1.0f, 0.0f, dbg), t2 = draw(ordinal, true, 0.0f, 0.0f, alpha),
                          t3 = draw(ordinal, true, 0.0f, 1.0f, opaque), t4 = draw(ordinal, true, 0.0f, -1.0f, glassy);
                int px = 0, magenta = 0, green = 0, cyan = 0, yellow = 0, other = 0, seeThrough = 0, both = 0, neither = 0, disagree = 0;
                float least = 1.0f;
                double room = 0.0;
                for (size_t p = 0; p < dbg.size() / 4; ++p)
                {
                    if (!drawnPx(alpha, p)) continue;
                    ++px;
                    const float* q = &dbg[p * 4];
                    (glassTint(q) ? magenta : is(q, 0, 1, 0) ? green : is(q, 0, 1, 1) ? cyan : is(q, 1, 1, 0) ? yellow : other) += 1;
                    const float a = alpha[p * 4 + 3];
                    least = std::min(least, a);
                    seeThrough += a < 0.999f ? 1 : 0;
                    room += 1.0 - a;
                    // the tint's mask and the alpha's: alpha = 1 - mask x 0.65 (the lead and the frame, green: alpha 1)
                    if (glassTint(q) || is(q, 0, 1, 0)) disagree += std::fabs(a - (1.0f - q[0] * 0.65f)) < 2e-3f ? 0 : 1;
                    const bool inOpaque = drawnPx(opaque, p), inGlass = drawnPx(glassy, p);
                    both += inOpaque && inGlass ? 1 : 0;
                    neither += !inOpaque && !inGlass ? 1 : 0;
                }
                share[mask > 0.0f][ordinal] = px ? room / px : 0.0;
                std::snprintf(line, sizeof(line), "the inn's window %u through the client's patched shader, %s (%d triangles): %d pixels; the debug view: %d open "
                              "glass, %d lead and frame, %d shut, %d no window, %d else (%d unlike the alpha); see-through at %d (least alpha %.2f), the room's share "
                              "of what's seen %.0f%%; the two draws: %d in both, %d in neither", ordinal, mask > 0.0f ? "the lead mask" : "the light's mask", t1, px,
                              magenta, green, cyan, yellow, other, disagree, seeThrough, least, px ? 100.0 * room / px : 0.0, both, neither);
                const bool ok = t1 > 0 && t1 == t2 && t2 == t3 && t3 == t4 && px > 50 && yellow == 0 && other == 0 && both == 0 && neither == 0 && disagree == 0 &&
                                (open ? cyan == 0 && magenta > 0 && seeThrough > 0 && (px < 200 || green > 0) : cyan == px && seeThrough == 0);
                Check(ok, line);
                if (!dump) continue;
                // the dumps: the same window from the same eye, filling the frame
                const float wide = focal;
                const float dist = std::sqrt((win.center[0] - eye[0]) * (win.center[0] - eye[0]) + (win.center[1] - eye[1]) * (win.center[1] - eye[1]) +
                                             (win.center[2] - eye[2]) * (win.center[2] - eye[2]));
                focal = 0.42f * kS * dist / std::max(win.halfWidth, win.halfHeight);
                std::vector<float> zDbg, zDay;
                draw(ordinal, true, 1.0f, 0.0f, zDbg);
                draw(ordinal, true, 0.0f, 0.0f, zDay);
                std::vector<uint8_t> rgb;
                const std::string tag = "inn_w" + std::to_string(ordinal) + (mask > 0.0f ? "_lead" : "_light");
                if (mask > 0.0f)
                {
                    rgb.assign(static_cast<size_t>(kS) * kS * 3, 0);
                    for (size_t p = 0; p < zDbg.size() / 4; ++p)
                        for (int k = 0; k < 3; ++k)
                            rgb[p * 3 + k] = drawnPx(zDay, p) ? static_cast<uint8_t>(std::lround(std::clamp(zDbg[p * 4 + k], 0.0f, 1.0f) * 255.0f)) : 0;
                    save(tag + "_debug.ppm", rgb);
                }
                composite(zDay, false, false, rgb);
                save(tag + "_day.ppm", rgb);
                setTime(true);
                std::vector<float> night;
                draw(ordinal, true, 0.0f, 0.0f, night);
                composite(night, false, true, rgb);
                save(tag + "_night.ppm", rgb);
                if (mask > 0.0f)
                {
                    wholeGlass = true;
                    std::vector<float> wd, wn;
                    setTime(false);
                    draw(ordinal, true, 0.0f, 0.0f, wd);
                    setTime(true);
                    draw(ordinal, true, 0.0f, 0.0f, wn);
                    wholeGlass = false;
                    composite(wd, true, false, rgb);
                    save("inn_w" + std::to_string(ordinal) + "_whole_day.ppm", rgb);
                    composite(wn, true, true, rgb);
                    save("inn_w" + std::to_string(ordinal) + "_whole_night.ppm", rgb);
                }
                focal = wide;
            }
        }
        setTime(false);
        leadOn = 0.0f;
        // the lead mask against the light's: more of the room through every open window, the lead and frame still opaque
        std::snprintf(line, sizeof(line), "the inn's open windows' room share with the lead mask against the light's (the whole glass: 65%%): window 3 %.0f%% (%.0f%%), "
                      "9 %.0f%% (%.0f%%), 14 %.0f%% (%.0f%%)", 100 * share[1][3], 100 * share[0][3], 100 * share[1][9], 100 * share[0][9], 100 * share[1][14],
                      100 * share[0][14]);
        Check(share[1][3] > share[0][3] && share[1][9] > share[0][9] && share[1][14] > share[0][14] && share[1][3] < 0.65 && share[1][9] < 0.65, line);
        if (decl) decl->Release();
        for (IUnknown* u : std::initializer_list<IUnknown*>{ ps, target, readback, glassTex, white, leadTex }) if (u) u->Release();
    }

    // --- 13. the game's sequence on WARP: the inn's glass batch drawn as the client draws it, captured, the device's state
    // clobbered and applied again, the redraw (features/SeeThroughDraw) -------------------------------------------------
    // "every see-through window pixel is solid YELLOW" (the debug view, in game, 2026-10-21): at the redraw the shader's
    // window lookup found no window. Here what the lookup reads comes the game's way: the inn placed as Goldshire's (turned
    // -97 degrees, its origin 9,464 yd out), the camera turned (view-space positions and normals, the view's rows c181..c183,
    // the eye c180), Glass's own RoomRegisters over its window table, the group's own vertex and index buffers (MOVT, MONR,
    // MOTV, MOVI from the map scan), the batch drawn with the colour writes off and captured (SeeThroughDraw's CaptureState),
    // the state clobbered (another shader, every constant and sampler cleared, another index buffer), applied
    // (ApplyCaptured), the redraw (DrawPerPixel). Then: (a) the redraw right after the client's draw: every pixel a window's,
    // the open windows (3, 9, 14) magenta or green, the shut ones cyan; (b) through the state block alone, as before this
    // fix: every pixel yellow, the game's screenshot, and the pixel constants it didn't put back (c128 and up: c180..c183
    // among them); (c) with every constant put back from the capture: every pixel decided as in (a), drawn whole and split
    // by window; (d) its plain view: the room (blue behind) through the open windows' glass only.
    struct InnGroup
    {
        std::vector<float> pos, nrm, uv;
        std::vector<uint16_t> idx;
        uint32_t start = 0, count = 0, minIndex = 0, maxIndex = 0;
    };

    // A WMO's group `group` from the scan (its file <wmo>_NNN.wmo, its backslashes as '#') and its first batch of material `material`
    // (the inn's 4 and 5: the shell's mm_elwynn_wnd_ext__01; 5 and 21: the kitchen's own glass, mm_elwynn_wnd_int__01, seen
    // from inside).
    bool ReadGroup(const std::filesystem::path& scan, const std::string& wmo, InnGroup& out, int group, uint8_t material)
    {
        std::string flat = wmo.substr(0, wmo.size() - 4);
        std::replace(flat.begin(), flat.end(), '\\', '#');
        char suffix[16];
        std::snprintf(suffix, sizeof(suffix), "_%03d.wmo", group);
        const std::vector<uint8_t> g = ReadBytes(scan / (flat + suffix));
        bool batch = false;
        for (size_t at = 0; at + 8 <= g.size();)
        {
            uint32_t size;
            std::memcpy(&size, &g[at + 4], 4);
            const std::string tag = { static_cast<char>(g[at + 3]), static_cast<char>(g[at + 2]), static_cast<char>(g[at + 1]), static_cast<char>(g[at]) };
            if (at + 8 + size > g.size()) break;
            if (tag == "MOGP")
                for (size_t a = at + 8 + 0x44; a + 8 <= at + 8 + size;)
                {
                    uint32_t s;
                    std::memcpy(&s, &g[a + 4], 4);
                    const std::string t = { static_cast<char>(g[a + 3]), static_cast<char>(g[a + 2]), static_cast<char>(g[a + 1]), static_cast<char>(g[a]) };
                    const uint8_t* p = &g[a + 8];
                    if (t == "MOVI") { out.idx.resize(s / 2); std::memcpy(out.idx.data(), p, out.idx.size() * 2); }
                    else if (t == "MOVT") { out.pos.resize(s / 4); std::memcpy(out.pos.data(), p, out.pos.size() * 4); }
                    else if (t == "MONR") { out.nrm.resize(s / 4); std::memcpy(out.nrm.data(), p, out.nrm.size() * 4); }
                    else if (t == "MOTV" && out.uv.empty()) { out.uv.resize(s / 4); std::memcpy(out.uv.data(), p, out.uv.size() * 4); }
                    else if (t == "MOBA")
                        for (size_t b = 0; !batch && b + 24 <= s; b += 24)
                            if (p[b + 23] == material)
                            {
                                uint16_t c, lo, hi;
                                std::memcpy(&out.start, p + b + 12, 4);
                                std::memcpy(&c, p + b + 16, 2); std::memcpy(&lo, p + b + 18, 2); std::memcpy(&hi, p + b + 20, 2);
                                out.count = c; out.minIndex = lo; out.maxIndex = hi;
                                batch = true;
                            }
                    a += 8 + s;
                }
            at += 8 + size;
        }
        return batch && !out.idx.empty() && out.pos.size() == out.nrm.size() && out.uv.size() / 2 == out.pos.size() / 3 &&
               out.start + out.count <= out.idx.size();
    }

    // A scene of the game's sequence (SequenceScene): a WMO's group and its glass batch, the batch's texture, its open windows
    // (the rest shut), the placement, the camera (in the WMO's space) and whether it's inside; the pixels (a) must find open
    // and shut; the tint (SeeThroughStainedTint, stained glass: its checks (e) and its sheets).
    struct SeqScene
    {
        const char* label;          // the checks'
        const char* prefix;         // the dumps' (GLASS_DUMP)
        const char* behind;         // what's behind the glass
        const char* wmo;
        int group;
        uint8_t material;           // the batch's (MOBA's material: the root's MOMT index)
        const char* texture;
        bool inside;
        std::vector<uint32_t> open;
        size_t windows;             // the group's windows, the batch's
        float yaw, origin[3];       // the placement: degrees, world yd
        float spot[3];              // the camera, the WMO's space
        int lookAt;                 // the window it looks at (ordinal), -1: `look`
        float look[3];
        int minOpen, minShut;       // (a): pixels at least
        float tint;                 // the tint's strength (0: no tint pass)
        float focal = 120.0f;       // the camera's (pixels of the 192 x 192 target)
    };

    // `inside` (SeeThroughFromInside, 2026-10-21: "windows should be see-through from the inside as well"): the same sequence
    // for the kitchen's own glass (group 5's mm_elwynn_wnd_int__01, its windows 18 .. 25) seen from inside, the camera in the
    // kitchen looking at its south wall, the street (blue) behind it: window 19 open (a way out; seethrough_tests' "from
    // inside" finds 18 and 19 open from there), 18 shut; Glass's constants as the client's camera indoors has them (its inside
    // look) over the kitchen's own window grid.
    SeqScene InnScene(bool inside)
    {
        SeqScene s{ inside ? "from inside (the kitchen's glass)" : "the game's sequence", inside ? "inside_" : "seq_", inside ? "the street" : "the room",
                    "world\\wmo\\azeroth\\buildings\\goldshireinn\\goldshireinn.wmo", inside ? 5 : 4, static_cast<uint8_t>(inside ? 21 : 5),
                    inside ? "dungeons\\textures\\windows\\mm_elwynn_wnd_int__01.blp" : "dungeons\\textures\\windows\\mm_elwynn_wnd_ext__01.blp", inside,
                    inside ? std::vector<uint32_t>{ 19 } : std::vector<uint32_t>{ 3, 9, 14 }, inside ? 8u : 17u,
                    -97.0f, { -9464.25f, 24.40f, 56.53f }, { inside ? 0.0f : 24.0f, inside ? 6.0f : 7.0f, inside ? 3.0f : 2.0f }, inside ? -1 : 9,
                    { 0, -8.8f, 2.8f }, 200, 200, 0.0f };
        return s;
    }

    // Stained glass (2026-10-22: "WARP proofs through the real sequence harness"): the Redridge chapel's south front from
    // the street, 10 yd out (its shell's mm_redridge_stained_01: window 15 open, 13 and 17 shut either side) and from inside its
    // nave (its own glass: 4 open, 2 and 6 shut, the street behind), placed turned 33 degrees; Northrend's town hall's west
    // front (nr_hu_window, its lead mask over the whole texture: 0 and 4 open, 1 shut), no tint (leaded glass).
    std::vector<SeqScene> StainedScenes()
    {
        const char* chapel = "world\\wmo\\azeroth\\buildings\\chapel\\redridgechapel.wmo";
        const char* stained = "dungeons\\textures\\windows\\mm_redridge_stained_01.blp";
        return {
            { "stained glass, the chapel from the street", "chapel_out_", "the room", chapel, 1, 6, stained, false, { 15 }, 11, 33.0f,
              { -9275.0f, -2163.0f, 71.0f }, { 4.6f, -19.0f, 4.0f }, -1, { 4.6f, -8.93f, 6.2f }, 100, 100, 0.8f, 115.0f },
            { "stained glass, the chapel from inside", "chapel_in_", "the street", chapel, 0, 6, stained, true, { 4 }, 11, 33.0f,
              { -9275.0f, -2163.0f, 71.0f }, { 4.6f, 3.0f, 3.5f }, -1, { 4.6f, -8.65f, 5.73f }, 100, 100, 0.8f, 120.0f },
            { "Northrend's arched windows, the town hall from the street", "hall_out_", "the room", "world\\wmo\\northrend\\buildings\\human\\nd_human_townhall\\nd_human_townhall.wmo",
              0, 17, "dungeons\\textures\\northrend\\humans\\nr_hu_window.blp", false, { 0, 4 }, 6, 33.0f, { 2100.0f, -5900.0f, 100.0f },
              { -22.0f, -0.4f, 3.0f }, 4, { 0, 0, 0 }, 100, 50, 0.0f },
        };
    }

    void SequenceScene(const std::filesystem::path& shaders, const std::filesystem::path& data, const SeqScene& sc)
    {
        namespace mi = wxl_livingazeroth::modelice;
        namespace sp = wxl_livingazeroth::shaderpatch;
        namespace st = wxl_livingazeroth::seethrough;
        const char* const label = sc.label;
        const char* const behind = sc.behind;
        const bool inside = sc.inside;
        const char* scan = std::getenv("GLASS_SCAN");
        const char* textures = TexturesFolder();
        if (!scan || !textures) { std::printf("skip  %s (GLASS_SCAN and GLASS_TEXTURES not both set)\n", label); return; }
        std::string src, why;
        for (const Blob& b : LoadBlobs(shaders))
            if (b.name == "ps_MapObjDiffuse_02" && !sp::Disassemble(b.code.data(), b.code.size(), src, why)) src.clear();
        std::map<int, std::string> usage;
        static const std::regex dcl(R"(dcl_(color|texcoord\d?|fog) v(\d+))");
        for (std::sregex_iterator it(src.begin(), src.end(), dcl), end; it != end; ++it)
            usage[std::stoi((*it)[2].str())] = (*it)[1].str() == "texcoord" ? "texcoord0" : (*it)[1].str();
        std::smatch mp, mn, mt;
        static const std::regex position(R"(dp3 r\d+\.\w, v(\d+), c3\b)"), normal(R"(dp3 r\d+\.\w, c4, v(\d+))"), texcoord(R"(texld r\d+, v(\d+), s0\b)");
        std::vector<uint8_t> code;
        std::string patched = src;
        if (src.empty() || !std::regex_search(src, mp, position) || !std::regex_search(src, mn, normal) || !std::regex_search(src, mt, texcoord) ||
            !mi::EditPixelWmoGlassForTests(patched, why) || !sp::Assemble(patched, "ps_MapObjDiffuse_02 patched", code, why))
        { Check(false, std::string(label) + ": the patched variant: " + why.substr(0, 200)); return; }
        const int vP = std::stoi(mp[1].str()), vN = std::stoi(mn[1].str()), vT = std::stoi(mt[1].str());
        InnGroup grp;
        if (!ReadGroup(scan, sc.wmo, grp, sc.group, sc.material)) { Check(false, std::string(label) + ": its group from the scan"); return; }
        Table mt0, wt, lt;
        if (!ReadTable(data / "GlassMaterials.cdbc", mt0) || !ReadTable(data / "GlassWindows.cdbc", wt) || !ReadTable(data / "GlassLead.cdbc", lt))
        { Check(false, std::string(label) + ": the tables read"); return; }
        const std::string texture = sc.texture;
        gl::Material m;
        if (!MaterialRow(mt0, texture, m)) { Check(false, std::string(label) + ": its glass's row"); return; }
        std::vector<gl::Window> all;
        std::vector<gl::Window> innWindows;
        const uint32_t innPath = gl::PathHash(sc.wmo);
        for (const auto& r : wt.rows)
        {
            gl::Window w;
            w.path = gl::PathHash(&wt.strings[r[1]]);
            w.group = r[2]; w.ordinal = r[3]; w.kind = r[4];
            for (int k = 0; k < 3; ++k) { w.center[k] = F(r[5 + k]); w.normal[k] = F(r[8 + k]); }
            w.halfWidth = F(r[11]); w.halfHeight = F(r[12]);
            w.thickness = wt.fields > 16 ? F(r[16]) : 0.0f;
            all.push_back(w);
            if (w.path == innPath && w.group == static_cast<uint32_t>(sc.group)) innWindows.push_back(w);
        }
        std::sort(innWindows.begin(), innWindows.end(), [](const gl::Window& a, const gl::Window& b) { return a.ordinal < b.ordinal; });
        // the see-through model's windows (ordinal order) and their states: the scene's open, the rest shut
        std::vector<st::SeeWindow> see;
        std::vector<uint8_t> states;
        const gl::Window* lookAt = nullptr;
        for (const gl::Window& w : innWindows)
        {
            st::SeeWindow s;
            s.ordinal = w.ordinal; s.group = w.group; s.kind = w.kind; s.interior = inside ? -1 : 1;
            for (int k = 0; k < 3; ++k) { s.center[k] = w.center[k]; s.normal[k] = w.normal[k]; }
            gl::WindowFrame(s.normal, s.along, s.up);
            s.halfWidth = w.halfWidth; s.halfHeight = w.halfHeight; s.thickness = w.thickness;
            see.push_back(s);
            const bool opened = std::find(sc.open.begin(), sc.open.end(), w.ordinal) != sc.open.end();
            states.push_back(opened ? st::kOpened : st::kShut);
            if (static_cast<int>(w.ordinal) == sc.lookAt) lookAt = &w;
        }
        const uint32_t nverts = static_cast<uint32_t>(grp.pos.size() / 3);
        const st::BatchWindows bw = st::WindowsOfBatch(grp.idx.data(), static_cast<uint32_t>(grp.idx.size()), grp.pos.data(), nverts, grp.start, grp.count,
                                                        see.data(), static_cast<uint32_t>(see.size()));
        std::vector<uint16_t> open, rest;
        st::SplitByWindow(bw, states.data(), states.size(), open, rest);
        float first = 0.0f;
        uint32_t bits = 0;
        st::OpenBits(bw, see.data(), states.data(), states.size(), first, bits);
        if (!bw.valid || open.empty() || rest.empty() || innWindows.size() != sc.windows || bw.strays != 0 || bw.windows.size() != innWindows.size() ||
            (sc.lookAt >= 0 && !lookAt))
        { Check(false, std::string(label) + ": the batch's windows (the model's)"); return; }
        // the lead mask
        std::string flat = texture;
        std::replace(flat.begin(), flat.end(), '\\', '#');
        const std::vector<uint8_t> blp = ReadBytes(std::filesystem::path(textures) / flat);
        std::vector<float> texRgb;
        std::vector<uint8_t> leadMask;
        uint32_t tw = 0, th = 0;
        if (!wxl_livingazeroth::blp::DecodeTop(blp.data(), blp.size(), texRgb, tw, th) || !gl::LeadMask(texRgb.data(), tw, th, m, LeadRuleOf(lt, texture), leadMask))
        { Check(false, std::string(label) + ": the lead mask"); return; }

        Warp w;
        if (!InitWarp(w, why)) { std::printf("skip  %s: %s\n", label, why.c_str()); return; }
        IDirect3DDevice9* d = w.dev;
        constexpr int kS = 192;
        IDirect3DPixelShader9 *ps = nullptr, *other = nullptr;
        IDirect3DSurface9 *target = nullptr, *depth = nullptr, *readback = nullptr;
        std::string blpError;
        IDirect3DTexture9* glassTex = wxl_livingazeroth::blp::LoadFromMemory(d, blp.data(), blp.size(), blpError);
        IDirect3DTexture9* white = rh::Texture(d, std::vector<uint32_t>(16, 0xFFFFFFFFu), 4, 4);
        // the sheets' backdrop: a warm room's checker (plaster and wood), so what the glass does to it shows
        std::vector<uint32_t> checker(64 * 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) checker[static_cast<size_t>(y) * 64 + x] = ((x / 8 + y / 8) & 1) ? 0xFFD8CCB4u : 0xFF7A5634u;
        IDirect3DTexture9* backTex = rh::Texture(d, checker, 64, 64);
        IDirect3DPixelShader9* tintShader = sc.tint > 0.0f ? st::CreateTintShader(d, why) : nullptr;
        std::vector<uint8_t> otherCode;
        if (!sp::Assemble("ps_3_0\n    def c0, 1, 0, 0, 1\n    mov oC0, c0\n", "another shader", otherCode, why) ||
            FAILED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps)) ||
            FAILED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(otherCode.data()), &other)) || !glassTex || !white || !backTex ||
            (sc.tint > 0.0f && !tintShader) ||
            FAILED(d->CreateRenderTarget(kS, kS, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr)) ||
            FAILED(d->CreateDepthStencilSurface(kS, kS, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &depth, nullptr)) ||
            FAILED(d->CreateOffscreenPlainSurface(kS, kS, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr)))
        { Check(false, std::string(label) + ": WARP resources " + why.substr(0, 120)); return; }
        IDirect3DTexture9* leadTex = nullptr;
        {
            UINT levels = 1;
            for (uint32_t sz = std::max(tw, th); sz > 1; sz >>= 1) ++levels;
            std::vector<uint8_t> level = leadMask, next;
            uint32_t lw = tw, lh = th;
            if (SUCCEEDED(d->CreateTexture(tw, th, levels, 0, D3DFMT_L8, D3DPOOL_MANAGED, &leadTex, nullptr)) && leadTex)
                for (UINT k = 0; k < levels; ++k)
                {
                    D3DLOCKED_RECT lr{};
                    if (FAILED(leadTex->LockRect(k, &lr, nullptr, 0))) break;
                    for (uint32_t y = 0; y < lh; ++y) std::memcpy(static_cast<uint8_t*>(lr.pBits) + static_cast<size_t>(y) * lr.Pitch, &level[static_cast<size_t>(y) * lw], lw);
                    leadTex->UnlockRect(k);
                    if (k + 1 < levels) { gl::LeadMip(level, lw, lh, next); level.swap(next); }
                }
        }
        // the placement (the inn's: Goldshire's MODF, turned -97 degrees, its origin in world yards) and the camera (the
        // inn's: the user's spot, looking at the arched window)
        const double yaw = sc.yaw * 3.14159265358979 / 180.0;
        const float axes[9] = { static_cast<float>(std::cos(yaw)), static_cast<float>(std::sin(yaw)), 0.0f,
                                static_cast<float>(-std::sin(yaw)), static_cast<float>(std::cos(yaw)), 0.0f, 0.0f, 0.0f, 1.0f };
        const float* const origin = sc.origin;
        auto world = [&](const float* l, float* out) { for (int k = 0; k < 3; ++k) out[k] = origin[k] + l[0] * axes[k] + l[1] * axes[3 + k] + l[2] * axes[6 + k]; };
        auto turn = [&](const float* l, float* out) { for (int k = 0; k < 3; ++k) out[k] = l[0] * axes[k] + l[1] * axes[3 + k] + l[2] * axes[6 + k]; };
        float eye[3], target9[3];
        world(sc.spot, eye);
        world(lookAt ? lookAt->center : sc.look, target9);
        float fwd[3] = { target9[0] - eye[0], target9[1] - eye[1], target9[2] - eye[2] };
        Norm(fwd);
        float right[3] = { fwd[1], -fwd[0], 0.0f };
        Norm(right);
        const float up[3] = { right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2], right[0] * fwd[1] - right[1] * fwd[0] };
        const float focal = sc.focal;
        // the vertex buffer: every vertex of the group as the client's vertex shader hands it on (view-space position and
        // normal, the uv); its index buffer: MOVI
        struct Vtx { float pos[4]; float col[4]; float tc[6][4]; float fog; };
        D3DVERTEXELEMENT9 el[10] = {
            { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITIONT, 0 }, { 0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
            { 0, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 }, { 0, 48, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },
            { 0, 64, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2 }, { 0, 80, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3 },
            { 0, 96, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 4 }, { 0, 112, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 5 },
            { 0, 128, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_FOG, 0 }, D3DDECL_END() };
        IDirect3DVertexDeclaration9* decl = nullptr;
        IDirect3DVertexBuffer9* vb = nullptr;
        IDirect3DIndexBuffer9 *ib = nullptr, *otherIb = nullptr;
        if (FAILED(d->CreateVertexDeclaration(el, &decl)) || FAILED(d->CreateVertexBuffer(nverts * sizeof(Vtx), 0, 0, D3DPOOL_MANAGED, &vb, nullptr)) ||
            FAILED(d->CreateIndexBuffer(static_cast<UINT>(grp.idx.size() * 2), 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib, nullptr)) ||
            FAILED(d->CreateIndexBuffer(64, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &otherIb, nullptr)))
        { Check(false, std::string(label) + ": its buffers"); return; }
        {
            Vtx* v = nullptr;
            vb->Lock(0, 0, reinterpret_cast<void**>(&v), 0);
            for (uint32_t i = 0; i < nverts; ++i)
            {
                float p[3], n[3];
                world(&grp.pos[3 * i], p);
                turn(&grp.nrm[3 * i], n);
                const float q[3] = { p[0] - eye[0], p[1] - eye[1], p[2] - eye[2] };
                const float vx = q[0] * right[0] + q[1] * right[1] + q[2] * right[2], vy = q[0] * up[0] + q[1] * up[1] + q[2] * up[2];
                const float ahead = q[0] * fwd[0] + q[1] * fwd[1] + q[2] * fwd[2], vz = std::max(0.1f, ahead);
                Vtx& o = v[i];
                o = Vtx{};
                // (behind the camera, from inside the windows at its back: past the depth, so the depth test drops them)
                o.pos[0] = kS * 0.5f + focal * vx / vz - 0.5f; o.pos[1] = kS * 0.5f - focal * vy / vz - 0.5f; o.pos[2] = ahead < 0.1f ? 2.0f : 0.5f;
                o.pos[3] = 1.0f / vz;
                for (float& c : o.col) c = 1.0f;
                o.fog = 1.0f;
                for (const auto& [reg, use] : usage)
                {
                    if (use.rfind("texcoord", 0) != 0 || use[8] - '0' > 5) continue;
                    float* tc = o.tc[use[8] - '0'];
                    if (reg == vP) { tc[0] = vx; tc[1] = vy; tc[2] = q[0] * fwd[0] + q[1] * fwd[1] + q[2] * fwd[2]; tc[3] = 1.0f; }
                    else if (reg == vN) { tc[0] = n[0] * right[0] + n[1] * right[1] + n[2] * right[2]; tc[1] = n[0] * up[0] + n[1] * up[1] + n[2] * up[2];
                                          tc[2] = n[0] * fwd[0] + n[1] * fwd[1] + n[2] * fwd[2]; }
                    else if (reg == vT) { tc[0] = grp.uv[2 * i]; tc[1] = grp.uv[2 * i + 1]; }
                    else { tc[0] = tc[1] = 2.0f; tc[2] = 0.5f; tc[3] = 1.0f; }
                }
            }
            vb->Unlock();
            void* p = nullptr;
            ib->Lock(0, 0, &p, 0);
            std::memcpy(p, grp.idx.data(), grp.idx.size() * 2);
            ib->Unlock();
        }
        // Glass's constants for the batch, its own way: the look (by day, or at night: the sheets'), the room's registers
        // over its window table (the real RoomRegisters with the placement and the camera), the view (c180: the eye;
        // c181..c183: the view's rows)
        rh::Rooms rooms;
        rooms.Textures(d, textures, 0);
        rooms.Table(d, all);
        gl::Settings s;
        gl::Inputs in;
        in.sun[0] = -0.4f; in.sun[1] = 0.3f; in.sun[2] = 0.86f; Norm(in.sun);
        float c[4 * gl::kConstantCount], seed[4 * gl::kSeedRegisters], look[4], day[4 * gl::kDayRegisters], regs[4 * gl::kRoomRegisters];
        gl::RoomInputs ri;
        ri.unlit = true; ri.kind = m.kind;
        const gl::RoomStyle style;
        auto setTime = [&](bool night) {
            in.daylight = night ? 0.0f : 1.0f; in.night = night ? 1.0f : 0.0f;
            in.sky[0] = night ? 0.06f : 0.55f; in.sky[1] = night ? 0.08f : 0.65f; in.sky[2] = night ? 0.14f : 0.8f;
            const gl::Light light = gl::Evaluate(in);
            gl::BatchConstants(m, inside ? 1.0f : 0.0f, light, in.sun, in.sunColour, in.sky, s, c);
            ri.night = night ? 1.0f : 0.0f; ri.hour = night ? 23.0f : 9.5f;
            gl::RoomBatchConstants(style, nullptr, light, ri, s, c, seed, look);
            gl::DayConstants(m, light, s, 0, day);
        };
        setTime(false);
        // (a stained batch's windows in its group's stained grid, as features/Glass finds them)
        gl::RoomRegisters(axes, origin, eye, rooms.table.Find(innPath, static_cast<uint32_t>(sc.group), m.kind == gl::kStained), rooms.table.Height(), style.cls, rooms.scales,
                          rooms.gains, s, regs, m.kind, m.notGlass);
        const float view[16] = { eye[0], eye[1], eye[2], 0, right[0], up[0], fwd[0], 0, right[1], up[1], fwd[1], 0, right[2], up[2], fwd[2], 0 };
        st::DrawArgs args;
        args.primitive = D3DPT_TRIANGLELIST; args.baseVertex = 0; args.minIndex = grp.minIndex; args.vertices = grp.maxIndex - grp.minIndex + 1;
        args.startIndex = grp.start; args.primitives = grp.count / 3;
        // One run: the client's draw (colour writes off), then (`through`) the capture, the clobbering and the apply with or
        // without the constants put back, else right away; the redraw split or whole, debug view or plain, tinted or not
        // (`tint`: the tint pass at that strength; the stencil filled with ones first, so its clearing under the batch is
        // what keeps the shut windows untinted); the target's pixels (BGRA) over the backdrop (blue; `checker`: the room's
        // checker). `lost`: the pixel constants after the apply unlike at the draw; `tinted`: the tint as drawn.
        enum Through { kRightAway, kStateBlockAlone, kWithConstants };
        st::SplitBuffer buf;
        std::string lost;
        int tinted = 0;
        auto run = [&](Through through, bool split, bool debug, std::vector<uint32_t>& img, D3DCOLOR backdrop = D3DCOLOR_ARGB(255, 0, 0, 255),
                       float tint = 0.0f, bool checkerBack = false) -> bool {
            d->SetRenderTarget(0, target);
            d->SetDepthStencilSurface(depth);
            D3DVIEWPORT9 vp{ 0, 0, kS, kS, 0.0f, 1.0f };
            d->SetViewport(&vp);
            d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, backdrop, 1.0f, 0xFF);
            if (FAILED(d->BeginScene())) return false;
            if (checkerBack)
            {
                struct Q { float x, y, z, rhw, u, v; };
                const float e = kS - 0.5f;
                const Q quad[4] = { { -0.5f, -0.5f, 0.99f, 1, 0, 0 }, { e, -0.5f, 0.99f, 1, 3, 0 }, { -0.5f, e, 0.99f, 1, 0, 3 }, { e, e, 0.99f, 1, 3, 3 } };
                d->SetVertexShader(nullptr); d->SetPixelShader(nullptr); d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
                d->SetTexture(0, backTex);
                d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
                d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP); d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
                d->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE); d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
                d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF); d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
                d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Q));
            }
            // the client's draw
            d->SetVertexShader(nullptr); d->SetPixelShader(ps); d->SetVertexDeclaration(decl);
            d->SetStreamSource(0, vb, 0, sizeof(Vtx)); d->SetIndices(ib);
            d->SetTexture(0, glassTex);
            for (DWORD k = 4; k <= 7; ++k) d->SetTexture(k, white);
            rooms.Bind(d);
            for (DWORD k : { 0u, 8u, 9u, 10u, 11u, 12u })
            {
                d->SetSamplerState(k, D3DSAMP_MINFILTER, D3DTEXF_LINEAR); d->SetSamplerState(k, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
                d->SetSamplerState(k, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
            }
            d->SetSamplerState(13, D3DSAMP_MINFILTER, D3DTEXF_POINT); d->SetSamplerState(13, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            d->SetSamplerState(13, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            d->SetSamplerState(13, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); d->SetSamplerState(13, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            float stock[4 * 13];
            for (float& x : stock) x = 0.5f;
            stock[8] = stock[9] = stock[10] = stock[11] = 0.0f;
            d->SetPixelShaderConstantF(0, stock, 13);
            d->SetPixelShaderConstantF(gl::kViewConstant, view, 4);
            d->SetPixelShaderConstantF(gl::kFirstConstant, c, gl::kConstantCount);
            d->SetPixelShaderConstantF(gl::kSeedConstant, seed, gl::kSeedRegisters);
            d->SetPixelShaderConstantF(gl::kLookConstant, look, gl::kLookRegisters);
            d->SetPixelShaderConstantF(gl::kDayConstant, day, gl::kDayRegisters);
            d->SetPixelShaderConstantF(gl::kRoomConstant, regs, gl::kRoomRegisters);
            const BOOL on = TRUE, off = FALSE;
            for (UINT b = 0; b < 16; ++b) d->SetPixelShaderConstantB(b, &off, 1);
            d->SetPixelShaderConstantB(gl::kSwitch, &on, 1);
            d->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE); d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL); d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE); d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE); d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            d->SetRenderState(D3DRS_FOGENABLE, FALSE); d->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
            d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
            d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, args.minIndex, args.vertices, args.startIndex, args.primitives);
            // the capture
            st::Deferral::Entry e;
            e.args = args; e.first = first; e.bits = bits; e.open = open; e.rest = rest;
            if (!st::CaptureState(d, e)) { d->EndScene(); return false; }
            if (through != kRightAway)
            {
                // the rest of the frame (the model scene's first pass): another shader, every constant and sampler cleared,
                // another index buffer
                std::vector<float> zeros(4 * st::kPixelConstants, 0.0f);
                d->SetPixelShader(other);
                d->SetPixelShaderConstantF(0, zeros.data(), st::kPixelConstants);
                for (UINT b = 0; b < 16; ++b) { d->SetPixelShaderConstantB(b, &off, 1); d->SetTexture(b, nullptr); }
                d->SetIndices(otherIb);
                d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
                // the redraw's: the state applied (with its constants or the state block alone)
                st::ApplyCaptured(d, e, through == kWithConstants);
                std::vector<float> now(4 * st::kPixelConstants);
                d->GetPixelShaderConstantF(0, now.data(), st::kPixelConstants);
                lost.clear();
                for (unsigned r = 0; r < st::kPixelConstants; ++r)
                    if (std::memcmp(&now[4 * r], &e.ps[4 * r], 16) != 0)
                    {
                        unsigned last = r;
                        while (last + 1 < st::kPixelConstants && std::memcmp(&now[4 * (last + 1)], &e.ps[4 * (last + 1)], 16) != 0) ++last;
                        lost += (lost.empty() ? "c" : ", c") + std::to_string(r) + (last > r ? "..c" + std::to_string(last) : std::string());
                        r = last;
                    }
            }
            // its writes, its draws
            d->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
            d->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
            st::PerPixel pp;
            pp.opacity = 0.35f; pp.debug = debug; pp.lead = leadTex; pp.split = split;
            pp.tintShader = tint > 0.0f ? tintShader : nullptr; pp.tint = tint; pp.stencil = true;
            const st::PerPixelDrawn drawn = st::DrawPerPixel(d, e, pp, buf);
            tinted = drawn.tinted;
            d->EndScene();
            static_cast<IDirect3DStateBlock9*>(e.state)->Release();
            D3DLOCKED_RECT lr{};
            if (!drawn.drawn || drawn.split != split || FAILED(d->GetRenderTargetData(target, readback)) || FAILED(readback->LockRect(&lr, nullptr, D3DLOCK_READONLY)))
                return false;
            img.resize(static_cast<size_t>(kS) * kS);
            for (int row = 0; row < kS; ++row) std::memcpy(&img[static_cast<size_t>(row) * kS], static_cast<const uint8_t*>(lr.pBits) + row * lr.Pitch, kS * 4);
            readback->UnlockRect();
            return true;
        };
        // a debug-view pixel: 0 not drawn (the room's blue), 1 an open window's (magenta by its mask to green), 2 a shut one's
        // (cyan), 3 no window found (yellow)
        auto kind = [](uint32_t px) {
            const int r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, b = px & 0xFF;
            if (r == 0 && g == 0 && b == 255) return 0;
            if (r > 200 && g > 200 && b < 60) return 3;
            if (r < 60 && g > 200 && b > 200) return 2;
            return 1;
        };
        std::vector<uint32_t> a, broken, fixed, fixedSplit, aloneSplit, plain, plainRed;
        std::string lostAlone, lostFixed;
        bool ok = run(kRightAway, false, true, a);
        ok = ok && run(kStateBlockAlone, false, true, broken);
        lostAlone = lost;
        ok = ok && run(kWithConstants, false, true, fixed);
        lostFixed = lost;
        ok = ok && run(kWithConstants, true, true, fixedSplit) && run(kStateBlockAlone, true, true, aloneSplit) && run(kWithConstants, false, false, plain) &&
             run(kWithConstants, false, false, plainRed, D3DCOLOR_ARGB(255, 255, 0, 0));
        int count[5][4] = {}, differ = 0, differSplit = 0, differAlone = 0;
        for (size_t p = 0; ok && p < a.size(); ++p)
        {
            const int ka = kind(a[p]);
            ++count[0][ka];
            ++count[1][kind(broken[p])];
            ++count[2][kind(fixed[p])];
            ++count[3][kind(fixedSplit[p])];
            ++count[4][kind(aloneSplit[p])];
            differ += ka != kind(fixed[p]) ? 1 : 0;
            // (a stray triangle, in no window: no window found drawn whole, shut drawn split)
            differSplit += ka != kind(fixedSplit[p]) && !(ka == 3 && kind(fixedSplit[p]) == 2) ? 1 : 0;
            differAlone += ka != kind(aloneSplit[p]) && !(ka == 3 && kind(aloneSplit[p]) == 2) ? 1 : 0;
        }
        char line[700];
        std::snprintf(line, sizeof(line), "%s (placed turned %.0f degrees, the camera turned, Glass's RoomRegisters): (a) the redraw "
                      "right after the client's draw: %d open, %d shut, %d no window (pixels; the batch's stray triangles, in no window: %u)", label, sc.yaw,
                      count[0][1], count[0][2], count[0][3], bw.strays);
        Check(ok && count[0][1] > sc.minOpen && count[0][2] > sc.minShut && count[0][3] <= (bw.strays ? 40 : 0), line);
        const int drawnPx = count[0][1] + count[0][2] + count[0][3];
        std::snprintf(line, sizeof(line), "%s: (b) captured, clobbered, applied through the state block alone (as before): %d no window, %d open, %d "
                      "shut (the game's screenshot: every pixel yellow); the pixel constants it didn't put back: %s", label, count[1][3], count[1][1], count[1][2],
                      lostAlone.c_str());
        Check(ok && count[1][3] > std::min(400, drawnPx * 9 / 10) && count[1][1] == 0 && count[1][2] == 0 && lostAlone.find("c180..c183") != std::string::npos, line);
        std::snprintf(line, sizeof(line), "%s: (c) with every constant put back from the capture (none unlike: %s): drawn whole %d open, %d shut, %d "
                      "no window, %d pixels decided otherwise than (a); split by window (%zu triangles open, %zu the rest) %d otherwise; split through the state "
                      "block alone (the lookup's inputs lost) %d otherwise", label, lostFixed.empty() ? "yes" : lostFixed.c_str(), count[2][1], count[2][2],
                      count[2][3], differ, open.size() / 3, rest.size() / 3, differSplit, differAlone);
        // (split: a pixel at a window's edge goes by its triangle's middle, the lookup by the pixel: a few pixels apart)
        Check(ok && lostFixed.empty() && differ == 0 && differSplit * 100 <= drawnPx && differAlone * 100 <= drawnPx, line);
        // (d) the plain view with the constants put back, drawn whole: what's behind (the backdrop) through the open windows'
        // glass (its debug tint mostly magenta: green under 100; in 8 of 10, where it's part glass the glass keeps more), not
        // their lead and frame (opaque green), not the shut windows (a pixel at a window's edge aside). A pixel shows what's
        // behind when it changes with the backdrop (blue, then red; from inside the glass's own look is sky-blue by day)
        int glassBlue = 0, glassPx = 0, leadBlue = 0, leadPx = 0, shutBlue = 0, shutPx = 0;
        for (size_t p = 0; ok && p < plain.size(); ++p)
        {
            const int k = kind(a[p]), red = (a[p] >> 16) & 0xFF, green = (a[p] >> 8) & 0xFF;
            const int dr = static_cast<int>((plainRed[p] >> 16) & 0xFF) - static_cast<int>((plain[p] >> 16) & 0xFF);
            const int db = static_cast<int>(plain[p] & 0xFF) - static_cast<int>(plainRed[p] & 0xFF);
            const bool blue = dr > 40 && db > 40;
            if (k == 1 && green < 100) { ++glassPx; glassBlue += blue ? 1 : 0; }
            if (k == 1 && green == 255 && red == 0) { ++leadPx; leadBlue += blue ? 1 : 0; }
            if (k == 2) { ++shutPx; shutBlue += blue ? 1 : 0; }
        }
        std::snprintf(line, sizeof(line), "%s: (d) the plain view: %s through %d of %d open windows' glass pixels, %d of %d of their lead "
                      "and frame, %d of %d shut windows'", label, behind, glassBlue, glassPx, leadBlue, leadPx, shutBlue, shutPx);
        Check(ok && glassPx > sc.minOpen / 2 && glassBlue * 10 >= glassPx * 8 && leadBlue * 100 <= leadPx && shutBlue * 100 <= shutPx, line);
        // (e) stained glass's tint (2026-10-22: "the coloured glass should stay coloured over the room (tint, not just
        // blend)"): over a grey backdrop, tinted against not, drawn whole (through the stencil, filled with ones first) and
        // split: the open windows' glass pixels take the glass's colour (most of them changed, and more coloured: the spread
        // between their channels wider); their lead and frame and the shut windows not a level apart; whole and split alike
        // but for the windows' edges
        std::vector<uint32_t> grey, greyTint, greyTintSplit, sheetDay, sheetNight;
        if (sc.tint > 0.0f)
        {
            const D3DCOLOR mid = D3DCOLOR_ARGB(255, 200, 200, 200);
            int tintWhole = 0, tintSplit = 0;
            bool tok = ok && run(kWithConstants, false, false, grey, mid);
            tok = tok && run(kWithConstants, false, false, greyTint, mid, sc.tint);
            tintWhole = tinted;
            tok = tok && run(kWithConstants, true, false, greyTintSplit, mid, sc.tint);
            tintSplit = tinted;
            int changed = 0, glass = 0, moved = 0, kept = 0, splitApart = 0;
            double spreadBefore = 0, spreadAfter = 0;
            auto ch = [](uint32_t px, int k) { return static_cast<int>((px >> (16 - 8 * k)) & 0xFF); };
            for (size_t p = 0; tok && p < a.size(); ++p)
            {
                const int k = kind(a[p]), red = (a[p] >> 16) & 0xFF, green = (a[p] >> 8) & 0xFF;
                int most = 0;
                for (int q = 0; q < 3; ++q) most = std::max(most, std::abs(ch(grey[p], q) - ch(greyTint[p], q)));
                if (k == 1 && green < 100)
                {
                    ++glass;
                    changed += most > 6 ? 1 : 0;
                    auto spread = [&](uint32_t px) { return std::max({ ch(px, 0), ch(px, 1), ch(px, 2) }) - std::min({ ch(px, 0), ch(px, 1), ch(px, 2) }); };
                    spreadBefore += spread(grey[p]);
                    spreadAfter += spread(greyTint[p]);
                }
                if ((k == 1 && green == 255 && red == 0) || k == 2) { ++kept; moved += most > 1 ? 1 : 0; }
                int apart = 0;
                for (int q = 0; q < 3; ++q) apart = std::max(apart, std::abs(ch(greyTint[p], q) - ch(greyTintSplit[p], q)));
                splitApart += apart > 6 ? 1 : 0;
            }
            std::snprintf(line, sizeof(line), "%s: (e) the tint (%.1f): %d of %d open windows' glass pixels changed, their channels' spread %.0f -> %.0f; %d of "
                          "%d lead, frame and shut-window pixels moved; drawn %s and %s; whole and split %d pixels apart", label, sc.tint, changed, glass,
                          glass ? spreadBefore / glass : 0.0, glass ? spreadAfter / glass : 0.0, moved, kept,
                          tintWhole == 2 ? "whole through the stencil" : "whole NOT tinted", tintSplit == 1 ? "split over the open windows" : "split NOT tinted", splitApart);
            Check(tok && glass > 50 && changed * 10 >= glass * 7 && spreadAfter > spreadBefore * 1.2 && moved == 0 && kept > 50 && tintWhole == 2 &&
                  tintSplit == 1 && splitApart * 100 <= drawnPx * 2, line);
        }
        if (const char* dump = std::getenv("GLASS_DUMP"))
        {
            auto save = [&](const std::string& name, const std::vector<uint32_t>& im) {
                std::vector<uint8_t> rgb(im.size() * 3);
                for (size_t p = 0; p < im.size(); ++p) { rgb[3 * p] = (im[p] >> 16) & 0xFF; rgb[3 * p + 1] = (im[p] >> 8) & 0xFF; rgb[3 * p + 2] = im[p] & 0xFF; }
                WritePnm(std::filesystem::path(dump) / name, kS, kS, 3, rgb);
            };
            const std::string pre = sc.prefix;
            save(pre + "a_right_away_debug.ppm", a);
            save(pre + "b_state_block_alone_debug.ppm", broken);
            save(pre + "c_with_constants_debug.ppm", fixed);
            save(pre + "d_with_constants_plain.ppm", plain);
            // the sheets: over the room's checker, by day and at night, the debug view and the plain (tinted, if it's stained)
            std::vector<uint32_t> im;
            for (const bool night : { false, true })
            {
                setTime(night);
                const std::string when = night ? "night" : "day";
                if (run(kWithConstants, false, true, im, D3DCOLOR_ARGB(255, 0, 0, 255), 0.0f, true)) save(pre + "sheet_" + when + "_debug.ppm", im);
                if (run(kWithConstants, false, false, im, D3DCOLOR_ARGB(255, 0, 0, 255), sc.tint, true)) save(pre + "sheet_" + when + "_plain.ppm", im);
                if (sc.tint > 0.0f && run(kWithConstants, false, false, im, D3DCOLOR_ARGB(255, 0, 0, 255), 0.0f, true)) save(pre + "sheet_" + when + "_untinted.ppm", im);
            }
            setTime(false);
        }
        buf.Release();
        for (IUnknown* u : std::initializer_list<IUnknown*>{ ps, other, target, depth, readback, glassTex, white, backTex, tintShader, leadTex, decl, vb, ib, otherIb })
            if (u) u->Release();
    }

    void SequenceTests(const std::filesystem::path& shaders, const std::filesystem::path& data, bool inside = false)
    {
        SequenceScene(shaders, data, InnScene(inside));
    }

    // --- 14. stained glass's tint pass on WARP (2026-10-22): its own pixel shader (SeeThroughDraw's TintShaderText) over a
    // backdrop, ZERO / SRCCOLOR as the redraw blends it: each pixel the backdrop x TintOf(the texel, the strength, the mask)
    // (its CPU twin) to a level; with c223.y 0, the backdrop as it was; the tint's constant put back after a TintDraw-like
    // use is the caller's (the redraw's); HasStencil on the depth formats.
    void TintTests()
    {
        namespace st = wxl_livingazeroth::seethrough;
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("skip  the tint pass: %s\n", why.c_str()); return; }
        IDirect3DDevice9* d = w.dev;
        IDirect3DPixelShader9* ps = st::CreateTintShader(d, why);
        if (!ps) { Check(false, "the tint pass's shader assembles: " + why.substr(0, 200)); return; }
        // eight texels (red, green, blue, gold, white, grey, deep blue, orange) and their masks (glass, lead, half)
        const uint32_t texels[8] = { 0xFFD02020u, 0xFF20C040u, 0xFF3040E0u, 0xFFE0B020u, 0xFFF0F0F0u, 0xFF808080u, 0xFF101860u, 0xFFF08020u };
        const uint8_t masks[8] = { 255, 255, 255, 255, 255, 0, 128, 255 };
        IDirect3DTexture9* tex = rh::Texture(d, std::vector<uint32_t>(texels, texels + 8), 8, 1);
        IDirect3DTexture9* lead = nullptr;
        constexpr int kW = 64, kH = 8;
        IDirect3DSurface9 *target = nullptr, *readback = nullptr;
        if (!tex || FAILED(d->CreateTexture(8, 1, 1, 0, D3DFMT_L8, D3DPOOL_MANAGED, &lead, nullptr)) ||
            FAILED(d->CreateRenderTarget(kW, kH, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr)) ||
            FAILED(d->CreateOffscreenPlainSurface(kW, kH, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr)))
        { Check(false, "the tint pass: WARP resources"); return; }
        D3DLOCKED_RECT lr{};
        lead->LockRect(0, &lr, nullptr, 0);
        std::memcpy(lr.pBits, masks, 8);
        lead->UnlockRect(0);
        const float backdrop[3] = { 200.0f / 255, 180.0f / 255, 150.0f / 255 };
        auto draw = [&](float strength, float on, std::vector<uint32_t>& out) -> bool {
            d->SetRenderTarget(0, target);
            d->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 200, 180, 150), 1.0f, 0);
            if (FAILED(d->BeginScene())) return false;
            struct Q { float x, y, z, rhw, u, v; };
            const Q quad[4] = { { -0.5f, -0.5f, 0.5f, 1, 0, 0.5f }, { kW - 0.5f, -0.5f, 0.5f, 1, 1, 0.5f }, { -0.5f, kH - 0.5f, 0.5f, 1, 0, 0.5f },
                                { kW - 0.5f, kH - 0.5f, 0.5f, 1, 1, 0.5f } };
            d->SetVertexShader(nullptr); d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1); d->SetPixelShader(ps);
            d->SetTexture(0, tex); d->SetTexture(15, lead);
            for (DWORD s : { 0u, 15u })
            {
                d->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_POINT); d->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
                d->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
                d->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); d->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            }
            const float c[4] = { strength, on, 0, 0 };
            d->SetPixelShaderConstantF(st::kTintConstant, c, 1);
            d->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE); d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE); d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO); d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
            d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
            d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Q));
            d->EndScene();
            D3DLOCKED_RECT r{};
            if (FAILED(d->GetRenderTargetData(target, readback)) || FAILED(readback->LockRect(&r, nullptr, D3DLOCK_READONLY))) return false;
            out.assign(static_cast<const uint32_t*>(r.pBits), static_cast<const uint32_t*>(r.pBits) + kW);   // (its first row)
            readback->UnlockRect();
            return true;
        };
        std::vector<uint32_t> tinted, off;
        bool ok = draw(0.8f, 1.0f, tinted) && draw(0.8f, 0.0f, off);
        int worst = 0, unchanged = 0;
        for (int x = 0; ok && x < kW; ++x)
        {
            const int i = x * 8 / kW;
            const float rgb[3] = { ((texels[i] >> 16) & 0xFF) / 255.0f, ((texels[i] >> 8) & 0xFF) / 255.0f, (texels[i] & 0xFF) / 255.0f };
            float t[3];
            st::TintOf(rgb, 0.8f, masks[i] / 255.0f, t);
            for (int k = 0; k < 3; ++k)
            {
                const int want = static_cast<int>(std::lround(backdrop[k] * t[k] * 255.0f));
                const int got = static_cast<int>((tinted[x] >> (16 - 8 * k)) & 0xFF), was = static_cast<int>((off[x] >> (16 - 8 * k)) & 0xFF);
                worst = std::max(worst, std::abs(want - got));
                unchanged += std::abs(was - static_cast<int>(std::lround(backdrop[k] * 255.0f))) <= 1 ? 1 : 0;
            }
        }
        float red[3], leadT[3];
        const float r0[3] = { 0.8f, 0.1f, 0.1f };
        st::TintOf(r0, 0.8f, 1.0f, red);
        st::TintOf(r0, 0.8f, 0.0f, leadT);
        char line[300];
        std::snprintf(line, sizeof(line), "the tint pass on WARP: the backdrop x TintOf to %d levels over eight texels (glass, lead, half); off (c223.y 0) "
                      "%d of %d channels as they were; red glass lets through %.2f %.2f %.2f, its lead all; a stencil in D24S8 %s, in D24X8 %s", worst,
                      unchanged, 3 * kW, red[0], red[1], red[2], st::HasStencil(D3DFMT_D24S8) ? "yes" : "no", st::HasStencil(D3DFMT_D24X8) ? "yes" : "no");
        Check(ok && worst <= 2 && unchanged == 3 * kW && red[0] == 1.0f && red[1] < 0.35f && leadT[0] == 1.0f && leadT[1] == 1.0f &&
              st::HasStencil(D3DFMT_D24S8) && !st::HasStencil(D3DFMT_D24X8), line);
        for (IUnknown* u : std::initializer_list<IUnknown*>{ ps, tex, lead, target, readback }) if (u) u->Release();
    }

    std::string LitProbeShader(const std::string& probe)
    {
        return "ps_3_0\n    def c175, 1, 0, 0.5, 2\n    def c176, 0.2126, 0.7152, 0.0722, 0.2\n" + gl::ShaderDefs() +
               "    dcl_texcoord0 v0.xyz\n    dcl_texcoord1 v1.xyz\n    dcl_texcoord2 v2.xy\n    dcl_texcoord3 v3.xyz\n    dcl_2d s0\n"
               "    mov r0, c175.y\n    mov r0.z, -c175.x\n    mov r12.xyz, v3\n    mov r31.xyz, c175.y" + gl::GlassBlock("v0", "v1", "v2", probe) +
               "\n    mov oC0, r0\n";
    }

    struct Band { const char* name; gl::DarkBand band; };
    gl::DarkBand PickBand(int win, float r1, float r2)
    {
        switch (win % 8)
        {
        case 0: return gl::Band(r1, r1);                                           // at rest
        case 1: return gl::Band(r1 + 6e-5f, r1);                                   // the clock, going dark
        case 2: { const float t = r1 * 0.7f; return gl::Band(t, t + 0.25f); }      // a fast clock, lit again
        case 3: { const float t = r1 * 0.5f; return gl::Band(t + 0.4f, t); }       // ... going dark
        case 4: { const float t = r1 * 0.3f; return gl::Crossfade(t, t + 0.5f, r2); } // a jump, going dark
        case 5: { const float t = r1 * 0.3f; return gl::Crossfade(t + 0.5f, t, r2); } // a jump, lit again
        case 6: return gl::Band(1.0f, 1.0f);                                       // a dark place
        default: return gl::Band(0.0f, 0.0f);                                      // every room lit
        }
    }

    void LitAgreementTests()
    {
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("skip  lit agreement: %s\n", why.c_str()); return; }
        const std::string probe = "\n          mov r0.x, r14.z\n          mov r0.y, r22.x\n          mov r0.z, r13.w\n          mov r0.w, r20.w";
        std::vector<uint8_t> code;
        if (!wxl_livingazeroth::shaderpatch::Assemble(LitProbeShader(probe), "lit probe", code, why)) { Check(false, "the lit probe assembles: " + why.substr(0, 300)); return; }
        IDirect3DPixelShader9* ps = nullptr;
        constexpr int kS = 96;
        IDirect3DSurface9 *target = nullptr, *readback = nullptr;
        if (FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps)) ||
            FAILED(w.dev->CreateRenderTarget(kS, kS, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr)) ||
            FAILED(w.dev->CreateOffscreenPlainSurface(kS, kS, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &readback, nullptr)))
        { Check(false, "the lit probe on WARP"); return; }
        rh::Rooms rooms;
        rooms.Textures(w.dev);
        float look[4];
        gl::LookConstants(gl::Light{}, look);
        w.dev->SetPixelShaderConstantF(gl::kLookConstant, look, gl::kLookRegisters);
        // Places: about the world's origin, Goldshire, Stormwind, Northrend's far corner. Each a window: a random facing (a
        // little tilt), 1.5 .. 5 yd wide, 1.2 .. 4 tall, 6 .. 45 yd from the camera; its key (an ordinal, a group), a seed, a
        // band. Every fourth is a light over two windows side by side with a mullion between them (not glass in the table:
        // its share stays lit, as the window lights' tool has it).
        struct Place { const char* name; float x, y, z; };
        const Place places[] = { { "the origin", 0, 0, 0 }, { "Goldshire", -9460, 62, 57 }, { "Stormwind", -8838.6f, 627.4f, 99.3f },
                                 { "Northrend's far side", 5800, 600, 640 } };
        // Each kind's rooms: clear glass's, stained glass's (its windows in grids of their own) and frosted glass's (its
        // windows' rooms allowed, c26.y), each with its batch's registers.
        gl::Settings kinds;
        kinds.frostedRooms = kinds.stainedRooms = 1;
        for (const uint32_t kind : { static_cast<uint32_t>(gl::kClear), static_cast<uint32_t>(gl::kStained), static_cast<uint32_t>(gl::kFrosted) })
        {
        uint32_t rng = 12345;
        auto rnd = [&rng]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };
        int windows = 0, missing = 0, keyOff = 0, hashOff = 0, litOff = 0, partLit = 0, wobbleOff = 0, pairs = 0;
        long long pixels = 0;
        float worstShare = 0, sumShare = 0;
        bool drewAll = true;
        std::string worstAt;
        for (const Place& pl : places)
            for (int win = 0; win < 24; ++win)
            {
                const float yaw = rnd() * 6.2831853f, pitch = (rnd() - 0.5f) * 0.3f;
                float n[3] = { std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch) };
                Norm(n);
                const float hw = 0.75f + rnd() * 1.75f, hh = 0.6f + rnd() * 1.4f, dist = 6.0f + rnd() * 39.0f;
                const float mid[3] = { pl.x + (rnd() - 0.5f) * 60.0f, pl.y + (rnd() - 0.5f) * 60.0f, pl.z + rnd() * 12.0f };
                const uint32_t group = static_cast<uint32_t>(rnd() * 300.0f), ordinal = static_cast<uint32_t>(rnd() * 2000.0f);
                const float seed[2] = { std::floor(rnd() * 1024.0f), std::floor(rnd() * 1024.0f) };
                const gl::DarkBand band = PickBand(win, rnd(), rnd());
                const bool pair = win % 4 == 3;
                float a[3], u[3];
                gl::WindowFrame(n, a, u);
                std::vector<gl::Window> ws;
                const float mullion = 0.3f;
                if (!pair) ws.push_back(rh::MakeWindow(kSecondPath, group, ordinal, mid, n, hw, hh, kind));
                else
                {
                    // two windows, each (hw - mullion / 2) / 2 wide... the pair spans the quad's width
                    const float half = 0.5f * (hw - 0.5f * mullion);
                    for (int sd = -1; sd <= 1; sd += 2)
                    {
                        const float m3[3] = { mid[0] + a[0] * sd * (half + 0.5f * mullion), mid[1] + a[1] * sd * (half + 0.5f * mullion), mid[2] + a[2] * sd * (half + 0.5f * mullion) };
                        ws.push_back(rh::MakeWindow(kSecondPath, group, ordinal + (sd > 0 ? 1u : 0u), m3, n, half, hh, kind));
                    }
                    ++pairs;
                }
                rooms.Table(w.dev, ws);
                const float side = (rnd() - 0.5f) * 0.6f, lift = (rnd() - 0.5f) * 0.4f;
                const float cam[3] = { mid[0] + (n[0] + a[0] * side + u[0] * lift) * dist, mid[1] + (n[1] + a[1] * side + u[1] * lift) * dist,
                                       mid[2] + (n[2] + a[2] * side + u[2] * lift) * dist };
                // the constants: outside, the room on, the band, the seed
                float c[4 * gl::kConstantCount] = {};
                c[3] = 1; c[5] = 1; c[6] = 0.65f;
                c[27] = 0.3f; c[28] = 0.85f; c[29] = 0.72f; c[30] = 0.55f; c[40] = 0.85f; c[41] = 0.72f; c[42] = 0.55f;
                c[35] = band.hi; c[47] = band.lo; c[51] = 1;
                const float sd[4 * gl::kSeedRegisters] = { seed[0], seed[1], 0, band.slope, 0, 0, 0, band.base };
                float view[16] = { cam[0], cam[1], cam[2], 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
                w.dev->SetPixelShaderConstantF(gl::kViewConstant, view, 4);
                w.dev->SetPixelShaderConstantF(gl::kSeedConstant, sd, gl::kSeedRegisters);
                w.dev->SetPixelShaderConstantF(gl::kFirstConstant, c, gl::kConstantCount);
                float regs[4 * gl::kRoomRegisters];
                rooms.Registers(cam, kSecondPath, group, gl::kClassHouse, kinds, regs, false, kind);
                w.dev->SetPixelShaderConstantF(gl::kRoomConstant, regs, gl::kRoomRegisters);
                rooms.Bind(w.dev);
                // the quad: the window (the pair: both and the mullion), a hair inside its edges
                const gl::Window whole = rh::MakeWindow(kSecondPath, group, ordinal, mid, n, hw, hh);
                float rel[4][3], N[3];
                WindowQuad(whole, cam, rel, N, 0.995f);
                // Twice: the vertex normal flat, then wobbled 8 degrees at the corners (a smoothed mesh): the same
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
                        v[k] = { xy[k][0], xy[k][1], 0, 1, { rel[k][0], rel[k][1], rel[k][2] }, { nn[0], nn[1], nn[2] }, { 0.5f, 0.5f }, { 0.5f, 0.4f, 0.3f } };
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
                    for (size_t i = 0; i < px.size(); ++i)
                        if (Bits(px[i]) != Bits(first[i])) { ++wobbleOff; break; }
                }
                if (first.empty()) continue;
                ++windows;
                // Pixel by pixel: the window found (in the pair the mullion none: lit), its key, the hash's and the lit
                // value's bits against the CPU's.
                double gpuSum = 0;
                int inA = 0, inB = 0, inNone = 0;
                for (int row = 0; row < kS; ++row)
                    for (int col = 0; col < kS; ++col)
                    {
                        const float* g = &first[(static_cast<size_t>(row) * kS + col) * 4];
                        ++pixels;
                        gpuSum += g[2] < 0 ? 1.0 : g[0];
                        const float fu = (col + 0.5f) / kS, fv = (row + 0.5f) / kS;
                        float P[3];
                        for (int i = 0; i < 3; ++i) P[i] = rel[0][i] + (rel[1][i] - rel[0][i]) * fu + (rel[2][i] - rel[0][i]) * fv + cam[i];
                        const gl::WindowTable::Grid* gr = rooms.table.Find(kSecondPath, group, kind == gl::kStained);
                        const int e = gr ? rooms.table.Lookup(*gr, P) : -1;
                        const int cpuKey = e < 0 ? -1 : static_cast<int>(rooms.table.Texel(e + 3)[3]);
                        if (g[2] < 0) { ++inNone; if (cpuKey >= 0 && !pair) ++missing; continue; }
                        (static_cast<uint32_t>(g[2]) == ordinal ? inA : inB)++;
                        if (static_cast<int>(g[2]) != cpuKey || static_cast<uint32_t>(g[3]) != group)
                        {
                            // a pixel a hair from a window's edge (in the pair, the mullion's) may land either side
                            if (!pair) ++keyOff;
                            continue;
                        }
                        const float h = gl::LitHash(g[2], g[3], seed[0], seed[1]);
                        hashOff += Bits(h) != Bits(g[1]) ? 1 : 0;
                        litOff += Bits(gl::RoomLit(h, band)) != Bits(g[0]) ? 1 : 0;
                        partLit += g[0] > 0.0f && g[0] < 1.0f ? 1 : 0;
                    }
                // The window light's share (its windows' shares of the glass, each x its room's lit value, the rest lit)
                // against the pixels'.
                // (a window's share as the lookup finds it: its rectangle and kMargin about it, within the quad; in the
                // pair each reaches kMargin into the mullion)
                std::vector<gl::LightCell> cells;
                for (const gl::Window& win2 : ws)
                {
                    gl::LightCell lc;
                    lc.hash = gl::LitHash(static_cast<float>(win2.ordinal), static_cast<float>(win2.group), seed[0], seed[1]);
                    const float off = (win2.center[0] - mid[0]) * a[0] + (win2.center[1] - mid[1]) * a[1] + (win2.center[2] - mid[2]) * a[2];
                    const float edge = 0.995f * hw;
                    const float lo = std::max(off - win2.halfWidth - gl::rooms::kMargin, -edge), hi = std::min(off + win2.halfWidth + gl::rooms::kMargin, edge);
                    lc.share = std::max(0.0f, hi - lo) / (2.0f * edge);
                    lc.band = band;
                    cells.push_back(lc);
                }
                const float cpuShare = gl::LitShare(cells.data(), cells.size()), gpuShare = static_cast<float>(gpuSum / (kS * kS));
                sumShare += gpuShare;
                if (std::fabs(cpuShare - gpuShare) > worstShare)
                {
                    worstShare = std::fabs(cpuShare - gpuShare);
                    char at[200];
                    std::snprintf(at, sizeof(at), "%s, window %d%s: %.3f against %.3f (pixels %d, %d, %d)", pl.name, win, pair ? " (a pair)" : "", cpuShare, gpuShare,
                                  inA, inB, inNone);
                    worstAt = at;
                }
            }
        char line[760];
        std::snprintf(line, sizeof(line), "WARP, lit or dark (%s glass's windows), the shader against the window lights' rules: %d windows (%d of them a light over two; "
                      "the origin, Goldshire, Stormwind, Northrend; 6 .. 45 yd; at rest, the clock fading, cross-fades after a jump, all dark, all lit), %lld "
                      "pixels: no window found at %d, another key at %d, the lit hash's bits differ at %d, the lit value's at %d (%d part lit); vertex normals "
                      "wobbled 8 degrees change %d windows; the light's share from its windows against the pixels' worst %.4f (%s), mean lit %.2f",
                      gl::KindName(kind), windows, pairs, pixels, missing, keyOff, hashOff, litOff, partLit, wobbleOff, worstShare, worstAt.c_str(),
                      windows ? sumShare / windows : 0.0f);
        Check(drewAll && windows == 96 && missing == 0 && keyOff == 0 && hashOff == 0 && litOff == 0 && wobbleOff == 0 && partLit > 1000 &&
              worstShare < 0.015f, line);
        }
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
        // or the flicker on. (Without frosted glass's rooms, FrostedRooms 0: these batches as they were before it;
        // NightRoomRulesTests has them with it.)
        gl::Settings s;
        s.frostedRooms = 0;
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

        // WindowRooms: the Flags column, frosted windows at the lights of dwarven, orcish and taunka towns (421 at 333 lights
        // on the 2026-10-17 scan, as read with stained glass's rooms; without them 449 at 355: Dalaran's stained glass beside
        // its frosted windows, the cells as they were), each light's windows at most its glass either way.
        std::map<uint32_t, float> perLight[2];
        int frostCells[2] = {};
        std::set<uint32_t> frostLights[2];
        for (const auto& r : wr.rows)
        {
            if (wr.fields < 8) break;
            for (int on = 0; on < 2; ++on)
            {
                if (r[7] & (on ? 0x4u : 0xAu)) continue;   // (read only without stained glass's rooms; only with them)
                perLight[on][r[1]] += F(r[5]);
                if (r[7] & 1u) { ++frostCells[on]; frostLights[on].insert(r[1]); }
            }
        }
        float most = 0;
        for (int on = 0; on < 2; ++on)
            for (const auto& kv : perLight[on]) most = std::max(most, kv.second);
        std::snprintf(line, sizeof(line), "WindowRooms.cdbc: %u columns, %zu rows; with stained glass's rooms %d lit frosted glass at %zu lights, without %d at %zu; a "
                      "light's windows at most %.4f of its glass", wr.fields, wr.rows.size(), frostCells[1], frostLights[1].size(), frostCells[0],
                      frostLights[0].size(), most);
        Check(wr.fields == 8 && frostCells[1] > 400 && frostLights[1].size() > 300 && frostCells[0] >= frostCells[1] && most <= 1.0005f, line);
    }

    // On WARP: frosted windows (no room) pixel by pixel against the CPU's rules, and each light's share with its flicker.
    void FrostedAgreementTests()
    {
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("skip  frosted agreement: %s\n", why.c_str()); return; }
        // The probe: r0 = (the flicker's dip, the glow hidden, lit, the lit hash).
        const std::string probe = "\n          mov r0.x, r21.w\n          mov r0.y, r14.y\n          mov r0.z, r14.z\n          mov r0.w, r22.x";
        std::vector<uint8_t> code;
        if (!wxl_livingazeroth::shaderpatch::Assemble(LitProbeShader(probe), "frosted probe", code, why)) { Check(false, "the frosted probe assembles: " + why.substr(0, 300)); return; }
        IDirect3DPixelShader9* ps = nullptr;
        constexpr int kS = 96;
        IDirect3DSurface9 *target = nullptr, *readback = nullptr;
        if (FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps)) ||
            FAILED(w.dev->CreateRenderTarget(kS, kS, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr)) ||
            FAILED(w.dev->CreateOffscreenPlainSurface(kS, kS, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &readback, nullptr)))
        { Check(false, "the frosted probe on WARP"); return; }
        rh::Rooms rooms;
        rooms.Textures(w.dev);
        float look[4];
        gl::LookConstants(gl::Light{}, look);
        w.dev->SetPixelShaderConstantF(gl::kLookConstant, look, gl::kLookRegisters);
        struct Place { const char* name; float x, y, z; };
        const Place places[] = { { "the origin", 0, 0, 0 }, { "Kharanos", -5600, -480, 397 }, { "Ironforge", -4900, -950, 501 },
                                 { "the Storm Peaks", 7800, -2600, 1100 } };
        uint32_t rng = 777;
        auto rnd = [&rng]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };
        int windows = 0, keyOff = 0, litOff = 0, nonFinite = 0, repeatOff = 0, missing = 0;
        long long pixels = 0;
        float worstDip = 0, worstHide = 0, worstShare = 0, worstWrap = 0;
        bool drewAll = true;
        std::string worstAt;
        for (const Place& pl : places)
            for (int win = 0; win < 12; ++win)
            {
                const float yaw = rnd() * 6.2831853f, pitch = (rnd() - 0.5f) * 0.3f;
                float n[3] = { std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch) };
                Norm(n);
                const float hw = 0.75f + rnd() * 1.75f, hh = 0.6f + rnd() * 1.4f, dist = 6.0f + rnd() * 30.0f;
                const float mid[3] = { pl.x + (rnd() - 0.5f) * 60.0f, pl.y + (rnd() - 0.5f) * 60.0f, pl.z + rnd() * 12.0f };
                const uint32_t group = static_cast<uint32_t>(rnd() * 200.0f), ordinal = static_cast<uint32_t>(rnd() * 3000.0f);
                const float cam[3] = { mid[0] + n[0] * dist, mid[1] + n[1] * dist, mid[2] + n[2] * dist };
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
                const gl::Window fw = rh::MakeWindow(kSecondPath, group, ordinal, mid, n, hw, hh, gl::kFrosted);
                rooms.Table(w.dev, { fw });
                // lit frosted glass, outside: no room (c193.z 0), c201.w 1, the hearth clock and amplitude, the glow a dark one hides
                float c[4 * gl::kConstantCount] = {};
                c[3] = 1; c[5] = 1; c[6] = 0;
                c[35] = band.hi; c[47] = band.lo;
                c[37] = clock; c[38] = amp; c[39] = 1;
                c[55] = gl::hearth::kFrostedHidden;
                const float sd[4 * gl::kSeedRegisters] = { seed[0], seed[1], 0, band.slope, 0, 0, 0, band.base };
                float view[16] = { cam[0], cam[1], cam[2], 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
                w.dev->SetPixelShaderConstantF(gl::kViewConstant, view, 4);
                w.dev->SetPixelShaderConstantF(gl::kSeedConstant, sd, gl::kSeedRegisters);
                float regs[4 * gl::kRoomRegisters];
                rooms.Registers(cam, kSecondPath, group, gl::kClassHouse, gl::Settings{}, regs);
                w.dev->SetPixelShaderConstantF(gl::kRoomConstant, regs, gl::kRoomRegisters);
                rooms.Bind(w.dev);
                float rel[4][3], N[3];
                WindowQuad(fw, cam, rel, N, 0.995f);
                // Three draws: as given, again (the same bits), and the clock a wrap later (no seam).
                std::vector<float> px[3];
                for (int pass = 0; pass < 3; ++pass)
                {
                    c[37] = pass == 2 ? clock + gl::hearth::kWrap : clock;
                    w.dev->SetPixelShaderConstantF(gl::kFirstConstant, c, gl::kConstantCount);
                    const float base[3] = { 0.5f, 0.4f, 0.3f };
                    if (!DrawQuad(w.dev, ps, target, readback, kS, rel, N, base, px[pass], w.texel)) { drewAll = false; break; }
                }
                if (px[2].empty()) continue;
                ++windows;
                const float hash = gl::LitHash(static_cast<float>(ordinal), static_cast<float>(group), seed[0], seed[1]);
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
                        ++pixels;
                        if (g[2] < 0.0f) { ++missing; continue; }   // the probe never ran: no window found
                        worstWrap = std::max(worstWrap, std::fabs(px[2][at] - g[0]) / amp);
                        keyOff += Bits(hash) != Bits(g[3]) ? 1 : 0;
                        const float lit = gl::RoomLit(g[3], band);
                        litOff += Bits(lit) != Bits(g[2]) ? 1 : 0;
                        const float dip = gl::HearthDip(g[3], clock, amp);
                        worstDip = std::max(worstDip, std::fabs(dip - g[0]) / amp);
                        const float hide = (1.0f - lit) * gl::hearth::kFrostedHidden + lit * dip;
                        worstHide = std::max(worstHide, std::fabs(hide - g[1]));
                        gpuSum += g[2] * (1.0f - g[0]);
                    }
                // The window light: its window lit x (1 - its dip), against the pixels'.
                gl::LightCell lc;
                lc.hash = hash; lc.share = 1.0f; lc.band = band; lc.dip = gl::HearthDip(hash, clock, amp);
                const float cpuShare = gl::LitShare(&lc, 1), gpuShare = static_cast<float>(gpuSum / (kS * kS));
                if (std::fabs(cpuShare - gpuShare) > worstShare)
                {
                    worstShare = std::fabs(cpuShare - gpuShare);
                    char at[160];
                    std::snprintf(at, sizeof(at), "%s, window %d: %.3f against %.3f", pl.name, win, cpuShare, gpuShare);
                    worstAt = at;
                }
            }
        char line[700];
        std::snprintf(line, sizeof(line), "WARP, frosted windows (no room), the shader against the window lights' rules: %d windows (the origin, Kharanos, Ironforge, "
                      "the Storm Peaks; at rest, fading, cross-fading, all dark, all lit; the flicker 0.05 .. 0.30), %lld pixels: no window found %d, NaN/Inf %d; "
                      "the hash off the window's key's at %d; the lit value's bits differ at %d; the dip off the CPU's by at most %.1e of its amplitude, the glow "
                      "hidden by %.1e; drawn again the same bits (%d differ); a wrap of the clock later the dip off by %.1e of it; the light's share with its "
                      "flicker against the pixels' worst %.4f (%s)", windows, pixels, missing, nonFinite, keyOff, litOff, worstDip, worstHide, repeatOff, worstWrap,
                      worstShare, worstAt.c_str());
        Check(drewAll && windows == 48 && missing == 0 && nonFinite == 0 && keyOff == 0 && litOff == 0 && worstDip < 2e-3f && worstHide < 1e-3f &&
              repeatOff == 0 && worstWrap < 2e-3f && worstShare < 0.01f, line);
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
    // --- 9. titan glass lit from behind (Ulduar, the Halls of Stone and of Lightning) -----------------------------------
    // "Their stained glass should glow on its own and cast coloured light from behind, not depend on the sun" (the user,
    // 2026-10-18).
    const char* const kTitanTextures[] = { "ce_evilglass", "ce_uldr_stainedglass01", "ce_uldr_stainedglass03", "ce_uldr_warroomstainedglass",
                                           "eb_stainedglass", "eb_uldr_redwindow", "wr_window" };

    // General Vezax's evil glass as gen_glass makes its row: stained, all glass here, its glow and its purple pulse.
    gl::Material EvilGlass()
    {
        gl::Material m;
        m.kind = gl::kStained;
        m.flags = gl::kBacklitPulse;
        m.mask[0] = m.mask[1] = m.mask[2] = 0; m.mask[3] = 1;
        m.light[0] = 1.0f; m.light[1] = 0.73f; m.light[2] = 0.9f;
        m.backlit = 1.7f; m.pulse = 0.05f; m.pulseSeconds = 6.5f;
        const float purple[3] = { 0.55f, 0.2f, 1.0f };
        const float lum = 0.2126f * purple[0] + 0.7152f * purple[1] + 0.0722f * purple[2];
        for (int k = 0; k < 3; ++k) m.pulseColour[k] = purple[k] / lum;
        return m;
    }

    // Noon (the sun high, clear) or midnight (the client's windows lit, the sun down).
    gl::Inputs HourInputs(bool night)
    {
        gl::Inputs in;
        in.sun[0] = 0.3f; in.sun[1] = -0.2f; in.sun[2] = night ? -0.6f : 0.93f; Norm(in.sun);
        in.daylight = night ? 0.0f : 1.0f; in.night = night ? 1.0f : 0.0f;
        return in;
    }

    // A batch's constants as the runtime makes them (BatchConstants, DayConstants, RoomBatchConstants, then BacklitConstants
    // for a backlit one) in a building flagged `flags`, seen from inside by `inside`, at `seconds` on the pulse's clock.
    void TitanBatch(const gl::Material& m, const gl::Settings& s, uint32_t flags, bool night, float inside, double seconds,
                    float c[4 * gl::kConstantCount], float sd[4 * gl::kSeedRegisters], float day[4 * gl::kDayRegisters])
    {
        const gl::Inputs in = HourInputs(night);
        const gl::Light lt = gl::Evaluate(in);
        gl::BatchConstants(m, inside, lt, in.sun, in.sunColour, in.sky, s, c);
        gl::DayConstants(m, lt, s, flags, day);
        gl::Building b;
        b.seed = 5; b.uniqueId = 2712187; b.flags = flags;
        gl::RoomInputs ri;
        ri.night = night ? 1.0f : 0.0f; ri.hour = night ? 0.0f : 12.0f; ri.kind = m.kind; ri.hearthClock = 10.0f;
        gl::RoomBatchConstants(gl::DefaultStyle(), &b, lt, ri, s, c, sd);
        if (gl::BacklitBatch(m, flags, s)) gl::BacklitConstants(m, seconds, s, c, day);
    }

    void BacklitRulesTests()
    {
        namespace bl = gl::backlit;
        char line[700];
        const double kPi = 3.14159265358979;
        const float tilt = static_cast<float>(bl::kTiltDegrees * kPi / 180.0);

        // Toward the light behind a pane: its way out tilted 30 degrees up (the light falls 30 degrees below the inward
        // normal); a skylight's straight up; a pane facing a little down still lit along its way out; unit, outside.
        gl::Pane wall = WallWindow();
        float d[3];
        gl::BacklitDirection(wall, d);
        const bool wallOk = Near(d[0], std::cos(tilt), 1e-5f) && Near(d[1], 0, 1e-6f) && Near(d[2], std::sin(tilt), 1e-5f);
        gl::Pane sky = wall;
        sky.out[0] = 0; sky.out[2] = 1;
        float ds[3];
        gl::BacklitDirection(sky, ds);
        gl::Pane down = wall;
        down.out[0] = 0.97f; down.out[2] = -0.2425f; Norm(down.out);
        float dd[3];
        gl::BacklitDirection(down, dd);
        const float ddOut = dd[0] * down.out[0] + dd[1] * down.out[1] + dd[2] * down.out[2];
        std::snprintf(line, sizeof(line), "backlit: toward the light behind a wall pane (%.4f, %.4f, %.4f) (out tilted %.0f degrees up), a skylight's "
                      "(%.2f, %.2f, %.2f), a pane facing a little down (%.3f along its way out, unit %.5f)", d[0], d[1], d[2], bl::kTiltDegrees, ds[0], ds[1],
                      ds[2], ddOut, std::sqrt(dd[0] * dd[0] + dd[1] * dd[1] + dd[2] * dd[2]));
        Check(wallOk && Near(ds[2], 1, 1e-6f) && ddOut >= bl::kMinOut && Near(dd[0] * dd[0] + dd[1] * dd[1] + dd[2] * dd[2], 1, 1e-5f), line);

        // The floor projection along it: the wall window (x = 0, y -1 .. 1, z 1.5 .. 4.5, its room behind at x < 0). A
        // floor point (x, y, 0) is lit where its ray toward the light crosses the glass: at y, z = -x tan(tilt), so the
        // patch is x -4.5 / tan .. -1.5 / tan, y -1 .. 1, against the rows' weight over a grid of floor points (away from
        // the soft edges); the leak guards: in front of the glass, outside the room's box, beyond the reach.
        const float tn = std::tan(tilt);
        int agree = 0, total = 0, lit = 0;
        for (int i = 0; i < 60; ++i)
            for (int j = 0; j < 30; ++j)
            {
                const float x = -9.8f + i * (9.6f / 59.0f), y = -1.45f + j * (2.9f / 29.0f);
                const float q[3] = { x, y, 0.0f };
                const float zc = -x * tn;
                const bool inside = std::fabs(y) < 0.8f && zc > 1.5f + 0.3f && zc < 4.5f - 0.3f;
                const bool outside = std::fabs(y) > 1.0f || zc < 1.5f || zc > 4.5f;
                if (!inside && !outside) continue;
                const float wgt = gl::FloorWeightWorld(wall, d, q, 30, 6, 4);
                ++total;
                lit += wgt > 0.0f ? 1 : 0;
                agree += (inside && wgt > 0.95f) || (outside && wgt == 0.0f) ? 1 : 0;
            }
        gl::Pane high = wall;
        high.pos[2] = 40; high.roomMax[2] = 60;   // 38.5 yd above the floor: its patch 66 yd off, its ray 77 yd: beyond the reach
        const float qHigh[3] = { -40.0f / tn, 0, 0 }, qFront[3] = { 3, 0, 0 }, qOut[3] = { -5.2f, 0, -1.0f };
        const float mid[3] = { -3.0f / tn, 0, 0 };
        std::snprintf(line, sizeof(line), "backlit floor light: the projection along the light behind the pane agrees with the crossing at %d of %d floor points "
                      "(%d lit; the patch's middle %.3f); in front of the glass %.2f, under the room's floor %.2f, beyond the reach %.2f",
                      agree, total, lit, gl::FloorWeightWorld(wall, d, mid, 30, 6, 4), gl::FloorWeightWorld(wall, d, qFront, 30, 6, 4),
                      gl::FloorWeightWorld(wall, d, qOut, 30, 6, 4), gl::FloorWeightWorld(high, d, qHigh, 30, 6, 4));
        Check(total > 1000 && agree == total && lit > 150 && Near(gl::FloorWeightWorld(wall, d, mid, 30, 6, 4), 1, 1e-4f) &&
              gl::FloorWeightWorld(wall, d, qFront, 30, 6, 4) == 0 && gl::FloorWeightWorld(wall, d, qOut, 30, 6, 4) == 0 &&
              gl::FloorWeightWorld(high, d, qHigh, 30, 6, 4) == 0, line);

        // The pulse: a pure function of the time (the same bits again), 0 at a whole period and 1 half-way, periodic (a
        // period later, or a million seconds later on a whole number of periods, the same within 1e-6), within 0..1, its
        // mean a half, a frame's step small; no period or no finite time: 0.
        const float T = 6.5f;
        float lo = 1, hi = 0, step = 0, period = 0, later = 0;
        double sum = 0;
        long n = 0;
        bool same = true;
        float last = gl::BacklitWave(0.0, T);
        for (int f = 1; f <= 60 * 130; ++f)
        {
            const double t = f / 60.0;
            const float w = gl::BacklitWave(t, T);
            lo = std::min(lo, w); hi = std::max(hi, w); sum += w; ++n;
            step = std::max(step, std::fabs(w - last));
            last = w;
            same = same && Bits(w) == Bits(gl::BacklitWave(t, T));
            period = std::max(period, std::fabs(w - gl::BacklitWave(t + T, T)));
            later = std::max(later, std::fabs(w - gl::BacklitWave(t + 153846.0 * T, T)));
        }
        const float inf = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
        const bool degenerate = gl::BacklitWave(1.0, 0.0f) == 0 && gl::BacklitWave(1.0, -2.0f) == 0 && gl::BacklitWave(1.0, nan) == 0 &&
                                gl::BacklitWave(1.0, inf) == 0 && gl::BacklitWave(std::nan(""), T) == 0 && gl::BacklitWave(HUGE_VAL, T) == 0;
        std::snprintf(line, sizeof(line), "backlit pulse: %.4f .. %.4f, 0 at a whole period (%.6f) and 1 half-way (%.6f), mean %.4f, a frame's step at most %.4f, "
                      "the same bits again, a period later off by %.1e, a million seconds later by %.1e; no period or time: 0",
                      lo, hi, gl::BacklitWave(13.0, T), gl::BacklitWave(3.25, T), sum / n, step, period, later);
        Check(lo >= 0 && hi <= 1 && hi > 0.999f && lo < 0.001f && gl::BacklitWave(13.0, T) < 1e-6f && gl::BacklitWave(3.25, T) > 1 - 1e-6f &&
              std::fabs(sum / n - 0.5) < 0.01 && step < 0.009f && same && period < 1e-6f && later < 1e-5f && degenerate, line);

        // Vezax's glow: at least its steady glow, at most a few percent more, the pulse deep purple (blue and red more than
        // green); VezaxPulse scales it (0: steady to the bit, 2: twice); another texture's shimmer ignores VezaxPulse; NaN
        // anywhere gives finite colours.
        const gl::Material evil = EvilGlass();
        gl::Material plain = evil;
        plain.flags = 0; plain.pulse = 0.012f; plain.pulseSeconds = 37.0f;
        for (float& c : plain.pulseColour) c = 1.0f;
        gl::Settings s;
        float minLum = 1e9f, maxLum = 0, peak[3] = {};
        bool steady = true, shimmer = true;
        for (int f = 0; f <= 60 * 7; ++f)
        {
            const double t = f / 60.0;
            float g[3], g0[3], gp[3], gq[3];
            gl::BacklitGlow(evil, t, 1.0f, gl::PulseScale(evil, s), g);
            const float l = 0.2126f * g[0] + 0.7152f * g[1] + 0.0722f * g[2];
            if (l > maxLum) { maxLum = l; for (int k = 0; k < 3; ++k) peak[k] = g[k]; }
            minLum = std::min(minLum, l);
            gl::Settings off = s; off.vezaxPulse = 0;
            gl::BacklitGlow(evil, t, 1.0f, gl::PulseScale(evil, off), g0);
            for (int k = 0; k < 3; ++k) steady = steady && Bits(g0[k]) == Bits(evil.backlit);
            gl::BacklitGlow(plain, t, 1.0f, gl::PulseScale(plain, off), gp);
            gl::BacklitGlow(plain, t, 1.0f, gl::PulseScale(plain, s), gq);
            for (int k = 0; k < 3; ++k) shimmer = shimmer && Bits(gp[k]) == Bits(gq[k]) && gp[k] <= plain.backlit * 1.0121f && gp[k] >= plain.backlit;
        }
        gl::Settings twice = s; twice.vezaxPulse = 2;
        float g2[3];
        gl::BacklitGlow(evil, 3.25, 1.0f, gl::PulseScale(evil, twice), g2);
        const float l2 = 0.2126f * g2[0] + 0.7152f * g2[1] + 0.0722f * g2[2];
        gl::Material bad = evil;
        bad.pulseSeconds = nan; bad.pulseColour[1] = nan;
        float gn[3], gs[3], gz[3];
        gl::BacklitGlow(bad, 1.0, 1.0f, 1.0f, gn);
        gl::BacklitGlow(evil, 1.0, nan, 1.0f, gs);
        gl::Material notBacklit = evil; notBacklit.backlit = 0;
        gl::BacklitGlow(notBacklit, 1.0, 1.0f, 1.0f, gz);
        bool finite = true;
        for (int k = 0; k < 3; ++k) finite = finite && std::isfinite(gn[k]) && std::isfinite(gs[k]) && gs[k] == 0 && gz[k] == 0 && Bits(gn[k]) == Bits(evil.backlit);
        const float b = evil.backlit;
        std::snprintf(line, sizeof(line), "Vezax's glow: luminance %.4f .. %.4f of %.2f (+%.1f%% at the peak: blue +%.1f%%, red +%.1f%%, green +%.1f%%); VezaxPulse 0 "
                      "steady to the bit, 2 +%.1f%%; another glass's shimmer within 1.2%%, deaf to VezaxPulse; NaN in: finite out, no glow without Backlit",
                      minLum, maxLum, b, 100 * (maxLum / b - 1), 100 * (peak[2] / b - 1), 100 * (peak[0] / b - 1), 100 * (peak[1] / b - 1), 100 * (l2 / b - 1));
        Check(Near(minLum, b, 1e-5f) && maxLum / b > 1.04f && maxLum / b < 1.06f && peak[2] > peak[0] && peak[0] > peak[1] && steady && shimmer &&
              Near(l2 / b, 1.10f, 2e-3f) && finite, line);

        // Which batches, and their constants: titan glass in a backlit building with TitanBacklit on; the inside look with
        // the glow behind it (no sun, sky, darkening or glint), and every register the inside look reads the same at noon
        // and at midnight, the camera outdoors or indoors.
        gl::Settings off = s;
        off.titanBacklit = 0;
        Check(gl::BacklitBatch(evil, gl::kBuildingBacklit, s) && !gl::BacklitBatch(evil, 0, s) && !gl::BacklitBatch(evil, gl::kBuildingBacklit, off) &&
              !gl::BacklitBatch(notBacklit, gl::kBuildingBacklit, s), "backlit batches: titan glass, its building backlit, TitanBacklit on (else the daylight look)");
        float ref[4 * gl::kConstantCount], sd[4 * gl::kSeedRegisters], day[4 * gl::kDayRegisters];
        TitanBatch(evil, s, gl::kBuildingBacklit | gl::kBuildingDungeon, false, 0.0f, 1.25, ref, sd, day);
        bool look = ref[4] == 1 && ref[6] == 0 && ref[11] == 0 && ref[19] == 0 && ref[20] == 0 && ref[21] == 0 && ref[31] == 1 && ref[39] == 0 &&
                    day[0] == 0 && day[4] == 0;
        float glow[3];
        gl::BacklitGlow(evil, 1.25, 1.0f, 1.0f, glow);
        for (int k = 0; k < 3; ++k) look = look && Bits(ref[8 + k]) == Bits(glow[k]);
        const int reads[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 15, 19, 21, 31, 39 };   // c192, c193, c194, c195.w, c196.w, c197.y, c199.w, c201.w
        int differ = 0;
        for (int night = 0; night < 2; ++night)
            for (int in = 0; in < 2; ++in)
            {
                float c[4 * gl::kConstantCount];
                TitanBatch(evil, s, gl::kBuildingBacklit | gl::kBuildingDungeon, night != 0, static_cast<float>(in), 1.25, c, sd, day);
                for (int r : reads) differ += Bits(c[r]) != Bits(ref[r]) ? 1 : 0;
            }
        float daylight[4 * gl::kConstantCount], nightlight[4 * gl::kConstantCount];
        TitanBatch(evil, off, gl::kBuildingBacklit, false, 0.0f, 1.25, daylight, sd, day);
        TitanBatch(evil, off, gl::kBuildingBacklit, true, 0.0f, 1.25, nightlight, sd, day);
        std::snprintf(line, sizeof(line), "a backlit batch: the inside look, its glow behind it (%.3f %.3f %.3f), no sun, sky, darkening or glint; the registers the "
                      "inside look reads differ between noon and midnight, outdoors and indoors at %d of %zu; with TitanBacklit off the daylight look (its sky "
                      "through %.3f at noon, %.3f at midnight)", ref[8], ref[9], ref[10], differ, 4 * std::size(reads), daylight[8], nightlight[8]);
        Check(look && differ == 0 && daylight[8] > 0.1f && nightlight[8] == 0, line);

        // A pane's light: a backlit pane's from behind it at any hour (its direction BacklitDirection, its colour the glow x
        // kFloorShare x stained's 0.8 x its light), the same at noon and at midnight; a sunlit pane's the sun's by day and
        // none at night; TitanBacklit off: the backlit pane the sun's; an underground pane none; a backlit pane whose
        // texture isn't backlit glass the sun's.
        gl::Pane pane = WallWindow();
        pane.flags = gl::kPaneBacklit;
        gl::Pane sunlit = WallWindow();
        gl::Pane under = WallWindow();
        under.flags = gl::kPaneNoSky;
        gl::PaneLight a, n2, sd1, sn, offDay, offNight, ug, plainLit;
        const gl::Inputs noonIn = HourInputs(false), nightIn = HourInputs(true);
        const gl::Light noonL = gl::Evaluate(noonIn), nightL = gl::Evaluate(nightIn);
        float noonSun[3] = { 0.8f, 0.1f, 0.59f }; Norm(noonSun);   // beyond the pane (+x)
        const bool la = gl::LightOfPane(pane, &evil, noonL, noonSun, noonIn.sunColour, 2.0, s, a);
        const bool ln = gl::LightOfPane(pane, &evil, nightL, nightIn.sun, nightIn.sunColour, 2.0, s, n2);
        const bool ls = gl::LightOfPane(sunlit, &evil, noonL, noonSun, noonIn.sunColour, 2.0, s, sd1);
        const bool lsn = gl::LightOfPane(sunlit, &evil, nightL, nightIn.sun, nightIn.sunColour, 2.0, s, sn);
        const bool lod = gl::LightOfPane(pane, &evil, noonL, noonSun, noonIn.sunColour, 2.0, off, offDay);
        const bool lon = gl::LightOfPane(pane, &evil, nightL, nightIn.sun, nightIn.sunColour, 2.0, off, offNight);
        const bool lu = gl::LightOfPane(under, &evil, noonL, noonSun, noonIn.sunColour, 2.0, s, ug);
        const bool lp = gl::LightOfPane(pane, &notBacklit, noonL, noonSun, noonIn.sunColour, 2.0, s, plainLit);
        float g[3];
        gl::BacklitGlow(evil, 2.0, 1.0f, 1.0f, g);
        bool same2 = la && ln && a.backlit && n2.backlit;
        for (int k = 0; k < 3; ++k)
            same2 = same2 && Bits(a.dir[k]) == Bits(n2.dir[k]) && Bits(a.colour[k]) == Bits(n2.colour[k]) && Near(a.dir[k], d[k], 1e-6f) &&
                    Near(a.colour[k], pane.light[k] * g[k] * bl::kFloorShare * 0.8f, 1e-6f);
        std::snprintf(line, sizeof(line), "a pane's light: backlit at noon and midnight the same (%.3f %.3f %.3f toward %.2f %.2f %.2f); sunlit by day %s, at night %s; "
                      "TitanBacklit off: the sun's by day %s, at night %s; underground %s; a backlit pane of plain glass the sun's %s",
                      a.colour[0], a.colour[1], a.colour[2], a.dir[0], a.dir[1], a.dir[2], ls ? "yes" : "no", lsn ? "yes" : "no", lod && !offDay.backlit ? "yes" : "no",
                      lon ? "lit" : "none", lu ? "lit" : "none", lp && !plainLit.backlit ? "yes" : "no");
        Check(same2 && ls && !sd1.backlit && !lsn && lod && !offDay.backlit && Near(offDay.dir[0], noonSun[0], 1e-6f) && !lon && !lu && lp && !plainLit.backlit, line);

        // The facing (c195 in a floor batch) and the shafts' forward scattering: the sun's to the bit for sunlit panes alone;
        // a backlit pane's light alone, its direction; two opposite lights cancel: from overhead; NaN directions and
        // weights ignored.
        float facing[3];
        gl::LightFacing(&sd1, 1, noonSun, facing);
        const bool sunBits = Bits(facing[0]) == Bits(noonSun[0]) && Bits(facing[1]) == Bits(noonSun[1]) && Bits(facing[2]) == Bits(noonSun[2]);
        gl::LightFacing(&a, 1, noonSun, facing);
        const bool own = Near(facing[0], d[0], 1e-6f) && Near(facing[2], d[2], 1e-6f);
        gl::PaneLight pair[2] = { a, a };
        pair[1].dir[0] = -pair[1].dir[0]; pair[1].dir[2] = -pair[1].dir[2];
        gl::LightFacing(pair, 2, noonSun, facing);
        const bool cancel = facing[2] == 1 && facing[0] == 0;
        const float dirs[9] = { 1, 0, 0, nan, 0, 0, 0, 1, 0 }, weights[3] = { 1, 5, nan };
        const float up[3] = { 0, 0, 1 };
        gl::MeanDirection(dirs, weights, 3, up, facing);
        const bool nanSafe = Near(facing[0], 1, 1e-6f) && std::isfinite(facing[1]) && std::isfinite(facing[2]);
        gl::MeanDirection(dirs, weights, 0, up, facing);
        Check(sunBits && own && cancel && nanSafe && facing[2] == 1,
              "the floor light's facing and the shafts' scattering: the sun's to the bit for sunlit panes alone, a backlit pane's own, opposite lights from "
              "overhead, NaN ignored, none: the fallback");
    }

    // On WARP: a backlit pane at noon and at midnight, the camera outdoors and indoors, the same pixels to the bit, the
    // rules' inside colour, never below the stock colour; its light on the floor at both hours against the rules.
    void BacklitWarpTests()
    {
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("skip  backlit WARP checks: %s\n", why.c_str()); return; }
        std::vector<uint8_t> code;
        if (!wxl_livingazeroth::shaderpatch::Assemble(BlockShader(), "glass blocks", code, why)) { Check(false, "the blocks assemble (backlit): " + why.substr(0, 300)); return; }
        IDirect3DPixelShader9* ps = nullptr;
        if (FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps))) { Check(false, "the blocks' shader on WARP (backlit)"); return; }
        const float e = static_cast<float>(kSide) - 0.5f;
        auto quad = [&](const float p[4][3], const float n[3], const float base[3], V out[4])
        {
            const float xy[4][2] = { { -0.5f, -0.5f }, { e, -0.5f }, { -0.5f, e }, { e, e } };
            for (int c = 0; c < 4; ++c) out[c] = { xy[c][0], xy[c][1], 0, 1, { p[c][0], p[c][1], p[c][2] }, { n[0], n[1], n[2] }, { 0.5f, 0.5f }, { base[0], base[1], base[2] } };
        };
        const float texel[3] = { 0.8f, 77.0f / 255.0f, 0.2f };
        float view[16] = { 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
        w.dev->SetPixelShaderConstantF(gl::kViewConstant, view, 4);
        UploadLook(w.dev, gl::Light{});
        const float noRooms[4 * gl::kRoomRegisters] = {};
        w.dev->SetPixelShaderConstantF(gl::kRoomConstant, noRooms, gl::kRoomRegisters);
        const gl::Material evil = EvilGlass();
        const gl::Settings s;
        char line[600];

        // The pane 5 yd ahead, at a slant, the client's own colour dim (an Ulduar hall's vertex light) and bright.
        {
            float n[3] = { -1, 0.4f, 0.1f }; Norm(n);
            const float p[4][3] = { { 5, 0, 0 }, { 5, 0, 0 }, { 5, 0, 0 }, { 5, 0, 0 } };
            const size_t mid = (static_cast<size_t>(kSide / 2) * kSide + kSide / 2) * 3;
            float first[3] = {}, worst = 0, below = 0;
            int bad = 0, differ = 0, draws = 0;
            bool drewAll = true;
            for (int bright = 0; bright < 2; ++bright)
            {
                const float base[3] = { bright ? 0.95f : 0.12f, bright ? 0.95f : 0.1f, bright ? 0.95f : 0.14f };
                for (int night = 0; night < 2; ++night)
                    for (int in = 0; in < 2; ++in)
                    {
                        float c[4 * gl::kConstantCount], sd[4 * gl::kSeedRegisters], day[4 * gl::kDayRegisters];
                        TitanBatch(evil, s, gl::kBuildingBacklit | gl::kBuildingDungeon, night != 0, static_cast<float>(in), 1.25, c, sd, day);
                        w.dev->SetPixelShaderConstantF(gl::kSeedConstant, sd, gl::kSeedRegisters);
                        w.dev->SetPixelShaderConstantF(gl::kDayConstant, day, gl::kDayRegisters);
                        V v[4];
                        quad(p, n, base, v);
                        std::vector<float> rgb;
                        const bool ok = Draw(w, ps, v, c, gl::kFirstConstant, gl::kConstantCount, true, false, rgb);
                        drewAll = drewAll && ok;
                        if (!ok) continue;
                        for (float x : rgb) bad += std::isfinite(x) ? 0 : 1;
                        if (night == 0 && in == 0) for (int k = 0; k < 3; ++k) first[k] = rgb[mid + k];
                        else for (int k = 0; k < 3; ++k) differ += Bits(first[k]) != Bits(rgb[mid + k]) ? 1 : 0;
                        float glow[3], want[3];
                        for (int k = 0; k < 3; ++k) glow[k] = c[8 + k] * c[31];
                        gl::InsideColour(texel, base, c[15], glow, want);
                        for (int k = 0; k < 3; ++k) { worst = std::max(worst, std::fabs(rgb[mid + k] - want[k])); below = std::max(below, base[k] - rgb[mid + k]); }
                        ++draws;
                    }
            }
            w.dev->SetPixelShaderConstantF(gl::kSeedConstant, kZeroSeed, gl::kSeedRegisters);
            std::snprintf(line, sizeof(line), "WARP, backlit glass at noon and at midnight, the camera outdoors and indoors (%d draws): the same pixels (%d channels "
                          "differ), the rules' inside colour with the glow behind it within %.4f, never below the client's colour (by at most %.1e), NaN/Inf %d",
                          draws, differ, worst, below, bad);
            Check(drewAll && draws == 8 && differ == 0 && worst < 0.01f && below <= 1e-6f && bad == 0, line);
        }

        // Its light on the floor (z = 0, x -10 .. 0, y -2 .. 2) under the wall window, at noon and at midnight: the same,
        // against the rules (the rows along the light behind the pane, the glow's colour, the facing toward it).
        {
            gl::Pane pane = WallWindow();
            pane.flags = gl::kPaneBacklit;
            const gl::Material& m = evil;
            std::vector<float> both[2];
            float worst = 0, peak = 0;
            int lit = 0;
            bool drew = true;
            for (int night = 0; night < 2; ++night)
            {
                const gl::Inputs in = HourInputs(night != 0);
                const gl::Light lt = gl::Evaluate(in);
                gl::PaneLight pl;
                if (!gl::LightOfPane(pane, &m, lt, in.sun, in.sunColour, 2.0, s, pl)) { drew = false; break; }
                const float origin[3] = { 0, 0, 0 };
                float panes[4 * gl::kPaneRegisters * gl::kFloorPanes] = {};
                gl::ProjectionRows(pane, pl.dir, origin, 30, panes);
                for (int k = 0; k < 3; ++k) { panes[12 + k] = pl.colour[k]; panes[16 + k] = pane.roomMin[k]; panes[20 + k] = pane.roomMax[k]; }
                panes[15] = 1; panes[19] = 6; panes[23] = 4;
                float facing[3];
                gl::LightFacing(&pl, 1, in.sun, facing);
                float c[4 * gl::kConstantCount] = {};
                c[12] = facing[0]; c[13] = facing[1]; c[14] = facing[2];
                c[36] = 1;
                w.dev->SetPixelShaderConstantF(gl::kPaneConstant, panes, gl::kPaneRegisters * gl::kFloorPanes);
                const float p[4][3] = { { -10, -2, 0 }, { 0, -2, 0 }, { -10, 2, 0 }, { 0, 2, 0 } };
                const float n[3] = { 0, 0, 1 }, base[3] = { 0, 0, 0 };
                V v[4];
                quad(p, n, base, v);
                drew = drew && Draw(w, ps, v, c, gl::kFirstConstant, gl::kConstantCount, false, true, both[night]);
                if (!drew) break;
                for (int row = 0; row < kSide; ++row)
                    for (int col = 0; col < kSide; ++col)
                    {
                        const float x = -10 + (col + 0.5f) / kSide * 10, y = -2 + (row + 0.5f) / kSide * 4;
                        const float q[3] = { x, y, 0 };
                        const float weight = gl::FloorWeightWorld(pane, pl.dir, q, 30, 6, 4);
                        const float want = texel[0] * pl.colour[0] * weight * std::max(0.0f, facing[2]);
                        const float got = both[night][(static_cast<size_t>(row) * kSide + col) * 3];
                        worst = std::max(worst, std::fabs(got - want));
                        peak = std::max(peak, got);
                        lit += got > 0.02f && night == 0 ? 1 : 0;
                    }
            }
            int differ = 0;
            if (drew) for (size_t i = 0; i < both[0].size(); ++i) differ += Bits(both[0][i]) != Bits(both[1][i]) ? 1 : 0;
            std::snprintf(line, sizeof(line), "WARP, the backlit pane's light on the floor at noon and at midnight: the same pixels (%d channels differ), off from the "
                          "rules by up to %.4f (peak %.3f, %d of %d floor points lit)", differ, worst, peak, lit, kSide * kSide);
            Check(drew && differ == 0 && worst < 0.01f && peak > 0.1f && lit > 60 && lit < kSide * kSide / 2, line);
        }
        ps->Release();
    }

    // The shipped tables: the titan textures backlit (Vezax's pulsing), the titan maps' panes backlit (by map; those facing
    // the sky and other glass not), no backlit pane or building elsewhere (Wyrmrest's wr_window keeps the daylight).
    void BacklitTableTests(const std::filesystem::path& data)
    {
        Table m, p, bu;
        if (!ReadTable(data / "GlassMaterials.cdbc", m) || !ReadTable(data / "GlassPanes.cdbc", p) || !ReadTable(data / "GlassBuildings.cdbc", bu))
        { Check(false, "the shipped tables read (backlit)"); return; }
        auto text = [](const Table& t, uint32_t at) { return at < t.strings.size() ? std::string(&t.strings[at]) : std::string(); };
        auto isTitan = [](const std::string& name) { for (const char* t : kTitanTextures) if (name == t) return true; return false; };
        std::map<uint32_t, bool> backlitId;
        std::set<std::string> backlitNames;
        int pulsing = 0, lumOff = 0;
        bool vezax = false, shimmers = true;
        for (const auto& r : m.rows)
        {
            if (m.fields < 24) break;
            std::string t = text(m, r[1]);
            t = t.substr(t.find_last_of('\\') + 1);
            t = t.substr(0, t.size() - 4);
            const bool b = F(r[16]) > 0;
            backlitId[r[0]] = b;
            if (b) backlitNames.insert(t);
            const float lum = 0.2126f * F(r[19]) + 0.7152f * F(r[20]) + 0.0722f * F(r[21]);
            lumOff += std::fabs(lum - 1) > 1e-3f ? 1 : 0;
            if (r[3] & gl::kBacklitPulse)
            {
                ++pulsing;
                vezax = t == "ce_evilglass" && F(r[17]) >= 0.02f && F(r[17]) <= 0.08f && F(r[18]) >= 3 && F(r[18]) <= 12 && F(r[21]) > F(r[19]) && F(r[19]) > F(r[20]);
            }
            else if (b) shimmers = shimmers && F(r[17]) <= 0.02f && F(r[18]) >= 20;
        }
        bool named = backlitNames.size() == std::size(kTitanTextures);
        for (const char* t : kTitanTextures) named = named && backlitNames.count(t);
        char line[900];
        std::snprintf(line, sizeof(line), "GlassMaterials.cdbc: %u columns; %zu titan textures backlit (the 7 named), Vezax's evil glass alone pulsing (%d; a few "
                      "percent over several seconds, purple), the others a slow shimmer; pulse colours at luminance 1 (%d off)",
                      m.fields, backlitNames.size(), pulsing, lumOff);
        Check(m.fields == 24 && named && pulsing == 1 && vezax && shimmers && lumOff == 0, line);

        std::map<uint32_t, int> backlit, titan, other;
        int elsewhere = 0, wrongMaterial = 0, noMaterial = 0;
        for (const auto& r : p.rows)
        {
            if (p.fields < 27) break;
            const std::string c = text(p, r[25]);
            const std::string tex = c.substr(c.find(": ") == std::string::npos ? 0 : c.find(": ") + 2);
            const bool titanMap = r[1] == 599 || r[1] == 602 || r[1] == 603;
            noMaterial += r[26] == 0 ? 1 : 0;
            if (r[24] & gl::kPaneBacklit)
            {
                backlit[r[1]]++;
                elsewhere += titanMap ? 0 : 1;
                wrongMaterial += backlitId.count(r[26]) && backlitId[r[26]] ? 0 : 1;
            }
            if (titanMap) (isTitan(tex) ? titan : other)[r[1]]++;
        }
        const int sky = titan[599] + titan[602] + titan[603] - backlit[599] - backlit[602] - backlit[603];
        std::snprintf(line, sizeof(line), "GlassPanes.cdbc: %u columns; backlit panes: Halls of Stone %d of %d titan, Halls of Lightning %d of %d, Ulduar %d of %d "
                      "(titan panes facing the sky: %d; other glass there not backlit: %d); backlit elsewhere %d, of glass that isn't backlit %d; panes with no "
                      "material %d", p.fields, backlit[599], titan[599], backlit[602], titan[602], backlit[603], titan[603], sky, other[603] + other[599] + other[602],
                      elsewhere, wrongMaterial, noMaterial);
        Check(p.fields == 27 && backlit[599] == 44 && backlit[602] == 11 && backlit[603] > 800 && backlit[603] <= titan[603] && sky >= 0 && elsewhere == 0 &&
              wrongMaterial == 0 && noMaterial == 0 && other[603] > 0, line);

        int rows = 0, outside = 0, wyrmrest = 0, ulduar = 0, halls = 0;
        for (const auto& r : bu.rows)
        {
            const std::string wmo = text(bu, r[11]);
            if (wmo.find("wyrmrest") != std::string::npos && (r[9] & gl::kBuildingBacklit)) ++wyrmrest;
            if (!(r[9] & gl::kBuildingBacklit)) continue;
            ++rows;
            outside += r[1] == 599 || r[1] == 602 || r[1] == 603 ? 0 : 1;
            ulduar += wmo.find("ulduar_raid.wmo") != std::string::npos && r[2] != 0 && r[6] == gl::kWholePlacement ? 1 : 0;
            halls += (wmo.find("ulduar_dwarf77.wmo") != std::string::npos || wmo.find("ulduar_irongiant80.wmo") != std::string::npos) && r[2] != 0 ? 1 : 0;
        }
        std::snprintf(line, sizeof(line), "GlassBuildings.cdbc: %d backlit rows (Ulduar's raid placement %d, the Halls' %d), none off the titan maps (%d), Wyrmrest's none (%d)",
                      rows, ulduar, halls, outside, wyrmrest);
        Check(rows > 0 && ulduar == 1 && halls == 2 && outside == 0 && wyrmrest == 0, line);
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
    // GLASS_ONLY=<parts, comma separated>: only those (rules, edit, shafts, warp, rays, tables, night, dayrooms, nightrooms, lit, frosted, titan,
    // replay, lead, inn, sequence, inside, stained, tint)
    const char* only = std::getenv("GLASS_ONLY");
    auto run = [only](const char* part) {
        if (!only) return true;
        const std::string list = std::string(",") + only + ",";
        return list.find(std::string(",") + part + ",") != std::string::npos;
    };
    if (run("rules")) { RulesTests(); WindowRulesTests(); RoomRulesTests(); DayRulesTests(); LitRulesTests(); NightRoomRulesTests(); }
    if (run("edit")) EditTests(args[0]);
    if (run("shafts")) ShaftShaderTests();
    if (!warp) std::printf("skip  WARP checks (--no-warp)\n");
    if (warp && run("warp")) WarpTests();
    if (warp && run("rays")) RoomRayTests();
    if (run("tables")) { TableTests(args[1]); WindowTableTests(args[1]); }
    if (warp && run("night")) NightTests(args[1]);
    if (warp && run("dayrooms")) DayRoomTests(args[1]);
    if (warp && run("nightrooms")) NightRoomTests(args[1]);
    if (warp && run("lit")) LitAgreementTests();
    if (run("frosted")) { FrostedRulesTests(); FrostedTableTests(args[1]); }
    if (warp && run("frosted")) FrostedAgreementTests();
    if (run("titan")) { BacklitRulesTests(); BacklitTableTests(args[1]); }
    if (warp && run("titan")) BacklitWarpTests();
    if (warp && run("replay")) ReplayTests(args[1]);
    if (run("lead")) LeadTests(args[1]);
    if (warp && run("inn")) InnRedrawTests(args[0], args[1]);
    if (warp && run("sequence")) SequenceTests(args[0], args[1]);
    if (warp && run("inside")) SequenceTests(args[0], args[1], true);
    if (warp && run("stained"))
        for (const SeqScene& sc : StainedScenes()) SequenceScene(args[0], args[1], sc);
    if (warp && run("tint")) TintTests();
    if (const char* preview = std::getenv("GLASS_PREVIEW")) Preview(preview);
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
