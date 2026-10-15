// Depth for the player's see-through parts: a mount's wing membranes are alpha-blended M2 batches, which the client
// draws without writing depth, so whatever reads the depth buffer afterwards (the water's screen-space reflections, the
// ground fog, the cloud deck) sees the scenery behind a wing instead of the wing. Right after each such batch of the
// player's own model family is drawn, it is drawn again into the depth buffer only. See PlayerDepth.cpp.
#pragma once

#include "wxl/PluginApi.h"

namespace wxl_livingazeroth::playerdepth
{
    struct Settings
    {
        int enabled = 1;    // the player's alpha-blended batches write depth
        int alphaRef = 128; // the alpha test that decides which of their pixels count as solid (0..255)
    };
    Settings& Config();

    /// From WXL_Load: the M2 batch draw and device draw hooks, and the panel.
    void Install(const WXL_Api* api);

    /// Once per frame (main thread, before the world draws): the root of the player's model family.
    void Update(bool inWorld);

    /// One line for the status log.
    const char* StatusLine();
}
