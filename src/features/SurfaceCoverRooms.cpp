// No surface cover inside buildings (features/CoverRooms.hpp has the rule and why): the buildings the scene draws, read
// from the client, and the cover's cells re-sampled where one comes, moves or goes.
//
// The scene's group draw (features/WmoIce's hook) notes every placed instance it draws; the next update reads the new
// ones -- the root's MOGI and the instance's placement -- and queues the rows under their rooms on every level. Those rows
// are worked within a budget of their own, re-sampling only the cells whose answer changed. The fill's samples
// (SurfaceCoverFill.cpp) ask InRoom for every cell, against the buildings the row can meet. A room that keeps cover out
// under its box alone gets its roof map from its group's triangles (a room that never took a cell needs none), and its
// rows are worked again. Buildings stay known until the player is far away (one out of sight keeps its floor clear when
// it comes back into view); one whose placement keeps changing is a transport (a ship, a zeppelin) and is left out.
#include "SurfaceCoverInternal.hpp"
#include "CoverRooms.hpp"
#include "GrassPerf.hpp"

#include <cstdio>
#include <cstring>

namespace wxl_livingazeroth::cover::detail
{
    namespace
    {
        struct Noted { const void* instanceArg; const void* root; };
        constexpr size_t   kMaxNoted = 256;       // buildings noted in a frame (more are picked up on the next frames)
        constexpr size_t   kMaxBuildings = 384;   // known at once (the farthest go first)
        constexpr float    kKeepYards = 800.0f;   // forgotten beyond this from the player (the outermost level: 640 yd)
        constexpr size_t   kRootPath = 0x1C;      // the root's WMO path, inline (features/WmoIce)
        constexpr unsigned kMovedFrames = 120;    // placed again within this many frames of the last move: a transport
        constexpr uint16_t kMaxTries = 600;       // frames drawn with its group not loaded before a room keeps its box alone
        constexpr uint32_t kMaxVertices = 65536, kMaxIndices = 3u * 262144u;

        struct Known : rooms::Building
        {
            unsigned moved = 0;                   // the frame its placement last changed (0: never)
            bool     transport = false;           // its placement keeps changing: left out
            std::vector<uint8_t> took;            // per room: its box alone kept cover out of a cell (it needs its roof map)
        };

        std::vector<Noted> g_noted;
        std::vector<Known> g_buildings;
        // The buildings the row being sampled can meet, each with the rooms it can meet (their indices, in g_rowRooms).
        struct RowEntry { Known* building; size_t first, count; };
        std::vector<RowEntry> g_row;
        std::vector<uint16_t> g_rowRooms;
        DWORD g_mainThread = 0;
        int   g_roomsMap = -1;
        int   g_lastKeepOut = -1;
        int   g_guards = 0;                       // 0 unchecked, 1 hold, -1 differ
        const char* g_guardWhy = "";
        struct Stats
        {
            unsigned registered = 0, reread = 0, transports = 0, rootMismatch = 0, unreadable = 0, dropped = 0;
            unsigned roofs = 0, roofsUnavailable = 0;
            unsigned rowsQueued = 0, rowsWorked = 0, cellsChanged = 0;
            double   workMs = 0, roofMs = 0;
        } g_stats;

        bool ReadBytes(const void* src, void* dst, size_t n)
        {
            __try { std::memcpy(dst, src, n); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool CheckGuards()
        {
            for (const coverprints::Guard& g : rooms::kGuards)
            {
                uint8_t bytes[24];
                if (!ReadBytes(reinterpret_cast<const void*>(g.address), bytes, g.size) || std::memcmp(bytes, g.bytes, g.size) != 0)
                {
                    g_guardWhy = g.what;
                    return false;
                }
            }
            return true;
        }

        float Margin(const Level& L) { return L.cell > rooms::kMinMargin ? L.cell : rooms::kMinMargin; }

        // The filled area of a level's grid (world cells, inclusive), and with a job running, the job's too.
        void Filled(const Level& L, bool withJob, int& i0, int& i1, int& j0, int& j1)
        {
            i0 = L.gridI - kApron; j0 = L.gridJ - kApron;
            i1 = i0 + kFilled - 1; j1 = j0 + kFilled - 1;
            if (!withJob || !L.jobActive) return;
            const int ji0 = L.jobI - kApron, jj0 = L.jobJ - kApron;
            if (!L.haveGrid) { i0 = ji0; j0 = jj0; i1 = ji0 + kFilled - 1; j1 = jj0 + kFilled - 1; return; }
            i0 = ji0 < i0 ? ji0 : i0; j0 = jj0 < j0 ? jj0 : j0;
            i1 = ji0 + kFilled - 1 > i1 ? ji0 + kFilled - 1 : i1;
            j1 = jj0 + kFilled - 1 > j1 ? jj0 + kFilled - 1 : j1;
        }

        // The rows under a world box (grown by the margin and a cell for the last triangle) on every level, where its grid
        // (or the job it is filling) holds them; cells outside get sampled when they come in.
        void Queue(const float lo[3], const float hi[3])
        {
            for (int k = 0; k < kMaxLevels; ++k)
            {
                Level& L = g_levels[k];
                if (!L.haveGrid && !L.jobActive) continue;
                // A level without cover or ground snow could settle on has nothing to keep out (its next fill asks anyway).
                if (L.coverCells <= 0 && L.snowCells <= 0 && L.roomCells <= 0 && !L.jobActive) continue;
                const float m = Margin(L) * 1.5f + L.cell;
                int i0 = static_cast<int>(std::floor((lo[0] - m) / L.cell)), i1 = static_cast<int>(std::ceil((hi[0] + m) / L.cell));
                int j0 = static_cast<int>(std::floor((lo[1] - m) / L.cell)), j1 = static_cast<int>(std::ceil((hi[1] + m) / L.cell));
                int fi0, fi1, fj0, fj1;
                Filled(L, true, fi0, fi1, fj0, fj1);
                i0 = i0 > fi0 ? i0 : fi0; i1 = i1 < fi1 ? i1 : fi1;
                j0 = j0 > fj0 ? j0 : fj0; j1 = j1 < fj1 ? j1 : fj1;
                if (i0 > i1 || j0 > j1) continue;
                for (int j = j0; j <= j1; ++j) L.redo.push_back({ i0, i1, j });
                g_stats.rowsQueued += static_cast<unsigned>(j1 - j0 + 1);
            }
        }

        // One room's box in the world.
        void QueueRoom(const Known& b, const rooms::Box& room)
        {
            rooms::Building one;
            std::memcpy(one.axes, b.axes, sizeof(one.axes));
            std::memcpy(one.origin, b.origin, sizeof(one.origin));
            one.rooms.push_back(rooms::Box{});
            std::memcpy(one.rooms[0].lo, room.lo, sizeof(room.lo));
            std::memcpy(one.rooms[0].hi, room.hi, sizeof(room.hi));
            if (rooms::Bounds(one)) Queue(one.lo, one.hi);
        }

        float DistanceXY(const rooms::Building& b, const float p[3])
        {
            const float dx = p[0] < b.lo[0] ? b.lo[0] - p[0] : (p[0] > b.hi[0] ? p[0] - b.hi[0] : 0.0f);
            const float dy = p[1] < b.lo[1] ? b.lo[1] - p[1] : (p[1] > b.hi[1] ? p[1] - b.hi[1] : 0.0f);
            return std::sqrt(dx * dx + dy * dy);
        }

        // The instance's root and placement as the client holds them now. False: unreadable, or not a placement.
        bool ReadPlacement(const void* instanceArg, const void*& root, float axes[9], float origin[3])
        {
            const auto* instance = static_cast<const uint8_t*>(instanceArg) - rooms::kInstanceArg;
            float m[16];
            if (!ReadBytes(instance + rooms::kInstanceRoot, &root, sizeof(root)) || !ReadBytes(instance + rooms::kInstanceMatrix, m, sizeof(m)))
                return false;
            for (int r = 0; r < 3; ++r)
                for (int k = 0; k < 3; ++k) axes[3 * r + k] = m[4 * r + k];
            for (int k = 0; k < 3; ++k) origin[k] = m[12 + k];
            for (int k = 0; k < 3; ++k)
                if (!std::isfinite(origin[k]) || std::fabs(origin[k]) > 40000.0f) return false;
            return rooms::IsRotation(axes);
        }

        // The root's rooms (MOGI) and its file name.
        bool ReadRooms(const void* root, rooms::Building& b)
        {
            const auto* r = static_cast<const uint8_t*>(root);
            const void* infos = nullptr;
            uint32_t count = 0;
            if (!ReadBytes(r + rooms::kRootGroupInfo, &infos, sizeof(infos)) || !ReadBytes(r + rooms::kRootGroupCount, &count, sizeof(count)) ||
                !infos || !count || count > rooms::kMaxGroups)
                return false;
            std::vector<uint8_t> raw(static_cast<size_t>(count) * rooms::kGroupInfoStride);
            if (!ReadBytes(infos, raw.data(), raw.size())) return false;
            std::vector<uint32_t> flags(count);
            std::vector<float> boxes(static_cast<size_t>(count) * 6);
            for (uint32_t g = 0; g < count; ++g)
            {
                const uint8_t* at = raw.data() + static_cast<size_t>(g) * rooms::kGroupInfoStride;
                std::memcpy(&flags[g], at, 4);
                std::memcpy(&boxes[6 * static_cast<size_t>(g)], at + 4, 24);
            }
            b.rooms = rooms::RoomsOf(flags.data(), boxes.data(), count);
            char path[260] = {};
            if (!ReadBytes(r + kRootPath, path, sizeof(path) - 1)) path[0] = 0;
            path[sizeof(path) - 1] = 0;
            const char* stem = path;
            for (const char* c = path; *c; ++c)
                if (*c == '\\' || *c == '/') stem = c + 1;
            std::snprintf(b.name, sizeof(b.name), "%s", stem);
            if (char* dot = std::strrchr(b.name, '.')) *dot = 0;
            return true;
        }

        // A room's group's triangles, copied out whole. False: not loaded yet (notLoaded), or unreadable.
        bool ReadGroupMesh(const void* root, uint16_t group, std::vector<float>& vertices, std::vector<uint16_t>& indices, bool& notLoaded)
        {
            notLoaded = false;
            const auto* r = static_cast<const uint8_t*>(root);
            uint32_t loaded = 0, flags = 0, vertexCount = 0, indexCount = 0;
            const uint8_t* g = nullptr;
            const void* v = nullptr;
            const void* ix = nullptr;
            if (!ReadBytes(r + rooms::kRootGroupsLoaded, &loaded, sizeof(loaded))) return false;
            if (!loaded) { notLoaded = true; return false; }
            if (!ReadBytes(r + rooms::kRootGroups + sizeof(void*) * group, &g, sizeof(g))) return false;
            if (!g || !ReadBytes(g + rooms::kGroupLoaded, &flags, sizeof(flags)) || !(flags & 1)) { notLoaded = true; return false; }
            if (!ReadBytes(g + rooms::kGroupVertices, &v, sizeof(v)) || !ReadBytes(g + rooms::kGroupVertexCount, &vertexCount, sizeof(vertexCount)) ||
                !ReadBytes(g + rooms::kGroupIndices, &ix, sizeof(ix)) || !ReadBytes(g + rooms::kGroupIndexCount, &indexCount, sizeof(indexCount)))
                return false;
            if (!v || !ix || !vertexCount || indexCount < 3 || vertexCount > kMaxVertices || indexCount > kMaxIndices) return false;
            vertices.resize(3 * static_cast<size_t>(vertexCount));
            indices.resize(indexCount);
            return ReadBytes(v, vertices.data(), vertices.size() * sizeof(float)) && ReadBytes(ix, indices.data(), indices.size() * sizeof(uint16_t));
        }

        // Roof maps for the rooms whose box alone took cells (one room at least a frame, more within the budget), from
        // buildings drawn a moment ago (their roots are alive); then their rows again.
        void BuildRoofs(double deadline)
        {
            const double t0 = grassperf::Now();
            std::vector<float> vertices;
            std::vector<uint16_t> indices;
            bool first = true;
            for (Known& b : g_buildings)
            {
                if (b.transport || g_frame - b.lastSeen > 2) continue;
                for (size_t n = 0; n < b.rooms.size(); ++n)
                {
                    rooms::Box& room = b.rooms[n];
                    if (room.roofState != rooms::RoofState::Pending || n >= b.took.size() || !b.took[n]) continue;
                    if (!first && grassperf::Now() >= deadline) { g_stats.roofMs = grassperf::Now() - t0; return; }
                    first = false;
                    bool notLoaded = false;
                    if (ReadGroupMesh(b.root, room.group, vertices, indices, notLoaded))
                    {
                        room.roof = rooms::BuildRoof(room, vertices.data(), static_cast<uint32_t>(vertices.size() / 3), indices.data(),
                                                     static_cast<uint32_t>(indices.size()));
                        room.roofState = rooms::RoofState::Built;
                        ++g_stats.roofs;
                        QueueRoom(b, room); // the band outside its walls gets its cover back
                    }
                    else if (!notLoaded || ++room.tries >= kMaxTries)
                    {
                        room.roofState = rooms::RoofState::Unavailable;
                        ++g_stats.roofsUnavailable;
                    }
                }
            }
            g_stats.roofMs = grassperf::Now() - t0;
        }

        bool SamePlacement(const rooms::Building& b, const float axes[9], const float origin[3])
        {
            for (int k = 0; k < 9; ++k)
                if (std::fabs(b.axes[k] - axes[k]) > 1e-4f) return false;
            for (int k = 0; k < 3; ++k)
                if (std::fabs(b.origin[k] - origin[k]) > 1e-3f) return false;
            return true;
        }

        void Register(const Noted& n, const float player[3])
        {
            const void* root = nullptr;
            float axes[9], origin[3];
            if (!ReadPlacement(n.instanceArg, root, axes, origin)) { ++g_stats.unreadable; return; }
            if (root != n.root) { ++g_stats.rootMismatch; return; } // freed since it was drawn, or not the layout we know
            const uintptr_t key = reinterpret_cast<uintptr_t>(n.instanceArg);
            Known* known = nullptr;
            for (Known& b : g_buildings)
                if (b.key == key) { known = &b; break; }
            if (known && known->root == root)
            {
                known->lastSeen = g_frame;
                if (known->transport || SamePlacement(*known, axes, origin)) return;
                // Moved: a transport if it moved a moment ago too; its rooms go, and it is left out from now on.
                if (known->moved && g_frame - known->moved < kMovedFrames)
                {
                    if (!known->rooms.empty()) Queue(known->lo, known->hi);
                    known->rooms.clear();
                    known->took.clear();
                    known->transport = true;
                    ++g_stats.transports;
                    return;
                }
            }
            Known b;
            b.key = key;
            b.root = root;
            std::memcpy(b.axes, axes, sizeof(b.axes));
            std::memcpy(b.origin, origin, sizeof(b.origin));
            b.lastSeen = g_frame;
            if (!ReadRooms(root, b)) { ++g_stats.unreadable; return; }
            b.took.assign(b.rooms.size(), 0);
            if (!rooms::Bounds(b))
                for (int k = 0; k < 3; ++k) b.lo[k] = b.hi[k] = origin[k];
            if (known)
            {
                // The same instance placed anew (moved once), or another WMO in its memory: its old rooms go.
                if (!known->rooms.empty()) Queue(known->lo, known->hi);
                b.moved = g_frame ? g_frame : 1;
                *known = std::move(b);
                ++g_stats.reread;
                if (!known->rooms.empty()) Queue(known->lo, known->hi);
                return;
            }
            if (g_buildings.size() >= kMaxBuildings)
            {
                // Full: the farthest goes (its rows are out of reach, or soon will be).
                size_t farthest = 0;
                float farthestD = -1.0f;
                for (size_t k = 0; k < g_buildings.size(); ++k)
                {
                    const float d = DistanceXY(g_buildings[k], player);
                    if (d > farthestD) { farthestD = d; farthest = k; }
                }
                if (!g_buildings[farthest].rooms.empty()) Queue(g_buildings[farthest].lo, g_buildings[farthest].hi);
                g_buildings.erase(g_buildings.begin() + static_cast<std::ptrdiff_t>(farthest));
                ++g_stats.dropped;
            }
            g_buildings.push_back(std::move(b));
            ++g_stats.registered;
            if (!g_buildings.back().rooms.empty()) Queue(g_buildings.back().lo, g_buildings.back().hi);
        }

        // A queued row: only the cells whose answer changed are sampled again (the base stands in for the terrain).
        void RedoRow(Level& L, bool isLevel0, int i0, int i1, int j)
        {
            RowRooms(L, i0, i1, j);
            const float margin = Margin(L);
            for (int i = i0; i <= i1; ++i)
            {
                const int s = SlotOf(i, j);
                const float base = L.base[s];
                if (base <= kHole * 0.5f) continue;
                const bool in = !g_row.empty() && InRoom(i * L.cell, j * L.cell, base, margin);
                if (in == (L.inRoom[s] != 0)) continue;
                SampleRect(L, isLevel0, i, i, j, j, false);
                ++g_stats.cellsChanged;
            }
        }
    }

    void UpdateRooms(const world::Snapshot& snap, bool refill)
    {
        g_mainThread = GetCurrentThreadId();
        if (!g_guards)
        {
            g_guards = CheckGuards() ? 1 : -1;
            if (g_guards < 0 && g_api)
                g_api->Log(WXL_LOG_WARN, kTag, "surface cover: the client differs (%s): the cover is not kept out of buildings", g_guardWhy);
        }
        if (snap.mapId != g_roomsMap)
        {
            g_buildings.clear();
            g_noted.clear();
            g_roomsMap = snap.mapId;
        }
        if (refill)
            for (Level& L : g_levels) { L.redo.clear(); L.redoPos = 0; }
        if (g_lastKeepOut != g_settings.keepOutOfRooms)
        {
            // Switched on or off: every known building's rows again (switched on: and the buildings drawn from now on).
            if (g_lastKeepOut >= 0)
                for (const Known& b : g_buildings)
                    if (!b.rooms.empty()) Queue(b.lo, b.hi);
            g_lastKeepOut = g_settings.keepOutOfRooms;
        }
        for (const Noted& n : g_noted) Register(n, snap.playerPos);
        g_noted.clear();
        if ((g_frame & 31) == 0)
            for (size_t k = g_buildings.size(); k-- > 0;)
                if (DistanceXY(g_buildings[k], snap.playerPos) > kKeepYards)
                {
                    g_buildings.erase(g_buildings.begin() + static_cast<std::ptrdiff_t>(k));
                    ++g_stats.dropped;
                }
        if (g_settings.keepOutOfRooms && g_guards > 0) BuildRoofs(grassperf::Now() + 1.0);
    }

    void ClearRooms()
    {
        g_buildings.clear();
        g_noted.clear();
        g_row.clear();
        g_rowRooms.clear();
        g_roomsMap = -1;
        for (Level& L : g_levels) { L.redo.clear(); L.redoPos = 0; }
    }

    void WorkRooms(double deadline)
    {
        const double t0 = grassperf::Now();
        const int levels = ActiveLevels();
        for (int k = 0; k < levels; ++k)
        {
            Level& L = g_levels[k];
            if (L.redoPos >= L.redo.size()) { L.redo.clear(); L.redoPos = 0; continue; }
            if (L.jobActive || !L.haveGrid) continue; // after the fill: it switches the grid to where it sampled
            int fi0, fi1, fj0, fj1;
            Filled(L, false, fi0, fi1, fj0, fj1);
            while (L.redoPos < L.redo.size())
            {
                const Level::Segment seg = L.redo[L.redoPos++];
                const int i0 = seg.i0 > fi0 ? seg.i0 : fi0, i1 = seg.i1 < fi1 ? seg.i1 : fi1;
                if (seg.j < fj0 || seg.j > fj1 || i0 > i1) continue;
                RedoRow(L, k == 0, i0, i1, seg.j);
                ++g_stats.rowsWorked;
                if (grassperf::Now() >= deadline) break;
            }
            if (L.redoPos >= L.redo.size()) { L.redo.clear(); L.redoPos = 0; }
            if (grassperf::Now() >= deadline) break;
        }
        g_stats.workMs = grassperf::Now() - t0;
    }

    void RowRooms(const Level& L, int i0, int i1, int j)
    {
        g_row.clear();
        g_rowRooms.clear();
        if (!g_settings.keepOutOfRooms || g_guards <= 0 || g_buildings.empty()) return;
        const float margin = Margin(L), m = margin * 1.5f;
        const float y = j * L.cell, x0 = i0 * L.cell, x1 = i1 * L.cell;
        for (Known& b : g_buildings)
        {
            if (b.rooms.empty() || y < b.lo[1] - m || y > b.hi[1] + m || x1 < b.lo[0] - m || x0 > b.hi[0] + m) continue;
            const size_t first = g_rowRooms.size();
            for (size_t n = 0; n < b.rooms.size(); ++n)
                if (rooms::RowMeets(b.rooms[n], y, x0, x1, margin)) g_rowRooms.push_back(static_cast<uint16_t>(n));
            if (g_rowRooms.size() > first) g_row.push_back({ &b, first, g_rowRooms.size() - first });
        }
    }

    bool InRoom(float x, float y, float z, float margin)
    {
        for (const RowEntry& e : g_row)
        {
            Known& b = *e.building;
            const int n = rooms::RoomAt(b, x, y, z, margin, g_rowRooms.data() + e.first, e.count);
            if (n < 0) continue;
            // The box alone took it: the room wants its roof map (BuildRoofs).
            if (b.rooms[static_cast<size_t>(n)].roofState == rooms::RoofState::Pending && static_cast<size_t>(n) < b.took.size()) b.took[static_cast<size_t>(n)] = 1;
            return true;
        }
        return false;
    }

    void RoomLines(std::vector<std::string>& out)
    {
        char line[480];
        unsigned roomCount = 0, transports = 0, built = 0, pending = 0, boxOnly = 0;
        for (const Known& b : g_buildings)
        {
            roomCount += static_cast<unsigned>(b.rooms.size());
            transports += b.transport ? 1u : 0u;
            for (size_t n = 0; n < b.rooms.size(); ++n)
            {
                const rooms::RoofState st = b.rooms[n].roofState;
                built += st == rooms::RoofState::Built ? 1u : 0u;
                boxOnly += st == rooms::RoofState::Unavailable ? 1u : 0u;
                pending += st == rooms::RoofState::Pending && n < b.took.size() && b.took[n] ? 1u : 0u;
            }
        }
        size_t queued = 0;
        for (const Level& L : g_levels) queued += L.redo.size() - L.redoPos;
        std::snprintf(line, sizeof(line), "no cover inside buildings: %s; buildings known %zu (%u rooms: roof maps %u, waiting for theirs %u, box alone %u; "
                      "%u transports left out), seen so far %u, placed anew %u, dropped %u; unreadable %u, root checks failed %u",
                      g_guards < 0 ? "off (the client differs)" : (g_settings.keepOutOfRooms ? "on" : "off"), g_buildings.size(), roomCount, built, pending,
                      boxOnly, transports, g_stats.registered, g_stats.reread, g_stats.dropped, g_stats.unreadable, g_stats.rootMismatch);
        out.emplace_back(line);
        if (g_guards < 0)
        {
            std::snprintf(line, sizeof(line), "  the client differs at: %s", g_guardWhy);
            out.emplace_back(line);
        }
        std::snprintf(line, sizeof(line), "  cells kept clear in rooms: level 0 %d, level 1 %d, level 2 %d, level 3 %d, level 4 %d; rows worked again %u of %u "
                      "queued (%zu waiting), cells changed %u; last frame %.2f ms rows, %.2f ms roof maps",
                      g_levels[0].roomCells, g_levels[1].roomCells, g_levels[2].roomCells, g_levels[3].roomCells, g_levels[4].roomCells,
                      g_stats.rowsWorked, g_stats.rowsQueued, queued, g_stats.cellsChanged, g_stats.workMs, g_stats.roofMs);
        out.emplace_back(line);
        const world::Snapshot& snap = world::Current();
        if (!snap.inWorld) return;
        // Where the player stands, and the nearest buildings.
        float z;
        const bool ground = terrain::HeightAt(snap.playerPos[0], snap.playerPos[1], z);
        const Known* in = nullptr;
        int room = -1;
        if (ground)
            for (const Known& b : g_buildings)
                if ((room = rooms::RoomAt(b, snap.playerPos[0], snap.playerPos[1], z, rooms::kMinMargin)) >= 0) { in = &b; break; }
        if (!ground) std::snprintf(line, sizeof(line), "  here: no terrain under the player");
        else if (in)
        {
            const rooms::Box& r = in->rooms[static_cast<size_t>(room)];
            std::snprintf(line, sizeof(line), "  here: the terrain (z %.2f) lies in room group %u of %s (%s): no cover", z, static_cast<unsigned>(r.group), in->name,
                          r.roofState == rooms::RoofState::Built ? "under its roof map" : "its box alone");
        }
        else std::snprintf(line, sizeof(line), "  here: the terrain (z %.2f) lies in no known building's room", z);
        out.emplace_back(line);
        std::vector<std::pair<float, const Known*>> byDistance;
        for (const Known& b : g_buildings) byDistance.push_back({ DistanceXY(b, snap.playerPos), &b });
        std::sort(byDistance.begin(), byDistance.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (size_t k = 0; k < byDistance.size() && k < 5; ++k)
        {
            const Known& b = *byDistance[k].second;
            unsigned roofs = 0;
            for (const rooms::Box& r : b.rooms) roofs += r.roofState == rooms::RoofState::Built ? 1u : 0u;
            std::snprintf(line, sizeof(line), "  %s at %.1f %.1f %.1f: %zu rooms (%u roof maps)%s, %.0f yd away, last drawn %u frames ago", b.name[0] ? b.name : "?",
                          b.origin[0], b.origin[1], b.origin[2], b.rooms.size(), roofs, b.transport ? " (a transport: left out)" : "", byDistance[k].first,
                          g_frame - b.lastSeen);
            out.emplace_back(line);
        }
    }
}

namespace wxl_livingazeroth::cover
{
    using namespace detail;

    void NoteBuilding(const void* root, const void* instanceArg)
    {
        if (!root || !instanceArg || !g_settings.enabled || !g_settings.keepOutOfRooms || g_guards <= 0 || GetCurrentThreadId() != g_mainThread)
            return;
        if (!g_noted.empty() && g_noted.back().instanceArg == instanceArg) return; // its next group
        for (const Noted& n : g_noted)
            if (n.instanceArg == instanceArg) return;
        if (g_noted.size() < kMaxNoted) g_noted.push_back({ instanceArg, root });
    }
}
