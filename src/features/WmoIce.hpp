// Clear ice on WMO ice (ice phase 3): the Nexus's and Icecrown's ice walls and floors, the ice caves,
// Azjol-Nerub's ice and ice stalactites, and the ice shards, cliffs and bridges built into buildings.
//
// The WMO pixel shaders (the MapObj* sets) are effects render/M2Effects records like the M2 ones, so
// features/ModelIce's pixel patch already carries the ice block behind b7 in their dynamic-shadow
// variants. This switches it on per batch: the batch loops (Wmo.IntRender / Wmo.ExtRender) name the
// group, hence the root and its materials; each batch passes Wmo.CullBatch right before its material,
// effect and draw, and the block goes on when that batch's material's first texture is an ice texture.
#pragma once

#include "ModelIce.hpp"

#include "wxl/PluginApi.h"

namespace wxl_livingazeroth::wmoice
{
    /// The batch-loop and batch-cull hooks. From WXL_Load.
    void Install(const WXL_Api* api);

    /// Whether a texture path (any case or slashes) is one of the WMO ice textures.
    bool IsIceTexture(const char* path);
    /// Whether a texture is a sand floor (its file name says sand, not stone, rock, wall, brick, trim, pillar, statue
    /// or wet): its batches get the footprint rims (ModelIce's b8 block).
    bool IsSandTexture(const char* path);
    /// Its look: glacier for Dragonblight's ice cliffs and icebergs, floe for the shoreline floe texture,
    /// light for the rest of the list, none when it isn't ice.
    modelice::Style StyleOfTexture(const char* path);

    void SetEnabled(bool enabled);
    bool Enabled();
    const char* StatusLine();
}
