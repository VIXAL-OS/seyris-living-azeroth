// Loads a BLP2 texture from the client's archives (or loose files, as the client sees them) into a
// texture of our own: DXT1/3/5, uncompressed BGRA and palettized BLPs, with their mip chains.
// Independent of the client's texture cache, so binding it never disturbs the client's own
// texture state.
#pragma once

#include <d3d9.h>

#include <string>

namespace wxl_livingazeroth::blp
{
    /// A managed-pool texture (survives device resets), or null with `error` set.
    IDirect3DTexture9* Load(IDirect3DDevice9* dev, const char* path, std::string& error);

    /// The file's average colour (RGB 0..1, as stored), no device needed (BlpColour.hpp); false if it can't be read.
    bool AverageColour(const char* path, float rgb[3]);
}
