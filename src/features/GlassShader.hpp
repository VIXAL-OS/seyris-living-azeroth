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
//  the client's own lit windows at night); seen from outside, darker glass with the sky in it (Schlick's Fresnel; the
//  sky deeper toward the zenith along the reflected ray),
//  the sun's glint, and behind clear glass a room (interior mapping in the pane's own frame: cells c198.xy wide and
//  high, 1 / c198.z deep, lit by c198.w, tinted c199, each cell its own brightness); the two mixed by c193.x.
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
//  its daylight); c199 (its tint rgb; the inside's brightness); c200 the sun's colour; c201 (the floor light's gain);
//  c206..c211 defined; the floor light's panes c101..c172 (six registers each; the rims' range: a batch has one or
//  the other, features/Glass re-uploads when the rims took it).
#pragma once

#include <string>

namespace wxl_livingazeroth::glass
{
    constexpr int kSwitch = 9;           // PS b9: the glass
    constexpr int kFloorSwitch = 10;     // PS b10: the floor light
    constexpr int kViewConstant = 180;   // c180..c183 (FillModelConstants' first four)
    constexpr int kFirstConstant = 192;  // c192..c201
    constexpr int kConstantCount = 10;
    constexpr int kPaneConstant = 101;   // c101..c172
    constexpr int kPaneRegisters = 6;
    constexpr int kFloorPanes = 12;

    inline std::string ShaderDefs()
    {
        return "    def c206, 0.0001, 0.05, 0, 300\n"
               "    def c207, -1, 1, 0.12, 0\n"
               "    def c208, 0.1031, 0.1103, 19.19, 0\n"
               "    def c209, 0.7, 0.55, 0.4, 1\n"
               "    def c210, 0.9, 0.35, 0, 0\n"
               "    def c211, 0.7, 0.85, 1.25, 0\n";
    }

    inline std::string GlassBlock(const std::string& vP, const std::string& vN, const std::string& vT)
    {
        return
            "\n    if b9"
            // the texel, its glass mask
            "\n      texld r13, " + vT + ", s0"
            "\n      dp3 r14.w, r13, c192"
            "\n      add_sat r14.w, r14.w, c192.w"
            "\n      mul r14.w, r14.w, c193.y"
            // P (world), V (world, toward the eye), N (world, facing the eye), cos = N.V
            "\n      dp3 r15.x, " + vP + ", c181"
            "\n      dp3 r15.y, " + vP + ", c182"
            "\n      dp3 r15.z, " + vP + ", c183"
            "\n      add r15.xyz, r15, c180"
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
            // the glass's thickness: grad u, grad v (x det) from the screen gradients; the texel h yd deeper
            "\n      dsx r19.xyz, r15"
            "\n      dsy r20.xyz, r15"
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
            "\n      add r26.w, r26_abs.z, -c206.x"
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
            // behind clear glass, a room: interior mapping in the pane's frame (along, up, deep), in cells
            "\n      mov r19.x, c193.z"
            "\n      if_gt r19.x, c175.y"
            "\n        mul r24.xy, r18.yxxx, c207"
            "\n        mov r24.z, c175.y"
            "\n        dp3 r24.w, r24, r24"
            "\n        add r25.x, r24.w, -c206.x"
            "\n        cmp r24.xyz, r25.x, r24, c175.xyyy"
            "\n        nrm r25.xyz, r24"
            "\n        crs r26.xyz, r18, r25"
            "\n        dp3 r27.x, -r16, r25"
            "\n        dp3 r27.y, -r16, r26"
            "\n        max r27.z, r17.w, c206.x"
            "\n        mul r27.xyz, r27, c198"
            "\n        dp3 r28.x, r15, r25"
            "\n        dp3 r28.y, r15, r26"
            "\n        mul r28.xy, r28, c198"
            "\n        frc r28.zw, r28.xxxy"
            "\n        add r29.xy, r28, -r28.zwww"
            "\n        cmp r29.zw, r27.xxxy, c175.x, c175.y"
            "\n        add r29.zw, r29, -r28"
            "\n        rcp r30.x, r27.x"
            "\n        rcp r30.y, r27.y"
            "\n        rcp r30.z, r27.z"
            "\n        mul r30.xy, r29.zwww, r30"
            "\n        min r30.w, r30.x, r30.y"
            "\n        min r30.w, r30.w, r30.z"
            // the face it meets: the back wall, a side wall, the floor or the ceiling; farther is darker
            "\n        cmp r24.z, r27.y, c209.z, c209.y"
            "\n        add r24.x, r30.z, -r30.w"
            "\n        add r24.y, r30.x, -r30.w"
            "\n        cmp r24.z, -r24.y, c209.x, r24.z"
            "\n        cmp r24.z, -r24.x, c209.w, r24.z"
            "\n        mul r24.w, r30.w, c207.z"
            "\n        add r24.w, r24.w, c175.x"
            "\n        rcp r24.w, r24.w"
            "\n        mul r24.z, r24.z, r24.w"
            // each cell its own light (a hash of the cell)
            "\n        mul r24.xy, r29, c208"
            "\n        frc r24.xy, r24"
            "\n        add r25.xy, r24.yxxx, c208.z"
            "\n        dp2add r25.w, r24, r25, c175.y"
            "\n        add r24.xy, r24, r25.w"
            "\n        add r25.x, r24.x, r24.y"
            "\n        mul r25.x, r25.x, r24.x"
            "\n        frc r25.x, r25.x"
            "\n        mad r25.x, r25.x, c210.x, c210.y"
            "\n        mul r24.z, r24.z, r25.x"
            "\n        mul r24.z, r24.z, c198.w"
            "\n        mul r28.xyz, r24.z, c199"
            "\n        mov r29.w, c193.z"
            "\n        lrp r30.xyz, r29.w, r28, r23"
            "\n        mov r23.xyz, r30"
            "\n      endif"
            // the sky in it (deeper toward the zenith: the reflected ray's height, 2 cos N.z - V.z) and the sun's
            // glint; then inside or outside, by c193.x
            "\n      add r24.w, r17.w, r17.w"
            "\n      mul r24.w, r24.w, r18.z"
            "\n      add_sat r24.w, r24.w, -r16.z"
            "\n      mov r24.xyz, c211"
            "\n      add r24.xyz, r24, -c175.x"
            "\n      mad r24.xyz, r24, r24.w, c175.x"
            "\n      mul r24.xyz, r24, c196"
            "\n      lrp r25.xyz, r23.w, r24, r23"
            "\n      add r26.xyz, r16, c195"
            "\n      nrm r27.xyz, r26"
            "\n      dp3_sat r26.w, r18, r27"
            "\n      pow r26.w, r26.w, c206.w"
            "\n      mul r26.w, r26.w, c197.y"
            "\n      mad r25.xyz, c200, r26.w, r25"
            "\n      mov r26.x, c193.x"
            "\n      lrp r27.xyz, r26.x, r21, r25"
            "\n      add r27.xyz, r27, -r12"
            "\n      mad r31.xyz, r27, r14.w, r31"
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
