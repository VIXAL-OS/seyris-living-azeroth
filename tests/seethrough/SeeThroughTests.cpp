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
//  - The buildings (SeeThroughBuildings.cdbc, 2026-10-22) through the real reader; the panel's wrapped text; with
//    SEETHROUGH_SURVEY (a file of WMO paths) a survey of each building printed: the street sweep, high and close, walks
//    past each front, the inside sweep, through each window's near groups (NearGroups).
// Usage:  seethrough_tests <Wow.exe> <repo data folder> [<map scan's wmo-windows folder>]
#include "env/GlassModel.hpp"
#include "env/GlassRooms.hpp"
#include "env/SeeThroughModel.hpp"
#include "env/SkyPath.hpp"
#include "debug/SnapshotView.hpp"
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
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
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
    // Dalaran (2026-10-24, Phase D): the city's one WMO
    const char* const kDalaran = "world\\wmo\\northrend\\dalaran\\nd_dalaran.wmo";
    // Dalaran's rooms added from outside on 2026-10-24 ("yeah add those four"; upstairs 9 and 13 and the Lounge's hall 55 out
    // again the same day: "Curiosities' outside windows seem to flicker a bit"; then by window, the same day, 9, 13 and 55
    // again with the Violet Gate's halls, 53 and 54, and Vargoth's Retreat, 90, their flickering windows shut by rows of their
    // own): the windows into each listed, and where to stand before the Silver Enclave's hall (SEETHROUGH_SPOTS' farther ring:
    // its one window 21.7 yd up)
    constexpr unsigned kInto6 = 4, kInto7 = 3, kInto10 = 4, kInto78 = 1;
    constexpr unsigned kInto9 = 2, kInto13 = 2, kInto55 = 1, kInto53 = 5, kInto54 = 2, kInto90 = 74, kInto89 = 62;
    constexpr float kSpot78[3] = { -70.03f, -11.67f, 2.00f };
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

    // The survey's probe: an eye in the WMO's space (SEETHROUGH_PROBE "x y z", or a snapshot's camera: SEETHROUGH_SNAPSHOT,
    // 2026-10-22): what each window facing it sees from there, from inside (its room's own) and from outside.
    bool g_haveProbe = false;
    float g_probe[3] = {};
    int g_probeSides = 3;   // 1 from outside, 2 from inside (a snapshot's: its walk's side), 3 both (SEETHROUGH_PROBE)

    // --- a survey of buildings (SEETHROUGH_SURVEY: a file of WMO paths, one a line; 2026-10-22, more buildings) ----------
    // Each WMO from the map scan's files with the loaded GlassWindows: its windows with rooms behind them; from outside the
    // street sweep, high and close views, and slow walks past each of its fronts (the held verdict's changes); from inside
    // each room's own windows from eyes all over it. Every window's rays go through its near groups (NearGroups), as the
    // runtime's. The eye stands on the building's own floors and streets where it has them (a city's), else at its lowest.
    struct ScanBuilding
    {
        std::string base;   // the scan's flat path less ".wmo"
        std::vector<st::GroupInfo> groups;
        std::vector<bool> glass;
        std::vector<std::string> names;   // each group's MOGN name (MOGI's name offset), the survey's lines
        std::map<int, ScanGroup> files;
        std::map<int, st::RoomMesh> meshes;
        float lowest = 0.0f;
        const ScanGroup& File(int g)
        {
            auto it = files.find(g);
            if (it == files.end())
            {
                char n[16];
                std::snprintf(n, sizeof(n), "_%03d.wmo", g);
                it = files.emplace(g, fs::exists(base + n) ? ReadScanGroup(base + n) : ScanGroup{}).first;
            }
            return it->second;
        }
        const st::RoomMesh& Mesh(int g)
        {
            auto it = meshes.find(g);
            if (it == meshes.end())
            {
                it = meshes.emplace(g, st::RoomMesh{}).first;
                BuildFull(File(g), glass, it->second);
            }
            return it->second;
        }
        std::vector<st::SightMesh> Near(const st::SeeWindow& w, int room)
        {
            std::vector<uint16_t> ids;
            st::NearGroups(w, room, groups.data(), groups.size(), ids);
            std::vector<st::SightMesh> out;
            for (const uint16_t g : ids) out.push_back({ &Mesh(g), (groups[g].flags & st::client::kExteriorGroup) != 0 });
            return out;
        }
        // The street under a window (a city's: the first upward face below a point 1.5 yd before it, among the exterior
        // groups over it), else the building's lowest point (a house on the terrain). The eyes before it stand on it.
        float StreetOf(const st::SeeWindow& w)
        {
            if (groups.size() < 50) return lowest;
            return StreetAt(w.center[0] + 1.5f * w.normal[0], w.center[1] + 1.5f * w.normal[1], w.center[2]);
        }
        // The street under (x, y) below z: the first upward face among the exterior groups over it (`found`: one was).
        float StreetAt(float x, float y, float z, bool* found = nullptr)
        {
            std::vector<st::SightMesh> over;
            for (size_t g = 0; g < groups.size(); ++g)
                if ((groups[g].flags & st::client::kExteriorGroup) && groups[g].lo[0] <= x && groups[g].hi[0] >= x && groups[g].lo[1] <= y &&
                    groups[g].hi[1] >= y && groups[g].lo[2] < z)
                    over.push_back({ &Mesh(static_cast<int>(g)), true });
            return Ground(x, y, z, over, found);
        }
        // The levels one can stand on under (x, y) from `top` down to `bottom` (2026-10-24, Curiosities & Moore's walkway: the
        // street under a window isn't the only place players stand): every upward face of the exterior groups over it, top
        // first, each with 2.5 yd or more to the level above it.
        std::vector<float> Levels(float x, float y, float top, float bottom)
        {
            std::vector<st::SightMesh> over;
            for (size_t g = 0; g < groups.size(); ++g)
                if ((groups[g].flags & st::client::kExteriorGroup) && groups[g].lo[0] <= x && groups[g].hi[0] >= x && groups[g].lo[1] <= y &&
                    groups[g].hi[1] >= y && groups[g].lo[2] < top)
                    over.push_back({ &Mesh(static_cast<int>(g)), true });
            std::vector<float> out;
            float z = top, above = 1e30f;
            for (int k = 0; k < 8 && z > bottom; ++k)
            {
                bool found = false;
                const float level = Ground(x, y, z, over, &found);
                if (!found || level < bottom) break;
                if (above - level >= 2.5f) out.push_back(level);
                above = level;
                z = level - 0.2f;
            }
            return out;
        }
        // How far the camera gets from `head` toward `to` before a surface of the building (either side) stops it, less 0.4 yd
        // (the client's camera stops at the first wall between it and the player), at least 1 yd.
        float CameraReach(const float head[3], const float to[3])
        {
            float d[3] = { to[0] - head[0], to[1] - head[1], to[2] - head[2] };
            const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len < 1e-3f) return 0.0f;
            for (float& v : d) v /= len;
            float best = len;
            for (size_t g = 0; g < groups.size(); ++g)
            {
                bool overlaps = true;
                for (int k = 0; k < 3; ++k)
                    overlaps = overlaps && std::min(head[k], to[k]) <= groups[g].hi[k] && std::max(head[k], to[k]) >= groups[g].lo[k];
                if (!overlaps) continue;
                float t = 0.0f;
                if (Mesh(static_cast<int>(g)).First(head, d, 0.0f, best, &t) != st::RoomMesh::kNone && t < best) best = t;
            }
            return best < len ? std::max(1.0f, best - 0.4f) : len;
        }
        // The first upward face under (x, y, z) among `near` (a street, a floor), else the building's lowest point.
        float Ground(float x, float y, float z, const std::vector<st::SightMesh>& around, bool* found = nullptr) const
        {
            if (found) *found = false;
            const float o[3] = { x, y, z }, d[3] = { 0, 0, -1 };
            float tmin = 0.0f;
            for (int pass = 0; pass < 8; ++pass)
            {
                float best = 200.0f;
                st::RoomMesh::Hit hit = st::RoomMesh::kNone;
                for (const st::SightMesh& m : around)
                {
                    float t = 0;
                    const auto h = m.mesh->First(o, d, tmin, best, &t);
                    if (h != st::RoomMesh::kNone && t < best) { best = t; hit = h; }
                }
                if (hit == st::RoomMesh::kNone) break;
                if (hit == st::RoomMesh::kFront)
                {
                    if (found) *found = true;
                    return z - best;
                }
                tmin = best + 1e-3f;
            }
            return lowest;
        }
    };

    struct SurveyRow
    {
        std::string wmo;
        unsigned groups = 0, ext = 0, real = 0, fake = 0, inside = 0;
        unsigned streetViews = 0, streetOpen = 0, highViews = 0, highOpen = 0;
        unsigned mostlyOpen = 0, neverOpen = 0, highOnly = 0;
        unsigned walkFrames = 0, walkOpen = 0, flips = 0, flipWindows = 0, maxFlips = 0;
        unsigned walks = 0, flickers = 0;   // walks, and those whose verdict changed more than twice (open, shut, open again)
        // the camera swung round a player standing before a window, on every level there (SEETHROUGH_SWINGS, 2026-10-24): the
        // windows, the runs (a spot and a camera, two swings), those that changed a window more than twice
        unsigned swingWindows = 0, swingRuns = 0, swingFlickerRuns = 0, swingFlickerWindows = 0, swingBlinkRuns = 0, swingBlinkWindows = 0;
        unsigned insideViews = 0, insideOpen = 0, insideWindows = 0, insideMostly = 0, insideNever = 0;
        // walks through each room (2026-10-22, Northshire Abbey: "it does flicker between fake rooms and outside"): the
        // windows' verdicts changed, over how many, at most for one, and the walks that changed one more than twice
        unsigned insideWalks = 0, insideFlips = 0, insideFlipWindows = 0, insideMaxFlips = 0, insideFlickers = 0, insideFrames = 0, insideOpenFrames = 0;
        // between rooms (2026-10-23, Phase C): the windows of interior groups onto another room (RoomBeyond), from eyes in
        // their own group (LookIn), and on the walks through it; a group's line each (the table's choice is per group)
        unsigned betweenWindows = 0, betweenFake = 0, betweenViews = 0, betweenOpen = 0, betweenMostly = 0, betweenNever = 0;
        unsigned betweenFrames = 0, betweenOpenFrames = 0, betweenFlips = 0, betweenMaxFlips = 0, betweenFlickers = 0, betweenWalks = 0;
        std::vector<std::string> betweenLines;
        std::vector<std::string> lines;
        double seconds = 0;
    };

    // A WMO from the map scan's files: its root's MOGI and MOGN, its materials' glass, its lowest exterior point.
    bool LoadScanBuilding(const std::string& scan, const std::string& wmo, ScanBuilding& b)
    {
        std::string flat = wmo;
        std::replace(flat.begin(), flat.end(), '\\', '#');
        b.base = (fs::path(scan) / flat).string();
        b.base.resize(b.base.size() - 4);
        if (!fs::exists(b.base + ".wmo")) return false;
        const std::vector<uint8_t> r = ReadFile(b.base + ".wmo");
        for (const Chunk& c : Chunks(r, 0, r.size()))
            if (c.tag == "MOGI")
                for (size_t at = c.at; at + 32 <= c.at + c.size; at += 32)
                {
                    st::GroupInfo gi;
                    std::memcpy(&gi.flags, &r[at], 4);
                    std::memcpy(gi.lo, &r[at + 4], 12);
                    std::memcpy(gi.hi, &r[at + 16], 12);
                    b.groups.push_back(gi);
                    int32_t nameAt = -1;
                    std::memcpy(&nameAt, &r[at + 28], 4);
                    b.names.push_back(std::to_string(nameAt));   // (resolved against MOGN below)
                }
        for (const Chunk& c : Chunks(r, 0, r.size()))
            if (c.tag == "MOGN")
                for (std::string& n : b.names)
                {
                    const long at = std::stol(n);
                    n.clear();
                    for (size_t k = at >= 0 ? static_cast<size_t>(at) : c.size; k < c.size && r[c.at + k]; ++k) n += static_cast<char>(r[c.at + k]);
                }
        const std::vector<std::string> textures = ReadRootTextures(b.base + ".wmo");
        b.glass.resize(textures.size());
        for (size_t i = 0; i < textures.size(); ++i) b.glass[i] = gl::MaterialOf(textures[i].c_str()) != 0;
        b.lowest = 1e30f;
        for (const st::GroupInfo& g : b.groups)
            if (g.flags & st::client::kExteriorGroup) b.lowest = std::min(b.lowest, g.lo[2]);
        if (b.lowest > 1e29f) b.lowest = 0.0f;
        return true;
    }

    bool SurveyBuilding(const std::string& scan, const std::string& wmo, const std::vector<gl::Window>& all, SurveyRow& row)
    {
        const auto t0 = std::chrono::steady_clock::now();
        row = SurveyRow{};
        row.wmo = wmo;
        ScanBuilding b;
        if (!LoadScanBuilding(scan, wmo, b)) return false;
        row.groups = static_cast<unsigned>(b.groups.size());
        const uint32_t path = gl::PathHash(wmo.c_str());
        st::RootWindows outside = st::BuildRoot(path, all, b.groups.data(), b.groups.size());
        st::MaskWindows(outside, gl::SeeThroughWindowMask);   // (2026-10-24: the loaded table's window rows, for "listed" and the spots)
        // (between rooms: every interior group's windows get the room beyond them with SEETHROUGH_BETWEEN, else the groups
        // the loaded table marks 0x4)
        std::vector<uint8_t> between(b.groups.size(), 0);
        bool anyBetween = false;
        for (size_t g = 0; g < b.groups.size(); ++g)
        {
            between[g] = !(b.groups[g].flags & st::client::kExteriorGroup) &&
                         (std::getenv("SEETHROUGH_BETWEEN") || (gl::SeeThroughGroupFlags(path, static_cast<uint32_t>(g)) & st::kSeeBetween)) ? 1 : 0;
            anyBetween = anyBetween || between[g];
        }
        const st::RootWindows in = anyBetween ? st::BuildInside(path, all, b.groups.data(), b.groups.size(), between)
                                              : st::BuildInside(path, all, b.groups.size());
        row.ext = static_cast<unsigned>(outside.windows.size());
        row.real = outside.real;
        row.fake = outside.fake;
        row.inside = static_cast<unsigned>(in.windows.size());
        const bool verbose = std::getenv("SEETHROUGH_SURVEY_VERBOSE") != nullptr;
        // each group's windows' sight points (its glass batches)
        auto pointsOf = [&](const st::RootWindows& rw, uint32_t g, std::vector<st::SightPoints>& out) {
            uint32_t n = 0;
            const st::SeeWindow* w = rw.Group(g, n);
            out.clear();
            if (!w) return;
            const ScanGroup& sg = b.File(static_cast<int>(g));
            std::vector<std::pair<uint32_t, uint32_t>> ranges;
            for (const ScanGroup::Moba& m : sg.batches)
                if (m.material < b.glass.size() && b.glass[m.material]) ranges.emplace_back(m.start, m.count);
            st::GlassSightPoints(sg.vertices.data(), static_cast<uint32_t>(sg.vertices.size() / 3), sg.indices.data(),
                                 static_cast<uint32_t>(sg.indices.size()), ranges, w, n, out);
        };
        const float dists[] = { 5, 8, 12, 18, 24, 30 }, degs[] = { -60, -40, -20, 0, 20, 40, 60 }, heights[] = { 2, 3.5f, 5, 6.5f, 8, 11 };
        const float closeDists[] = { 4, 6, 8, 10, 12 }, highHeights[] = { 9, 11, 13, 15 };
        for (uint32_t g = 0; g < b.groups.size(); ++g)
        {
            uint32_t n = 0;
            const st::SeeWindow* w = outside.Group(g, n);
            if (!w) continue;
            std::vector<st::SightPoints> points;
            pointsOf(outside, g, points);
            for (uint32_t i = 0; i < n; ++i)
            {
                if (w[i].interior < 0) continue;
                const float nx = w[i].normal[0], ny = w[i].normal[1], l = std::sqrt(nx * nx + ny * ny);
                if (l < 0.5f) continue;   // (a skylight: no street before it)
                const std::vector<st::SightMesh> around = b.Near(w[i], w[i].interior);
                const st::SightPoints* sp = i < points.size() && points[i].n ? &points[i] : nullptr;
                const float street = b.StreetOf(w[i]);
                unsigned views = 0, open = 0, hviews = 0, hopen = 0;
                unsigned rays[3] = {};   // (verbose) the street sweep's rays: seeing in, against, neither
                auto look = [&](float r, float deg, float h, unsigned& v, unsigned& o) {
                    const float a = deg * 3.14159265f / 180.0f;
                    const float dx = (nx * std::cos(a) - ny * std::sin(a)) / l, dy = (ny * std::cos(a) + nx * std::sin(a)) / l;
                    const float x = w[i].center[0] + r * dx, y = w[i].center[1] + r * dy;
                    const float eye[3] = { x, y, street + h };
                    if (!st::Facing(w[i], eye)) return;
                    ++v;
                    st::Sight s;
                    st::LookIn(w[i], eye, around.data(), around.size(), sp, s);
                    o += st::SightOpen(s, false) ? 1 : 0;
                    if (&v == &views) { rays[0] += s.front; rays[1] += s.back; rays[2] += s.none; }
                };
                for (const float r : dists)
                    for (const float deg : degs)
                        for (const float h : heights) look(r, deg, h, views, open);
                for (const float r : closeDists)
                    for (const float deg : degs)
                        for (const float h : highHeights) look(r, deg, h, hviews, hopen);
                row.streetViews += views; row.streetOpen += open; row.highViews += hviews; row.highOpen += hopen;
                if (views && open * 2 >= views) ++row.mostlyOpen;
                if (!open) ++row.neverOpen;
                if (!open && hopen) ++row.highOnly;
                if (verbose)
                {
                    char line[240];
                    const unsigned all = std::max(rays[0] + rays[1] + rays[2], 1u);
                    std::snprintf(line, sizeof(line), "    group %3u window %4u (z %5.1f, room %3d): street %3u of %3u, high and close %3u of %3u; "
                                  "its rays %u%% in, %u%% against, %u%% neither", g, w[i].ordinal, w[i].center[2], w[i].interior, open, views,
                                  hopen, hviews, 100 * rays[0] / all, 100 * rays[1] / all, 100 * rays[2] / all);
                    row.lines.push_back(line);
                }
                // (the probe from outside: this window, facing it within kRange, as the walk from outside decides it)
                if (g_haveProbe && (g_probeSides & 1) && st::Facing(w[i], g_probe))
                {
                    const float d[3] = { g_probe[0] - w[i].center[0], g_probe[1] - w[i].center[1], g_probe[2] - w[i].center[2] };
                    if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] <= st::kRange * st::kRange)
                    {
                        st::Sight s;
                        st::LookIn(w[i], g_probe, around.data(), around.size(), sp, s);
                        char line[240];
                        std::snprintf(line, sizeof(line), "    probe (%.2f %.2f %.2f) from outside: group %3u window %4u (room %3d): %s; %d see a room from inside, "
                                      "%d against or nothing, %d the outside", g_probe[0], g_probe[1], g_probe[2], g, w[i].ordinal, w[i].interior,
                                      st::SightOpen(s, false) ? "open" : "shut", s.front, s.back, s.none);
                        row.lines.push_back(line);
                    }
                }
            }
            // walks past each front of the group (its windows with rooms facing within 30 degrees of each other): 8 and 14 yd
            // out, 3, 7 and 12 yd up, to 60 degrees aside, 7 yd a second, a frame each 1/60 s
            std::vector<bool> walked(n, false);
            for (uint32_t i = 0; i < n; ++i)
            {
                if (walked[i] || w[i].interior < 0) continue;
                const float nx = w[i].normal[0], ny = w[i].normal[1], l = std::sqrt(nx * nx + ny * ny);
                if (l < 0.5f) continue;
                std::vector<uint32_t> front;
                for (uint32_t j = i; j < n; ++j)
                {
                    const float mx = w[j].normal[0], my = w[j].normal[1], lm = std::sqrt(mx * mx + my * my);
                    if (walked[j] || w[j].interior < 0 || lm < 0.5f || (nx * mx + ny * my) / (l * lm) < 0.866f) continue;
                    walked[j] = true;
                    front.push_back(j);
                }
                float c[3] = { 0, 0, 0 };
                for (const uint32_t j : front) for (int k = 0; k < 3; ++k) c[k] += w[j].center[k] / static_cast<float>(front.size());
                const float ox = nx / l, oy = ny / l, ax = -oy, ay = ox;
                for (const uint32_t j : front)
                {
                    const std::vector<st::SightMesh> around = b.Near(w[j], w[j].interior);
                    const st::SightPoints* sp = j < points.size() && points[j].n ? &points[j] : nullptr;
                    const float street = b.StreetOf(w[j]);
                    unsigned flips = 0, flickers = 0;
                    for (const float out : { 8.0f, 14.0f })
                        for (const float h : { 3.0f, 7.0f, 12.0f })
                        {
                            st::Gate gate;
                            bool last = false, first = true;
                            const unsigned before = flips;
                            const float span = out * 1.7320508f;
                            for (float s = -span; s <= span; s += 7.0f / 60.0f)
                            {
                                const float x = c[0] + ox * out + ax * s, y = c[1] + oy * out + ay * s;
                                const float eye[3] = { x, y, street + h };
                                if (!st::Facing(w[j], eye)) { first = true; gate = st::Gate{}; continue; }
                                st::Sight sg;
                                st::LookIn(w[j], eye, around.data(), around.size(), sp, sg);
                                const bool held = st::Hold(gate, st::SightOpen(sg, gate.seen && gate.open), 1.0f / 60.0f);
                                if (!first) flips += held != last ? 1 : 0;
                                last = held;
                                first = false;
                                ++row.walkFrames;
                                row.walkOpen += held ? 1 : 0;
                            }
                            ++row.walks;
                            flickers += flips - before > 2 ? 1 : 0;
                        }
                    row.flips += flips;
                    row.flickers += flickers;
                    row.flipWindows += flips ? 1 : 0;
                    row.maxFlips = std::max(row.maxFlips, flips);
                    if (verbose && flips)
                    {
                        char line[160];
                        std::snprintf(line, sizeof(line), "    group %3u window %4u: its verdict changed %u times over the walks (%u of them changing more than twice)",
                                      g, w[j].ordinal, flips, flickers);
                        row.lines.push_back(line);
                    }
                }
            }
            // The camera swung round a player standing before the window (SEETHROUGH_SWINGS, 2026-10-24: "Curiosities' outside
            // windows seem to flicker a bit", the user, on the raised walkway by its wall, the camera 16 yd behind and 9 up). The
            // walks above pass each front once, so a window open only in a narrow cone of views changes twice and passes; a
            // third-person camera swinging across that cone's edge changes it every swing. Spots: 3 .. 18 yd before the window,
            // up to 8 yd aside, on every level one can stand on there (street, walkway, stairs: Levels), from 25 yd below the
            // window to a yard over it; the player's head 1.7 yd over it, looking at the window; the camera 16 yd behind at 30
            // degrees up (the user's) or 10 yd at 20, swung 60 degrees one way and back at 30 degrees a second (60 frames a
            // second; swung faster, a narrow cone of views is crossed within the gate's hold and never opens), stopped by the
            // first wall between it and the head as the client's camera is (CameraReach); each frame the walk's held verdict
            // where the window faces the camera within kRange. A run flickers when it changes the window more than twice (a
            // window whose views open on one side of the swing and shut on the other changes twice: one edge crossed out and
            // back; one open, or shut, in a band within the swing four times). "1": the windows the table lists from outside;
            // "all": every one with a room.
            if (const char* swings = std::getenv("SEETHROUGH_SWINGS"))
                for (uint32_t i = 0; i < n; ++i)
                {
                    if (w[i].interior < 0) continue;
                    // (listed: its room's or group's rows, and its own row's, 2026-10-24, if it has one: as ListOutside)
                    const bool listedOut = ((gl::SeeThroughGroupFlags(path, g) | gl::SeeThroughGroupFlags(path, static_cast<uint32_t>(w[i].interior))) &
                                            w[i].see & st::kSeeFromOutside) != 0;
                    if (std::string(swings) != "all" && !listedOut) continue;
                    const float nx = w[i].normal[0], ny = w[i].normal[1], l = std::sqrt(nx * nx + ny * ny);
                    if (l < 0.5f) continue;   // (a skylight)
                    const float ox = nx / l, oy = ny / l, ax = -oy, ay = ox;
                    const std::vector<st::SightMesh> around = b.Near(w[i], w[i].interior);
                    const st::SightPoints* sp = i < points.size() && points[i].n ? &points[i] : nullptr;
                    unsigned runs = 0, flickerRuns = 0, blinkRuns = 0, spots = 0, most = 0;
                    float worst[4] = {};
                    for (const float out : { 3.0f, 6.0f, 10.0f, 14.0f, 18.0f })
                        for (const float side : { -8.0f, -4.0f, 0.0f, 4.0f, 8.0f })
                        {
                            const float px = w[i].center[0] + ox * out + ax * side, py = w[i].center[1] + oy * out + ay * side;
                            for (const float level : b.Levels(px, py, w[i].center[2] + 1.0f, w[i].center[2] - 25.0f))
                            {
                                const float head[3] = { px, py, level + 1.7f };
                                if (!st::Facing(w[i], head)) continue;
                                ++spots;
                                // toward the window, level; the camera behind the head
                                float dx = w[i].center[0] - px, dy = w[i].center[1] - py;
                                const float dl = std::sqrt(dx * dx + dy * dy);
                                if (dl < 0.1f) continue;
                                dx /= dl; dy /= dl;
                                for (const auto& cam : { std::make_pair(16.0f, 30.0f), std::make_pair(10.0f, 20.0f) })
                                {
                                    const float back = cam.first * std::cos(cam.second * 3.14159265f / 180.0f), up = cam.first * std::sin(cam.second * 3.14159265f / 180.0f);
                                    st::Gate gate;
                                    bool last = false, first = true;
                                    unsigned changes = 0;
                                    for (int f = 0; f < 480; ++f)
                                    {
                                        const float t = static_cast<float>(f) / 480.0f;   // -60 .. +60 .. -60 degrees, 30 a second
                                        const float phi = (t < 0.5f ? -60.0f + 240.0f * t : 180.0f - 240.0f * t) * 3.14159265f / 180.0f;
                                        const float bx = -(dx * std::cos(phi) - dy * std::sin(phi)), by = -(dy * std::cos(phi) + dx * std::sin(phi));
                                        const float want[3] = { head[0] + bx * back, head[1] + by * back, head[2] + up };
                                        // (stopped by the first wall between it and the head, as the client's camera)
                                        const float reach = b.CameraReach(head, want), s = reach / cam.first;
                                        const float eye[3] = { head[0] + (want[0] - head[0]) * s, head[1] + (want[1] - head[1]) * s, head[2] + (want[2] - head[2]) * s };
                                        const float d[3] = { eye[0] - w[i].center[0], eye[1] - w[i].center[1], eye[2] - w[i].center[2] };
                                        if (!st::Facing(w[i], eye) || d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > st::kRange * st::kRange)
                                        {
                                            first = true;
                                            gate = st::Gate{};
                                            continue;
                                        }
                                        st::Sight sg;
                                        st::LookIn(w[i], eye, around.data(), around.size(), sp, sg);
                                        const bool held = st::Hold(gate, st::SightOpen(sg, gate.seen && gate.open), 1.0f / 60.0f);
                                        if (!first) changes += held != last ? 1 : 0;
                                        last = held;
                                        first = false;
                                    }
                                    ++runs;
                                    if (changes > 2) ++flickerRuns;
                                    if (changes > 4) ++blinkRuns;   // (two narrow bands of views, or one crossed back and forth)
                                    if (changes > most)
                                    {
                                        most = changes;
                                        worst[0] = px; worst[1] = py; worst[2] = level; worst[3] = cam.first;
                                    }
                                }
                            }
                        }
                    ++row.swingWindows;
                    row.swingRuns += runs;
                    row.swingFlickerRuns += flickerRuns;
                    row.swingFlickerWindows += flickerRuns ? 1 : 0;
                    row.swingBlinkRuns += blinkRuns;
                    row.swingBlinkWindows += blinkRuns ? 1 : 0;
                    if (verbose)
                    {
                        char line[300];
                        std::snprintf(line, sizeof(line), "    swings: group %3u window %4u (z %5.1f, room %3d%s): %u runs from %u spots, %u flickering (more than twice), %u blinking (more than four), "
                                      "at most %u changes, at %.2f %.2f on the level %.2f, the camera %.0f yd", g, w[i].ordinal, w[i].center[2], w[i].interior,
                                      listedOut ? ", listed" : "", runs, spots, flickerRuns, blinkRuns, most, worst[0], worst[1], worst[2], worst[3]);
                        row.lines.push_back(line);
                    }
                }
        }
        // Where to stand before a room's windows from outside (SEETHROUGH_SPOTS, 2026-10-24: Dalaran's rooms): of the eyes 4 ..
        // 12 yd before each of its windows (15 .. 30 yd if none opens from there), up to 30 degrees aside, 2 yd over the street
        // under them, the one that opens the most of the room's windows (those facing it within kRange), as the walk from
        // outside decides each.
        if (std::getenv("SEETHROUGH_SPOTS"))
        {
            struct Ref { uint32_t g, i; };
            std::map<int, std::vector<Ref>> byRoom;
            std::map<uint32_t, std::vector<st::SightPoints>> pts;
            for (uint32_t g = 0; g < b.groups.size(); ++g)
            {
                uint32_t n = 0;
                const st::SeeWindow* w = outside.Group(g, n);
                if (!w) continue;
                pointsOf(outside, g, pts[g]);
                // (not the windows the table's own rows shut, 2026-10-24: where to stand for the ones that open)
                for (uint32_t i = 0; i < n; ++i)
                    if (w[i].interior >= 0 && (w[i].see & st::kSeeFromOutside)) byRoom[w[i].interior].push_back({ g, i });
            }
            auto windowOf = [&](const Ref& r) { uint32_t n = 0; return outside.Group(r.g, n) + r.i; };
            for (const auto& [room, refs] : byRoom)
            {
                std::vector<std::vector<st::SightMesh>> around(refs.size());
                for (size_t k = 0; k < refs.size(); ++k) around[k] = b.Near(*windowOf(refs[k]), room);
                unsigned bestOpen = 0, bestFacing = 0;
                float bestEye[3] = {};
                std::string bestList;
                // (2026-10-24: a room none of whose windows opens from 4 .. 12 yd, 15 .. 30 yd out: the Silver Enclave's hall's
                // one window, 21.7 yd up)
                const std::vector<float> rings[2] = { { 4.0f, 6.0f, 8.0f, 10.0f, 12.0f }, { 15.0f, 18.0f, 22.0f, 26.0f, 30.0f } };
                for (int ring = 0; ring < 2 && !bestOpen; ++ring)
                for (const Ref& r : refs)
                {
                    const st::SeeWindow& w = *windowOf(r);
                    const float nx = w.normal[0], ny = w.normal[1], l = std::sqrt(nx * nx + ny * ny);
                    if (l < 0.5f) continue;
                    for (const float dist : rings[ring])
                        for (const float deg : { -30.0f, -15.0f, 0.0f, 15.0f, 30.0f })
                        {
                            const float a = deg * 3.14159265f / 180.0f;
                            const float dx = (nx * std::cos(a) - ny * std::sin(a)) / l, dy = (ny * std::cos(a) + nx * std::sin(a)) / l;
                            const float x = w.center[0] + dist * dx, y = w.center[1] + dist * dy;
                            bool found = true;   // (a house on the terrain: its lowest point)
                            const float street = b.groups.size() < 50 ? b.lowest : b.StreetAt(x, y, w.center[2] + 2.0f, &found);
                            if (!found) continue;
                            const float eye[3] = { x, y, street + 2.0f };
                            unsigned op = 0, fc = 0;
                            std::string list;
                            for (size_t k = 0; k < refs.size(); ++k)
                            {
                                const st::SeeWindow& v = *windowOf(refs[k]);
                                const float d[3] = { eye[0] - v.center[0], eye[1] - v.center[1], eye[2] - v.center[2] };
                                if (!st::Facing(v, eye) || d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > st::kRange * st::kRange) continue;
                                ++fc;
                                const std::vector<st::SightPoints>& pp = pts[refs[k].g];
                                const st::SightPoints* sp = refs[k].i < pp.size() && pp[refs[k].i].n ? &pp[refs[k].i] : nullptr;
                                st::Sight s;
                                st::LookIn(v, eye, around[k].data(), around[k].size(), sp, s);
                                if (st::SightOpen(s, false)) { ++op; list += " " + std::to_string(v.ordinal); }
                            }
                            if (op > bestOpen || (op == bestOpen && op && fc < bestFacing))
                            {
                                bestOpen = op; bestFacing = fc; bestList = list;
                                for (int k = 0; k < 3; ++k) bestEye[k] = eye[k];
                            }
                        }
                }
                if (!bestOpen) continue;
                char line[600];
                std::snprintf(line, sizeof(line), "    spot outside: room %3d %-22s stand at %.2f %.2f %.2f: %u of the %u windows into it facing there open:%s", room,
                              static_cast<size_t>(room) < b.names.size() ? b.names[static_cast<size_t>(room)].c_str() : "", bestEye[0], bestEye[1], bestEye[2],
                              bestOpen, bestFacing, bestList.substr(0, 400).c_str());
                row.lines.push_back(line);
            }
        }
        // from inside: each room's own windows from eyes all over it (on its floor, found from above), within 20 yd
        for (uint32_t g = 0; g < b.groups.size(); ++g)
        {
            uint32_t n = 0;
            const st::SeeWindow* w = in.Group(g, n);
            if (!w) continue;
            std::vector<st::SightPoints> points;
            pointsOf(in, g, points);
            const st::GroupInfo& box = b.groups[g];
            const std::vector<st::SightMesh> roomOnly = { { &b.Mesh(static_cast<int>(g)), false } };
            std::vector<unsigned> views(n), open(n);
            std::vector<std::vector<st::SightMesh>> around(n);
            // (between rooms: a window onto another room looked through as from outside, LookIn; a room's own, LookOut)
            for (uint32_t i = 0; i < n; ++i) around[i] = b.Near(w[i], w[i].interior >= 0 ? w[i].interior : static_cast<int>(g));
            auto look = [&](uint32_t i, const float eye[3], st::Sight& s) {
                const st::SightPoints* sp = i < points.size() && points[i].n ? &points[i] : nullptr;
                if (w[i].interior >= 0) st::LookIn(w[i], eye, around[i].data(), around[i].size(), sp, s, st::RoomReach(eye, b.groups[static_cast<size_t>(w[i].interior)]));
                else st::LookOut(w[i], eye, around[i].data(), around[i].size(), sp, s);
            };
            const bool betweenGroup = between[g] != 0;
            for (uint32_t i = 0; betweenGroup && i < n; ++i) (w[i].interior >= 0 ? row.betweenWindows : row.betweenFake) += 1;
            // (an eye farther than 20 yd from all the group's windows sees none of them: a city's street, skipped)
            auto nearAny = [&](float x, float y) {
                for (uint32_t i = 0; i < n; ++i)
                {
                    const float dx = x - w[i].center[0], dy = y - w[i].center[1];
                    if (dx * dx + dy * dy <= 400.0f) return true;
                }
                return false;
            };
            // (SEETHROUGH_SPOTS, 2026-10-24: where to stand in the room, on foot, for the most of its windows open as ways out)
            unsigned spotOpen = 0, spotFacing = 0;
            float spotEye[3] = {};
            std::string spotList;
            const bool spots = std::getenv("SEETHROUGH_SPOTS") != nullptr;
            for (float x = box.lo[0] + 0.5f; x <= box.hi[0] - 0.5f; x += 1.5f)
                for (float y = box.lo[1] + 0.5f; y <= box.hi[1] - 0.5f; y += 1.5f)
                {
                    if (!nearAny(x, y)) continue;
                    bool found = false;
                    const float floorZ = b.Ground(x, y, box.hi[2] - 0.1f, roomOnly, &found);
                    if (!found) continue;
                    for (const float h : { 1.8f, 3.0f, 4.5f })
                    {
                        const float eye[3] = { x, y, floorZ + h };
                        if (eye[2] >= box.hi[2]) continue;
                        unsigned eyeOpen = 0, eyeFacing = 0;
                        std::string eyeList;
                        for (uint32_t i = 0; i < n; ++i)
                        {
                            const float d[3] = { eye[0] - w[i].center[0], eye[1] - w[i].center[1], eye[2] - w[i].center[2] };
                            if (!st::FacingIn(w[i], eye) || d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > 400.0f) continue;
                            if (betweenGroup && w[i].interior < 0) continue;   // (a fake between rooms: nothing beyond it)
                            st::Sight s;
                            look(i, eye, s);
                            ++views[i];
                            open[i] += st::SightOpen(s, false) ? 1 : 0;
                            if (spots && w[i].interior < 0)
                            {
                                ++eyeFacing;
                                if (st::SightOpen(s, false)) { ++eyeOpen; eyeList += " " + std::to_string(w[i].ordinal); }
                            }
                            // (SEETHROUGH_PROBE_WINDOW <ordinal>: that window's every eye, shut ones with what their rays met)
                            if (const char* pw = std::getenv("SEETHROUGH_PROBE_WINDOW"); pw && std::atoi(pw) == static_cast<int>(w[i].ordinal) &&
                                !st::SightOpen(s, false) && row.lines.size() < 4000)
                            {
                                char line[200];
                                std::snprintf(line, sizeof(line), "    eye %.1f %.1f %.1f -> window %u: %d out, %d against, %d neither; first not out %s at %.2f",
                                              eye[0], eye[1], eye[2], w[i].ordinal, s.front, s.back, s.none,
                                              s.missHit == st::RoomMesh::kBack ? "against" : "neither", s.missDepth);
                                row.lines.push_back(line);
                            }
                        }
                        if (spots && h == 1.8f && (eyeOpen > spotOpen || (eyeOpen == spotOpen && eyeOpen && eyeFacing < spotFacing)))
                        {
                            spotOpen = eyeOpen; spotFacing = eyeFacing; spotList = eyeList;
                            for (int k = 0; k < 3; ++k) spotEye[k] = eye[k];
                        }
                    }
                }
            if (spotOpen)
            {
                char line[600];
                std::snprintf(line, sizeof(line), "    spot inside: group %3u %-22s stand at %.2f %.2f %.2f: %u of the %u windows facing there open as ways out:%s", g,
                              g < b.names.size() ? b.names[g].c_str() : "", spotEye[0], spotEye[1], spotEye[2], spotOpen, spotFacing, spotList.substr(0, 400).c_str());
                row.lines.push_back(line);
            }
            for (uint32_t i = 0; i < n; ++i)
            {
                if (!views[i]) continue;
                if (w[i].interior >= 0)
                {
                    row.betweenViews += views[i];
                    row.betweenOpen += open[i];
                    if (open[i] * 2 >= views[i]) ++row.betweenMostly;
                    if (!open[i]) ++row.betweenNever;
                    continue;
                }
                ++row.insideWindows;
                row.insideViews += views[i];
                row.insideOpen += open[i];
                if (open[i] * 2 >= views[i]) ++row.insideMostly;
                if (!open[i]) ++row.insideNever;
                if (verbose)
                {
                    char line[160];
                    std::snprintf(line, sizeof(line), "    inside: group %3u window %4u (z %5.1f): opens from %4u of %4u eyes", g, w[i].ordinal, w[i].center[2],
                                  open[i], views[i]);
                    row.lines.push_back(line);
                }
            }
            // (SEETHROUGH_PROBE "x y z": what each of the room's windows facing that eye, within kInsideRange, sees from there)
            if (g_haveProbe && (g_probeSides & 2))
            {
                const float* eye = g_probe;
                if (eye)
                    for (uint32_t i = 0; i < n; ++i)
                    {
                        const float d[3] = { eye[0] - w[i].center[0], eye[1] - w[i].center[1], eye[2] - w[i].center[2] };
                        if (!st::FacingIn(w[i], eye) || d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > st::kInsideRange * st::kInsideRange) continue;
                        if (betweenGroup && w[i].interior < 0) continue;
                        st::Sight s;
                        look(i, eye, s);
                        char line[220];
                        std::snprintf(line, sizeof(line), "    probe (%.2f %.2f %.2f): group %3u window %4u (outer %.2f yd%s): %s; %d see %s, %d against, "
                                      "%d neither; the first not out: %s %.2f yd past the glass", eye[0], eye[1], eye[2], g, w[i].ordinal, w[i].outer,
                                      w[i].interior >= 0 ? (", onto group " + std::to_string(w[i].interior)).c_str() : "",
                                      st::SightOpen(s, false) ? "open" : "shut", s.front, w[i].interior >= 0 ? "the room" : "the street", s.back, s.none,
                                      s.firstMiss < 0 ? "-" : s.missHit == st::RoomMesh::kBack ? "against" : "neither", s.missDepth);
                        row.lines.push_back(line);
                    }
            }
            // Between rooms, where players stand (2026-10-23, Ironforge's Great Forge: "Ironforge windows don't work"; the user
            // stood on the forge's ring before a shop's windows): SEETHROUGH_FRONTS, eyes before each of the group's windows
            // onto a room (2 .. 10 yd out, up to 60 degrees aside, on the group's floor at 1.8 and 3 yd) and walks along each
            // window's front (3, 5 and 8 yd out, 15 yd either side, on foot, 7 yd a second), every window held as the runtime
            // holds it; SEETHROUGH_SPOTS, the eye before a window opening the most of the group's windows (a place to stand).
            if (betweenGroup && (std::getenv("SEETHROUGH_FRONTS") || std::getenv("SEETHROUGH_SPOTS")))
            {
                auto flat = [](const st::SeeWindow& wi, float& nx, float& ny) {
                    nx = wi.normal[0]; ny = wi.normal[1];
                    const float l = std::sqrt(nx * nx + ny * ny);
                    if (l < 0.3f) return false;
                    nx /= l; ny /= l;
                    return true;
                };
                auto eyeAt = [&](const st::SeeWindow& wi, float back, float side, float h, float eye[3]) {
                    float nx, ny;
                    if (!flat(wi, nx, ny)) return false;
                    const float x = wi.center[0] - back * nx - side * ny, y = wi.center[1] - back * ny + side * nx;
                    bool found = false;
                    const float floorZ = b.Ground(x, y, wi.center[2] + 1.0f, roomOnly, &found);
                    if (!found || floorZ < wi.center[2] - 4.0f) return false;   // its own storey's floor
                    eye[0] = x; eye[1] = y; eye[2] = floorZ + h;
                    return eye[2] < box.hi[2];
                };
                auto opens = [&](uint32_t i, const float eye[3]) {
                    st::Sight s;
                    look(i, eye, s);
                    return st::SightOpen(s, false);
                };
                auto facing = [&](uint32_t i, const float eye[3]) {
                    const float d[3] = { eye[0] - w[i].center[0], eye[1] - w[i].center[1], eye[2] - w[i].center[2] };
                    return w[i].interior >= 0 && st::FacingIn(w[i], eye) && d[0] * d[0] + d[1] * d[1] + d[2] * d[2] <= st::kInsideRange * st::kInsideRange;
                };
                unsigned nearEyes = 0, nearOpen = 0, midEyes = 0, midOpen = 0, frames = 0, openFrames = 0, changesAll = 0, flickerWalks = 0, walksDone = 0;
                std::vector<unsigned> winNear(n, 0), winNearOpen(n, 0);
                float bestEye[3] = {};
                unsigned bestOpen = 0, bestFacing = 0;
                std::string bestList;
                for (uint32_t j = 0; j < n; ++j)
                {
                    if (w[j].interior < 0) continue;
                    for (const float back : { 2.0f, 3.0f, 4.5f, 6.0f, 8.0f, 10.0f })
                        for (const float aside : { -60.0f, -40.0f, -20.0f, 0.0f, 20.0f, 40.0f, 60.0f })
                            for (const float h : { 1.8f, 3.0f })
                            {
                                const float a = aside * 3.14159265f / 180.0f;
                                float eye[3];
                                if (!eyeAt(w[j], back * std::cos(a), back * std::sin(a), h, eye) || !facing(j, eye)) continue;
                                const bool o = opens(j, eye);
                                (back <= 4.5f ? nearEyes : midEyes) += 1;
                                (back <= 4.5f ? nearOpen : midOpen) += o ? 1 : 0;
                                if (back <= 4.5f) { ++winNear[j]; winNearOpen[j] += o ? 1 : 0; }
                                if (std::getenv("SEETHROUGH_SPOTS") && h == 1.8f && std::fabs(aside) <= 20.0f && back >= 3.0f && back <= 6.0f)
                                {
                                    unsigned op = 0, fc = 0;
                                    std::string list;
                                    for (uint32_t i = 0; i < n; ++i)
                                    {
                                        if (!facing(i, eye)) continue;
                                        ++fc;
                                        if (opens(i, eye)) { ++op; list += " " + std::to_string(w[i].ordinal); }
                                    }
                                    if (op > bestOpen || (op == bestOpen && fc < bestFacing))
                                    {
                                        bestOpen = op; bestFacing = fc; bestList = list;
                                        for (int k = 0; k < 3; ++k) bestEye[k] = eye[k];
                                    }
                                }
                            }
                    if (!std::getenv("SEETHROUGH_FRONTS")) continue;
                    // walks along its front
                    float nx, ny;
                    if (!flat(w[j], nx, ny)) continue;
                    for (const float out : { 3.0f, 5.0f, 8.0f })
                    {
                        std::vector<st::Gate> gates(n);
                        std::vector<uint8_t> last(n, 0), seen(n, 0);
                        std::vector<unsigned> changes(n, 0);
                        for (float t = -15.0f; t <= 15.0f; t += 7.0f / 60.0f)
                        {
                            float eye[3];
                            if (!eyeAt(w[j], out, t, 2.5f, eye))
                            {
                                for (uint32_t i = 0; i < n; ++i) { gates[i] = st::Gate{}; seen[i] = 0; }
                                continue;
                            }
                            for (uint32_t i = 0; i < n; ++i)
                            {
                                if (!facing(i, eye)) { gates[i] = st::Gate{}; seen[i] = 0; continue; }
                                st::Sight sg;
                                look(i, eye, sg);
                                const bool held = st::Hold(gates[i], st::SightOpen(sg, gates[i].seen && gates[i].open), 1.0f / 60.0f);
                                if (seen[i]) changes[i] += held != (last[i] != 0) ? 1 : 0;
                                last[i] = held ? 1 : 0;
                                seen[i] = 1;
                                ++frames;
                                openFrames += held ? 1 : 0;
                            }
                        }
                        ++walksDone;
                        bool flick = false;
                        for (uint32_t i = 0; i < n; ++i) { changesAll += changes[i]; flick = flick || changes[i] > 2; }
                        flickerWalks += flick ? 1 : 0;
                    }
                }
                char line[700];
                if (std::getenv("SEETHROUGH_FRONTS"))
                {
                    unsigned mostly = 0, real = 0;
                    std::string weak;
                    for (uint32_t i = 0; i < n; ++i)
                    {
                        if (w[i].interior < 0 || !winNear[i]) continue;
                        ++real;
                        if (winNearOpen[i] * 4 >= winNear[i] * 3) ++mostly;
                        else weak += " " + std::to_string(w[i].ordinal) + "(" + std::to_string(winNearOpen[i]) + "/" + std::to_string(winNear[i]) + ")";
                    }
                    std::snprintf(line, sizeof(line), "    fronts: group %3u %-22s 2 .. 4.5 yd before its windows open %u of %u (%.0f%%), 6 .. 10 yd %u of %u (%.0f%%); "
                                  "%u of %u windows from 3/4 of their near eyes; walks along the fronts: open %u of %u frames, changed %u times, %u of %u walks "
                                  "changed one more than twice; weakest:%s", g, g < b.names.size() ? b.names[g].c_str() : "", nearOpen, nearEyes,
                                  nearEyes ? 100.0 * nearOpen / nearEyes : 0.0, midOpen, midEyes, midEyes ? 100.0 * midOpen / midEyes : 0.0, mostly, real, openFrames, frames, changesAll,
                                  flickerWalks, walksDone, weak.substr(0, 300).c_str());
                    row.betweenLines.push_back(line);
                    // (SEETHROUGH_FRONTS=2: each window's)
                    if (std::getenv("SEETHROUGH_FRONTS")[0] == '2')
                        for (uint32_t i = 0; i < n; ++i)
                        {
                            if (w[i].interior < 0 || !winNear[i]) continue;
                            std::snprintf(line, sizeof(line), "      window %4u (z %5.1f, %.2f x %.2f yd, onto %d): open from %u of %u eyes 2 .. 4.5 yd before it",
                                          w[i].ordinal, w[i].center[2], 2 * w[i].halfWidth, 2 * w[i].halfHeight, w[i].interior, winNearOpen[i], winNear[i]);
                            row.betweenLines.push_back(line);
                        }
                }
                if (std::getenv("SEETHROUGH_SPOTS") && bestOpen)
                {
                    std::snprintf(line, sizeof(line), "    spot: group %3u %-22s stand at %.2f %.2f %.2f: %u of the %u windows it faces open:%s", g,
                                  g < b.names.size() ? b.names[g].c_str() : "", bestEye[0], bestEye[1], bestEye[2], bestOpen, bestFacing, bestList.c_str());
                    row.betweenLines.push_back(line);
                }
            }
            // walks through the room: across it along x and along y at a quarter, half and three quarters of its box, the
            // camera 2.5 yd over the floor (on foot) and 5 (mounted), 7 yd a second, a frame each 1/60 s; each window facing
            // in within kInsideRange held as the runtime holds it (as if the camera looked at it)
            std::vector<unsigned> flips(n, 0), flickers(n, 0);
            for (int axis = 0; axis < 2; ++axis)
                for (const float at : { 0.25f, 0.5f, 0.75f })
                    for (const float h : { 2.5f, 5.0f })
                    {
                        std::vector<st::Gate> gates(n);
                        std::vector<uint8_t> last(n, 0), seen(n, 0);
                        std::vector<unsigned> changes(n, 0);
                        const int other = 1 - axis;
                        const float fixed = box.lo[other] + at * (box.hi[other] - box.lo[other]);
                        for (float s = box.lo[axis] + 0.5f; s <= box.hi[axis] - 0.5f; s += 7.0f / 60.0f)
                        {
                            float p[2];
                            p[axis] = s;
                            p[other] = fixed;
                            bool found = false;
                            const float floorZ = b.Ground(p[0], p[1], box.hi[2] - 0.1f, roomOnly, &found);
                            const float eye[3] = { p[0], p[1], floorZ + h };
                            if (!found || eye[2] >= box.hi[2])
                            {
                                for (uint32_t i = 0; i < n; ++i) { gates[i] = st::Gate{}; seen[i] = 0; }
                                continue;
                            }
                            for (uint32_t i = 0; i < n; ++i)
                            {
                                const float d[3] = { eye[0] - w[i].center[0], eye[1] - w[i].center[1], eye[2] - w[i].center[2] };
                                if (!st::FacingIn(w[i], eye) || d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > st::kInsideRange * st::kInsideRange)
                                {
                                    gates[i] = st::Gate{};
                                    seen[i] = 0;
                                    continue;
                                }
                                if (betweenGroup && w[i].interior < 0) continue;
                                st::Sight sg;
                                look(i, eye, sg);
                                const bool held = st::Hold(gates[i], st::SightOpen(sg, gates[i].seen && gates[i].open), 1.0f / 60.0f);
                                if (seen[i]) changes[i] += held != (last[i] != 0) ? 1 : 0;
                                last[i] = held ? 1 : 0;
                                seen[i] = 1;
                                ++(w[i].interior >= 0 ? row.betweenFrames : row.insideFrames);
                                (w[i].interior >= 0 ? row.betweenOpenFrames : row.insideOpenFrames) += held ? 1 : 0;
                            }
                        }
                        ++(betweenGroup ? row.betweenWalks : row.insideWalks);
                        bool flickered = false;
                        for (uint32_t i = 0; i < n; ++i)
                        {
                            flips[i] += changes[i];
                            flickers[i] += changes[i] > 2 ? 1 : 0;
                            flickered = flickered || changes[i] > 2;
                        }
                        (betweenGroup ? row.betweenFlickers : row.insideFlickers) += flickered ? 1 : 0;
                    }
            // between rooms: the group's line (its windows onto rooms, their eyes open, the walks' changes)
            if (betweenGroup)
            {
                unsigned gw = 0, gv = 0, go = 0, gf = 0, gmax = 0, gfake = 0, gflick = 0;
                std::map<int, unsigned> onto;
                for (uint32_t i = 0; i < n; ++i)
                {
                    if (w[i].interior < 0) { ++gfake; continue; }
                    ++gw; gv += views[i]; go += open[i]; gf += flips[i]; gmax = std::max(gmax, flips[i]); gflick += flickers[i];
                    ++onto[w[i].interior];
                    row.betweenFlips += flips[i];
                    row.betweenMaxFlips = std::max(row.betweenMaxFlips, flips[i]);
                }
                std::string rooms;
                for (const auto& [room, k] : onto)
                    rooms += (rooms.empty() ? "" : ", ") + std::to_string(room) + (static_cast<size_t>(room) < b.names.size() && !b.names[room].empty() ? " " + b.names[room] : "") +
                             (k > 1 ? " x" + std::to_string(k) : "");
                char line[600];
                std::snprintf(line, sizeof(line), "    between: group %3u %-22s %3u windows onto rooms (%u fake): open from %5u of %5u eyes (%3.0f%%); walks changed them %u "
                              "times (at most %u for one; %u times a window more than twice in one walk); onto %s", g, g < b.names.size() ? b.names[g].c_str() : "",
                              gw, gfake, go, gv, gv ? 100.0 * go / gv : 0.0, gf, gmax, gflick, rooms.c_str());
                row.betweenLines.push_back(line);
            }
            for (uint32_t i = 0; i < n; ++i)
            {
                if (betweenGroup) continue;
                row.insideFlips += flips[i];
                row.insideFlipWindows += flips[i] ? 1 : 0;
                row.insideMaxFlips = std::max(row.insideMaxFlips, flips[i]);
                if (verbose && flips[i])
                {
                    char line[160];
                    std::snprintf(line, sizeof(line), "    inside walks: group %3u window %4u: its verdict changed %u times (%u walks more than twice)", g,
                                  w[i].ordinal, flips[i], flickers[i]);
                    row.lines.push_back(line);
                }
            }
        }
        row.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return true;
    }

    void PrintSurveyRow(const SurveyRow& r)
    {
        std::printf("  %s: %u groups; %u outside windows, %u with a room, %u fake; %u in its rooms\n", r.wmo.c_str(), r.groups, r.ext, r.real, r.fake, r.inside);
        std::printf("    street sweep: open from %u of %u views (%.0f%%); %u windows from half their views or more, %u never (%u of them only from "
                    "high and close); high and close: %u of %u\n", r.streetOpen, r.streetViews, r.streetViews ? 100.0 * r.streetOpen / r.streetViews : 0.0,
                    r.mostlyOpen, r.neverOpen, r.highOnly, r.highOpen, r.highViews);
        std::printf("    walks: open %u of %u frames; the verdict changed %u times in all, over %u windows (at most %u for one); %u of %u walks "
                    "changed it more than twice\n", r.walkOpen, r.walkFrames, r.flips, r.flipWindows, r.maxFlips, r.flickers, r.walks);
        if (r.swingWindows)
            std::printf("    swings (SEETHROUGH_SWINGS): %u windows, %u runs; %u runs changed a window more than twice, over %u windows; %u more than four "
                        "times, over %u windows\n", r.swingWindows, r.swingRuns, r.swingFlickerRuns, r.swingFlickerWindows, r.swingBlinkRuns, r.swingBlinkWindows);
        std::printf("    from inside: %u windows seen, open from %u of %u eyes (%.0f%%); %u from half their eyes or more, %u never; %.1f s\n", r.insideWindows,
                    r.insideOpen, r.insideViews, r.insideViews ? 100.0 * r.insideOpen / r.insideViews : 0.0, r.insideMostly, r.insideNever, r.seconds);
        std::printf("    walks inside: open %u of %u frames; the verdict changed %u times in all, over %u windows (at most %u for one); %u of %u walks "
                    "changed one more than twice\n", r.insideOpenFrames, r.insideFrames, r.insideFlips, r.insideFlipWindows, r.insideMaxFlips, r.insideFlickers,
                    r.insideWalks);
        if (r.betweenWindows || r.betweenFake)
        {
            std::printf("    between rooms: %u windows onto another room (%u fake), open from %u of %u eyes (%.0f%%); %u from half their eyes or more, %u "
                        "never; walks: open %u of %u frames, changed %u times (at most %u for one), %u of %u walks changed one more than twice\n",
                        r.betweenWindows, r.betweenFake, r.betweenOpen, r.betweenViews, r.betweenViews ? 100.0 * r.betweenOpen / r.betweenViews : 0.0,
                        r.betweenMostly, r.betweenNever, r.betweenOpenFrames, r.betweenFrames, r.betweenFlips, r.betweenMaxFlips, r.betweenFlickers, r.betweenWalks);
            for (const std::string& l : r.betweenLines) std::printf("%s\n", l.c_str());
        }
        for (const std::string& l : r.lines) std::printf("%s\n", l.c_str());
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

    // --- a snapshot's view (2026-10-22, debug/SnapshotView) ------------------------------------------------------------
    Test("snapshot view: its lines read back as written (the first building's; \"wmo camera\" not \"wmo camera forward\"); the "
         "placement's yaw, tilt and origin; a point and a direction into the WMO's space and back; none, a bad file", [] {
        namespace sn = wxl_livingazeroth::snapshot;
        // a placement turned 33 degrees at (-5600, -480, 396), as the frosted scenes'
        const float a = 33.0f * 3.14159265f / 180.0f;
        const float model[16] = { std::cos(a), std::sin(a), 0, 0, -std::sin(a), std::cos(a), 0, 0, 0, 0, 1, 0, -5600.0f, -480.0f, 396.0f, 1 };
        float inv[16], back[16];
        CHECK(sn::Invert(model, inv) && sn::Invert(inv, back));
        for (int i = 0; i < 16; ++i) CHECK(std::fabs(back[i] - model[i]) < 1e-3f);
        sn::WmoView v;
        v.wmo = "World\\wmo\\KhazModan\\Buildings\\Dwarven_2story_Hovel\\Snow_2story\\Snow_2story.wmo";
        v.group = 1;
        v.inside = true;
        sn::Placement(model, v.yaw, v.tilt, v.origin);
        CHECK(std::fabs(v.yaw - 33.0f) < 1e-3f && v.tilt < 0.01f && v.origin[0] == -5600.0f && v.origin[2] == 396.0f);
        const float world[3] = { -5590.0f, -470.0f, 400.0f }, worldDir[3] = { 0.0f, 1.0f, 0.0f };
        float local[3], dir[3], again[3];
        sn::Apply(inv, world, 1.0f, local);
        sn::Apply(model, local, 1.0f, again);
        for (int k = 0; k < 3; ++k) CHECK(std::fabs(again[k] - world[k]) < 1e-3f);
        sn::Apply(inv, worldDir, 0.0f, dir);   // world +y, 33 degrees on from the WMO's x: (sin 33, cos 33, 0) in its space
        CHECK(std::fabs(dir[0] - std::sin(a)) < 1e-5f && std::fabs(dir[1] - std::cos(a)) < 1e-5f && std::fabs(dir[2]) < 1e-6f);
        for (int k = 0; k < 3; ++k) { v.cam[k] = local[k]; v.forward[k] = dir[k]; }
        v.up[2] = 1.0f;
        v.open = { 5 };
        v.shut = { 4, 6 };
        const std::string text = "Living Azeroth snapshot\n\nCamera\n" + sn::FovLine(0.9f) + "\nThe nearest WMO\n" + sn::FormatView(v) +
                                 "wmo how: a test\nwmo: World\\wmo\\another.wmo\nwmo camera: 1 2 3\nwmo camera forward: 1 0 0\n";
        sn::WmoView r;
        CHECK(sn::ParseView(text, r));
        CHECK(r.wmo == v.wmo && r.group == 1 && r.inside && std::fabs(r.yaw - 33.0f) < 1e-3f && r.origin[1] == -480.0f && std::fabs(r.fov - 0.9f) < 1e-3f);
        for (int k = 0; k < 3; ++k) CHECK(std::fabs(r.cam[k] - v.cam[k]) < 2e-3f && std::fabs(r.forward[k] - v.forward[k]) < 2e-4f);
        CHECK(r.open == std::vector<uint32_t>{ 5 } && (r.shut == std::vector<uint32_t>{ 4, 6 }) && r.up[2] == 1.0f);
        CHECK(!sn::ParseView("wmo: none (the camera is in no building)\ncamera fov: 50 degrees\n", r) && r.wmo.empty());
        CHECK(!sn::ParseView("wmo: a.wmo\nwmo camera: 1 2\n", r));
        CHECK(sn::Ordinals({}) == "none" && sn::Ordinals({ 3, 9 }) == "3, 9");
        const float flat[16] = {};
        CHECK(!sn::Invert(flat, inv));
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

    Test("RoomBeyond, BuildInside between rooms (2026-10-23, Phase C, Ironforge: the shop's window from the street, the street's "
         "from the shop): the smallest other interior box past the window along its normal; not its own group, an exterior or a "
         "skipped one; only the groups marked between rooms get it, counted real or fake", [] {
        const uint32_t path = gl::PathHash("world\\wmo\\test\\city.wmo");
        const st::GroupInfo groups[] = {
            { 0x8, { -100, -100, -10 }, { 100, 100, 50 } },   // 0 the city's outside: never
            { 0x2000, { -50, -50, 0 }, { 50, 50, 20 } },      // 1 the street (it holds the shop's box too)
            { 0x2000, { 10, -5, 0 }, { 20, 5, 6 } },          // 2 a shop off it
            { 0x12000, { 10, -5, 0 }, { 12, 5, 6 } },         // 3 smaller, 0x10000: the walk never enters
        };
        std::vector<gl::Window> all;
        auto add = [&](uint32_t group, uint32_t ordinal, float x, float nx) {
            gl::Window w;
            w.path = path; w.group = group; w.ordinal = ordinal; w.flags = 0; w.kind = gl::kFrosted;
            w.center[0] = x; w.center[1] = 0; w.center[2] = 3;
            w.normal[0] = nx; w.normal[1] = 0; w.normal[2] = 0;
            w.halfWidth = 1; w.halfHeight = 1;
            all.push_back(w);
        };
        add(1, 4, 9.5f, 1);      // the street's window in the shop's wall, its normal out of the street: into the shop
        add(2, 6, 10.5f, -1);    // the shop's window in the same wall, its normal out of the shop: into the street
        add(1, 5, -49.5f, -1);   // the street's window onto the rock (nothing beyond): a fake
        st::SeeWindow w;
        w.group = 1; w.center[0] = 9.5f; w.center[2] = 3; w.normal[0] = 1; w.normal[1] = 0; w.normal[2] = 0;
        CHECK(st::RoomBeyond(w, groups, 4) == 2);
        w.group = 2; w.center[0] = 10.5f; w.normal[0] = -1;
        CHECK(st::RoomBeyond(w, groups, 4) == 1);
        w.group = 1; w.center[0] = -49.5f;
        CHECK(st::RoomBeyond(w, groups, 4) == -1 && st::RoomBeyond(w, nullptr, 4) == -1);
        // only the marked groups: the street's windows get the room beyond them, the shop's (unmarked) none
        const st::RootWindows r = st::BuildInside(path, all, groups, 4, { 0, 1, 0, 0 });
        uint32_t n = 0;
        const st::SeeWindow* street = r.Group(1, n);
        CHECK(street && n == 2 && street[0].ordinal == 4 && street[0].interior == 2 && street[1].ordinal == 5 && street[1].interior == -1);
        const st::SeeWindow* shop = r.Group(2, n);
        CHECK(shop && n == 1 && shop->interior == -1 && r.real == 1 && r.fake == 1);
        // (2026-10-23) glazed on both sides: the pane on the window's other side names the room where boxes overlap (a hall's
        // box over the street in front of the shop: the smallest box alone took the hall, as for Ironforge's herbalist)
        const st::GroupInfo overlap[] = { groups[0], groups[1], groups[2], groups[3], { 0x2000, { 0, -8, 0 }, { 10, 8, 20 } } };
        w.group = 2; w.center[0] = 10.5f; w.normal[0] = -1;
        CHECK(st::RoomBeyond(w, overlap, 5) == 4 && st::GlazedPartner(w, path, all, overlap, 5) == 1);
        w.group = 1; w.center[0] = 9.5f; w.normal[0] = 1;
        CHECK(st::GlazedPartner(w, path, all, overlap, 5) == 2);
        w.center[0] = -49.5f; w.normal[0] = -1;
        CHECK(st::GlazedPartner(w, path, all, overlap, 5) == -1 && st::GlazedPartner(w, path, all, nullptr, 5) == -1);
        w.center[0] = 12.0f; w.normal[0] = 1;   // the shop's pane behind it, not across its glass
        CHECK(st::GlazedPartner(w, path, all, overlap, 5) == -1);
        const st::RootWindows paired = st::BuildInside(path, all, overlap, 5, { 0, 0, 1, 0, 0 });
        const st::SeeWindow* shopPaired = paired.Group(2, n);
        CHECK(shopPaired && n == 1 && shopPaired->interior == 1 && paired.real == 1);
        // unmarked everywhere: as before (no room behind a room's own glass)
        const st::RootWindows plain = st::BuildInside(path, all, 4);
        CHECK(plain.real == 0 && plain.fake == 0 && plain.Group(1, n)->interior == -1);
        static_assert(st::kSeeBetween == 0x4, "the table's between-rooms flag");
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
        // the budget: one left, the nearest candidate takes it (2026-10-24; before, the first in the windows' order): 5 is
        // 20.2 yd from the camera, 0 20.7
        budget = 1;
        st::PlanWindows(w.data(), static_cast<uint32_t>(w.size()), cam, lo, hi, full, kIdentity, cam, vp, budget, plan);
        CHECK(budget == 0 && plan[5].candidate && !plan[0].candidate && plan[0].state == st::kShut);
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

    Test("ListOutside, PlanWindows (2026-10-24, Dalaran): a city's rows name its rooms from outside; a window into another "
         "room is shut and takes none of the budget; a building listed whole keeps every window; the budget nearest first", [] {
        const float cam[3] = { 0, -30, 5 }, at[3] = { 0, 0, 5 };
        float vp[16];
        Camera(cam, at, 1.0f, 16.0f / 9.0f, vp);
        // one exterior group (0) with four south windows: into room 5 (two), room 6, and a fake
        st::RootWindows r;
        r.windows = { MakeWindow(-6, -10, 3, 0, -1, 0, 1.5f, 1.2f, 5, 1), MakeWindow(-2, -10, 3, 0, -1, 0, 1.5f, 1.2f, 6, 2),
                      MakeWindow(2, -10, 3, 0, -1, 0, 1.5f, 1.2f, 5, 3), MakeWindow(6, -10, 3, 0, -1, 0, 1.5f, 1.2f, -1, 4) };
        r.byGroup = { { 0u, 4u } };
        // a city's rows: room 5 from outside, room 6 from inside only (its own windows onto the street)
        std::vector<uint32_t> flags(8, 0u);
        flags[5] = st::kSeeFromOutside;
        flags[6] = st::kSeeFromInside;
        CHECK(st::ListOutside(r, flags) == 2);
        CHECK(!r.windows[0].unlisted && r.windows[1].unlisted && !r.windows[2].unlisted && r.windows[3].unlisted);
        const float lo[3] = { -20, -10, 0 }, hi[3] = { 20, 10, 15 };
        std::vector<st::WindowPlan> plan;
        unsigned budget = 10;
        st::PlanWindows(r.windows.data(), 4, cam, lo, hi, { -1, -1, 1, 1 }, kIdentity, cam, vp, budget, plan);
        CHECK(budget == 8 && plan[0].candidate && plan[2].candidate && !plan[1].candidate && !plan[3].candidate);
        // (unlisted: left unseen, not even projected, as the client draws them; the whole-glass redraw keeps their batch)
        CHECK(plan[1].state == st::kUnseen && plan[3].state == st::kUnseen);
        st::BatchWindows bw;
        bw.valid = true;
        bw.windows = { 0, 2 };
        CHECK(!st::BatchUnlisted(bw, r.windows.data(), 4));
        bw.windows = { 0, 1, 2 };
        CHECK(st::BatchUnlisted(bw, r.windows.data(), 4));
        // one left: the nearer of the two listed (2: 20.2 yd, 0: 21.0) takes it; the unlisted 1, as near as 2, none
        budget = 1;
        st::PlanWindows(r.windows.data(), 4, cam, lo, hi, { -1, -1, 1, 1 }, kIdentity, cam, vp, budget, plan);
        CHECK(budget == 0 && plan[2].candidate && !plan[0].candidate && !plan[1].candidate);
        // the exterior group's own row: every window of it (a fake still opens nothing: no room)
        std::vector<uint32_t> group(8, 0u);
        group[0] = st::kSeeFromOutside;
        CHECK(st::ListOutside(r, group) == 4);
        budget = 10;
        st::PlanWindows(r.windows.data(), 4, cam, lo, hi, { -1, -1, 1, 1 }, kIdentity, cam, vp, budget, plan);
        CHECK(budget == 7 && plan[0].candidate && plan[1].candidate && plan[2].candidate && !plan[3].candidate);
        // a building listed whole (its rows' flags in every group): as before
        std::vector<uint32_t> whole(8, st::kSeeFromOutside | st::kSeeFromInside);
        CHECK(st::ListOutside(r, whole) == 4);
        // none from outside (Ironforge's: between rooms only): none listed
        std::vector<uint32_t> between(8, st::kSeeBetween);
        CHECK(st::ListOutside(r, between) == 0);
        std::vector<uint32_t> none;
        CHECK(st::ListOutside(r, none) == 0);
    });

    Test("MaskWindows, ListOutside (2026-10-24, \"the Violet Gate windows work fine from the inside but not from the outside\"): a "
         "window's own row keeps that window shut while its room opens; it only takes ways away, never adds one", [] {
        const float cam[3] = { 0, -30, 5 }, at[3] = { 0, 0, 5 };
        float vp[16];
        Camera(cam, at, 1.0f, 16.0f / 9.0f, vp);
        // as above: one exterior group (0), into room 5 (ordinals 1 and 3), room 6 (2) and a fake (4), of the WMO 77
        st::RootWindows r;
        r.path = 77;
        r.windows = { MakeWindow(-6, -10, 3, 0, -1, 0, 1.5f, 1.2f, 5, 1), MakeWindow(-2, -10, 3, 0, -1, 0, 1.5f, 1.2f, 6, 2),
                      MakeWindow(2, -10, 3, 0, -1, 0, 1.5f, 1.2f, 5, 3), MakeWindow(6, -10, 3, 0, -1, 0, 1.5f, 1.2f, -1, 4) };
        r.byGroup = { { 0u, 4u } };
        // the rows (glass::SeeThroughWindowMask's way): 3 never; 1 both ways (as if none); 2 from outside, its room from inside
        // only; another WMO's 3 none of these
        auto maskOf = [](uint32_t path, uint32_t ordinal) -> uint32_t {
            if (path != 77) return ~0u;
            return ordinal == 3 ? 0u : ordinal == 1 ? (st::kSeeFromOutside | st::kSeeFromInside) : ordinal == 2 ? st::kSeeFromOutside : ~0u;
        };
        CHECK(st::MaskWindows(r, maskOf) == 3);
        CHECK(r.windows[0].see == (st::kSeeFromOutside | st::kSeeFromInside) && r.windows[1].see == st::kSeeFromOutside && r.windows[2].see == 0u &&
              r.windows[3].see == ~0u);
        std::vector<uint32_t> flags(8, 0u);
        flags[5] = st::kSeeFromOutside;
        flags[6] = st::kSeeFromInside;
        // room 5 open from outside: its 1 listed, its 3 shut by its own row; 2's row adds nothing to room 6's
        CHECK(st::ListOutside(r, flags) == 1);
        CHECK(!r.windows[0].unlisted && r.windows[1].unlisted && r.windows[2].unlisted && r.windows[3].unlisted);
        const float lo[3] = { -20, -10, 0 }, hi[3] = { 20, 10, 15 };
        std::vector<st::WindowPlan> plan;
        unsigned budget = 10;
        st::PlanWindows(r.windows.data(), 4, cam, lo, hi, { -1, -1, 1, 1 }, kIdentity, cam, vp, budget, plan);
        // (the shut one left unseen and none of the budget, as a window into a room not listed)
        CHECK(budget == 9 && plan[0].candidate && !plan[2].candidate && plan[2].state == st::kUnseen);
        // the exterior group's own row: every window but 3 (the fake listed, as before)
        std::vector<uint32_t> group(8, 0u);
        group[0] = st::kSeeFromOutside;
        CHECK(st::ListOutside(r, group) == 3 && r.windows[2].unlisted);
        // another WMO's rows: none of its windows has one; and no table (no rows): as before
        r.path = 78;
        CHECK(st::MaskWindows(r, maskOf) == 0 && st::ListOutside(r, flags) == 2);
        r.path = 77;
        CHECK(st::MaskWindows(r, nullptr) == 0 && st::ListOutside(r, flags) == 2 && !r.windows[2].unlisted);
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
        // (2026-10-23) the budget to the nearest first, whatever the windows' order; a fake between rooms takes none of it
        {
            st::SeeWindow row[4] = { MakeWindow(0, 0.05f, 2, 0, -1, 0, 1, 1, -1, 18), MakeWindow(2.5f, 0.05f, 2, 0, -1, 0, 1, 1, -1, 19),
                                     MakeWindow(-2.5f, 0.05f, 2, 0, -1, 0, 1, 1, -1, 20), MakeWindow(1.0f, 0.05f, 2, 0, -1, 0, 1, 1, -1, 21) };
            row[0].interior = 3; row[1].interior = 3; row[2].interior = 3; row[3].interior = -1;
            const float near5[3] = { 2.5f, 5, 2 }, ahead[3] = { 2.5f, 0, 2 };
            Camera(near5, ahead, 1.4f, 16.0f / 9.0f, vp);
            unsigned two = 2;
            st::PlanInside(row, 4, near5, { -1, -1, 1, 1 }, kIdentity, near5, vp, two, plan, true);
            CHECK(two == 0 && plan[1].candidate && plan[0].candidate && !plan[2].candidate && !plan[3].candidate && plan[3].state == st::kShut);
            two = 2;
            st::PlanInside(row, 4, near5, { -1, -1, 1, 1 }, kIdentity, near5, vp, two, plan);   // (fakes take their share)
            CHECK(two == 0 && plan[1].candidate && plan[3].candidate && !plan[0].candidate && !plan[2].candidate);
        }
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
        // (2026-10-24) between rooms, a hall's far side past kVoidReach (Ironforge's Great Forge seen from a shop on its
        // ring: 66 .. 180 yd): within kVoidReach its rays meet nothing (the void: against); as far as the hall's box reaches
        // (RoomReach), its far wall from inside. A room no larger than that looks as far as before.
        {
            std::vector<float> hv;
            std::vector<uint16_t> hi;
            AddRect(hv, hi, 1, 100, -50, 50, -50, 50, -1);   // the hall's far wall (y 100), facing back at the shop
            st::RoomMesh hall;
            CHECK(hall.Build(hv.data(), static_cast<uint32_t>(hv.size() / 3), hi.data(), static_cast<uint32_t>(hi.size()), all(hi)));
            const st::SightMesh halls[1] = { { &hall, false } };
            const st::SeeWindow shop = MakeWindow(0, 0, 1.5f, 0, 1, 0, 0.8f, 1.0f, 0, 30);   // its normal out of the shop, into the hall
            const float inside[3] = { 0, -3, 1.5f };
            st::LookIn(shop, inside, halls, 1, nullptr, s);
            CHECK(s.back == 9 && !st::SightOpen(s, false));
            const st::GroupInfo hallBox{ 0x2000, { -60, 0, -50 }, { 60, 120, 60 } };
            const float reach = st::RoomReach(inside, hallBox);
            CHECK(reach > 140.0f && reach < st::kRoomReach);
            st::LookIn(shop, inside, halls, 1, nullptr, s, reach);
            CHECK(s.front == 9 && st::SightOpen(s, false));
            const st::GroupInfo small{ 0x2000, { -5, 0, 0 }, { 5, 8, 4 } }, huge{ 0x2000, { -900, -900, -50 }, { 900, 900, 50 } };
            CHECK(st::RoomReach(inside, small) == st::kVoidReach && st::RoomReach(inside, huge) == st::kRoomReach);
        }
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

    Test("LookIn's reveal (2026-10-22, stained glass): a thick wall's inner face round the window's opening and a side of a "
         "narrow opening, from behind within kReveal of the glass, passed; the same wall farther back, or a ceiling across "
         "the window, still against", [] {
        // the room: x -6..6, y `front`..12, z 0..8, its faces in; its front wall (y `front`) with an opening x -0.3..0.3,
        // z 2.6..3.4, smaller than the glass (x -1..1, z 1.5..4.5 at y 0: the chapel's tall windows), the opening's sides
        // (x -+0.3, y 0.2..front) facing into it
        auto room = [](float front, st::RoomMesh& m) {
            std::vector<float> v;
            std::vector<uint16_t> i;
            AddRect(v, i, 2, 0, -6, 6, front, 12, 1);                 // floor, ceiling
            AddRect(v, i, 2, 8, -6, 6, front, 12, -1);
            AddRect(v, i, 0, -6, front, 12, 0, 8, 1);                 // sides
            AddRect(v, i, 0, 6, front, 12, 0, 8, -1);
            AddRect(v, i, 1, 12, 0, 8, -6, 6, -1);                    // back wall
            AddRect(v, i, 1, front, 0, 2.6f, -6, 6, 1);               // the front wall round the opening, facing in
            AddRect(v, i, 1, front, 3.4f, 8, -6, 6, 1);
            AddRect(v, i, 1, front, 2.6f, 3.4f, -6, -0.3f, 1);
            AddRect(v, i, 1, front, 2.6f, 3.4f, 0.3f, 6, 1);
            AddRect(v, i, 0, -0.3f, 0.2f, front, 2.6f, 3.4f, 1);      // the opening's sides
            AddRect(v, i, 0, 0.3f, 0.2f, front, 2.6f, 3.4f, -1);
            return m.Build(v.data(), static_cast<uint32_t>(v.size() / 3), i.data(), static_cast<uint32_t>(i.size()),
                           { { 0u, static_cast<uint32_t>(i.size()) } });
        };
        st::RoomMesh thick, deep;
        CHECK(room(1.2f, thick) && room(3.0f, deep));
        const st::SeeWindow w = MakeWindow(0, 0, 3, 0, -1, 0, 1, 1.5f, 0, 11);
        st::Sight s;
        // head on: the middle ray through the opening, the others meet the wall's inner face from behind 1.2 yd past the
        // glass: passed, on to the room's back wall from inside
        const float ahead[3] = { 0, -12, 3 };
        const st::SightMesh thickRoom[1] = { { &thick, false } };
        st::LookIn(w, ahead, thickRoom, 1, nullptr, s);
        CHECK(s.front == 9 && s.back == 0 && st::SightOpen(s, false));
        // the same wall 3 yd back (past kReveal): against, as before
        const st::SightMesh deepRoom[1] = { { &deep, false } };
        st::LookIn(w, ahead, deepRoom, 1, nullptr, s);
        CHECK(s.front == 1 && s.back == 8 && !st::SightOpen(s, false));
        // at a slant from the right: the middle row's rays meet the opening's right side from behind (passed: along the
        // window), then its left side from the front; the other rows the wall's inner face from behind
        const float right[3] = { 8, -10, 3 };
        st::LookIn(w, right, thickRoom, 1, nullptr, s);
        CHECK(s.front == 9 && st::SightOpen(s, false));
        // a ceiling just behind the glass, across it (a dormer's): its face is up the window, not along it: against
        std::vector<float> cv;
        std::vector<uint16_t> ci;
        AddRect(cv, ci, 2, 0, -6, 6, 0.2f, 12, 1);       // floor
        AddRect(cv, ci, 2, 3, -6, 6, 0.2f, 12, -1);      // the ceiling at z 3, from y 0.2
        AddRect(cv, ci, 1, 12, 0, 3, -6, 6, -1);         // back wall
        st::RoomMesh low;
        CHECK(low.Build(cv.data(), static_cast<uint32_t>(cv.size() / 3), ci.data(), static_cast<uint32_t>(ci.size()),
                        { { 0u, static_cast<uint32_t>(ci.size()) } }));
        const st::SeeWindow over = MakeWindow(0, 0, 3.5f, 0, -1, 0, 1, 1, 0, 12);
        const float high[3] = { 0, -4, 12 };
        const st::SightMesh lowRoom[1] = { { &low, false } };
        st::LookIn(over, high, lowRoom, 1, nullptr, s);
        CHECK(s.front == 3 && s.back == 6 && !st::SightOpen(s, false));
        CHECK(st::kReveal == 2.0f && st::kRevealFacing == 0.7f);
    });

    Test("LookOut's lining (2026-10-22, Northshire Abbey: \"it does flicker between fake rooms and outside\"): a room's own "
         "window 2.7 yd inside the building's outer window (OuterBehind finds it) sees the street at a slant past the outer "
         "wall's back; with no outer window behind it, against; the building's own outside past the lining is the street", [] {
        // the room: x -6..6, y 0..12, z 0..8, its faces in, its front wall (y 0) with the window's opening x -0.65..0.65,
        // z 2..6, its pane in it (facing the room); the shell's outer wall 2.7 yd out (y -2.7, facing out) with the outer
        // window's opening x -0.7..0.7, z 2..6 and its pane; nothing between them (the abbey's embrasure isn't modelled)
        std::vector<float> rv, pv, sv, ov, wv;
        std::vector<uint16_t> ri, pi, si, oi, wi;
        AddRect(rv, ri, 2, 0, -6, 6, 0, 12, 1);
        AddRect(rv, ri, 2, 8, -6, 6, 0, 12, -1);
        AddRect(rv, ri, 0, -6, 0, 12, 0, 8, 1);
        AddRect(rv, ri, 0, 6, 0, 12, 0, 8, -1);
        AddRect(rv, ri, 1, 12, 0, 8, -6, 6, -1);
        AddRect(rv, ri, 1, 0, 0, 2, -6, 6, 1);
        AddRect(rv, ri, 1, 0, 6, 8, -6, 6, 1);
        AddRect(rv, ri, 1, 0, 2, 6, -6, -0.65f, 1);
        AddRect(rv, ri, 1, 0, 2, 6, 0.65f, 6, 1);
        AddRect(pv, pi, 1, 0.02f, 2, 6, -0.65f, 0.65f, 1);
        AddRect(sv, si, 1, -2.7f, 0, 8, -12, -0.7f, -1);
        AddRect(sv, si, 1, -2.7f, 0, 8, 0.7f, 12, -1);
        AddRect(sv, si, 1, -2.7f, 0, 2, -0.7f, 0.7f, -1);
        AddRect(sv, si, 1, -2.7f, 6, 8, -0.7f, 0.7f, -1);
        AddRect(ov, oi, 1, -2.7f, 2, 6, -0.7f, 0.7f, -1);
        AddRect(wv, wi, 1, -14, 0, 10, -30, 30, 1);   // another wing 14 yd out, facing the window
        st::RoomMesh room, pane, shell, outerPane, wing;
        const auto all = [](const std::vector<uint16_t>& i) { return std::vector<std::pair<uint32_t, uint32_t>>{ { 0u, static_cast<uint32_t>(i.size()) } }; };
        CHECK(room.Build(rv.data(), static_cast<uint32_t>(rv.size() / 3), ri.data(), static_cast<uint32_t>(ri.size()), all(ri)));
        CHECK(pane.Build(pv.data(), static_cast<uint32_t>(pv.size() / 3), pi.data(), static_cast<uint32_t>(pi.size()), all(pi), { st::kGlassTag }));
        CHECK(shell.Build(sv.data(), static_cast<uint32_t>(sv.size() / 3), si.data(), static_cast<uint32_t>(si.size()), all(si)));
        CHECK(outerPane.Build(ov.data(), static_cast<uint32_t>(ov.size() / 3), oi.data(), static_cast<uint32_t>(oi.size()), all(oi), { st::kGlassTag }));
        CHECK(wing.Build(wv.data(), static_cast<uint32_t>(wv.size() / 3), wi.data(), static_cast<uint32_t>(wi.size()), all(wi)));
        // OuterBehind: the outer window found 2.7 yd behind; not another WMO's, not one beside it, not one facing across
        const uint32_t path = gl::PathHash("world\\wmo\\test\\abbey.wmo");
        auto glassWindow = [&](uint32_t p, uint32_t flags, float x, float y, float nx, float ny) {
            gl::Window g;
            g.path = p; g.flags = flags; g.center[0] = x; g.center[1] = y; g.center[2] = 4;
            g.normal[0] = nx; g.normal[1] = ny; g.normal[2] = 0; g.halfWidth = 0.7f; g.halfHeight = 2;
            return g;
        };
        st::SeeWindow w = MakeWindow(0, 0.02f, 4, 0, -1, 0, 0.65f, 2, -1, 4);
        CHECK(Close(st::OuterBehind(w, path, { glassWindow(path, 1, 0, -2.7f, 0, -1) }), 2.72f, 1e-4f));
        CHECK(st::OuterBehind(w, path, { glassWindow(path + 1, 1, 0, -2.7f, 0, -1) }) == 0.0f);   // another WMO's
        CHECK(st::OuterBehind(w, path, { glassWindow(path, 0, 0, -2.7f, 0, -1) }) == 0.0f);       // a room's own, not outer
        CHECK(st::OuterBehind(w, path, { glassWindow(path, 1, 3, -2.7f, 0, -1) }) == 0.0f);       // beside it
        CHECK(st::OuterBehind(w, path, { glassWindow(path, 1, 0, -2.7f, 1, 0) }) == 0.0f);        // across it
        CHECK(st::OuterBehind(w, path, { glassWindow(path, 1, 0, -7.0f, 0, -1) }) == 0.0f);       // past kOuterReach
        // head on: out by the outer pane, as before
        const st::SightMesh building[4] = { { &room, false }, { &pane, false }, { &shell, true }, { &outerPane, true } };
        const float ahead[3] = { 0, 8, 4 }, slant[3] = { 5, 6, 4 };
        st::Sight s;
        w.outer = 0.0f;
        st::LookOut(w, ahead, building, 4, nullptr, s);
        CHECK(s.front == 9 && st::SightOpen(s, false));
        // at a slant the rays miss the outer window and meet the outer wall's back beside it: against without the lining ...
        st::LookOut(w, slant, building, 4, nullptr, s);
        CHECK(s.back == 9 && !st::SightOpen(s, false));
        // ... passed with it (the opening 2.72 yd deep): out into the open
        w.outer = 2.72f;
        st::LookOut(w, slant, building, 4, nullptr, s);
        CHECK(s.front == 9 && s.back == 0 && st::SightOpen(s, false));
        // and the building's other wing met from the front past it is the street seen out of the window
        const st::SightMesh winged[5] = { { &room, false }, { &pane, false }, { &shell, true }, { &outerPane, true }, { &wing, true } };
        st::LookOut(w, slant, winged, 5, nullptr, s);
        CHECK(s.front == 9 && st::SightOpen(s, false));
        w.outer = 0.0f;
        st::LookOut(w, slant, winged, 5, nullptr, s);
        CHECK(s.back == 9 && !st::SightOpen(s, false));
        CHECK(st::kLiningMargin == 1.0f && st::kOuterReach == 6.0f);
    });

    Test("LookOut through the sides of a deep opening (2026-10-24, Northshire Abbey: \"the windows *mostly* work except at weird "
         "angles\"): at a slant the rays meet the far jamb of the window's own opening from the front, and that counts as seen "
         "out; head on as before; a blind wall behind the glass, a wall from behind, no lining: as before", [] {
        // the room: x -6..6, y 0..12, z 0..8, its faces in, the window's opening x -0.7..0.7, z 2..6 in its front wall (y 0),
        // its pane (facing the room) just inside; the shell's wall 1.5 yd thick round the opening (its outer face at y -1.5
        // facing out, the opening's sides x -+0.7 facing into it, its sill and head z 2, 6 facing into it), its outer pane
        // 0.22 yd out (y -0.22), smaller than the opening (x -0.3..0.3, z 3..5): the abbey's window 0, 0.22 yd before its
        // outer glass, the stone round them 1.5 yd deep, a slanted ray past the outer glass meeting the opening's far side
        std::vector<float> rv, pv, sv, ov;
        std::vector<uint16_t> ri, pi, si, oi;
        AddRect(rv, ri, 2, 0, -6, 6, 0, 12, 1);
        AddRect(rv, ri, 2, 8, -6, 6, 0, 12, -1);
        AddRect(rv, ri, 0, -6, 0, 12, 0, 8, 1);
        AddRect(rv, ri, 0, 6, 0, 12, 0, 8, -1);
        AddRect(rv, ri, 1, 12, 0, 8, -6, 6, -1);
        AddRect(rv, ri, 1, 0, 0, 2, -6, 6, 1);
        AddRect(rv, ri, 1, 0, 6, 8, -6, 6, 1);
        AddRect(rv, ri, 1, 0, 2, 6, -6, -0.7f, 1);
        AddRect(rv, ri, 1, 0, 2, 6, 0.7f, 6, 1);
        AddRect(pv, pi, 1, 0.02f, 2, 6, -0.7f, 0.7f, 1);
        AddRect(sv, si, 1, -1.5f, 0, 8, -12, -0.7f, -1);   // the outer face round the opening
        AddRect(sv, si, 1, -1.5f, 0, 8, 0.7f, 12, -1);
        AddRect(sv, si, 1, -1.5f, 0, 2, -0.7f, 0.7f, -1);
        AddRect(sv, si, 1, -1.5f, 6, 8, -0.7f, 0.7f, -1);
        AddRect(sv, si, 0, -0.7f, -1.5f, 0, 2, 6, 1);      // the opening's sides (x = -+0.7: a = y, b = z), facing into it
        AddRect(sv, si, 0, 0.7f, -1.5f, 0, 2, 6, -1);
        AddRect(sv, si, 2, 2, -0.7f, 0.7f, -1.5f, 0, 1);   // its sill (z 2) and head (z 6)
        AddRect(sv, si, 2, 6, -0.7f, 0.7f, -1.5f, 0, -1);
        AddRect(ov, oi, 1, -0.22f, 3, 5, -0.3f, 0.3f, -1);
        st::RoomMesh room, pane, shell, outerPane;
        const auto all = [](const std::vector<uint16_t>& i) { return std::vector<std::pair<uint32_t, uint32_t>>{ { 0u, static_cast<uint32_t>(i.size()) } }; };
        CHECK(room.Build(rv.data(), static_cast<uint32_t>(rv.size() / 3), ri.data(), static_cast<uint32_t>(ri.size()), all(ri)));
        CHECK(pane.Build(pv.data(), static_cast<uint32_t>(pv.size() / 3), pi.data(), static_cast<uint32_t>(pi.size()), all(pi), { st::kGlassTag }));
        CHECK(shell.Build(sv.data(), static_cast<uint32_t>(sv.size() / 3), si.data(), static_cast<uint32_t>(si.size()), all(si)));
        CHECK(outerPane.Build(ov.data(), static_cast<uint32_t>(ov.size() / 3), oi.data(), static_cast<uint32_t>(oi.size()), all(oi), { st::kGlassTag }));
        const st::SightMesh building[4] = { { &room, false }, { &pane, false }, { &shell, true }, { &outerPane, true } };
        st::SeeWindow w = MakeWindow(0, 0.02f, 4, 0, -1, 0, 0.7f, 2, -1, 0);
        w.outer = 0.24f;
        st::Sight s;
        // head on: out by the outer pane
        const float ahead[3] = { 0, 8, 4 };
        st::LookOut(w, ahead, building, 4, nullptr, s);
        CHECK(s.front == 9 && st::SightOpen(s, false));
        // 56 degrees off the axis (the user's camera through the archway): past the glass most rays miss the outer pane and
        // meet the far side of the opening (x -0.7) from the front, 0.5 .. 1 yd on: seen out now (before 2026-10-24 neither)
        const float slant[3] = { 6.6f, 4.45f, 4 };
        st::LookOut(w, slant, building, 4, nullptr, s);
        std::printf("  at 56 degrees: %d see out, %d against, %d neither (the first not out %.2f yd past the glass)\n", s.front, s.back, s.none, s.missDepth);
        CHECK(s.front == 9 && s.back == 0 && s.none == 0 && st::SightOpen(s, false));
        // with no outer window behind it (w.outer 0) the shell met from the front past the glass was already the building's own
        // outside, the street seen out of it (2026-10-22): open as before
        w.outer = 0.0f;
        st::LookOut(w, slant, building, 4, nullptr, s);
        CHECK(s.front == 9 && st::SightOpen(s, false));
        w.outer = 0.24f;
        // a blind wall just behind the glass facing the room (the shell's, a hand's breadth out): neither, still shut
        std::vector<float> bv;
        std::vector<uint16_t> bi;
        AddRect(bv, bi, 1, -0.1f, 2, 6, -0.7f, 0.7f, 1);
        st::RoomMesh blind;
        CHECK(blind.Build(bv.data(), static_cast<uint32_t>(bv.size() / 3), bi.data(), static_cast<uint32_t>(bi.size()), all(bi)));
        const st::SightMesh walled[3] = { { &room, false }, { &pane, false }, { &blind, true } };
        st::LookOut(w, ahead, walled, 3, nullptr, s);
        CHECK(s.none == 9 && !st::SightOpen(s, false));
        st::LookOut(w, slant, walled, 3, nullptr, s);
        CHECK(!st::SightOpen(s, false));
        // a wall met from behind past the lining (the roof over a dormer's sloped pane, seen from below): against, as before
        std::vector<float> kv;
        std::vector<uint16_t> ki;
        AddRect(kv, ki, 1, -4.0f, 0, 10, -30, 30, -1);   // a wall 4 yd out facing away (its back to the window)
        st::RoomMesh back;
        CHECK(back.Build(kv.data(), static_cast<uint32_t>(kv.size() / 3), ki.data(), static_cast<uint32_t>(ki.size()), all(ki)));
        const st::SightMesh backed[3] = { { &room, false }, { &pane, false }, { &back, false } };
        st::LookOut(w, ahead, backed, 3, nullptr, s);
        CHECK(s.back == 9 && !st::SightOpen(s, false));
    });

    Test("NearGroups (2026-10-22): the groups round a window's room and its middle, grown 6 yd; none for a window far off", [] {
        std::vector<uint16_t> ids;
        // the inn's window 1 (north front, ground floor), the kitchen behind it: the kitchen, the shell, the rooms over and beside it
        const st::SeeWindow w = MakeWindow(-8.68f, -9.23f, 2.83f, 0, -1, 0, 2.74f, 1.31f, 5, 1);
        st::NearGroups(w, 5, kInnGroups, 12, ids);
        auto has = [&](uint16_t g) { return std::find(ids.begin(), ids.end(), g) != ids.end(); };
        CHECK(has(5) && has(4) && has(9) && has(3) && ids.size() >= 8);
        // a small room's window: the room alone and what's within 6 yd (room04, the porch: not the far west's room01)
        const st::SeeWindow porch = MakeWindow(21.9f, -2.6f, 3.0f, 1, 0, 0, 1, 1, 11, 2);
        st::NearGroups(porch, 11, kInnGroups, 12, ids);
        CHECK(has(11) && has(0) && has(4) && !has(8) && !has(10));
        const st::SeeWindow lone = MakeWindow(500, 500, 0, 0, -1, 0, 1, 1, -1, 3);
        st::NearGroups(lone, -1, kInnGroups, 12, ids);
        CHECK(ids.empty());
        st::NearGroups(w, 5, nullptr, 12, ids);
        CHECK(ids.empty());
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

    Test("OpenBits: the lowest open ordinal and a bit for each open window within 16 of it; shut, unseen and other batches' none; "
         "(2026-10-24) the sixteen with the most open windows, taking in the one nearest the camera", [] {
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
        // (2026-10-24, Dalaran: a batch over hundreds of windows, ordinals far apart) the sixteen that hold the most open
        // windows: 579 alone (597 shut), 1964, 1965 and 1968 together (1972 shut): from 1964
        std::vector<st::SeeWindow> city;
        const uint32_t ordinals[6] = { 579, 597, 1964, 1965, 1968, 1972 };
        for (int i = 0; i < 6; ++i) city.push_back(MakeWindow(10.0f * i, 0, 0, 0, -1, 0, 1, 1, 57, ordinals[i]));
        st::BatchWindows cb;
        cb.valid = true;
        cb.windows = { 0, 1, 2, 3, 4, 5 };
        const uint8_t cs[6] = { st::kOpened, st::kShut, st::kOpened, st::kOpened, st::kOpened, st::kShut };
        st::OpenBits(cb, city.data(), cs, 6, first, bits);
        CHECK(first == 1964.0f && bits == ((1u << 0) | (1u << 1) | (1u << 4)));
        // with the camera before 579 (its window nearest): from 579, the window in front of the camera see-through
        const float before579[3] = { 0, -5, 0 };
        st::OpenBits(cb, city.data(), cs, 6, first, bits, before579);
        CHECK(first == 579.0f && bits == 1u);
        // before 1968: the sixteen that take it in, the most of them: from 1964 again
        const float before1968[3] = { 40, -5, 0 };
        st::OpenBits(cb, city.data(), cs, 6, first, bits, before1968);
        CHECK(first == 1964.0f && bits == ((1u << 0) | (1u << 1) | (1u << 4)));
        // the inn's with a camera anywhere: as before (its open windows within sixteen of the lowest)
        const float anywhere[3] = { 3, -40, 2 };
        st::OpenBits(all, inn.data(), s.data(), s.size(), first, bits, anywhere);
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

    Test("PlanFrost (2026-10-23, \"if I set the blur to exactly 0 it doesn't work, any value above 0 does\"): frosted windows "
         "open with SeeThroughFrosted and the per-pixel redraw; over the blurred copy with the blur above 0 (shut if it can't run), "
         "at 0 as through clear glass", [] {
        const st::FrostPlan on = st::PlanFrost(true, true, 0.5f, false), zero = st::PlanFrost(true, true, 0.0f, false);
        CHECK(on.opens && on.copies && zero.opens && !zero.copies);
        CHECK(!st::PlanFrost(true, true, 0.5f, true).opens && st::PlanFrost(true, true, 0.0f, true).opens && !st::PlanFrost(true, true, 0.0f, true).copies);
        CHECK(!st::PlanFrost(false, true, 0.5f, false).opens && !st::PlanFrost(false, true, 0.0f, false).opens && !st::PlanFrost(true, false, 0.5f, false).opens);
        const st::FrostPlan nan = st::PlanFrost(true, true, std::nanf(""), false), neg = st::PlanFrost(true, true, -1.0f, false);
        CHECK(nan.opens && !nan.copies && neg.opens && !neg.copies);
    });

    Test("Deferral, a transition batch (2026-10-23, Kharanos's house from inside: \"it can't see outside even though it should be "
         "able to\"): the client draws it twice, its exterior-lit draw (Gx mode 9) silenced and its interior-lit one (7) deferred, as "
         "one batch; why a pending batch's draw was drawn as it was", [] {
        st::Deferral d;
        using D = st::Deferral::Draw;
        using W = st::Deferral::Why;
        CHECK(st::client::kBlendTransExterior == 9 && st::client::kBlendTransInterior == 7);
        // the house's room glass: MOBA 8 (a transition batch, both windows), then MOBA 21 (an interior one, window 4's edge)
        d.BeginBatch(true, 2910, 63, 4.0f, 0x3u, 11);
        CHECK(d.OnDraw(true, 3, 2910, 63, 9) == D::Silent && d.Pending() && d.Silenced() == 1 && d.LastWhy() == W::kNone);
        CHECK(d.OnDraw(true, 3, 2910, 63, 7) == D::Defer && !d.Pending() && d.Passed() == 0);
        st::Deferral::Entry e;
        d.Captured(e);
        d.BeginBatch(true, 10590, 6, 4.0f, 0x1u, 11);
        CHECK(d.OnDraw(true, 3, 10590, 6, 0) == D::Defer);
        d.Captured(e);
        auto all = d.Take();
        CHECK(all.size() == 2 && all[0].trans && all[0].batchStart == 2910 && all[0].batchCount == 63 && all[0].bits == 0x3u && !all[1].trans &&
              all[1].batchStart == 10590 && all[1].bits == 0x1u);
        // a second draw that isn't its interior-lit one: drawn, and why; mode 7 alone: blended, drawn
        d.BeginBatch(true, 2910, 63);
        CHECK(d.OnDraw(true, 3, 2910, 63, 9) == D::Silent && d.OnDraw(true, 3, 2910, 63, 2) == D::Pass && d.LastWhy() == W::kTransSecond);
        d.BeginBatch(true, 2910, 63);
        CHECK(d.OnDraw(true, 3, 2910, 63, 7) == D::Pass && d.LastWhy() == W::kBlending);
        // another range, not indexed triangles: why
        d.BeginBatch(true, 2910, 63);
        CHECK(d.OnDraw(true, 3, 0, 63, 0) == D::Pass && d.LastWhy() == W::kRange);
        d.BeginBatch(true, 2910, 63);
        CHECK(d.OnDraw(false, 3, 2910, 63, 0) == D::Pass && d.LastWhy() == W::kNotTriangles);
        // full: the pair drawn as the client draws it, neither silenced
        for (unsigned i = 0; i < st::kMaxDeferred; ++i) d.Captured(e);
        d.BeginBatch(true, 2910, 63);
        CHECK(d.OnDraw(true, 3, 2910, 63, 9) == D::Pass && d.LastWhy() == W::kFull && d.OnDraw(true, 3, 2910, 63, 7) == D::Pass);
        all = d.Take();
        // the loop's end drops a silenced batch's second draw; nothing pending: drawn, no reason
        d.BeginBatch(true, 2910, 63);
        CHECK(d.OnDraw(true, 3, 2910, 63, 9) == D::Silent);
        d.EndLoop();
        CHECK(!d.Pending() && d.OnDraw(true, 3, 2910, 63, 7) == D::Pass && d.LastWhy() == W::kNone);
        d.ResetCounts();
        CHECK(d.Silenced() == 0 && d.Passed() == 0);
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
        // blended (mode 2: alpha), or not indexed: drawn (a transition batch's pair: the test above)
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
            // SeeThroughBuildings.cdbc (2026-10-22), the same way
            const fs::path buildings = fs::exists(fs::path(data) / "SeeThroughBuildings.cdbc") ? fs::path(data) / "SeeThroughBuildings.cdbc"
                                       : fs::path(exe).parent_path() / ".." / ".." / "repo" / "data" / "SeeThroughBuildings.cdbc";
            std::printf("  SeeThroughBuildings.cdbc from %s\n", buildings.string().c_str());
            copied = copied && fs::copy_file(buildings, db / "SeeThroughBuildings.cdbc", fs::copy_options::overwrite_existing, ec);
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
            // the see-through buildings (SeeThroughBuildings.cdbc, 2026-10-22; before, the inn alone, hard-coded): the inn by
            // any case, from outside and inside; not its closed variant, Stormwind or nothing
            std::printf("  SeeThroughBuildings.cdbc: %zu buildings\n", gl::SeeThroughBuildings());
            // (2026-10-22: 24, with stained glass 33, with frosted glass 47; 2026-10-23 Ironforge's groups; 2026-10-24 Dalaran's)
            CHECK(gl::SeeThroughBuildings() == 49);
            // (37 group rows: Dalaran's four more from outside, 2026-10-24, and four more by window the same day; 20 window rows)
            CHECK(gl::SeeThroughGroupRows() == 37 && gl::SeeThroughWindowRows() == 20 && gl::SeeThroughGroupFlags(gl::PathHash("world\\wmo\\khazmodan\\cities\\ironforge\\ironforge.wmo"), 71) == st::kSeeBetween && gl::SeeThroughGroupFlags(gl::PathHash("world\\wmo\\khazmodan\\cities\\ironforge\\ironforge.wmo"), 47) == st::kSeeBetween &&
                  gl::SeeThroughGroupFlags(gl::PathHash("world\\wmo\\khazmodan\\cities\\ironforge\\ironforge.wmo"), 65) == st::kSeeBetween);   // (+ the Great Forge, 2026-10-23)
            CHECK(gl::SeeThroughFlags(gl::PathHash("world\\wmo\\kalimdor\\buildings\\orczeppelinhouse\\orczeppelinhouse.wmo")) == st::kSeeFromInside);
            CHECK(gl::SeeThroughFlags(gl::PathHash("world\\wmo\\azeroth\\buildings\\chapel\\redridgechapel.wmo")) == 3);
            CHECK(gl::SeeThroughFlags(gl::PathHash("world\\wmo\\northrend\\buildings\\human\\nd_human_townhall\\nd_human_townhall.wmo")) == st::kSeeFromOutside);
            CHECK(gl::SeeThroughFlags(gl::PathHash(kInn)) == (st::kSeeFromOutside | st::kSeeFromInside));
            CHECK(gl::SeeThroughFlags(gl::PathHash("World\\WMO\\Azeroth\\Buildings\\GoldshireInn\\GoldshireInn.wmo")) == 3);   // as the client names it
            CHECK(gl::SeeThroughFlags(gl::PathHash("world\\wmo\\azeroth\\buildings\\goldshireinn\\goldshireinn_closed.wmo")) == 0);
            CHECK(gl::SeeThroughFlags(gl::PathHash("world\\wmo\\azeroth\\buildings\\stormwind\\stormwind.wmo")) == 0);
            CHECK(gl::SeeThroughFlags(0) == 0);
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

        // --- Dalaran (2026-10-24, Phase D) ----------------------------------------------------------------------------------
        Test("Dalaran's rows through the real reader (2026-10-24, Phase D): its rooms by group, from outside, from inside and between "
             "rooms, the rest none; Ironforge's and the inn's as they were; a building listed whole lists every window from outside", [&] {
            const uint32_t dal = gl::PathHash(kDalaran);
            CHECK(gl::SeeThroughFlags(dal) == (st::kSeeFromOutside | st::kSeeFromInside | st::kSeeBetween));
            // (2026-10-24, the stone surrounds no windows, "yeah add those four": from outside Curiosities & Moore, 6, 7 and 9; the
            // Threads of Fate, 10 and 13; the Legerdemain Lounge's hall, 55; the Silver Enclave's hall, 78; the Violet Gate's halls
            // still from inside only; then by window, the same day: the Violet Gate's halls from outside too, 9, 13 and 55 again
            // and Vargoth's Retreat, 90, their flickering windows shut by rows of their own)
            std::map<uint32_t, uint32_t> want = { { 6, st::kSeeFromOutside }, { 7, st::kSeeFromOutside }, { 9, st::kSeeFromOutside },
                                                  { 10, st::kSeeFromOutside }, { 11, st::kSeeFromOutside }, { 13, st::kSeeFromOutside },
                                                  { 18, st::kSeeFromInside }, { 19, st::kSeeFromOutside | st::kSeeFromInside },
                                                  { 53, st::kSeeFromOutside | st::kSeeFromInside }, { 54, st::kSeeFromOutside | st::kSeeFromInside },
                                                  { 55, st::kSeeFromOutside }, { 57, st::kSeeFromOutside }, { 58, st::kSeeFromOutside },
                                                  { 71, st::kSeeBetween }, { 78, st::kSeeFromOutside }, { 89, st::kSeeFromOutside },
                                                  { 90, st::kSeeFromOutside } };
            unsigned rows = 0;
            for (uint32_t g = 0; g < 91; ++g)
            {
                const uint32_t f = gl::SeeThroughGroupFlags(dal, g);
                const auto it = want.find(g);
                if (f != (it == want.end() ? 0u : it->second)) throw std::runtime_error("Dalaran's group " + std::to_string(g) + ": flags " + std::to_string(f));
                rows += f ? 1 : 0;
            }
            CHECK(rows == 17 && gl::SeeThroughGroupFlags(dal, 3) == 0 && gl::SeeThroughGroupFlags(dal, 8) == 0 && gl::SeeThroughGroupFlags(dal, 12) == 0 &&
                  gl::SeeThroughGroupFlags(dal, 36) == 0);
            // the windows shut by their own rows (Flags 0: never), the swing survey's above 15% of its runs or a walk's more than
            // twice: the Violet Gate's 53 (3) and 54 (7), 9's 1948 (the walkway's), 13's 1932, 90's five and the Purple Parlor's three
            const uint32_t shutWindows[] = { 1955, 1956, 1957, 1952, 1953, 1954, 1985, 1988, 1990, 1992, 1948, 1932,
                                             4078, 4125, 4127, 4150, 4151, 3905, 3961, 3964 };
            for (const uint32_t ordinal : shutWindows)
                if (gl::SeeThroughWindowMask(dal, ordinal) != 0u) throw std::runtime_error("Dalaran's window " + std::to_string(ordinal) + ": no row shutting it");
            CHECK(gl::SeeThroughWindowRows() == std::size(shutWindows));
            // and the ones kept: 53's 1959 and 1991, 54's 1958 and 1961, 11's 1967 ("works fine afaict", 14%), 9's 1947, 55's 1971
            for (const uint32_t ordinal : { 1959u, 1991u, 1958u, 1961u, 1967u, 1947u, 1971u }) CHECK(gl::SeeThroughWindowMask(dal, ordinal) == ~0u);
            const uint32_t ironforge = gl::PathHash("world\\wmo\\khazmodan\\cities\\ironforge\\ironforge.wmo");
            CHECK(gl::SeeThroughFlags(ironforge) == st::kSeeBetween && gl::SeeThroughGroupFlags(ironforge, 65) == st::kSeeBetween &&
                  gl::SeeThroughGroupFlags(ironforge, 60) == 0);
            // the inn listed whole: every one of its 17 windows from outside, in RootFor's way (its groups' flags)
            std::vector<uint32_t> innFlags(12);
            for (uint32_t g = 0; g < 12; ++g) innFlags[g] = gl::SeeThroughGroupFlags(gl::PathHash(kInn), g);
            st::RootWindows listed = inn;
            CHECK(st::ListOutside(listed, innFlags) == 17);
            for (const st::SeeWindow& w : listed.windows) CHECK(!w.unlisted);
            // Ironforge's from outside: none (between rooms only), as before (its facade's windows fake anyway)
            std::vector<uint32_t> ifFlags(104);
            for (uint32_t g = 0; g < 104; ++g) ifFlags[g] = gl::SeeThroughGroupFlags(ironforge, g);
            st::RootWindows ifOut;
            ifOut.windows = { MakeWindow(0, 0, 0, 0, -1, 0, 1, 1, 47, 1) };
            ifOut.windows[0].group = 0;
            CHECK(st::ListOutside(ifOut, ifFlags) == 0);
        });

        // "as is, Dalaran windows cast light (which rules) but only in certain angles" (the user, 2026-10-24, in the Silver
        // Enclave's hall by the emblem quartermasters). The floor light's panes at the user's two snapshots through the real
        // reader (the shipped GlassPanes as glass::Load keeps them): the rule before took the camera's nearest, two sets; now
        // the same twelve, the hall's big pane among them.
        Test("the floor light's panes at Dalaran's snapshots through the real reader (2026-10-24): the rule before took two different "
             "sets from the two cameras; ChoosePanes the same, the big pane by the player among them", [&] {
            const gl::PaneIndex& index = gl::Panes();
            std::printf("  GlassPanes.cdbc: %zu panes kept\n", index.Size());
            CHECK(index.Size() > 10000);
            const float player[3] = { 5763.438f, 734.347f, 653.665f };
            gl::Inputs in;
            wxl_livingazeroth::sky::LightToward(0.2914f, in.sun);   // 07:00, the snapshots' sky
            in.daylight = 0.77f;
            const gl::Light light = gl::Evaluate(in);
            const gl::Settings s;
            const float cams[2][3] = { { 5762.745f, 748.501f, 664.881f }, { 5751.142f, 732.377f, 670.238f } };
            std::vector<uint32_t> before[2], now[2];
            for (int v = 0; v < 2; ++v)
            {
                std::vector<const gl::Pane*> candidates;
                index.Near(571, cams[v], 70.0f, 256, candidates);
                std::vector<gl::PaneLight> lit;
                for (const gl::Pane* p : candidates)
                {
                    gl::PaneLight pl;
                    if (gl::LightOfPane(*p, gl::MaterialById(p->material), light, in.sun, in.sunColour, 0.0, s, pl)) lit.push_back(pl);
                }
                for (int pass = 0; pass < 2; ++pass)   // (the rule before: the camera's room first, the nearest the camera; twelve, the cap then)
                    for (const gl::PaneLight& pl : lit)
                    {
                        if (before[v].size() >= 12) break;
                        bool inRoom = true;
                        for (int k = 0; k < 3; ++k) inRoom = inRoom && cams[v][k] >= pl.pane->roomMin[k] && cams[v][k] <= pl.pane->roomMax[k];
                        if (inRoom == (pass == 0)) before[v].push_back(pl.pane->id);
                    }
                std::sort(before[v].begin(), before[v].end());
                std::vector<uint32_t> last;
                std::vector<gl::PaneLight> chosen;
                gl::ChoosePanes(lit, player, player, 0.0f, false, static_cast<size_t>(gl::kFloorPanes), last, chosen);
                now[v] = last;
                std::printf("  camera %d: %zu panes near, %zu lit; before %u .. %u, now %u .. %u\n", v, candidates.size(), lit.size(),
                            before[v].empty() ? 0 : before[v].front(), before[v].empty() ? 0 : before[v].back(), now[v].empty() ? 0 : now[v].front(),
                            now[v].empty() ? 0 : now[v].back());
            }
            auto has = [](const std::vector<uint32_t>& v, uint32_t id) { return std::binary_search(v.begin(), v.end(), id); };
            CHECK(before[0].size() == 12 && before[0] != before[1] && has(before[0], 21045) && !has(before[1], 21045));
            CHECK(now[0].size() >= 12 && now[0] == now[1] && has(now[0], 21045));   // (sixteen at most since 2026-10-24)
        });

        if (!scan.empty())
            Test("Dalaran from the scan (2026-10-24, Phase D): the shipped rows list the windows into its five rooms from outside and no "
                 "other; from where to stand each room's windows open from outside, from inside onto the street and between rooms; a "
                 "frame's plan over every exterior group's windows", [&] {
                ScanBuilding b;
                CHECK(LoadScanBuilding(scan, kDalaran, b) && b.groups.size() == 91);
                const uint32_t path = gl::PathHash(kDalaran);
                std::vector<uint32_t> flags(b.groups.size());
                std::vector<uint8_t> between(b.groups.size(), 0);
                for (uint32_t g = 0; g < b.groups.size(); ++g)
                {
                    flags[g] = gl::SeeThroughGroupFlags(path, g);
                    between[g] = (flags[g] & st::kSeeBetween) && !(b.groups[g].flags & st::client::kExteriorGroup) ? 1 : 0;
                }
                // (each group's windows' sight points, from its glass batches, as the runtime's PointsOf)
                std::map<std::pair<const st::RootWindows*, uint32_t>, std::vector<st::SightPoints>> points;
                auto pointsOf = [&](const st::RootWindows& rw, uint32_t g, uint32_t i) -> const st::SightPoints* {
                    auto key = std::make_pair(&rw, g);
                    auto it = points.find(key);
                    if (it == points.end())
                    {
                        uint32_t n = 0;
                        const st::SeeWindow* w = rw.Group(g, n);
                        const ScanGroup& sg = b.File(static_cast<int>(g));
                        std::vector<std::pair<uint32_t, uint32_t>> ranges;
                        for (const ScanGroup::Moba& m : sg.batches)
                            if (m.material < b.glass.size() && b.glass[m.material]) ranges.emplace_back(m.start, m.count);
                        it = points.emplace(key, std::vector<st::SightPoints>{}).first;
                        if (w) st::GlassSightPoints(sg.vertices.data(), static_cast<uint32_t>(sg.vertices.size() / 3), sg.indices.data(),
                                                    static_cast<uint32_t>(sg.indices.size()), ranges, w, n, it->second);
                    }
                    return i < it->second.size() && it->second[i].n ? &it->second[i] : nullptr;
                };
                // from outside: the windows into the rooms the rows name listed, no other (2026-10-24: 11, 19, 57, 58 and 89; then
                // Curiosities & Moore's 6, 7 and 9, the Threads of Fate's 10 and 13, the Lounge's hall 55, the Silver Enclave's
                // hall 78)
                // (then by window, the same day: 9, 13, 53, 54, 55 and 90, twenty windows shut by their own rows)
                const std::vector<int> listedRooms = { 6, 7, 9, 10, 11, 13, 19, 53, 54, 55, 57, 58, 78, 89, 90 };
                st::RootWindows outside = st::BuildRoot(path, gl::Windows().All(), b.groups.data(), b.groups.size());
                const unsigned ownRows = st::MaskWindows(outside, gl::SeeThroughWindowMask);
                const unsigned listed = st::ListOutside(outside, flags);
                std::map<int, unsigned> byRoom;
                bool onlyThose = true;
                for (const st::SeeWindow& w : outside.windows)
                    if (!w.unlisted && w.interior >= 0)
                    {
                        ++byRoom[w.interior];
                        onlyThose = onlyThose && std::find(listedRooms.begin(), listedRooms.end(), w.interior) != listedRooms.end();
                    }
                std::string into;
                unsigned sum = 0;
                for (const int r : listedRooms) { into += " " + std::to_string(r) + ": " + std::to_string(byRoom[r]); sum += byRoom[r]; }
                std::printf("  Dalaran: %zu windows in its exterior groups, %u listed from outside (%u with a row of their own), into%s\n", outside.windows.size(),
                            listed, ownRows, into.c_str());
                // (2026-10-24, the Violet Gate: 3,949 before, 1,724 of them the stained windows' stone surrounds, now no windows:
                // into 11 were 28, 57 and 58 10 each)
                CHECK(outside.windows.size() == 2225 && onlyThose && listed == sum && ownRows == 20);
                // (89: five of them skylights; 65 before its three shut by their rows. 90: 79 before its five shut by theirs)
                CHECK(byRoom[11] == 3 && byRoom[19] == 10 && byRoom[57] == 2 && byRoom[58] == 2 && byRoom[89] == kInto89);
                CHECK(byRoom[6] == kInto6 && byRoom[7] == kInto7 && byRoom[10] == kInto10 && byRoom[78] == kInto78);
                CHECK(byRoom[9] == kInto9 && byRoom[13] == kInto13 && byRoom[55] == kInto55 && byRoom[53] == kInto53 && byRoom[54] == kInto54 &&
                      byRoom[90] == kInto90);
                // none of the twenty shut listed; still out, the Violet Citadel (36), the Visitor Center and the Wonderworks (8, 12)
                for (const st::SeeWindow& w : outside.windows)
                    if (w.see == 0u) CHECK(w.unlisted);
                CHECK(byRoom[36] == 0 && byRoom[8] == 0 && byRoom[12] == 0);
                // where to stand before each (the survey's SEETHROUGH_SPOTS: the street eye opening the most of the room's windows)
                struct Spot { int room; float eye[3]; unsigned atLeast; };
                const Spot fromOutside[] = { { 19, { 12.14f, 89.10f, -0.63f }, 4 }, { 57, { 27.42f, 18.90f, 7.54f }, 2 }, { 58, { 48.17f, -13.54f, 9.07f }, 2 },
                                             { 11, { -21.46f, 18.15f, 7.32f }, 2 }, { 89, { -80.96f, 178.11f, 206.39f }, 9 },
                                             { 6, { -46.57f, -60.51f, 10.32f }, 2 }, { 7, { -25.81f, -85.27f, 10.17f }, 2 },
                                             { 10, { -46.22f, -10.29f, 3.70f }, 2 },
                                             { 78, { kSpot78[0], kSpot78[1], kSpot78[2] }, 1 },
                                             // (by window, the same day: the spots for the windows that open, the shut ones left out)
                                             { 53, { 32.54f, -88.57f, 9.75f }, 1 }, { 54, { 61.83f, -64.03f, 10.87f }, 2 },
                                             { 9, { -47.43f, -66.76f, 9.79f }, 1 }, { 13, { -47.08f, -0.36f, 6.75f }, 1 },
                                             { 55, { 10.79f, 12.27f, 17.96f }, 1 }, { 90, { -146.14f, 102.95f, 228.14f }, 9 } };
                for (const Spot& sp : fromOutside)
                {
                    unsigned facing = 0, open = 0;
                    for (uint32_t g = 0; g < b.groups.size(); ++g)
                    {
                        uint32_t n = 0;
                        const st::SeeWindow* w = outside.Group(g, n);
                        for (uint32_t i = 0; w && i < n; ++i)
                        {
                            if (w[i].interior != sp.room || w[i].unlisted || !st::Facing(w[i], sp.eye)) continue;
                            const float d[3] = { sp.eye[0] - w[i].center[0], sp.eye[1] - w[i].center[1], sp.eye[2] - w[i].center[2] };
                            if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > st::kRange * st::kRange) continue;
                            ++facing;
                            const std::vector<st::SightMesh> around = b.Near(w[i], sp.room);
                            st::Sight s;
                            st::LookIn(w[i], sp.eye, around.data(), around.size(), pointsOf(outside, g, i), s);
                            open += st::SightOpen(s, false) ? 1 : 0;
                        }
                    }
                    std::printf("  from outside, before room %d at %.2f %.2f %.2f: %u of the %u windows into it facing there open\n", sp.room, sp.eye[0], sp.eye[1],
                                sp.eye[2], open, facing);
                    CHECK(open >= sp.atLeast);
                }
                // from inside onto the street (ways out: Cartier & Co., the Violet Gate's halls, the Agronomical Apothecary), and between
                // rooms (the Circle of Wills' window 1681 onto Cantrips & Crows, 70)
                st::RootWindows inside = st::BuildInside(path, gl::Windows().All(), b.groups.data(), b.groups.size(), between);
                const Spot fromInside[5] = { { 19, { 2.45f, 64.69f, 4.31f }, 10 }, { 53, { 16.85f, -80.02f, 13.62f }, 4 }, { 54, { 31.36f, -58.05f, 14.73f }, 4 },
                                             { 18, { 17.09f, 63.66f, 3.08f }, 1 }, { 71, { -18.1f, 33.2f, 0.0f }, 1 } };
                for (const Spot& sp0 : fromInside)
                {
                    Spot sp = sp0;
                    const uint32_t g = static_cast<uint32_t>(sp.room);
                    if (g == 71)   // (on the tunnel's floor, 3 yd before the window)
                    {
                        bool found = false;
                        const std::vector<st::SightMesh> room = { { &b.Mesh(71), false } };
                        sp.eye[2] = b.Ground(sp.eye[0], sp.eye[1], -19.0f, room, &found) + 1.8f;
                        CHECK(found);
                    }
                    uint32_t n = 0;
                    const st::SeeWindow* w = inside.Group(g, n);
                    CHECK(w && n > 0);
                    unsigned facing = 0, open = 0, onto = 0;
                    for (uint32_t i = 0; i < n; ++i)
                    {
                        const float d[3] = { sp.eye[0] - w[i].center[0], sp.eye[1] - w[i].center[1], sp.eye[2] - w[i].center[2] };
                        if (!st::FacingIn(w[i], sp.eye) || d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > st::kInsideRange * st::kInsideRange) continue;
                        if (between[g] && w[i].interior < 0) continue;   // (between rooms a window onto nothing: a fake)
                        ++facing;
                        const std::vector<st::SightMesh> around = b.Near(w[i], w[i].interior >= 0 ? w[i].interior : static_cast<int>(g));
                        st::Sight s;
                        if (w[i].interior >= 0)
                        {
                            st::LookIn(w[i], sp.eye, around.data(), around.size(), pointsOf(inside, g, i), s, st::RoomReach(sp.eye, b.groups[static_cast<size_t>(w[i].interior)]));
                            onto += w[i].interior == 70 && w[i].ordinal == 1681 && st::SightOpen(s, false) ? 1 : 0;
                        }
                        else st::LookOut(w[i], sp.eye, around.data(), around.size(), pointsOf(inside, g, i), s);
                        open += st::SightOpen(s, false) ? 1 : 0;
                    }
                    std::printf("  from inside group %u at %.2f %.2f %.2f: %u of the %u windows facing there open%s\n", g, sp.eye[0], sp.eye[1], sp.eye[2], open,
                                facing, g == 71 ? (onto ? " (1681 onto Cantrips & Crows among them)" : " (1681 NOT open)") : " as ways out");
                    CHECK(open >= sp.atLeast && (g != 71 || onto == 1));
                }
                // the walk from outside's plan over every exterior group's windows, from before Cartier & Co. (every window planned,
                // only the listed ones candidates): what it costs a frame
                const float cam[3] = { 12.14f, 89.10f, 1.4f };
                float at[3] = { 0, 0, 0 };   // (its windows' middle)
                unsigned into19 = 0;
                for (const st::SeeWindow& w : outside.windows)
                    if (w.interior == 19 && !w.unlisted) { for (int k = 0; k < 3; ++k) at[k] += w.center[k]; ++into19; }
                CHECK(into19 > 0);
                for (float& v : at) v /= static_cast<float>(into19);
                float vp[16];
                Camera(cam, at, 1.0f, 16.0f / 9.0f, vp);
                unsigned candidates = 0, planned = 0;
                std::vector<st::WindowPlan> plan;
                const auto t0 = std::chrono::steady_clock::now();
                for (int repeat = 0; repeat < 10; ++repeat)
                {
                    unsigned budget = st::kMaxOpenPerFrame;
                    candidates = planned = 0;
                    for (uint32_t g = 0; g < b.groups.size(); ++g)
                    {
                        uint32_t n = 0;
                        const st::SeeWindow* w = outside.Group(g, n);
                        if (!w) continue;
                        st::PlanWindows(w, n, cam, b.groups[g].lo, b.groups[g].hi, { -1, -1, 1, 1 }, kIdentity, cam, vp, budget, plan);
                        planned += n;
                        for (const st::WindowPlan& p : plan) candidates += p.candidate ? 1 : 0;
                    }
                }
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10.0;
                std::printf("  a frame's plan from before Cartier & Co.: %u windows of the city's exterior groups planned, %u candidates, %.3f ms\n", planned,
                            candidates, ms);
                CHECK(planned == 2225 && candidates > 0 && candidates <= 10 && ms < 50.0);
            });

        // "the Northshire Abbey windows *mostly* work except at weird angles" (the user, 2026-10-24, in the nave; snapshots
        // 20261024-041636 and -041712): from the nave's corner window 0 opened, from the archway at a slant it stayed shut, two
        // of its nine rays seeing the street and seven meeting the far side of its deep opening from the front.
        if (!scan.empty())
            Test("Northshire Abbey's nave from the user's cameras through the scan (2026-10-24): window 0 opens from the archway at "
                 "56 degrees off its axis (its opening's far side seen out), as from the corner; windows 4 .. 6 as before", [&] {
                ScanBuilding b;
                const char* const abbey = "world\\wmo\\azeroth\\buildings\\nsabbey\\nsabbey.wmo";
                CHECK(LoadScanBuilding(scan, abbey, b) && b.groups.size() == 14);
                const uint32_t path = gl::PathHash(abbey);
                const st::RootWindows inside = st::BuildInside(path, gl::Windows().All(), b.groups.size());   // (each with its outer glass)
                uint32_t n = 0;
                const st::SeeWindow* w = inside.Group(0, n);
                CHECK(w && n == 10);
                const ScanGroup& sg = b.File(0);
                std::vector<std::pair<uint32_t, uint32_t>> ranges;
                for (const ScanGroup::Moba& m : sg.batches)
                    if (m.material < b.glass.size() && b.glass[m.material]) ranges.emplace_back(m.start, m.count);
                std::vector<st::SightPoints> points;
                st::GlassSightPoints(sg.vertices.data(), static_cast<uint32_t>(sg.vertices.size() / 3), sg.indices.data(),
                                     static_cast<uint32_t>(sg.indices.size()), ranges, w, n, points);
                auto look = [&](uint32_t ordinal, const float eye[3], st::Sight& s) {
                    for (uint32_t i = 0; i < n; ++i)
                        if (w[i].ordinal == ordinal)
                        {
                            const std::vector<st::SightMesh> around = b.Near(w[i], 0);
                            st::LookOut(w[i], eye, around.data(), around.size(), i < points.size() && points[i].n ? &points[i] : nullptr, s);
                            return true;
                        }
                    return false;
                };
                const float corner[3] = { -19.49f, 4.20f, 10.13f }, archway[3] = { -12.568f, 2.772f, 7.649f };
                st::Sight s;
                CHECK(look(0, archway, s));
                std::printf("  window 0 from the archway: %d see out, %d against, %d neither\n", s.front, s.back, s.none);
                CHECK(s.back == 0 && s.front == 9 && st::SightOpen(s, false));
                CHECK(look(0, corner, s));
                std::printf("  window 0 from the corner: %d see out, %d against, %d neither\n", s.front, s.back, s.none);
                CHECK(s.front == 9 && st::SightOpen(s, false));
                for (const uint32_t o : { 4u, 5u, 6u })
                    for (const float* eye : { corner, archway })
                    {
                        CHECK(look(o, eye, s));
                        CHECK(st::SightOpen(s, false));
                    }
                // window 1 from the archway still shut: six of its rays meet a wall from behind 9.7 yd on (the void past it)
                CHECK(look(1, archway, s));
                std::printf("  window 1 from the archway: %d see out, %d against, %d neither\n", s.front, s.back, s.none);
                CHECK(s.back >= 3 && !st::SightOpen(s, false));
            });

        // "hmm, Curiosities' outside windows seem to flicker a bit" (the user, 2026-10-24, on the raised walkway by Curiosities &
        // Moore's wall at Dalaran 49.7, 58.1; snapshots 20261024-162235 and -162259: the player still, the camera 16 yd behind
        // and 9 up, swung 56 degrees between them). Its upstairs room's (9) window 1948 opens from 19% of its street views: from
        // the first camera 7 of its 9 rays see the room, from the second none, and a camera swinging round the player there
        // crosses that narrow cone's edge every swing. The rows listed room 9 from outside no more (nor 13, the same building's
        // other upstairs room, nor the Lounge's hall 55), so 1948 stayed the client's glass; since window rows (the same day) room
        // 9 is listed again and 1948 alone is shut, by a row of its own (the swing survey's 17% of its runs, over the 15% cut).
        if (!scan.empty())
            Test("Curiosities & Moore's walkway (2026-10-24, snapshots 162235 and 162259): window 1948 open from the first camera, shut from "
                 "the second, changing more than twice as the camera swings round the player there; its room listed from outside, it shut "
                 "by its own row", [&] {
                ScanBuilding b;
                CHECK(LoadScanBuilding(scan, kDalaran, b) && b.groups.size() == 91);
                const uint32_t path = gl::PathHash(kDalaran);
                st::RootWindows outside = st::BuildRoot(path, gl::Windows().All(), b.groups.data(), b.groups.size());
                std::vector<uint32_t> flags(b.groups.size());
                for (uint32_t g = 0; g < b.groups.size(); ++g) flags[g] = gl::SeeThroughGroupFlags(path, g);
                st::MaskWindows(outside, gl::SeeThroughWindowMask);
                st::ListOutside(outside, flags);
                uint32_t n = 0;
                const st::SeeWindow* w = outside.Group(3, n);
                CHECK(w && n > 0);
                int at = -1, kept = -1;
                for (uint32_t i = 0; i < n; ++i)
                {
                    at = w[i].ordinal == 1948 ? static_cast<int>(i) : at;
                    kept = w[i].ordinal == 1947 ? static_cast<int>(i) : kept;
                }
                CHECK(at >= 0 && w[at].interior == 9 && kept >= 0 && w[kept].interior == 9);
                // (its room listed: 1947, the one street views open, 86% of them; 1948 shut by its row)
                CHECK((flags[9] & st::kSeeFromOutside) && !w[kept].unlisted && w[at].see == 0u);
                const st::SeeWindow& win = w[at];
                const ScanGroup& sg = b.File(3);
                std::vector<std::pair<uint32_t, uint32_t>> ranges;
                for (const ScanGroup::Moba& m : sg.batches)
                    if (m.material < b.glass.size() && b.glass[m.material]) ranges.emplace_back(m.start, m.count);
                std::vector<st::SightPoints> points;
                st::GlassSightPoints(sg.vertices.data(), static_cast<uint32_t>(sg.vertices.size() / 3), sg.indices.data(),
                                     static_cast<uint32_t>(sg.indices.size()), ranges, w, n, points);
                const st::SightPoints* sp = static_cast<size_t>(at) < points.size() && points[at].n ? &points[at] : nullptr;
                const std::vector<st::SightMesh> around = b.Near(win, 9);
                const float camA[3] = { -52.451f, -81.996f, 15.567f }, camB[3] = { -57.792f, -70.822f, 16.184f };
                st::Sight a, c;
                st::LookIn(win, camA, around.data(), around.size(), sp, a);
                st::LookIn(win, camB, around.data(), around.size(), sp, c);
                std::printf("  window 1948 from the first camera: %d see its room, %d against; from the second: %d, %d\n", a.front, a.back, c.front, c.back);
                CHECK(st::SightOpen(a, false) && !st::SightOpen(c, false));
                // the camera swung round the player where they stood (WMO space; the head 1.7 yd over the walkway, 2 yd from the wall
                // under the window), 16 yd out at 30 degrees up, 40 degrees either way about the middle of the user's two cameras
                // (they looked along the walkway, 56 degrees apart) and back, 20 degrees a second, stopped by walls, as the survey's swings
                const float head[3] = { -44.88f, -71.10f, 6.32f + 1.7f };
                float ux = (camA[0] - head[0]) / std::hypot(camA[0] - head[0], camA[1] - head[1]) + (camB[0] - head[0]) / std::hypot(camB[0] - head[0], camB[1] - head[1]);
                float uy = (camA[1] - head[1]) / std::hypot(camA[0] - head[0], camA[1] - head[1]) + (camB[1] - head[1]) / std::hypot(camB[0] - head[0], camB[1] - head[1]);
                const float ul = std::sqrt(ux * ux + uy * uy);
                const float dx = -ux / ul, dy = -uy / ul;   // (the camera behind the head along -d)
                const float back = 16.0f * std::cos(30.0f * 3.14159265f / 180.0f), up = 16.0f * std::sin(30.0f * 3.14159265f / 180.0f);
                st::Gate gate;
                bool last = false, first = true;
                unsigned changes = 0, openFrames = 0, frames = 0;
                float reachLo = 1e9f, reachHi = 0.0f;
                for (int f = 0; f < 480; ++f)
                {
                    const float t = static_cast<float>(f) / 480.0f;   // -40 .. +40 .. -40 degrees, 20 a second
                    const float phi = (t < 0.5f ? -40.0f + 160.0f * t : 120.0f - 160.0f * t) * 3.14159265f / 180.0f;
                    const float want[3] = { head[0] - (dx * std::cos(phi) - dy * std::sin(phi)) * back, head[1] - (dy * std::cos(phi) + dx * std::sin(phi)) * back,
                                            head[2] + up };
                    const float reach = b.CameraReach(head, want), s = reach / 16.0f;
                    reachLo = std::min(reachLo, reach); reachHi = std::max(reachHi, reach);
                    const float eye[3] = { head[0] + (want[0] - head[0]) * s, head[1] + (want[1] - head[1]) * s, head[2] + (want[2] - head[2]) * s };
                    if (!st::Facing(win, eye)) { first = true; gate = st::Gate{}; continue; }
                    st::Sight sgt;
                    st::LookIn(win, eye, around.data(), around.size(), sp, sgt);
                    const bool held = st::Hold(gate, st::SightOpen(sgt, gate.seen && gate.open), 1.0f / 60.0f);
                    changes += !first && held != last ? 1 : 0;
                    last = held;
                    first = false;
                    ++frames;
                    openFrames += held ? 1 : 0;
                }
                std::printf("  the camera swung round the player on the walkway (it reached %.1f .. %.1f yd): window 1948 open %u of %u frames, its verdict "
                            "changed %u times; listed from outside: %s\n", reachLo, reachHi, openFrames, frames, changes, win.unlisted ? "no" : "yes");
                CHECK(changes > 2 && win.unlisted);
            });

        // "re: dalaran, amusingly the Violet Gate windows' *borders* are transparent when seen from the inside, and the actual
        // glass flickers" (the user, 2026-10-24, at Dalaran 55.6, 43.2; snapshot 20261024-083247, from inside group 54). The
        // surrounds of its three tall stained windows (eb_dalaran_window3: stone and a gold frame, no glass) were frosted
        // windows, 24 of the hall's 28: they opened onto the street, and with the 16 a frame from inside, nearest first, the
        // four stained panes in them lost their places as the camera moved (the snapshot: 2867 and 2868 over the budget).
        // GlassWindows has no row for them now (gen_glass NO_WINDOWS). Through the scan with the shipped table: the hall's
        // windows; two walks with the user's camera (the player along the window wall, then standing where they stood with
        // the camera swinging round them), each frame PlanInside's budget and each candidate's held verdict as the runtime's.
        // With the table before, the same walks print the panes refused the budget (the test fails there).
        if (!scan.empty())
            Test("the Violet Gate's hall from the user's camera through the scan (2026-10-24, snapshot 20261024-083247): its windows are "
                 "its four stained panes, not their surrounds; on two walks every pane on the screen gets the budget every frame and its "
                 "verdict holds", [&] {
                ScanBuilding b;
                CHECK(LoadScanBuilding(scan, kDalaran, b) && b.groups.size() == 91);
                const uint32_t path = gl::PathHash(kDalaran);
                const st::RootWindows inside = st::BuildInside(path, gl::Windows().All(), b.groups.size());
                // the surrounds: no frosted window left in the Violet Gate's halls, the Violet Citadel, the Silver Enclave's hall
                unsigned surrounds = 0;
                for (const uint32_t g : { 36u, 53u, 54u, 78u })
                {
                    uint32_t k = 0;
                    const st::SeeWindow* gw = inside.Group(g, k);
                    for (uint32_t i = 0; gw && i < k; ++i) surrounds += gw[i].kind == gl::kFrosted ? 1 : 0;
                }
                uint32_t n = 0;
                const st::SeeWindow* w = inside.Group(54, n);
                CHECK(w && n > 0);
                std::string list;
                unsigned stained = 0;
                for (uint32_t i = 0; i < n; ++i)
                {
                    stained += w[i].kind == gl::kStained ? 1 : 0;
                    if (w[i].kind == gl::kStained) list += " " + std::to_string(w[i].ordinal);
                }
                std::printf("  group 54: %u windows, %u of them stained (%s); frosted windows in groups 36, 53, 54 and 78: %u\n", n, stained,
                            list.c_str(), surrounds);
                const ScanGroup& sg = b.File(54);
                std::vector<std::pair<uint32_t, uint32_t>> ranges;
                for (const ScanGroup::Moba& m : sg.batches)
                    if (m.material < b.glass.size() && b.glass[m.material]) ranges.emplace_back(m.start, m.count);
                std::vector<st::SightPoints> points;
                st::GlassSightPoints(sg.vertices.data(), static_cast<uint32_t>(sg.vertices.size() / 3), sg.indices.data(),
                                     static_cast<uint32_t>(sg.indices.size()), ranges, w, n, points);
                std::vector<std::vector<st::SightMesh>> around(n);
                for (uint32_t i = 0; i < n; ++i) around[i] = b.Near(w[i], 54);
                // the hall (group 54's box: x 27.9 .. 50.9, y -67.6 .. -52.1, z 10.5 .. 31.6): the camera kept a yard inside it, as
                // the client's camera stops at the walls; 60 frames a second
                auto keepIn = [](float c[3]) {
                    c[0] = std::clamp(c[0], 28.9f, 49.0f); c[1] = std::clamp(c[1], -66.6f, -53.1f); c[2] = std::clamp(c[2], 11.5f, 30.6f);
                };
                static constexpr float kDt = 1.0f / 60.0f, kFov = 54.0f * 3.14159265f / 180.0f;
                struct Tally { unsigned onScreen = 0, refused = 0, open = 0, changes = 0; bool was = false, wasOn = false; };
                unsigned surroundOpen = 0;
                auto walk = [&](const char* name, int frames, auto&& at) {
                    std::vector<Tally> t(n);
                    std::vector<st::Gate> gates(n);
                    for (int f = 0; f < frames; ++f)
                    {
                        float player[3], cam[3];
                        at(f, player, cam);
                        keepIn(cam);
                        const float look[3] = { player[0], player[1], player[2] + 1.5f };
                        float vp[16];
                        Camera(cam, look, kFov, 16.0f / 9.0f, vp);
                        std::vector<st::WindowPlan> plan;
                        unsigned budget = st::kMaxInsidePerFrame;
                        st::PlanInside(w, n, cam, { -1, -1, 1, 1 }, kIdentity, cam, vp, budget, plan);
                        for (uint32_t i = 0; i < n; ++i)
                        {
                            const bool on = plan[i].state == st::kShut;   // facing in and on the screen
                            bool open = false;
                            if (plan[i].candidate)
                            {
                                st::Sight s;
                                st::LookOut(w[i], cam, around[i].data(), around[i].size(), i < points.size() && points[i].n ? &points[i] : nullptr, s);
                                open = st::Hold(gates[i], st::SightOpen(s, gates[i].seen && gates[i].open), kDt);
                            }
                            Tally& ti = t[i];
                            if (on)
                            {
                                const float d[3] = { cam[0] - w[i].center[0], cam[1] - w[i].center[1], cam[2] - w[i].center[2] };
                                ++ti.onScreen;
                                ti.refused += !plan[i].candidate && d[0] * d[0] + d[1] * d[1] + d[2] * d[2] <= st::kInsideRange * st::kInsideRange ? 1 : 0;
                                ti.open += open ? 1 : 0;
                                ti.changes += ti.wasOn && open != ti.was ? 1 : 0;
                            }
                            ti.was = open;
                            ti.wasOn = on;
                            surroundOpen += open && w[i].kind == gl::kFrosted ? 1 : 0;
                        }
                    }
                    unsigned refused = 0, changes = 0, most = 0;
                    for (uint32_t i = 0; i < n; ++i)
                    {
                        if (w[i].kind != gl::kStained) continue;
                        std::printf("  %s: pane %u on the screen %u frames, open %u, refused the budget %u, its verdict changed %u times\n", name,
                                    w[i].ordinal, t[i].onScreen, t[i].open, t[i].refused, t[i].changes);
                        refused += t[i].refused;
                        changes += t[i].changes;
                        most = std::max(most, t[i].changes);
                    }
                    return std::make_tuple(refused, changes, most);
                };
                // the player along the window wall and back (a walk, 2.5 yd a second), the camera 13 yd behind and 7.8 up swinging
                // 35 degrees either way every 2.5 s
                const auto [refusedA, changesA, mostA] = walk("along the wall", 576, [](int f, float player[3], float cam[3]) {
                    const float s = f * kDt, y = -54.0f - 12.0f * (1.0f - std::fabs(1.0f - std::fmod(s * 2.5f / 12.0f, 2.0f)));
                    player[0] = 47.5f; player[1] = y; player[2] = 11.31f;
                    const float a = 35.0f * 3.14159265f / 180.0f * std::sin(2.0f * 3.14159265f * s / 2.5f);
                    cam[0] = player[0] - 13.0f * std::cos(a); cam[1] = player[1] - 13.0f * std::sin(a); cam[2] = player[2] + 7.8f;
                });
                // where the user stood (the snapshot's player, WMO space), the camera from where it was swinging 60 degrees either way
                // over 6 s
                const auto [refusedB, changesB, mostB] = walk("the user's spot", 360, [](int f, float player[3], float cam[3]) {
                    player[0] = 49.57f; player[1] = -57.04f; player[2] = 11.31f;
                    const float a0 = std::atan2(-56.141f + 57.04f, 36.763f - 49.57f);   // the snapshot's camera about the player
                    const float a = a0 + 60.0f * 3.14159265f / 180.0f * std::sin(2.0f * 3.14159265f * f * kDt / 6.0f);
                    const float r = std::hypot(36.763f - 49.57f, -56.141f + 57.04f);
                    cam[0] = player[0] + r * std::cos(a); cam[1] = player[1] + r * std::sin(a); cam[2] = 19.107f;
                });
                std::printf("  the panes refused the budget %u and %u frames, their verdicts changed %u and %u times (at most %u and %u for one); "
                            "frames a surround stood open %u\n", refusedA, refusedB, changesA, changesB, mostA, mostB, surroundOpen);
                CHECK(n == 4 && stained == 4 && surrounds == 0 && surroundOpen == 0);
                CHECK(refusedA == 0 && refusedB == 0 && mostA <= 2 && mostB <= 2);
            });

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
        if (const char* probe = std::getenv("SEETHROUGH_PROBE"))
            g_haveProbe = std::sscanf(probe, "%f %f %f", &g_probe[0], &g_probe[1], &g_probe[2]) == 3;
        // the survey of buildings (SEETHROUGH_SURVEY: a file of WMO paths, one a line; # a comment): printed, not checked
        if (const char* list = std::getenv("SEETHROUGH_SURVEY"); list && !scan.empty())
        {
            std::ifstream f(list);
            std::string line;
            std::printf("the survey of buildings (%s):\n", list);
            while (std::getline(f, line))
            {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                if (line.empty() || line[0] == '#') continue;
                SurveyRow row;
                if (!SurveyBuilding(scan, line, gl::Windows().All(), row)) { std::printf("  %s: not in the scan\n", line.c_str()); continue; }
                PrintSurveyRow(row);
                std::fflush(stdout);
            }
        }
        // a snapshot's view (SEETHROUGH_SNAPSHOT: a file the snapshot key wrote, debug/Snapshot): its building surveyed with
        // the probe at its camera, what each window facing it sees from there; printed, not checked
        if (const char* file = std::getenv("SEETHROUGH_SNAPSHOT"); file && !scan.empty())
        {
            std::ifstream f(file, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            wxl_livingazeroth::snapshot::WmoView v;
            if (!wxl_livingazeroth::snapshot::ParseView(text, v)) std::printf("the snapshot %s: no WMO view in it\n", file);
            else
            {
                for (int k = 0; k < 3; ++k) g_probe[k] = v.cam[k];
                g_haveProbe = true;
                g_probeSides = v.inside ? 2 : 1;
                std::printf("the snapshot %s: %s group %d (from %s), the camera at %.2f %.2f %.2f looking %.3f %.3f %.3f; open %s, shut %s\n", file,
                            v.wmo.c_str(), v.group, v.inside ? "inside" : "outside", v.cam[0], v.cam[1], v.cam[2], v.forward[0], v.forward[1], v.forward[2],
                            wxl_livingazeroth::snapshot::Ordinals(v.open).c_str(), wxl_livingazeroth::snapshot::Ordinals(v.shut).c_str());
                SurveyRow row;
                if (!SurveyBuilding(scan, v.wmo, gl::Windows().All(), row)) std::printf("  %s: not in the scan\n", v.wmo.c_str());
                else PrintSurveyRow(row);
            }
        }
    }
    else std::printf("SKIP the real tables (no data folder given)\n");

    std::printf("%s: %u passed, %u failed\n", g_failed ? "FAILED" : "PASSED", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
