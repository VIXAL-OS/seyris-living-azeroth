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
//  (c197.x: clear most, stained barely), behind clear glass a room (interior mapping in the pane's own frame: cells
//  c198.xy wide and high, 1 / c198.z deep; by day seen through the glass, its tint x the client's colour's luminance x
//  c198.w, darker than the glass (room::kDayRoom), farther darker (c86.y), a patch of sun on its floor by the window
//  (c86.x); c193.z of it), by day the two cooler (c85: toward the sky's hue at their luminance, more toward the top of
//  the pane and at a slant, env/GlassModel's DayCool), with the sky in it (Schlick's Fresnel from F0 c197.z, x c196.w;
//  the sky deeper toward the zenith along the reflected ray) and the sun's glint; inside and outside mixed by c193.x.
//  The room's look (c218, c219: env/GlassModel's RoomLook, by day or night): its walls' shade is soft (how near the
//  point the ray meets is to each wall, smoothstepped over the walls' width: by day a gradient, the corners darker; at
//  night about a pixel there), the doorway's, the curtains' and the shelves' edges over the layouts' width; the detail
//  fades over the distance (c219.xy) and the things on the shelves keep c218.w of their contrast; by day near a
//  cell's edge on the glass, or its anchor's (c219.zw), the room eases to its mean, so neighbouring rooms meet without
//  a step and a mullion no longer splits a window into different blocks.
//  The room (env/GlassModel's RoomOutside works it out the same way, op for op): its cell is the point about the
//  building's origin (c213; farther than 32 yd on an axis, about the nearest 64-yd anchor from it, the anchor's index
//  in the cell: c215..c217) in the face's own frame, so it doesn't depend on the camera or on a city's -8800 yd
//  coordinates. Its building (a hash of the seed c212.xy, in a city of its 14-yd block too: c212.z) picks its tint
//  between c199 and c202 and its brightness (c202.w); each room (a hash of its cell and the seed) its layout (c204.xyz:
//  the class's cumulative weights for shelves, curtains half drawn, a dividing wall with a doorway, else plain), its
//  light, the lamp's place, the curtains' gap, the things on its shelves; and whether it's lit: an exact hash of the
//  cell and the seed (env/GlassModel's LitHash: whole numbers mod 289, the same bits on any GPU and on the CPU, so the
//  window lights know which rooms are dark) against the dark band (env/GlassModel's DarkBand: c203.w lo, c200.w hi,
//  c212.w slope, c213.w base: a room the threshold crossed in the last 1.5 s, or every room a jump of the time
//  crossed, is part lit, so it fades). The cell's frame is the face's own normal (from P's screen gradients), not the
//  vertex normal: smoothed glass doesn't bend the cells. At night the lit rooms behind the client's lit
//  windows have a lamp (c203.rgb, as bright as the client's window: x its luminance); dark ones there hide most of its
//  glow (c205.w) from the whole window, lead and frame too (the client lights all of it). The detail fades from 30 to
//  70 yd at night, 15 to 40 by day (x c204.w), and where a pixel's footprint on the glass passes 0.05 .. 0.15 yd.
//  Curtains c205.rgb.
//  Lit frosted glass (c201.w: the client lights it, SIDN or unlit) has no room, but the same cell, lit hash and dark band
//  run for it, and a dark one hides c205.w of the window's glow as a dark room does; a lit one (and, with LampFlicker,
//  a lamp-lit room and its lamp) flickers like a hearth: its glow down by the dip c201.z x (0.5 + the three waves of
//  env/GlassModel's HearthDip on the hearth clock c201.y, each window its own phase from its lit hash), so the window
//  light in front (features/Windows) dips with it.
// Floor light (b10: on for the batches of a building's interior groups while sunlit panes are near): up to
//  kFloorPanes panes, each as three rows taking the point (camera-relative, world axes) to where the ray toward the
//  sun crosses the pane (|a|, |b| < 1) and how far (0 < t < 1), with soft edges, kept inside its room's box; x the
//  surface's own texel x its facing to the sun.
//
// Registers (WMO pixel shaders; the stock ones use r0..r11, c0..c14, s0..s7 and no b/i registers):
//  b9 glass, b10 floor light; r12..r31 (shared with the ice and the rims, see ModelIce.hpp);
//  c180..c183 the camera and the view's rows (terrainice::FillModelConstants, as the ice); c175, c176 (the ice's defs);
//  c192 mask plane; c193 (inside weight, strength, the room's share, thickness yd); c194 (the sky through the glass
//  rgb, the sun's gain); c195 (toward the sun, world xyz; saturation inside); c196 (the reflected sky rgb, Fresnel
//  strength); c197 (darkening outside, glint, F0, diffusion); c198 (1 / the room's cell width, height, depth;
//  its daylight); c199 (its tint rgb; the inside's brightness); c200 (the sun's colour; the dark band's hi); c201 (the
//  floor light's gain, the hearth clock, the flicker's amplitude, lit frosted glass 1);
//  c202 (the room's other tint rgb, the buildings' spread in brightness); c203 (the lamp rgb, the share of rooms
//  dark band's lower end); c204 (the layouts' thresholds, the detail's strength); c205 (the curtains rgb, the glow dark
//  rooms hide); c212 (the building's seed xy, 1 / its block (0: one building), the dark band's slope); c213 (the
//  building's origin, world xyz; the dark band's base); c218 (the walls' and the layouts' soft widths, the pixel's
//  share of them, the things' contrast), c219 (the detail's fade over the distance squared, the ease at the cells'
//  edges: 1 / its width, its base); c85 (by day the glass cooler: how much, more up, more at a slant, the sky's hue's
//  share), c86 (by day the room: the sun's patch's gain, farther darker; zeros: the night's look); c206..c211, c214 (the
//  lit hash's numbers), c215..c217 (the anchors' step and keys), c220, c221 (the flicker's rates, weights and spread),
//  c83, c84 (the sun's patch's shape, the sky's hue's bounds) and c87..c100 (the room's numbers, env/GlassModel's
//  glass::room; c208 the footprint's fade) defined; the floor light's panes c101..c172 (six registers each; the rims' range: a
//  batch has one or the other, features/Glass re-uploads when the rims took it). ps_3_0 has c0..c223, and an
//  instruction reads at most one constant register.
#pragma once

#include "../env/GlassModel.hpp"

#include <cstdio>
#include <string>

namespace wxl_livingazeroth::glass
{
    constexpr int kSwitch = 9;           // PS b9: the glass
    constexpr int kFloorSwitch = 10;     // PS b10: the floor light
    constexpr int kViewConstant = 180;   // c180..c183 (FillModelConstants' first four)
    constexpr int kFirstConstant = 192;  // c192..c205
    constexpr int kConstantCount = 14;
    constexpr int kSeedConstant = 212;   // c212: the building's seed, its block, the dark band; c213: its origin
    constexpr int kSeedRegisters = 2;
    constexpr int kPaneConstant = 101;   // c101..c172
    constexpr int kPaneRegisters = 6;
    constexpr int kFloorPanes = 12;
    constexpr int kLookConstant = 218;   // c218, c219: the room's look by day and night (RoomLook)
    constexpr int kLookRegisters = 2;
    constexpr int kDayConstant = 85;     // c85: the glass cooler by day (DayCool); c86: the room by day (the sun's patch, farther darker)
    constexpr int kDayRegisters = 2;     // (zeros: neither, the night's look)

    inline std::string ShaderDefs()
    {
        using namespace room;
        auto def = [](int reg, float a, float b, float c, float d) {
            char line[128];
            std::snprintf(line, sizeof(line), "    def c%d, %.9g, %.9g, %.9g, %.9g\n", reg, a, b, c, d);
            return std::string(line);
        };
        return "    def c206, 0.0001, 0.05, 0, 300\n" +
               def(207, -1.0f, 1.0f, kFar, 0.0f) +
               def(208, kFootprintB, kFootprintA, 1e-12f, 0.0f) +
               def(209, kFace[0], kFace[1], kFace[2], kFace[3]) +
               def(210, kCellLight[0], kCellLight[1], kEdgeMean, kSlotSoft) +
               def(211, kZenith[0], kZenith[1], kZenith[2], 3.0f) +
               def(83, kPatchDeep, 1.0f / kPatchDeepSoft, 1.0f / kPatchHigh, kEdgePixels) +
               def(84, kSkyHue[0], kSkyHue[1], kPatchAcross, 1.0f / kPatchSoft) +
               def(87, 1.0f - kDarkDay, kDarkDay, kCurtainGain, kDarkDayMean) +
               def(88, kHashK[0], kHashK[1], kHashK[2], kHashK[3]) +
               def(89, kHashAdd, kDividerDepth, kDoorSpan, kDoorBase) +
               def(90, 2.0f, -1.0f, kDoorHalfWidth, kDoorHeight) +
               def(91, kCurtainDepth, kGapSpan, kGapBase, kClothEdge) +
               def(92, kGapMinHalf, kGapHalfSpan, kGapHash, kFolds) +
               def(93, kSlots, kShelves, kPlank, kThingMax) +
               def(94, kLamp[0], kLamp[1], kLamp[2], kLamp[3]) +
               def(95, 1.0f / kLampK, kLampGain / kLampK, kLampBase, kDividerShade) +
               def(96, kShelfCentre[0], kShelfCentre[1], kShelfHalf[0], kShelfHalf[1]) +
               def(97, kFoldAmp, kFoldBase, kFoldMean, kThingGap) +
               def(98, kShelfHashShelf, kShelfHashRoom, kThingHash, kDoorDim) +
               def(99, kThingSpan, kThingBase, kShelfBack, kPlankLight) +
               def(100, kPlankMid, kPlankHalf, kClothFar, kCurtainDay) +
               def(214, kLitInv, kLitMod, 0.5f, kLitPoly) +
               def(215, 1.0f / kAnchor, 0.5f, kAnchor, 0.5f * kAnchor) +
               def(216, kAnchorKeyX[0], kAnchorKeyX[1], kAnchorKeyX[2], 0.0f) +
               def(217, kAnchorKeyY[0], kAnchorKeyY[1], kAnchorKeyY[2], 0.0f) +
               def(220, hearth::kRate[0], hearth::kRate[1], hearth::kRate[2], 0.0f) +
               def(221, hearth::kWeight[0], hearth::kWeight[1], hearth::kWeight[2], hearth::kSpread);
    }

    /// `probe` (the tests'): instructions put right after the room's lit value, the flicker and the glow hidden are
    /// worked out (r20.w lit, r22.x the lit hash, r29.xy the cell about the building's origin, r21.w the flicker's dip,
    /// r14.y the share of the glow hidden).
    inline std::string GlassBlock(const std::string& vP, const std::string& vN, const std::string& vT, const std::string& probe = std::string())
    {
        // Mod 289 of r22.x (a whole number below 2^22) in place, exactly: v - floor((v + 0.5) / 289) 289.
        const std::string mod =
            "\n        add r22.z, r22.x, c214.z"
            "\n        mul r22.z, r22.z, c214.x"
            "\n        frc r22.w, r22.z"
            "\n        add r22.z, r22.z, -r22.w"
            "\n        mad r22.x, r22.z, -c214.y, r22.x";
        // The permutation (34 v + 1) v mod 289 of r22.x in place.
        const std::string permute =
            "\n        mul r22.z, r22.x, c214.w"
            "\n        mad r22.x, r22.z, r22.x, r22.x" + mod;
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
            "\n      mul r14.w, r14.w, c193.y"
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
            // the screen gradients of P about the camera (a city's world coordinates, -8800 in Stormwind, would leave
            // them a few bits); the pixel's footprint on the glass, squared (r14.x: the room's detail fades by it)
            "\n      dsx r19.xyz, r15"
            "\n      dsy r20.xyz, r15"
            // (the face's own normal, unscaled: the rooms' cells go by it, not the vertex normal's wobble)
            "\n      crs r17.xyz, r19, r20"
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
            "\n      add r26.w, r26.w, -c208.z"
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
            // inside: the texel (more saturated) x (the sky + the sun beyond the pane), never below the stock colour
            "\n      dp3 r19.x, r13, c176"
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
            // behind clear glass, a room: interior mapping in the pane's frame (along, up, deep), in cells; behind lit
            // frosted glass (c201.w) only the cell and its lit hash: the glass itself goes dark as a room would (r14.y: the
            // share of the client's glow hidden, none without the room or frosted glass)
            "\n      mov r14.y, c175.y"
            "\n      mov r19.x, c193.z"
            "\n      max r19.x, r19.x, c201.w"
            "\n      if_gt r19.x, c175.y"
            // the pane's frame from the face's normal (no gradients: the vertex normal's), facing the eye
            "\n        dp3 r22.w, r17, r17"
            "\n        cmp r22.xyz, -r22.w, r18, r17"
            "\n        nrm r26.xyz, r22"
            "\n        dp3 r22.w, r26, r16"
            "\n        cmp r22.xyz, r22.w, r26, -r26"
            "\n        mul r24.xy, r22.yxxx, c207"
            "\n        mov r24.z, c175.y"
            "\n        dp3 r24.w, r24, r24"
            "\n        add r25.x, r24.w, -c206.x"
            "\n        cmp r24.xyz, r25.x, r24, c175.xyyy"
            "\n        nrm r25.xyz, r24"
            "\n        crs r26.xyz, r22, r25"
            "\n        dp3 r27.x, -r16, r25"
            "\n        dp3 r27.y, -r16, r26"
            "\n        max r27.z, r17.w, c206.x"
            "\n        mul r27.xyz, r27, c198"
            // (the point about the building's origin, then its anchor's: no camera in it, never far, so the face
            // normal's noise barely moves the cells; the anchor's offset to the cell, whole numbers)
            "\n        add r29.xyz, r15, -c213"
            "\n        mad r28.xyz, r29, c215.x, c215.y"
            "\n        frc r30.xyz, r28"
            "\n        add r28.xyz, r28, -r30"
            "\n        mad r29.xyz, r28, -c215.z, r29"
            "\n        dp3 r30.x, r28, c216"
            "\n        dp3 r30.y, r28, c217"
            // (how far along the glass the nearest anchor's edge is, yd: where the cells are cut, as at a cell's edge;
            // each axis's plane over sqrt(|1 - N_i^2|), none parallel to the glass; a unit N's rounding never gives a
            // negative root): r24.x
            "\n        mul r24.xyz, r22, r22"
            "\n        add r24.xyz, -r24, c175.x"
            "\n        add r24.xyz, r24_abs, c208.z"
            "\n        rsq r24.x, r24.x"
            "\n        rsq r24.y, r24.y"
            "\n        rsq r24.z, r24.z"
            "\n        add r20.xyz, -r29_abs, c215.w"
            "\n        mul r20.xyz, r20, r24"
            "\n        min r20.x, r20.x, r20.y"
            "\n        min r24.x, r20.x, r20.z"
            "\n        dp3 r28.x, r29, r25"
            "\n        dp3 r28.y, r29, r26"
            "\n        mul r28.xy, r28, c198"
            "\n        frc r28.zw, r28.xxxy"
            "\n        add r29.xy, r28, -r28.zwww"
            "\n        add r29.xy, r29, r30"
            "\n        cmp r29.zw, r27.xxxy, c175.x, c175.y"
            "\n        add r29.zw, r29, -r28"
            // (the way to the wall over the ray's step, both as sizes: a step of 0 or -0 never divides)
            "\n        max r30.xy, r27_abs, c206.x"
            "\n        rcp r30.x, r30.x"
            "\n        rcp r30.y, r30.y"
            "\n        rcp r30.z, r27.z"
            "\n        mul r30.xy, r29_abs.zwww, r30"
            "\n        min r30.w, r30.x, r30.y"
            "\n        min r30.w, r30.w, r30.z"
            // lit or dark: the exact hash of the cell and the seed, P(M(P(M(x)) + M(y))) with M mod 289 and P the
            // permutation (34 v + 1) v (every step a whole number below 2^22: the same bits everywhere), then
            // h = (k + 0.5) / 289 against the dark band: at or below lo (c203.w) dark, above hi (c200.w) lit, between
            // part lit, saturate((h - lo) slope + base) (c212.w, c213.w): a room fading
            "\n        add r22.xy, r29, c212"
            "\n        add r22.zw, r22.xxxy, c214.z"
            "\n        mul r22.zw, r22, c214.x"
            "\n        frc r28.xy, r22.zwzw"
            "\n        add r22.zw, r22, -r28.xxxy"
            "\n        mad r22.xy, r22.zwzw, -c214.y, r22" + permute +
            "\n        add r22.x, r22.x, r22.y" + mod + permute +
            "\n        add r22.x, r22.x, c214.z"
            "\n        mul r22.x, r22.x, c214.x"
            "\n        add r20.w, r22.x, -c203.w"
            "\n        mul r22.y, r20.w, c212.w"
            "\n        add_sat r22.y, r22.y, c213.w"
            "\n        cmp r22.y, -r20.w, c175.y, r22.y"
            "\n        add r22.z, r22.x, -c200.w"
            "\n        cmp r20.w, -r22.z, r22.y, c175.x"
            // the flicker (lit frosted glass: a hearth's; a room's lamp with LampFlicker): its dip r21.w = c201.z x (0.5 +
            // sum w q (1 - |q|)), q = 2 frac((hash x spread + the hearth clock c201.y) x rate) - 1 for env/Flame's three
            // rates (c220.xyz, whole cycles in the clock's wrap), weights c221.xyz (env/GlassModel's HearthDip, op for op)
            "\n        mov r22.y, c201.y"
            "\n        mad r22.y, r22.x, c221.w, r22.y"
            "\n        mul r19.xyz, r22.y, c220"
            "\n        frc r19.xyz, r19"
            "\n        mad r19.xyz, r19, c90.x, c90.y"
            "\n        add r22.yzw, c175.x, -r19_abs.xxyz"
            "\n        mul r19.xyz, r19, r22.yzww"
            "\n        dp3 r19.x, r19, c221"
            "\n        add r19.x, r19.x, c175.z"
            "\n        mul r21.w, r19.x, c201.z"
            // a dark room (or dark frosted glass) behind the client's lit window at night hides most of its glow (c205.w),
            // a lit one the flicker's dip, from the whole window (the client lights all of it, the lead and the frame too:
            // hidden through the glass mask alone, the lead's edges and the glass's darker texels kept it and sparkled);
            // seen from inside, only the glass
            "\n        add r22.w, c175.x, -r20.w"
            "\n        mul r22.w, r22.w, c205.w"
            "\n        mad r22.w, r20.w, r21.w, r22.w"
            "\n        mad r23.xyz, r23, -r22.w, r23"
            "\n        mad r14.y, r22.w, -c193.x, r22.w" + probe +
            // the room (clear and leaded glass: c193.z of it), its lamp down by the flicker's dip too
            "\n        mov r19.w, c193.z"
            "\n        if_gt r19.w, c175.y"
            // each room its own: a hash of the cell and the building's seed (layout, dark, light and lamp, the rest)
            "\n          add r13.xy, r29, c212"
            "\n          mul r13, r13.xyxy, c88"
            "\n          frc r13, r13"
            "\n          add r25, r13.wzxy, c89.x"
            "\n          dp4 r25.x, r13, r25"
            "\n          add r13, r13, r25.x"
            "\n          add r25, r13.xxyz, r13.yzzw"
            "\n          mul r25, r25, r13.zywx"
            "\n          frc r13, r25"
            // the building (in a city, its block): a hash of the seed; its tint and its brightness
            "\n          mul r25.xy, r15, c212.z"
            "\n          frc r25.zw, r25.xxxy"
            "\n          add r25.xy, r25, -r25.zwww"
            "\n          add r25.xy, r25.yxzw, c212"
            "\n          mul r25, r25.xyxy, c88"
            "\n          frc r25, r25"
            "\n          add r26, r25.wzxy, c89.x"
            "\n          dp4 r26.x, r25, r26"
            "\n          add r25, r25, r26.x"
            "\n          add r26, r25.xxyz, r25.yzzw"
            "\n          mul r26, r26, r25.zywx"
            "\n          frc r26.xy, r26"
            "\n          mov r25.xyz, c202"
            "\n          add r25.xyz, r25, -c199"
            "\n          mad r25.xyz, r25, r26.x, c199"
            "\n          mad r25.w, r26.y, c90.x, c90.y"
            "\n          mul r25.w, r25.w, c202.w"
            "\n          add r25.w, r25.w, c175.x"
            // the layout (the class's weights): shelves, curtains, a dividing wall, else plain
            "\n          add r19.xyz, r13.x, -c204"
            "\n          cmp r19.xyz, r19, c175.y, c175.x"
            "\n          add r19.yz, r19, -r19.xxyy"
            // where the ray meets the room (across, up, deep)
            "\n          mad r20.xy, r27, r30.w, r28.zwzw"
            "\n          mul r20.z, r27.z, r30.w"
            // the soft widths (cells) at the point the ray meets: the walls' and the layouts' (c218.xy: wide by day)
            // plus the pixel's footprint there (c218.z of it: on the glass, r14.x its square, x (1 + the ray's depth
            // over the eye's distance)), never below 1e-4; r26.w, r14.z their inverses
            "\n          add r22.x, r14.x, c208.z"
            "\n          rsq r22.x, r22.x"
            "\n          rcp r22.x, r22.x"
            "\n          add r26.xyz, r15, -c180"
            "\n          dp3 r26.x, r26, r26"
            "\n          rsq r26.x, r26.x"
            "\n          mad r26.x, r30.w, r26.x, c175.x"
            "\n          mul r26.x, r26.x, r22.x"
            "\n          mul r26.x, r26.x, c198.x"
            "\n          mad r26.y, r26.x, c218.z, c218.x"
            "\n          mad r26.z, r26.x, c218.z, c218.y"
            "\n          max r26.yz, r26, c206.x"
            "\n          rcp r26.w, r26.y"
            "\n          rcp r14.z, r26.z"
            // near the cell's edge on the glass, or its anchor's (by day, c219.zw; 1 at night) the room eases to its
            // mean, over c219.z cells plus c83.w of the pixel's footprint there (r26.x: wider seen at a slant, where the
            // cells crowd on the screen): r19.w
            "\n          add r22.xy, c175.x, -r28.zwzw"
            "\n          min r22.xy, r22, r28.zwzw"
            "\n          min r22.x, r22.x, r22.y"
            "\n          mul r24.x, r24.x, c198.x"
            "\n          min r22.x, r22.x, r24.x"
            "\n          mul r22.y, r26.x, c83.w"
            "\n          add r22.y, r22.y, c219.z"
            "\n          rcp r22.y, r22.y"
            "\n          mul r22.x, r22.x, r22.y"
            "\n          add_sat r19.w, r22.x, c219.w"
            // a dividing wall half way back (where the ray would go deeper: soft, so the side wall runs into it), a
            // doorway through it (its edge soft) to a second, dimmer room: the point and the way to it are the wall's
            // where it's in the way (r20.xyz, r24.w)
            "\n          mul r22.x, c89.y, r30.z"
            "\n          mad r22.yz, r27.xxyw, r22.x, r28.zzww"
            "\n          add r22.w, r20.z, -c89.y"
            "\n          mad_sat r22.w, r22.w, r14.z, c175.z"
            "\n          mul r22.w, r22.w, r19.z"
            "\n          mad r29.x, r13.w, c89.z, c89.w"
            "\n          add r29.x, r22.y, -r29.x"
            "\n          add r29.x, -r29_abs.x, c90.z"
            "\n          add r29.y, -r22.z, c90.w"
            "\n          min r29.x, r29.x, r29.y"
            "\n          mad_sat r29.x, r29.x, r14.z, c175.z"
            "\n          mul r29.x, r29.x, r22.w"
            "\n          add r29.y, r22.w, -r29.x"
            "\n          lrp r24.w, r29.y, r22.x, r30.w"
            "\n          mov r22.w, c89.y"
            "\n          lrp r26.xyz, r29.y, r22.yzww, r20"
            "\n          mov r20.xyz, r26"
            // the walls' shade, soft: how near the point is to each wall (across the nearer side, up the floor and
            // the ceiling) smoothstepped over the walls' width, each wall's shade (c209) toward 1 away from it, x each
            // other (the corners darker); the dividing wall a little darker; farther darker; beyond the doorway dimmer
            "\n          add r24.x, c175.x, -r20.x"
            "\n          min r24.x, r24.x, r20.x"
            "\n          mov r24.y, r20.y"
            "\n          add r24.z, c175.x, -r20.y"
            "\n          mul_sat r24.xyz, r24, r26.w"
            "\n          add r22.xyz, r24, r24"
            "\n          add r22.xyz, -r22, c211.w"
            "\n          mul r24.xyz, r24, r24"
            "\n          mul r24.xyz, r24, r22"
            "\n          mov r26.xyz, c209"
            "\n          lrp r22.xyz, r24, c175.x, r26"
            "\n          mul r22.x, r22.x, r22.y"
            "\n          mul r22.x, r22.x, r22.z"
            "\n          mul r22.y, r24.w, c207.z"
            "\n          mad r22.y, r24.w, c86.y, r22.y"
            "\n          add r22.y, r22.y, c175.x"
            "\n          rcp r22.y, r22.y"
            "\n          mul r22.x, r22.x, r22.y"
            "\n          mul r22.y, r29.y, c95.w"
            "\n          add r22.y, r22.y, -r29.y"
            "\n          add r22.y, r22.y, c175.x"
            "\n          mul r22.x, r22.x, r22.y"
            "\n          mul r22.z, r29.x, c98.w"
            "\n          add r22.z, c175.x, -r22.z"
            "\n          mul r24.z, r22.x, r22.z"
            // curtains half drawn: cloth just behind the glass at the sides, in folds, a gap between them (its edges
            // soft over the layouts' width, at least 1/25 of the cell)
            "\n          mul r26.x, c91.x, r30.z"
            "\n          mad r26.x, r27.x, r26.x, r28.z"
            "\n          mul r26.y, r13.w, c92.z"
            "\n          frc r26.y, r26.y"
            "\n          mad r26.y, r26.y, c92.y, c92.x"
            "\n          mad r26.z, r13.w, c91.y, c91.z"
            "\n          add r26.z, r26.x, -r26.z"
            "\n          add r26.z, r26_abs.z, -r26.y"
            "\n          min r26.w, r14.z, c91.w"
            "\n          mul_sat r26.z, r26.z, r26.w"
            "\n          mul r26.z, r26.z, r19.y"
            "\n          mul r26.x, r26.x, c92.w"
            "\n          frc r26.x, r26.x"
            "\n          add r26.x, r26.x, -c175.z"
            "\n          mad r26.x, r26_abs.x, c97.x, c97.y"
            // shelves on the back wall: planks, and things on them (a hash per place on a shelf); every edge soft over
            // the layouts' width (the planks' about their middles, so a shelf's row meets the next without a step; the
            // things' inward from their outlines, so a place meets the next without one)
            "\n          add r27.xy, r20, -c96"
            "\n          add r27.xy, -r27_abs, c96.zwzw"
            "\n          min r27.x, r27.x, r27.y"
            "\n          mad_sat r27.x, r27.x, r14.z, c175.z"
            "\n          mul r27.x, r27.x, r19.x"
            "\n          mul r27.yz, r20.xxyw, c93.xxyw"
            "\n          frc r28.yz, r27"
            "\n          add r27.yz, r27, -r28"
            "\n          mad r27.y, r27.z, c98.x, r27.y"
            "\n          mad r27.y, r13.w, c98.y, r27.y"
            "\n          mul r27.y, r27.y, c88.x"
            "\n          frc r27.y, r27.y"
            "\n          add r27.z, r27.y, c89.x"
            "\n          mul r27.y, r27.y, r27.z"
            "\n          mul r27.z, r27.y, r27.y"
            "\n          add r27.y, r27.z, r27.z"
            "\n          frc r27.y, r27.y"
            "\n          add r28.w, r28.z, -c93.z"
            "\n          mul r29.x, r27.y, c93.w"
            "\n          add r29.x, r29.x, -r28.w"
            "\n          add r29.y, r28.y, -c175.z"
            "\n          add r29.y, -r29_abs.y, c97.w"
            "\n          min r29.x, r29.x, r29.y"
            "\n          mul r28.x, r14.z, c210.w"
            "\n          add r28.w, r28.z, c100.x"
            "\n          frc r28.w, r28.w"
            "\n          add r28.w, r28.w, -c175.z"
            "\n          add r28.w, -r28_abs.w, c100.y"
            "\n          mad_sat r28.w, r28.w, r28.x, c175.z"
            "\n          mul_sat r28.x, r29.x, r28.x"
            "\n          mul r29.z, r27.y, c98.z"
            "\n          frc r29.z, r29.z"
            "\n          mad r29.z, r29.z, c99.x, c99.y"
            "\n          lrp r29.w, r28.x, r29.z, c99.z"
            "\n          lrp r27.w, r28.w, c99.w, r29.w"
            "\n          add r29.w, r27.w, -c175.x"
            "\n          mul r29.w, r29.w, r27.x"
            // by day the sun falling in: a patch on the floor by the window (across the middle, not deep, low: soft over
            // c83, c84.zw), x its cosine to the face, x c86.x (none at night): r27.y
            "\n          dp3_sat r27.y, r18, c195"
            "\n          add r27.z, r20.x, -c175.z"
            "\n          add r27.z, -r27_abs.z, c84.z"
            "\n          mul_sat r27.z, r27.z, c84.w"
            "\n          mul r27.y, r27.y, r27.z"
            "\n          add r27.z, -r20.z, c83.x"
            "\n          mul_sat r27.z, r27.z, c83.y"
            "\n          mul r27.y, r27.y, r27.z"
            "\n          mul r27.z, r20.y, c83.z"
            "\n          add_sat r27.z, -r27.z, c175.x"
            "\n          mul r27.y, r27.y, r27.z"
            "\n          mul r27.y, r27.y, c86.x"
            // the lamp: its light falls off from where it stands (across by the room's hash, up, toward the back)
            "\n          mad r22.x, r13.z, c94.y, c94.x"
            "\n          add r22.x, r20.x, -r22.x"
            "\n          add r22.yz, r20.xyzz, -c94.xzww"
            "\n          dp3 r22.x, r22, r22"
            "\n          add r22.x, r22.x, c95.x"
            "\n          rcp r22.x, r22.x"
            "\n          mad r20.x, r22.x, c95.y, c95.z"
            // the detail fades with distance (c219.xy over its square: nearer by day; x the layouts' strength), where a
            // pixel is too big for it (its footprint on the glass: none from 0.15 yd, no shimmer) and near the cell's
            // edge by day: the curtains (their cover too) and folds to their average; the things on the shelves at
            // c218.w of their contrast
            "\n          add r28.xyz, r15, -c180"
            "\n          dp3 r28.x, r28, r28"
            "\n          mad_sat r28.x, r28.x, c219.y, c219.x"
            "\n          mul r28.x, r28.x, c204.w"
            "\n          mad_sat r28.y, r14.x, c208.x, c208.y"
            "\n          mul r28.x, r28.x, r28.y"
            "\n          mul r28.x, r28.x, r19.w"
            "\n          mul r28.z, r28.x, c218.w"
            "\n          mad r29.w, r29.w, r28.z, c175.x"
            "\n          mul r28.y, r19.y, c100.z"
            "\n          mul r28.y, r28.y, r19.w"
            "\n          add r26.z, r26.z, -r28.y"
            "\n          mad r26.z, r26.z, r28.x, r28.y"
            "\n          add r26.x, r26.x, -c97.z"
            "\n          mad r26.x, r26.x, r28.x, c97.z"
            // the light: the day's (x the building's tint and brightness, x the client's colour's luminance: the room
            // seen through the glass, c198.w makes it about room::kDayRoom of the glass's brightness, darker than its
            // sunlit face; lighter low and darker high (c86.z), the sun's patch on its floor; a dark room keeps some; the
            // curtains in front of the glass too),
            // and at night the lamp's in lit rooms behind the client's lit windows, as bright as the window (x its
            // luminance); near the cell's edge by day the room's light and its dark rooms' ease to their means
            "\n          dp3 r22.x, r12, c176"
            "\n          mad r22.y, r13.z, c210.x, c210.y"
            "\n          mul r22.y, r22.y, r24.z"
            "\n          mul r22.y, r22.y, r29.w"
            "\n          add r27.w, -r20.y, c175.z"
            "\n          mul r27.w, r27.w, c86.z"
            "\n          mad r22.y, r22.y, r27.w, r22.y"
            "\n          add r22.y, r22.y, r27.y"
            "\n          lrp r22.y, r19.w, r22.y, c210.z"
            "\n          mul r22.z, r25.w, c198.w"
            "\n          mad r22.w, r20.w, c87.x, c87.y"
            "\n          lrp r22.w, r19.w, r22.w, c87.w"
            "\n          mul r22.z, r22.z, r22.w"
            "\n          mul r22.z, r22.z, r22.x"
            "\n          mul r30.xyz, r25, r22.z"
            "\n          mul r22.w, r22.x, r20.w"
            "\n          mad r22.w, r22.w, -r21.w, r22.w"
            "\n          mul r29.xyz, r25, c203"
            "\n          mul r28.w, r22.w, r20.x"
            "\n          mad r30.xyz, r29, r28.w, r30"
            "\n          mul r30.xyz, r30, r22.y"
            // the curtains: lit from the window by day, by the lamp behind them at night
            "\n          mul r29.xyz, r22.w, c203"
            "\n          mul r29.w, r22.z, c100.w"
            "\n          add r29.xyz, r29, r29.w"
            "\n          mul r29.xyz, r29, c205"
            "\n          mul r29.xyz, r29, r26.x"
            "\n          mul r29.xyz, r29, c87.z"
            "\n          lrp r28.xyz, r26.z, r29, r30"
            // then the room behind the glass
            "\n          mov r29.w, c193.z"
            "\n          lrp r30.xyz, r29.w, r28, r23"
            "\n          mov r23.xyz, r30"
            "\n        endif"
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
            // the glass by its mask; the rest of the window (1 - hidden) of the client's colour
            "\n      add r27.xyz, r27, -r12"
            "\n      mad r27.xyz, r12, r14.y, r27"
            "\n      mad r31.xyz, r27, r14.w, r31"
            "\n      mad r31.xyz, r12, -r14.y, r31"
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
        for (int k = 0; k < kFloorPanes; ++k)
        {
            auto c = [k](int r) { return "c" + std::to_string(kPaneConstant + kPaneRegisters * k + r); };
            s += "\n      mov r20.w, " + c(3) + ".w"
                 "\n      if_gt r20.w, c175.y"
                 "\n        dp4 r21.x, r15, " + c(0) +
                 "\n        dp4 r21.y, r15, " + c(1) +
                 "\n        dp4 r21.z, r15, " + c(2) +
                 "\n        add r22.xy, c175.x, -r21_abs"
                 "\n        mul_sat r22.xy, r22, " + c(4) + ".w"
                 "\n        mul r22.x, r22.x, r22.y"
                 "\n        add r22.z, c175.x, -r21.z"
                 "\n        mul_sat r22.z, r22.z, " + c(5) + ".w"
                 "\n        cmp r22.z, r21.z, r22.z, c175.y"
                 "\n        mul r22.x, r22.x, r22.z"
                 "\n        add r23.xyz, r15, -" + c(4) +
                 "\n        add r24.xyz, -r15, " + c(5) +
                 "\n        min r23.xyz, r23, r24"
                 "\n        min r23.x, r23.x, r23.y"
                 "\n        min r23.x, r23.x, r23.z"
                 "\n        cmp r22.x, r23.x, r22.x, c175.y"
                 "\n        mad r19.xyz, " + c(3) + ", r22.x, r19"
                 "\n      endif";
        }
        s += "\n      mul r19.xyz, r19, r18.w"
             "\n      mul r19.xyz, r19, r13"
             "\n      mad r31.xyz, r19, c201.x, r31"
             "\n    endif";
        return s;
    }
}
