// Coloured shafts through window glass: in a room, the air the sun crosses on its way in through the windows glows
// in each window's colour (the Cathedral of Light's stained glass, a house's leaded panes). The shaders:
// GlassShaftShaders.hpp; the panes: features/Glass (GlassPanes.cdbc; the sunlit ones whose room holds the camera).
//
// A post pass at the weather render's entry, right after the sun shafts (which are off indoors): a short march along
// each view ray at a quarter of the resolution against up to kMaxPanes panes, then added over the frame. Off where
// there's no glass (no pane's room holds the camera), at night, in the rain and in storms (the sun through glass); in
// Ulduar and the Halls of Stone and of Lightning the titan glass's light from behind each pane, at any hour (each pane
// along its own light: glass::LightOfPane).
#pragma once

#include "wxl/PluginApi.h"

namespace wxl_livingazeroth::glassshafts
{
    /// The hook (from WXL_Load).
    void Install(const WXL_Api* api);
    /// Before IDirect3DDevice9::Reset: releases its targets and shaders.
    void OnDeviceLost();
    /// One line for the glass panel: drawing (how many panes, the cost), or why not.
    const char* StatusLine();
}
