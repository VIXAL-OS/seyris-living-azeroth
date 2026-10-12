// The client's own footprints on top of the surface cover (features/SurfaceCover).
//
// How the 12340 client draws them (static disassembly, 2026-10-11):
//  - The world render (0x4F8EA0) draws the scene at 0x4F909F (0x79A870; its terrain stage 0x798DA0 is
//    where the cover draws, with depth writes), then the footprints at 0x4F90A4 (0x77F070 -> 0x79FCC0).
//  - 0x79FCC0 runs only with showfootprints on (0x400 in 0xCD774C). It fades every print out over 6 s,
//    then draws them alpha-blended (blend mode 2), without depth writes but depth-tested against the
//    scene, with a polygon offset of footstepBias (0.125) x 1/256, which the D3D9 backend sets as
//    D3DRS_DEPTHBIAS -0.00049 (0x6A4C30). The prints themselves are copies of the terrain triangles under
//    the foot with the print's texture projected onto them (0x79FA70), at the terrain's own heights.
//  - So a print lies on the terrain, and that small bias pulls it over the terrain only: under a cover
//    a few centimetres above the terrain (sand, ash, snow) it failed the depth test, except in a trench
//    still open to the ground.
//
// The fix: during the client's own print draw, every print lying on covered terrain is raised by how far
// the cover drawn this frame rises above the terrain at the print's centre (the trench floor when it lies
// in one, which it does, being where the foot was), and put back right after. The client draws it as
// before -- its texture, fade, blending and bias -- on the cover's surface, and units and objects in
// front still hide it. Prints on WMO floors (their vertices aren't the terrain's) and where nothing is
// drawn above the terrain are left alone.
//
// Print pool (0x79FCC0 and 0x79FA70): 0x240 entries of 0x34 bytes at 0xCF4960 (the active player's in
// the first 0x40): +0x00 the print's position (x, y, z), +0x10 its footprint texture (FootprintTextures.dbc's
// ID: the print draw groups the prints by it, into a table the client fills by that ID at 0x7A03C0), +0x20
// its vertex count, +0x24 its vertices, 0x1C bytes each: position, then the texture's coordinates (+0x0C,
// texture coordinate 0 of the drawn vertex: the texture projected from above, turned the way the unit faces,
// stretched to the print's length and width and mirrored for the other foot, at 0x79F760 and 0x7E2D60), then a
// second pair (+0x14, the projection's height). A print that runs out keeps its vertex memory: 0x79F980 only lowers
// the count. Every address and layout this relies on is checked byte for byte against the 12340 client
// before the hook goes in (kGuards; tests/cover checks them against Wow.exe itself).
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

struct WXL_Api;

namespace wxl_livingazeroth::coverprints
{
    /// Checks the client (12340, every guard below), then hooks its print draw. Off, with the reason in
    /// Status(), when anything differs.
    void Install(const WXL_Api* api);
    const char* Status();
    /// The last print draw: live prints, and those lifted onto the cover.
    unsigned LastLive();
    unsigned LastOnCover();

    struct NewPrint;
    /// The prints the client put down since the last call, found at its print draws (at most 64 kept between
    /// calls; features/SurfaceCover rings the sand around them). Appended to `out`.
    void TakeNewPrints(std::vector<NewPrint>& out);
    /// Prints found so far this session.
    unsigned PrintsFound();

    static_assert(sizeof(void*) == 4, "the client's print pool holds 32-bit pointers");

    constexpr uintptr_t kClientImageBase = 0x00400000;
    constexpr uint32_t  kClientTimestamp = 0x4C2452FE; // 3.3.5a build 12340

    constexpr uintptr_t kPrintDraw = 0x0079FCC0;  // __cdecl, no arguments
    constexpr uintptr_t kPool = 0x00CF4960;
    constexpr uint32_t  kPoolEntries = 0x240;
    constexpr uint32_t  kEntrySize = 0x34;
    constexpr uint32_t  kEntryPosition = 0x00;
    constexpr uint32_t  kEntryVertexCount = 0x20;
    constexpr uint32_t  kEntryVertices = 0x24;
    constexpr uint32_t  kEntryTexture = 0x10;
    constexpr uint32_t  kVertexSize = 0x1C;
    constexpr uint32_t  kVertexZ = 0x08;
    constexpr uint32_t  kVertexUV = 0x0C;
    constexpr uint32_t  kMaxVertices = 3 * 512; // sanity: a print is a few terrain triangles

    // A print counts as lying on the terrain when its first vertices are within this of the terrain's
    // height there (they are the terrain's own vertices, so they match to float precision).
    constexpr float kOnTerrain = 0.05f;
    // Raised this much above the cover's surface besides: the cover is drawn on a 0.25 yd grid, the
    // print on the terrain's triangles, and the client's depth bias is small up close.
    constexpr float kAboveSurface = 0.01f;

    struct Guard
    {
        const char* what;
        uintptr_t   address;
        uint32_t    size;
        uint8_t     bytes[24];
    };

    const Guard kGuards[] = {
        {"print draw: entry, and its showfootprints test", 0x0079FCC0, 16,
         {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x78, 0xF7, 0x05, 0x4C, 0x77, 0xCD, 0x00, 0x00, 0x04, 0x00, 0x00}},
        {"print draw: pool walk (base, entry size, vertex count)", 0x0079FCF0, 15,
         {0x8B, 0xF7, 0x6B, 0xF6, 0x34, 0x81, 0xC6, 0x60, 0x49, 0xCF, 0x00, 0x83, 0x7E, 0x20, 0x00}},
        {"print draw: pool size", 0x0079FD98, 9,
         {0x83, 0xC7, 0x01, 0x81, 0xFF, 0x40, 0x02, 0x00, 0x00}},
        {"print draw: an entry's vertex count and vertices", 0x0079FFC6, 22,
         {0x8B, 0x53, 0x0C, 0x8B, 0x3C, 0x8A, 0x6B, 0xFF, 0x34, 0x81, 0xC7, 0x60, 0x49, 0xCF, 0x00, 0x83, 0x7F, 0x20,
          0x00, 0x8B, 0x57, 0x24}},
        {"print draw: vertex position", 0x0079FFF5, 16,
         {0x8D, 0x4A, 0x14, 0x8B, 0x1A, 0x89, 0x1E, 0x8B, 0x5A, 0x04, 0x89, 0x5E, 0x04, 0x8B, 0x5A, 0x08}},
        {"print draw: vertex size", 0x007A0055, 9,
         {0x8B, 0x5D, 0xF4, 0x83, 0xC2, 0x1C, 0x83, 0xC1, 0x1C}},
        {"print add: position stored at the entry's start", 0x0079FB60, 24,
         {0x8D, 0x4B, 0x2C, 0xE8, 0xB8, 0xFC, 0xFF, 0xFF, 0x8B, 0x07, 0x89, 0x03, 0x8B, 0x4F, 0x04, 0x8B, 0x45, 0x08,
          0x89, 0x4B, 0x04, 0x8B, 0x57, 0x08}},
        {"print vertex array: lowering the count keeps the memory", 0x0079F98A, 7,
         {0x3B, 0x77, 0x04, 0x76, 0x70, 0x3B, 0x37}},
        {"print add: the footprint texture stored at the entry's +0x10", 0x0079FB75, 24,
         {0x8B, 0x57, 0x08, 0x8B, 0x0D, 0xAC, 0x76, 0xCD, 0x00, 0x89, 0x4B, 0x18, 0x8D, 0x8D, 0x38, 0xFF, 0xFF, 0xFF,
          0x89, 0x53, 0x08, 0x89, 0x43, 0x10}},
        {"print add: a vertex's second half from +0x14", 0x0079FC1B, 3, {0x8D, 0x7B, 0x14}},
        {"print add: the texture's coordinates at the vertex's +0x0C", 0x0079FC58, 24,
         {0xE8, 0x93, 0x51, 0xD2, 0xFF, 0x8B, 0x08, 0x89, 0x4F, 0xF8, 0x8B, 0x50, 0x04, 0x8D, 0x85, 0xF8, 0xFE, 0xFF,
          0xFF, 0x50, 0x8D, 0x4D, 0xB8, 0x56}},
        {"print draw: grouped by the entry's texture", 0x0079FD78, 17,
         {0x8B, 0x46, 0x10, 0x8D, 0x14, 0x40, 0xA1, 0x68, 0xBE, 0xCF, 0x00, 0x8D, 0x4D, 0xFC, 0x8D, 0x1C, 0xD0}},
        {"print draw: texture coordinate 0 from the vertex's +0x0C", 0x007A0033, 12,
         {0x8B, 0x59, 0xF8, 0x89, 0x5E, 0x1C, 0x8B, 0x59, 0xFC, 0x89, 0x5E, 0x20}},
        {"print textures: the table filled by FootprintTextures' ID", 0x007A0431, 15,
         {0x8B, 0x07, 0x8B, 0x15, 0x68, 0xBE, 0xCF, 0x00, 0x8D, 0x0C, 0x40, 0x83, 0x3C, 0xCA, 0x00}},
        {"world render: the scene, then the prints", 0x004F909F, 10,
         {0xE8, 0x4C, 0x5F, 0x28, 0x00, 0xE8, 0xC7, 0x5F, 0x28, 0x00}},
        {"world render: the prints' thunk", 0x0077F070, 5,
         {0xE9, 0x4B, 0x0C, 0x02, 0x00}},
        {"scene: the terrain stage (where the cover draws)", 0x0079AC30, 5,
         {0xE8, 0x6B, 0xE1, 0xFF, 0xFF}},
    };

    /// The first guard whose bytes differ (read through `read(address, size)`, which returns a pointer to
    /// that many bytes or null), or null when all match.
    template <class Read> const Guard* FirstMismatch(Read read)
    {
        for (const Guard& g : kGuards)
        {
            const void* at = read(g.address, g.size);
            if (!at || std::memcmp(at, g.bytes, g.size) != 0) return &g;
        }
        return nullptr;
    }

    /// What a lift changed, to put back exactly.
    struct Lifted
    {
        uint32_t entry;
        uint32_t vertices;  // the vertex array it was, to put back only into the same one
        uint32_t count;
        uint32_t firstZ;    // index into LiftRun::z
    };
    struct LiftRun
    {
        std::vector<Lifted> lifted;
        std::vector<float>  z;     // the original heights
        unsigned live = 0, onCover = 0;
    };

    inline float ReadFloat(const uint8_t* p) { float f; std::memcpy(&f, p, 4); return f; }
    inline uint32_t ReadU32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

    /// Raises every live print lying on the terrain by liftAt(x, y, terrainZ) at its centre (yd, 0 = not
    /// covered there) plus kAboveSurface. terrainAt(x, y, z) is the terrain's height (false: none there).
    /// Fills `run` for RestorePrints.
    template <class LiftAt, class TerrainAt>
    void LiftPrints(uint8_t* pool, LiftAt liftAt, TerrainAt terrainAt, LiftRun& run)
    {
        run.lifted.clear();
        run.z.clear();
        run.live = run.onCover = 0;
        for (uint32_t e = 0; e < kPoolEntries; ++e)
        {
            uint8_t* entry = pool + e * kEntrySize;
            const uint32_t count = ReadU32(entry + kEntryVertexCount);
            const uint32_t vertices = ReadU32(entry + kEntryVertices);
            if (!count || count > kMaxVertices || !vertices) continue;
            ++run.live;
            uint8_t* v = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(vertices));
            const float x = ReadFloat(entry + kEntryPosition), y = ReadFloat(entry + kEntryPosition + 4);
            float ground;
            if (!std::isfinite(x) || !std::isfinite(y) || !terrainAt(x, y, ground)) continue;
            // On the terrain: its vertices are the terrain's own (a print on a WMO floor's aren't).
            int checked = 0;
            bool onTerrain = true;
            for (uint32_t k = 0; k < count && k < 3; ++k)
            {
                const uint8_t* p = v + k * kVertexSize;
                float h;
                if (!terrainAt(ReadFloat(p), ReadFloat(p + 4), h)) continue;
                ++checked;
                if (!(std::fabs(ReadFloat(p + kVertexZ) - h) <= kOnTerrain)) { onTerrain = false; break; }
            }
            if (!onTerrain || !checked) continue;
            const float lift = liftAt(x, y, ground);
            if (!(lift > 0.0f) || !std::isfinite(lift)) continue;
            run.lifted.push_back({ e, vertices, count, static_cast<uint32_t>(run.z.size()) });
            for (uint32_t k = 0; k < count; ++k)
            {
                uint8_t* z = v + k * kVertexSize + kVertexZ;
                const float before = ReadFloat(z);
                run.z.push_back(before);
                const float after = before + lift + kAboveSurface;
                std::memcpy(z, &after, 4);
            }
            ++run.onCover;
        }
    }

    /// A print the client has just put down: its pool entry, the position stored at the entry's start, its footprint
    /// texture (FootprintTextures.dbc ID), and, when its vertices give it (`mapped`), the texture's coordinates over
    /// the ground about that position (FitPrintMapping).
    struct NewPrint
    {
        uint32_t entry;
        float    x, y, z;
        uint32_t texture = 0;
        bool     mapped = false;
        float    map[6] = {};
    };

    /// How far a vertex's texture coordinates may stray from the fitted mapping (the client's projection is exact up to
    /// float rounding).
    constexpr float kMapFitError = 0.02f;

    /// The print's texture coordinates over the ground, from its vertices: least squares u = map[0] dx + map[1] dy +
    /// map[2], v = map[3] dx + map[4] dy + map[5] with (dx, dy) = (x - px, y - py). False when the vertices don't pin it
    /// down (fewer than three, or in a line) or don't follow one such mapping (by more than kMapFitError).
    inline bool FitPrintMapping(const uint8_t* vertices, uint32_t count, float px, float py, float map[6])
    {
        if (!vertices || count < 3 || count > kMaxVertices) return false;
        double m[3][3] = {}, ru[3] = {}, rv[3] = {};
        for (uint32_t k = 0; k < count; ++k)
        {
            const uint8_t* p = vertices + k * kVertexSize;
            const double b[3] = { static_cast<double>(ReadFloat(p)) - px, static_cast<double>(ReadFloat(p + 4)) - py, 1.0 };
            const double u = ReadFloat(p + kVertexUV), v = ReadFloat(p + kVertexUV + 4);
            if (!std::isfinite(b[0]) || !std::isfinite(b[1]) || !std::isfinite(u) || !std::isfinite(v)) return false;
            for (int i = 0; i < 3; ++i)
            {
                for (int j = 0; j < 3; ++j) m[i][j] += b[i] * b[j];
                ru[i] += b[i] * u;
                rv[i] += b[i] * v;
            }
        }
        // Cramer's rule on the normal equations.
        const auto det = [](const double a[3][3])
        {
            return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
                   a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
        };
        const double d = det(m);
        if (!(std::fabs(d) > 1e-6 * m[0][0] * m[1][1] * m[2][2]) || !(m[0][0] > 0.0) || !(m[1][1] > 0.0)) return false;
        double sol[6];
        for (int c = 0; c < 3; ++c)
        {
            double mu[3][3], mv[3][3];
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) { mu[i][j] = j == c ? ru[i] : m[i][j]; mv[i][j] = j == c ? rv[i] : m[i][j]; }
            sol[c] = det(mu) / d;
            sol[3 + c] = det(mv) / d;
        }
        for (uint32_t k = 0; k < count; ++k)
        {
            const uint8_t* p = vertices + k * kVertexSize;
            const double dx = static_cast<double>(ReadFloat(p)) - px, dy = static_cast<double>(ReadFloat(p + 4)) - py;
            if (std::fabs(sol[0] * dx + sol[1] * dy + sol[2] - ReadFloat(p + kVertexUV)) > kMapFitError ||
                std::fabs(sol[3] * dx + sol[4] * dy + sol[5] - ReadFloat(p + kVertexUV + 4)) > kMapFitError)
                return false;
        }
        for (int i = 0; i < 6; ++i) map[i] = static_cast<float>(sol[i]);
        return true;
    }
    /// What the last walk saw in a pool entry.
    struct PrintSeen
    {
        float x = 0.0f, y = 0.0f;
        bool  live = false;
    };
    /// Calls onNew(NewPrint) for every print put down since the last walk: an entry that is live now and was not,
    /// or that holds a print at another place (the client reuses an entry for a later print). `seen` is sized to
    /// kPoolEntries here; the first walk only notes what is there, since those prints are already old.
    template <class OnNew>
    void FindNewPrints(const uint8_t* pool, std::vector<PrintSeen>& seen, OnNew onNew)
    {
        const bool first = seen.size() != kPoolEntries;
        if (first) seen.assign(kPoolEntries, PrintSeen{});
        for (uint32_t e = 0; e < kPoolEntries; ++e)
        {
            const uint8_t* entry = pool + e * kEntrySize;
            const uint32_t count = ReadU32(entry + kEntryVertexCount);
            PrintSeen& s = seen[e];
            if (!count || count > kMaxVertices || !ReadU32(entry + kEntryVertices)) { s.live = false; continue; }
            const float x = ReadFloat(entry + kEntryPosition), y = ReadFloat(entry + kEntryPosition + 4);
            const float z = ReadFloat(entry + kEntryPosition + 8);
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) { s.live = false; continue; }
            const bool fresh = !s.live || std::fabs(x - s.x) > 0.01f || std::fabs(y - s.y) > 0.01f;
            s.x = x; s.y = y; s.live = true;
            if (fresh && !first)
            {
                NewPrint p{ e, x, y, z };
                p.texture = ReadU32(entry + kEntryTexture);
                p.mapped = FitPrintMapping(reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(ReadU32(entry + kEntryVertices))),
                                           count, x, y, p.map);
                onNew(p);
            }
        }
    }

    /// Puts back the heights LiftPrints changed, into the same vertex arrays only.
    inline void RestorePrints(uint8_t* pool, const LiftRun& run)
    {
        for (const Lifted& l : run.lifted)
        {
            const uint8_t* entry = pool + l.entry * kEntrySize;
            if (ReadU32(entry + kEntryVertices) != l.vertices) continue;
            uint8_t* v = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(l.vertices));
            // All of them, or as many as were saved before a lift was cut short.
            const size_t saved = run.z.size() > l.firstZ ? run.z.size() - l.firstZ : 0;
            const uint32_t n = l.count < saved ? l.count : static_cast<uint32_t>(saved);
            for (uint32_t k = 0; k < n; ++k) std::memcpy(v + k * kVertexSize + kVertexZ, &run.z[l.firstZ + k], 4);
        }
    }
}
