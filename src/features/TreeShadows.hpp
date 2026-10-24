// Tree shadows that follow the wind (2026-10-24; the user: TreeWind's trees move their shadows "only with a low
// framerate and only when my character moves. The rest of the shadows in the game move much more smoothly").
//
// At shadow quality 3-4 the client keeps the shadows of everything static -- nearly every tree is one rigid bone -- in
// three cached maps round the player and draws one again only after the player has walked 2 (40 yd map), 4 (160 yd)
// or 32 yards (640 yd), a ninth of it a frame; units and animated doodads go in a small map drawn every frame. So the
// sway wxl-treewind gives a tree's bones reached its shadow only in steps, as the player walked. Here, right after the
// client draws its maps each frame, the near map's shown texture is drawn again, whole, through the client's own
// callbacks, where a tree or bush casts into it: its shadows follow the sway. Quality 5 does that for every map
// itself, and costs far more; 0-2 have no tree shadows. See env/TreeShadowModel.hpp.
#pragma once

#include "wxl/PluginApi.h"

#include "../env/TreeShadowModel.hpp"

#include <string>
#include <vector>

namespace wxl_livingazeroth::treeshadows
{
    treeshadow::Settings& Config();

    /// From WXL_Load: the guards against the client's code, the hook on CShadowCache::RenderBands, the panel.
    void Install(const WXL_Api* api);

    /// Once a frame (main thread): the panel's one-second counts.
    void Tick();

    /// For the 30-second status log: what happened since the last call.
    const char* LogLine();

    /// The panel's status, as lines (the snapshot too).
    void StatusLines(std::vector<std::string>& out);
}
