// Offline tests for see-through windows (env/SeeThroughModel, as features/SeeThrough uses it). No game.
//  - The guards against the client's exe (its bytes, never a hooked function's first six).
//  - The room behind a window (InteriorBehind): the smallest interior box holding a probe, the flags the walk skips, the
//    deeper probe, a fake window; BuildRoot's windows by group.
//  - The window on the screen: its quad, its rectangle through a camera (in front, behind, across the near plane, off to
//    a side, through a placement's turn), Overlap, the client's CRect order, Facing; PlanWindows (facing, range, budget,
//    fake, off the screen).
//  - The room seen through a window: RoomMesh's first hit (front from inside, back from outside, none; the grid against the
//    brute force on 6,000 random rays), SeesRoom (a window onto its room, a dormer over the ceiling from the street and from
//    above, a window on a blind wall, half over the opening, no room); the walkers' guarded stores are the fields read.
//  - Which windows a glass batch is in (WindowOf, WindowsOfBatch: strays, a bad index), when it may be deferred (a shut
//    window no longer stops it) and its open windows' bits (OpenBits).
//  - The deferred draw: DrawIndexedPrimitive's arguments as the client's device draw works them out, the redraw's c219
//    (ReplayConstants), and the deferral's bookkeeping with stand-in draws (one capture a batch, the range, the blending
//    mode, the cap, each entry its own batch's open windows).
//  - The real data through the real reader: glass::Load over wxl-seyris-tools' Cdbc.cpp on copies of the shipped tables,
//    then the Lion's Pride Inn's windows (GlassWindows.cdbc) against its root's group boxes: 17 windows, every one with the
//    room the map scan's survey found (tools/see_through_survey.py); with the scan's WMO files (optional), the boxes
//    against its root file and its one glass batch against its group's triangles: all 17 windows, no strays; and each room's
//    surfaces from its group file (its glass aside, by GlassMaterials.cdbc), the grid against the brute force on the
//    kitchen, and which of the 17 windows show their room from 30 street viewpoints each (printed; the ground floor's
//    front two head on, the back dormers never).
// Usage:  seethrough_tests <Wow.exe> <repo data folder> [<map scan's wmo-windows folder>]
#include "env/GlassModel.hpp"
#include "env/GlassRooms.hpp"
#include "env/SeeThroughModel.hpp"
#include "features/Glass.hpp"
#include "wxl_seyris/CdbcApi.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace st = wxl_livingazeroth::seethrough;
namespace gl = wxl_livingazeroth::glass;

// wxl-seyris-tools' reader (its src/Cdbc.cpp, linked in), as glass_load_tests links it.
namespace wxl_seyris::cdbc
{
    void*       Load(const WXL_SeyrisCdbcDefinition* def, char* errorBuf, size_t errorBufSize);
    void        Release(void* table);
    uint32_t    RowCount(void* table);
    const void* RowAt(void* table, uint32_t index);
    const void* FindRow(void* table, uint32_t id);
    uint32_t    Value(void* table, const void* row, const char* field, uint32_t element);
    const char* GetString(void* table, const void* row, const char* field);
    size_t      FieldIndex(void* table, const char* field);
    const void* FindRowByField(void* table, const char* field, uint32_t value, const void* after);
    void*       RowAdd(void* table);
    int         RowUpdateValue(void* table, void* row, const char* field, uint32_t value);
    int         RowUpdateString(void* table, void* row, const char* field, const char* value);
    int         RowDelete(void* table, void* row);
    int         Save(void* table, char* errorBuf, size_t errorBufSize);
    void*       Reload(void* table, char* errorBuf, size_t errorBufSize);
}

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at line " + std::to_string(__LINE__)); } while (false)

namespace
{
    unsigned g_passed = 0, g_failed = 0;
    template <class F> void Test(const char* name, F f)
    {
        try
        {
            f();
            ++g_passed;
            std::printf("PASS %s\n", name);
        }
        catch (const std::exception& e)
        {
            ++g_failed;
            std::printf("FAIL %s: %s\n", name, e.what());
        }
    }

    bool Close(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

    std::vector<uint8_t> ReadFile(const std::string& path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("can't open " + path);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }

    // --- the exe ------------------------------------------------------------------------------------------------------
    struct Image
    {
        std::vector<uint8_t> bytes;
        uint32_t base = 0;
        struct Section { uint32_t rva, size, raw, rawSize; };
        std::vector<Section> sections;
        const void* At(uintptr_t va, uint32_t size) const
        {
            if (va < base) return nullptr;
            const uint32_t rva = static_cast<uint32_t>(va - base);
            for (const Section& s : sections)
                if (rva >= s.rva && rva + size <= s.rva + s.rawSize && s.raw + (rva - s.rva) + size <= bytes.size())
                    return bytes.data() + s.raw + (rva - s.rva);
            return nullptr;
        }
    };

    Image LoadImage(const char* path)
    {
        Image im;
        im.bytes = ReadFile(path);
        CHECK(im.bytes.size() > 0x400);
        auto u32 = [&](size_t at) { uint32_t v; CHECK(at + 4 <= im.bytes.size()); std::memcpy(&v, im.bytes.data() + at, 4); return v; };
        auto u16 = [&](size_t at) { return static_cast<uint32_t>(im.bytes.at(at) | (im.bytes.at(at + 1) << 8)); };
        const uint32_t pe = u32(0x3C);
        CHECK(u32(pe) == 0x00004550);
        const uint32_t count = u16(pe + 6), optional = u16(pe + 20);
        im.base = u32(pe + 24 + 28);
        const size_t table = pe + 24 + optional;
        for (uint32_t i = 0; i < count; ++i)
        {
            const size_t s = table + 40 * i;
            im.sections.push_back({ u32(s + 12), u32(s + 8), u32(s + 20), u32(s + 16) });
        }
        return im;
    }

    // --- a camera: row vectors, D3D's left-handed look-at and perspective (z up as the world is) ------------------------
    void Mul(const float a[16], const float b[16], float out[16])
    {
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
            {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += a[r * 4 + k] * b[k * 4 + c];
                out[r * 4 + c] = s;
            }
    }

    // The view-projection of a camera at `eye` looking at `at` (camera-relative: the eye is the origin, as the client's
    // s_viewProj takes positions less s_activeWorldView). The world is right-handed, z up; the view x right, y up, z ahead.
    void Camera(const float eye[3], const float at[3], float fovY, float aspect, float out[16])
    {
        float z[3] = { at[0] - eye[0], at[1] - eye[1], at[2] - eye[2] };
        float l = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
        for (float& v : z) v /= l;
        const float up[3] = { 0, 0, 1 };
        float x[3] = { z[1] * up[2] - z[2] * up[1], z[2] * up[0] - z[0] * up[2], z[0] * up[1] - z[1] * up[0] };
        l = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
        for (float& v : x) v /= l;
        const float y[3] = { x[1] * z[2] - x[2] * z[1], x[2] * z[0] - x[0] * z[2], x[0] * z[1] - x[1] * z[0] };
        const float view[16] = { x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0, 0, 0, 0, 1 };
        const float zn = 0.2f, zf = 1000.0f, ys = 1.0f / std::tan(fovY * 0.5f), xs = ys / aspect;
        const float proj[16] = { xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zf - zn), 1, 0, 0, -zn * zf / (zf - zn), 0 };
        Mul(view, proj, out);
    }

    const float kIdentity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

    st::SeeWindow MakeWindow(float cx, float cy, float cz, float nx, float ny, float nz, float hw, float hh, int interior, uint32_t ordinal = 0)
    {
        st::SeeWindow w;
        w.center[0] = cx; w.center[1] = cy; w.center[2] = cz;
        w.normal[0] = nx; w.normal[1] = ny; w.normal[2] = nz;
        gl::WindowFrame(w.normal, w.along, w.up);
        w.halfWidth = hw; w.halfHeight = hh;
        w.interior = interior;
        w.ordinal = ordinal;
        return w;
    }

    // --- the Lion's Pride Inn (world\wmo\azeroth\buildings\goldshireinn\goldshireinn.wmo): its root's MOGI ------------------
    const char* const kInn = "world\\wmo\\azeroth\\buildings\\goldshireinn\\goldshireinn.wmo";
    const st::GroupInfo kInnGroups[12] = {
        { 0x00002000u, { 14.1116447f, -4.85658407f, 0.395452768f }, { 20.4711647f, -0.377688199f, 4.37204647f } },
        { 0x00002000u, { 9.97781086f, -4.94043112f, 0.395452768f }, { 14.1673288f, 2.82283831f, 6.56520176f } },
        { 0x00002000u, { -4.95740891f, 0.885613203f, 7.25258732f }, { 3.14176393f, 15.5288067f, 16.3215199f } },      // hall
        { 0x00002000u, { -33.0326805f, -8.62736893f, 0.395461589f }, { -17.0094624f, 4.3552146f, 6.68433475f } },     // entry
        { 0x00000008u, { -37.7729988f, -13.5254183f, -0.176483989f }, { 19.9118061f, 18.6811275f, 23.2766132f } },    // upstairs (the shell)
        { 0x00002000u, { -17.069437f, -11.0770741f, -0.214727089f }, { 14.385973f, 15.5403919f, 16.3215218f } },      // kitchen
        { 0x00002000u, { 3.04366994f, 4.89253712f, 7.26631117f }, { 15.7386818f, 15.6087236f, 16.3215218f } },        // exterior02
        { 0x00002000u, { -0.717519581f, -9.92844296f, 7.26631117f }, { 14.1452093f, 4.26601887f, 16.3191967f } },     // commonroom
        { 0x00002000u, { -33.5041504f, -10.7231054f, 7.10701323f }, { -15.2166376f, 1.73665118f, 16.439436f } },      // room01
        { 0x00002000u, { -15.4666376f, -9.92844296f, 7.26518345f }, { -1.27548277f, 3.89281297f, 16.3191967f } },     // room02
        { 0x00002000u, { -36.7471085f, -8.66531277f, -6.76181269f }, { -17.0140305f, 12.6791506f, 0.451028585f } },   // room03
        { 0x00000008u, { 14.3330688f, -6.80315208f, -0.176445588f }, { 21.9426155f, 1.5690881f, 7.83475828f } },      // room04
    };
    // The rooms the map scan's survey found behind its 17 exterior windows (ordinals 1..17), all in group 4.
    const int kInnRooms[17] = { 5, 5, 5, 5, 5, 6, 8, 5, 6, 9, 7, 8, 5, 6, 5, 2, 6 };

    // --- a WMO file's chunks (the map scan's copies) ----------------------------------------------------------------
    struct Chunk { std::string tag; size_t at = 0, size = 0; };
    std::vector<Chunk> Chunks(const std::vector<uint8_t>& b, size_t start, size_t end)
    {
        std::vector<Chunk> out;
        for (size_t at = start; at + 8 <= end;)
        {
            Chunk c;
            c.tag = { static_cast<char>(b[at + 3]), static_cast<char>(b[at + 2]), static_cast<char>(b[at + 1]), static_cast<char>(b[at]) };
            uint32_t size;
            std::memcpy(&size, &b[at + 4], 4);
            c.at = at + 8;
            c.size = size;
            if (c.at + c.size > end) break;
            out.push_back(c);
            at = c.at + c.size;
        }
        return out;
    }

    // A group file's rendered geometry (MOVI, MOVT, MOBA: first index, index count, material).
    struct ScanGroup
    {
        std::vector<uint16_t> indices;
        std::vector<float> vertices;
        struct Moba { uint32_t start; uint16_t count; uint8_t material; };
        std::vector<Moba> batches;
    };
    ScanGroup ReadScanGroup(const std::string& path)
    {
        const std::vector<uint8_t> g = ReadFile(path);
        ScanGroup out;
        for (const Chunk& c : Chunks(g, 0, g.size()))
            if (c.tag == "MOGP")
                for (const Chunk& s : Chunks(g, c.at + 0x44, c.at + c.size))
                {
                    if (s.tag == "MOVI") { out.indices.resize(s.size / 2); std::memcpy(out.indices.data(), &g[s.at], out.indices.size() * 2); }
                    else if (s.tag == "MOVT") { out.vertices.resize(s.size / 4); std::memcpy(out.vertices.data(), &g[s.at], out.vertices.size() * 4); }
                    else if (s.tag == "MOBA")
                        for (size_t at = s.at; at + 24 <= s.at + s.size; at += 24)
                        {
                            ScanGroup::Moba m;
                            std::memcpy(&m.start, &g[at + 12], 4);
                            std::memcpy(&m.count, &g[at + 16], 2);
                            m.material = g[at + 23];
                            out.batches.push_back(m);
                        }
                }
        return out;
    }
    // A root file's materials' first textures (MOTX, MOMT: +0x0C the texture's offset).
    std::vector<std::string> ReadRootTextures(const std::string& path)
    {
        const std::vector<uint8_t> r = ReadFile(path);
        std::string motx;
        std::vector<std::string> out;
        std::vector<uint32_t> offsets;
        for (const Chunk& c : Chunks(r, 0, r.size()))
        {
            if (c.tag == "MOTX") motx.assign(reinterpret_cast<const char*>(&r[c.at]), c.size);
            else if (c.tag == "MOMT")
                for (size_t at = c.at; at + 64 <= c.at + c.size; at += 64)
                {
                    uint32_t o;
                    std::memcpy(&o, &r[at + 12], 4);
                    offsets.push_back(o);
                }
        }
        for (const uint32_t o : offsets) out.push_back(o < motx.size() ? std::string(motx.c_str() + o) : std::string());
        return out;
    }
    // The room's surfaces as the runtime takes them: its batches but the glass ones (`glass`: per material).
    bool BuildMesh(const ScanGroup& g, const std::vector<bool>& glass, st::RoomMesh& mesh)
    {
        std::vector<std::pair<uint32_t, uint32_t>> ranges;
        for (const ScanGroup::Moba& m : g.batches)
            if (!(m.material < glass.size() && glass[m.material])) ranges.emplace_back(m.start, m.count);
        return mesh.Build(g.vertices.data(), static_cast<uint32_t>(g.vertices.size() / 3), g.indices.data(), static_cast<uint32_t>(g.indices.size()), ranges);
    }

    // A quad (corners in order round it) as two triangles facing `n` (their winding's front: cross(b - a, c - a) . n > 0).
    void AddQuad(std::vector<float>& v, std::vector<uint16_t>& idx, const float (&q)[4][3], const float n[3])
    {
        const float e1[3] = { q[1][0] - q[0][0], q[1][1] - q[0][1], q[1][2] - q[0][2] };
        const float e2[3] = { q[2][0] - q[0][0], q[2][1] - q[0][1], q[2][2] - q[0][2] };
        const float c[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
        const bool flip = c[0] * n[0] + c[1] * n[1] + c[2] * n[2] < 0.0f;
        const uint16_t base = static_cast<uint16_t>(v.size() / 3);
        for (int k = 0; k < 4; ++k)
            for (int i = 0; i < 3; ++i) v.push_back(q[flip ? 3 - k : k][i]);
        for (const uint16_t i : { 0, 1, 2, 0, 2, 3 }) idx.push_back(static_cast<uint16_t>(base + i));
    }
    // An axis-aligned rectangle on the plane `axis` = `at`, from (a0, b0) to (a1, b1) on the other two axes in order.
    void AddRect(std::vector<float>& v, std::vector<uint16_t>& idx, int axis, float at, float a0, float a1, float b0, float b1, float facing)
    {
        const int a = (axis + 1) % 3, b = (axis + 2) % 3;
        float q[4][3] = {};
        const float as[4] = { a0, a1, a1, a0 }, bs[4] = { b0, b0, b1, b1 };
        for (int k = 0; k < 4; ++k) { q[k][axis] = at; q[k][a] = as[k]; q[k][b] = bs[k]; }
        float n[3] = { 0, 0, 0 };
        n[axis] = facing;
        AddQuad(v, idx, q, n);
    }
    // A room x -5..5, y 0..8, z 0..4, its faces facing in, a hole x -1..1, z 1..3 in its front wall (y 0).
    st::RoomMesh TestRoom()
    {
        std::vector<float> v;
        std::vector<uint16_t> idx;
        AddRect(v, idx, 2, 0, -5, 5, 0, 8, 1);     // floor (z = 0: a = x, b = y)
        AddRect(v, idx, 2, 4, -5, 5, 0, 8, -1);    // ceiling
        AddRect(v, idx, 1, 8, 0, 4, -5, 5, -1);    // back wall (y = 8: a = z, b = x)
        AddRect(v, idx, 0, -5, 0, 8, 0, 4, 1);     // side walls (x: a = y, b = z)
        AddRect(v, idx, 0, 5, 0, 8, 0, 4, -1);
        AddRect(v, idx, 1, 0, 0, 4, -5, -1, 1);    // the front wall round the hole
        AddRect(v, idx, 1, 0, 0, 4, 1, 5, 1);
        AddRect(v, idx, 1, 0, 0, 1, -1, 1, 1);
        AddRect(v, idx, 1, 0, 3, 4, -1, 1, 1);
        st::RoomMesh m;
        CHECK(m.Build(v.data(), static_cast<uint32_t>(v.size() / 3), idx.data(), static_cast<uint32_t>(idx.size()), { { 0u, static_cast<uint32_t>(idx.size()) } }));
        return m;
    }

    // --- the linked reader ----------------------------------------------------------------------------------------------
    int __cdecl LinkedHasFeature(const char* name)
    {
        return name && (std::strcmp(name, "cdbc-load") == 0 || std::strcmp(name, "cdbc-find-by-field") == 0 || std::strcmp(name, "cdbc-write") == 0);
    }

    WXL_SeyrisCdbcApi LinkedApi()
    {
        WXL_SeyrisCdbcApi api{};
        api.structSize = sizeof(WXL_SeyrisCdbcApi);
        api.major = 1; api.minor = 3; api.patch = 0;
        api.HasFeature = &LinkedHasFeature;
        api.Load = &wxl_seyris::cdbc::Load;
        api.Release = &wxl_seyris::cdbc::Release;
        api.RowCount = &wxl_seyris::cdbc::RowCount;
        api.RowAt = &wxl_seyris::cdbc::RowAt;
        api.FindRow = &wxl_seyris::cdbc::FindRow;
        api.Value = &wxl_seyris::cdbc::Value;
        api.GetString = &wxl_seyris::cdbc::GetString;
        api.FieldIndex = &wxl_seyris::cdbc::FieldIndex;
        api.FindRowByField = &wxl_seyris::cdbc::FindRowByField;
        api.RowAdd = &wxl_seyris::cdbc::RowAdd;
        api.RowUpdateValue = &wxl_seyris::cdbc::RowUpdateValue;
        api.RowUpdateString = &wxl_seyris::cdbc::RowUpdateString;
        api.RowDelete = &wxl_seyris::cdbc::RowDelete;
        api.Save = &wxl_seyris::cdbc::Save;
        api.Reload = &wxl_seyris::cdbc::Reload;
        return api;
    }
}

int main(int argc, char** argv)
{
    const char* exe = argc > 1 ? argv[1] : nullptr;
    const std::string data = argc > 2 ? argv[2] : "";
    const std::string scan = argc > 3 ? argv[3] : "";

    // --- the guards ------------------------------------------------------------------------------------------------------
    if (exe)
        Test("the guards hold against the client's exe, none in a hooked function's first bytes", [&] {
            const Image im = LoadImage(exe);
            const st::Guard* bad = st::FirstMismatch(st::kGuards, [&](uintptr_t va, uint32_t size) { return im.At(va, size); });
            if (bad) throw std::runtime_error(std::string("differs: ") + bad->what);
            // Wmo.RenderThroughPortals, Gx.DeviceDraw, M2.SceneDraw, Wmo.CullBatch, Wmo.RenderGroup, Wmo.ExtRender, Wmo.IntRender:
            // a detour may hold their first bytes.
            for (const uintptr_t hooked : { 0x007AC060u, 0x006A3620u, 0x00823CB0u, 0x007A7630u, 0x007ABF50u, 0x007AC6A0u, 0x007AC9F0u })
                for (const st::Guard& g : st::kGuards)
                    CHECK(!(g.address < hooked + 6 && g.address + g.size > hooked));
            // Nor a call's target that Modern Water redirects (wxl-modern-water engine.h, the *Site constants): with
            // those calls pointed elsewhere, as in the game, the guards still hold.
            const uintptr_t redirected[] = { 0x004FB03Du, 0x004F911Du, 0x004F9170u, 0x007E5818u, 0x004F9281u,
                                             0x0081FD15u, 0x004F9213u, 0x00790AA2u, 0x00780810u, 0x00781444u };
            for (const uintptr_t site : redirected)
                for (const st::Guard& g : st::kGuards)
                    CHECK(!(g.address < site + 5 && g.address + g.size > site + 1));
            std::vector<uint8_t> patched;
            const st::Guard* badPatched = st::FirstMismatch(st::kGuards, [&](uintptr_t va, uint32_t size) -> const void* {
                const auto* at = static_cast<const uint8_t*>(im.At(va, size));
                if (!at) return nullptr;
                patched.assign(at, at + size);
                for (const uintptr_t site : redirected)
                    for (uintptr_t b = site + 1; b < site + 5; ++b)
                        if (b >= va && b < va + size) patched[b - va] ^= 0x5A;
                return patched.data();
            });
            if (badPatched) throw std::runtime_error(std::string("differs with Modern Water's calls redirected: ") + badPatched->what);
            // The statics the guards name are the ones the runtime reads.
            uint32_t v = 0;
            std::memcpy(&v, static_cast<const uint8_t*>(im.At(0x007AC0B4, 11)) + 2, 4);
            CHECK(v == st::client::kInteriorPass);
            std::memcpy(&v, static_cast<const uint8_t*>(im.At(0x007AC0D9, 19)) + 1, 4);
            CHECK(v == st::client::kRenderCallback);
            std::memcpy(&v, static_cast<const uint8_t*>(im.At(0x007AC0D9, 19)) + 11, 4);
            CHECK(v == st::client::kRenderParam);
            std::memcpy(&v, static_cast<const uint8_t*>(im.At(0x007AC245, 6)) + 2, 4);
            CHECK(v == st::client::kCamPosLocal);
            std::memcpy(&v, static_cast<const uint8_t*>(im.At(0x007A8645, 5)) + 1, 4);
            CHECK(v == st::client::kMapObjToWorld);
            std::memcpy(&v, static_cast<const uint8_t*>(im.At(0x007A873C, 11)) + 2, 4);
            CHECK(v == st::client::kActiveWorldView);
            std::memcpy(&v, static_cast<const uint8_t*>(im.At(0x007A873C, 11)) + 7, 4);
            CHECK(v == st::client::kViewProj);
            std::memcpy(&v, static_cast<const uint8_t*>(im.At(0x007AC45D, 19)) + 10, 4);
            CHECK(v == st::client::kFrustumCorners);
            // The calls go where the constants say.
            int32_t rel = 0;
            std::memcpy(&rel, static_cast<const uint8_t*>(im.At(0x007AC45D, 19)) + 1, 4);
            CHECK(static_cast<uintptr_t>(0x007AC45D + 5 + rel) == st::client::kFrustumPush);
            std::memcpy(&rel, static_cast<const uint8_t*>(im.At(0x007AC45D, 19)) + 15, 4);
            CHECK(static_cast<uintptr_t>(0x007AC46B + 5 + rel) == st::client::kFrustumSet);
            std::memcpy(&rel, static_cast<const uint8_t*>(im.At(0x007AC48D, 10)) + 1, 4);
            CHECK(static_cast<uintptr_t>(0x007AC48D + 5 + rel) == st::client::kRenderThruPortals);
            std::memcpy(&rel, static_cast<const uint8_t*>(im.At(0x007AC48D, 10)) + 6, 4);
            CHECK(static_cast<uintptr_t>(0x007AC492 + 5 + rel) == st::client::kFrustumPop);
            std::memcpy(&rel, static_cast<const uint8_t*>(im.At(0x004F9113, 15)) + 11, 4);
            CHECK(static_cast<uintptr_t>(0x004F911D + 5 + rel) == 0x00823CB0u);
            std::memcpy(&v, static_cast<const uint8_t*>(im.At(0x004F90EC, 6)) + 2, 4);
            CHECK(v == st::client::kM2Scene);
            // The walkers' stores are the fields the room's surfaces are read from (and their counts' divisors the strides).
            auto field = [&](uintptr_t va, uint32_t at) { uint32_t f = 0; std::memcpy(&f, static_cast<const uint8_t*>(im.At(va, at + 4)) + at, 4); return f; };
            CHECK(field(0x007D7F76, 2) == st::client::kGroupIndices && field(0x007D7F76, 13) == st::client::kGroupIndexCount);
            CHECK(field(0x007D7F94, 2) == st::client::kGroupVertices && field(0x007D7F94, 14) == st::client::kGroupVertexCount);
            CHECK(field(0x007D7FEC, 2) == st::client::kGroupBatches && field(0x007D7FEC, 14) == st::client::kGroupBatchCount);
            CHECK(field(0x007D7499, 2) == st::client::kRootMotx && field(0x007D7499, 11) == st::client::kRootMotxSize);
            CHECK(field(0x007D74B0, 2) == st::client::kRootMaterials && field(0x007D74B0, 14) == st::client::kRootMaterialCount);
            // (MOBA's count: x 0xAAAAAAAB >> 32, >> 4: / 24; MOMT's: >> 6: / 64)
            CHECK(static_cast<const uint8_t*>(im.At(0x007D7FE2, 1))[1] == 0xAB && static_cast<const uint8_t*>(im.At(0x007D7FF5, 3))[2] == 4 &&
                  st::client::kMobaStride == 24);
            CHECK(static_cast<const uint8_t*>(im.At(0x007D74B9, 3))[2] == 6 && st::client::kMomtStride == 64);
            std::printf("  %zu guards\n", sizeof(st::kGuards) / sizeof(st::kGuards[0]));
        });
    else std::printf("SKIP guards (no Wow.exe given)\n");

    // --- the room behind a window ----------------------------------------------------------------------------------
    Test("InteriorBehind: the smallest interior box holding a probe; the walk's skipped groups; the deeper probe; none", [] {
        const st::GroupInfo groups[] = {
            { 0x8, { -20, -20, -1 }, { 20, 20, 20 } },        // 0 the exterior shell: never
            { 0x2000, { -10, -10, 0 }, { 10, 10, 10 } },      // 1 the hall
            { 0x2000, { -10, -10, 0 }, { 0, 0, 5 } },         // 2 a room in it: smaller
            { 0x12000, { -10, -10, 0 }, { -5, -5, 3 } },      // 3 smaller still, but 0x10000: the walk never enters
            { 0x2000, { 30, 0, 0 }, { 40, 10, 10 } },         // 4 far away
        };
        const float n[3] = { 0, -1, 0 };
        const float a[3] = { -4, -11, 2 };       // 1 yd in: (-4, -10, 2), in 1 and 2 (margin); 2 is smaller
        CHECK(st::InteriorBehind(a, n, groups, 5) == 2);
        const float b[3] = { 5, -11, 7 };        // in 1 only
        CHECK(st::InteriorBehind(b, n, groups, 5) == 1);
        const float c[3] = { 5, -12.0f, 7 };     // 1 yd in: y -11 (outside 1's margin, -10.25); 2 yd in: -10: in 1
        CHECK(st::InteriorBehind(c, n, groups, 5) == 1);
        const float d[3] = { 5, -14.0f, 7 };     // both probes outside every interior box: a fake window
        CHECK(st::InteriorBehind(d, n, groups, 5) == -1);
        const float nan[3] = { NAN, 0, 0 };
        CHECK(st::InteriorBehind(nan, n, groups, 5) == -1);
        CHECK(st::InteriorBehind(a, n, nullptr, 5) == -1);
    });

    Test("BuildRoot: the WMO's exterior windows by group then ordinal, each with its room; the rest left out", [] {
        const uint32_t path = gl::PathHash("world\\wmo\\test\\house.wmo"), other = gl::PathHash("world\\wmo\\test\\other.wmo");
        const st::GroupInfo groups[] = {
            { 0x8, { -20, -20, -1 }, { 20, 20, 20 } },
            { 0x2000, { -10, -10, 0 }, { 10, 10, 10 } },
            { 0x8, { 20, -20, -1 }, { 40, 20, 20 } },
        };
        std::vector<gl::Window> all;
        auto add = [&](uint32_t p, uint32_t group, uint32_t ordinal, uint32_t flags, float x, float y, float nx, float ny) {
            gl::Window w;
            w.path = p; w.group = group; w.ordinal = ordinal; w.flags = flags; w.kind = gl::kLeaded;
            w.center[0] = x; w.center[1] = y; w.center[2] = 3;
            w.normal[0] = nx; w.normal[1] = ny; w.normal[2] = 0;
            w.halfWidth = 1; w.halfHeight = 1;
            all.push_back(w);
        };
        add(path, 2, 7, 1, 40, 0, 1, 0);        // group 2: fake (nothing interior behind x = 39)
        add(path, 0, 3, 1, 0, -10.5f, 0, -1);   // group 0: the hall behind
        add(path, 0, 1, 1, 5, -10.5f, 0, -1);
        add(path, 1, 9, 0, 0, -9.5f, 0, -1);    // the hall's own glass (an interior group's): not from outside
        add(other, 0, 0, 1, 0, -10.5f, 0, -1);  // another WMO
        add(path, 5, 2, 1, 0, 0, 0, -1);        // a group the root hasn't
        const st::RootWindows r = st::BuildRoot(path, all, groups, 3);
        CHECK(r.windows.size() == 3 && r.real == 2 && r.fake == 1);
        uint32_t n = 0;
        const st::SeeWindow* g0 = r.Group(0, n);
        CHECK(g0 && n == 2 && g0[0].ordinal == 1 && g0[1].ordinal == 3 && g0[0].interior == 1 && g0[1].interior == 1);
        CHECK(Close(g0[0].along[0], 1.0f, 1e-6f) && Close(g0[0].up[2], 1.0f, 1e-6f));   // n = -y: along +x, up +z
        const st::SeeWindow* g2 = r.Group(2, n);
        CHECK(g2 && n == 1 && g2->interior == -1);
        CHECK(!r.Group(1, n) && n == 0);
        CHECK(!r.Group(7, n));
    });

    // --- the window on the screen ---------------------------------------------------------------------------------------
    Test("Corners, ScreenRect: a window ahead is a rectangle about the middle; behind the eye, off to a side: none", [] {
        const float eye[3] = { 0, -20, 2 }, at[3] = { 0, 0, 2 };
        float vp[16];
        Camera(eye, at, 1.0f, 16.0f / 9.0f, vp);
        const st::SeeWindow w = MakeWindow(0, 0, 2, 0, -1, 0, 1.0f, 1.0f, 1);
        float q[4][3];
        st::Corners(w, 0.0f, q);
        CHECK(Close(q[0][0], -1, 1e-6f) && Close(q[0][2], 1, 1e-6f) && Close(q[2][0], 1, 1e-6f) && Close(q[2][2], 3, 1e-6f));
        st::Rect r;
        CHECK(st::ScreenRect(q, 4, kIdentity, eye, vp, r));
        CHECK(Close(r.minX, -r.maxX, 1e-4f) && Close(r.minY, -r.maxY, 1e-4f) && r.maxX > 0.01f && r.maxX < 0.2f);
        // 2 yd wide at 20 yd, tan(0.5) x 16/9 across: 1 / (20 x 0.5463 x 1.778) = 0.0515 of the half screen
        CHECK(Close(r.maxX, 1.0f / (20.0f * std::tan(0.5f) * 16.0f / 9.0f), 1e-3f));
        // behind the eye
        const st::SeeWindow back = MakeWindow(0, -40, 2, 0, -1, 0, 1, 1, 1);
        st::Corners(back, 0.0f, q);
        CHECK(!st::ScreenRect(q, 4, kIdentity, eye, vp, r));
        // off to the side
        const st::SeeWindow side = MakeWindow(100, 0, 2, 0, -1, 0, 1, 1, 1);
        st::Corners(side, 0.0f, q);
        CHECK(!st::ScreenRect(q, 4, kIdentity, eye, vp, r));
    });

    Test("ScreenRect: a window across the near plane and the eye is clipped to the screen, not lost", [] {
        const float eye[3] = { 0, -1, 2 }, at[3] = { 0, 0, 2 };
        float vp[16];
        Camera(eye, at, 1.0f, 1.0f, vp);
        // a long window just to the right, along the view, reaching from behind the eye to 3.5 yd ahead
        const st::SeeWindow w = MakeWindow(0.5f, -0.5f, 2, 1, 0, 0, 3, 1, 1);   // in the plane x = 0.5
        float q[4][3];
        st::Corners(w, 0.0f, q);
        st::Rect r;
        CHECK(st::ScreenRect(q, 4, kIdentity, eye, vp, r));
        CHECK(r.minX >= -1.0f && r.maxX <= 1.0f && r.minY >= -1.0f && r.maxY <= 1.0f && r.maxY - r.minY > 0.5f);
        CHECK(r.minX > 0.0f && Close(r.maxX, 1.0f, 1e-5f));   // on the right, to the screen's edge
    });

    Test("ScreenRect goes through the placement: a turned and moved WMO's window lands where the world one does", [] {
        const float eye[3] = { 100, 180, 12 }, at[3] = { 100, 200, 12 };
        float vp[16];
        Camera(eye, at, 1.0f, 1.5f, vp);
        // the WMO turned 90 degrees about z (its x is the world's y) and placed at (100, 200, 10): row vectors
        const float model[16] = { 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 100, 200, 10, 1 };
        // its window at local (-10, 0, 2), normal local (-1, 0, 0): in the world at (100, 190, 12), normal (0, -1, 0)
        const st::SeeWindow local = MakeWindow(-10, 0, 2, -1, 0, 0, 1, 1, 1);
        const st::SeeWindow world = MakeWindow(100, 190, 12, 0, -1, 0, 1, 1, 1);
        float q[4][3];
        st::Rect a, b;
        st::Corners(local, 0.0f, q);
        CHECK(st::ScreenRect(q, 4, model, eye, vp, a));
        st::Corners(world, 0.0f, q);
        CHECK(st::ScreenRect(q, 4, kIdentity, eye, vp, b));
        CHECK(Close(a.minX, b.minX, 1e-4f) && Close(a.maxX, b.maxX, 1e-4f) && Close(a.minY, b.minY, 1e-4f) && Close(a.maxY, b.maxY, 1e-4f));
    });

    Test("Overlap, the client's CRect order {minY, minX, maxY, maxX}, the frustum rectangle (clip + 1) / 2, Facing", [] {
        st::Rect r;
        CHECK(st::Overlap({ -1, -1, 1, 1 }, { -0.5f, 0.2f, 2, 0.4f }, r) && r.minX == -0.5f && r.minY == 0.2f && r.maxX == 1 && r.maxY == 0.4f);
        CHECK(!st::Overlap({ -1, -1, 0, 0 }, { 0.5f, 0.5f, 1, 1 }, r));
        CHECK(!st::Overlap({ -1, -1, 1, 1 }, { 0.2f, 0.2f, 0.2005f, 0.5f }, r));   // thinner than 0.001
        const st::Rect clip{ -0.5f, -0.25f, 0.5f, 0.75f };
        float c[4], f[4];
        st::ToCRect(clip, c);
        CHECK(c[0] == -0.25f && c[1] == -0.5f && c[2] == 0.75f && c[3] == 0.5f);
        const st::Rect back = st::FromCRect(c);
        CHECK(back.minX == clip.minX && back.minY == clip.minY && back.maxX == clip.maxX && back.maxY == clip.maxY);
        st::FrustumRect(clip, f);
        CHECK(f[0] == 0.375f && f[1] == 0.25f && f[2] == 0.875f && f[3] == 0.75f);
        const st::SeeWindow w = MakeWindow(0, 0, 0, 0, -1, 0, 1, 1, 1);
        const float out[3] = { 0, -3, 0 }, in[3] = { 0, 3, 0 }, on[3] = { 0, -0.01f, 0 };
        CHECK(st::Facing(w, out) && !st::Facing(w, in) && !st::Facing(w, on));
        const float lo[3] = { 0, 0, 0 }, hi[3] = { 1, 1, 1 }, p[3] = { 4, 5, 0.5f }, inside[3] = { 0.5f, 0.5f, 0.5f };
        CHECK(Close(st::DistanceToBox(p, lo, hi), 5.0f, 1e-5f) && st::DistanceToBox(inside, lo, hi) == 0.0f);
    });

    Test("PlanWindows: facing and on the screen with a room in range opens; fake, far or over budget stays shut; the rest unseen", [] {
        const float cam[3] = { 0, -30, 5 }, at[3] = { 0, 0, 5 };
        float vp[16];
        Camera(cam, at, 1.0f, 16.0f / 9.0f, vp);
        std::vector<st::SeeWindow> w = {
            MakeWindow(-5, -10, 3, 0, -1, 0, 1.5f, 1.2f, 5, 1),    // 0 south, room 5: opens
            MakeWindow(5, -10, 3, 0, -1, 0, 1.5f, 1.2f, -1, 2),    // 1 south, fake: shut
            MakeWindow(0, 10, 3, 0, 1, 0, 1.5f, 1.2f, 5, 3),       // 2 north (facing away): unseen
            MakeWindow(14, 0, 3, 1, 0, 0, 1.5f, 1.2f, 5, 4),       // 3 east, the camera west of its plane: unseen
            MakeWindow(-200, -10, 3, 0, -1, 0, 1.5f, 1.2f, 5, 5),  // 4 south but far off the screen: unseen
            MakeWindow(0, -10, 8, 0, -1, 0, 1.5f, 1.2f, 6, 6),     // 5 south, room 6: opens
        };
        const float lo[3] = { -20, -10, 0 }, hi[3] = { 20, 10, 15 };
        const st::Rect full{ -1, -1, 1, 1 };
        std::vector<st::WindowPlan> plan;
        unsigned budget = 10;
        st::PlanWindows(w.data(), static_cast<uint32_t>(w.size()), cam, lo, hi, full, kIdentity, cam, vp, budget, plan);
        CHECK(plan.size() == 6 && budget == 8);
        CHECK(plan[0].candidate && plan[0].state == st::kShut && plan[0].clip.maxX < 0.0f);   // left of the middle
        CHECK(!plan[1].candidate && plan[1].state == st::kShut);
        CHECK(plan[2].state == st::kUnseen && plan[3].state == st::kUnseen && plan[4].state == st::kUnseen);
        CHECK(plan[5].candidate);
        // the budget: one left, the first candidate takes it
        budget = 1;
        st::PlanWindows(w.data(), static_cast<uint32_t>(w.size()), cam, lo, hi, full, kIdentity, cam, vp, budget, plan);
        CHECK(budget == 0 && plan[0].candidate && !plan[5].candidate && plan[5].state == st::kShut);
        // out of range: on the screen and facing, shut
        const float distant[3] = { 0, -10 - st::kRange - 5, 5 }, distantAt[3] = { 0, 0, 5 };
        Camera(distant, distantAt, 1.0f, 16.0f / 9.0f, vp);
        budget = 10;
        st::PlanWindows(w.data(), static_cast<uint32_t>(w.size()), distant, lo, hi, full, kIdentity, distant, vp, budget, plan);
        CHECK(budget == 10 && plan[0].state == st::kShut && !plan[0].candidate && plan[5].state == st::kShut);
        // the walk's own rectangle (a doorway's): only what overlaps it
        Camera(cam, at, 1.0f, 16.0f / 9.0f, vp);
        const st::Rect rightHalf{ 0.0f, -1, 1, 1 };
        st::PlanWindows(w.data(), static_cast<uint32_t>(w.size()), cam, lo, hi, rightHalf, kIdentity, cam, vp, budget, plan);
        CHECK(plan[0].state == st::kUnseen && plan[5].candidate && plan[5].clip.minX >= 0.0f);
    });

    // --- the room seen through a window ---------------------------------------------------------------------------------
    Test("RoomMesh: a room's faces from inside are front, from outside back; the grid's first hit is every triangle's", [] {
        const st::RoomMesh room = TestRoom();
        CHECK(room.Triangles() == 18);
        float t = 0;
        const float in[3] = { 0, 4, 2 }, up[3] = { 0, 0, 1 }, down[3] = { 0, 0, -1 }, back[3] = { 0, 1, 0 };
        CHECK(room.First(in, up, 0, 100, &t) == st::RoomMesh::kFront && Close(t, 2, 1e-5f));     // the ceiling from below
        CHECK(room.First(in, back, 0, 100, &t) == st::RoomMesh::kFront && Close(t, 4, 1e-5f));   // the back wall
        const float above[3] = { 0, 4, 10 };
        CHECK(room.First(above, down, 0, 100, &t) == st::RoomMesh::kBack && Close(t, 6, 1e-5f)); // the ceiling from above
        CHECK(room.First(above, up, 0, 100) == st::RoomMesh::kNone);
        CHECK(room.First(in, up, 0, 1.5f) == st::RoomMesh::kNone);                               // tmax short of it
        CHECK(room.First(above, down, 6.5f, 100, &t) == st::RoomMesh::kFront && Close(t, 10, 1e-5f));   // past the ceiling: the floor
        // random rays through the room and a soup of triangles: the grid's answer is the brute force's
        uint32_t seed = 12345;
        auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
        std::vector<float> v;
        std::vector<uint16_t> idx;
        for (int i = 0; i < 1500; ++i)
        {
            const float c[3] = { rnd() * 60 - 30, rnd() * 60 - 30, rnd() * 20 };
            for (int k = 0; k < 3; ++k)
                for (int j = 0; j < 3; ++j) v.push_back(c[j] + (rnd() - 0.5f) * (k == 0 ? 0.0f : 6.0f));
            for (int k = 0; k < 3; ++k) idx.push_back(static_cast<uint16_t>(3 * i + k));
        }
        st::RoomMesh soup;
        CHECK(soup.Build(v.data(), static_cast<uint32_t>(v.size() / 3), idx.data(), static_cast<uint32_t>(idx.size()), { { 0u, static_cast<uint32_t>(idx.size()) } }));
        int agree = 0, hits = 0, n = 0;
        for (const st::RoomMesh* m : { &room, static_cast<const st::RoomMesh*>(&soup) })
            for (int i = 0; i < 3000; ++i, ++n)
            {
                float o[3] = { rnd() * 80 - 40, rnd() * 80 - 40, rnd() * 30 - 5 }, d[3] = { rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f };
                const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                for (float& x : d) x /= l;
                float ta = -1, tb = -1;
                const auto a = m->First(o, d, 0.0f, 200.0f, &ta), b = m->FirstBrute(o, d, 0.0f, 200.0f, &tb);
                agree += a == b && (a == st::RoomMesh::kNone || Close(ta, tb, 1e-4f)) ? 1 : 0;
                hits += a != st::RoomMesh::kNone ? 1 : 0;
            }
        std::printf("  %d of %d random rays as the brute force's (%d meet a triangle)\n", agree, n, hits);
        CHECK(agree == n && hits > n / 10);
        // bad input: an index past the vertices, a range past the indices, not whole triangles
        st::RoomMesh bad;
        const uint16_t wrong[3] = { 0, 1, 99 };
        CHECK(!bad.Build(v.data(), 3, wrong, 3, { { 0u, 3u } }) && bad.Empty());
        CHECK(!bad.Build(v.data(), 3, idx.data(), 3, { { 0u, 6u } }) && !bad.Build(v.data(), 3, idx.data(), 3, { { 0u, 2u } }));
        CHECK(bad.Build(v.data(), 3, idx.data(), 3, {}) && bad.Empty() && bad.First(in, up, 0, 10) == st::RoomMesh::kNone);
    });

    Test("SeesRoom: a window onto its room opens; a dormer over the ceiling, a window on a blind wall, no room: shut", [] {
        const st::RoomMesh room = TestRoom();
        // the window in the front wall's hole (its outer glass a little in front of the wall), facing out (-y)
        const st::SeeWindow w = MakeWindow(0, -0.2f, 2, 0, -1, 0, 1, 1, 0);
        const float street[3] = { 0, -15, 2 }, aside[3] = { -6, -12, 3 }, low[3] = { 0, -10, 0.5f };
        CHECK(st::SeesRoom(w, street, room) && st::SeesRoom(w, aside, room) && st::SeesRoom(w, low, room));
        // a dormer above the ceiling (z 4): from the street the line of sight goes over the room (nothing), from up high
        // it meets the ceiling from above (its back): shut either way
        const st::SeeWindow dormer = MakeWindow(0, -0.2f, 6, 0, -1, 0, 1, 1, 0);
        const float high[3] = { 0, -6, 20 };
        CHECK(!st::SeesRoom(dormer, street, room) && !st::SeesRoom(dormer, high, room));
        float t = 0, d[3] = { 0, 5.8f, -14.0f };
        const float l = std::sqrt(d[1] * d[1] + d[2] * d[2]);
        d[1] /= l; d[2] /= l;
        CHECK(room.First(high, d, 15.0f, 100.0f, &t) == st::RoomMesh::kBack);   // (what it meets: the ceiling's back)
        // a window painted on the blind side wall (x -5): its line of sight meets the wall from outside
        const st::SeeWindow blind = MakeWindow(-5.2f, 4, 2, -1, 0, 0, 1, 1, 0);
        const float west[3] = { -20, 4, 2 };
        CHECK(!st::SeesRoom(blind, west, room));
        // a window half over the hole: some of its rays meet the front wall from outside
        const st::SeeWindow half = MakeWindow(1.0f, -0.2f, 2, 0, -1, 0, 1, 1, 0);
        CHECK(!st::SeesRoom(half, street, room));
        // nothing to look into, or no eye
        CHECK(!st::SeesRoom(w, street, st::RoomMesh{}));
        const float nan[3] = { NAN, 0, 0 };
        CHECK(!st::SeesRoom(w, nan, room));
    });

    // --- which windows a batch is in -----------------------------------------------------------------------------------
    Test("WindowOf, WindowsOfBatch: each triangle's middle in a window's box; strays and their area; a bad index reads nothing", [] {
        const st::SeeWindow w[2] = { MakeWindow(0, 0, 0, 0, -1, 0, 1, 1, 1, 0), MakeWindow(4, 0, 0, 0, -1, 0, 1, 1, 1, 1) };
        const float in0[3] = { 0.5f, 0.1f, -0.5f }, in1[3] = { 4.9f, -0.2f, 0.9f }, gap[3] = { 2, 0, 0 }, deep[3] = { 0, 0.6f, 0 };
        CHECK(st::WindowOf(in0, w, 2) == 0 && st::WindowOf(in1, w, 2) == 1 && st::WindowOf(gap, w, 2) == -1 && st::WindowOf(deep, w, 2) == -1);
        // vertices: window 0's quad, window 1's quad, and a stray triangle between them (area 0.5)
        const float v[] = { -1, 0, -1, 1, 0, -1, 1, 0, 1, -1, 0, 1,     3, 0, -1, 5, 0, -1, 5, 0, 1, 3, 0, 1,     1.5f, 0, 0, 2.5f, 0, 0, 2, 0, 1 };
        const uint16_t idx[] = { 0, 1, 2, 0, 2, 3,   4, 5, 6, 4, 6, 7,   8, 9, 10 };
        st::BatchWindows b = st::WindowsOfBatch(idx, 15, v, 11, 0, 15, w, 2);
        CHECK(b.valid && b.triangles == 5 && b.strays == 1 && Close(b.strayArea, 0.5f, 1e-5f) && b.windows == std::vector<uint16_t>({ 0, 1 }));
        b = st::WindowsOfBatch(idx, 15, v, 11, 0, 12, w, 2);
        CHECK(b.valid && b.strays == 0 && b.windows.size() == 2);
        b = st::WindowsOfBatch(idx, 15, v, 11, 6, 6, w, 2);
        CHECK(b.valid && b.windows == std::vector<uint16_t>({ 1 }));
        const uint16_t bad[] = { 0, 1, 99 };
        CHECK(!st::WindowsOfBatch(bad, 3, v, 11, 0, 3, w, 2).valid);
        CHECK(!st::WindowsOfBatch(idx, 15, v, 11, 12, 6, w, 2).valid);   // past the end
        CHECK(!st::WindowsOfBatch(idx, 15, v, 11, 0, 4, w, 2).valid);    // not whole triangles
    });

    Test("Deferrable: at least one of its windows open (shut ones stay opaque in the redraw), strays only as trim", [] {
        st::BatchWindows b;
        b.valid = true;
        b.windows = { 0, 2 };
        const uint8_t open2[3] = { st::kOpened, st::kShut, st::kOpened };
        const uint8_t oneUnseen[3] = { st::kOpened, st::kShut, st::kUnseen };
        const uint8_t oneShut[3] = { st::kOpened, st::kOpened, st::kShut };
        const uint8_t none[3] = { st::kUnseen, st::kOpened, st::kUnseen };
        const uint8_t allShut[3] = { st::kShut, st::kOpened, st::kShut };
        CHECK(st::Deferrable(b, open2, 3));
        CHECK(st::Deferrable(b, oneUnseen, 3));      // window 1 isn't the batch's
        CHECK(st::Deferrable(b, oneShut, 3));        // its shut window: opaque, its fake room in it (OpenBits)
        CHECK(!st::Deferrable(b, none, 3));          // nothing open to see through
        CHECK(!st::Deferrable(b, allShut, 3));
        CHECK(!st::Deferrable(b, open2, 2));         // a window past the states
        b.strayArea = st::kTrimArea * 0.5f;
        CHECK(st::Deferrable(b, open2, 3));
        b.strayArea = st::kTrimArea * 2.0f;
        CHECK(!st::Deferrable(b, open2, 3));
        b.strayArea = 0;
        b.valid = false;
        CHECK(!st::Deferrable(b, open2, 3));
    });

    Test("OpenBits: the lowest open ordinal and a bit for each open window within 24 of it; shut, unseen and other batches' none", [] {
        std::vector<st::SeeWindow> w;
        for (uint32_t o : { 7u, 3u, 5u, 40u, 9u, 31u }) w.push_back(MakeWindow(0, 0, 0, 0, -1, 0, 1, 1, 1, o));
        st::BatchWindows b;
        b.valid = true;
        b.windows = { 0, 1, 2, 3, 5 };   // window 4 (ordinal 9) another batch's
        const uint8_t states[6] = { st::kOpened, st::kShut, st::kOpened, st::kOpened, st::kOpened, st::kOpened };
        float first = -1;
        uint32_t bits = 0;
        st::OpenBits(b, w.data(), states, w.size(), first, bits);
        // open: 7, 5, 40, 31 (3 shut, 9 not the batch's); from 5: 7 -> bit 2, 5 -> bit 0, 31 -> bit 26 (past 24: opaque), 40 past
        CHECK(first == 5.0f && bits == ((1u << 0) | (1u << 2)));
        const uint8_t noneOpen[6] = { st::kShut, st::kShut, st::kUnseen, st::kShut, st::kOpened, st::kShut };
        st::OpenBits(b, w.data(), noneOpen, w.size(), first, bits);
        CHECK(first == 0.0f && bits == 0);
        st::OpenBits(b, nullptr, states, w.size(), first, bits);
        CHECK(bits == 0);
        // the inn's batch from the street: 1, 2, 10, 11 open, the rest shut: from 1, bits 0, 1, 9, 10
        std::vector<st::SeeWindow> inn;
        for (uint32_t o = 1; o <= 17; ++o) inn.push_back(MakeWindow(0, 0, 0, 0, -1, 0, 1, 1, 5, o));
        st::BatchWindows all;
        all.valid = true;
        for (uint16_t i = 0; i < 17; ++i) all.windows.push_back(i);
        std::vector<uint8_t> s(17, st::kShut);
        s[0] = s[1] = s[9] = s[10] = st::kOpened;
        st::OpenBits(all, inn.data(), s.data(), s.size(), first, bits);
        CHECK(first == 1.0f && bits == 0x603u);
    });

    // --- the deferred draw ---------------------------------------------------------------------------------------------
    Test("DrawArgsOf: the client's device draw's arguments (base vertex from the stream, first index from the index buffer)", [] {
        const uint32_t batch[4] = { 3, 120, 186, (40u << 16) | 4u };   // triangles, from index 120, 62 of them, vertices 4..40
        st::DrawArgs a;
        CHECK(st::DrawArgsOf(batch, 0, 32 * 1000, 32, 2 * 5000, 4, a));
        CHECK(a.primitive == 4 && a.baseVertex == 1000 && a.minIndex == 4 && a.vertices == 37 && a.startIndex == 5120 && a.primitives == 62);
        CHECK(st::DrawArgsOf(batch, 1, 32 * 1000, 32, 0, 4, a) && a.baseVertex == 0 && a.startIndex == 120);   // the stream-offset device
        const uint32_t strip[4] = { 4, 0, 6, 3u << 16 };
        CHECK(!st::DrawArgsOf(strip, 0, 0, 32, 0, 5, a));          // not a triangle list
        CHECK(!st::DrawArgsOf(batch, 0, 0, 32, 0, 5, a));          // the table says otherwise
        const uint32_t odd[4] = { 3, 0, 4, 3u << 16 };
        CHECK(!st::DrawArgsOf(odd, 0, 0, 32, 0, 4, a));
        CHECK(!st::DrawArgsOf(batch, 0, 0, 0, 0, 4, a));           // no stride to derive a base from
        const uint32_t backwards[4] = { 3, 0, 3, 2u | (1u << 16) };
        CHECK(!st::DrawArgsOf(backwards, 0, 0, 32, 0, 4, a));
    });

    Test("ReplayConstants: 1 - the opacity, the batch's alpha test as the stock alpha's threshold (or none), the open windows", [] {
        float c[4];
        st::ReplayConstants(0.35f, false, 0, 0, 1.0f, 0x603u, c);
        CHECK(Close(c[0], 0.65f, 1e-6f) && c[1] == -1.0f && c[2] == 1.0f && c[3] == 1539.0f);
        st::ReplayConstants(0.35f, true, st::kD3dCmpGreaterEqual, 224, 3.0f, 1u, c);      // alpha-keyed: >= 224
        CHECK(Close(c[1], 224.0f / 255.0f, 1e-6f));
        st::ReplayConstants(0.35f, true, st::kD3dCmpGreater, 128, 3.0f, 1u, c);
        CHECK(Close(c[1], 128.5f / 255.0f, 1e-6f));
        st::ReplayConstants(0.35f, true, 8 /* always */, 128, 3.0f, 1u, c);
        CHECK(c[1] == -1.0f);
        st::ReplayConstants(2.0f, false, 0, 0, 0, 0, c);
        CHECK(c[0] == 0.0f);                                                            // opaque
        st::ReplayConstants(-1.0f, false, 0, 0, 0, 0, c);
        CHECK(c[0] == 1.0f);                                                            // clear
        st::ReplayConstants(NAN, false, 0, 0, NAN, 0xFFFFFFFFu, c);
        CHECK(c[0] == 0.0f && c[2] == 0.0f && c[3] == 16777215.0f);                     // 24 bits, exactly
    });

    Test("Deferral: the pending batch's one draw is deferred (its range, opaque or alpha-keyed); everything else drawn", [] {
        st::Deferral d;
        using D = st::Deferral::Draw;
        // nothing pending: drawn
        CHECK(d.OnDraw(true, 3, 0, 3, 0) == D::Pass && d.Passed() == 0);
        // a deferrable batch: its draw deferred, once, its open windows with it
        d.BeginBatch(true, 120, 186, 1.0f, 0x603u);
        CHECK(d.Pending() && d.OnDraw(true, 3, 120, 186, 0) == D::Defer && !d.Pending());
        st::Deferral::Entry e;
        e.args.primitives = 62;
        e.state = reinterpret_cast<void*>(0x1234);
        d.Captured(e);
        CHECK(d.OnDraw(true, 3, 120, 186, 0) == D::Pass && d.Count() == 1);   // a second draw of it: the client's
        // not deferrable: drawn
        d.BeginBatch(false, 0, 3);
        CHECK(!d.Pending() && d.OnDraw(true, 3, 0, 3, 0) == D::Pass);
        // another batch's draw (a texture missing: the client skipped this one's): drawn, counted
        d.BeginBatch(true, 300, 30);
        CHECK(d.OnDraw(true, 3, 330, 30, 0) == D::Pass && d.Passed() == 1);
        // blended (a transition batch's two passes), or not indexed: drawn
        d.BeginBatch(true, 300, 30);
        CHECK(d.OnDraw(true, 3, 300, 30, 2) == D::Pass && d.Passed() == 2);
        d.BeginBatch(true, 300, 30);
        CHECK(d.OnDraw(false, 3, 300, 30, 0) == D::Pass && d.Passed() == 3);
        // alpha-keyed: deferred
        d.BeginBatch(true, 300, 30);
        CHECK(d.OnDraw(true, 3, 300, 30, 1) == D::Defer);
        d.CaptureFailed();
        CHECK(d.Failed() == 1 && d.Count() == 1);
        // the loop's end drops what's pending
        d.BeginBatch(true, 300, 30);
        d.EndLoop();
        CHECK(!d.Pending() && d.OnDraw(true, 3, 300, 30, 0) == D::Pass);
        // the cap
        for (unsigned i = d.Count(); i < st::kMaxDeferred; ++i) d.Captured(e);
        d.BeginBatch(true, 0, 3);
        CHECK(d.OnDraw(true, 3, 0, 3, 0) == D::Pass && d.Count() == st::kMaxDeferred);
        // taken in order, the list emptied
        const auto all = d.Take();
        CHECK(all.size() == st::kMaxDeferred && all[0].args.primitives == 62 && d.Count() == 0);
        CHECK(all[0].first == 1.0f && all[0].bits == 0x603u && all[1].bits == 0);   // each its own batch's open windows
        d.ResetCounts();
        CHECK(d.Passed() == 0 && d.Failed() == 0);
    });

    Test("the whitelist: the Lion's Pride Inn, and nothing else", [] {
        CHECK(st::Whitelisted(gl::PathHash(kInn)));
        CHECK(st::Whitelisted(gl::PathHash("World\\WMO\\Azeroth\\Buildings\\GoldshireInn\\GoldshireInn.wmo")));   // as the client names it
        CHECK(!st::Whitelisted(gl::PathHash("world\\wmo\\azeroth\\buildings\\goldshireinn\\goldshireinn_closed.wmo")));
        CHECK(!st::Whitelisted(0));
    });

    // --- the real data -------------------------------------------------------------------------------------------------
    if (!data.empty())
    {
        char tmp[MAX_PATH] = {};
        GetTempPathA(MAX_PATH, tmp);
        const fs::path root = fs::path(tmp) / ("la-seethrough-" + std::to_string(GetCurrentProcessId()));
        const fs::path db = root / "DBFilesClient";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(db, ec);
        bool copied = !ec;
        for (const char* name : { "GlassMaterials.cdbc", "GlassPanes.cdbc", "RoomStyles.cdbc", "GlassBuildings.cdbc", "GlassWindows.cdbc" })
            copied = copied && fs::copy_file(fs::path(data) / name, db / name, fs::copy_options::overwrite_existing, ec);
        const fs::path cwd = fs::current_path();
        if (copied) fs::current_path(root);
        st::RootWindows inn;
        Test("the real tables through the real reader: glass::Load keeps the windows; the inn's 17 open onto the survey's rooms", [&] {
            CHECK(copied);
            const WXL_SeyrisCdbcApi linked = LinkedApi();
            gl::Load(&linked);
            const auto& all = gl::Windows().All();
            std::printf("  GlassWindows.cdbc: %zu windows kept\n", all.size());
            CHECK(all.size() > 10000);
            const uint32_t path = gl::PathHash(kInn);
            inn = st::BuildRoot(path, all, kInnGroups, 12);
            std::printf("  the inn: %zu exterior windows, %u with a room behind, %u fake\n", inn.windows.size(), inn.real, inn.fake);
            CHECK(inn.windows.size() == 17 && inn.real == 17 && inn.fake == 0);
            uint32_t n = 0;
            const st::SeeWindow* w = inn.Group(4, n);
            CHECK(w && n == 17);
            for (uint32_t i = 0; i < n; ++i)
            {
                CHECK(w[i].ordinal == i + 1);
                if (w[i].interior != kInnRooms[i])
                    throw std::runtime_error("window " + std::to_string(w[i].ordinal) + ": room " + std::to_string(w[i].interior) + ", the survey's " +
                                             std::to_string(kInnRooms[i]));
            }
            // standing south of the inn in the street, 25 yd out, looking at it: its four south windows open, nothing on the
            // other sides (as the inn stands in its own space; the placement's turn is ScreenRect's test above)
            const float cam[3] = { 0, -35, 4 }, at[3] = { 0, 0, 6 };
            float vp[16];
            Camera(cam, at, 1.0f, 16.0f / 9.0f, vp);
            std::vector<st::WindowPlan> plan;
            unsigned budget = st::kMaxOpenPerFrame;
            st::PlanWindows(w, n, cam, kInnGroups[4].lo, kInnGroups[4].hi, { -1, -1, 1, 1 }, kIdentity, cam, vp, budget, plan);
            std::vector<uint32_t> open;
            for (uint32_t i = 0; i < n; ++i)
                if (plan[i].candidate) open.push_back(w[i].ordinal);
            CHECK(open == std::vector<uint32_t>({ 1, 2, 10, 11 }));
        });
        fs::current_path(cwd);
        fs::remove_all(root, ec);

        if (!scan.empty())
            Test("the scan's inn: the root's MOGI as above; its one glass batch covers all 17 windows, no strays, deferred when they're open", [&] {
                CHECK(inn.windows.size() == 17);
                const fs::path base = fs::path(scan) / "world#wmo#azeroth#buildings#goldshireinn#goldshireinn";
                const std::vector<uint8_t> r = ReadFile(base.string() + ".wmo");
                bool sawMogi = false;
                for (const Chunk& c : Chunks(r, 0, r.size()))
                    if (c.tag == "MOGI")
                    {
                        sawMogi = true;
                        CHECK(c.size == 12 * 32);
                        for (uint32_t i = 0; i < 12; ++i)
                        {
                            uint32_t flags;
                            float box[6];
                            std::memcpy(&flags, &r[c.at + 32 * i], 4);
                            std::memcpy(box, &r[c.at + 32 * i + 4], 24);
                            CHECK(flags == kInnGroups[i].flags);
                            for (int k = 0; k < 3; ++k) CHECK(box[k] == kInnGroups[i].lo[k] && box[3 + k] == kInnGroups[i].hi[k]);
                        }
                    }
                CHECK(sawMogi);
                const std::vector<uint8_t> g = ReadFile(base.string() + "_004.wmo");
                std::vector<uint16_t> indices;
                std::vector<float> vertices;
                struct Moba { uint32_t start; uint16_t count; uint8_t material; };
                std::vector<Moba> batches;
                for (const Chunk& c : Chunks(g, 0, g.size()))
                    if (c.tag == "MOGP")
                        for (const Chunk& s : Chunks(g, c.at + 0x44, c.at + c.size))
                        {
                            if (s.tag == "MOVI") { indices.resize(s.size / 2); std::memcpy(indices.data(), &g[s.at], indices.size() * 2); }
                            else if (s.tag == "MOVT") { vertices.resize(s.size / 4); std::memcpy(vertices.data(), &g[s.at], vertices.size() * 4); }
                            else if (s.tag == "MOBA")
                                for (size_t at = s.at; at + 24 <= s.at + s.size; at += 24)
                                {
                                    Moba m;
                                    std::memcpy(&m.start, &g[at + 12], 4);
                                    std::memcpy(&m.count, &g[at + 16], 2);
                                    m.material = g[at + 23];
                                    batches.push_back(m);
                                }
                        }
                CHECK(!indices.empty() && !vertices.empty() && !batches.empty());
                uint32_t n = 0;
                const st::SeeWindow* w = inn.Group(4, n);
                int glassBatches = 0;
                for (const Moba& m : batches)
                {
                    if (m.material != 5) continue;   // mm_elwynn_wnd_ext__01: the outside face of the leaded glass
                    ++glassBatches;
                    const st::BatchWindows b = st::WindowsOfBatch(indices.data(), static_cast<uint32_t>(indices.size()), vertices.data(),
                                                                  static_cast<uint32_t>(vertices.size() / 3), m.start, m.count, w, n);
                    std::printf("  group 4's glass batch: %u triangles in %zu windows, %u strays\n", b.triangles, b.windows.size(), b.strays);
                    CHECK(b.valid && b.triangles == 62 && b.windows.size() == 17 && b.strays == 0);
                    std::vector<uint8_t> states(n, st::kOpened);
                    CHECK(st::Deferrable(b, states.data(), states.size()));
                    for (uint32_t i = 0; i < n; ++i) states[i] = i < 4 ? st::kOpened : st::kUnseen;   // from the street: four open
                    CHECK(st::Deferrable(b, states.data(), states.size()));
                    states[5] = st::kShut;                                                         // one facing but not drawn:
                    CHECK(st::Deferrable(b, states.data(), states.size()));                        // opaque in the redraw
                    for (uint8_t& s : states) s = s == st::kOpened ? st::kShut : s;                // none open: as it was
                    CHECK(!st::Deferrable(b, states.data(), states.size()));
                }
                CHECK(glassBatches == 1);
            });
        if (!scan.empty())
            Test("the scan's inn from the street: which windows show their room (SeesRoom over its real groups, the glass aside)", [&] {
                CHECK(inn.windows.size() == 17);
                const std::string base = (fs::path(scan) / "world#wmo#azeroth#buildings#goldshireinn#goldshireinn").string();
                const std::vector<std::string> textures = ReadRootTextures(base + ".wmo");
                std::vector<bool> glass(textures.size());
                unsigned glassMaterials = 0;
                for (size_t i = 0; i < textures.size(); ++i) glassMaterials += (glass[i] = gl::MaterialOf(textures[i].c_str()) != 0) ? 1 : 0;
                std::printf("  the root's %zu materials, %u of them glass (GlassMaterials.cdbc)\n", textures.size(), glassMaterials);
                CHECK(glassMaterials >= 2);
                // each room's surfaces, as the runtime copies them out (timed: the frame's one build)
                std::map<int, st::RoomMesh> rooms;
                double buildMs = 0;
                for (const st::SeeWindow& w : inn.windows)
                    if (!rooms.count(w.interior))
                    {
                        char name[16];
                        std::snprintf(name, sizeof(name), "_%03d.wmo", w.interior);
                        const ScanGroup g = ReadScanGroup(base + name);
                        const auto t0 = std::chrono::steady_clock::now();
                        CHECK(BuildMesh(g, glass, rooms[w.interior]));
                        buildMs = std::max(buildMs, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                        std::printf("  room %d: %zu triangles (its glass aside)\n", w.interior, rooms[w.interior].Triangles());
                    }
                // the grid against the brute force on the biggest room (the kitchen: rays from round the inn into it)
                {
                    const st::RoomMesh& kitchen = rooms.at(5);
                    uint32_t seed = 777;
                    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
                    int agree = 0;
                    for (int i = 0; i < 2000; ++i)
                    {
                        const float o[3] = { rnd() * 80 - 40, rnd() * 80 - 40, rnd() * 30 };
                        const float at[3] = { rnd() * 30 - 16, rnd() * 26 - 11, rnd() * 16 };
                        float d[3] = { at[0] - o[0], at[1] - o[1], at[2] - o[2] };
                        const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                        for (float& x : d) x /= l;
                        float ta = -1, tb = -1;
                        const auto a = kitchen.First(o, d, 0.0f, 200.0f, &ta), b = kitchen.FirstBrute(o, d, 0.0f, 200.0f, &tb);
                        agree += a == b && (a == st::RoomMesh::kNone || Close(ta, tb, 1e-3f)) ? 1 : 0;
                    }
                    std::printf("  the kitchen: %d of 2000 random rays as the brute force's\n", agree);
                    CHECK(agree >= 1998);   // (a ray along a shared edge may take either triangle)
                }
                // typical street viewpoints in front of each window: 8, 15 and 30 yd out, straight and 30 / 60 degrees
                // aside, the eye 2 and 6 yd up (the inn's ground floor is at 0.4)
                uint32_t n = 0;
                const st::SeeWindow* w = inn.Group(4, n);
                CHECK(w && n == 17);
                int raysTimed = 0;
                double rayUs = 0;
                std::vector<int> opens(n), views(n), headOn(n);
                for (uint32_t i = 0; i < n; ++i)
                {
                    const float nx = w[i].normal[0], ny = w[i].normal[1], l = std::sqrt(nx * nx + ny * ny);
                    for (const float r : { 8.0f, 15.0f, 30.0f })
                        for (const float deg : { -60.0f, -30.0f, 0.0f, 30.0f, 60.0f })
                            for (const float z : { 2.0f, 6.0f })
                            {
                                const float a = deg * 3.14159265f / 180.0f;
                                const float dx = (nx * std::cos(a) - ny * std::sin(a)) / l, dy = (ny * std::cos(a) + nx * std::sin(a)) / l;
                                const float eye[3] = { w[i].center[0] + r * dx, w[i].center[1] + r * dy, z };
                                if (!st::Facing(w[i], eye)) continue;
                                const auto t0 = std::chrono::steady_clock::now();
                                const bool sees = st::SeesRoom(w[i], eye, rooms.at(w[i].interior));
                                rayUs += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
                                raysTimed += static_cast<int>(sizeof(st::kSight) / sizeof(st::kSight[0]));
                                ++views[i];
                                opens[i] += sees ? 1 : 0;
                                if (r == 15.0f && deg == 0.0f && z == 2.0f) headOn[i] = sees ? 1 : -1;
                            }
                }
                std::printf("  (the biggest room's build %.1f ms; %.2f us a ray)\n", buildMs, raysTimed ? rayUs / raysTimed : 0.0);
                for (uint32_t i = 0; i < n; ++i)
                    std::printf("  window %2u (z %4.1f, facing %+.2f %+.2f %+.2f, room %d): opens from %2d of %2d street views%s\n", w[i].ordinal,
                                w[i].center[2], w[i].normal[0], w[i].normal[1], w[i].normal[2], w[i].interior, opens[i], views[i],
                                headOn[i] > 0 ? ", head on from 15 yd" : headOn[i] < 0 ? ", shut head on from 15 yd" : "");
                // the ground floor's front windows (south: the street) open head on; the back dormers (z 15.5) never from the
                // street; the inn's batch still goes see-through with them shut (Deferrable)
                CHECK(headOn[0] > 0 && headOn[1] > 0);
                CHECK(opens[14] == 0 && opens[15] == 0 && opens[16] == 0);
                std::vector<uint8_t> states(n, st::kShut);
                states[0] = states[1] = st::kOpened;
                st::BatchWindows b;
                b.valid = true;
                for (uint16_t k = 0; k < n; ++k) b.windows.push_back(k);
                CHECK(st::Deferrable(b, states.data(), states.size()));
            });
        else std::printf("SKIP the scan's inn (no wmo-windows folder given)\n");
    }
    else std::printf("SKIP the real tables (no data folder given)\n");

    std::printf("%s: %u passed, %u failed\n", g_failed ? "FAILED" : "PASSED", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
