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
//  - From inside (SeeThroughFromInside, 2026-10-21): FacingIn, PlanInside, the view the client would push (ViewOf: its
//    CPortalView layout, CRect's order, (x + 1) / 2, the depth), LookOut on a test room with a glazed, a blind and no
//    outside; the inn's rooms' own 17 windows through the real reader (BuildInside); with the scan, each room's windows
//    from eyes all over it against its surfaces and the shell's walls and glass (printed; the ground floor's four open
//    from nearly everywhere), and from the GPU test's spot the open windows' views and the street behind them inside
//    their union (the rectangle the client culls the outdoors through).
//  - The rays followed on through the building (LookIn, 2026-10-22): a doorway into the next room, the roof from inside, the
//    void, a window half over its room's ceiling; the scan's inn by the rule of 2026-10-21 and now over the street sweep,
//    high and close and high and far before the dormer front, the user's viewpoint, and walks past every front at heights.
// Usage:  seethrough_tests <Wow.exe> <repo data folder> [<map scan's wmo-windows folder>]
#include "env/GlassModel.hpp"
#include "env/GlassRooms.hpp"
#include "env/SeeThroughModel.hpp"
#include "debug/UiText.hpp"
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
#include <limits>
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
    // A group's surfaces as the runtime takes them since 2026-10-22 (MeshOf): every batch, its glass tagged kGlassTag.
    bool BuildFull(const ScanGroup& g, const std::vector<bool>& glass, st::RoomMesh& mesh)
    {
        std::vector<std::pair<uint32_t, uint32_t>> ranges;
        std::vector<uint8_t> tags;
        for (const ScanGroup::Moba& m : g.batches)
        {
            ranges.emplace_back(m.start, m.count);
            tags.push_back(m.material < glass.size() && glass[m.material] ? st::kGlassTag : 0);
        }
        return mesh.Build(g.vertices.data(), static_cast<uint32_t>(g.vertices.size() / 3), g.indices.data(), static_cast<uint32_t>(g.indices.size()),
                          ranges, tags);
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
            // a detour may hold their first bytes. And (from inside) wxl-core's other hook points on the way: Wmo.PortalRectAccum
            // (AddExteriorPortalView), Wmo.PortalTraverse (RenderWithCallback), CullSortTable, the scene render; and the
            // functions called (PushExteriorPortalView, the two merges).
            for (const uintptr_t hooked : { 0x007AC060u, 0x006A3620u, 0x00823CB0u, 0x007A7630u, 0x007ABF50u, 0x007AC6A0u, 0x007AC9F0u,
                                            0x007A8F20u, 0x007B3B20u, 0x0079A790u, 0x0079A870u, 0x00795D00u, 0x00790AB0u, 0x00790AD0u })
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
            // From inside: AddExteriorPortalView's view (at ebp-34h, its depth stored at ebp-24h: +0x10) goes to the functions
            // the runtime calls, which take the arrays and views it reads; the scene render reads the frustum view's depth
            // and the exterior views' count where the constants say.
            const auto* add = static_cast<const uint8_t*>(im.At(0x007A904C, 26));
            CHECK(static_cast<int8_t>(add[10]) - static_cast<int8_t>(add[7]) == static_cast<int>(st::client::kViewDepth));
            std::memcpy(&rel, add + 13, 4);
            CHECK(static_cast<uintptr_t>(0x007A9058 + 5 + rel) == st::client::kPushExteriorPortalView);
            std::memcpy(&rel, add + 22, 4);
            CHECK(static_cast<uintptr_t>(0x007A9061 + 5 + rel) == st::client::kMergeIntoWorldRect);
            std::memcpy(&v, static_cast<const uint8_t*>(im.At(0x00795D06, 13)) + 4, 4);
            CHECK(v == st::client::kExteriorPortalViews);
            std::memcpy(&v, static_cast<const uint8_t*>(im.At(0x00790AB6, 11)) + 2, 4);
            CHECK(v == st::client::kWorldView);
            std::memcpy(&v, static_cast<const uint8_t*>(im.At(0x00790AD6, 11)) + 2, 4);
            CHECK(v == st::client::kFrustumView);
            std::memcpy(&v, static_cast<const uint8_t*>(im.At(0x0079AA33, 6)) + 2, 4);
            CHECK(v == st::client::kFrustumView + st::client::kViewDepth);
            std::memcpy(&v, static_cast<const uint8_t*>(im.At(0x0079AA4B, 16)) + 1, 4);
            CHECK(v == st::client::kFrustumView);
            std::memcpy(&v, static_cast<const uint8_t*>(im.At(0x0079AC41, 19)) + 2, 4);
            CHECK(v == st::client::kWorldView + st::client::kViewDepth);
            std::memcpy(&v, static_cast<const uint8_t*>(im.At(0x0079AC41, 19)) + 15, 4);
            CHECK(v == st::client::kExteriorPortalViews + st::client::kArrayCount);
            // (the walk from inside: the call at 0x7AC3D8 is AddExteriorPortalView)
            std::memcpy(&rel, static_cast<const uint8_t*>(im.At(0x007AC3D8, 5)) + 1, 4);
            CHECK(static_cast<uintptr_t>(0x007AC3D8 + 5 + rel) == 0x007A8F20u);
            std::printf("  %zu guards\n", sizeof(st::kGuards) / sizeof(st::kGuards[0]));
        });
    else std::printf("SKIP guards (no Wow.exe given)\n");

    // --- the panel's text -------------------------------------------------------------------------------------------
    Test("panel text (2026-10-22): UiTextWrapped where the running core's table carries it (v1.1.272), UiText on an older "
         "core's shorter table or with the member unset", [] {
        static int plain = 0, wrapped = 0;
        WXL_Api api{};
        api.UiText = [](const char*) { ++plain; };
        api.UiTextWrapped = [](const char*) { ++wrapped; };
        // (appended right after UiInputText: the core before v1.1.272 hands a table that ends there)
        CHECK(offsetof(WXL_Api, UiTextWrapped) == offsetof(WXL_Api, UiInputText) + sizeof(api.UiInputText));
        api.structSize = static_cast<uint32_t>(sizeof(WXL_Api));
        CHECK(wxl_livingazeroth::ui::HasTextWrapped(&api));
        wxl_livingazeroth::ui::Text(&api, "a line");
        CHECK(wrapped == 1 && plain == 0);
        api.structSize = static_cast<uint32_t>(offsetof(WXL_Api, UiTextWrapped));   // the older core's size
        CHECK(!wxl_livingazeroth::ui::HasTextWrapped(&api));
        wxl_livingazeroth::ui::Text(&api, "a line");
        CHECK(wrapped == 1 && plain == 1);
        api.structSize = static_cast<uint32_t>(sizeof(WXL_Api));
        api.UiTextWrapped = nullptr;
        wxl_livingazeroth::ui::Text(&api, "a line");
        CHECK(wrapped == 1 && plain == 2 && !wxl_livingazeroth::ui::HasTextWrapped(nullptr));
    });

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

    Test("SeesRoom: a window onto its room opens, one whose outer rays meet the wall round its opening too; a dormer over the "
         "ceiling, a window on a blind wall or mostly over it, no room: shut; what the rays met", [] {
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
        // a window mostly over the wall beside the hole: its rays meet the front wall from outside
        const st::SeeWindow over = MakeWindow(1.6f, -0.2f, 2, 0, -1, 0, 1, 1, 0);
        st::Sight sight;
        CHECK(!st::SeesRoom(over, street, room, &sight) && sight.middle == st::RoomMesh::kBack && sight.back >= 6 && sight.firstMiss == 0);
        // one whose outer column of rays meets the wall round the opening (a wider outer pane, an arch's corner): the middle and
        // six of nine see the room, so it opens; what the three met is said
        const st::SeeWindow wider = MakeWindow(0.6f, -0.2f, 2, 0, -1, 0, 1.2f, 1, 0);
        CHECK(st::SeesRoom(wider, street, room, &sight) && sight.front == 6 && sight.back == 3 && sight.middle == st::RoomMesh::kFront &&
              sight.missHit == st::RoomMesh::kBack && sight.firstMiss == 3 && Close(sight.missDepth, 0.2f, 0.05f));
        // the middle seeing the room but most of the rays the wall round the opening: shut
        const st::SeeWindow most = MakeWindow(0.7f, -0.2f, 2, 0, -1, 0, 1.0f, 2.2f, 0);
        CHECK(!st::SeesRoom(most, street, room, &sight) && sight.middle == st::RoomMesh::kFront && sight.front == 2 && sight.back == 7);
        // nothing to look into, or no eye
        CHECK(!st::SeesRoom(w, street, st::RoomMesh{}));
        const float nan[3] = { NAN, 0, 0 };
        CHECK(!st::SeesRoom(w, nan, room));
    });

    // --- from inside: the street through the room's own windows ----------------------------------------------------------
    Test("from inside: FacingIn, PlanInside (on the screen within the walk's rectangle, range, budget), the view the client "
         "would make of it (ViewOf: CRect's order, (x + 1) / 2, its farthest depth)", [] {
        // the room's own glass in the front wall's hole (TestRoom), its normal out of the building (-y); the eye in the room
        const st::SeeWindow w = MakeWindow(0, 0.05f, 2, 0, -1, 0, 1, 1, -1, 18);
        const float eye[3] = { 0, 5, 2 }, at[3] = { 0, 0, 2 }, street[3] = { 0, -5, 2 };
        CHECK(st::FacingIn(w, eye) && !st::FacingIn(w, street) && !st::Facing(w, eye) && st::Facing(w, street));
        const float onPlane[3] = { 0, 0.08f, 2 };   // (within kFacing of its plane: neither)
        CHECK(!st::FacingIn(w, onPlane) && !st::Facing(w, onPlane));
        float vp[16];
        Camera(eye, at, 1.0f, 16.0f / 9.0f, vp);
        std::vector<st::WindowPlan> plan;
        unsigned budget = st::kMaxInsidePerFrame;
        st::PlanInside(&w, 1, eye, { -1, -1, 1, 1 }, kIdentity, eye, vp, budget, plan);
        CHECK(plan.size() == 1 && plan[0].state == st::kShut && plan[0].candidate && budget == st::kMaxInsidePerFrame - 1);
        const st::Rect s = plan[0].screen;
        CHECK(Close(s.minX, -s.maxX, 1e-4f) && Close(s.minY, -s.maxY, 1e-4f) && s.maxX > 0.1f && s.maxX < 0.5f);
        CHECK(Close(plan[0].depth, 4.95f, 1e-3f));   // (the quad's plane, 4.95 yd ahead along the view)
        const st::PortalView view = st::ViewOf(s, plan[0].depth);
        CHECK(Close(view.rect[0], (s.minY + 1) * 0.5f, 1e-6f) && Close(view.rect[1], (s.minX + 1) * 0.5f, 1e-6f) &&
              Close(view.rect[2], (s.maxY + 1) * 0.5f, 1e-6f) && Close(view.rect[3], (s.maxX + 1) * 0.5f, 1e-6f));
        CHECK(Close(view.maxViewDepth, 4.95f, 1e-3f) && view.verts == 0 && view.vertCount == 0);
        CHECK(st::ViewOf(s, -3.0f).maxViewDepth == 0.0f && st::ViewOf(s, NAN).maxViewDepth == 0.0f);
        // the layout the client reads: its rectangle at +0, its depth at +0x10, a null vertex pointer and count
        CHECK(offsetof(st::PortalView, maxViewDepth) == st::client::kViewDepth && offsetof(st::PortalView, verts) == 0x14 &&
              offsetof(st::PortalView, vertCount) == 0x18);
        // the budget spent, too far, off the walk's rectangle, from outside
        budget = 0;
        st::PlanInside(&w, 1, eye, { -1, -1, 1, 1 }, kIdentity, eye, vp, budget, plan);
        CHECK(plan[0].state == st::kShut && !plan[0].candidate);
        budget = 4;
        const float distant[3] = { 0, 45, 2 };
        Camera(distant, at, 1.0f, 16.0f / 9.0f, vp);
        st::PlanInside(&w, 1, distant, { -1, -1, 1, 1 }, kIdentity, distant, vp, budget, plan);
        CHECK(plan[0].state == st::kShut && !plan[0].candidate && budget == 4);
        Camera(eye, at, 1.0f, 16.0f / 9.0f, vp);
        st::PlanInside(&w, 1, eye, { 0.6f, 0.6f, 1, 1 }, kIdentity, eye, vp, budget, plan);
        CHECK(plan[0].state == st::kUnseen && !plan[0].candidate);
        const float back[3] = { 0, -5, 2 };
        Camera(back, at, 1.0f, 16.0f / 9.0f, vp);
        st::PlanInside(&w, 1, back, { -1, -1, 1, 1 }, kIdentity, back, vp, budget, plan);
        CHECK(plan[0].state == st::kUnseen && budget == 4);
        float depth = 0;
        float quad[4][3];
        st::Corners(w, 0.0f, quad);
        Camera(eye, at, 1.0f, 16.0f / 9.0f, vp);
        CHECK(st::FarthestDepth(quad, 4, kIdentity, eye, vp, depth) && Close(depth, 4.95f, 1e-3f));
        CHECK(!st::FarthestDepth(quad, 0, kIdentity, eye, vp, depth));
    });

    Test("LookOut: out by the building's own glass (open), into the open (open), through a blind wall from inside (shut: what "
         "it met and how far), at the room's own wall from the front (shut); a room's pane from behind see-through", [] {
        const st::RoomMesh room = TestRoom();
        const st::SeeWindow w = MakeWindow(0, 0.05f, 2, 0, -1, 0, 1, 1, -1, 18);
        // the building's outside 0.4 yd out: a wall facing the street with the same opening, its outer pane in it (tagged
        // glass); the room's own inner pane in the opening (tagged glass, facing the room)
        std::vector<float> v, gv, iv, bv;
        std::vector<uint16_t> idx, gidx, iidx, bidx;
        AddRect(v, idx, 1, -0.4f, -1, 5, -6, -1, -1);
        AddRect(v, idx, 1, -0.4f, -1, 5, 1, 6, -1);
        AddRect(v, idx, 1, -0.4f, -1, 1, -1, 1, -1);
        AddRect(v, idx, 1, -0.4f, 3, 5, -1, 1, -1);
        AddRect(gv, gidx, 1, -0.4f, 1, 3, -1, 1, -1);
        AddRect(iv, iidx, 1, 0.05f, 1, 3, -1, 1, 1);
        AddRect(bv, bidx, 1, -0.4f, -1, 5, -6, 6, -1);
        st::RoomMesh walls, pane, inner, blind;
        const auto all = [](const std::vector<uint16_t>& i) { return std::vector<std::pair<uint32_t, uint32_t>>{ { 0u, static_cast<uint32_t>(i.size()) } }; };
        CHECK(walls.Build(v.data(), static_cast<uint32_t>(v.size() / 3), idx.data(), static_cast<uint32_t>(idx.size()), all(idx)));
        CHECK(pane.Build(gv.data(), static_cast<uint32_t>(gv.size() / 3), gidx.data(), static_cast<uint32_t>(gidx.size()), all(gidx), { st::kGlassTag }));
        CHECK(inner.Build(iv.data(), static_cast<uint32_t>(iv.size() / 3), iidx.data(), static_cast<uint32_t>(iidx.size()), all(iidx), { st::kGlassTag }));
        CHECK(blind.Build(bv.data(), static_cast<uint32_t>(bv.size() / 3), bidx.data(), static_cast<uint32_t>(bidx.size()), all(bidx)));
        const float eye[3] = { 0, 5, 2 }, aside[3] = { -3, 6, 3 }, low[3] = { 1, 3, 1 };
        const st::SightMesh glazed[4] = { { &room, false }, { &inner, false }, { &walls, true }, { &pane, true } };
        st::Sight s;
        for (const auto* e : { eye, aside, low })
        {
            st::LookOut(w, e, glazed, 4, nullptr, s);
            CHECK(s.rays == 9 && s.front == 9 && s.back == 0 && s.none == 0 && st::SightOpen(s, false));
        }
        const st::SightMesh open[1] = { { &room, false } };   // no outside modelled: into the open
        st::LookOut(w, eye, open, 1, nullptr, s);
        CHECK(s.front == 9 && st::SightOpen(s, false));
        const st::SightMesh walled[2] = { { &room, false }, { &blind, true } };   // a blind wall behind the room's glass: its back
        st::LookOut(w, eye, walled, 2, nullptr, s);
        CHECK(s.back == 9 && s.front == 0 && !st::SightOpen(s, false) && s.firstMiss == 0 && s.missHit == st::RoomMesh::kBack &&
              Close(s.missDepth, 0.45f, 0.03f));
        // the room's own front wall (solid at x 3): a window painted on it sees the wall from the front, nothing of the street
        const st::SeeWindow painted = MakeWindow(3, 0.05f, 2, 0, -1, 0, 0.8f, 0.8f, -1, 19);
        const float there[3] = { 3, 5, 2 };
        st::LookOut(painted, there, glazed, 4, nullptr, s);
        CHECK(s.none == 9 && s.front == 0 && !st::SightOpen(s, false));
        // from the street the room's inner pane is seen from behind: see-through (LookIn's), the room behind it
        const st::SeeWindow outer = MakeWindow(0, -0.4f, 2, 0, -1, 0, 1, 1, 0, 1);
        const float street[3] = { 0, -15, 2 };
        st::LookIn(outer, street, glazed, 4, nullptr, s);
        CHECK(s.front == 9 && st::SightOpen(s, false));
        // its own glass points when given (the first, the middle's nearest)
        st::SightPoints p;
        p.n = 3;
        const float pts[3][3] = { { 0, 0.05f, 2 }, { 0.5f, 0.05f, 2.4f }, { -0.5f, 0.05f, 1.6f } };
        std::memcpy(p.p, pts, sizeof(pts));
        st::LookOut(w, eye, glazed, 4, &p, s);
        CHECK(s.onGlass && s.rays == 3 && s.front == 3 && s.middle == st::RoomMesh::kFront);
        const float nan[3] = { NAN, 0, 0 };
        st::LookOut(w, nan, glazed, 4, nullptr, s);
        CHECK(s.rays == 0 && !st::SightOpen(s, false));
    });

    Test("LookIn (2026-10-22): a ray that meets nothing of the room followed on: into the next room through a doorway "
         "(seeing in), the roof from inside or nothing at all (against); a window half over its room's ceiling now shut", [] {
        // room A: x -5..5, y 0..8, z 0..4, open at the front (y 0), its back wall (y 8) with a doorway x -1..1, z 0..3 into
        // room B (y 8..12, its own walls facing in), both rooms' faces facing in
        std::vector<float> av, bv, rv;
        std::vector<uint16_t> ai, bi, ri;
        AddRect(av, ai, 2, 0, -5, 5, 0, 8, 1);       // A's floor, ceiling
        AddRect(av, ai, 2, 4, -5, 5, 0, 8, -1);
        AddRect(av, ai, 0, -5, 0, 8, 0, 4, 1);       // A's sides
        AddRect(av, ai, 0, 5, 0, 8, 0, 4, -1);
        AddRect(av, ai, 1, 8, 0, 4, -5, -1, -1);     // A's back wall round the doorway
        AddRect(av, ai, 1, 8, 0, 4, 1, 5, -1);
        AddRect(av, ai, 1, 8, 3, 4, -1, 1, -1);
        AddRect(bv, bi, 1, 12, 0, 4, -5, 5, -1);     // B's back wall, its floor, its ceiling
        AddRect(bv, bi, 2, 0, -5, 5, 8, 12, 1);
        AddRect(bv, bi, 2, 4, -5, 5, 8, 12, -1);
        AddRect(rv, ri, 2, 6, -6, 6, -1, 40, 1);     // the shell's roof slab above both and on, facing up (out)
        st::RoomMesh a, b, roof;
        const auto all = [](const std::vector<uint16_t>& i) { return std::vector<std::pair<uint32_t, uint32_t>>{ { 0u, static_cast<uint32_t>(i.size()) } }; };
        CHECK(a.Build(av.data(), static_cast<uint32_t>(av.size() / 3), ai.data(), static_cast<uint32_t>(ai.size()), all(ai)));
        CHECK(b.Build(bv.data(), static_cast<uint32_t>(bv.size() / 3), bi.data(), static_cast<uint32_t>(bi.size()), all(bi)));
        CHECK(roof.Build(rv.data(), static_cast<uint32_t>(rv.size() / 3), ri.data(), static_cast<uint32_t>(ri.size()), all(ri)));
        st::Sight s;
        // straight through the doorway: before 2026-10-22 its rays met nothing of A (neither); now they meet B from inside
        const st::SeeWindow door = MakeWindow(0, -0.2f, 1.5f, 0, -1, 0, 0.8f, 1.2f, 0, 1);
        const float street[3] = { 0, -15, 1.5f };
        st::LookThrough(door, street, a, nullptr, s);
        CHECK(s.none == 9 && !st::SightOpen(s, false));
        const st::SightMesh rooms[2] = { { &a, false }, { &b, false } };
        st::LookIn(door, street, rooms, 2, nullptr, s);
        CHECK(s.front == 9 && st::SightOpen(s, false));
        // a window half over A's ceiling (z 3.1 .. 4.1 at the front, the eye 3 yd up): the lower rows see A's back wall,
        // the upper row goes over the ceiling. The rule before: six see it, three meet nothing of it, so it opened; now
        // those three meet the roof from inside (or, with none modelled, nothing at all): against, so it's shut
        const st::SeeWindow over = MakeWindow(0, -0.2f, 3.6f, 0, -1, 0, 0.6f, 1.0f, 0, 15);
        const float low[3] = { 0, -15, 3 };
        st::LookThrough(over, low, a, nullptr, s);
        CHECK(s.front == 6 && s.none == 3 && s.back == 0 && st::SightOpen(s, false));
        const st::SightMesh roofed[3] = { { &a, false }, { &b, false }, { &roof, true } };
        st::LookIn(over, low, roofed, 3, nullptr, s);
        CHECK(s.front == 6 && s.back == 3 && s.none == 0 && !st::SightOpen(s, false));
        CHECK(s.missHit == st::RoomMesh::kBack && s.missDepth > 0.0f);
        st::LookIn(over, low, rooms, 2, nullptr, s);   // (no roof: the void)
        CHECK(s.front == 6 && s.back == 3 && !st::SightOpen(s, false));
        // a dormer wholly over the ceiling: nothing of it seen, all against
        const st::SeeWindow dormer = MakeWindow(0, -0.2f, 5.0f, 0, -1, 0, 0.6f, 0.5f, 0, 16);
        st::LookIn(dormer, low, roofed, 3, nullptr, s);
        CHECK(s.front == 0 && s.back == 9);
        // the building's outside from outside past an opening: neither (a roof seen from above)
        const float high[3] = { 0, -6, 30 };
        const st::SeeWindow skylight = MakeWindow(0, 4, 6.2f, 0, 0, 1, 1, 1, 0, 17);   // (a pane over the roof, facing up)
        st::LookIn(skylight, high, roofed, 3, nullptr, s);
        CHECK(s.none == 9);
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

    Test("DeferrableWhole (SeeThroughRedraw 0): as Deferrable, and none of its windows shut (its glass goes see-through as one)", [] {
        st::BatchWindows b;
        b.valid = true;
        b.windows = { 0, 2 };
        const uint8_t open2[3] = { st::kOpened, st::kShut, st::kOpened };
        const uint8_t oneUnseen[3] = { st::kOpened, st::kShut, st::kUnseen };
        const uint8_t oneShut[3] = { st::kOpened, st::kOpened, st::kShut };
        const uint8_t none[3] = { st::kUnseen, st::kOpened, st::kUnseen };
        CHECK(st::DeferrableWhole(b, open2, 3));       // window 1 isn't the batch's
        CHECK(st::DeferrableWhole(b, oneUnseen, 3));
        CHECK(!st::DeferrableWhole(b, oneShut, 3));    // the whole glass would show the void behind its shut window
        CHECK(!st::DeferrableWhole(b, none, 3));
        CHECK(!st::DeferrableWhole(b, open2, 2));
        b.strayArea = st::kTrimArea * 2.0f;
        CHECK(!st::DeferrableWhole(b, open2, 3));
    });

    Test("SplitByWindow: the open windows' triangles apart from the rest (shut, unseen, strays), as the batch's own indices", [] {
        // two windows side by side (x 0 and 4), a triangle in each and a stray between them
        const st::SeeWindow w[2] = { MakeWindow(0, 0, 2, 0, -1, 0, 1, 1, 1, 1), MakeWindow(4, 0, 2, 0, -1, 0, 1, 1, 1, 2) };
        const float v[] = { -0.5f, 0, 1.5f, 0.5f, 0, 1.5f, 0, 0, 2.5f,    3.5f, 0, 1.5f, 4.5f, 0, 1.5f, 4, 0, 2.5f,    1.8f, 0, 2, 2.2f, 0, 2, 2, 0, 2.4f };
        const uint16_t idx[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
        const st::BatchWindows b = st::WindowsOfBatch(idx, 9, v, 9, 0, 9, w, 2);
        CHECK(b.valid && b.window == std::vector<int16_t>({ 0, 1, -1 }) && b.indices.size() == 9);
        std::vector<uint16_t> open, rest;
        const uint8_t firstOpen[2] = { st::kOpened, st::kShut };
        st::SplitByWindow(b, firstOpen, 2, open, rest);
        CHECK(open == std::vector<uint16_t>({ 0, 1, 2 }) && rest == std::vector<uint16_t>({ 3, 4, 5, 6, 7, 8 }));
        const uint8_t bothOpen[2] = { st::kOpened, st::kOpened };
        st::SplitByWindow(b, bothOpen, 2, open, rest);
        CHECK(open == std::vector<uint16_t>({ 0, 1, 2, 3, 4, 5 }) && rest == std::vector<uint16_t>({ 6, 7, 8 }));
        const uint8_t unseen[2] = { st::kUnseen, st::kShut };
        st::SplitByWindow(b, unseen, 2, open, rest);
        CHECK(open.empty() && rest.size() == 9);
        st::SplitByWindow(st::BatchWindows{}, bothOpen, 2, open, rest);
        CHECK(open.empty() && rest.empty());
    });

    Test("SightOpen, Hold: to open five rays seeing the room (four before 2026-10-22) and more than meet it from outside or "
         "nothing; to stay three; a change stands after kGateHold seconds", [] {
        auto sight = [](int f, int b, int n) { st::Sight s; s.front = f; s.back = b; s.none = n; s.rays = f + b + n; return s; };
        CHECK(st::SightOpen(sight(9, 0, 0), false) && st::SightOpen(sight(5, 1, 3), false) && st::SightOpen(sight(5, 2, 2), false));
        CHECK(!st::SightOpen(sight(3, 0, 6), false));    // a dormer's: mostly over the room
        CHECK(!st::SightOpen(sight(4, 2, 3), false));    // a dormer's from high and far: four seeing in aren't a majority
        CHECK(st::SightOpen(sight(4, 2, 3), true));      // (once open, it would stay)
        CHECK(!st::SightOpen(sight(4, 1, 4), false));    // ... no more seeing it than meeting nothing
        CHECK(!st::SightOpen(sight(5, 3, 1), false));    // three meet it from outside
        CHECK(!st::SightOpen(sight(4, 5, 0), false) && !st::SightOpen(sight(4, 5, 0), true));   // more from outside than in
        CHECK(st::SightOpen(sight(3, 1, 5), true) && !st::SightOpen(sight(3, 1, 5), false));    // stays open, wouldn't open
        CHECK(st::SightOpen(sight(4, 3, 2), true) && !st::SightOpen(sight(2, 0, 7), true) && !st::SightOpen(sight(3, 0, 6), true));
        st::Gate g;
        CHECK(st::Hold(g, true, 0.016f));                // the first verdict stands at once
        CHECK(st::Hold(g, false, 0.1f) && st::Hold(g, false, 0.1f) && st::Hold(g, false, 0.1f));   // 0.3 s: still open
        CHECK(!st::Hold(g, false, 0.1f));                // 0.4 s: shut
        CHECK(!st::Hold(g, true, 0.2f) && !st::Hold(g, false, 0.016f) && !st::Hold(g, true, 0.2f));   // a blip starts it again
        CHECK(st::Hold(g, true, 0.2f));                  // 0.4 s since: open
    });

    Test("GlassSightPoints: an arched window's points on its glass, off its edge, the middle's first; none without glass", [] {
        // a window 2 yd wide, 3 tall about (0, 0, 2) facing -y; its glass a square below and a fan of triangles over it (the arch)
        const st::SeeWindow w[2] = { MakeWindow(0, 0, 2, 0, -1, 0, 1.0f, 1.5f, 1, 1), MakeWindow(10, 0, 2, 0, -1, 0, 1, 1, 1, 2) };
        std::vector<float> v = { -1, 0, 0.5f, 1, 0, 0.5f, 1, 0, 2.5f, -1, 0, 2.5f };   // the square (z 0.5 .. 2.5)
        std::vector<uint16_t> idx = { 0, 1, 2, 0, 2, 3 };
        const int fan = 6;
        v.insert(v.end(), { 0, 0, 2.5f });                                              // the fan's hub
        for (int k = 0; k <= fan; ++k)
        {
            const float a = 3.14159265f * k / fan;
            v.insert(v.end(), { std::cos(a), 0, 2.5f + std::sin(a) });
        }
        for (int k = 0; k < fan; ++k) idx.insert(idx.end(), { 4, static_cast<uint16_t>(5 + k), static_cast<uint16_t>(6 + k) });
        std::vector<st::SightPoints> pts;
        st::GlassSightPoints(v.data(), static_cast<uint32_t>(v.size() / 3), idx.data(), static_cast<uint32_t>(idx.size()),
                             { { 0u, static_cast<uint32_t>(idx.size()) } }, w, 2, pts);
        CHECK(pts.size() == 2 && pts[0].n == st::kSightPoints && pts[1].n == 0);
        bool inside = true, offEdge = true;
        for (int k = 0; k < pts[0].n; ++k)
        {
            const float* p = pts[0].p[k];
            const float x = p[0], z = p[2];
            inside = inside && (z <= 2.5f ? std::fabs(x) <= 1.0f : x * x + (z - 2.5f) * (z - 2.5f) <= 1.0f);   // on the glass
            offEdge = offEdge && std::fabs(x) <= 0.6f + 1e-4f && std::fabs(z - 2.0f) <= 0.9f + 1e-4f;           // its middle 60%
        }
        const float* first = pts[0].p[0];
        CHECK(inside && offEdge && std::fabs(first[0]) < 0.2f && std::fabs(first[2] - 2.0f) < 0.25f);
    });

    Test("BlendFactor: the whole-glass redraw's opacity as a byte in all four channels, clamped; not finite: opaque", [] {
        CHECK(st::BlendFactor(0.35f) == 0x59595959u);
        CHECK(st::BlendFactor(0.0f) == 0u);
        CHECK(st::BlendFactor(1.0f) == 0xFFFFFFFFu);
        CHECK(st::BlendFactor(-2.0f) == 0u);
        CHECK(st::BlendFactor(7.0f) == 0xFFFFFFFFu);
        CHECK(st::BlendFactor(std::numeric_limits<float>::quiet_NaN()) == 0xFFFFFFFFu);
        CHECK(st::BlendFactor(0.5f) == 0x80808080u);
    });

    Test("OpenBits: the lowest open ordinal and a bit for each open window within 16 of it; shut, unseen and other batches' none", [] {
        std::vector<st::SeeWindow> w;
        for (uint32_t o : { 7u, 3u, 5u, 40u, 9u, 31u }) w.push_back(MakeWindow(0, 0, 0, 0, -1, 0, 1, 1, 1, o));
        st::BatchWindows b;
        b.valid = true;
        b.windows = { 0, 1, 2, 3, 5 };   // window 4 (ordinal 9) another batch's
        const uint8_t states[6] = { st::kOpened, st::kShut, st::kOpened, st::kOpened, st::kOpened, st::kOpened };
        float first = -1;
        uint32_t bits = 0;
        st::OpenBits(b, w.data(), states, w.size(), first, bits);
        // open: 7, 5, 40, 31 (3 shut, 9 not the batch's); from 5: 7 -> bit 2, 5 -> bit 0, 31 -> bit 26 (past 16: opaque), 40 past
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
        CHECK(Close(c[0], 0.65f, 1e-6f) && c[1] == -1.0f && c[2] == 1.0f && c[3] == 1539.5f);   // the bits + 0.5
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
        CHECK(c[0] == 0.0f && c[2] == 0.0f && c[3] == 65535.5f);                        // 16 bits
    });

    Test("OpenBit: the shader's test of a window's bit holds with its exp 2^-20 off either way; the old one shut the first open window", [] {
        // every 16-bit pattern's every bit (a sample of patterns: all ones, single bits, the inn's, random), from 3 below the
        // first ordinal to 4 past the last, with exp exact and a hair off
        std::vector<uint32_t> patterns = { 0xFFFFu, 0x0603u, 0x0001u, 0x8000u, 0x0801u, 0x5555u, 0xAAAAu };
        uint32_t seed = 99;
        for (int i = 0; i < 300; ++i) { seed = seed * 1664525u + 1013904223u; patterns.push_back((seed >> 9) & 0xFFFFu); }
        int wrong = 0, checked = 0;
        for (const uint32_t bits : patterns)
            for (const float first : { 1.0f, 40.0f, 5000.0f })
            {
                float c[4];
                st::ReplayConstants(0.35f, false, 0, 0, first, bits, c);
                for (int k = -3; k < static_cast<int>(st::kOpenBits) + 4; ++k)
                    for (const float err : { 1.0f, 1.0f - 1.0f / 1048576.0f, 1.0f + 1.0f / 1048576.0f })
                    {
                        const bool want = k >= 0 && k < static_cast<int>(st::kOpenBits) && ((bits >> k) & 1u);
                        wrong += st::OpenBit(first + static_cast<float>(k), c, err) != want ? 1 : 0;
                        ++checked;
                    }
            }
        std::printf("  %d of %d bit tests wrong\n", wrong, checked);
        CHECK(checked > 50000 && wrong == 0);
        // the old encoding (c219.w the bits, no half): the first open window's value sat on 0.5, and an exp a hair short of
        // 2^-1 shut it
        const float old[4] = { 0.65f, -1.0f, 3.0f, 2049.0f };   // windows 3 and 14 open, as the inn's west front in game
        auto oldBit = [&](float ordinal, float err) {
            const float k = ordinal - old[2];
            const float v = std::exp2(-(std::max(k, -1.0f) + 1.0f)) * err * old[3];
            return v - std::floor(v) - 0.5f >= 0.0f;
        };
        CHECK(oldBit(3.0f, 1.0f) && !oldBit(3.0f, 1.0f - 1.0f / 1048576.0f) && oldBit(14.0f, 1.0f - 1.0f / 1048576.0f));
        float now[4];
        st::ReplayConstants(0.35f, false, 0, 0, 3.0f, 2049u, now);
        CHECK(st::OpenBit(3.0f, now, 1.0f - 1.0f / 1048576.0f) && st::OpenBit(14.0f, now, 1.0f - 1.0f / 1048576.0f) && !st::OpenBit(9.0f, now));
    });

    Test("Deferral: the pending batch's one draw is deferred (its range, opaque or alpha-keyed); everything else drawn", [] {
        st::Deferral d;
        using D = st::Deferral::Draw;
        // nothing pending: drawn
        CHECK(d.OnDraw(true, 3, 0, 3, 0) == D::Pass && d.Passed() == 0);
        // a deferrable batch: its draw deferred, once, its open windows with it
        d.BeginBatch(true, 120, 186, 1.0f, 0x603u, 7);
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
        CHECK(all[0].first == 1.0f && all[0].bits == 0x603u && all[0].material == 7 && all[1].bits == 0 && all[1].material == 0);   // each its own batch's
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
        // GlassLead.cdbc (2026-10-21: glass::Load reads it too, and a table not copied here falls back to the client's file
        // layer, which a test can't call): the data folder's, else the repo's beside the build (the game's before its stage)
        {
            char exe[MAX_PATH] = {};
            GetModuleFileNameA(nullptr, exe, MAX_PATH);
            const fs::path lead = fs::exists(fs::path(data) / "GlassLead.cdbc") ? fs::path(data) / "GlassLead.cdbc"
                                                                               : fs::path(exe).parent_path() / ".." / ".." / "repo" / "data" / "GlassLead.cdbc";
            std::printf("  GlassLead.cdbc from %s\n", lead.string().c_str());
            copied = copied && fs::copy_file(lead, db / "GlassLead.cdbc", fs::copy_options::overwrite_existing, ec);
        }
        const fs::path cwd = fs::current_path();
        if (copied) fs::current_path(root);
        st::RootWindows inn, innInside;
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
            // from inside (SeeThroughFromInside): its rooms' own windows, the same table's rows of interior groups: the
            // kitchen's eight (the ground floor's four and the stair hall's four), exterior02's four, the hall's, the common
            // room's, room01's two and room02's; none in the shell
            innInside = st::BuildInside(path, all, 12);
            std::printf("  the inn from inside: %zu windows in its rooms\n", innInside.windows.size());
            CHECK(innInside.windows.size() == 17 && innInside.real == 0 && innInside.fake == 0);
            const uint32_t expect[12] = { 0, 0, 1, 0, 0, 8, 4, 1, 2, 1, 0, 0 };
            for (uint32_t g = 0; g < 12; ++g)
            {
                uint32_t k = 0;
                const st::SeeWindow* in = innInside.Group(g, k);
                CHECK(k == expect[g] && (k == 0) == (in == nullptr));
                for (uint32_t i = 0; i < k; ++i) CHECK(in[i].interior == -1 && in[i].group == g && (i == 0 || in[i].ordinal > in[i - 1].ordinal));
            }
            uint32_t k = 0;
            const st::SeeWindow* kitchen = innInside.Group(5, k);
            CHECK(kitchen[0].ordinal == 18 && kitchen[7].ordinal == 25);
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
            Test("the scan's inn from the street, the rule before the gate (SeesRoom over its real groups, the glass aside)", [&] {
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
                // the ground floor's front windows (the inn's -y side: north, the road) open head on; the back dormers (z 15.5)
                // never from the street; the inn's batch still goes see-through with them shut (Deferrable)
                CHECK(headOn[0] > 0 && headOn[1] > 0);
                CHECK(opens[14] == 0 && opens[15] == 0 && opens[16] == 0);
                // The west front as the user stood before it in game (2026-10-21: Elwynn 42.9, 66.0, about 21 yd west of the
                // inn's origin: its +x side; the camera 8-10 yd out, low, looking up at the arched upstairs window, 9): the
                // ground floor's (3), the arched one (9) and the small one over it (14) all open. With every one of the nine
                // rays required, 9 never opened (its arch's corner meets the wall from outside, two rays leave the room by
                // the stair's opening).
                const float eyes[4][3] = { { 24, 7, 2 }, { 23, 9, 1.5f }, { 25, 6, 4 }, { 30, 9, 6 } };
                for (const auto& eye : eyes)
                {
                    std::string seen;
                    for (const uint32_t ordinal : { 3u, 9u, 14u })
                    {
                        st::Sight s;
                        const bool open = st::SeesRoom(w[ordinal - 1], eye, rooms.at(w[ordinal - 1].interior), &s);
                        char one[96];
                        std::snprintf(one, sizeof(one), " %u %s (%d front, %d back, %d none)", ordinal, open ? "open" : "SHUT", s.front, s.back, s.none);
                        seen += one;
                        CHECK(open);
                    }
                    std::printf("  from (%.0f, %.0f, %.1f):%s\n", eye[0], eye[1], eye[2], seen.c_str());
                }
                CHECK(headOn[8] > 0 && opens[8] >= 20);
                std::vector<uint8_t> states(n, st::kShut);
                states[0] = states[1] = st::kOpened;
                st::BatchWindows b;
                b.valid = true;
                for (uint16_t k = 0; k < n; ++k) b.windows.push_back(k);
                CHECK(st::Deferrable(b, states.data(), states.size()));
            });
        if (!scan.empty())
            Test("the scan's inn, the gate (2026-10-21; 2026-10-22 the rays followed on through the building): rays through its glass, a "
                 "surface from behind or the void against, the verdict held; the street sweep, high and close before the dormer front, the "
                 "user's viewpoint, walks past the fronts", [&] {
                const std::string base = (fs::path(scan) / "world#wmo#azeroth#buildings#goldshireinn#goldshireinn").string();
                const std::vector<std::string> textures = ReadRootTextures(base + ".wmo");
                std::vector<bool> glass(textures.size());
                for (size_t i = 0; i < textures.size(); ++i) glass[i] = gl::MaterialOf(textures[i].c_str()) != 0;
                // the rooms as the rule of 2026-10-21 took them (their glass aside), and the building as now (MeshOf: every
                // group, every batch, its glass tagged)
                std::map<int, st::RoomMesh> rooms;
                for (const st::SeeWindow& w : inn.windows)
                    if (!rooms.count(w.interior))
                    {
                        char name[16];
                        std::snprintf(name, sizeof(name), "_%03d.wmo", w.interior);
                        CHECK(BuildMesh(ReadScanGroup(base + name), glass, rooms[w.interior]));
                    }
                std::vector<st::RoomMesh> full(12);
                std::vector<st::SightMesh> building;
                for (int g = 0; g < 12; ++g)
                {
                    char name[16];
                    std::snprintf(name, sizeof(name), "_%03d.wmo", g);
                    CHECK(BuildFull(ReadScanGroup(base + name), glass, full[static_cast<size_t>(g)]));
                    building.push_back({ &full[static_cast<size_t>(g)], (kInnGroups[g].flags & st::client::kExteriorGroup) != 0 });
                }
                uint32_t n = 0;
                const st::SeeWindow* w = inn.Group(4, n);
                CHECK(w && n == 17);
                // each window's sight points from group 4's glass (as the runtime takes them from the client's copy)
                const ScanGroup g4 = ReadScanGroup(base + "_004.wmo");
                std::vector<std::pair<uint32_t, uint32_t>> glassRanges;
                for (const ScanGroup::Moba& m : g4.batches)
                    if (m.material < glass.size() && glass[m.material]) glassRanges.emplace_back(m.start, m.count);
                std::vector<st::SightPoints> points;
                st::GlassSightPoints(g4.vertices.data(), static_cast<uint32_t>(g4.vertices.size() / 3), g4.indices.data(),
                                     static_cast<uint32_t>(g4.indices.size()), glassRanges, w, n, points);
                int withPoints = 0, fewest = st::kSightPoints;
                bool onGlass = true;
                for (uint32_t i = 0; i < n; ++i)
                {
                    withPoints += points[i].n == st::kSightPoints ? 1 : 0;
                    fewest = std::min(fewest, points[i].n);
                    for (int k = 0; k < points[i].n; ++k) onGlass = onGlass && st::WindowOf(points[i].p[k], w, n) == static_cast<int>(i);
                }
                std::printf("  sight points: %d of %u windows with %d on their glass (the fewest %d), every one in its own window: %s\n", withPoints, n,
                            st::kSightPoints, fewest, onGlass ? "yes" : "no");
                CHECK(withPoints >= 12 && fewest >= 4 && onGlass);
                // the two rules: 2026-10-21's (the room alone, nothing of it neither) and now's (followed on through the building)
                auto then = [&](uint32_t i, const float eye[3], st::Sight& s) { st::LookThrough(w[i], eye, rooms.at(w[i].interior), &points[i], s); };
                auto now = [&](uint32_t i, const float eye[3], st::Sight& s) { st::LookIn(w[i], eye, building.data(), building.size(), &points[i], s); };
                // (the verdict of 2026-10-21: four seeing in opened)
                auto openBefore = [](const st::Sight& s, bool was) {
                    if (!(s.front > s.back)) return false;
                    if (was) return s.front >= st::kStayFront && s.back <= st::kStayBack && s.front + st::kStayNone >= s.none;
                    return s.front >= 4 && s.back <= st::kOpenBack && s.front > s.none;
                };
                // A sweep: in front of each window from each of `dists` yd out, `degs` degrees aside, `zs` yd up (the ground at
                // about 0, the ground floor at 0.4): of the views facing it, how many open it by each rule, and now's f/b/n
                struct Tally { int views = 0, then = 0, now = 0; std::map<std::string, int> combos; };
                auto sweep = [&](const std::vector<uint32_t>& ordinals, std::initializer_list<float> dists, std::initializer_list<float> degs,
                                 std::initializer_list<float> zs, std::vector<Tally>& out) {
                    out.assign(n, Tally{});
                    for (const uint32_t o : ordinals)
                    {
                        const uint32_t i = o - 1;
                        const float nx = w[i].normal[0], ny = w[i].normal[1], l = std::sqrt(nx * nx + ny * ny);
                        if (l < 0.5f) continue;
                        for (const float r : dists)
                            for (const float deg : degs)
                                for (const float z : zs)
                                {
                                    const float a = deg * 3.14159265f / 180.0f;
                                    const float dx = (nx * std::cos(a) - ny * std::sin(a)) / l, dy = (ny * std::cos(a) + nx * std::sin(a)) / l;
                                    const float eye[3] = { w[i].center[0] + r * dx, w[i].center[1] + r * dy, z };
                                    if (!st::Facing(w[i], eye)) continue;
                                    Tally& t = out[i];
                                    ++t.views;
                                    st::Sight s;
                                    then(i, eye, s);
                                    t.then += openBefore(s, false) ? 1 : 0;
                                    now(i, eye, s);
                                    const bool open = st::SightOpen(s, false);
                                    t.now += open ? 1 : 0;
                                    ++t.combos[std::to_string(s.front) + "/" + std::to_string(s.back) + "/" + std::to_string(s.none) + (open ? "+" : "")];
                                }
                    }
                };
                auto print = [&](const char* what, const std::vector<uint32_t>& ordinals, const std::vector<Tally>& t) {
                    std::printf("  %s:\n", what);
                    for (const uint32_t o : ordinals)
                    {
                        const Tally& x = t[o - 1];
                        std::string top;
                        int shown = 0;
                        for (const auto& kv : x.combos)
                            if (shown++ < 10) top += " " + kv.first + ":" + std::to_string(kv.second);
                        std::printf("    window %2u (z %4.1f, room %d): open from %3d of %3d views by the rule of 2026-10-21, %3d now; f/b/n now:%s\n", o,
                                    w[o - 1].center[2], w[o - 1].interior, x.then, x.views, x.now, top.c_str());
                    }
                };
                std::vector<uint32_t> every;
                for (uint32_t o = 1; o <= n; ++o) every.push_back(o);
                std::vector<Tally> street;
                sweep(every, { 5, 8, 12, 18, 24, 30 }, { -60, -40, -20, 0, 20, 40, 60 }, { 2, 3.5f, 5, 6.5f, 8, 11 }, street);
                print("the street sweep (5 .. 30 yd out, 60 degrees aside, the eye 2 .. 11 yd up)", every, street);
                // high and close before the dormer front (the +y side: 4, 5, 6 and the dormers 15 .. 17): 4 .. 12 yd out, a mounted
                // and zoomed-out camera 9 .. 15 yd up, and farther out at those heights
                const std::vector<uint32_t> front = { 4, 5, 6, 15, 16, 17 };
                std::vector<Tally> high, distant;
                sweep(front, { 4, 6, 8, 10, 12 }, { -60, -40, -20, 0, 20, 40, 60 }, { 9, 11, 13, 15 }, high);
                print("high and close before the dormer front (4 .. 12 yd out, 9 .. 15 yd up)", front, high);
                sweep(front, { 18, 24, 30, 36 }, { -60, -40, -20, 0, 20, 40, 60 }, { 9, 11, 13, 15 }, distant);
                print("high and farther out (18 .. 36 yd, 9 .. 15 yd up)", front, distant);
                // the west front's (3, 9, 14) and the north front's (1, 2, 10, 11) open across the street sweep (3 and 9 from nine
                // in ten views, the rest from seven); the dormers (15 .. 17) never, from the street, high and close or high and far
                for (const uint32_t o : { 3u, 9u })
                    CHECK(street[o - 1].now * 10 >= street[o - 1].views * 9);
                for (const uint32_t o : { 1u, 2u, 10u, 11u, 14u })
                    CHECK(street[o - 1].now * 10 >= street[o - 1].views * 7);
                for (const uint32_t o : { 15u, 16u, 17u })
                    CHECK(street[o - 1].now == 0 && high[o - 1].now == 0 && distant[o - 1].now == 0);
                // The user's viewpoint (2026-10-22, the panel: the camera in the WMO's space 6.39, 48.03, 14.47; 4, 5, 6 open with
                // nine of nine seeing the room; 15 shut with 4 / 1 / 4, 16 and 17 open with 5 / 0 / 4 seeing in, from outside,
                // nothing): the rule of 2026-10-21 here gives what the panel showed; now 15 .. 17 are shut, 4 .. 6 open.
                const float user[3] = { 6.39f, 48.03f, 14.47f };
                for (const uint32_t o : front)
                {
                    st::Sight a, b;
                    then(o - 1, user, a);
                    now(o - 1, user, b);
                    std::printf("  from the user's viewpoint: window %2u by the rule of 2026-10-21 %d / %d / %d (%s), now %d / %d / %d (%s)\n", o, a.front,
                                a.back, a.none, openBefore(a, false) ? "open" : "shut", b.front, b.back, b.none, st::SightOpen(b, false) ? "open" : "shut");
                    const bool dormer = o >= 15;
                    CHECK(st::SightOpen(b, false) == !dormer && (!dormer || !st::SightOpen(b, true)));
                }
                // Slow walks (7 yd a second, a frame each 1/60 s) past the fronts to 60 degrees aside: the west and north fronts on
                // foot (3 yd up) and mounted (7), 8 and 14 yd out; the dormer front at 3, 9, 12 and 15 yd up, 8 and 12 yd out and
                // at the user's 32.5 yd. Each window's verdict held as the runtime holds it, its changes counted, and the rule of
                // 2026-10-21's for comparison.
                struct Walk { std::string name; float out, z; std::vector<uint32_t> windows; };
                std::vector<Walk> walks = { { "the west front 8 yd out", 8, 3, { 3, 9, 14 } }, { "the west front 14 yd out", 14, 3, { 3, 9, 14 } },
                                            { "the west front 8 yd out, mounted", 8, 7, { 3, 9, 14 } }, { "the west front 14 yd out, mounted", 14, 7, { 3, 9, 14 } },
                                            { "the north front 8 yd out", 8, 3, { 1, 2, 10, 11 } }, { "the north front 14 yd out", 14, 3, { 1, 2, 10, 11 } },
                                            { "the north front 14 yd out, mounted", 14, 7, { 1, 2, 10, 11 } } };
                for (const float out : { 8.0f, 12.0f, 32.5f })
                    for (const float z : { 3.0f, 9.0f, 12.0f, 15.0f })
                        walks.push_back({ "the dormer front " + std::to_string(static_cast<int>(out)) + " yd out, " + std::to_string(static_cast<int>(z)) + " yd up",
                                          out, z, front });
                std::map<uint32_t, int> flips, flipsThen, openFrames, frames;
                for (const Walk& walk : walks)
                {
                    const st::SeeWindow& f = w[walk.windows[0] - 1];
                    float c[2] = { 0, 0 };
                    for (const uint32_t o : walk.windows) { c[0] += w[o - 1].center[0]; c[1] += w[o - 1].center[1]; }
                    c[0] /= walk.windows.size(); c[1] /= walk.windows.size();
                    const float nx = f.normal[0], ny = f.normal[1], l = std::sqrt(nx * nx + ny * ny);
                    const float ox = nx / l, oy = ny / l, ax = -oy, ay = ox;
                    for (const uint32_t o : walk.windows)
                    {
                        st::Gate gate, gateThen;
                        bool last = false, lastThen = false, first = true;
                        const float step = 7.0f / 60.0f;
                        const float span = walk.out * 1.7320508f;   // (tan 60 degrees)
                        for (float s = -span; s <= span; s += step)
                        {
                            const float eye[3] = { c[0] + ox * walk.out + ax * s, c[1] + oy * walk.out + ay * s, walk.z };
                            const st::SeeWindow& win = w[o - 1];
                            if (!st::Facing(win, eye)) { first = true; gate = st::Gate{}; gateThen = st::Gate{}; continue; }
                            st::Sight a, b;
                            then(o - 1, eye, a);
                            now(o - 1, eye, b);
                            const bool heldThen = st::Hold(gateThen, openBefore(a, gateThen.seen && gateThen.open), 1.0f / 60.0f);
                            const bool held = st::Hold(gate, st::SightOpen(b, gate.seen && gate.open), 1.0f / 60.0f);
                            if (!first)
                            {
                                flips[o] += held != last ? 1 : 0;
                                flipsThen[o] += heldThen != lastThen ? 1 : 0;
                            }
                            last = held;
                            lastThen = heldThen;
                            first = false;
                            ++frames[o];
                            openFrames[o] += held ? 1 : 0;
                        }
                    }
                }
                for (const uint32_t o : { 3u, 9u, 14u, 1u, 2u, 10u, 11u, 4u, 5u, 6u, 15u, 16u, 17u })
                    std::printf("  walking past window %2u: open %d of %d frames; its verdict changed %d times (by the rule of 2026-10-21: %d)\n", o,
                                openFrames[o], frames[o], flips[o], flipsThen[o]);
                // 9, 14 and the north front's windows open all the way (none changes its verdict); 3, the shuttered ground-floor
                // window, shuts past about 50 degrees on its one side, where its deep reveal hides the room, a few times at
                // most; the dormers never open, so never flip
                for (const uint32_t o : { 9u, 14u, 1u, 2u, 10u, 11u })
                    CHECK(flips[o] == 0 && openFrames[o] == frames[o]);
                CHECK(flips[3] <= 4 && openFrames[3] * 10 >= frames[3] * 9);
                for (const uint32_t o : { 15u, 16u, 17u })
                    CHECK(frames[o] > 0 && openFrames[o] == 0 && flips[o] == 0);
            });
        if (!scan.empty())
            Test("the scan's inn from inside (2026-10-21; 2026-10-22 the building's every group): its rooms' windows open as ways out where "
                 "the street is seen through them (LookOut); the views the client would push; the street behind them inside the rectangle the "
                 "outdoors is culled through", [&] {
                CHECK(innInside.windows.size() == 17);
                const std::string base = (fs::path(scan) / "world#wmo#azeroth#buildings#goldshireinn#goldshireinn").string();
                const std::vector<std::string> textures = ReadRootTextures(base + ".wmo");
                std::vector<bool> glass(textures.size());
                for (size_t i = 0; i < textures.size(); ++i) glass[i] = gl::MaterialOf(textures[i].c_str()) != 0;
                // the building as the runtime takes it (MeshOf, BuildingOf: every group, every batch, its glass tagged; 2026-10-22)
                std::vector<st::RoomMesh> full(12);
                std::vector<st::SightMesh> building;
                std::vector<ScanGroup> scanned(12);
                for (int g = 0; g < 12; ++g)
                {
                    char name[16];
                    std::snprintf(name, sizeof(name), "_%03d.wmo", g);
                    scanned[static_cast<size_t>(g)] = ReadScanGroup(base + name);
                    CHECK(BuildFull(scanned[static_cast<size_t>(g)], glass, full[static_cast<size_t>(g)]));
                    building.push_back({ &full[static_cast<size_t>(g)], (kInnGroups[g].flags & st::client::kExteriorGroup) != 0 });
                }
                std::printf("  the building: %zu groups, the shell's group 4 %zu triangles (its glass among them), the kitchen's %zu\n", building.size(),
                            full[4].Triangles(), full[5].Triangles());
                CHECK(full[4].Triangles() > 1000);
                // each room with windows of its own: its surfaces, its windows' sight points (from its glass batches)
                struct Room { std::vector<st::SightPoints> points; const st::SeeWindow* w = nullptr; uint32_t n = 0; };
                std::map<int, Room> rooms;
                for (const int g : { 2, 5, 6, 7, 8, 9 })
                {
                    Room& r = rooms[g];
                    r.w = innInside.Group(static_cast<uint32_t>(g), r.n);
                    CHECK(r.w && r.n > 0);
                    const ScanGroup& sg = scanned[static_cast<size_t>(g)];
                    std::vector<std::pair<uint32_t, uint32_t>> glassRanges;
                    for (const ScanGroup::Moba& b : sg.batches)
                        if (b.material < glass.size() && glass[b.material]) glassRanges.emplace_back(b.start, b.count);
                    st::GlassSightPoints(sg.vertices.data(), static_cast<uint32_t>(sg.vertices.size() / 3), sg.indices.data(),
                                         static_cast<uint32_t>(sg.indices.size()), glassRanges, r.w, r.n, r.points);
                    int fewest = st::kSightPoints;
                    for (const st::SightPoints& p : r.points) fewest = std::min(fewest, p.n);
                    std::printf("  room %d: %u windows, the fewest sight points %d\n", g, r.n, fewest);
                    CHECK(fewest >= 4);
                }
                // eyes all over each room's box (1.5 yd apart, half a yard in from its sides; 1.8, 3 and 4.5 yd over its floor: on
                // foot to a raised camera), each window from those inside its plane within 20 yd
                std::map<uint32_t, std::pair<int, int>> tally;   // ordinal: (open, views)
                std::map<uint32_t, std::map<std::string, int>> combos;
                for (auto& [g, r] : rooms)
                {
                    const st::GroupInfo& box = kInnGroups[g];
                    for (float z0 : { 1.8f, 3.0f, 4.5f })
                        for (float x = box.lo[0] + 0.5f; x <= box.hi[0] - 0.5f; x += 1.5f)
                            for (float y = box.lo[1] + 0.5f; y <= box.hi[1] - 0.5f; y += 1.5f)
                            {
                                const float floorZ = g == 5 ? 0.4f : 7.3f;
                                const float eye[3] = { x, y, floorZ + z0 };
                                for (uint32_t i = 0; i < r.n; ++i)
                                {
                                    const st::SeeWindow& w = r.w[i];
                                    const float d[3] = { eye[0] - w.center[0], eye[1] - w.center[1], eye[2] - w.center[2] };
                                    if (!st::FacingIn(w, eye) || d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > 400.0f) continue;
                                    st::Sight s;
                                    st::LookOut(w, eye, building.data(), building.size(), &r.points[i], s);
                                    const bool open = st::SightOpen(s, false);
                                    tally[w.ordinal].first += open ? 1 : 0;
                                    ++tally[w.ordinal].second;
                                    ++combos[w.ordinal][std::to_string(s.front) + "/" + std::to_string(s.back) + "/" + std::to_string(s.none) + (open ? "+" : "")];
                                }
                            }
                    for (uint32_t i = 0; i < r.n; ++i)
                    {
                        const st::SeeWindow& w = r.w[i];
                        std::string top;
                        int shown = 0;
                        for (const auto& kv : combos[w.ordinal])
                            if (shown++ < 8) top += " " + kv.first + ":" + std::to_string(kv.second);
                        std::printf("  window %2u (room %d, z %4.1f, facing %+.2f %+.2f %+.2f): opens from %3d of %3d eyes inside; f/b/n:%s\n", w.ordinal, g,
                                    w.center[2], w.normal[0], w.normal[1], w.normal[2], tally[w.ordinal].first, tally[w.ordinal].second, top.c_str());
                    }
                }
                // The ground floor's four windows (18 .. 21: out by the shell's panes 1 .. 4) open from nearly everywhere in it;
                // the sloped inner panes under the back dormers (0, 25, 29: up into the dormer, its roof and cheeks from inside)
                // from few places.
                for (const uint32_t o : { 18u, 19u, 20u, 21u })
                    CHECK(tally[o].second > 20 && tally[o].first * 10 >= tally[o].second * 8);
                // From where the GPU test stands (glass_tests' "inside": the kitchen, looking at its south wall): the south windows
                // 18 and 19 open; the views the client would push for them; each one's street (20 yd past its glass points)
                // within the rectangle round the ways out, so the outdoors there is culled in, not out.
                const Room& kitchen = rooms.at(5);
                const float spot[3] = { 0, 6, 3 }, look[3] = { 0, -8.8f, 2.8f };
                float vp[16];
                Camera(spot, look, 2.0f * std::atan(96.0f / 120.0f), 1.0f, vp);
                std::vector<st::WindowPlan> plan;
                unsigned budget = st::kMaxInsidePerFrame;
                st::PlanInside(kitchen.w, kitchen.n, spot, { -1, -1, 1, 1 }, kIdentity, spot, vp, budget, plan);
                float merged[4] = { 1e30f, 1e30f, -1e30f, -1e30f };
                std::vector<uint32_t> opened;
                for (uint32_t i = 0; i < kitchen.n; ++i)
                {
                    if (!plan[i].candidate) continue;
                    st::Sight s;
                    st::LookOut(kitchen.w[i], spot, building.data(), building.size(), &kitchen.points[i], s);
                    if (!st::SightOpen(s, false)) continue;
                    opened.push_back(kitchen.w[i].ordinal);
                    const st::PortalView v = st::ViewOf(plan[i].screen, plan[i].depth);
                    std::printf("  from (0, 6, 3): window %u opens (%d of %d rays see the street), its view %.3f %.3f %.3f %.3f (minY minX maxY maxX), "
                                "depth %.1f\n", kitchen.w[i].ordinal, s.front, s.rays, v.rect[0], v.rect[1], v.rect[2], v.rect[3], v.maxViewDepth);
                    // (CPortalView::Merge: the rectangles' union)
                    merged[0] = std::min(merged[0], v.rect[0]); merged[1] = std::min(merged[1], v.rect[1]);
                    merged[2] = std::max(merged[2], v.rect[2]); merged[3] = std::max(merged[3], v.rect[3]);
                }
                CHECK(std::find(opened.begin(), opened.end(), 18u) != opened.end() && std::find(opened.begin(), opened.end(), 19u) != opened.end());
                int inside = 0, beyond = 0;
                for (uint32_t i = 0; i < kitchen.n; ++i)
                {
                    if (std::find(opened.begin(), opened.end(), kitchen.w[i].ordinal) == opened.end()) continue;
                    const st::SightPoints& p = kitchen.points[i];
                    for (int k = 0; k < p.n; ++k)
                    {
                        float d[3] = { p.p[k][0] - spot[0], p.p[k][1] - spot[1], p.p[k][2] - spot[2] };
                        const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                        const float q[3] = { d[0] / l * (l + 20.0f), d[1] / l * (l + 20.0f), d[2] / l * (l + 20.0f) };
                        float c[4];
                        for (int j = 0; j < 4; ++j) c[j] = q[0] * vp[j] + q[1] * vp[4 + j] + q[2] * vp[8 + j] + vp[12 + j];
                        const float x = (c[0] / c[3] + 1) * 0.5f, y = (c[1] / c[3] + 1) * 0.5f;
                        ++beyond;
                        inside += x >= merged[1] && x <= merged[3] && y >= merged[0] && y <= merged[2] ? 1 : 0;
                    }
                }
                std::printf("  the rectangle round them %.3f %.3f %.3f %.3f: %d of %d points of the street behind them in it\n", merged[0], merged[1],
                            merged[2], merged[3], inside, beyond);
                CHECK(beyond > 0 && inside == beyond);
            });
        else std::printf("SKIP the scan's inn (no wmo-windows folder given)\n");
    }
    else std::printf("SKIP the real tables (no data folder given)\n");

    std::printf("%s: %u passed, %u failed\n", g_failed ? "FAILED" : "PASSED", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
