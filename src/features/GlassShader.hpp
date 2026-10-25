// Window glass: the two blocks features/ModelIce's WMO pixel edit adds after the ice and the footprint rims, apart
// from the runtime so tests/glass can assemble them, count their slots and run them. The runtime: features/Glass.
//
// Both blocks read the variant's view-space position and normal (the ice block's vP, vN) and its first texture's
// coordinates (vT: the stock `texld rK, vT, s0`), and add their change to r31 like the ice (r12 = the stock colour
// before the fog, read only). r13..r30 are free by then (the ice's and the rims' values are all in r31).
//
// Glass (b9: features/Glass switches it on for the batches whose texture is glass, GlassMaterials.cdbc):
//  the texel and its glass mask (c192: saturate(dot(texel.rgb, c192.xyz) + c192.w), x c193.y); the glass's thickness
//  (a second texel deeper along the view, c193.w yd, through the uv's screen gradients); seen from inside, the light
//  coming through it (the texel more saturated x (the sky + the sun beyond the pane), never below the stock colour:
//  the client's own lit windows at night); seen from outside, the client's painted glass a little darker by day
//  (c197.x: clear most, stained barely), by day cooler (c85: toward the sky's hue at its luminance, more toward the top
//  of the pane and at a slant, env/GlassModel's DayCool), behind clear and leaded glass a room (c193.z of it; frosted
//  glass's blurred, stained glass's tinted and faint), with the sky in it (Schlick's Fresnel from F0 c197.z, x c196.w;
//  the sky deeper toward the zenith along the reflected ray) and the sun's glint; inside and outside mixed by c193.x.
//
//  One room per window (env/GlassRooms: "actual 3D fake room renders instead of flat 2D ones", the user, 2026-10-18).
//  The point and the way to the eye go into the building's own space (its WMO's: c15..c17, the instance's frame about
//  the camera); the group's grid (c18..c20) gives the cell holding the point, and the window table (s13: RGBA floats,
//  WindowTable's layout) the windows reaching into it; the first whose rectangle (and plane) holds the point is the
//  pixel's window. No window there (an unknown WMO, a pane the table doesn't have): no room, lit.
//  Its key (its ordinal in its WMO, its group) and the building's seed give the room's lit hash, exactly
//  (env/GlassModel's LitHash: whole numbers mod 289, the same bits on any GPU and on the CPU, so the window lights know
//  which rooms are dark: WindowRooms.cdbc has the same keys), against the dark band (env/GlassModel's DarkBand: c203.w
//  lo, c200.w hi, c212.w slope, c213.w base: a room the threshold crossed in the last 1.5 s, or every room a jump of the
//  time crossed, is part lit, so it fades). At night the lit rooms behind the client's lit windows have a lamp (c203.rgb,
//  as bright as the client's window: x its luminance); dark ones there hide most of its glow (c205.w) from the whole
//  window, lead and frame too (the client lights all of it); their room keeps the moon's and the night's light coming in
//  at the window (c198: the day's light by day, the moon's at night, in dark rooms only: lit x c218.z + c218.w, so a
//  lit room's lamp is as it was). A frosted window (the table's flag: a room only for a batch that allows it, c26.y)
//  runs the same for its lights out, and a lit one (and, with LampFlicker, a lamp-lit room and its lamp) flickers like a
//  hearth: its glow down by the dip c201.z x (0.5 + the three waves of env/GlassModel's HearthDip on the hearth clock
//  c201.y, each window its own phase from its lit hash), so the window light in front (features/Windows) dips with it.
//  Stained windows are in grids of their own (a stained batch's c18..c20), so no other batch ever finds one.
//  The room: the window's opening extruded inward (the table's box: a little wider than the window, the floor below the
//  sill; c21.x deep by the building's class, x (1 + c21.y (hash - 0.5))), ray-cast in the window's own frame (CastRoom
//  is the CPU's twin). Its floor, walls and ceiling sample the palette's client interior textures (s8, s9, s10: c22 1 /
//  the yards each repeats over, c23 each one's gain to a mean of 1), their gradients from the pixel's footprint at the
//  face (anisotropic along the face's slant: no aliasing, no seam at the corners); the floor darker, the ceiling the
//  dimmest, the side walls lit by the window's light falling off, the corners darker (c209 by day, c210 at night, c215).
//  Behind frosted glass the footprint is at least c26.x yd (the textures at a low mip, the furniture's and the curtains'
//  edges soft: soft dark shapes, the lamp a soft glow) and the faces' shades c26.z of the way to c26.w (no crisp edge
//  where two faces meet); behind stained glass the room x lerp(1, the glass's hue, c27.y) (c27.x = 1 - c27.y).
//  Two layers of furniture at their depths (the back wall's: shelves, a hearth, a wardrobe, a painting...; the middle's:
//  a table and chairs, a bed, a counter...; c25, c24: the class's four each, picked by the room's hash) are cut-outs
//  from the atlas (s12: a signed distance, crisp over a pixel's footprint at any size), their wood the palette's (s11),
//  their cloth the curtains' colour (c205), lit by the room's light at their depth: they shift against the back wall as
//  the eye moves. Curtains half drawn just behind the glass (c204.xy: the style's share). By day the light comes in at
//  the window (farther darker: c86.y; lower lighter: c86.z; a patch of sun on the floor by it: c86.x); at night the lamp
//  at a hashed spot (c216, c217), the hearths and candles aglow (c96). Far away (c218) the room fades to its plain
//  shading: no texture, furniture or corners to shimmer.
// Floor light (b10: on for the batches of a building's interior groups while sunlit or backlit panes are near): up to
//  kFloorPanes panes (each faded in and out as it joins and leaves: its colour x its fade), each as three rows taking the point (camera-relative, world axes) to where the ray toward the
//  sun crosses the pane (|a|, |b| < 1) and how far (0 < t < 1), with soft edges, kept inside its room's box; x the
//  surface's own texel x its facing to the sun. Titan glass lit from behind (Ulduar, the Halls of Stone and of
//  Lightning) the same with no new code: each backlit pane's rows along the light behind it instead of the sun's, at
//  any hour, its colour the glow's, and the facing (c195 in a floor batch) toward its lights (features/Glass's
//  LightOfPane, LightFacing). Its glass's look is the inside look with a steady light behind it in c194 (BacklitConstants).
//
// The texture's corner that isn't glass (c27.zw, GlassMaterials' NotGlassU, NotGlassV): its texels keep the client's colour
//  (the glass mask 0 there): the human window atlases' fourth quadrant is a forge's fire slots ("blacksmith windows look
//  very weird lol", the user, 2026-10-20).
// The redraw (b11: features/SeeThrough draws a deferred glass batch again over the real room behind it, and only then):
//  every pixel looks up its window (r31.w: whether it's open, its bit in c219.w, the bits + 0.5, from the ordinal c219.z);
//  an open window has no fake room and hides none of its glow (r29.w: 0 for it and for a window the batch wouldn't have looked up, 1
//  otherwise and outside the redraw), and the alpha (ReplayAlpha, the edit's last lines: the stock alpha in r12.w) is
//  1 - the mask x (1 - the opacity, c219.x) where it's open, 1 elsewhere: an open window's glass texels see-through at
//  the opacity, its lead, frame and tracery and every shut window opaque ("the tracery also becomes more transparent",
//  the user, 2026-10-20). The batch's own alpha test (c219.y, -1 none) on the stock alpha is a texkill there, and so is
//  the redraw's split into its two draws (c28.y: +1 keeps alpha >= c28.z, the opaque; -1 the rest), not the device's alpha
//  test. The mask the alpha goes by is the redraw's own where it has one (c28.w 1: the lead mask at s15, every pane's
//  inside glass, lit and shaded alike; "yeah build the lead mask pls :D", the user, 2026-10-21), else the light's. Its
//  debug view (c28.x > 0) colours each pixel by what was decided: an open window's glass magenta, its lead and frame
//  green, a shut window cyan, no window found yellow (c29..c32 defined); r31.w -1 no window, 0 shut, 1 open.
//
// Registers (WMO pixel shaders; the stock ones use r0..r11, c0..c14, s0..s7 and no b/i registers):
//  b9 glass, b10 floor light, b11 the redraw; r12..r31 (shared with the ice and the rims, see ModelIce.hpp; r12.w the stock
//  alpha with the glass blocks, r29.w and r31.w the redraw's); i1 (defined: the window
//  loop's bound); s8..s13 (the room: floor, walls, ceiling, wood, the furniture atlas, the window table), s15 (the redraw's
//  lead mask);
//  c180..c183 the camera and the view's rows (terrainice::FillModelConstants, as the ice); c175, c176 (the ice's defs);
//  c192 mask plane; c193 (inside weight, strength, the room's share, thickness yd); c194 (the sky through the glass
//  rgb, the sun's gain); c195 (toward the sun, world xyz, in a floor batch toward its panes' lights; saturation inside); c196 (the reflected sky rgb, Fresnel
//  strength); c197 (darkening outside, glint, F0, diffusion); c198 (the room's daylight: its colour, its strength); c199 (its tint rgb; the
//  inside's brightness); c200 (the sun's colour; the dark band's hi); c201 (the floor light's gain, the hearth clock,
//  the flicker's amplitude, lit frosted glass 1); c202 (the room's other tint rgb, the buildings' spread in brightness);
//  c203 (the lamp rgb; the dark band's lower end); c204 (the curtains' band in the room's hash; .w their detail's
//  strength); c205 (the curtains rgb, the glow dark rooms hide); c212 (the building's seed xy, 1 / its block (0: one
//  building), the dark band's slope); c213 (.w the dark band's base); c218 (the room's detail by the distance squared:
//  base, slope; .zw the room's light lit and dark); c85 (by day the glass cooler: how much, more up, more at a slant, the sky's hue's share), c86 (by day
//  the room: the sun's patch's gain, farther darker, the light falling; zeros: the night's look); c15..c17 (the
//  instance's frame: WMO space = the rows . (P about the camera, 1)), c18..c20 (the group's grid: its corner, the first
//  cell's texel; 1 / the cell, x, x y cells, 1 / the table's height; the last cell, 1 with a grid), c21 (the room's depth,
//  its spread, the furniture's strength), c22, c23 (the textures' scales and gains), c24, c25 (the furniture: the middle
//  and back layers' tiles), c26 (the kind's room: the footprint's least yd, frosted windows' rooms allowed 1, the faces'
//  shades' flattening and their mean), c27 (1 - the stained tint, the tint); c218.zw (the room's light by lit x .z + .w:
//  by day a dark room keeps room::kDarkDay, at night the moon's in dark rooms only); c206, c207, c208, c209, c210, c211,
//  c214..c217, c220, c221, c83, c84, c87..c100 defined; c27.zw the corner that isn't glass; c219 the redraw's (b11 only:
//  1 - the opacity, the alpha test's reference, the first ordinal, the open windows' bits; c28 its debug view, its
//  draw's sign and threshold, whether s15 holds its lead mask; c29..c32 the debug view's tints, defined; c33.x the drawn
//  triangles' window state, the split redraw's);
//  the floor light's panes c101..c172 (six registers each; the rims' range: a batch has one or the other, features/Glass
//  re-uploads when the rims took it), and since 2026-10-24 four more in c34..c57 (features/Glass re-uploads them at every
//  floor batch). ps_3_0 has c0..c223, and an instruction reads at most one constant register.
#pragma once

#include "../env/GlassModel.hpp"
#include "../env/GlassRooms.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace wxl_livingazeroth::glass
{
    constexpr int kSwitch = 9;           // PS b9: the glass
    constexpr int kFloorSwitch = 10;     // PS b10: the floor light
    constexpr int kViewConstant = 180;   // c180..c183 (FillModelConstants' first four)
    constexpr int kFirstConstant = 192;  // c192..c205
    constexpr int kConstantCount = 14;
    constexpr int kSeedConstant = 212;   // c212: the building's seed, its block, the dark band; c213: the band's base
    constexpr int kSeedRegisters = 2;
    constexpr int kPaneConstant = 101;   // c101..c172: the floor light's first kPanesAt101 panes
    constexpr int kPaneRegisters = 6;
    constexpr int kFloorPanes = 16;      // (twelve before 2026-10-24: "lots of pop-in when i move", the user; Glass's PaneFader)
    constexpr int kPanesAt101 = 12;
    constexpr int kPaneConstantMore = 34;   // c34..c57: the rest (2026-10-24). Free in the WMO pixel shaders, but other
                                            // shaders (the terrain's lights, the cover's ripples) write them between draws:
                                            // features/Glass uploads them again at every floor batch.
    /// Pane k's first register (k < kFloorPanes).
    constexpr int PaneRegister(int k) { return k < kPanesAt101 ? kPaneConstant + kPaneRegisters * k : kPaneConstantMore + kPaneRegisters * (k - kPanesAt101); }
    constexpr int kPaneSoftConstant = 58;   // c58 (defined): the panes' soft edges, kPaneSoft, kPaneSoft, kPaneFarSoft
    constexpr float kPaneSoft = 6.0f, kPaneFarSoft = 4.0f;   // across and up a pane's patch, and along its light (1 / the share soft)
    static_assert(kPaneConstant + kPaneRegisters * kPanesAt101 <= 173, "the rims' range, c101..c172");
    static_assert(kPaneConstantMore + kPaneRegisters * (kFloorPanes - kPanesAt101) <= kPaneSoftConstant, "the panes below c58");
    static_assert(kPaneSoftConstant < 78, "below the crystals' c78..c81");
    static_assert(kFloorPanes % 2 == 0, "FloorBlock takes the panes two at a time");
    /// A pane's room as its block reads it (c+4 its middle about `origin`, c+5 its half size; `c` the pane's 24 floats).
    inline void PaneRoom(const float roomMin[3], const float roomMax[3], const float origin[3], float* c)
    {
        for (int k = 0; k < 3; ++k)
        {
            c[16 + k] = 0.5f * (roomMin[k] + roomMax[k]) - origin[k];
            c[20 + k] = 0.5f * (roomMax[k] - roomMin[k]);
        }
        c[19] = 0.0f;
        c[23] = 0.0f;
    }
    constexpr int kLookConstant = 218;   // c218: the room's detail by the distance (RoomLook)
    constexpr int kLookRegisters = 1;
    constexpr int kDayConstant = 85;     // c85: the glass cooler by day (DayCool); c86: the room by day (the sun's patch, farther darker)
    constexpr int kDayRegisters = 2;     // (zeros: neither, the night's look)
    constexpr int kRoomConstant = 15;    // c15..c27: the instance's frame, the group's grid, the room's depth, textures, furniture,
    constexpr int kRoomRegisters = 13;   // the kind's look (c26, c27: frosted glass's blur, stained glass's tint)
    constexpr int kFirstSampler = 8;     // s8 floor, s9 walls, s10 ceiling, s11 wood, s12 the furniture atlas, s13 the window table
    constexpr int kTableSampler = 13;
    constexpr int kReplaySwitch = 11;    // PS b11: features/SeeThrough's redraw of a deferred glass batch (never on otherwise)
    constexpr int kReplayConstant = 219; // c219, read under b11 only: 1 - the opacity, the batch's alpha-test reference (-1:
                                         // none), the first ordinal of .w's bits, the open windows' bits
    constexpr int kReplayBits = 16;      // windows c219.w holds (bits + 0.5: a margin no approximate exp crosses)
    constexpr int kReplayDrawConstant = 28;   // c28, read under b11 only: the debug view (1 on), the draw (+1 the opaque, -1
                                              // the see-through; 0 both), the alpha the two split at, the lead mask (1: s15's)
    constexpr int kLeadSampler = 15;     // s15, read under b11 only: the redraw's lead mask (features/SeeThrough binds it)
    constexpr int kReplayWindowConstant = 33;   // c33, read under b11 only: .x the drawn triangles' window state at least
                                                // (1 open, 0 shut, -1 the lookup's alone)
    /// GlassBlock's parts besides the look as it was (the tests build the block without them to hold it to the old one).
    enum BlockPart : int { kPartNotGlass = 1, kPartReplay = 2, kAllParts = 3 };

    inline std::string ShaderDefs()
    {
        using namespace room;
        using namespace rooms;
        auto def = [](int reg, float a, float b, float c, float d) {
            char line[128];
            std::snprintf(line, sizeof(line), "    def c%d, %.9g, %.9g, %.9g, %.9g\n", reg, a, b, c, d);
            return std::string(line);
        };
        const float atlasTexelsPerYd = kTileTexels / kTileSize;
        return "    def c206, 0.0001, 0.05, 0, 300\n" +
               def(207, 1e-12f, 0.08f, 10.0f, kTexelFloor) +
               def(208, 1.0f / kWidth, 0.5f / kWidth, static_cast<float>(kEntryTexels), 4.0f) +
               def(209, kFaceDay[0], kFaceDay[1], kFaceDay[2], kFaceDay[3]) +
               def(210, kFaceNight[0], kFaceNight[1], kFaceNight[2], kFaceNight[3]) +
               def(211, kZenith[0], kZenith[1], kZenith[2], 3.0f) +
               def(83, kPatchDeep, 1.0f / kPatchDeepSoft, kLightCap, 0.0f) +
               def(84, kSkyHue[0], kSkyHue[1], kPatchAcross, 1.0f / kPatchSoft) +
               def(87, 1.0f - kDarkDay, kDarkDay, kCurtainGain, kCurtainDay) +
               def(88, kHashK[0], kHashK[1], kHashK[2], kHashK[3]) +
               def(89, kHashAdd, kGapShift, kTileSize, 0.0f) +
               def(90, 2.0f, -1.0f, 3.0f, kThingTint) +
               def(91, 0.0f, 1.0f, 2.0f, 3.0f) +
               def(92, 1.0f / kTileSize, 0.5f, 1.0f / 8.0f, 0.5f) +
               def(93, 0.004f, 0.996f, 2.0f * kSdfRange, kPropHash[0]) +
               def(94, atlasTexelsPerYd, kBackGap, kMidDepth[0], kMidDepth[1]) +
               def(95, 2.0f * kPropAcross, kPropHalf, kPropHash[1], kPropHash[2]) +
               def(96, kGlow[0], kGlow[1], kGlow[2], kGlowNight) +
               def(97, kCurtainDepth, kGapMin, kGapSpan, kFolds) +
               def(98, kFoldAmp, kGlassAlbedo, kGlowDay, kFoldFade) +
               def(99, 1.0f / 6.0f, 0.5f, 5.0f / 6.0f, kStoneGrey) +
               def(100, 1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 6.0f) +
               def(214, kLitInv, kLitMod, 0.5f, kLitPoly) +
               def(215, 1.0f / kAoRadius, kAo, kFar, kAoMean) +
               def(216, 2.0f * kLampAcross, kLampHeight, kLampDepth[0], kLampDepth[1]) +
               def(217, 1.0f / kLampK, kLampGain / kLampK, kLampBase, kTextureTexels) +
               def(220, hearth::kRate[0], hearth::kRate[1], hearth::kRate[2], 0.0f) +
               def(221, hearth::kWeight[0], hearth::kWeight[1], hearth::kWeight[2], hearth::kSpread) +
               // the redraw's debug view's tints (kReplayDebug): an open window's glass, its lead and frame, a shut window, no window
               def(29, 1.0f, 0.0f, 1.0f, 0.0f) + def(30, 0.0f, 1.0f, 0.0f, 0.0f) + def(31, 0.0f, 1.0f, 1.0f, 0.0f) + def(32, 1.0f, 1.0f, 0.0f, 0.0f) +
               // the floor light's soft edges, across and up the pane and along its light (every pane's: FloorBlock)
               def(kPaneSoftConstant, kPaneSoft, kPaneSoft, kPaneFarSoft, 0.0f) +
               "    defi i1, " + std::to_string(kMaxPerCell) + ", 0, 0, 0\n"
               "    dcl_2d s8\n    dcl_2d s9\n    dcl_2d s10\n    dcl_2d s11\n    dcl_2d s12\n    dcl_2d s13\n    dcl_2d s15\n";
    }

    /// The window table's texel `index` (a whole number, in `idx`) as point-sampling coordinates in `o` (.xy, .zw 0):
    /// u = (index mod kWidth + 0.5) / kWidth, v = (index div kWidth + 0.5) / the height (c19.w its inverse); kWidth a power
    /// of two, so every step is exact.
    inline std::string TableTexel(const std::string& idx, const std::string& o)
    {
        return "\n          mul " + o + ".x, " + idx + ", c208.x"
               "\n          frc " + o + ".y, " + o + ".x"
               "\n          add " + o + ".z, " + o + ".x, -" + o + ".y"
               "\n          add " + o + ".x, " + o + ".y, c208.y"
               "\n          add " + o + ".y, " + o + ".z, c175.z"
               "\n          mul " + o + ".y, " + o + ".y, c19.w"
               "\n          mov " + o + ".zw, c175.y";
    }

    /// The room's light at a point P (across, up, deep: the window's frame about its middle; r30 the room's box (half
    /// width, floor, ceiling, depth), r24 its hash) into `day` and `night` (two components): by day falling off from the
    /// window (1 / (1 + (kFar + c86.y) deep)) and lighter low, darker high (x (1 + c86.z (0.5 - height))); at night the
    /// lamp's (across (h.z - 0.5) 2 kLampAcross x the half width, kLampHeight above the floor, (d0 + d1 h.w) of the depth
    /// back): base + gain / (1 + k d^2). T a temporary.
    inline std::string RoomLight(const std::string& P, const std::string& day, const std::string& night, const std::string& T)
    {
        return "\n            add " + T + ".x, " + P + ".y, -r30.y"
               "\n            add " + T + ".y, r30.z, -r30.y"
               "\n            rcp " + T + ".y, " + T + ".y"
               "\n            mad " + T + ".x, " + T + ".x, -" + T + ".y, c175.z"
               "\n            mul " + T + ".x, " + T + ".x, c86.z"
               "\n            add " + T + ".x, " + T + ".x, c175.x"
               "\n            mov " + T + ".y, c215.z"
               "\n            add " + T + ".y, " + T + ".y, c86.y"
               "\n            mad " + T + ".y, " + T + ".y, " + P + ".z, c175.x"
               "\n            rcp " + T + ".y, " + T + ".y"
               "\n            mul " + day + ", " + T + ".x, " + T + ".y"
               "\n            add " + T + ".x, r24.z, -c175.z"
               "\n            mul " + T + ".x, " + T + ".x, c216.x"
               "\n            mul " + T + ".x, " + T + ".x, r30.x"
               "\n            add " + T + ".x, " + P + ".x, -" + T + ".x"
               "\n            add " + T + ".y, " + P + ".y, -r30.y"
               "\n            add " + T + ".y, " + T + ".y, -c216.y"
               "\n            mov " + T + ".z, c216.z"
               "\n            mad " + T + ".z, r24.w, c216.w, " + T + ".z"
               "\n            mad " + T + ".z, " + T + ".z, -r30.w, " + P + ".z"
               "\n            dp3 " + T + ".x, " + T + ", " + T +
               "\n            add " + T + ".x, " + T + ".x, c217.x"
               "\n            rcp " + T + ".x, " + T + ".x"
               "\n            mad " + night + ", " + T + ".x, c217.y, c217.z";
    }

    /// One layer of furniture at the depth in r25.z (yd): the menu `menu` (c24 the middle's, c25 the back's) picked by the
    /// hash component `h`; composited over the room's colour so far (r26.xyz). Uses r15, r17, r22, r25, r29; reads r13
    /// (1 / the eye's distance, the footprint on the glass, the detail, t), r19.xy (the day's and the night's light), r20
    /// (the building's tint), r24, r27.xy (where the ray enters: across, up), r28 (the ray), r30.
    inline std::string FurnitureLayer(const std::string& h, const std::string& menu)
    {
        return
            // where the ray crosses the layer (across, up; its t in .w), and whether it's in front of the room's face
            "\n            rcp r25.w, r28.z"
            "\n            mul r25.w, r25.z, r25.w"
            "\n            mad r25.xy, r28, r25.w, r27"
            "\n            add r15.w, r13.w, -r25.w"
            "\n            cmp r15.w, r15.w, c175.x, c175.y"
            // its light there (the back wall's faces' shades: its props face the window)
            + RoomLight("r25", "r19.z", "r19.w", "r22") +
            "\n            mul r19.zw, r19, c209.w"
            "\n            mul r19.z, r19.z, r19.x"
            "\n            mul r19.w, r19.w, r19.y"
            // which prop (the menu's, by the hash: four choices), its middle across ((frac(h k) - 0.5) 2 kPropAcross x
            // max(the half width - kPropHalf, 0))
            "\n            mul r15.x, " + h + ", c208.w"
            "\n            frc r15.y, r15.x"
            "\n            add r15.x, r15.x, -r15.y"
            "\n            add r22, r15.x, -c91"
            "\n            cmp r22, -r22_abs, c175.x, c175.y"
            "\n            dp4 r15.x, r22, " + menu +
            "\n            mul r15.y, " + h + ", c93.w"
            "\n            frc r15.y, r15.y"
            "\n            add r15.y, r15.y, -c175.z"
            "\n            mul r15.y, r15.y, c95.x"
            "\n            add r15.z, r30.x, -c95.y"
            "\n            max r15.z, r15.z, c175.y"
            "\n            mul r15.y, r15.y, r15.z"
            // the atlas: u = (frac(tile / 8) + clamp((across - its middle) / kTileSize + 0.5)) / 8, v = (tile div 8 +
            // clamp(1 - up from the floor / kTileSize)) / 2; its mip by the footprint there (yd: on the glass x (1 + t / the
            // eye's distance))
            "\n            mul r15.x, r15.x, c92.z"
            "\n            frc r15.z, r15.x"
            "\n            add r15.x, r15.x, -r15.z"
            "\n            add r22.x, r25.x, -r15.y"
            "\n            mad r22.x, r22.x, c92.x, c92.y"
            "\n            add r22.y, r25.y, -r30.y"
            "\n            add r22.y, -r22.y, c89.z"
            "\n            mul r22.y, r22.y, c92.x"
            "\n            max r22.xy, r22, c93.x"
            "\n            min r22.xy, r22, c93.y"
            "\n            mad r22.x, r22.x, c92.z, r15.z"
            "\n            add r22.y, r22.y, r15.x"
            "\n            mul r22.y, r22.y, c92.w"
            "\n            mad r15.x, r25.w, r13.x, c175.x"
            "\n            mul r15.x, r15.x, r13.y"
            "\n            mul r22.w, r15.x, c94.x"
            "\n            log r22.w, r22.w"
            "\n            max r22.w, r22.w, c175.y"
            "\n            texldl r22, r22, s12"
            // its cut-out: the signed distance over the footprint (a pixel's edge at any size), in front, the furniture on,
            // none far away
            "\n            add r15.y, r22.w, -c175.z"
            "\n            mul r15.y, r15.y, c93.z"
            "\n            rcp r15.z, r15.x"
            "\n            mad_sat r15.y, r15.y, r15.z, c175.z"
            "\n            mul r15.y, r15.y, r15.w"
            "\n            mul r15.y, r15.y, c21.z"
            "\n            mul r15.y, r15.y, r13.z"
            // its wood (the palette's, its mip by the footprint)
            "\n            mul r17.xy, r25, c22.w"
            "\n            mul r17.w, r15.x, c22.w"
            "\n            mul r17.w, r17.w, c217.w"
            "\n            log r17.w, r17.w"
            "\n            max r17.w, r17.w, c175.y"
            "\n            texldl r17, r17, s11"
            "\n            mul r17.xyz, r17, c23.w"
            // its stuff (G: wood, cloth, stone and metal, a glow) and a thing's hue (B: books, bottles)
            "\n            add r29.xyz, r22.y, -c99"
            "\n            cmp r29.xyz, r29, c175.x, c175.y"
            "\n            lrp r25.xyz, r29.x, c205, r17"
            "\n            lrp r17.xyz, r29.y, c99.w, r25"
            "\n            lrp r25.xyz, r29.z, c96, r17"
            "\n            add r17.xyz, r22.z, c100"
            "\n            frc r17.xyz, r17"
            "\n            mul r17.xyz, r17, c100.w"
            "\n            add r17.xyz, r17, -c90.z"
            "\n            add r17.xyz, r17_abs, -c175.x"
            "\n            mov_sat r17.xyz, r17"
            "\n            add r17.xyz, r17, -c175.z"
            "\n            mul r17.xyz, r17, c90.w"
            "\n            add r29.xyw, r17.xyzz, c175.z"
            "\n            add r15.z, r22.z, -c206.y"
            "\n            cmp r15.z, r15.z, c175.x, c175.y"
            "\n            lrp r17.yzw, r15.z, r29.xxyw, r25.xxyz"
            // shaded (R), lit by the room's light there (x the building's tint), a glow its own light (brighter at night)
            "\n            mul r17.yzw, r17, r22.x"
            "\n            mul r25.xyz, r19.w, c203"
            "\n            add r25.xyz, r25, r19.z"
            "\n            mul r25.xyz, r25, r20"
            "\n            mul r25.xyz, r25, r17.yzww"
            "\n            mul r17.x, r19.y, c96.w"
            "\n            mad r17.x, r19.x, c98.z, r17.x"
            "\n            mul r17.x, r17.x, r29.z"
            "\n            mad r25.xyz, r17.yzww, r17.x, r25"
            "\n            lrp r17.xyz, r15.y, r25, r26"
            "\n            mov r26.xyz, r17";
    }

    /// `probe` (the tests'): instructions put right after the window's lit value, the flicker and the glow hidden are
    /// worked out (r14.z lit, r22.x the lit hash, r13.w and r20.w its key: the window's ordinal and group, r21.w the
    /// flicker's dip, r14.y the share of the glow hidden). `probeRoom`: right after the ray meets the room (r13.w its t,
    /// r25 the faces (back wall, side wall, floor or ceiling, ceiling), r26.xyz where: across, up, deep, r30 the room's
    /// box: half width, floor, ceiling, depth). `parts`: the corner that isn't glass (kPartNotGlass) and the redraw's
    /// (kPartReplay), both inert with c27.zw 0 and b11 off.
    inline std::string GlassBlock(const std::string& vP, const std::string& vN, const std::string& vT, const std::string& probe = std::string(),
                                  const std::string& probeRoom = std::string(), int parts = kAllParts)
    {
        // Not glass: the texture's corner from c27.zw's (1 - where it starts, u and v; 0 none) to its tile's far corner, the
        // human window atlases' fire slots (a forge's mouth): its texels keep the client's colour.
        const std::string notGlass = !(parts & kPartNotGlass) ? std::string() :
            "\n      frc r15.xy, " + vT +
            "\n      add r15.xy, r15, c27.zwzw"
            "\n      add r15.xy, r15, -c175.x"
            "\n      min r15.x, r15.x, r15.y"
            "\n      cmp r14.w, r15.x, c175.y, r14.w";
        const bool replay = (parts & kPartReplay) != 0;
        // The redraw: the window looked up whatever the batch wants (r29.w: what it wants, > 0 to look; outside the redraw
        // 1), none open yet (r31.w).
        const std::string replayLook = !replay ? std::string() :
            "\n      mov r29.w, c175.x"
            "\n      if b11"
            "\n        mov r31.w, -c175.x"
            "\n        mov r29.w, r19.x"
            "\n        max r19.x, r19.x, c175.x"
            "\n      endif";
        // ... and found: open when its bit is in c219.w (bits + 0.5, from ordinal c219.z: k = ordinal - first >= 0 and
        // (2026-10-24, whole rooms: the entry's 3.w is its fake room's key, so its own ordinal is read from texel 6.x)
        // frac(c219.w / 2^(k + 1)) >= 0.5; the half keeps a set bit's value a quarter or more from the boundary for k = 0,
        // so a GPU's approximate exp can't flip it: env/SeeThroughModel's OpenBit is the twin; none past kReplayBits); r29.w
        // its share of the fake room and of the glow hidden: none for an open window (the real room behind it) or one the
        // batch wouldn't have looked up, all for any other.
        const std::string replayOpen = !replay ? std::string() :
            "\n          if b11"
            "\n            add r22.x, r22.x, c208.x"
            "\n            texldl r19, r22, s13"
            "\n            add r19.x, r19.x, -c219.z"
            "\n            max r19.y, r19.x, -c175.x"
            "\n            add r19.y, r19.y, c175.x"
            "\n            exp r19.y, -r19.y"
            "\n            mul r19.y, r19.y, c219.w"
            "\n            frc r19.y, r19.y"
            "\n            add r19.y, r19.y, -c175.z"
            "\n            cmp r31.w, r19.y, c175.x, c175.y"
            "\n            cmp r31.w, r19.x, r31.w, c175.y"
            "\n            cmp r29.w, -r29.w, c175.y, c175.x"
            "\n            add r19.x, c175.x, -r31.w"
            "\n            mul r29.w, r29.w, r19.x"
            "\n          endif";
        const std::string replayKeep = !replay ? std::string() : "\n          mul r22.w, r22.w, r29.w";
        // The redraw's own mask (the look above done with the light's): the lead mask at s15 where features/SeeThrough bound
        // one (c28.w 1: GlassLead.cdbc's panes, env/GlassModel LeadMask), else the light's; what the alpha (ReplayAlpha) and
        // the debug view read. And the window's state at least c33.x: the redraw drawn split by window (SeeThroughDraw) says
        // it per draw (1 its open windows' triangles, 0 the rest; -1 whole: the lookup's alone), as in game the lookup found
        // no window (the debug view all yellow, 2026-10-21). The debug view (c28.x > 0: features/SeeThrough's panel): the pixel the colour of what the
        // shader decided (before the fog): an open window's glass magenta (c29) by its mask, through grey to its lead and
        // frame green (c30: mask 0, alpha 1), a shut window cyan (c31), no window found yellow (c32).
        const std::string replayDebug = !replay ? std::string() :
            "\n      if b11"
            "\n        texld r13, " + vT + ", s15"
            "\n        add r13.x, r13.x, -r14.w"
            "\n        mad r14.w, c28.w, r13.x, r14.w"
            "\n        max r31.w, r31.w, c33.x"
            "\n        mov r13.x, c28.x"
            "\n        if_gt r13.x, c175.y"
            "\n          mov r13, c32"
            "\n          if_ge r31.w, c175.y"
            "\n            mov r13, c31"
            "\n            if_gt r31.w, c175.y"
            "\n              mov r13, c29"
            "\n              add r13, r13, -c30"
            "\n              mad r13, r13, r14.w, c30"
            "\n            endif"
            "\n          endif"
            "\n          add r31.xyz, r13, -r12"
            "\n        endif"
            "\n      endif";
        // Mod 289 of r22.x (a whole number below 2^22) in place, exactly: v - floor((v + 0.5) / 289) 289.
        const std::string mod =
            "\n          add r22.z, r22.x, c214.z"
            "\n          mul r22.z, r22.z, c214.x"
            "\n          frc r22.w, r22.z"
            "\n          add r22.z, r22.z, -r22.w"
            "\n          mad r22.x, r22.z, -c214.y, r22.x";
        // The permutation (34 v + 1) v mod 289 of r22.x in place.
        const std::string permute =
            "\n          mul r22.z, r22.x, c214.w"
            "\n          mad r22.x, r22.z, r22.x, r22.x" + mod;
        // The sky along the reflected ray (env/GlassModel's SkyAlong): c196 deeper toward the zenith (c211) by the ray's
        // height, saturate(2 cos N.z - V.z): r24.xyz, its height r24.w.
        const std::string sky =
            "\n      add r24.w, r17.w, r17.w"
            "\n      mul r24.w, r24.w, r18.z"
            "\n      add_sat r24.w, r24.w, -r16.z"
            "\n      mov r24.xyz, c211"
            "\n      add r24.xyz, r24, -c175.x"
            "\n      mad r24.xyz, r24, r24.w, c175.x"
            "\n      mul r24.xyz, r24, c196";
        return
            "\n    if b9"
            // the texel, its glass mask
            "\n      texld r13, " + vT + ", s0"
            "\n      dp3 r14.w, r13, c192"
            "\n      add_sat r14.w, r14.w, c192.w"
            "\n      mul r14.w, r14.w, c193.y" + notGlass +
            // P (world; camera-relative until the screen gradients are taken), V (world, toward the eye), N (world,
            // facing the eye), cos = N.V
            "\n      dp3 r15.x, " + vP + ", c181"
            "\n      dp3 r15.y, " + vP + ", c182"
            "\n      dp3 r15.z, " + vP + ", c183"
            "\n      mov r16.xyz, -" + vP +
            "\n      nrm r17.xyz, r16"
            "\n      dp3 r16.x, r17, c181"
            "\n      dp3 r16.y, r17, c182"
            "\n      dp3 r16.z, r17, c183"
            "\n      nrm r17.xyz, " + vN +
            "\n      dp3 r18.x, r17, c181"
            "\n      dp3 r18.y, r17, c182"
            "\n      dp3 r18.z, r17, c183"
            "\n      dp3 r17.w, r18, r16"
            "\n      cmp r18.xyz, r17.w, r18, -r18"
            "\n      dp3_sat r17.w, r18, r16"
            // the point and the way to the eye in the building's own space (its WMO's: c15..c17 take P about the camera
            // there; no camera in the result, exact about the instance): r28 (w 1), r29
            "\n      mov r15.w, c175.x"
            "\n      dp4 r28.x, r15, c15"
            "\n      dp4 r28.y, r15, c16"
            "\n      dp4 r28.z, r15, c17"
            "\n      mov r28.w, c175.x"
            "\n      dp3 r29.x, r16, c15"
            "\n      dp3 r29.y, r16, c16"
            "\n      dp3 r29.z, r16, c17"
            // the screen gradients of P about the camera (a city's world coordinates, -8800 in Stormwind, would leave
            // them a few bits); the pixel's footprint on the glass, squared (r14.x)
            "\n      dsx r19.xyz, r15"
            "\n      dsy r20.xyz, r15"
            "\n      add r15.xyz, r15, c180"
            "\n      dp3 r14.x, r19, r19"
            "\n      dp3 r14.z, r20, r20"
            "\n      max r14.x, r14.x, r14.z"
            // the glass's thickness: grad u, grad v (x det) from the screen gradients; the texel h yd deeper (none
            // where the glass is nearly edge-on: det cos below 5% of the footprint's square)
            "\n      dsx r21.xy, " + vT +
            "\n      dsy r21.zw, " + vT + ".xyxy"
            "\n      crs r22.xyz, r20, r18"
            "\n      crs r23.xyz, r18, r19"
            "\n      dp3 r24.w, r19, r22"
            "\n      mul r24.xyz, r22, r21.x"
            "\n      mad r24.xyz, r23, r21.z, r24"
            "\n      mul r25.xyz, r22, r21.y"
            "\n      mad r25.xyz, r23, r21.w, r25"
            "\n      dp3 r26.x, r24, r16"
            "\n      dp3 r26.y, r25, r16"
            "\n      mul r26.z, r24.w, r17.w"
            "\n      mad r26.w, r14.x, -c206.y, r26_abs.z"
            "\n      add r26.w, r26.w, -c207.x"
            "\n      rcp r27.x, r26.z"
            "\n      cmp r27.x, r26.w, r27.x, c175.y"
            "\n      mul r26.xy, r26, r27.x"
            "\n      mul r26.xy, r26, -c193.w"
            "\n      max r26.xy, r26, -c206.y"
            "\n      min r26.xy, r26, c206.y"
            "\n      add r26.xy, r26, " + vT +
            "\n      texld r27, r26, s0"
            "\n      add r13.xyz, r13, r27"
            "\n      mul r13.xyz, r13, c175.z"
            // inside: the texel (more saturated) x (the sky + the sun beyond the pane), never below the stock colour (and
            // the texel's luminance, at least kTexelFloor, for the room's light: r16.w)
            "\n      dp3 r19.x, r13, c176"
            "\n      max r16.w, r19.x, c207.w"
            "\n      mov r19.w, c195.w"
            "\n      lrp r20.xyz, r19.w, r13, r19.x"
            "\n      max r20.xyz, r20, c175.y"
            "\n      dp3 r19.y, r18, c195"
            "\n      mov_sat r19.z, -r19.y"
            "\n      mad_sat r19.w, -r19.y, c175.z, c175.z"
            "\n      mov r21.w, c197.w"
            "\n      lrp r22.x, r21.w, r19.w, r19.z"
            "\n      mul r21.xyz, r22.x, c200"
            "\n      mul r21.xyz, r21, c194.w"
            "\n      add r21.xyz, r21, c194"
            "\n      mul r21.xyz, r21, c199.w"
            "\n      mul r21.xyz, r21, r20"
            "\n      max r21.xyz, r21, r12"
            // outside: Fresnel, the glass darker by day
            "\n      add r22.x, c175.x, -r17.w"
            "\n      mul r22.y, r22.x, r22.x"
            "\n      mul r22.y, r22.y, r22.y"
            "\n      mul r22.y, r22.y, r22.x"
            "\n      mov r22.z, c197.z"
            "\n      lrp r23.w, r22.y, c175.x, r22.z"
            "\n      mul r23.w, r23.w, c196.w"
            "\n      mad r23.xyz, r12, -c197.x, r12"
            // by day the painted glass cooler (env/GlassModel's DayCool, op for op; not the room behind it: that keeps its
            // own colours): toward its luminance x lerp(1, the sky's hue, c85.w) (the hue: the sky along the reflected
            // ray over its luminance, within c84.xy), by saturate(c85.x (1 + c85.y height + c85.z (1 - cos))): more toward
            // the top of the pane and at a slant; c85.x is 0 at night
            + sky +
            "\n      dp3 r25.w, r24, c176"
            "\n      max r25.w, r25.w, c206.y"
            "\n      rcp r25.w, r25.w"
            "\n      mul r25.xyz, r24, r25.w"
            "\n      max r25.xyz, r25, c84.x"
            "\n      min r25.xyz, r25, c84.y"
            "\n      add r25.xyz, r25, -c175.x"
            "\n      mul r25.xyz, r25, c85.w"
            "\n      add r25.xyz, r25, c175.x"
            "\n      dp3 r26.w, r23, c176"
            "\n      mul r25.xyz, r25, r26.w"
            "\n      mul r26.x, r24.w, c85.y"
            "\n      add r26.y, c175.x, -r17.w"
            "\n      mad r26.x, r26.y, c85.z, r26.x"
            "\n      mad_sat r26.x, r26.x, c85.x, c85.x"
            "\n      lrp r27.xyz, r26.x, r25, r23"
            "\n      mov r23.xyz, r27"
            // behind clear glass a room, behind lit frosted glass (c201.w) only its lights out: the window the point is in
            // (r14.y: the share of the client's glow hidden, none without the room or frosted glass; r14.z lit)
            "\n      mov r14.y, c175.y"
            "\n      mov r14.z, c175.x"
            "\n      mov r19.x, c193.z"
            "\n      max r19.x, r19.x, c201.w" + replayLook +
            "\n      if_gt r19.x, c175.y"
            "\n        mov r18.w, r17.w"
            // the group's grid: the cell holding the point (c18: its corner; c19.x 1 / its size; c20.xyz the last cell,
            // c20.w 1 with a grid), its texel (c18.w + x + c19.y y + c19.z z): its first window's texel and how many
            "\n        add r13.xyz, r28, -c18"
            "\n        mul r13.xyz, r13, c19.x"
            "\n        frc r19.xyz, r13"
            "\n        add r13.xyz, r13, -r19"
            "\n        cmp r19.xyz, r13, c175.x, c175.y"
            "\n        add r20.xyz, -r13, c20"
            "\n        cmp r19.xyz, r20, r19, c175.y"
            "\n        mul r19.x, r19.x, r19.y"
            "\n        mul r19.x, r19.x, r19.z"
            "\n        mul r19.x, r19.x, c20.w"
            "\n        mad r13.w, r13.y, c19.y, r13.x"
            "\n        mad r13.w, r13.z, c19.z, r13.w"
            "\n        add r13.w, r13.w, c18.w"
            + TableTexel("r13.w", "r20") +
            "\n        texldl r20, r20, s13"
            "\n        mul r20.y, r20.y, r19.x"
            // its windows, kEntryTexels texels each: the first whose rows (across, up over the half sizes; out over the
            // glass's thickness) put the point within 1 (r19.y the last one's, 2 with none; r19.z its texel)
            "\n        mov r19.x, c175.y"
            "\n        mov r19.y, c90.x"
            "\n        rep i1"
            "\n          break_ge r19.x, r20.y"
            "\n          mad r19.z, r19.x, c208.z, r20.x"
            + TableTexel("r19.z", "r22") +
            "\n          texldl r24, r22, s13"
            "\n          add r22.x, r22.x, c208.x"
            "\n          texldl r25, r22, s13"
            "\n          add r22.x, r22.x, c208.x"
            "\n          texldl r26, r22, s13"
            "\n          dp4 r27.x, r24, r28"
            "\n          dp4 r27.y, r25, r28"
            "\n          dp4 r27.z, r26, r28"
            "\n          max r27.w, r27_abs.x, r27_abs.y"
            "\n          max r19.y, r27.w, r27_abs.z"
            "\n          break_le r19.y, c175.x"
            "\n          add r19.x, r19.x, c175.x"
            "\n        endrep"
            "\n        if_le r19.y, c175.x"
            // the window's: its half sizes and key (r13: half width, half height, thickness, its ordinal), its room's box
            // (r30: half width, floor, ceiling; 1 frosted), its middle and its group (r20)
            "\n          add r22.x, r22.x, c208.x"
            "\n          texldl r13, r22, s13"
            "\n          add r22.x, r22.x, c208.x"
            "\n          texldl r30, r22, s13"
            "\n          add r22.x, r22.x, c208.x"
            "\n          texldl r20, r22, s13" + replayOpen +
            // lit or dark: the exact hash of the key (its fake room's: 3.w and 5.w, a room's one with whole rooms) and the
            // seed, P(M(P(M(x)) + M(y))) with M mod 289 and P the
            // permutation (34 v + 1) v (every step a whole number below 2^22: the same bits everywhere), then
            // h = (k + 0.5) / 289 against the dark band: at or below lo (c203.w) dark, above hi (c200.w) lit, between
            // part lit, saturate((h - lo) slope + base) (c212.w, c213.w): a room fading
            "\n          mov r22.x, r13.w"
            "\n          mov r22.y, r20.w"
            "\n          add r22.xy, r22, c212"
            "\n          add r22.zw, r22.xxxy, c214.z"
            "\n          mul r22.zw, r22, c214.x"
            "\n          frc r19.xy, r22.zwzw"
            "\n          add r22.zw, r22, -r19.xxxy"
            "\n          mad r22.xy, r22.zwzw, -c214.y, r22" + permute +
            "\n          add r22.x, r22.x, r22.y" + mod + permute +
            "\n          add r22.x, r22.x, c214.z"
            "\n          mul r22.x, r22.x, c214.x"
            "\n          add r14.z, r22.x, -c203.w"
            "\n          mul r22.y, r14.z, c212.w"
            "\n          add_sat r22.y, r22.y, c213.w"
            "\n          cmp r22.y, -r14.z, c175.y, r22.y"
            "\n          add r22.z, r22.x, -c200.w"
            "\n          cmp r14.z, -r22.z, r22.y, c175.x"
            // the flicker (lit frosted glass: a hearth's; a room's lamp with LampFlicker): its dip r21.w = c201.z x (0.5 +
            // sum w q (1 - |q|)), q = 2 frac((hash x spread + the hearth clock c201.y) x rate) - 1 for env/Flame's three
            // rates (c220.xyz, whole cycles in the clock's wrap), weights c221.xyz (env/GlassModel's HearthDip, op for op)
            "\n          mov r22.y, c201.y"
            "\n          mad r22.y, r22.x, c221.w, r22.y"
            "\n          mul r19.xyz, r22.y, c220"
            "\n          frc r19.xyz, r19"
            "\n          mad r19.xyz, r19, c90.x, c90.y"
            "\n          add r22.yzw, c175.x, -r19_abs.xxyz"
            "\n          mul r19.xyz, r19, r22.yzww"
            "\n          dp3 r19.x, r19, c221"
            "\n          add r19.x, r19.x, c175.z"
            "\n          mul r21.w, r19.x, c201.z"
            // a dark room (or dark frosted glass) behind the client's lit window at night hides most of its glow (c205.w),
            // a lit one the flicker's dip, from the whole window (the client lights all of it, the lead and the frame too:
            // hidden through the glass mask alone, the lead's edges and the glass's darker texels kept it and sparkled);
            // seen from inside, only the glass
            "\n          add r22.w, c175.x, -r14.z"
            "\n          mul r22.w, r22.w, c205.w"
            "\n          mad r22.w, r14.z, r21.w, r22.w" + replayKeep +
            "\n          mad r23.xyz, r23, -r22.w, r23"
            "\n          mad r14.y, r22.w, -c193.x, r22.w" + probe +
            // the room (clear and leaded glass: c193.z of it; not a frosted window), its lamp down by the flicker's dip too
            "\n          mov r22.w, c193.z"
            "\n          add r22.z, c26.y, -r30.w"
            "\n          cmp r22.w, r22.z, r22.w, c175.y" + replayKeep +
            "\n          if_gt r22.w, c175.y"
            // the window's frame (its rows' directions: across, up, out), the ray into the room (across, up, deep: away
            // from the eye, never edge-on: r28), where it enters (r27.xy: across, up about the window's middle, yd)
            "\n            nrm r17.xyz, r24"
            "\n            dp3 r28.x, r29, r17"
            "\n            nrm r17.xyz, r25"
            "\n            dp3 r28.y, r29, r17"
            "\n            nrm r17.xyz, r26"
            "\n            dp3 r28.z, r29, r17"
            "\n            mov r28.xy, -r28"
            "\n            max r28.z, r28_abs.z, c206.x"
            "\n            mul r27.xy, r27, r13"
            // each room its own: a hash of its key and the building's seed (r24: the curtains, the middle's prop, the
            // back's and the lamp across, the depth and the lamp deep)
            "\n            mov r24.x, r13.w"
            "\n            mov r24.y, r20.w"
            "\n            add r24.xy, r24, c212"
            "\n            mul r24, r24.xyxy, c88"
            "\n            frc r24, r24"
            "\n            add r25, r24.wzxy, c89.x"
            "\n            dp4 r25.x, r24, r25"
            "\n            add r24, r24, r25.x"
            "\n            add r25, r24.xxyz, r24.yzzw"
            "\n            mul r25, r25, r24.zywx"
            "\n            frc r24, r25"
            // the building (in a city, the block of the window's middle): a hash of the seed; its tint and brightness (r20)
            "\n            mul r25.xy, r20, c212.z"
            "\n            frc r25.zw, r25.xxxy"
            "\n            add r25.xy, r25, -r25.zwww"
            "\n            add r25.xy, r25.yxzw, c212"
            "\n            mul r25, r25.xyxy, c88"
            "\n            frc r25, r25"
            "\n            add r26, r25.wzxy, c89.x"
            "\n            dp4 r26.x, r25, r26"
            "\n            add r25, r25, r26.x"
            "\n            add r26, r25.xxyz, r25.yzzw"
            "\n            mul r26, r26, r25.zywx"
            "\n            frc r26.xy, r26"
            "\n            mov r20.xyz, c202"
            "\n            add r20.xyz, r20, -c199"
            "\n            mad r20.xyz, r20, r26.x, c199"
            "\n            mad r20.w, r26.y, c90.x, c90.y"
            "\n            mul r20.w, r20.w, c202.w"
            "\n            add r20.w, r20.w, c175.x"
            // its depth (r30.w: the class's c21.x, x (1 + c21.y (h.w - 0.5)))
            "\n            add r25.x, r24.w, -c175.z"
            "\n            mul r25.x, r25.x, c21.y"
            "\n            add r25.x, r25.x, c175.x"
            "\n            mul r30.w, r25.x, c21.x"
            // 1 / the eye's distance (r13.x), the pixel's footprint on the glass (yd, r13.y), the detail by the distance
            // (r13.z: c218, 1 near .. 0 far)
            "\n            add r25.xyz, r15, -c180"
            "\n            dp3 r25.x, r25, r25"
            "\n            rsq r13.x, r25.x"
            "\n            mad_sat r13.z, r25.x, c218.y, c218.x"
            "\n            add r25.y, r14.x, c207.x"
            "\n            rsq r25.y, r25.y"
            "\n            rcp r13.y, r25.y"
            "\n            max r13.y, r13.y, c26.x"
            // where the ray leaves the box: across (W - pa sign(da)) / |da|, up |bound - pb| / |db| (the ceiling going up,
            // the floor going down), deep D / dc; the nearest (r13.w); which face (r25: back wall, side wall, floor or
            // ceiling, ceiling); where (r26: across, up, deep)
            "\n            cmp r26.x, r28.x, r27.x, -r27.x"
            "\n            add r26.x, r30.x, -r26.x"
            "\n            cmp r26.y, r28.y, r30.z, r30.y"
            "\n            add r26.y, r26.y, -r27.y"
            "\n            max r26.zw, r28_abs.xxxy, c206.x"
            "\n            rcp r26.z, r26.z"
            "\n            rcp r26.w, r26.w"
            "\n            mul r26.x, r26.x, r26.z"
            "\n            mul r26.y, r26_abs.y, r26.w"
            "\n            rcp r26.z, r28.z"
            "\n            mul r26.z, r30.w, r26.z"
            "\n            min r26.w, r26.x, r26.y"
            "\n            min r13.w, r26.w, r26.z"
            "\n            add r25.x, r26.w, -r26.z"
            "\n            cmp r25.x, r25.x, c175.x, c175.y"
            "\n            add r25.y, r26.y, -r26.x"
            "\n            cmp r25.y, r25.y, c175.x, c175.y"
            "\n            mad r25.y, r25.x, -r25.y, r25.y"
            "\n            add r25.z, c175.x, -r25.x"
            "\n            add r25.z, r25.z, -r25.y"
            "\n            cmp r25.w, r28.y, r25.z, c175.y"
            "\n            mad r26.xy, r28, r13.w, r27"
            "\n            mul r26.z, r28.z, r13.w" + probeRoom +
            // on the face: its coordinates (r17.xy, yd: across or deep; down from the floor or deep), the ray's slant along
            // it (r17.zw) and its cosine to it (r15.x); the footprint there (yd, r15.y) stretched along the slant: the
            // texture's gradients (r19.xy, yd)
            "\n            lrp r17.x, r25.y, r26.z, r26.x"
            "\n            add r15.z, r30.y, -r26.y"
            "\n            lrp r17.y, r25.z, r26.z, r15.z"
            "\n            lrp r17.z, r25.y, r28.z, r28.x"
            "\n            lrp r17.w, r25.z, r28.z, -r28.y"
            "\n            mul r15.x, r25.x, r28.z"
            "\n            mad r15.x, r25.y, r28_abs.x, r15.x"
            "\n            mad r15.x, r25.z, r28_abs.y, r15.x"
            "\n            max r15.x, r15.x, c207.y"
            "\n            mad r15.y, r13.w, r13.x, c175.x"
            "\n            mul r15.y, r15.y, r13.y"
            "\n            mul r15.zw, r17, r17"
            "\n            add r15.z, r15.z, r15.w"
            "\n            add r15.z, r15.z, c206.x"
            "\n            rsq r15.z, r15.z"
            "\n            mul r19.xy, r17.zwzw, r15.z"
            "\n            rcp r15.w, r15.x"
            "\n            add r15.w, r15.w, -c175.x"
            "\n            mad r19.xy, r19_abs, r15.w, c175.x"
            "\n            mul r19.xy, r19, r15.y"
            // its light (r15.x by day, r15.y at night): the room's at the point x the face's shade (c209, c210: side walls,
            // floor, ceiling, back wall), the corners darker (each other face within kAoRadius; far away their mean), and
            // by day the sun's patch on the floor by the window (x its cosine to the glass, c86.x)
            + RoomLight("r26", "r15.x", "r15.y", "r22") +
            "\n            mov r22.x, r25.y"
            "\n            add r22.y, r25.z, -r25.w"
            "\n            mov r22.z, r25.w"
            "\n            mov r22.w, r25.x"
            "\n            dp4 r15.z, r22, c209"
            "\n            dp4 r15.w, r22, c210"
            "\n            lrp r22.zw, c26.z, c26.w, r15"
            "\n            mul r15.xy, r15, r22.zwzw"
            "\n            add r22.x, r30.x, -r26_abs.x"
            "\n            add r22.y, r26.y, -r30.y"
            "\n            add r22.z, r30.z, -r26.y"
            "\n            min r22.y, r22.y, r22.z"
            "\n            add r22.z, r30.w, -r26.z"
            "\n            mov r22.w, r26.z"
            "\n            mad r22.x, r25.y, c207.z, r22.x"
            "\n            mad r22.y, r25.z, c207.z, r22.y"
            "\n            mad r22.z, r25.x, c207.z, r22.z"
            "\n            mul_sat r22, r22, c215.x"
            "\n            add r22, c175.x, -r22"
            "\n            mul r22, r22, r22"
            "\n            mul r22, r22, -c215.y"
            "\n            add r22, r22, c175.x"
            "\n            mul r22.x, r22.x, r22.y"
            "\n            mul r22.x, r22.x, r22.z"
            "\n            mul r22.x, r22.x, r22.w"
            "\n            lrp r22.y, r13.z, r22.x, c215.w"
            "\n            mul r15.xy, r15, r22.y"
            "\n            dp3_sat r22.y, r18, c195"
            "\n            add r22.z, -r26.z, c83.x"
            "\n            mul_sat r22.z, r22.z, c83.y"
            "\n            mul r22.y, r22.y, r22.z"
            "\n            mul r22.z, r30.x, c84.z"
            "\n            add r22.z, r22.z, -r26_abs.x"
            "\n            mul_sat r22.z, r22.z, c84.w"
            "\n            mul r22.y, r22.y, r22.z"
            "\n            add r22.z, r25.z, -r25.w"
            "\n            mul r22.y, r22.y, r22.z"
            "\n            mad r15.x, r22.y, c86.x, r15.x"
            "\n            mov r15.zw, r25"
            // its textures: the floor (s8), the walls (s9), the ceiling (s10), each at its scale (c22) and gain (c23: a mean
            // of 1), its gradients the footprint's; far away their mean
            "\n            mov r22, c175.y"
            "\n            mov r29, c175.y"
            "\n            mul r26.xy, r17, c22.x"
            "\n            mul r22.x, r19.x, c22.x"
            "\n            mul r29.y, r19.y, c22.x"
            "\n            texldd r26, r26, s8, r22, r29"
            "\n            mul r26.xyz, r26, c23.x"
            "\n            mul r25.xy, r17, c22.y"
            "\n            mul r22.x, r19.x, c22.y"
            "\n            mul r29.y, r19.y, c22.y"
            "\n            texldd r25, r25, s9, r22, r29"
            "\n            mul r25.xyz, r25, c23.y"
            "\n            lrp r26.xyz, r15.z, r26, r25"
            "\n            mul r25.xy, r17, c22.z"
            "\n            mul r22.x, r19.x, c22.z"
            "\n            mul r29.y, r19.y, c22.z"
            "\n            texldd r25, r25, s10, r22, r29"
            "\n            mul r25.xyz, r25, c23.z"
            "\n            lrp r17.xyz, r15.w, r25, r26"
            "\n            mov r26.xyz, r17"
            "\n            add r26.xyz, r26, -c175.x"
            "\n            mad r26.xyz, r26, r13.z, c175.x"
            // the light: by day (x the building's brightness, x the light the client puts on the glass: its colour's
            // luminance over its texel's (not the painted glass's pattern), at most kLightCap, x kGlassAlbedo: c198.w makes
            // the room about room::kDayRoom of the glass's brightness; a dark room keeps some: r19.x) in the day's colour
            // (c198.xyz: the sky's and the sun's through a pane, luminance 1: orange at dusk), at night the lamp's in lit
            // rooms behind the client's lit windows (c203, as bright as the window: x that light; down by the flicker's
            // dip: r19.y); x the building's tint
            "\n            dp3 r19.z, r12, c176"
            "\n            rcp r19.w, r16.w"
            "\n            mul r19.z, r19.z, r19.w"
            "\n            min r19.z, r19.z, c83.z"
            "\n            mul r19.z, r19.z, c98.y"
            "\n            mad r19.x, r14.z, c218.z, c218.w"
            "\n            mul r19.x, r19.x, r20.w"
            "\n            mul r19.x, r19.x, c198.w"
            "\n            mul r19.x, r19.x, r19.z"
            "\n            mul r19.y, r19.z, r14.z"
            "\n            mad r19.y, r19.y, -r21.w, r19.y"
            "\n            mul r15.xy, r15, r19"
            "\n            mul r22.xyz, r15.y, c203"
            "\n            mad r22.xyz, c198, r15.x, r22"
            "\n            mul r22.xyz, r22, r20"
            "\n            mul r26.xyz, r26, r22"
            // the furniture: the back wall's (kBackGap in front of it), then the middle's (kMidDepth of the way back)
            "\n            add r25.z, r30.w, -c94.y"
            + FurnitureLayer("r24.z", "c25") +
            "\n            mul r25.z, r24.y, c95.z"
            "\n            frc r25.z, r25.z"
            "\n            mov r25.x, c94.z"
            "\n            mad r25.z, r25.z, c94.w, r25.x"
            "\n            mul r25.z, r25.z, r30.w"
            + FurnitureLayer("r24.y", "c24") +
            // curtains half drawn just behind the glass (the rooms whose hash falls in the style's band, c204.xy): the gap
            // between them (its middle and width by the hash, its edges a pixel soft), the folds (fading where they'd
            // shimmer); lit from the window by day, glowing with the lamp behind them at night
            "\n            add r15.x, r24.x, -c204.x"
            "\n            cmp r15.x, r15.x, c175.x, c175.y"
            "\n            add r15.y, r24.x, -c204.y"
            "\n            cmp r15.y, r15.y, c175.y, r15.x"
            "\n            rcp r25.w, r28.z"
            "\n            mul r25.w, r25.w, c97.x"
            "\n            mad r25.x, r28.x, r25.w, r27.x"
            "\n            mad r15.x, r25.w, r13.x, c175.x"
            "\n            mul r15.x, r15.x, r13.y"
            "\n            mul r15.z, r24.x, c95.w"
            "\n            frc r15.z, r15.z"
            "\n            mov r15.w, c97.y"
            "\n            mad r15.w, r15.z, c97.z, r15.w"
            "\n            mul r15.w, r15.w, r30.x"
            "\n            add r15.z, r15.z, -c175.z"
            "\n            mul r15.z, r15.z, r30.x"
            "\n            mul r15.z, r15.z, c89.y"
            "\n            add r22.x, r25.x, -r15.z"
            "\n            add r22.x, r22_abs.x, -r15.w"
            "\n            rcp r22.y, r15.x"
            "\n            mad_sat r22.x, r22.x, r22.y, c175.z"
            "\n            mul r15.y, r15.y, r22.x"
            "\n            mul r22.x, r25.x, c97.w"
            "\n            frc r22.x, r22.x"
            "\n            add r22.x, r22.x, -c175.z"
            "\n            add r22.x, r22_abs.x, r22_abs.x"
            "\n            add r22.x, r22.x, -c175.z"
            "\n            mul r22.y, r15.x, c98.w"
            "\n            add_sat r22.y, c175.x, -r22.y"
            "\n            mul r22.y, r22.y, c204.w"
            "\n            mul r22.y, r22.y, c98.x"
            "\n            mad r22.x, r22.x, r22.y, c175.x"
            "\n            mul r22.yzw, r19.y, c203.xxyz"
            "\n            mul r22.yzw, r22, c87.z"
            "\n            mad r22.yzw, r19.x, c87.w, r22"
            "\n            mul r22.yzw, r22, c205.xxyz"
            "\n            mul r22.yzw, r22, r22.x"
            "\n            lrp r17.xyz, r15.y, r22.yzww, r26"
            // tinted by the glass's colours (stained glass: x lerp(1, the inside colour over its luminance, c27.y); any
            // other kind x 1), then the room behind the glass
            "\n            dp3 r30.w, r21, c176"
            "\n            max r30.w, r30.w, c206.x"
            "\n            rcp r30.w, r30.w"
            "\n            mul r30.xyz, r21, r30.w"
            "\n            mad r30.xyz, r30, c27.y, c27.x"
            "\n            mul r17.xyz, r17, r30"
            "\n            mov r29.w, c193.z"
            "\n            lrp r30.xyz, r29.w, r17, r23"
            "\n            mov r23.xyz, r30"
            "\n          endif"
            "\n        endif"
            "\n        mov r17.w, r18.w"
            "\n      endif"
            // the sky in it (deeper toward the zenith: the reflected ray's height, 2 cos N.z - V.z) and the sun's
            // glint; then inside or outside, by c193.x
            + sky +
            "\n      lrp r25.xyz, r23.w, r24, r23"
            "\n      add r26.xyz, r16, c195"
            "\n      nrm r27.xyz, r26"
            "\n      dp3_sat r26.w, r18, r27"
            "\n      pow r26.w, r26.w, c206.w"
            "\n      mul r26.w, r26.w, c197.y"
            "\n      mad r25.xyz, c200, r26.w, r25"
            "\n      mov r26.x, c193.x"
            "\n      lrp r27.xyz, r26.x, r21, r25"
            // where the client's glow is hidden (outside at night: a dark room, a dim one, the flicker's dip) the rest of
            // the window (the lead, the frame, the texels the mask leaves) at least as much as the glass went dark: a dark
            // room's lead never glows brighter than its glass (r14.y: at least 1 - the glass's luminance over the client's)
            "\n      dp3 r26.y, r27, c176"
            "\n      dp3 r26.z, r12, c176"
            "\n      max r26.z, r26.z, c206.x"
            "\n      rcp r26.z, r26.z"
            "\n      mad_sat r26.y, r26.y, -r26.z, c175.x"
            "\n      max r26.y, r26.y, r14.y"
            "\n      cmp r14.y, -r14.y, r14.y, r26.y"
            // the glass by its mask; the rest of the window (1 - hidden) of the client's colour
            "\n      add r27.xyz, r27, -r12"
            "\n      mad r27.xyz, r12, r14.y, r27"
            "\n      mad r31.xyz, r27, r14.w, r31"
            "\n      mad r31.xyz, r12, -r14.y, r31" + replayDebug +
            "\n    endif";
    }

    /// Whether a pixel shader's code (its tokens, version token first) has the redraw: an `if b11` (D3DSIO_IF, one parameter,
    /// on the bool constant kReplaySwitch). features/SeeThrough redraws a batch see-through only with such a shader (a
    /// variant without the glass blocks would give its stock alpha, not the mask's).
    inline bool HasReplaySwitch(const uint32_t* tokens, size_t count)
    {
        // D3DSPR_CONSTBOOL (14): its low three bits at 28..30, its high two at 11..12; any swizzle, no modifier
        constexpr uint32_t kIf = 0x01000028u;
        constexpr uint32_t kB11 = 0x80000000u | (6u << 28) | (1u << 11) | static_cast<uint32_t>(kReplaySwitch);
        if (!tokens) return false;
        for (size_t i = 0; i + 1 < count; ++i)
            if (tokens[i] == kIf && (tokens[i + 1] & ~0x00FF0000u) == kB11) return true;
        return false;
    }

    /// The redraw's alpha, after the stock alpha is in r12.w (the edit moves the stock shader's alpha write there and writes
    /// oC0.w from it after this): under b11 the batch's own alpha test on the stock alpha (c219.y; -1 none: the device's
    /// alpha test is the redraw's own split, features/SeeThrough), then 1 - the mask x open x (1 - the opacity, c219.x).
    /// Reads r14.w and r31.w (GlassBlock's); r13 a temporary.
    inline std::string ReplayAlpha()
    {
        return "\n    if b11"
               "\n      add r13, r12.w, -c219.y"
               "\n      texkill r13"
               "\n      max r13.y, r31.w, c175.y"
               "\n      mul r13.x, r14.w, r13.y"
               "\n      mul r13.x, r13.x, c219.x"
               "\n      add r12.w, -r13.x, c175.x"
               // the redraw's two draws (c28.y: +1 the opaque, -1 the see-through), split here, not by the device's alpha test
               // (a driver's alpha-to-coverage or a translation layer may treat that otherwise): keep alpha >= c28.z, or below
               "\n      add r13, r12.w, -c28.z"
               "\n      mul r13, r13, c28.y"
               "\n      texkill r13"
               "\n    endif";
    }

    inline std::string FloorBlock(const std::string& vP, const std::string& vN, const std::string& vT)
    {
        std::string s =
            "\n    if b10"
            "\n      texld r13, " + vT + ", s0"
            "\n      dp3 r15.x, " + vP + ", c181"
            "\n      dp3 r15.y, " + vP + ", c182"
            "\n      dp3 r15.z, " + vP + ", c183"
            "\n      mov r15.w, c175.x"
            "\n      nrm r17.xyz, " + vN +
            "\n      dp3 r18.x, r17, c181"
            "\n      dp3 r18.y, r17, c182"
            "\n      dp3 r18.z, r17, c183"
            "\n      dp3_sat r18.w, r18, c195"
            "\n      mov r19.xyz, c175.y";
        // Two panes at a time (2026-10-24, sixteen in the slots twelve took): the pair's first present (its colour's w 1, r15.w
        // 1; the panes packed from the first, an absent one all zeros adds nothing); its patch's soft edges by c58 (|a|, |b|
        // across and up, t along the light, none behind the glass), its room by its middle and half size.
        const std::string soft = "c" + std::to_string(kPaneSoftConstant);
        for (int k0 = 0; k0 < kFloorPanes; k0 += 2)
        {
            s += "\n      if_ge c" + std::to_string(PaneRegister(k0) + 3) + ".w, r15.w";
            for (int k = k0; k < k0 + 2; ++k)
            {
                auto c = [k](int r) { return "c" + std::to_string(PaneRegister(k) + r); };
                s += "\n        dp4 r21.x, r15, " + c(0) +
                     "\n        dp4 r21.y, r15, " + c(1) +
                     "\n        dp4 r21.z, r15, " + c(2) +
                     "\n        add r22.xyz, c175.x, -r21_abs"
                     "\n        mul_sat r22.xyz, r22, " + soft +
                     "\n        mul r22.x, r22.x, r22.y"
                     "\n        mul r22.x, r22.x, r22.z"
                     "\n        cmp r22.x, r21.z, r22.x, c175.y"
                     "\n        add r23.xyz, r15, -" + c(4) +
                     "\n        add r23.xyz, " + c(5) + ", -r23_abs"
                     "\n        min r23.x, r23.x, r23.y"
                     "\n        min r23.x, r23.x, r23.z"
                     "\n        cmp r22.x, r23.x, r22.x, c175.y"
                     "\n        mad r19.xyz, " + c(3) + ", r22.x, r19";
            }
            s += "\n      endif";
        }
        s += "\n      mul r19.xyz, r19, r18.w"
             "\n      mul r19.xyz, r19, r13"
             "\n      mad r31.xyz, r19, c201.x, r31"
             "\n    endif";
        return s;
    }
}
