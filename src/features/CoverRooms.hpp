// No surface cover inside buildings (2026-10-23, Everlook: "snow inside buildings ... will only be visible from certain
// angles"). The cover is drawn right after the client's terrain stage, before the WMOs, on top of the terrain; where a
// building's floor lies within the cover's depth over the terrain (Everlook's observatory: 0 .. 0.6 yd over it almost
// everywhere) the cover pokes through the floor. Indoors the client runs the terrain stage only while a way out (an
// exterior portal) is on screen, so the snow showed with the door in view and was gone without it.
//
// The rule: no cover where the terrain lies inside one of a placed WMO's rooms -- the boxes (MOGI) of its interior groups
// (flag 0x2000; not the always-drawn 0x10000 ones, which the walk from outside never enters either), from a yard under
// the box (the floor's foundation) to its top, grown by a margin in x and y (one cell of the level, so the cover's last
// triangle tapers off under the wall) -- and only where some surface of that room lies above the terrain (its roof map:
// the highest of its triangles over each 0.25 yd cell, grown by the margin). The box alone also took the band outside the
// walls where a building's foundation lies under the terrain: half of what it took at a Gadgetzan goblin house was open
// sky. Until a room's triangles are read (its group still loading) the box alone holds, so the floor is clear at once.
// Upper storeys' boxes lie above the terrain and take nothing; the mountain over Ironforge's halls lies above their boxes.
//
// Offline over the map scan (scratchpad cover\room_roof.py, 0.5 yd steps): every point where a room's surface lies
// 0 .. 0.6 yd over the terrain is taken (where the cover poked through: Everlook's observatory 951, its igloos 1012, 1968
// and 84, Tanaris's goblin houses, Kharanos's houses, the snow inn, Northrend's ice troll house, Goldshire's inn), and
// none of the points kept clear lies under the open sky (the box alone: up to 49% of them at Gadgetzan, 16% at the ice
// troll house and Goldshire's inn).
//
// Pure: the cover's fill (SurfaceCoverFill.cpp) asks, SurfaceCoverRooms.cpp reads the buildings from the client.
#pragma once

#include "CoverPrints.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace wxl_livingazeroth::cover::rooms
{
    constexpr uint32_t kInteriorGroup = 0x2000;     // MOGI flags: an interior group (a room)
    constexpr uint32_t kAlwaysDrawnGroup = 0x10000; // drawn whatever the portals say; the walk never enters it
    constexpr float    kBelow = 1.0f;               // yd under a room's box the terrain still counts as inside it
    constexpr float    kMinMargin = 0.25f;          // yd around a room's box (level 0's cell; coarser levels: their cell)

    // --- the client (12340, static disassembly 2026-10-23), checked byte for byte before the buildings are read ----------
    // The scene's WMO pass (0x7965D7..0x7967B9) takes each placed instance's root from instance + 0xF4 and calls the group
    // draw (Wmo.RenderGroup, 0x7ABF50, which features/WmoIce hooks) with that root and instance + 0xB0; the instance's
    // placement matrix sits at +0x70 (row-major, row vectors, its translation at +0xA0). The root's MOGI: at +0x130, 0x20
    // bytes a group (flags, box min, box max), its count at +0x16C; its groups (CMapObjGroup*[512]) at +0x1F8 once +0x1E0
    // is set, a group loaded when bit 0 of its +0x198 is; a group's MOVI at +0xE0 (u16, its count at +0x154) and MOVT at
    // +0xE8 (three floats a vertex, its count at +0x15C), in the WMO's own space. env/SeeThroughModel.hpp guards these
    // reads as well.
    constexpr size_t kInstanceArg = 0xB0, kInstanceMatrix = 0x70, kInstanceRoot = 0xF4;
    constexpr size_t kRootGroupInfo = 0x130, kRootGroupCount = 0x16C, kGroupInfoStride = 0x20;
    constexpr size_t kRootGroupsLoaded = 0x1E0, kRootGroups = 0x1F8, kGroupLoaded = 0x198;
    constexpr size_t kGroupIndices = 0xE0, kGroupIndexCount = 0x154, kGroupVertices = 0xE8, kGroupVertexCount = 0x15C;
    constexpr uint32_t kMaxGroups = 512;
    const coverprints::Guard kGuards[] = {
        {"WMO pass: the instance's root at +0xF4", 0x007965D7, 12,
         {0x8B, 0x40, 0x08, 0x8B, 0x80, 0xF4, 0x00, 0x00, 0x00, 0x89, 0x45, 0xF8}},
        {"WMO pass: the placement matrix at +0x70", 0x00796666, 4, {0x8D, 0x47, 0x70, 0x50}},
        {"WMO pass: the group draw gets the root and instance + 0xB0", 0x00796792, 24,
         {0x8B, 0x4D, 0xF8, 0x8D, 0x53, 0x6C, 0x52, 0x89, 0x35, 0xB8, 0xBE, 0xCF, 0x00, 0x8B, 0x43, 0x50, 0x81, 0xC7, 0xB0, 0x00,
          0x00, 0x00, 0x57, 0x50}},
        {"GetGroupFlags: MOGI at +0x130", 0x007AE7C2, 12, {0x8B, 0x45, 0x08, 0x8B, 0x89, 0x30, 0x01, 0x00, 0x00, 0xC1, 0xE0, 0x05}},
        {"RenderInterior: MOGI's count at +0x16C, 0x20 a group", 0x007AD337, 11, {0x83, 0xC7, 0x20, 0x3B, 0x9E, 0x6C, 0x01, 0x00, 0x00, 0x72, 0x9E}},
        {"GetGroup: the root's groups loaded at +0x1E0", 0x007AEA83, 9, {0x83, 0xB9, 0xE0, 0x01, 0x00, 0x00, 0x00, 0x74, 0x19}},
        {"GetGroup: the group array at +0x1F8, a group's loaded bit at +0x198", 0x007AEA8C, 19,
         {0x8B, 0x45, 0x08, 0x8B, 0x84, 0x81, 0xF8, 0x01, 0x00, 0x00, 0xF6, 0x80, 0x98, 0x01, 0x00, 0x00, 0x01, 0x75, 0x08}},
        {"group walker: MOVI at +0xE0, its index count at +0x154", 0x007D7F76, 17,
         {0x89, 0xB1, 0xE0, 0x00, 0x00, 0x00, 0x8B, 0x50, 0x04, 0xD1, 0xEA, 0x89, 0x91, 0x54, 0x01, 0x00, 0x00}},
        {"group walker: MOVT at +0xE8, its vertex count at +0x15C", 0x007D7F94, 18,
         {0x89, 0xB1, 0xE8, 0x00, 0x00, 0x00, 0xF7, 0x67, 0x04, 0xC1, 0xEA, 0x03, 0x89, 0x91, 0x5C, 0x01, 0x00, 0x00}},
    };

    inline bool IsRoom(uint32_t flags) { return (flags & kInteriorGroup) && !(flags & kAlwaysDrawnGroup); }

    /// A room's surfaces seen from under them: per cell of its box (its own x, y; rows along y), the highest of its group's
    /// triangles over the cell's middle, grown by kMinMargin; kNoRoof where none.
    constexpr float  kRoofCell = 0.25f;           // level 0's cell; a room larger than kMaxRoofCells of them gets coarser ones
    constexpr size_t kMaxRoofCells = size_t(1) << 18;
    constexpr float  kNoRoof = -1e30f;
    struct Roof
    {
        float x0 = 0.0f, y0 = 0.0f, cell = kRoofCell;
        int   w = 0, h = 0;
        std::vector<float> top;
    };
    /// Pending: its triangles not read yet (the box alone holds); Built; Unavailable: never read (the box alone holds).
    enum class RoofState : uint8_t { Pending, Built, Unavailable };

    struct Box
    {
        float lo[3] = {}, hi[3] = {};
        float worldLo[2] = {}, worldHi[2] = {};   // its x, y in the world (Bounds): which rows of samples can meet it
        uint16_t  group = 0;
        RoofState roofState = RoofState::Pending;
        uint16_t  tries = 0;    // reads that found its group not loaded yet
        Roof      roof;
    };

    /// Grows the roof by `cells` every way (the highest of the cells around).
    inline void Grow(Roof& r, int cells)
    {
        if (cells <= 0 || r.top.empty()) return;
        std::vector<float> tmp(r.top.size(), kNoRoof);
        for (int j = 0; j < r.h; ++j)
            for (int i = 0; i < r.w; ++i)
            {
                float m = kNoRoof;
                for (int k = i - cells; k <= i + cells; ++k)
                    if (k >= 0 && k < r.w && r.top[static_cast<size_t>(j) * r.w + k] > m) m = r.top[static_cast<size_t>(j) * r.w + k];
                tmp[static_cast<size_t>(j) * r.w + i] = m;
            }
        for (int j = 0; j < r.h; ++j)
            for (int i = 0; i < r.w; ++i)
            {
                float m = kNoRoof;
                for (int k = j - cells; k <= j + cells; ++k)
                    if (k >= 0 && k < r.h && tmp[static_cast<size_t>(k) * r.w + i] > m) m = tmp[static_cast<size_t>(k) * r.w + i];
                r.top[static_cast<size_t>(j) * r.w + i] = m;
            }
    }

    /// The room's roof map from its group's triangles (indices into three floats a vertex, in the WMO's space): the upright
    /// ones show nothing from below.
    inline Roof BuildRoof(const Box& box, const float* vertices, uint32_t vertexCount, const uint16_t* indices, uint32_t indexCount)
    {
        Roof r;
        const float pad = kMinMargin + kRoofCell;   // the grown cells reach past the box by the margin
        const float sx = box.hi[0] - box.lo[0] + 2.0f * pad, sy = box.hi[1] - box.lo[1] + 2.0f * pad;
        if (sx * sy > kRoofCell * kRoofCell * static_cast<float>(kMaxRoofCells)) r.cell = std::sqrt(sx * sy / static_cast<float>(kMaxRoofCells));
        r.x0 = box.lo[0] - pad;
        r.y0 = box.lo[1] - pad;
        r.w = static_cast<int>(std::ceil(sx / r.cell)) + 1;
        r.h = static_cast<int>(std::ceil(sy / r.cell)) + 1;
        r.top.assign(static_cast<size_t>(r.w) * r.h, kNoRoof);
        for (uint32_t t = 0; t + 2 < indexCount; t += 3)
        {
            if (indices[t] >= vertexCount || indices[t + 1] >= vertexCount || indices[t + 2] >= vertexCount) continue;
            const float* a = vertices + 3 * indices[t];
            const float* b = vertices + 3 * indices[t + 1];
            const float* c = vertices + 3 * indices[t + 2];
            const float d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
            if (!(std::fabs(d) > 1e-6f)) continue; // upright (or broken)
            const float minX = std::fmin(a[0], std::fmin(b[0], c[0])), maxX = std::fmax(a[0], std::fmax(b[0], c[0]));
            const float minY = std::fmin(a[1], std::fmin(b[1], c[1])), maxY = std::fmax(a[1], std::fmax(b[1], c[1]));
            const int i0 = (std::max)(0, static_cast<int>(std::ceil((minX - r.x0) / r.cell - 0.5f)));
            const int i1 = (std::min)(r.w - 1, static_cast<int>(std::floor((maxX - r.x0) / r.cell - 0.5f)));
            const int j0 = (std::max)(0, static_cast<int>(std::ceil((minY - r.y0) / r.cell - 0.5f)));
            const int j1 = (std::min)(r.h - 1, static_cast<int>(std::floor((maxY - r.y0) / r.cell - 0.5f)));
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i)
                {
                    const float px = r.x0 + (i + 0.5f) * r.cell, py = r.y0 + (j + 0.5f) * r.cell;
                    const float l1 = ((b[1] - c[1]) * (px - c[0]) + (c[0] - b[0]) * (py - c[1])) / d;
                    const float l2 = ((c[1] - a[1]) * (px - c[0]) + (a[0] - c[0]) * (py - c[1])) / d;
                    const float l3 = 1.0f - l1 - l2;
                    if (l1 < -1e-4f || l2 < -1e-4f || l3 < -1e-4f) continue;
                    const float z = l1 * a[2] + l2 * b[2] + l3 * c[2];
                    float& top = r.top[static_cast<size_t>(j) * r.w + i];
                    if (z > top) top = z;
                }
        }
        Grow(r, static_cast<int>(std::ceil(kMinMargin / r.cell - 1e-4f)));
        return r;
    }

    /// Whether some surface of the room lies above `z` at (x, y) of its space, or within `extra` yd around it (a coarser
    /// level's margin beyond the grown one).
    inline bool RoofAbove(const Roof& r, float x, float y, float z, float extra)
    {
        if (r.top.empty()) return false;
        const int ci = static_cast<int>(std::floor((x - r.x0) / r.cell)), cj = static_cast<int>(std::floor((y - r.y0) / r.cell));
        const int g = extra > 0.0f ? static_cast<int>(std::ceil(extra / r.cell - 1e-4f)) : 0;
        for (int j = (std::max)(0, cj - g); j <= (std::min)(r.h - 1, cj + g); ++j)
            for (int i = (std::max)(0, ci - g); i <= (std::min)(r.w - 1, ci + g); ++i)
                if (r.top[static_cast<size_t>(j) * r.w + i] > z) return true;
        return false;
    }

    /// A placed WMO: its placement (rows of a rigid matrix, row vectors: world = local x axes + origin) and its rooms in
    /// its own space; `lo`/`hi` bound the rooms in the world (Bounds).
    struct Building
    {
        uintptr_t key = 0;          // the placed instance (as the client's group draw hands it over)
        const void* root = nullptr; // its WMO (the root)
        float axes[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        float origin[3] = {};
        std::vector<Box> rooms;
        float lo[3] = {}, hi[3] = {};
        unsigned lastSeen = 0;      // the cover's frame it was last drawn in
        char name[48] = "";         // its WMO's file name (status lines)
    };

    /// A world point in the building's space (the axes are orthonormal: their transpose undoes them).
    inline void ToLocal(const Building& b, float x, float y, float z, float out[3])
    {
        const float d[3] = { x - b.origin[0], y - b.origin[1], z - b.origin[2] };
        for (int r = 0; r < 3; ++r) out[r] = d[0] * b.axes[3 * r] + d[1] * b.axes[3 * r + 1] + d[2] * b.axes[3 * r + 2];
    }

    inline void ToWorld(const Building& b, const float p[3], float out[3])
    {
        for (int k = 0; k < 3; ++k) out[k] = b.origin[k] + p[0] * b.axes[k] + p[1] * b.axes[3 + k] + p[2] * b.axes[6 + k];
    }

    /// The rooms' world bounds (every box's eight corners, with kBelow under them). False: no rooms.
    inline bool Bounds(Building& b)
    {
        if (b.rooms.empty()) return false;
        for (int k = 0; k < 3; ++k) { b.lo[k] = 1e30f; b.hi[k] = -1e30f; }
        for (Box& r : b.rooms)
        {
            r.worldLo[0] = r.worldLo[1] = 1e30f;
            r.worldHi[0] = r.worldHi[1] = -1e30f;
            for (int c = 0; c < 8; ++c)
            {
                const float p[3] = { (c & 1) ? r.hi[0] : r.lo[0], (c & 2) ? r.hi[1] : r.lo[1], (c & 4) ? r.hi[2] : r.lo[2] - kBelow };
                float w[3];
                ToWorld(b, p, w);
                for (int k = 0; k < 3; ++k) { b.lo[k] = w[k] < b.lo[k] ? w[k] : b.lo[k]; b.hi[k] = w[k] > b.hi[k] ? w[k] : b.hi[k]; }
                for (int k = 0; k < 2; ++k) { r.worldLo[k] = (std::min)(r.worldLo[k], w[k]); r.worldHi[k] = (std::max)(r.worldHi[k], w[k]); }
            }
        }
        return true;
    }

    /// Whether the room takes the point `p` of the building's space (see RoomAt).
    inline bool Takes(const Box& r, const float p[3], float margin)
    {
        return p[0] >= r.lo[0] - margin && p[0] <= r.hi[0] + margin && p[1] >= r.lo[1] - margin && p[1] <= r.hi[1] + margin &&
               p[2] >= r.lo[2] - kBelow && p[2] <= r.hi[2] &&
               (r.roofState != RoofState::Built || RoofAbove(r.roof, p[0], p[1], p[2], margin - kMinMargin));
    }

    /// Whether a row of samples along x (x0 .. x1 at y; margin: yd around the box) can meet the room (Bounds first).
    inline bool RowMeets(const Box& r, float y, float x0, float x1, float margin)
    {
        const float wm = margin * 1.5f;
        return y >= r.worldLo[1] - wm && y <= r.worldHi[1] + wm && x1 >= r.worldLo[0] - wm && x0 <= r.worldHi[0] + wm;
    }

    /// The room of `b` the terrain at world (x, y, z) lies in (its index in b.rooms), or -1: in its box (margin: yd around
    /// it in x and y, the box's own space; kBelow under it) and, once its roof map is built, under a surface of it.
    inline int RoomAt(const Building& b, float x, float y, float z, float margin)
    {
        const float wm = margin * 1.5f; // the margin is in the building's space: turned, it reaches up to sqrt(2) further
        if (x < b.lo[0] - wm || x > b.hi[0] + wm || y < b.lo[1] - wm || y > b.hi[1] + wm || z < b.lo[2] || z > b.hi[2])
            return -1;
        float p[3];
        ToLocal(b, x, y, z, p);
        for (size_t n = 0; n < b.rooms.size(); ++n)
            if (Takes(b.rooms[n], p, margin)) return static_cast<int>(n);
        return -1;
    }

    /// The same over some of its rooms only (`rooms`: indices into b.rooms, e.g. the ones a row meets): Stormwind's WMO
    /// alone has 267 rooms.
    inline int RoomAt(const Building& b, float x, float y, float z, float margin, const uint16_t* rooms, size_t count)
    {
        const float wm = margin * 1.5f;
        if (!count || x < b.lo[0] - wm || x > b.hi[0] + wm || y < b.lo[1] - wm || y > b.hi[1] + wm || z < b.lo[2] || z > b.hi[2])
            return -1;
        float p[3];
        ToLocal(b, x, y, z, p);
        for (size_t k = 0; k < count; ++k)
            if (rooms[k] < b.rooms.size() && Takes(b.rooms[rooms[k]], p, margin)) return rooms[k];
        return -1;
    }

    /// Whether the axes are a rotation (unit rows at right angles, right-handed): a placement the rule can undo.
    inline bool IsRotation(const float a[9])
    {
        for (int r = 0; r < 3; ++r)
        {
            if (!std::isfinite(a[3 * r]) || !std::isfinite(a[3 * r + 1]) || !std::isfinite(a[3 * r + 2])) return false;
            const float len = a[3 * r] * a[3 * r] + a[3 * r + 1] * a[3 * r + 1] + a[3 * r + 2] * a[3 * r + 2];
            if (std::fabs(len - 1.0f) > 0.01f) return false;
            for (int s = r + 1; s < 3; ++s)
                if (std::fabs(a[3 * r] * a[3 * s] + a[3 * r + 1] * a[3 * s + 1] + a[3 * r + 2] * a[3 * s + 2]) > 0.01f) return false;
        }
        const float det = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) + a[2] * (a[3] * a[7] - a[4] * a[6]);
        return det > 0.99f && det < 1.01f;
    }

    /// The rooms of a root's MOGI: its interior groups' boxes (finite, not inside out, at most `maxSide` yd across).
    inline std::vector<Box> RoomsOf(const uint32_t* flags, const float* boxes, size_t count, float maxSide = 2000.0f)
    {
        std::vector<Box> out;
        for (size_t g = 0; g < count; ++g)
        {
            if (!IsRoom(flags[g])) continue;
            Box b;
            bool ok = true;
            for (int k = 0; k < 3; ++k)
            {
                b.lo[k] = boxes[6 * g + k];
                b.hi[k] = boxes[6 * g + 3 + k];
                ok = ok && std::isfinite(b.lo[k]) && std::isfinite(b.hi[k]) && b.lo[k] <= b.hi[k] && b.hi[k] - b.lo[k] <= maxSide;
            }
            if (!ok) continue;
            b.group = static_cast<uint16_t>(g);
            out.push_back(b);
        }
        return out;
    }
}
