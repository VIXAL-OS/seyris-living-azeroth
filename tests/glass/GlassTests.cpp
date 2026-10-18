// Window glass (env/GlassModel, features/GlassShader, features/GlassShaftShaders, features/ModelIce's WMO edit, the
// shipped tables).
//  1. The rules: the light through glass by time and weather, Schlick's Fresnel, the sun through a pane, the inside or
//     outside decision, the inside colour, the floor light's projection and its leak guards, the fake room's faces.
//  2. The WMO pixel edit with the glass blocks on every stock WMO pixel shader (the client's ps_MapObj*.bls, argv
//     folder): every shadow variant takes them, assembles, and stays within the slots (reported).
//  3. The coloured shafts' shaders compile (slots reported).
//  4. On WARP (the CPU rasterizer behind D3D9On12; no GPU), the blocks drawn: the inside colour, Fresnel outside and
//     the floor light against the rules above. --no-warp skips it.
//  5. The shipped tables (argv data folder): GlassMaterials.cdbc and GlassPanes.cdbc read back sane.
// Run: glass_tests <folder with the WMO ps_MapObj*.bls> <repo data folder> [--no-warp]
#include "env/GlassModel.hpp"
#include "features/Glass.hpp"
#include "features/GlassShader.hpp"
#include "features/ModelIce.hpp"
#include "render/ShaderPatch.hpp"

#include <windows.h>
#include <d3d9.h>
#include <d3d9on12.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <string>
#include <vector>

namespace
{
    namespace gl = wxl_livingazeroth::glass;
    int g_failures = 0;
    void Check(bool ok, const std::string& what)
    {
        std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) ++g_failures;
    }
    bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
    void Norm(float v[3]) { const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); for (int k = 0; k < 3; ++k) v[k] /= n; }

    // --- 1. the rules -----------------------------------------------------------------------------------------
    gl::Pane WallWindow()
    {
        // In the wall x = 0, facing out along +x; 2 yd wide (along y), 3 yd tall (z 1.5 .. 4.5); its room behind it.
        gl::Pane p;
        p.pos[0] = 0; p.pos[1] = 0; p.pos[2] = 3;
        p.out[0] = 1; p.out[1] = 0; p.out[2] = 0;
        p.along[0] = 0; p.along[1] = 1; p.along[2] = 0;    // horizontal, as gen_glass gives it (z x out); up = out x along
        p.up[0] = p.out[1] * p.along[2] - p.out[2] * p.along[1];
        p.up[1] = p.out[2] * p.along[0] - p.out[0] * p.along[2];
        p.up[2] = p.out[0] * p.along[1] - p.out[1] * p.along[0];
        p.halfWidth = 1; p.halfHeight = 1.5f;
        p.roomMin[0] = -10; p.roomMin[1] = -10; p.roomMin[2] = -0.5f;
        p.roomMax[0] = 0.3f; p.roomMax[1] = 10; p.roomMax[2] = 8;
        p.light[0] = 1; p.light[1] = 0.5f; p.light[2] = 0.25f;
        p.kind = gl::kStained;
        return p;
    }

    void RulesTests()
    {
        // The light through glass.
        gl::Inputs noon;
        noon.sun[0] = 0.3f; noon.sun[1] = 0.2f; noon.sun[2] = 0.93f; Norm(noon.sun);
        noon.daylight = 1; noon.night = 0;
        const gl::Light day = gl::Evaluate(noon);
        Check(day.sunGain > 0.95f && day.skyGain > 0.95f && day.day > 0.99f, "clear noon: the sun and the sky come through in full");
        gl::Inputs night = noon; night.night = 1; night.daylight = 0;
        const gl::Light dark = gl::Evaluate(night);
        Check(dark.sunGain == 0 && dark.skyGain == 0 && dark.day == 0, "night (the client's lit windows): no light through, no outside look (they keep their own)");
        gl::Inputs rain = noon; rain.precipitation = 1; rain.overcast = 0.8f;
        gl::Inputs storm = rain; storm.storm = 1;
        gl::Inputs cloud = noon; cloud.overcast = 1;
        const gl::Light r = gl::Evaluate(rain), s = gl::Evaluate(storm), c = gl::Evaluate(cloud);
        char line[256];
        std::snprintf(line, sizeof(line), "weather dims it: sun through overcast %.2f, rain %.2f, storm %.2f; sky overcast %.2f, rain %.2f, storm %.2f",
                      c.sunGain, r.sunGain, s.sunGain, c.skyGain, r.skyGain, s.skyGain);
        Check(c.sunGain < 0.2f && r.sunGain < 0.1f && s.sunGain == 0 && c.skyGain < day.skyGain && r.skyGain < c.skyGain && s.skyGain < r.skyGain && s.skyGain > 0, line);
        gl::Inputs low = noon; low.sun[2] = -0.1f;
        Check(gl::Evaluate(low).sunGain == 0, "the sun below the horizon: none of it through the glass");

        // Fresnel.
        Check(Near(gl::Fresnel(1, 0.04f), 0.04f, 1e-6f) && Near(gl::Fresnel(0, 0.04f), 1, 1e-6f) &&
              gl::Fresnel(0.3f, 0.04f) > gl::Fresnel(0.6f, 0.04f), "Fresnel: F0 face on, 1 at grazing, more at a slant");
        // The sun through a pane.
        const float out[3] = { 1, 0, 0 };
        const float beyond[3] = { 0.8f, 0, 0.6f }, behind[3] = { -0.8f, 0, 0.6f };
        Check(Near(gl::SunThrough(beyond, out, 0), 0.8f, 1e-6f) && gl::SunThrough(behind, out, 0) == 0 &&
              Near(gl::SunThrough(behind, out, 1), 0.1f, 1e-6f) && Near(gl::SunThrough(beyond, out, 1), 0.9f, 1e-6f),
              "the sun through a pane: its cosine beyond it, none behind; frosted spreads it (0.9 / 0.1)");
        // Inside or outside.
        Check(gl::InsideWeight(gl::kInsideFace, gl::kGroupExterior, 0) == 1 && gl::InsideWeight(gl::kOutsideFace, gl::kGroupIndoor, 1) == 0 &&
              gl::InsideWeight(0, gl::kGroupIndoor, 1) == 1 && gl::InsideWeight(0, gl::kGroupIndoor, 0) == 0 &&
              gl::InsideWeight(0, gl::kGroupExterior, 1) == 0 && Near(gl::InsideWeight(0, gl::kGroupIndoor, 0.4f), 0.4f, 1e-6f),
              "inside or outside: _int_ textures inside, _ext_ outside; else an interior group's face as far as the camera is indoors");
        // The inside colour.
        const float texel[3] = { 0.8f, 0.3f, 0.2f }, dim[3] = { 0.05f, 0.05f, 0.05f }, lit[3] = { 0.9f, 0.9f, 0.9f };
        const float bright[3] = { 2, 2, 2 }, none[3] = { 0, 0, 0 };
        float a[3], b[3], keep[3];
        gl::InsideColour(texel, dim, 1.0f, bright, a);
        gl::InsideColour(texel, dim, 1.5f, bright, b);
        gl::InsideColour(texel, lit, 1.0f, none, keep);
        Check(Near(a[0], 1.6f, 1e-5f) && (b[0] - b[1]) > (a[0] - a[1]) && keep[0] == 0.9f && keep[2] == 0.9f,
              "the inside colour: the texel x the light, more saturated with saturation > 1, never below the client's own (its lit windows)");

        // The floor light: the window in the wall x = 0, the sun from +x at 45 degrees: its patch on the floor z = 0
        // is x in -4.5 .. -1.5, y in -1 .. 1 (the ray toward the sun from (x, y, 0) crosses x = 0 at z = -x).
        const gl::Pane p = WallWindow();
        float sun[3] = { 1, 0, 1 }; Norm(sun);
        auto w = [&](float x, float y, float z) { const float q[3] = { x, y, z }; return gl::FloorWeightWorld(p, sun, q, 30.0f, 6.0f, 4.0f); };
        std::snprintf(line, sizeof(line), "floor light: the patch's middle %.2f, its corners inside %.2f; past its edges %.2f %.2f %.2f %.2f",
                      w(-3, 0, 0), w(-1.7f, 0.8f, 0), w(-1.2f, 0, 0), w(-4.8f, 0, 0), w(-3, 1.3f, 0), w(-3, -1.3f, 0));
        Check(Near(w(-3, 0, 0), 1, 1e-4f) && w(-1.7f, 0.8f, 0) > 0.4f && w(-1.2f, 0, 0) == 0 && w(-4.8f, 0, 0) == 0 && w(-3, 1.3f, 0) == 0 && w(-3, -1.3f, 0) == 0, line);
        // The leak guards: in front of the pane (outside), outside the room's box, beyond the reach, the sun behind.
        float behindSun[3] = { -1, 0, 1 }; Norm(behindSun);
        const float q[3] = { -3, 0, 0 };
        gl::Pane high = p; high.pos[2] = 40; high.roomMax[2] = 60;   // a window 37 yd above the floor: beyond the 30 yd reach
        const float qFar[3] = { -38.5f, 0, 0 };
        std::snprintf(line, sizeof(line), "leak guards: outside the building %.2f, outside the room's box %.2f, beyond the reach %.2f, the sun behind %.2f",
                      w(3, 0, -6), w(-11, 0, 8.5f), gl::FloorWeightWorld(high, sun, qFar, 30, 6, 4), gl::FloorWeightWorld(p, behindSun, q, 30, 6, 4));
        Check(w(3, 0, -6) == 0 && w(-11, 0, 8.5f) == 0 && gl::FloorWeightWorld(high, sun, qFar, 30, 6, 4) == 0 && gl::FloorWeightWorld(p, behindSun, q, 30, 6, 4) == 0, line);
        // The rows about a far origin (the camera) give the same.
        const float origin[3] = { 1234.5f, -987.25f, 55.0f };
        float rows[12];
        gl::ProjectionRows(p, sun, origin, 30, rows);
        const float rel[3] = { -3 - origin[0], 0 - origin[1], 0 - origin[2] };
        const float rmin[3] = { p.roomMin[0] - origin[0], p.roomMin[1] - origin[1], p.roomMin[2] - origin[2] };
        const float rmax[3] = { p.roomMax[0] - origin[0], p.roomMax[1] - origin[1], p.roomMax[2] - origin[2] };
        Check(Near(gl::FloorWeight(rows, rel, rmin, rmax, 6, 4), 1, 1e-3f), "floor light: the same about the camera (camera-relative rows and box)");

        // The fake room.
        float dist = 0;
        const float at[2] = { 0.5f, 1.6f };
        const float straight[3] = { 0, 0, 1 }, down[3] = { 0, -1.2f, 0.4f }, side[3] = { 1.5f, 0, 0.3f }, upward[3] = { 0, 1.5f, 0.3f };
        const gl::RoomFace f1 = gl::RoomHit(at, straight, 3.5f, 3.2f, 4, dist);
        const float d1 = dist;
        const gl::RoomFace f2 = gl::RoomHit(at, down, 3.5f, 3.2f, 4, dist);
        const gl::RoomFace f3 = gl::RoomHit(at, side, 3.5f, 3.2f, 4, dist);
        const gl::RoomFace f4 = gl::RoomHit(at, upward, 3.5f, 3.2f, 4, dist);
        Check(f1 == gl::kBackWall && Near(d1, 4, 1e-4f) && f2 == gl::kFloor && f3 == gl::kSideWall && f4 == gl::kCeiling,
              "the fake room: straight in the back wall (4 yd), down the floor, sideways a side wall, up the ceiling");
    }

    // --- 2. the WMO edit ----------------------------------------------------------------------------------------
    struct Blob { std::string name; std::vector<uint8_t> code; };
    std::vector<Blob> LoadBlobs(const std::filesystem::path& dir)
    {
        std::vector<Blob> out;
        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            if (entry.path().extension() != ".bls") continue;
            std::ifstream f(entry.path(), std::ios::binary);
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            int k = 0;
            for (size_t i = 0; i + 8 <= data.size(); i += 4)
            {
                uint32_t token;
                std::memcpy(&token, data.data() + i, 4);
                if (token != 0xFFFF0300u) continue;
                size_t j = i + 4;
                for (; j + 4 <= data.size(); j += 4)
                {
                    uint32_t t;
                    std::memcpy(&t, data.data() + j, 4);
                    if (t == 0x0000FFFFu) break;
                }
                if (j + 4 > data.size()) break;
                char name[96];
                std::snprintf(name, sizeof(name), "%s_%02d", entry.path().stem().string().c_str(), k++);
                out.push_back({ name, std::vector<uint8_t>(data.begin() + i, data.begin() + j + 4) });
                i = j;
            }
        }
        return out;
    }

    int Slots(const std::vector<uint8_t>& code)
    {
        std::string text, why;
        if (!wxl_livingazeroth::shaderpatch::Disassemble(code.data(), code.size(), text, why)) return -1;
        std::smatch m;
        static const std::regex slots(R"(approximately (\d+) instruction slots)");
        return std::regex_search(text, m, slots) ? std::stoi(m[1].str()) : -1;
    }

    void EditTests(const std::filesystem::path& folder)
    {
        namespace mi = wxl_livingazeroth::modelice;
        namespace sp = wxl_livingazeroth::shaderpatch;
        if (!std::filesystem::exists(folder)) { Check(false, "WMO shader folder " + folder.string()); return; }
        const std::vector<Blob> blobs = LoadBlobs(folder);
        static const std::regex position(R"(dp3 r\d+\.\w, (v\d+), c3\b)"), normal(R"(dp3 r\d+\.\w, c4, (v\d+))");
        int shadow = 0, patched = 0, assembled = 0, withBlocks = 0, maxSlots = 0, maxIceSlots = 0;
        std::string worst;
        std::map<std::string, int> reasons;
        for (const Blob& b : blobs)
        {
            std::string src, why;
            if (!sp::Disassemble(b.code.data(), b.code.size(), src, why)) { reasons["disassemble: " + why]++; continue; }
            const bool isShadow = std::regex_search(src, position) && std::regex_search(src, normal);
            shadow += isShadow ? 1 : 0;
            std::string ice = src;
            std::string glassSrc = src;
            if (!mi::EditPixelWmoGlassForTests(glassSrc, why)) { reasons[std::string(isShadow ? "SHADOW VARIANT skipped: " : "skipped: ") + why]++; continue; }
            ++patched;
            withBlocks += glassSrc.find("if b9") != std::string::npos && glassSrc.find("if b10") != std::string::npos ? 1 : 0;
            std::vector<uint8_t> code;
            if (!sp::Assemble(glassSrc, b.name.c_str(), code, why))
            {
                // The assembler's message names the line: show it.
                std::smatch at;
                static const std::regex where(R"(\((\d+),[\d-]+\): (error [^\r\n]*))");
                std::string shown = why.substr(0, 200);
                if (std::regex_search(why, at, where))
                {
                    int line = std::stoi(at[1].str());
                    size_t pos = 0;
                    for (int l = 1; l < line && pos != std::string::npos; ++l) pos = glassSrc.find('\n', pos + 1);
                    const std::string text = pos == std::string::npos ? "?" : glassSrc.substr(pos + 1, glassSrc.find('\n', pos + 1) - pos - 1);
                    shown = at[2].str() + " at `" + text + "`";
                }
                reasons["assemble: " + shown]++;
                continue;
            }
            ++assembled;
            const int slots = Slots(code);
            if (slots > maxSlots) { maxSlots = slots; worst = b.name; }
            std::vector<uint8_t> iceCode;
            if (mi::EditPixelWmoForTests(ice, why) && sp::Assemble(ice, b.name.c_str(), iceCode, why)) maxIceSlots = std::max(maxIceSlots, Slots(iceCode));
        }
        for (const auto& r : reasons) std::printf("      %3d x %s\n", r.second, r.first.c_str());
        Check(blobs.size() == 112, "stock WMO pixel shaders found: " + std::to_string(blobs.size()) + " (7 sets x 16)");
        Check(patched == shadow && shadow == 84 && withBlocks == patched, "every WMO shadow variant takes the glass and floor blocks: " +
              std::to_string(withBlocks) + " of " + std::to_string(shadow));
        Check(assembled == patched, "every one assembles: " + std::to_string(assembled));
        char line[256];
        std::snprintf(line, sizeof(line), "largest WMO variant with the ice, the rims, the glass and the floor light: %d instruction slots (%s; %d before the glass), "
                      "under kGlassMinSlots %u and the ~3,500 the GTX 1660 SUPER takes", maxSlots, worst.c_str(), maxIceSlots, mi::kGlassMinSlots);
        Check(maxSlots > 0 && maxSlots <= static_cast<int>(mi::kGlassMinSlots) && maxSlots < 3500, line);
    }

    // --- 4. on WARP -----------------------------------------------------------------------------------------------
    template <class T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

    struct Warp
    {
        ID3D12Device* d12 = nullptr;
        IDirect3D9* d3d = nullptr;
        IDirect3DDevice9* dev = nullptr;
        IDirect3DSurface9* target = nullptr;
        IDirect3DSurface9* readback = nullptr;
        IDirect3DTexture9* texel = nullptr;
        ~Warp() { Release(texel); Release(readback); Release(target); Release(dev); Release(d3d); Release(d12); }
    };
    constexpr int kSide = 32;

    bool InitWarp(Warp& w, std::string& why)
    {
        IDXGIFactory4* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory)))) { why = "no DXGI factory"; return false; }
        IDXGIAdapter* adapter = nullptr;
        const HRESULT ha = factory->EnumWarpAdapter(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&adapter));
        factory->Release();
        if (FAILED(ha) || !adapter) { why = "no WARP adapter"; return false; }
        const HRESULT hd = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&w.d12));
        adapter->Release();
        if (FAILED(hd) || !w.d12) { why = "no D3D12 device on WARP"; return false; }
        using Create9On12 = IDirect3D9*(WINAPI*)(UINT, D3D9ON12_ARGS*, UINT);
        HMODULE d9 = LoadLibraryW(L"d3d9.dll");
        auto create = d9 ? reinterpret_cast<Create9On12>(GetProcAddress(d9, "Direct3DCreate9On12")) : nullptr;
        if (!create) { why = "no Direct3DCreate9On12"; return false; }
        D3D9ON12_ARGS a{};
        a.Enable9On12 = TRUE;
        a.pD3D12Device = w.d12;
        w.d3d = create(D3D_SDK_VERSION, &a, 1);
        if (!w.d3d) { why = "D3D9On12 failed"; return false; }
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"GlassTests";
        RegisterClassW(&wc);
        HWND window = CreateWindowW(wc.lpszClassName, L"glass", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window;
        pp.BackBufferWidth = 64; pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        if (FAILED(w.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &w.dev))) { why = "CreateDevice failed"; return false; }
        if (FAILED(w.dev->CreateRenderTarget(kSide, kSide, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &w.target, nullptr)) ||
            FAILED(w.dev->CreateOffscreenPlainSurface(kSide, kSide, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &w.readback, nullptr)))
        { why = "no float target"; return false; }
        if (FAILED(w.dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &w.texel, nullptr))) { why = "no texture"; return false; }
        D3DLOCKED_RECT lr;
        w.texel->LockRect(0, &lr, nullptr, 0);
        *static_cast<uint32_t*>(lr.pBits) = 0xFFCC4D33u;   // (0.8, 0.302, 0.2)
        w.texel->UnlockRect(0);
        return true;
    }

    // The blocks on their own: v0 = the view position, v1 = the normal, v2 = the uv, v3 = the stock colour (r12).
    std::string BlockShader()
    {
        return "ps_3_0\n    def c175, 1, 0, 0.5, 2\n    def c176, 0.2126, 0.7152, 0.0722, 0.2\n" + gl::ShaderDefs() +
               "    dcl_texcoord0 v0.xyz\n    dcl_texcoord1 v1.xyz\n    dcl_texcoord2 v2.xy\n    dcl_texcoord3 v3.xyz\n    dcl_2d s0\n"
               "    mov r12.xyz, v3\n    mov r31.xyz, c175.y" + gl::GlassBlock("v0", "v1", "v2") + gl::FloorBlock("v0", "v1", "v2") +
               "\n    add oC0.xyz, r12, r31\n    mov oC0.w, c175.x\n";
    }

    struct V { float x, y, z, rhw, p[3], n[3], uv[2], base[3]; };

    // Draws the quad (corner values as given, interpolated) with the constants, returns the target's pixels (rgb).
    bool Draw(Warp& w, IDirect3DPixelShader9* ps, const V corners[4], const float* consts, int first, int count, bool glass, bool floor,
              std::vector<float>& rgb)
    {
        IDirect3DDevice9* d = w.dev;
        d->SetRenderTarget(0, w.target);
        D3DVIEWPORT9 vp{ 0, 0, kSide, kSide, 0.0f, 1.0f };
        d->SetViewport(&vp);
        d->SetRenderState(D3DRS_ZENABLE, FALSE); d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE); d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        d->SetVertexShader(nullptr); d->SetPixelShader(ps);
        d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE3(0) | D3DFVF_TEXCOORDSIZE3(1) | D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE3(3));
        d->SetTexture(0, w.texel);
        d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT); d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
        d->SetPixelShaderConstantF(first, consts, count);
        const BOOL b9 = glass ? TRUE : FALSE, b10 = floor ? TRUE : FALSE;
        d->SetPixelShaderConstantB(gl::kSwitch, &b9, 1);
        d->SetPixelShaderConstantB(gl::kFloorSwitch, &b10, 1);
        if (FAILED(d->BeginScene())) return false;
        d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, corners, sizeof(V));
        d->EndScene();
        D3DLOCKED_RECT lr{};
        if (FAILED(d->GetRenderTargetData(w.target, w.readback)) || FAILED(w.readback->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
        rgb.resize(static_cast<size_t>(kSide) * kSide * 3);
        for (int row = 0; row < kSide; ++row)
            for (int col = 0; col < kSide; ++col)
                for (int k = 0; k < 3; ++k)
                    rgb[(static_cast<size_t>(row) * kSide + col) * 3 + k] = reinterpret_cast<const float*>(static_cast<const uint8_t*>(lr.pBits) + row * lr.Pitch)[col * 4 + k];
        w.readback->UnlockRect();
        return true;
    }

    void WarpTests()
    {
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("skip  WARP checks: %s\n", why.c_str()); return; }
        std::vector<uint8_t> code;
        if (!wxl_livingazeroth::shaderpatch::Assemble(BlockShader(), "glass blocks", code, why)) { Check(false, "the blocks on their own assemble: " + why.substr(0, 300)); return; }
        IDirect3DPixelShader9* ps = nullptr;
        if (FAILED(w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps))) { Check(false, "the blocks' shader on WARP"); return; }
        const float e = static_cast<float>(kSide) - 0.5f;
        auto quad = [&](const float p[4][3], const float n[3], const float base[3], V out[4])
        {
            const float xy[4][2] = { { -0.5f, -0.5f }, { e, -0.5f }, { -0.5f, e }, { e, e } };
            for (int c = 0; c < 4; ++c)
            {
                out[c] = { xy[c][0], xy[c][1], 0, 1, { p[c][0], p[c][1], p[c][2] }, { n[0], n[1], n[2] }, { 0.5f, 0.5f }, { base[0], base[1], base[2] } };
            }
        };
        const float texel[3] = { 0.8f, 77.0f / 255.0f, 0.2f };
        // The view's rows as the identity: view space = world axes, the camera at the origin (c180..c183).
        float view[16] = { 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
        w.dev->SetPixelShaderConstantF(gl::kViewConstant, view, 4);

        // Inside: a pane 5 yd ahead (+x), its face toward the eye; the sun beyond it.
        {
            const float p[4][3] = { { 5, 0, 0 }, { 5, 0, 0 }, { 5, 0, 0 }, { 5, 0, 0 } };
            const float n[3] = { -1, 0, 0 }, base[3] = { 0.1f, 0.1f, 0.1f };
            V v[4];
            quad(p, n, base, v);
            float sun[3] = { 1, 0, 0.4f }; Norm(sun);
            float c[4 * gl::kConstantCount] = {};
            c[0] = c[1] = c[2] = 0; c[3] = 1;            // mask: all glass
            c[4] = 1; c[5] = 1; c[6] = 0; c[7] = 0;      // inside, strength 1, no room, no thickness
            c[8] = 0.3f; c[9] = 0.35f; c[10] = 0.4f; c[11] = 0.9f;   // sky through, sun gain
            c[12] = sun[0]; c[13] = sun[1]; c[14] = sun[2]; c[15] = 1.3f;
            c[16] = c[17] = c[18] = 0.5f; c[19] = 1;
            c[20] = 0.5f; c[21] = 0; c[22] = 0.04f; c[23] = 0.2f;
            c[24] = 1 / 3.5f; c[25] = 1 / 3.2f; c[26] = 0.25f; c[27] = 0.3f;
            c[28] = 0.85f; c[29] = 0.72f; c[30] = 0.55f; c[31] = 1.6f;
            c[32] = 1.0f; c[33] = 0.95f; c[34] = 0.85f; c[36] = 1;
            std::vector<float> rgb;
            const bool drew = Draw(w, ps, v, c, gl::kFirstConstant, gl::kConstantCount, true, false, rgb);
            const float out[3] = { 1, 0, 0 };
            const float through = gl::SunThrough(sun, out, 0.2f);
            const float L[3] = { (0.3f + 1.0f * 0.9f * through) * 1.6f, (0.35f + 0.95f * 0.9f * through) * 1.6f, (0.4f + 0.85f * 0.9f * through) * 1.6f };
            float expect[3];
            gl::InsideColour(texel, base, 1.3f, L, expect);
            const size_t mid = (static_cast<size_t>(kSide / 2) * kSide + kSide / 2) * 3;
            char line[256];
            std::snprintf(line, sizeof(line), "WARP, inside: %.3f %.3f %.3f against the rules' %.3f %.3f %.3f", drew ? rgb[mid] : -1, drew ? rgb[mid + 1] : -1,
                          drew ? rgb[mid + 2] : -1, expect[0], expect[1], expect[2]);
            Check(drew && Near(rgb[mid], expect[0], 0.01f) && Near(rgb[mid + 1], expect[1], 0.01f) && Near(rgb[mid + 2], expect[2], 0.01f), line);

            // Outside, face on and at a slant (the sun behind the eye: no glint): (1 - darken) base, then Fresnel to the sky.
            c[4] = 0;
            float tilted[3] = { -1, 3, 0 }; Norm(tilted);   // cos 0.32: Fresnel ~0.18
            for (int pass = 0; pass < 2; ++pass)
            {
                const float* nn = pass ? tilted : n;
                quad(p, nn, base, v);
                const bool ok = Draw(w, ps, v, c, gl::kFirstConstant, gl::kConstantCount, true, false, rgb);
                const float cosine = -nn[0];
                const float f = gl::Fresnel(cosine, 0.04f);
                const float want = (0.1f * 0.5f) * (1 - f) + 0.5f * f;
                std::snprintf(line, sizeof(line), "WARP, outside %s (cos %.2f): %.4f against the rules' %.4f (Fresnel %.3f)", pass ? "at a slant" : "face on", cosine,
                              ok ? rgb[mid] : -1, want, f);
                Check(ok && Near(rgb[mid], want, 0.003f), line);
            }
            // The room behind clear glass changes it, stays finite and in range.
            c[6] = 0.75f;
            quad(p, tilted, base, v);
            std::vector<float> room;
            const bool ok = Draw(w, ps, v, c, gl::kFirstConstant, gl::kConstantCount, true, false, room);
            bool finite = ok;
            for (float x : room) finite = finite && std::isfinite(x) && x >= 0 && x <= 2;
            std::snprintf(line, sizeof(line), "WARP, the fake room behind the glass: %.3f %.3f %.3f (finite, in range, not the plain outside)", ok ? room[mid] : -1,
                          ok ? room[mid + 1] : -1, ok ? room[mid + 2] : -1);
            Check(finite && std::fabs(room[mid] - rgb[mid]) > 0.002f, line);
        }

        // The floor light: a floor (z = 0) from x -6 .. 0, y -2 .. 2 under the wall window, against the rules.
        {
            const gl::Pane pane = WallWindow();
            float sun[3] = { 1, 0, 1 }; Norm(sun);
            const float origin[3] = { 0, 0, 0 };
            float panes[4 * gl::kPaneRegisters * gl::kFloorPanes] = {};
            gl::ProjectionRows(pane, sun, origin, 30, panes);
            for (int k = 0; k < 3; ++k) { panes[12 + k] = pane.light[k]; panes[16 + k] = pane.roomMin[k]; panes[20 + k] = pane.roomMax[k]; }
            panes[15] = 1; panes[19] = 6; panes[23] = 4;
            float c[4 * gl::kConstantCount] = {};
            c[12] = sun[0]; c[13] = sun[1]; c[14] = sun[2];
            c[36] = 1;
            w.dev->SetPixelShaderConstantF(gl::kPaneConstant, panes, gl::kPaneRegisters * gl::kFloorPanes);
            const float p[4][3] = { { -6, -2, 0 }, { 0, -2, 0 }, { -6, 2, 0 }, { 0, 2, 0 } };
            const float n[3] = { 0, 0, 1 }, base[3] = { 0, 0, 0 };
            V v[4];
            quad(p, n, base, v);
            // the quad's x runs along screen x (left -6 .. right 0) and y along screen y
            std::vector<float> rgb;
            const bool drew = Draw(w, ps, v, c, gl::kFirstConstant, gl::kConstantCount, false, true, rgb);
            float worst = 0, peak = 0;
            int lit = 0;
            for (int row = 0; row < kSide && drew; ++row)
                for (int col = 0; col < kSide; ++col)
                {
                    const float x = -6 + (col + 0.5f) / kSide * 6, y = -2 + (row + 0.5f) / kSide * 4;
                    const float q[3] = { x, y, 0 };
                    const float weight = gl::FloorWeightWorld(pane, sun, q, 30, 6, 4);
                    const float want = texel[0] * pane.light[0] * weight * sun[2];
                    const float got = rgb[(static_cast<size_t>(row) * kSide + col) * 3];
                    worst = std::max(worst, std::fabs(got - want));
                    peak = std::max(peak, got);
                    lit += got > 0.05f ? 1 : 0;
                }
            char line[256];
            std::snprintf(line, sizeof(line), "WARP, the floor light over %d x %d floor points: off from the rules by up to %.4f (peak %.3f, %d lit)", kSide, kSide, worst, peak, lit);
            Check(drew && worst < 0.02f && peak > 0.3f && lit > 100 && lit < kSide * kSide / 2, line);
        }
        ps->Release();

    }

    // --- preview (GLASS_PREVIEW=<folder>: for each <name>.rgba there, 256 x 256 RGBA texels of a window texture, a
    // <name>.ppm of it as a tilted 2 x 3 yd pane 4 yd ahead: stock, outside by day, inside by day, outside at night) --
    void Preview(const char* folder)
    {
        Warp w;
        std::string why;
        if (!InitWarp(w, why)) { std::printf("preview: %s\n", why.c_str()); return; }
        const std::string text = "ps_3_0\n    def c175, 1, 0, 0.5, 2\n    def c176, 0.2126, 0.7152, 0.0722, 0.2\n" + gl::ShaderDefs() +
                                 "    dcl_texcoord0 v0.xyz\n    dcl_texcoord1 v1.xyz\n    dcl_texcoord2 v2.xy\n    dcl_texcoord3 v3.xyz\n    dcl_2d s0\n"
                                 "    texld r0, v2, s0\n    mul r12.xyz, r0, v3\n    mov r31.xyz, c175.y" + gl::GlassBlock("v0", "v1", "v2") +
                                 "\n    add oC0.xyz, r12, r31\n    mov oC0.w, c175.x\n";
        std::vector<uint8_t> code;
        if (!wxl_livingazeroth::shaderpatch::Assemble(text, "preview", code, why)) { std::printf("preview: %s\n", why.c_str()); return; }
        IDirect3DPixelShader9* ps = nullptr;
        w.dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps);
        constexpr int kOut = 160;
        IDirect3DSurface9* target = nullptr;
        IDirect3DSurface9* readback = nullptr;
        w.dev->CreateRenderTarget(kOut, kOut, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr);
        w.dev->CreateOffscreenPlainSurface(kOut, kOut, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &readback, nullptr);
        const float view[16] = { 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
        w.dev->SetPixelShaderConstantF(gl::kViewConstant, view, 4);
        for (const auto& entry : std::filesystem::directory_iterator(folder))
        {
            if (entry.path().extension() != ".rgba") continue;
            std::ifstream f(entry.path(), std::ios::binary);
            std::vector<uint8_t> px((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (px.size() != 256 * 256 * 4) continue;
            IDirect3DTexture9* tex = nullptr;
            w.dev->CreateTexture(256, 256, 0, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr);
            D3DLOCKED_RECT lr;
            tex->LockRect(0, &lr, nullptr, 0);
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256; ++x)
                {
                    const uint8_t* s = &px[(static_cast<size_t>(y) * 256 + x) * 4];
                    reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch)[x] = (uint32_t(s[3]) << 24) | (uint32_t(s[0]) << 16) | (uint32_t(s[1]) << 8) | s[2];
                }
            tex->UnlockRect(0);
            const std::string kind = entry.path().stem().string();
            std::vector<uint8_t> image(static_cast<size_t>(kOut) * 4 * kOut * 3);
            for (int pass = 0; pass < 4; ++pass)
            {
                // the pane: x = 4 + 0.6 y (turned ~30 degrees), 2 yd wide, 3 yd tall; its face toward the eye
                float n[3] = { -1, 0.6f, 0 }; Norm(n);
                const float e = kOut - 0.5f;
                const float level = pass == 2 ? 0.35f : (pass == 3 ? 0.9f : 0.75f);   // the client's: dim inside, the lit window at night
                struct PV { float x, y, z, rhw, p[3], nn[3], uv[2], base[3]; };
                const PV v[4] = {
                    { -0.5f, -0.5f, 0, 1, { 4 + 0.6f, 1, 1.5f }, { n[0], n[1], n[2] }, { 0, 0 }, { level, level, level } },
                    { e, -0.5f, 0, 1, { 4 - 0.6f, -1, 1.5f }, { n[0], n[1], n[2] }, { 1, 0 }, { level, level, level } },
                    { -0.5f, e, 0, 1, { 4 + 0.6f, 1, -1.5f }, { n[0], n[1], n[2] }, { 0, 1 }, { level, level, level } },
                    { e, e, 0, 1, { 4 - 0.6f, -1, -1.5f }, { n[0], n[1], n[2] }, { 1, 1 }, { level, level, level } } };
                // The runtime's own constants (glass::BatchConstants) for the texture's row (<name>.mask: the mask plane,
                // kind, flags), at a clear noon (night for the last), the sun in front of the pane (behind it inside).
                gl::Material mat;
                mat.kind = gl::kClear;
                {
                    std::ifstream m(entry.path().parent_path() / (kind + ".mask"));
                    float plane[4];
                    uint32_t k = 0, fl = 0;
                    if (m >> plane[0] >> plane[1] >> plane[2] >> plane[3] >> k >> fl)
                    {
                        for (int i = 0; i < 4; ++i) mat.mask[i] = plane[i];
                        mat.kind = k; mat.flags = fl;
                    }
                }
                gl::Inputs in;
                float sun[3] = { pass == 2 ? 0.6f : -0.5f, 0.2f, 0.75f }; Norm(sun);
                for (int i = 0; i < 3; ++i) in.sun[i] = sun[i];
                in.daylight = pass == 3 ? 0.0f : 1.0f; in.night = pass == 3 ? 1.0f : 0.0f;
                in.sky[0] = 0.55f; in.sky[1] = 0.65f; in.sky[2] = 0.8f;
                const gl::Light light = gl::Evaluate(in);
                float c[4 * gl::kConstantCount] = {};
                gl::BatchConstants(mat, pass == 2 ? 1.0f : 0.0f, light, in.sun, in.sunColour, in.sky, gl::Settings{}, c);
                const BOOL on = pass == 0 ? FALSE : TRUE, off = FALSE;
                w.dev->SetRenderTarget(0, target);
                D3DVIEWPORT9 vp{ 0, 0, kOut, kOut, 0.0f, 1.0f };
                w.dev->SetViewport(&vp);
                w.dev->SetRenderState(D3DRS_ZENABLE, FALSE); w.dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
                w.dev->SetVertexShader(nullptr); w.dev->SetPixelShader(ps);
                w.dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE3(0) | D3DFVF_TEXCOORDSIZE3(1) | D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE3(3));
                w.dev->SetTexture(0, tex);
                w.dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR); w.dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
                w.dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
                w.dev->SetPixelShaderConstantF(gl::kFirstConstant, c, gl::kConstantCount);
                w.dev->SetPixelShaderConstantB(gl::kSwitch, &on, 1);
                w.dev->SetPixelShaderConstantB(gl::kFloorSwitch, &off, 1);
                w.dev->BeginScene();
                w.dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(PV));
                w.dev->EndScene();
                D3DLOCKED_RECT rr{};
                if (FAILED(w.dev->GetRenderTargetData(target, readback)) || FAILED(readback->LockRect(&rr, nullptr, D3DLOCK_READONLY))) continue;
                for (int y = 0; y < kOut; ++y)
                    for (int x = 0; x < kOut; ++x)
                        for (int k = 0; k < 3; ++k)
                        {
                            const float val = reinterpret_cast<const float*>(static_cast<const uint8_t*>(rr.pBits) + y * rr.Pitch)[x * 4 + k];
                            image[(static_cast<size_t>(y) * kOut * 4 + pass * kOut + x) * 3 + k] = static_cast<uint8_t>(std::lround(std::clamp(val, 0.0f, 1.0f) * 255.0f));
                        }
                readback->UnlockRect();
            }
            tex->Release();
            std::ofstream out(entry.path().parent_path() / (kind + ".ppm"), std::ios::binary);
            out << "P6\n" << kOut * 4 << " " << kOut << "\n255\n";
            out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
            std::printf("preview: %s.ppm (stock, outside by day, inside by day, outside at night)\n", kind.c_str());
        }
        readback->Release();
        target->Release();
        ps->Release();
    }

    // --- 5. the shipped tables --------------------------------------------------------------------------------------
    struct Table { uint32_t fields = 0; std::vector<std::vector<uint32_t>> rows; std::vector<char> strings; };
    bool ReadTable(const std::filesystem::path& p, Table& t)
    {
        std::ifstream f(p, std::ios::binary);
        std::vector<char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (d.size() < 20 || std::memcmp(d.data(), "WDBC", 4) != 0) return false;
        uint32_t h[4];
        std::memcpy(h, d.data() + 4, 16);
        t.fields = h[1];
        if (h[2] != h[1] * 4 || d.size() < 20 + static_cast<size_t>(h[0]) * h[2] + h[3]) return false;
        for (uint32_t i = 0; i < h[0]; ++i)
        {
            std::vector<uint32_t> r(h[1]);
            std::memcpy(r.data(), d.data() + 20 + static_cast<size_t>(i) * h[2], h[2]);
            t.rows.push_back(std::move(r));
        }
        t.strings.assign(d.begin() + 20 + static_cast<size_t>(h[0]) * h[2], d.end());
        return true;
    }
    float F(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

    void TableTests(const std::filesystem::path& data)
    {
        Table m, p;
        if (!ReadTable(data / "GlassMaterials.cdbc", m) || !ReadTable(data / "GlassPanes.cdbc", p)) { Check(false, "the shipped glass tables read"); return; }
        std::map<uint32_t, int> kinds;
        bool sane = m.fields == 16;
        std::map<std::string, uint32_t> byName;
        for (const auto& r : m.rows)
        {
            kinds[r[2]]++;
            sane = sane && r[2] >= 1 && r[2] <= 4 && r[1] < m.strings.size();
            if (r[1] < m.strings.size())
            {
                std::string t = &m.strings[r[1]];
                byName[t.substr(t.find_last_of('\\') + 1)] = r[2];
            }
            for (int k = 7; k <= 9; ++k) sane = sane && F(r[k]) >= 0 && F(r[k]) <= 1.0001f;
        }
        char line[256];
        std::snprintf(line, sizeof(line), "GlassMaterials.cdbc: %zu glass textures (clear %d, stained %d, frosted %d, leaded %d), all sane",
                      m.rows.size(), kinds[1], kinds[2], kinds[3], kinds[4]);
        Check(sane && m.rows.size() > 80, line);
        Check(byName["mm_strmwnd_cwindows_02.blp"] == gl::kStained && byName["mm_stained_01.blp"] == gl::kStained &&
              byName["mm_redridge_stained_01.blp"] == gl::kStained && byName["mm_windows_11.blp"] == gl::kStained &&
              byName["mm_windows_16.blp"] == gl::kStained && byName["eb_dal_allianceglass.blp"] == gl::kStained &&
              byName["vrykul_window04.blp"] == gl::kFrosted && byName["mm_elwynn_wnd_ext__01.blp"] == gl::kLeaded &&
              byName.count("mm_strmwnd_wall_01.blp") == 0 && byName.count("jlo_bloodelf_windows_abndn.blp") == 0 &&
              byName.count("stormwindlampglass.blp") == 0,
              "the stained glass the user named is stained; the vrykul's icy panes frosted, Elwynn's leaded; no Stormwind walls, abandoned glass or lamps");
        std::map<uint32_t, int> maps;
        bool unit = p.fields == 26;
        for (const auto& r : p.rows)
        {
            maps[r[1]]++;
            const float o = std::sqrt(F(r[6]) * F(r[6]) + F(r[7]) * F(r[7]) + F(r[8]) * F(r[8]));
            const float a = std::sqrt(F(r[9]) * F(r[9]) + F(r[10]) * F(r[10]) + F(r[11]) * F(r[11]));
            unit = unit && std::fabs(o - 1) < 1e-3f && std::fabs(a - 1) < 1e-3f && F(r[12]) > 0 && F(r[13]) > 0 &&
                   F(r[18]) <= F(r[21]) && F(r[19]) <= F(r[22]) && F(r[20]) <= F(r[23]);
        }
        std::snprintf(line, sizeof(line), "GlassPanes.cdbc: %zu panes (Eastern Kingdoms %d, Kalimdor %d, Outland %d, Northrend %d, other maps %zu), unit facings, boxes sane",
                      p.rows.size(), maps[0], maps[1], maps[530], maps[571], p.rows.size() - maps[0] - maps[1] - maps[530] - maps[571]);
        Check(unit && maps[0] > 1000 && maps[1] > 500 && maps[530] > 500 && maps[571] > 1000, line);
    }
}

int main(int argc, char** argv)
{
    bool warp = true;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--no-warp") == 0) warp = false;
        else args.push_back(argv[i]);
    }
    if (args.size() < 2) { std::printf("usage: glass_tests <folder with the WMO ps_MapObj*.bls> <repo data folder> [--no-warp]\n"); return 2; }
    RulesTests();
    EditTests(args[0]);
    if (warp) WarpTests();
    else std::printf("skip  WARP checks (--no-warp)\n");
    TableTests(args[1]);
    if (const char* preview = std::getenv("GLASS_PREVIEW")) Preview(preview);
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
