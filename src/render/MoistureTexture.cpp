#include "MoistureTexture.hpp"

#include "../env/Fields.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace wxl_livingazeroth::moisturetex
{
    namespace
    {
        IDirect3DDevice9*  g_device = nullptr;
        IDirect3DTexture9* g_texture = nullptr;
        IDirect3DTexture9* g_farPixel = nullptr;   // A8R8G8B8, alpha = excess
        IDirect3DTexture9* g_farVertex = nullptr;  // R32F
        int                g_farSize = 0;
        uint32_t           g_farVersion = 0;
        bool               g_farUploaded = false;

        // The far textures, current (created on first use, re-uploaded when the data changes).
        bool EnsureFar(IDirect3DDevice9* device)
        {
            const fields::WetGrid& wet = fields::Wet();
            if (!device || !wet.farBytes || wet.farSize <= 0) return false;
            if (device != g_device && g_device) Release();
            g_device = device;
            if (wet.farSize != g_farSize)
            {
                if (g_farPixel) { g_farPixel->Release(); g_farPixel = nullptr; }
                if (g_farVertex) { g_farVertex->Release(); g_farVertex = nullptr; }
                g_farUploaded = false;
            }
            if (!g_farPixel && FAILED(device->CreateTexture(wet.farSize, wet.farSize, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_farPixel, nullptr))) { g_farPixel = nullptr; return false; }
            if (!g_farVertex && FAILED(device->CreateTexture(wet.farSize, wet.farSize, 1, 0, D3DFMT_R32F, D3DPOOL_MANAGED, &g_farVertex, nullptr))) { g_farVertex = nullptr; return false; }
            g_farSize = wet.farSize;
            if (!g_farUploaded || wet.version != g_farVersion)
            {
                D3DLOCKED_RECT lr{};
                if (SUCCEEDED(g_farPixel->LockRect(0, &lr, nullptr, 0)))
                {
                    for (int row = 0; row < wet.farSize; ++row)
                    {
                        uint32_t* out = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lr.pBits) + row * lr.Pitch);
                        for (int col = 0; col < wet.farSize; ++col) out[col] = static_cast<uint32_t>(wet.farBytes[row * wet.farSize + col]) << 24;
                    }
                    g_farPixel->UnlockRect(0);
                }
                if (SUCCEEDED(g_farVertex->LockRect(0, &lr, nullptr, 0)))
                {
                    for (int row = 0; row < wet.farSize; ++row)
                        std::memcpy(static_cast<uint8_t*>(lr.pBits) + row * lr.Pitch, wet.farValues + row * wet.farSize, wet.farSize * sizeof(float));
                    g_farVertex->UnlockRect(0);
                }
                g_farVersion = wet.version;
                g_farUploaded = true;
            }
            return true;
        }
        int                g_size = 0;
        uint32_t           g_version = 0;
        bool               g_uploaded = false;

        // Wet surfaces' texture and the terrain wetness's debug copy: the near grid's layout.
        struct GridTexture
        {
            IDirect3DTexture9* texture = nullptr;
            int                size = 0;
            uint32_t           version = 0;
            int                mode = -1;
            bool               uploaded = false;
            void Drop() { if (texture) texture->Release(); texture = nullptr; size = 0; uploaded = false; mode = -1; }
        };
        GridTexture g_surfaceTex, g_debugTex;
        unsigned    g_surfaceUploads = 0;

        void Linear(IDirect3DDevice9* device, DWORD sampler)
        {
            device->SetSamplerState(sampler, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(sampler, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(sampler, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            device->SetSamplerState(sampler, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
            device->SetSamplerState(sampler, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
            device->SetSamplerState(sampler, D3DSAMP_SRGBTEXTURE, FALSE);
        }

        // Creates (or recreates at a new size) one of the grid's textures; false if it can't.
        bool Ensure(IDirect3DDevice9* device, GridTexture& t, int size)
        {
            if (device != g_device && g_device) Release();
            g_device = device;
            if (t.texture && t.size != size) t.Drop();
            if (!t.texture)
            {
                if (FAILED(device->CreateTexture(size, size, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t.texture, nullptr))) { t.texture = nullptr; return false; }
                t.size = size;
                t.uploaded = false;
            }
            return true;
        }

        // Fills the texture with fn(cell slot) per texel (A8R8G8B8).
        template <class Fn> bool Fill(GridTexture& t, Fn fn)
        {
            D3DLOCKED_RECT lr{};
            if (FAILED(t.texture->LockRect(0, &lr, nullptr, 0))) return false;
            for (int row = 0; row < t.size; ++row)
            {
                uint32_t* out = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lr.pBits) + row * lr.Pitch);
                for (int col = 0; col < t.size; ++col) out[col] = fn(row * t.size + col, row, col);
            }
            t.texture->UnlockRect(0);
            t.uploaded = true;
            return true;
        }
    }

    void Release()
    {
        if (g_texture) g_texture->Release();
        if (g_farPixel) g_farPixel->Release();
        if (g_farVertex) g_farVertex->Release();
        g_texture = g_farPixel = g_farVertex = nullptr;
        g_surfaceTex.Drop();
        g_debugTex.Drop();
        g_device = nullptr;
        g_size = g_farSize = 0;
        g_uploaded = g_farUploaded = false;
    }

    bool BindSurface(IDirect3DDevice9* device, DWORD sampler)
    {
        const fields::WetGrid& wet = fields::Wet();
        if (!device || !wet.surface || wet.size <= 0 || !Ensure(device, g_surfaceTex, wet.size)) return false;
        if (!g_surfaceTex.uploaded || g_surfaceTex.version != wet.version)
        {
            const bool ok = Fill(g_surfaceTex, [&](int k, int, int)
            {
                return (static_cast<uint32_t>(wet.surface[k * 2]) << 16) | (static_cast<uint32_t>(wet.surface[k * 2 + 1]) << 8);
            });
            if (!ok) return false;
            g_surfaceTex.version = wet.version;
            ++g_surfaceUploads;
        }
        device->SetTexture(sampler, g_surfaceTex.texture);
        Linear(device, sampler);
        return true;
    }

    unsigned SurfaceUploads() { return g_surfaceUploads; }

    bool BindDebug(IDirect3DDevice9* device, DWORD sampler, int mode)
    {
        const fields::WetGrid& wet = fields::Wet();
        if (!device || !wet.excess || wet.size <= 0 || !Ensure(device, g_debugTex, wet.size)) return false;
        if (!g_debugTex.uploaded || g_debugTex.version != wet.version || g_debugTex.mode != mode)
        {
            // The world cell a texel holds (the toroidal layout: column mod(i, size)), for the stripes.
            const int size = wet.size;
            auto worldX = [&](int col)
            {
                const int m = ((wet.firstI % size) + size) % size;
                const int i = wet.firstI + ((col - m) % size + size) % size;
                return (i + 0.5f) * wet.cellSize;
            };
            const bool ok = Fill(g_debugTex, [&](int k, int, int col)
            {
                uint32_t excess = 255;
                if (mode == 3)
                {
                    const float x = worldX(col) * 0.1f;
                    excess = x - std::floor(x) < 0.5f ? 255u : 0u;
                }
                const uint32_t nx = wet.normal ? wet.normal[k * 2] : 128u, ny = wet.normal ? wet.normal[k * 2 + 1] : 128u;
                return (excess << 24) | (nx << 16) | (ny << 8);
            });
            if (!ok) return false;
            g_debugTex.version = wet.version;
            g_debugTex.mode = mode;
        }
        device->SetTexture(sampler, g_debugTex.texture);
        Linear(device, sampler);
        return true;
    }

    IDirect3DTexture9* Get(IDirect3DDevice9* device)
    {
        const fields::WetGrid& wet = fields::Wet();
        if (!device || !wet.excess || wet.size <= 0) return nullptr;
        if (device != g_device && g_device) Release();
        g_device = device;
        if (g_texture && wet.size != g_size) { g_texture->Release(); g_texture = nullptr; g_uploaded = false; }
        if (!g_texture)
        {
            if (FAILED(device->CreateTexture(wet.size, wet.size, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_texture, nullptr)))
            {
                g_texture = nullptr;
                return nullptr;
            }
            g_device = device;
            g_size = wet.size;
        }
        if (!g_uploaded || wet.version != g_version)
        {
            D3DLOCKED_RECT lr{};
            if (SUCCEEDED(g_texture->LockRect(0, &lr, nullptr, 0)))
            {
                for (int row = 0; row < wet.size; ++row)
                {
                    uint32_t* out = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lr.pBits) + row * lr.Pitch);
                    for (int col = 0; col < wet.size; ++col)
                    {
                        const int k = row * wet.size + col;
                        const uint32_t nx = wet.normal ? wet.normal[k * 2] : 128u, ny = wet.normal ? wet.normal[k * 2 + 1] : 128u;
                        out[col] = (static_cast<uint32_t>(wet.excess[k]) << 24) | (nx << 16) | (ny << 8);
                    }
                }
                g_texture->UnlockRect(0);
                g_version = wet.version;
                g_uploaded = true;
            }
        }
        return g_texture;
    }

    bool Bind(IDirect3DDevice9* device, DWORD sampler)
    {
        IDirect3DTexture9* tex = Get(device);
        if (!tex) return false;
        device->SetTexture(sampler, tex);
        device->SetSamplerState(sampler, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(sampler, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(sampler, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        device->SetSamplerState(sampler, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        device->SetSamplerState(sampler, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        device->SetSamplerState(sampler, D3DSAMP_SRGBTEXTURE, FALSE);
        return true;
    }

    float FarExcess() { return fields::Wet().farExcess; }

    bool BindFarPixel(IDirect3DDevice9* device, DWORD sampler)
    {
        if (!EnsureFar(device)) return false;
        device->SetTexture(sampler, g_farPixel);
        device->SetSamplerState(sampler, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(sampler, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(sampler, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        device->SetSamplerState(sampler, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        device->SetSamplerState(sampler, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        device->SetSamplerState(sampler, D3DSAMP_SRGBTEXTURE, FALSE);
        return true;
    }

    bool BindFarVertex(IDirect3DDevice9* device, DWORD vertexSampler)
    {
        if (!EnsureFar(device)) return false;
        device->SetTexture(vertexSampler, g_farVertex);
        device->SetSamplerState(vertexSampler, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        device->SetSamplerState(vertexSampler, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        device->SetSamplerState(vertexSampler, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        device->SetSamplerState(vertexSampler, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        device->SetSamplerState(vertexSampler, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        return true;
    }

    bool FarMapping(float& inverseExtent, float box[4], int& size, float& outside)
    {
        const fields::WetGrid& wet = fields::Wet();
        outside = wet.farExcess;
        if (!wet.farBytes || wet.farSize <= 0) return false;
        const float extent = wet.farSize * wet.farCellSize;
        inverseExtent = 1.0f / extent;
        size = wet.farSize;
        box[0] = wet.farFirstI * wet.farCellSize;
        box[1] = wet.farFirstJ * wet.farCellSize;
        box[2] = (wet.farFirstI + wet.farSize) * wet.farCellSize;
        box[3] = (wet.farFirstJ + wet.farSize) * wet.farCellSize;
        return true;
    }

    bool Mapping(float& inverseExtent, float box[4])
    {
        const fields::WetGrid& wet = fields::Wet();
        if (!wet.excess || wet.size <= 0) return false;
        const float extent = wet.size * wet.cellSize;
        inverseExtent = 1.0f / extent;
        box[0] = wet.firstI * wet.cellSize;
        box[1] = wet.firstJ * wet.cellSize;
        box[2] = (wet.firstI + wet.size) * wet.cellSize;
        box[3] = (wet.firstJ + wet.size) * wet.cellSize;
        return true;
    }
}

namespace wxl_livingazeroth::moisturetex
{
    void GpuMemory(memuse::Gpu& out)
    {
        out.Add(g_texture);
        out.Add(g_farPixel);
        out.Add(g_farVertex);
        out.Add(g_surfaceTex.texture);
        out.Add(g_debugTex.texture);
    }
}
