// Sun shafts (god rays): light shafts through trees, fog and the gaps of the cloud deck. The rules: env/ShaftModel;
// the shaders: SunShaftShaders.hpp.
//
// A post pass at the weather render's entry, right after the ground fog (so name text, the UI, rain and snow, drawn
// later, never take part): a source at a quarter of the resolution -- the sunlit air each pixel shows toward the sun,
// from the scene depth (the sky full, a near trunk nothing), x what the cloud deck lets through there -- blurred
// radially toward the place on the screen of the sun the player sees in the sky (or the moon; climate::SunNow().sky,
// .moon: env/SkyPath), a little brighter where branches break the line into gaps, and added over the frame in the
// light's colour. Under the cloud deck (GroundFog, AreaClimate CloudBase) the light scattered in along the view ray is
// added too, each step lit by the deck's own density toward the light (climate::SunNow().dir, as the scene is
// shaded): shafts below its gaps, with the sun off the screen as well.
//
// How strong: by what's in the air (the place's fogginess and the ground fog around the player), the time of day (the
// low sun most), the weather (none in heavy rain or a storm) and indoors (none); fading as the sun leaves the screen
// or sets.
#pragma once

#include "wxl/PluginApi.h"

namespace wxl_livingazeroth::sunshafts
{
    struct Settings
    {
        int   enabled = 1;
        float strength = 1.0f;      // x the shafts' brightness
        int   quality = 1;          // 0 quarter resolution, one blur pass; 1 quarter, smoothed (a second short pass); 2 half resolution, smoothed
        int   samples = 32;         // taps per pixel along the line toward the sun (8 .. 64)
        float length = 0.85f;       // how far toward the sun the taps reach (share of the way)
        int   throughDeck = 1;      // the cloud deck: its gaps let the shafts through, and shafts under it from its own density
        float moon = 0.0f;          // faint shafts from the night light: x this (0 = none)
        float testAir = -1.0f;      // the panel's test (not saved): this much in the air, 0 .. 1, instead of the fog's; < 0 off
    };
    Settings& Config();

    /// The hook (from WXL_Load).
    void Install(const WXL_Api* api);
    /// Before IDirect3DDevice9::Reset: releases its targets and shaders.
    void OnDeviceLost();
    /// The fog panel's section.
    void DrawPanelSection(const WXL_Api* api);
}
