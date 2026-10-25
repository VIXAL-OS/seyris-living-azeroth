// The window glow: a soft bloom around the lit glass of inhabited buildings at night (features/Windows). The cloud
// deck's glow (GroundFog DrawGlow) blooms what's bright in the whole frame; this one only takes its light from the
// windows: each lit window's rectangle (WindowLights.cdbc) is drawn into a half-size source, where only the pixels
// that are the glass itself count -- at the window's own depth (a character or a tree in front of it isn't), bright
// and coloured (grey-white snow or a moonlit wall isn't). That source is copied down to 1/4 and 1/8 and added over
// the frame, blurred, as the deck's glow is. Each window's glow x the share of its glass whose room is lit
// (windows::LitShare): a window whose rooms have gone dark doesn't glow.
//
// Drawn at the weather render's entry (after the opaque world, the water and the ground fog; before rain, see-through
// models and effects), so name text and the UI, drawn after the world, never glow. With the client's own Full Screen
// Glow (ffxGlow) on, half as strong by default (or off; windows::Settings::withClientGlow).
#pragma once

#include "../debug/MemoryUse.hpp"
#include "wxl/PluginApi.h"

namespace wxl_livingazeroth::windowglow
{
    /// The hook (from WXL_Load).
    void Install(const WXL_Api* api);
    /// Before IDirect3DDevice9::Reset: releases its targets and shaders.
    void OnDeviceLost();

    /// Its Direct3D resources, for the memory readout (debug/Memory).
    void GpuMemory(memuse::Gpu& out);
}
