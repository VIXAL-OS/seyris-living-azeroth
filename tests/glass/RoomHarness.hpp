// The rooms behind the glass on a D3D9 device of the tests' own (WARP): the window table, the furniture atlas and the
// rooms' textures as the runtime makes them (features/Glass), bound to s8..s13, and the room's registers (c15..c25) for a
// group of windows placed straight in the world (the instance's frame the identity about the world's origin, so its
// WMO space is the world's). Shared by tests/glass and the contact sheets.
#pragma once

#include "env/GlassRooms.hpp"
#include "features/Glass.hpp"
#include "features/GlassShader.hpp"
#include "render/BlpColour.hpp"
#include "render/BlpTexture.hpp"

#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace roomharness
{
    namespace gl = wxl_livingazeroth::glass;

    /// A texture of A8R8G8B8 texels with its mips (box filtered).
    inline IDirect3DTexture9* Texture(IDirect3DDevice9* dev, const std::vector<uint32_t>& texels, int width, int height)
    {
        IDirect3DTexture9* tex = nullptr;
        if (FAILED(dev->CreateTexture(width, height, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) || !tex) return nullptr;
        std::vector<uint32_t> level = texels;
        int w = width, h = height;
        for (DWORD m = 0; m < tex->GetLevelCount(); ++m)
        {
            if (m)
            {
                const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
                std::vector<uint32_t> next(static_cast<size_t>(nw) * nh);
                for (int y = 0; y < nh; ++y)
                    for (int x = 0; x < nw; ++x)
                    {
                        uint32_t sum[4] = {};
                        for (int dy = 0; dy < 2; ++dy)
                            for (int dx = 0; dx < 2; ++dx)
                            {
                                const uint32_t t = level[static_cast<size_t>(std::min(h - 1, y * 2 + dy)) * w + std::min(w - 1, x * 2 + dx)];
                                for (int c = 0; c < 4; ++c) sum[c] += (t >> (8 * c)) & 0xFF;
                            }
                        uint32_t v = 0;
                        for (int c = 0; c < 4; ++c) v |= ((sum[c] + 2) / 4) << (8 * c);
                        next[static_cast<size_t>(y) * nw + x] = v;
                    }
                level.swap(next);
                w = nw; h = nh;
            }
            D3DLOCKED_RECT lr{};
            if (FAILED(tex->LockRect(m, &lr, nullptr, 0))) { tex->Release(); return nullptr; }
            for (int y = 0; y < h; ++y) std::memcpy(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch, &level[static_cast<size_t>(y) * w], w * 4);
            tex->UnlockRect(m);
        }
        return tex;
    }

    /// Synthetic interiors (no client files): planks for the floor and the wood, plaster in a timber frame for the walls,
    /// beams for the ceiling, 256 x 256, mean luminance about 0.5.
    inline std::vector<uint32_t> Synthetic(int which)
    {
        std::vector<uint32_t> t(256 * 256);
        auto hash = [](int x, int y) { uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0f; };
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x)
            {
                float r = 0.5f, g = 0.5f, b = 0.5f;
                if (which == 0 || which == 3)   // planks: 8 a repeat, grain, dark seams
                {
                    const int plank = y / 32;
                    const float grain = 0.85f + 0.15f * std::sin(x * 0.11f + plank * 1.7f + 3.0f * hash(plank, 0));
                    const float seam = (y % 32 < 2 || (x + plank * 73) % 256 < 2) ? 0.45f : 1.0f;
                    const float k = (0.75f + 0.25f * hash(plank, 1)) * grain * seam;
                    r = 0.62f * k; g = 0.45f * k; b = 0.3f * k;
                }
                else if (which == 1)            // plaster in a timber frame
                {
                    const bool beam = x < 22 || x > 233 || y < 18 || y > 237 || std::abs((x - y)) < 9;
                    const float n = 0.9f + 0.1f * hash(x / 4, y / 4);
                    if (beam) { r = 0.36f * n; g = 0.25f * n; b = 0.16f * n; }
                    else { r = 0.86f * n; g = 0.8f * n; b = 0.7f * n; }
                }
                else                            // beams across, darker
                {
                    const bool beam = (y % 64) < 20;
                    const float n = 0.85f + 0.15f * hash(x / 8, y / 3);
                    r = (beam ? 0.3f : 0.5f) * n; g = (beam ? 0.21f : 0.4f) * n; b = (beam ? 0.14f : 0.3f) * n;
                }
                auto byte = [](float f) { return static_cast<uint32_t>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
                t[static_cast<size_t>(y) * 256 + x] = 0xFF000000u | (byte(r) << 16) | (byte(g) << 8) | byte(b);
            }
        return t;
    }

    inline float MeanLum(const std::vector<uint32_t>& t)
    {
        double s = 0;
        for (uint32_t v : t) s += 0.2126 * ((v >> 16) & 0xFF) / 255.0 + 0.7152 * ((v >> 8) & 0xFF) / 255.0 + 0.0722 * (v & 0xFF) / 255.0;
        return static_cast<float>(s / std::max<size_t>(1, t.size()));
    }

    struct Rooms
    {
        gl::WindowTable table;
        IDirect3DTexture9* tableTex = nullptr;
        IDirect3DTexture9* atlas = nullptr;
        IDirect3DTexture9* white = nullptr;
        IDirect3DTexture9* faces[4] = {};
        float gains[4] = { 1, 1, 1, 1 }, scales[4] = { 2.0f, 2.6f, 2.4f, 1.6f };
        std::string source = "none";

        ~Rooms() { Clear(); }
        void Clear()
        {
            for (IDirect3DTexture9** t : { &tableTex, &atlas, &white, &faces[0], &faces[1], &faces[2], &faces[3] })
                if (*t) { (*t)->Release(); *t = nullptr; }
        }

        /// The table of these windows (their WMO space the world's) on the device.
        bool Table(IDirect3DDevice9* dev, std::vector<gl::Window> windows)
        {
            if (tableTex) { tableTex->Release(); tableTex = nullptr; }
            table.Build(std::move(windows));
            if (FAILED(dev->CreateTexture(gl::rooms::kWidth, table.Height(), 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED, &tableTex, nullptr)) || !tableTex)
                return false;
            D3DLOCKED_RECT lr{};
            if (FAILED(tableTex->LockRect(0, &lr, nullptr, 0))) return false;
            for (int y = 0; y < table.Height(); ++y)
                std::memcpy(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch, &table.Texels()[static_cast<size_t>(y) * gl::rooms::kWidth * 4], gl::rooms::kWidth * 16);
            tableTex->UnlockRect(0);
            return true;
        }

        /// The atlas and the faces' textures: the client's (`folder` with the extracted BLPs of texture set `set`, as
        /// waterscan --extract-list names them: '\' as '#'), else synthetic ones.
        bool Textures(IDirect3DDevice9* dev, const char* folder = nullptr, int set = 0)
        {
            std::vector<uint32_t> a;
            gl::FurnitureAtlas(a);
            atlas = Texture(dev, a, gl::rooms::kAtlasWidth, gl::rooms::kAtlasHeight);
            white = Texture(dev, std::vector<uint32_t>(16, 0xFFFFFFFFu), 4, 4);
            const gl::TextureSet& ts = gl::TextureSetAt(set);
            const char* paths[4] = { ts.floor, ts.wall, ts.ceiling, ts.wood };
            bool real = folder != nullptr;
            for (int k = 0; k < 4 && real; ++k)
            {
                std::string flat = paths[k];
                std::replace(flat.begin(), flat.end(), '\\', '#');
                std::ifstream f(std::filesystem::path(folder) / flat, std::ios::binary);
                const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                std::string error;
                faces[k] = bytes.empty() ? nullptr : wxl_livingazeroth::blp::LoadFromMemory(dev, bytes.data(), bytes.size(), error);
                float rgb[3];
                if (!faces[k] || !wxl_livingazeroth::blp::AverageColour(bytes.data(), bytes.size(), rgb)) { real = false; break; }
                gains[k] = 1.0f / std::max(0.05f, 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2]);
                scales[k] = ts.scale[k];
            }
            if (real) { source = ts.name; return atlas && white; }
            for (IDirect3DTexture9*& t : faces) if (t) { t->Release(); t = nullptr; }
            for (int k = 0; k < 4; ++k)
            {
                const std::vector<uint32_t> s = Synthetic(k);
                faces[k] = Texture(dev, s, 256, 256);
                gains[k] = 1.0f / MeanLum(s);
                scales[k] = ts.scale[k];
            }
            source = "synthetic";
            return atlas && white && faces[0] && faces[1] && faces[2] && faces[3];
        }

        /// s8..s13 and their states; `plain`: white faces (RoomTextures off).
        void Bind(IDirect3DDevice9* dev, bool plain = false) const
        {
            for (int k = 0; k < 4; ++k) dev->SetTexture(gl::kFirstSampler + k, plain || !faces[k] ? white : faces[k]);
            dev->SetTexture(gl::kFirstSampler + 4, atlas);
            dev->SetTexture(gl::kTableSampler, tableTex);
            for (int k = 0; k < 6; ++k)
            {
                const DWORD s = static_cast<DWORD>(gl::kFirstSampler + k);
                const bool tbl = s == static_cast<DWORD>(gl::kTableSampler);
                dev->SetSamplerState(s, D3DSAMP_MINFILTER, tbl ? D3DTEXF_POINT : (k < 4 ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR));
                dev->SetSamplerState(s, D3DSAMP_MAGFILTER, tbl ? D3DTEXF_POINT : D3DTEXF_LINEAR);
                dev->SetSamplerState(s, D3DSAMP_MIPFILTER, tbl ? D3DTEXF_NONE : D3DTEXF_LINEAR);
                dev->SetSamplerState(s, D3DSAMP_MAXANISOTROPY, 8);
                const DWORD address = tbl || k == 4 ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP;
                dev->SetSamplerState(s, D3DSAMP_ADDRESSU, address);
                dev->SetSamplerState(s, D3DSAMP_ADDRESSV, address);
                dev->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
            }
        }

        /// The room's registers (c15..c27) for windows of WMO `path`, group `group` placed straight in the world, the
        /// camera `cam` (world), the building's class, the settings; `plain`: gains of 1 (white faces); the batch's glass
        /// kind (stained glass's grid, the kind's look: c26, c27).
        void Registers(const float cam[3], uint32_t path, uint32_t group, uint32_t cls, const gl::Settings& s, float out[4 * gl::kRoomRegisters],
                       bool plain = false, uint32_t kind = gl::kClear) const
        {
            const float axes[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, origin[3] = { 0, 0, 0 };
            const float ones[4] = { 1, 1, 1, 1 };
            gl::RoomRegisters(axes, origin, cam, table.Find(path, group, kind == gl::kStained), table.Height(), cls, scales, plain ? ones : gains, s, out, kind);
        }
    };

    /// A window facing `n` (unit) about `center` (world = its WMO space), half sizes hw, hh, its key.
    inline gl::Window MakeWindow(uint32_t path, uint32_t group, uint32_t ordinal, const float center[3], const float n[3], float hw, float hh,
                                 uint32_t kind = gl::kClear)
    {
        gl::Window w;
        w.path = path; w.group = group; w.ordinal = ordinal; w.kind = kind;
        for (int k = 0; k < 3; ++k) { w.center[k] = center[k]; w.normal[k] = n[k]; }
        w.halfWidth = hw; w.halfHeight = hh;
        return w;
    }
}
