// Offline tests for the sun shafts (env/ShaftModel, features/SunShaftShaders.hpp): the strength model (the air, the
// time of day, rain and storms, indoors, the moon), the sun's place on the screen and its fade, the view ray and its
// world direction as the shaders work them out, the cloud deck's helpers; the three pixel shaders compiled for
// ps_3_0 (instruction slots under the budget); and on the GPU, if one can be had: an occluder between a pixel and the
// sun darkens its shaft and nothing beside it, the deck's shadow only ever darkens the in-scatter, the composite
// adds nothing when the parts are off, never more than its cap and less over near surfaces -- and what the pass
// costs at 1920 x 1080. No game.
//
// Usage:  shafts_tests [--no-gpu]
#include "env/ShaftModel.hpp"
#include "features/SunShaftShaders.hpp"

#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace sm = wxl_livingazeroth::shafts;

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at line " + std::to_string(__LINE__)); } while (false)

namespace
{
    unsigned g_passed = 0;
    template <class F> void Test(const char* name, F f)
    {
        f();
        ++g_passed;
        std::printf("PASS %s\n", name);
    }

    bool Close(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }
    float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
    void Norm(float v[3]) { const float l = std::sqrt(Dot(v, v)); for (int i = 0; i < 3; ++i) v[i] /= l; }

    // The fog's sky night (env/Fog FogNight), to feed the model the way the runtime does.
    float SkyNight(float hour)
    {
        auto smooth = [](float a, float b, float v) { float t = std::clamp((v - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); };
        if (hour >= 18.0f) return smooth(18.0f, 22.0f, hour);
        if (hour < 3.0f) return 1.0f;
        return 1.0f - smooth(3.0f, 6.0f, hour);
    }

    sm::Inputs Place(float fogginess, float hour, float sunHeight)
    {
        sm::Inputs in;
        in.fogginess = fogginess;
        in.hour = hour;
        in.night = SkyNight(hour);
        in.sunHeight = sunHeight;
        return in;
    }

    // --- matrices as the client keeps them: row vectors (clip = p x view x proj), OpenGL-style depth, w = view depth
    struct Camera { float view[16], proj[16]; float pos[3], right[3], up[3], fwd[3]; };

    void Projection(float fovY, float aspect, float n, float f, float m20, float m21, float proj[16])
    {
        std::fill(proj, proj + 16, 0.0f);
        const float m11 = 1.0f / std::tan(fovY * 0.5f);
        proj[0] = m11 / aspect;
        proj[5] = m11;
        proj[8] = m20; proj[9] = m21;          // an off-centre frustum's shift
        proj[10] = (f + n) / (f - n);
        proj[11] = 1.0f;                        // w = the view depth
        proj[14] = -2.0f * f * n / (f - n);
    }

    // A camera at pos looking at yaw (from +x toward +y) and pitch (up), z up; mirrored flips the view's x axis (det -1).
    Camera MakeCamera(const float pos[3], float yaw, float pitch, bool relative, bool mirrored = false)
    {
        Camera c{};
        for (int i = 0; i < 3; ++i) c.pos[i] = pos[i];
        const float fwd[3] = { std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch) };
        const float worldUp[3] = { 0.0f, 0.0f, 1.0f };
        // right = up x fwd (x right, y up, z forward: a left-handed view, as the client's w = +z)
        float right[3] = { worldUp[1] * fwd[2] - worldUp[2] * fwd[1], worldUp[2] * fwd[0] - worldUp[0] * fwd[2], worldUp[0] * fwd[1] - worldUp[1] * fwd[0] };
        Norm(right);
        float up[3] = { fwd[1] * right[2] - fwd[2] * right[1], fwd[2] * right[0] - fwd[0] * right[2], fwd[0] * right[1] - fwd[1] * right[0] };
        if (mirrored) for (float& v : right) v = -v;
        for (int i = 0; i < 3; ++i) { c.right[i] = right[i]; c.up[i] = up[i]; c.fwd[i] = fwd[i]; }
        std::fill(c.view, c.view + 16, 0.0f);
        for (int i = 0; i < 3; ++i) { c.view[4 * i + 0] = right[i]; c.view[4 * i + 1] = up[i]; c.view[4 * i + 2] = fwd[i]; }
        const float* p = relative ? nullptr : pos;
        c.view[12] = p ? -Dot(p, right) : 0.0f;
        c.view[13] = p ? -Dot(p, up) : 0.0f;
        c.view[14] = p ? -Dot(p, fwd) : 0.0f;
        c.view[15] = 1.0f;
        Projection(1.0472f, 16.0f / 9.0f, 0.2f, 1000.0f, 0.0f, 0.0f, c.proj);
        return c;
    }

    // A world point to viewport uv the way the window glow projects its corners (w = 1).
    bool PointUv(const Camera& c, const float world[3], bool relative, float& u, float& v)
    {
        const float p[3] = { world[0] - (relative ? c.pos[0] : 0.0f), world[1] - (relative ? c.pos[1] : 0.0f), world[2] - (relative ? c.pos[2] : 0.0f) };
        float vv[4], clip[4];
        for (int j = 0; j < 4; ++j) vv[j] = p[0] * c.view[j] + p[1] * c.view[4 + j] + p[2] * c.view[8 + j] + c.view[12 + j];
        for (int j = 0; j < 4; ++j) clip[j] = vv[0] * c.proj[j] + vv[1] * c.proj[4 + j] + vv[2] * c.proj[8 + j] + vv[3] * c.proj[12 + j];
        if (clip[3] <= 0.0f) return false;
        u = 0.5f + 0.5f * clip[0] / clip[3];
        v = 0.5f - 0.5f * clip[1] / clip[3];
        return true;
    }

    // --- shaders -------------------------------------------------------------------------------------------------
    struct Compiled { std::vector<DWORD> code; int slots = -1; std::string error; };

    Compiled CompilePs(const char* source)
    {
        Compiled out;
        ID3DBlob* code = nullptr;
        ID3DBlob* err = nullptr;
        const HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", "ps_3_0", 0, 0, &code, &err);
        if (err) { out.error = static_cast<const char*>(err->GetBufferPointer()); err->Release(); }
        if (FAILED(hr) || !code) return out;
        out.code.resize(code->GetBufferSize() / sizeof(DWORD));
        std::memcpy(out.code.data(), code->GetBufferPointer(), out.code.size() * sizeof(DWORD));
        ID3DBlob* text = nullptr;
        if (SUCCEEDED(D3DDisassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr, &text)) && text)
        {
            const std::string dis(static_cast<const char*>(text->GetBufferPointer()), text->GetBufferSize());
            const size_t at = dis.find("approximately ");
            if (at != std::string::npos) out.slots = std::atoi(dis.c_str() + at + 14);
            text->Release();
        }
        code->Release();
        return out;
    }

    // --- the GPU -----------------------------------------------------------------------------------------------------
    constexpr UINT kW = 1920, kH = 1080;
    constexpr float kMaxZ = 0.94f; // the world's depth range (debug/DepthView)

    struct Gpu
    {
        IDirect3D9* d3d = nullptr;
        IDirect3DDevice9* dev = nullptr;
        IDirect3DPixelShader9* source = nullptr, *blur = nullptr, *composite = nullptr;
        IDirect3DTexture9* quarter[2] = {};
        IDirect3DTexture9* frame = nullptr;
        IDirect3DVolumeTexture9* noise = nullptr;
    };

    struct Vtx { float x, y, z, rhw, u, v; };
    void Quad(IDirect3DDevice9* dev, float w, float h)
    {
        const float x0 = -0.5f, y0 = -0.5f, x1 = x0 + w, y1 = y0 + h;
        const Vtx q[4] = { { x0, y0, 0, 1, 0, 0 }, { x1, y0, 0, 1, 1, 0 }, { x0, y1, 0, 1, 0, 1 }, { x1, y1, 0, 1, 1, 1 } };
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));
    }

    void Sampler(IDirect3DDevice9* dev, DWORD s, D3DTEXTUREFILTERTYPE f, D3DTEXTUREADDRESS a, D3DTEXTUREFILTERTYPE mip = D3DTEXF_NONE)
    {
        dev->SetSamplerState(s, D3DSAMP_MINFILTER, f);
        dev->SetSamplerState(s, D3DSAMP_MAGFILTER, f);
        dev->SetSamplerState(s, D3DSAMP_MIPFILTER, mip);
        dev->SetSamplerState(s, D3DSAMP_ADDRESSU, a);
        dev->SetSamplerState(s, D3DSAMP_ADDRESSV, a);
        dev->SetSamplerState(s, D3DSAMP_ADDRESSW, a);
        dev->SetSamplerState(s, D3DSAMP_BORDERCOLOR, 0);
    }

    bool Target(IDirect3DDevice9* dev, IDirect3DTexture9* t)
    {
        IDirect3DSurface9* s = nullptr;
        if (FAILED(t->GetSurfaceLevel(0, &s)) || !s) return false;
        const bool ok = SUCCEEDED(dev->SetRenderTarget(0, s));
        s->Release();
        return ok;
    }

    bool InitGpu(Gpu& g, const Compiled& source, const Compiled& blur, const Compiled& composite)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"ShaftTests";
        RegisterClassW(&wc);
        HWND window = CreateWindowW(wc.lpszClassName, L"shafts", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
        g.d3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (!g.d3d || !window) return false;
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window;
        pp.BackBufferWidth = 64; pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        if (FAILED(g.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g.dev))) return false;
        if (FAILED(g.dev->CreatePixelShader(source.code.data(), &g.source)) || FAILED(g.dev->CreatePixelShader(blur.code.data(), &g.blur)) ||
            FAILED(g.dev->CreatePixelShader(composite.code.data(), &g.composite)))
            throw std::runtime_error("CreatePixelShader failed on this GPU");
        for (auto*& t : g.quarter)
            if (FAILED(g.dev->CreateTexture(kW / 4, kH / 4, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t, nullptr))) return false;
        if (FAILED(g.dev->CreateTexture(kW, kH, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g.frame, nullptr))) return false;
        // The deck's noise: 32^3 L8, value noise on a period-8 lattice, smoothly interpolated, with three box-filtered levels.
        if (FAILED(g.dev->CreateVolumeTexture(32, 32, 32, 4, 0, D3DFMT_L8, D3DPOOL_MANAGED, &g.noise, nullptr))) return false;
        auto hash = [](int x, int y, int z) { uint32_t h = static_cast<uint32_t>((x & 7) * 374761393 + (y & 7) * 668265263 + (z & 7) * 2147483647); h = (h ^ (h >> 13)) * 1274126177u; return ((h ^ (h >> 16)) & 0xFF) / 255.0f; };
        std::vector<float> level(32 * 32 * 32);
        for (int z = 0; z < 32; ++z)
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                {
                    const float fx = x / 4.0f, fy = y / 4.0f, fz = z / 4.0f;
                    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy), iz = static_cast<int>(fz);
                    auto s = [](float t) { return t * t * (3 - 2 * t); };
                    const float tx = s(fx - ix), ty = s(fy - iy), tz = s(fz - iz);
                    float v = 0;
                    for (int c = 0; c < 8; ++c)
                        v += hash(ix + (c & 1), iy + ((c >> 1) & 1), iz + (c >> 2)) * ((c & 1) ? tx : 1 - tx) * (((c >> 1) & 1) ? ty : 1 - ty) * ((c >> 2) ? tz : 1 - tz);
                    level[(z * 32 + y) * 32 + x] = v;
                }
        for (UINT m = 0, n = 32; m < 4; ++m, n /= 2)
        {
            D3DLOCKED_BOX box{};
            if (FAILED(g.noise->LockBox(m, &box, nullptr, 0))) return false;
            const UINT step = 32 / n;
            for (UINT z = 0; z < n; ++z)
                for (UINT y = 0; y < n; ++y)
                    for (UINT x = 0; x < n; ++x)
                    {
                        float sum = 0;
                        for (UINT k = 0; k < step * step * step; ++k)
                            sum += level[((z * step + k / (step * step)) * 32 + y * step + (k / step) % step) * 32 + x * step + k % step];
                        static_cast<uint8_t*>(box.pBits)[z * box.SlicePitch + y * box.RowPitch + x] = static_cast<uint8_t>(std::lround(sum / (step * step * step) * 255.0f));
                    }
            g.noise->UnlockBox(m);
        }
        return true;
    }

    void ReleaseGpu(Gpu& g)
    {
        for (auto*& t : g.quarter) if (t) t->Release();
        if (g.frame) g.frame->Release();
        if (g.noise) g.noise->Release();
        for (auto* ps : { g.source, g.blur, g.composite }) if (ps) ps->Release();
        if (g.dev) g.dev->Release();
        if (g.d3d) g.d3d->Release();
        g = Gpu{};
    }

    // The scene's depth: 0..kMaxZ for the world (OpenGL-style: ndc = m22 + m32 / z), 1 for the sky.
    float DepthOf(const float proj[16], float z) { return kMaxZ * ((proj[10] + proj[14] / z) * 0.5f + 0.5f); }

    // A depth picture: `rows(y)` gives each row's view depth (<= 0: the sky); `box` (x0, y0, x1, y1, z) an occluder over it.
    IDirect3DTexture9* DepthTexture(IDirect3DDevice9* dev, const float proj[16], float groundFrom, const float* box)
    {
        IDirect3DTexture9* t = nullptr;
        if (FAILED(dev->CreateTexture(kW, kH, 1, 0, D3DFMT_R32F, D3DPOOL_MANAGED, &t, nullptr))) throw std::runtime_error("no R32F texture");
        D3DLOCKED_RECT lr{};
        t->LockRect(0, &lr, nullptr, 0);
        for (UINT y = 0; y < kH; ++y)
        {
            float* row = reinterpret_cast<float*>(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch);
            // Below groundFrom (share of the height): ground, from 300 yd at the horizon to 4 yd at the bottom.
            const float g = (static_cast<float>(y) / kH - groundFrom) / (1.0f - groundFrom);
            const float rowDepth = g < 0.0f ? 1.0f : DepthOf(proj, 300.0f * std::pow(4.0f / 300.0f, g));
            for (UINT x = 0; x < kW; ++x)
            {
                float d = rowDepth;
                if (box && x >= box[0] && x < box[2] && y >= box[1] && y < box[3]) d = DepthOf(proj, box[4]);
                row[x] = d;
            }
        }
        t->UnlockRect(0);
        return t;
    }

    struct PassSetup
    {
        const float* proj;
        const float* sunView;   // toward the sun, view space
        const float* sunWorld;
        float sunU, sunV;
        float airPerYard = 0.01f;
        bool  source = true;     // the screen-space source
        bool  volume = false;    // the in-scatter under the deck
        float deckDensity = 0.0f;
        int   samples = 32;
        bool  smooth = true;
        float shaftGain = 0.6f, volumeGain = 0.5f, cap = 0.4f;
    };

    // The source, the blurs and (optionally) the composite, constants packed as features/SunShafts packs them.
    IDirect3DTexture9* RunShafts(Gpu& g, IDirect3DTexture9* depth, const PassSetup& s, bool composite, IDirect3DTexture9* compositeDepth = nullptr)
    {
        IDirect3DDevice9* dev = g.dev;
        const UINT tw = kW / 4, th = kH / 4;
        dev->SetVertexShader(nullptr);
        dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
        dev->SetRenderState(D3DRS_ZENABLE, FALSE);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);
        dev->SetDepthStencilSurface(nullptr);
        const D3DVIEWPORT9 tv = { 0, 0, tw, th, 0.0f, 1.0f };
        const float m22 = s.proj[10], m32 = s.proj[14];
        const float depthC[4] = { 0.0f, 1.0f / kMaxZ, kMaxZ + 1.0e-4f, 0.0f };
        // 1. The source. The camera at the origin looking along +x (the world rows: view x = -y... any orthonormal set).
        Target(dev, g.quarter[0]);
        dev->SetViewport(&tv);
        const float c[15 * 4] = {
            depthC[0], depthC[1], depthC[2], depthC[3],
            s.proj[0], s.proj[5], s.proj[8], s.proj[9],
            m22, m32, 1000.0f, s.airPerYard,
            0.0f, 0.0f, 1.0f, 1.0f,
            4.0f / kW, 4.0f / kH, 1.0f / kW, 1.0f / kH,
            s.sunView[0], s.sunView[1], s.sunView[2], s.source ? 1.0f : 0.0f,
            0.0f, 0.0f, 1.0f, 1.0f,
            0.0f, -1.0f, 0.0f, 0.0f,   // view x -> world -y
            0.0f, 0.0f, 1.0f, 0.0f,    // view y -> world z
            1.0f, 0.0f, 0.0f, 0.0f,    // view z -> world x
            0.0f, 0.0f, 0.0f, s.volume ? 1.0f : 0.0f,
            200.0f, 320.0f, s.deckDensity, 1.0f / 600.0f,
            0.3f, 600.0f, 2.0f, 1200.0f,
            0.1f, 0.2f, 0.3f, 0.01f,
            s.sunWorld[0], s.sunWorld[1], s.sunWorld[2], 12.0f,
        };
        dev->SetPixelShaderConstantF(0, c, 15);
        dev->SetPixelShader(g.source);
        dev->SetTexture(0, depth);
        Sampler(dev, 0, D3DTEXF_POINT, D3DTADDRESS_CLAMP);
        dev->SetTexture(2, s.volume ? g.noise : nullptr);
        Sampler(dev, 2, D3DTEXF_LINEAR, D3DTADDRESS_WRAP, D3DTEXF_LINEAR);
        Quad(dev, static_cast<float>(tw), static_cast<float>(th));
        dev->SetTexture(2, nullptr);
        // 2. The blurs.
        IDirect3DTexture9* result = g.quarter[0];
        dev->SetPixelShader(g.blur);
        Sampler(dev, 0, D3DTEXF_LINEAR, D3DTADDRESS_BORDER);
        for (int p = 0; p < (s.smooth ? 2 : 1); ++p)
        {
            Target(dev, g.quarter[(p + 1) & 1]);
            dev->SetViewport(&tv);
            const float b[8] = { s.sunU, s.sunV, p == 0 ? 0.85f : 2.5f * 0.85f / s.samples, p == 0 ? static_cast<float>(s.samples) : 8.0f,
                                 p == 0 ? 0.985f : 1.0f, p == 0 ? 1.0f : 0.0f, 1.0f / tw, 1.0f / th };
            dev->SetPixelShaderConstantF(0, b, 2);
            dev->SetTexture(0, g.quarter[p & 1]);
            Quad(dev, static_cast<float>(tw), static_cast<float>(th));
            result = g.quarter[(p + 1) & 1];
        }
        dev->SetTexture(0, nullptr);
        if (!composite) return result;
        // 3. Over a mid-grey frame, added.
        Target(dev, g.frame);
        const D3DVIEWPORT9 fv = { 0, 0, kW, kH, 0.0f, 1.0f };
        dev->SetViewport(&fv);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 64, 64, 64), 1.0f, 0);
        const float k[6 * 4] = {
            depthC[0], depthC[1], depthC[2], depthC[3],
            m22, m32, 1.0f / 20.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 0.5f / tw, 0.5f / th,
            s.shaftGain, s.shaftGain, s.shaftGain, s.cap,
            s.volumeGain, s.volumeGain, s.volumeGain, 0.6f,
        };
        dev->SetPixelShaderConstantF(0, k, 6);
        dev->SetPixelShader(g.composite);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x07);
        dev->SetTexture(0, compositeDepth ? compositeDepth : depth);
        Sampler(dev, 0, D3DTEXF_POINT, D3DTADDRESS_CLAMP);
        dev->SetTexture(1, result);
        Sampler(dev, 1, D3DTEXF_LINEAR, D3DTADDRESS_CLAMP);
        Quad(dev, static_cast<float>(kW), static_cast<float>(kH));
        dev->SetTexture(0, nullptr);
        dev->SetTexture(1, nullptr);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);
        return g.frame;
    }

    // A render target's pixels (A8R8G8B8).
    std::vector<uint32_t> ReadBack(IDirect3DDevice9* dev, IDirect3DTexture9* t, UINT& w, UINT& h)
    {
        IDirect3DSurface9* s = nullptr;
        t->GetSurfaceLevel(0, &s);
        D3DSURFACE_DESC d{};
        s->GetDesc(&d);
        IDirect3DSurface9* sys = nullptr;
        dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr);
        CHECK(SUCCEEDED(dev->GetRenderTargetData(s, sys)));
        D3DLOCKED_RECT lr{};
        sys->LockRect(&lr, nullptr, D3DLOCK_READONLY);
        std::vector<uint32_t> px(d.Width * d.Height);
        for (UINT y = 0; y < d.Height; ++y) std::memcpy(&px[y * d.Width], static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch, d.Width * 4);
        sys->UnlockRect();
        sys->Release();
        s->Release();
        w = d.Width; h = d.Height;
        return px;
    }
    float R(uint32_t argb) { return ((argb >> 16) & 0xFF) / 255.0f; }

    // One run in its own scene, read back after it.
    std::vector<uint32_t> Run(Gpu& g, IDirect3DTexture9* depth, const PassSetup& s, bool composite, UINT& w, UINT& h, IDirect3DTexture9* compositeDepth = nullptr)
    {
        g.dev->BeginScene();
        IDirect3DTexture9* out = RunShafts(g, depth, s, composite, compositeDepth);
        g.dev->EndScene();
        return ReadBack(g.dev, out, w, h);
    }
    float G(uint32_t argb) { return ((argb >> 8) & 0xFF) / 255.0f; }
}

int main(int argc, char** argv)
{
    bool gpu = true;
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], "--no-gpu") == 0) gpu = false;
    try
    {
        // ---------------------------------------------------------------- the strength model
        Test("the air: nothing is nothing, Duskwood's fogginess alone is thick, veils add up but never past 1", [] {
            CHECK(sm::AirAmount(0, 0, 0, 0, 0) == 0.0f);
            CHECK(Close(sm::AirAmount(0.6f, 0, 0, 0, 0), 0.72f, 1e-5f));
            CHECK(Close(sm::AirAmount(0, 0, 0.6f, 0, 0), 0.72f, 1e-5f)); // its area fog, once built up, counts the same
            CHECK(sm::AirAmount(0.15f, 0.5f, 0, 0, 0) > sm::AirAmount(0.15f, 0, 0, 0, 0));
            CHECK(sm::AirAmount(1, 1, 1, 1, 1) <= 1.0f && sm::AirAmount(1, 1, 1, 1, 1) > 0.99f);
            CHECK(sm::AirAmount(-1, -1, -1, -1, -1) == 0.0f);
        });
        Test("the low sun: full at 07:00 and 19:00, nothing at noon or midnight, by the hour round the clock", [] {
            CHECK(Close(sm::GoldenHour(7.0f), 1.0f, 1e-6f) && Close(sm::GoldenHour(19.0f), 1.0f, 1e-6f));
            CHECK(sm::GoldenHour(12.0f) < 0.01f && sm::GoldenHour(0.0f) < 0.01f);
            CHECK(Close(sm::GoldenHour(31.0f), 1.0f, 1e-5f) && Close(sm::GoldenHour(-5.0f), 1.0f, 1e-5f));
            CHECK(sm::GoldenHour(8.0f) > 0.5f && sm::GoldenHour(17.5f) > 0.4f);
        });
        Test("Duskwood's fog in the morning and evening is strong; a forest at noon is subtle, a few times fainter", [] {
            sm::Inputs dusk = Place(0.6f, 7.0f, 0.3f);
            dusk.areaFog = 0.6f;
            const sm::Strength morning = sm::Evaluate(dusk);
            dusk.hour = 19.0f; dusk.night = SkyNight(19.0f);
            const sm::Strength evening = sm::Evaluate(dusk);
            const sm::Strength forestNoon = sm::Evaluate(Place(0.15f, 12.0f, 0.85f));
            std::printf("     Duskwood 07:00 %.2f, 19:00 %.2f; a forest at noon %.2f\n", morning.total, evening.total, forestNoon.total);
            CHECK(morning.total > 0.75f && !morning.why);
            CHECK(evening.total > 0.6f);
            CHECK(forestNoon.total > 0.12f && forestNoon.total < 0.3f && !forestNoon.why);
            CHECK(morning.total > 3.0f * forestNoon.total);
            CHECK(morning.airPerYard > forestNoon.airPerYard);
        });
        Test("a misty forest morning (Ashenvale) is well above its own noon", [] {
            sm::Inputs in = Place(0.15f, 7.5f, 0.25f);
            in.mist = 0.6f;
            const float misty = sm::Evaluate(in).total, noon = sm::Evaluate(Place(0.15f, 12.0f, 0.85f)).total;
            CHECK(misty > 2.5f * noon);
        });
        Test("rain and storms: light rain barely, heavy rain or a storm nothing", [] {
            sm::Inputs in = Place(0.6f, 7.0f, 0.3f);
            const float dry = sm::Evaluate(in).total;
            in.precipitation = 0.2f;
            CHECK(sm::Evaluate(in).total > 0.9f * dry);
            in.precipitation = 0.45f;
            CHECK(sm::Evaluate(in).total < 0.5f * dry);
            in.precipitation = 0.8f;
            sm::Strength s = sm::Evaluate(in);
            CHECK(s.total == 0.0f && s.why);
            in.precipitation = 0.0f; in.storminess = 0.8f;
            s = sm::Evaluate(in);
            CHECK(s.total == 0.0f && s.why && std::strcmp(s.why, "a storm") == 0);
        });
        Test("indoors nothing; in a doorway (half inside) half", [] {
            sm::Inputs in = Place(0.6f, 7.0f, 0.3f);
            const float out = sm::Evaluate(in).total;
            in.interior = 1.0f;
            const sm::Strength s = sm::Evaluate(in);
            CHECK(s.total == 0.0f && s.why && std::strcmp(s.why, "indoors") == 0);
            in.interior = 0.5f;
            CHECK(Close(sm::Evaluate(in).total, 0.5f * out, 1e-5f));
        });
        Test("night: nothing without the Moon setting, faint with it; the evening fades with the sky (FogNight)", [] {
            sm::Inputs in = Place(0.6f, 1.0f, 0.4f);
            sm::Strength s = sm::Evaluate(in);
            CHECK(s.total == 0.0f && s.why);
            in.moon = 1.0f;
            s = sm::Evaluate(in);
            CHECK(s.total > 0.05f && s.total <= 0.3f && !s.why);
            const float at19 = sm::Evaluate(Place(0.6f, 19.0f, 0.2f)).total, at21 = sm::Evaluate(Place(0.6f, 21.0f, 0.2f)).total;
            CHECK(at21 < 0.3f * at19 && at21 > 0.0f);
            CHECK(sm::Evaluate(Place(0.6f, 23.0f, 0.2f)).total == 0.0f);
        });
        Test("a light on the horizon gives nothing; just above it, the full low sun", [] {
            CHECK(sm::Evaluate(Place(0.6f, 7.0f, 0.0f)).total == 0.0f);
            CHECK(sm::Evaluate(Place(0.6f, 7.0f, 0.1f)).total > 0.7f);
        });

        // ---------------------------------------------------------------- the sun on the screen
        const float origin[3] = { 1234.5f, -567.25f, 89.0f };
        Test("straight ahead it's the screen's centre; behind, nowhere; the fade 1 on the screen", [&] {
            const Camera c = MakeCamera(origin, 0.7f, 0.1f, false);
            sm::SunScreen s = sm::ProjectSun(c.view, c.proj, c.fwd, 0.35f);
            CHECK(s.inFront && Close(s.u, 0.5f, 1e-4f) && Close(s.v, 0.5f, 1e-4f) && s.fade == 1.0f);
            const float back[3] = { -c.fwd[0], -c.fwd[1], -c.fwd[2] };
            s = sm::ProjectSun(c.view, c.proj, back, 0.35f);
            CHECK(!s.inFront && s.fade == 0.0f);
        });
        Test("up by half the vertical field of view it's the top edge; up is v < 0.5, right is u > 0.5", [&] {
            const Camera c = MakeCamera(origin, 0.0f, 0.0f, false);
            const float half = 1.0472f * 0.5f;
            float dir[3] = { std::cos(half) * c.fwd[0] + std::sin(half) * c.up[0], std::cos(half) * c.fwd[1] + std::sin(half) * c.up[1],
                             std::cos(half) * c.fwd[2] + std::sin(half) * c.up[2] };
            sm::SunScreen s = sm::ProjectSun(c.view, c.proj, dir, 0.35f);
            CHECK(s.inFront && Close(s.v, 0.0f, 1e-4f) && Close(s.u, 0.5f, 1e-4f) && s.fade == 1.0f);
            float right[3] = { c.fwd[0] + 0.2f * c.right[0], c.fwd[1] + 0.2f * c.right[1], c.fwd[2] + 0.2f * c.right[2] };
            Norm(right);
            s = sm::ProjectSun(c.view, c.proj, right, 0.35f);
            CHECK(s.u > 0.5f && Close(s.v, 0.5f, 1e-4f));
        });
        Test("leaving the screen it fades: 1 at the edge, between past it, 0 from the margin on", [&] {
            CHECK(sm::ScreenFade(1.0f, 0.5f, 0.35f) == 1.0f && sm::ScreenFade(0.0f, 0.0f, 0.35f) == 1.0f);
            const float a = sm::ScreenFade(1.1f, 0.5f, 0.35f), b = sm::ScreenFade(1.2f, 0.5f, 0.35f);
            CHECK(a < 1.0f && b < a && b > 0.0f);
            CHECK(sm::ScreenFade(1.35f, 0.5f, 0.35f) == 0.0f && sm::ScreenFade(-0.5f, 0.5f, 0.35f) == 0.0f);
            CHECK(sm::ScreenFade(1.2f, 1.2f, 0.35f) < sm::ScreenFade(1.2f, 0.5f, 0.35f)); // a corner is farther
            const Camera c = MakeCamera(origin, 0.0f, 0.0f, false);
            float dir[3] = { c.fwd[0] + 1.5f * c.right[0], c.fwd[1] + 1.5f * c.right[1], c.fwd[2] + 1.5f * c.right[2] };
            Norm(dir);
            const sm::SunScreen s = sm::ProjectSun(c.view, c.proj, dir, 0.35f);
            CHECK(s.inFront && s.u > 1.0f && s.fade < 1.0f);
        });
        Test("the sun's direction lands where a far point along it lands (the window glow's projection), camera-relative or not", [&] {
            for (bool relative : { false, true })
                for (float yaw : { 0.0f, 1.1f, -2.4f })
                    for (float pitch : { -0.3f, 0.0f, 0.45f })
                    {
                        const Camera c = MakeCamera(origin, yaw, pitch, relative);
                        float dir[3] = { std::cos(yaw + 0.2f) * 0.8f, std::sin(yaw + 0.2f) * 0.8f, 0.45f + 0.3f * pitch };
                        Norm(dir);
                        const sm::SunScreen s = sm::ProjectSun(c.view, c.proj, dir, 0.35f);
                        const float distant[3] = { origin[0] + dir[0] * 1.0e5f, origin[1] + dir[1] * 1.0e5f, origin[2] + dir[2] * 1.0e5f };
                        float u = 0, v = 0;
                        const bool front = PointUv(c, distant, relative, u, v);
                        CHECK(front == s.inFront);
                        if (front) CHECK(Close(s.u, u, 2e-3f) && Close(s.v, v, 2e-3f));
                    }
        });
        Test("the shaders' view ray through the sun's uv is the sun's view direction, off-centre frustum too", [&] {
            Camera c = MakeCamera(origin, 0.4f, 0.2f, false);
            Projection(1.2f, 1.6f, 0.2f, 1200.0f, 0.03f, -0.02f, c.proj);
            for (float a : { 0.0f, 0.15f, -0.3f })
            {
                float dir[3] = { c.fwd[0] + a * c.right[0] + 0.2f * c.up[0], c.fwd[1] + a * c.right[1] + 0.2f * c.up[1], c.fwd[2] + a * c.right[2] + 0.2f * c.up[2] };
                Norm(dir);
                const sm::SunScreen s = sm::ProjectSun(c.view, c.proj, dir, 0.35f);
                float ray[3];
                sm::ViewRay(c.proj, s.u, s.v, ray);
                CHECK(Dot(ray, s.view) > 0.99999f);
            }
        });
        Test("view to world undoes the view (mirrored too): the deck march heads where the pixel looks", [&] {
            for (bool mirrored : { false, true })
            {
                const Camera c = MakeCamera(origin, -0.8f, 0.3f, true, mirrored);
                float dir[3] = { 0.3f, -0.5f, 0.8f };
                Norm(dir);
                const sm::SunScreen s = sm::ProjectSun(c.view, c.proj, dir, 0.35f);
                float back[3];
                sm::ViewToWorld(c.view, s.view, back);
                CHECK(Dot(back, dir) > 0.99999f);
            }
        });

        // ---------------------------------------------------------------- the deck's helpers
        Test("under the deck: all below its base, none above its soft bottom, between in it", [] {
            CHECK(sm::UnderDeck(100.0f, 200.0f, 320.0f) == 1.0f && sm::UnderDeck(200.0f, 200.0f, 320.0f) == 1.0f);
            CHECK(sm::UnderDeck(230.0f, 200.0f, 320.0f) == 0.0f); // its soft bottom: 30 yd of a 120 yd deck
            const float mid = sm::UnderDeck(215.0f, 200.0f, 320.0f);
            CHECK(mid > 0.0f && mid < 1.0f);
            CHECK(sm::UnderDeck(50.0f, 0.0f, 1000.0f) > 0.0f && sm::UnderDeck(150.0f, 0.0f, 1000.0f) == 0.0f); // at most 100 yd (not 250)
        });
        Test("the deck's mean extinction is its glow's; its slab along a ray", [] {
            CHECK(Close(sm::DeckMeanExtinction(0.003f, 1.0f), 0.003f * 0.55f, 1e-8f));
            CHECK(Close(sm::DeckMeanExtinction(0.003f, 0.0f), 0.003f * 0.55f * 0.3f, 1e-8f));
            CHECK(Close(sm::DeckSlabLength(100.0f, 1.0f, 200.0f, 320.0f, 1200.0f), 120.0f, 1e-3f));   // straight up through it
            CHECK(Close(sm::DeckSlabLength(100.0f, 0.5f, 200.0f, 320.0f, 1200.0f), 240.0f, 1e-3f));
            CHECK(sm::DeckSlabLength(100.0f, 0.0f, 200.0f, 320.0f, 1200.0f) == 0.0f);                   // level, below it
            CHECK(sm::DeckSlabLength(250.0f, 0.0f, 200.0f, 320.0f, 1200.0f) == 1200.0f);                // level, in it
            CHECK(sm::DeckSlabLength(100.0f, -0.5f, 200.0f, 320.0f, 1200.0f) == 0.0f);                  // down, below it
            CHECK(Close(sm::DeckSlabLength(400.0f, -0.25f, 200.0f, 320.0f, 1200.0f), 480.0f, 1e-3f));   // down from above
            CHECK(Close(sm::DeckSlabLength(100.0f, 0.05f, 200.0f, 320.0f, 1200.0f), 0.0f, 1e-3f));      // reaches it past the range
        });

        // ---------------------------------------------------------------- the shaders
        const Compiled source = CompilePs(wxl_livingazeroth::sunshafts::kShaftSourceHlsl);
        const Compiled blur = CompilePs(wxl_livingazeroth::sunshafts::kShaftBlurHlsl);
        const Compiled composite = CompilePs(wxl_livingazeroth::sunshafts::kShaftCompositeHlsl);
        Test("the three shaders compile for ps_3_0, each well under ~3,500 instruction slots", [&] {
            for (const Compiled* c : { &source, &blur, &composite })
            {
                if (!c->error.empty()) std::printf("     %s\n", c->error.c_str());
                CHECK(!c->code.empty());
                CHECK(c->slots > 0 && c->slots < 3500);
            }
            std::printf("     instruction slots: source %d, blur %d, composite %d\n", source.slots, blur.slots, composite.slots);
        });

        if (gpu)
        {
            Gpu g;
            if (!InitGpu(g, source, blur, composite))
            {
                std::printf("SKIP the GPU checks: no D3D9 device\n");
                ReleaseGpu(g);
            }
            else
            {
                float proj[16];
                Projection(1.0472f, 16.0f / 9.0f, 0.2f, 1000.0f, 0.0f, 0.0f, proj);
                // The sun at pixel (960, 250): its view direction is the shaders' ray there; the world one by the
                // source pass's rows (view x -> -y, y -> z, z -> x).
                float sunView[3];
                sm::ViewRay(proj, 0.5f, 250.0f / kH, sunView);
                const float sunWorld[3] = { sunView[2], -sunView[0], sunView[1] };
                PassSetup s{ proj, sunView, sunWorld, 0.5f, 250.0f / kH };
                const float trunk[5] = { 900, 280, 1020, 594, 12 }; // a trunk 12 yd off, below the sun, up into the sky
                const float nearBox[5] = { 800, 500, 1120, 700, 2 }; // something 2 yd in front of you
                IDirect3DTexture9* open = DepthTexture(g.dev, proj, 0.55f, nullptr);
                IDirect3DTexture9* blocked = DepthTexture(g.dev, proj, 0.55f, trunk);
                IDirect3DTexture9* nearer = DepthTexture(g.dev, proj, 0.55f, nearBox);
                UINT w = 0, h = 0;
                Test("GPU: a trunk between the ground and the sun shades the ground behind it; beside it nothing changes", [&] {
                    const std::vector<uint32_t> a = Run(g, open, s, false, w, h), b = Run(g, blocked, s, false, w, h);
                    // Behind the trunk (960, 650) and well to the side (300, 500), in quarter-resolution pixels.
                    const size_t behind = (650 / 4) * w + 960 / 4, side = (500 / 4) * w + 300 / 4, nearSun = (300 / 4) * w + 960 / 4;
                    std::printf("     behind the trunk %.3f (without it %.3f); to the side %.3f (%.3f); near the sun %.3f\n",
                                R(b[behind]), R(a[behind]), R(b[side]), R(a[side]), R(a[nearSun]));
                    CHECK(R(a[behind]) > 0.05f);
                    CHECK(R(b[behind]) < 0.5f * R(a[behind]));
                    CHECK(Close(R(b[side]), R(a[side]), 0.02f));
                    CHECK(R(a[nearSun]) > R(a[behind])); // the shafts grow toward the sun
                });
                Test("GPU: the cloud deck's shadow only darkens the light scattered in under it, in shafts, not evenly", [&] {
                    PassSetup v = s;
                    v.source = false; v.volume = true;
                    v.deckDensity = 0.0f;
                    const std::vector<uint32_t> clear = Run(g, open, v, false, w, h);
                    v.deckDensity = 0.004f; // Fogginess 0.8, as the mountain decks
                    const std::vector<uint32_t> clouded = Run(g, open, v, false, w, h);
                    float sumClear = 0, sumCloud = 0, lo = 1, hi = 0;
                    bool never = true, noSource = true;
                    for (size_t i = 0; i < clear.size(); ++i)
                    {
                        sumClear += G(clear[i]); sumCloud += G(clouded[i]);
                        if (G(clouded[i]) > G(clear[i]) + 2.5f / 255.0f) never = false;
                        if (R(clouded[i]) != 0.0f) noSource = false; // no screen-space source asked for
                        if (G(clear[i]) > 0.1f) { const float ratio = G(clouded[i]) / G(clear[i]); lo = std::min(lo, ratio); hi = std::max(hi, ratio); }
                    }
                    std::printf("     in-scatter: mean %.3f without the deck's shadow, %.3f with it; lit share %.2f .. %.2f\n",
                                sumClear / clear.size(), sumCloud / clear.size(), lo, hi);
                    CHECK(never && noSource);
                    CHECK(sumCloud < 0.95f * sumClear && sumCloud > 0.0f);
                    CHECK(hi - lo > 0.3f); // lit and shadowed air side by side
                });
                Test("GPU: the composite adds nothing with both parts off, never more than its cap, less over near things", [&] {
                    PassSetup off = s;
                    off.source = false;
                    const std::vector<uint32_t> none = Run(g, open, off, true, w, h);
                    PassSetup hot = s;
                    hot.shaftGain = 4.0f;
                    const std::vector<uint32_t> capped = Run(g, open, hot, true, w, h);
                    bool untouched = true, underCap = true;
                    for (size_t i = 0; i < none.size(); ++i)
                    {
                        if ((none[i] & 0xFFFFFF) != 0x404040) untouched = false;
                        if (R(capped[i]) > 64 / 255.0f + 0.4f + 1.5f / 255.0f) underCap = false;
                    }
                    CHECK(untouched && underCap);
                    // The same shafts over the ground 280 yd off and over something 2 yd in front of you.
                    const std::vector<uint32_t> overFar = Run(g, open, s, true, w, h), overNear = Run(g, open, s, true, w, h, nearer);
                    const size_t at = 600 * w + 960;
                    const float addFar = R(overFar[at]) - 64 / 255.0f, addNear = R(overNear[at]) - 64 / 255.0f;
                    std::printf("     added over the far ground %.3f, over the thing in front of you %.3f\n", addFar, addNear);
                    CHECK(addFar > 0.01f && addNear < 0.25f * addFar);
                });
                Test("GPU: the cost at 1920 x 1080 (timestamps; medium quality, 32 samples)", [&] {
                    IDirect3DQuery9* disjoint = nullptr, *freq = nullptr, *t0 = nullptr, *t1 = nullptr;
                    if (FAILED(g.dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &disjoint)) || FAILED(g.dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &freq)) ||
                        FAILED(g.dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &t0)) || FAILED(g.dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &t1)))
                    {
                        std::printf("     (no timestamp queries here)\n");
                        return;
                    }
                    auto measure = [&](const PassSetup& p, int frames) {
                        double best = 1e9;
                        for (int attempt = 0; attempt < 3; ++attempt)
                        {
                            g.dev->BeginScene();
                            disjoint->Issue(D3DISSUE_BEGIN);
                            freq->Issue(D3DISSUE_END);
                            t0->Issue(D3DISSUE_END);
                            for (int f = 0; f < frames; ++f) RunShafts(g, open, p, true);
                            t1->Issue(D3DISSUE_END);
                            disjoint->Issue(D3DISSUE_END);
                            g.dev->EndScene();
                            BOOL dis = TRUE; UINT64 hz = 0, a0 = 0, a1 = 0;
                            while (disjoint->GetData(&dis, sizeof(dis), D3DGETDATA_FLUSH) == S_FALSE) {}
                            while (freq->GetData(&hz, sizeof(hz), D3DGETDATA_FLUSH) == S_FALSE) {}
                            while (t0->GetData(&a0, sizeof(a0), D3DGETDATA_FLUSH) == S_FALSE) {}
                            while (t1->GetData(&a1, sizeof(a1), D3DGETDATA_FLUSH) == S_FALSE) {}
                            if (!dis && hz) best = std::min(best, static_cast<double>(a1 - a0) / hz * 1000.0 / frames);
                        }
                        return best;
                    };
                    PassSetup screenOnly = s;
                    PassSetup both = s;
                    both.volume = true; both.deckDensity = 0.004f;
                    PassSetup low = s;
                    low.smooth = false;
                    const double msScreen = measure(screenOnly, 20), msBoth = measure(both, 20), msLow = measure(low, 20);
                    std::printf("     per frame: %.3f ms (screen-space shafts), %.3f ms (with the in-scatter under the deck), %.3f ms (low quality)\n",
                                msScreen, msBoth, msLow);
                    for (auto* q : { disjoint, freq, t0, t1 }) q->Release();
                    CHECK(msScreen < 0.5 && msBoth < 0.5);
                });
                open->Release();
                blocked->Release();
                nearer->Release();
                ReleaseGpu(g);
            }
        }
    }
    catch (const std::exception& e)
    {
        std::printf("FAIL %s\n", e.what());
        return 1;
    }
    std::printf("%u passed\n", g_passed);
    return 0;
}
