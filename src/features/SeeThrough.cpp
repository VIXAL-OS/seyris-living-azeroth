#include "SeeThrough.hpp"

#include "../env/SeeThroughModel.hpp"
#include "../render/BlpColour.hpp"
#include "../render/BlpTexture.hpp"
#include "../render/GxState.hpp"
#include "Glass.hpp"
#include "GlassShader.hpp"
#include "SeeThroughDraw.hpp"

#include "game/Gx.hpp"

#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace wxl_livingazeroth::seethrough
{
    namespace
    {
        namespace gx = wxl::game::gx;
        constexpr const char* kTag = "wxl-seyris-living-azeroth";
        constexpr uintptr_t kFrustumIndex = 0x00CD8798;   // CWorldScene::frustumIndex (FrustumPush's and FrustumPop's)
        static_assert(kOpenBits == static_cast<uint32_t>(glass::kReplayBits), "c219.w's windows");

        using WalkFn = void(__fastcall*)(void* root, void* edx, uint32_t group, uint32_t fromGroup, float* rect, uint32_t depth, int interior);
        using DeviceDrawFn = void(__fastcall*)(void* device, void* edx, uint32_t* batch, int indexed);
        using SceneDrawFn = void(__fastcall*)(void* scene, void* edx, int pass);
        using RenderCallbackFn = void(__cdecl*)(uint32_t group, void* param);
        using FrustumPushFn = void(__cdecl*)();
        using FrustumSetFn = void(__cdecl*)(const void* corners, const float* rect);

        WalkFn g_origWalk = nullptr;
        DeviceDrawFn g_origDraw = nullptr;
        SceneDrawFn g_origScene = nullptr;
        const WXL_Api* g_api = nullptr;
        Settings g_settings;
        bool g_hookedWalk = false, g_hookedDraw = false, g_hookedScene = false;
        int g_guards = 0;                 // 0 unchecked, 1 hold, 2 differ (g_guardWhy)
        const char* g_guardWhy = "";
        bool g_faulted = false;           // a fault: off for the session (g_faultWhere)
        const char* g_faultWhere = "";
        unsigned g_faults = 0;
        bool g_inWalk = false;            // our own walk into a room runs (its depth > 0 anyway)

        // Counted over a frame; the last whole frame's for the panel.
        struct Counts
        {
            unsigned groups = 0, opened = 0, shut = 0, groupsAdded = 0, glassBatches = 0, kept = 0, deferred = 0, redrawn = 0;
            unsigned passed = 0, captureFailed = 0, targetMoved = 0, dropped = 0;
            unsigned blind = 0;      // shut: the room not seen through the window from here (SeesRoom)
            unsigned opaque = 0;     // redrawn opaque: the batch's shader without the redraw (or its glass off)
            // Each window on the screen and facing the camera: open, or why it's shut (the panel's list, so an in-game check
            // says what happened: "see-through windows don't seem to work anymore now", the user, 2026-10-21).
            enum Why : uint8_t { kOpen, kFake, kFar, kBudget, kUnloaded, kNoMesh, kBlind, kNotReached };
            struct Note
            {
                uint32_t ordinal = 0;
                Why why = kOpen;
                Sight sight;
            };
            static constexpr unsigned kNotes = 24;
            Note notes[kNotes];
            unsigned noteCount = 0;
            float replayFirst = -1.0f;   // the last deferred batch's open windows: from this ordinal, these bits
            uint32_t replayBits = 0;
        };

        void NoteWindow(Counts& c, uint32_t ordinal, Counts::Why why, const Sight* sight = nullptr)
        {
            if (c.noteCount >= Counts::kNotes) return;
            Counts::Note& n = c.notes[c.noteCount++];
            n.ordinal = ordinal;
            n.why = why;
            n.sight = sight ? *sight : Sight{};
        }
        Counts g_now, g_last;
        unsigned g_everOpened = 0, g_everRedrawn = 0;

        // --- guarded reads ----------------------------------------------------------------------------------------
        bool ReadU32(uintptr_t at, uint32_t& out)
        {
            __try { out = *reinterpret_cast<const uint32_t*>(at); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; return false; }
        }
        bool ReadPtr(uintptr_t at, const void*& out)
        {
            __try { out = *reinterpret_cast<const void* const*>(at); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { out = nullptr; return false; }
        }
        bool ReadFloats(uintptr_t at, int n, float* out)
        {
            __try { std::memcpy(out, reinterpret_cast<const void*>(at), sizeof(float) * static_cast<size_t>(n)); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        uint32_t ReadPathHash(const uint8_t* root)
        {
            char path[260];
            __try
            {
                uint32_t n = 0;
                for (; n < sizeof(path) - 1 && root[client::kRootPath + n]; ++n) path[n] = static_cast<char>(root[client::kRootPath + n]);
                path[n] = 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
            return glass::PathHash(path);
        }
        bool ReadPath(const uint8_t* root, char (&path)[260])
        {
            __try
            {
                uint32_t n = 0;
                for (; n < sizeof(path) - 1 && root[client::kRootPath + n]; ++n) path[n] = static_cast<char>(root[client::kRootPath + n]);
                path[n] = 0;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { path[0] = 0; return false; }
        }
        // The root's materials: which are glass (GlassMaterials.cdbc by their first texture; MOTX bounded by its size).
        bool ReadMaterialCount(const uint8_t* root, const char*& motx, uint32_t& motxSize, const uint8_t*& materials, uint32_t& count)
        {
            __try
            {
                motx = *reinterpret_cast<const char* const*>(root + client::kRootMotx);
                motxSize = *reinterpret_cast<const uint32_t*>(root + client::kRootMotxSize);
                materials = *reinterpret_cast<const uint8_t* const*>(root + client::kRootMaterials);
                count = *reinterpret_cast<const uint32_t*>(root + client::kRootMaterialCount);
                return motx && materials && count > 0 && count <= client::kMaxMaterials;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool ReadTextureName(const char* motx, uint32_t motxSize, const uint8_t* materials, uint32_t i, char (&out)[260])
        {
            __try
            {
                const uint32_t at = *reinterpret_cast<const uint32_t*>(materials + client::kMomtStride * i + client::kMomtTexture);
                if (at >= motxSize) return false;
                const uint32_t limit = std::min<uint32_t>(motxSize - at, sizeof(out) - 1);
                uint32_t n = 0;
                for (; n < limit && motx[at + n]; ++n) out[n] = motx[at + n];
                out[n] = 0;
                return n > 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        std::vector<uint8_t> GlassMaterials(const uint8_t* root)
        {
            const char* motx = nullptr;
            const uint8_t* materials = nullptr;
            uint32_t motxSize = 0, count = 0;
            std::vector<uint8_t> out;
            if (!ReadMaterialCount(root, motx, motxSize, materials, count)) return out;
            out.assign(count, 0);
            char name[260];
            for (uint32_t i = 0; i < count; ++i)
                if (ReadTextureName(motx, motxSize, materials, i, name)) out[i] = glass::MaterialOf(name) ? 1 : 0;
            return out;
        }

        bool ReadGroupInfos(const uint8_t* infos, uint32_t count, GroupInfo* out)
        {
            __try
            {
                for (uint32_t i = 0; i < count; ++i)
                {
                    const uint8_t* g = infos + client::kGroupInfoStride * i;
                    out[i].flags = *reinterpret_cast<const uint32_t*>(g);
                    std::memcpy(out[i].lo, g + 4, 12);
                    std::memcpy(out[i].hi, g + 16, 12);
                }
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        void Fault(const char* where)
        {
            ++g_faults;
            if (g_faulted) return;
            g_faulted = true;
            g_faultWhere = where;
            if (g_api) g_api->Log(WXL_LOG_WARN, kTag, "see-through windows: a fault in %s; off for this session", where);
        }

        bool Active()
        {
            return g_settings.enabled && g_guards == 1 && !g_faulted && g_hookedWalk && g_hookedDraw && g_hookedScene;
        }

        // --- the buildings: per root, its windows and the rooms behind them ----------------------------------------
        struct RootEntry
        {
            const void* groupInfo = nullptr;
            uint32_t groupCount = 0, generation = 0, path = 0;
            bool whitelisted = false;
            std::vector<GroupInfo> groups;
            RootWindows windows;
            std::vector<uint8_t> glassMaterial;   // per material: 1 glass (the room's surfaces leave it out)
        };
        std::unordered_map<const void*, RootEntry> g_roots;
        std::unordered_map<uint32_t, bool> g_logged;   // a whitelisted WMO's windows logged once

        const RootEntry* RootFor(const void* root)
        {
            const auto* r = static_cast<const uint8_t*>(root);
            const void* infos = nullptr;
            uint32_t count = 0;
            if (!r || !ReadPtr(reinterpret_cast<uintptr_t>(r + client::kRootGroupInfo), infos) ||
                !ReadU32(reinterpret_cast<uintptr_t>(r + client::kRootGroupCount), count) || !infos || !count || count > client::kMaxGroups)
                return nullptr;
            RootEntry& e = g_roots[root];   // never cleared during a frame: this frame's walks hold pointers to them (Update)
            if (e.groupInfo == infos && e.groupCount == count && e.generation == glass::Generation()) return &e;
            e = RootEntry{};
            e.groupInfo = infos;
            e.groupCount = count;
            e.generation = glass::Generation();
            e.path = ReadPathHash(r);
            e.whitelisted = Whitelisted(e.path);
            if (!e.whitelisted) return &e;
            e.groups.resize(count);
            if (!ReadGroupInfos(static_cast<const uint8_t*>(infos), count, e.groups.data()))
            {
                e.whitelisted = false;
                return &e;
            }
            e.windows = BuildRoot(e.path, glass::Windows().All(), e.groups.data(), e.groups.size());
            e.glassMaterial = GlassMaterials(r);
            if (g_api && !g_logged[e.path])
            {
                g_logged[e.path] = true;
                char path[260];
                ReadPath(r, path);
                g_api->Log(WXL_LOG_INFO, kTag, "see-through windows: %s: %u windows in its exterior groups, %u with a room behind, %u fake",
                           path, static_cast<unsigned>(e.windows.windows.size()), e.windows.real, e.windows.fake);
            }
            return &e;
        }

        // --- this frame: the exterior groups walked from outside, their windows' states ----------------------------
        struct GroupFrame
        {
            const RootEntry* root = nullptr;
            uint32_t group = 0;
            std::vector<uint8_t> states;   // per window of the group (RootWindows::Group's order)
            unsigned opened = 0;
            float cam[3] = {}, eye[3] = {};   // the client's camera in the WMO's space (the walk's) and the view's eye (world)
            bool haveCam = false;
        };
        std::unordered_map<uint64_t, GroupFrame> g_frame;
        unsigned g_budget = kMaxOpenPerFrame;

        uint64_t Key(const void* instance, uint32_t group)
        {
            return (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(instance)) << 16) ^ group;
        }

        // The room's group can be walked into: the root's groups loaded, the group's object there and loaded (GetGroup's
        // own test, which reads no bounds), and the instance's group object for it (the sort table's callback reads it).
        bool Enterable(const uint8_t* root, const uint8_t* instance, uint32_t index, uint32_t groupCount)
        {
            __try
            {
                if (index >= groupCount || index >= client::kMaxGroups) return false;
                if (!*reinterpret_cast<const uint32_t*>(root + client::kRootGroupsLoaded)) return false;
                const uint8_t* g = reinterpret_cast<const uint8_t* const*>(root + client::kRootGroups)[index];
                if (!g || !(*reinterpret_cast<const uint32_t*>(g + client::kGroupLoadedFlags) & 1)) return false;
                const uint32_t n = *reinterpret_cast<const uint32_t*>(instance + client::kDefGroupsCount);
                const void* const* data = nullptr;
                uint32_t have = 0;
                if (n == 0xFFFFFFFFu)
                {
                    have = *reinterpret_cast<const uint32_t*>(instance + client::kDefGroupsDynCount);
                    data = *reinterpret_cast<const void* const* const*>(instance + client::kDefGroupsDynData);
                }
                else
                {
                    have = n;
                    data = reinterpret_cast<const void* const*>(instance + client::kDefGroups);
                }
                return data && index < have && have <= client::kMaxGroups && data[index] != nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // --- the room's surfaces for the line of sight (RoomMesh), copied out of its group once ----------------------------
        struct GroupArrays
        {
            const void* vertices = nullptr; const void* indices = nullptr; const void* batches = nullptr;
            uint32_t vertexCount = 0, indexCount = 0, batchCount = 0;
        };
        bool ReadGroupArrays(const uint8_t* root, uint32_t index, const uint8_t*& group, GroupArrays& a)
        {
            __try
            {
                group = reinterpret_cast<const uint8_t* const*>(root + client::kRootGroups)[index];
                if (!group) return false;
                a.vertices = *reinterpret_cast<const void* const*>(group + client::kGroupVertices);
                a.vertexCount = *reinterpret_cast<const uint32_t*>(group + client::kGroupVertexCount);
                a.indices = *reinterpret_cast<const void* const*>(group + client::kGroupIndices);
                a.indexCount = *reinterpret_cast<const uint32_t*>(group + client::kGroupIndexCount);
                a.batches = *reinterpret_cast<const void* const*>(group + client::kGroupBatches);
                a.batchCount = *reinterpret_cast<const uint32_t*>(group + client::kGroupBatchCount);
                return a.vertices && a.indices && a.batches;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool CopyOut(const void* from, void* to, size_t bytes)
        {
            __try { std::memcpy(to, from, bytes); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        struct MeshEntry
        {
            GroupArrays arrays;
            uint32_t generation = 0;
            bool ok = false;
            RoomMesh mesh;
        };
        std::unordered_map<const void*, MeshEntry> g_meshes;   // by the room's group object
        unsigned g_meshBuilds = 0;                             // this frame's: one a frame (a big room takes a few ms)

        // The room's surfaces (its rendered triangles but its glass), built the first time a window of it is a candidate
        // and again whenever the group's arrays move; null this frame when the frame's one build went elsewhere.
        const RoomMesh* MeshOf(const RootEntry& r, const void* root, uint32_t room)
        {
            const uint8_t* group = nullptr;
            GroupArrays a;
            if (!ReadGroupArrays(static_cast<const uint8_t*>(root), room, group, a)) return nullptr;
            if (a.vertexCount == 0 || a.vertexCount > 65536 || a.indexCount == 0 || a.indexCount > (3u << 20) || a.batchCount == 0 || a.batchCount > 4096)
                return nullptr;
            if (const auto it = g_meshes.find(group); it != g_meshes.end())
            {
                const MeshEntry& e = it->second;
                if (e.arrays.vertices == a.vertices && e.arrays.indices == a.indices && e.arrays.batches == a.batches &&
                    e.arrays.vertexCount == a.vertexCount && e.arrays.indexCount == a.indexCount && e.arrays.batchCount == a.batchCount &&
                    e.generation == glass::Generation())
                    return e.ok ? &e.mesh : nullptr;
            }
            if (g_meshBuilds) return nullptr;
            ++g_meshBuilds;
            if (g_meshes.size() > 64) g_meshes.clear();
            MeshEntry& e = g_meshes[group];
            e = MeshEntry{};
            e.arrays = a;
            e.generation = glass::Generation();
            std::vector<float> vertices(3 * static_cast<size_t>(a.vertexCount));
            std::vector<uint16_t> indices(a.indexCount);
            std::vector<uint8_t> batches(client::kMobaStride * static_cast<size_t>(a.batchCount));
            if (!CopyOut(a.vertices, vertices.data(), vertices.size() * sizeof(float)) || !CopyOut(a.indices, indices.data(), indices.size() * 2) ||
                !CopyOut(a.batches, batches.data(), batches.size()))
                return nullptr;
            std::vector<std::pair<uint32_t, uint32_t>> ranges;
            for (uint32_t b = 0; b < a.batchCount; ++b)
            {
                const uint8_t* m = batches.data() + client::kMobaStride * b;
                uint32_t start = 0;
                uint16_t count = 0;
                std::memcpy(&start, m + client::kMobaStart, 4);
                std::memcpy(&count, m + client::kMobaCount, 2);
                const uint8_t material = m[client::kMobaMaterial];
                if (material < r.glassMaterial.size() && r.glassMaterial[material]) continue;   // a window's inner face
                ranges.emplace_back(start, count);
            }
            e.ok = e.mesh.Build(vertices.data(), a.vertexCount, indices.data(), a.indexCount, ranges);
            return e.ok ? &e.mesh : nullptr;
        }

        // The group render callback while our walk runs: notes the room reached, then the client's (the sort table's).
        RenderCallbackFn g_savedCallback = nullptr;
        uint32_t g_target = 0xFFFFFFFFu;
        bool g_targetSeen = false;
        unsigned g_added = 0;

        void __cdecl Trampoline(uint32_t group, void* param)
        {
            if (group == g_target) g_targetSeen = true;
            ++g_added;
            g_savedCallback(group, param);
        }

        // The client's walk on into the room through the window: as RenderThruPortals goes through a portal (FrustumPush,
        // FrustumSet over the window's rectangle, itself at depth 1), with our callback in front of the client's. The
        // callback and the frustum stack are put back whatever happens.
        bool OpenWindow(void* root, uint32_t room, uint32_t fromGroup, float clip[4], const float frustumRect[4])
        {
            const auto callback = reinterpret_cast<RenderCallbackFn*>(client::kRenderCallback);
            auto* index = reinterpret_cast<int32_t*>(kFrustumIndex);
            const RenderCallbackFn saved = *callback;
            const int32_t savedIndex = *index;
            // The walk below pushes a frustum for each portal it goes through (at most the client's depth of 10): the stack
            // has 32.
            if (saved != reinterpret_cast<RenderCallbackFn>(client::kAddToSortTable) || savedIndex < 0 || savedIndex > 20) return false;
            g_savedCallback = saved;
            g_target = room;
            g_targetSeen = false;
            __try
            {
                *callback = &Trampoline;
                reinterpret_cast<FrustumPushFn>(client::kFrustumPush)();
                reinterpret_cast<FrustumSetFn>(client::kFrustumSet)(reinterpret_cast<const void*>(client::kFrustumCorners), frustumRect);
                g_inWalk = true;
                g_origWalk(root, nullptr, room, fromGroup, clip, 1, 0);
            }
            __finally
            {
                g_inWalk = false;
                *index = savedIndex;
                *callback = saved;
            }
            return g_targetSeen;
        }

        void Walk(void* root, uint32_t group, float* rect)
        {
            uint32_t interiorPass = 1, cullValid = 0;
            const void* callback = nullptr;
            const void* instance = nullptr;
            const void* viewer = nullptr;
            if (!ReadU32(client::kInteriorPass, interiorPass) || interiorPass || !ReadU32(client::kCullStateValid, cullValid) || !cullValid) return;
            if (!ReadPtr(client::kRenderCallback, callback) || callback != reinterpret_cast<const void*>(client::kAddToSortTable)) return;
            if (!ReadPtr(client::kRenderParam, instance) || !instance || !ReadPtr(client::kViewerMapObjDef, viewer) || viewer == instance) return;
            const void* owner = nullptr;
            if (!ReadPtr(reinterpret_cast<uintptr_t>(instance) + client::kDefOwner, owner) || owner != root) return;
            const RootEntry* r = RootFor(root);
            if (!r || !r->whitelisted || group >= r->groups.size() || !(r->groups[group].flags & client::kExteriorGroup)) return;
            uint32_t count = 0;
            const SeeWindow* windows = r->windows.Group(group, count);
            if (!windows) return;
            GroupFrame& f = g_frame[Key(instance, group)];
            if (f.root) return;   // once a frame
            f.root = r;
            f.group = group;
            ++g_now.groups;

            float cam[3], model[16], viewProj[16], eye[3], crect[4];
            if (!ReadFloats(client::kCamPosLocal, 3, cam) || !ReadFloats(client::kMapObjToWorld, 16, model) ||
                !ReadFloats(client::kViewProj, 16, viewProj) || !ReadFloats(client::kActiveWorldView, 3, eye) ||
                !ReadFloats(reinterpret_cast<uintptr_t>(rect), 4, crect))
            {
                f.states.assign(count, kShut);
                return;
            }
            for (int k = 0; k < 3; ++k) { f.cam[k] = cam[k]; f.eye[k] = eye[k]; }
            f.haveCam = true;
            std::vector<WindowPlan> plan;
            PlanWindows(windows, count, cam, r->groups[group].lo, r->groups[group].hi, FromCRect(crect), model, eye, viewProj, g_budget, plan);
            f.states.assign(count, kUnseen);
            for (uint32_t i = 0; i < count; ++i)
            {
                const WindowPlan& p = plan[i];
                f.states[i] = p.state;
                if (p.state == kShut && !p.candidate)
                {
                    ++g_now.shut;
                    NoteWindow(g_now, windows[i].ordinal, windows[i].interior < 0 ? Counts::kFake
                                                         : DistanceToBox(cam, r->groups[group].lo, r->groups[group].hi) > kRange ? Counts::kFar
                                                         : Counts::kBudget);
                }
                if (!p.candidate) continue;
                const uint32_t room = static_cast<uint32_t>(windows[i].interior);
                bool opened = false;
                Sight sight;
                Counts::Why why = Counts::kUnloaded;
                if (Enterable(static_cast<const uint8_t*>(root), static_cast<const uint8_t*>(instance), room, r->groupCount))
                {
                    // only where the room is seen through it from here: not over its ceiling, nor through its walls (the per-pixel
                    // redraw's; the whole-glass one opens every window with a room, as on 2026-10-20)
                    const bool lineOfSight = g_settings.redraw != 0;
                    const RoomMesh* mesh = lineOfSight ? MeshOf(*r, root, room) : nullptr;
                    if (lineOfSight && !mesh) why = Counts::kNoMesh;
                    else if (lineOfSight && !SeesRoom(windows[i], cam, *mesh, &sight)) { ++g_now.blind; why = Counts::kBlind; }
                    else
                    {
                        float clip[4], frustum[4];
                        ToCRect(p.clip, clip);
                        FrustumRect(p.clip, frustum);
                        opened = OpenWindow(root, room, group, clip, frustum);
                        why = opened ? Counts::kOpen : Counts::kNotReached;
                    }
                }
                NoteWindow(g_now, windows[i].ordinal, why, &sight);
                f.states[i] = opened ? kOpened : kShut;
                ++(opened ? g_now.opened : g_now.shut);
                f.opened += opened ? 1 : 0;
            }
            g_now.groupsAdded += g_added;
            g_added = 0;
            if (f.opened && !g_everOpened++ && g_api)
                g_api->Log(WXL_LOG_INFO, kTag, "see-through windows: the first window opened (%u of %u in group %u)", f.opened, count, group);
        }

        void WalkSafe(void* root, uint32_t group, float* rect)
        {
            __try { Walk(root, group, rect); }
            __except (EXCEPTION_EXECUTE_HANDLER) { Fault("the portal walk"); }
        }

        void __fastcall hkWalk(void* root, void* edx, uint32_t group, uint32_t fromGroup, float* rect, uint32_t depth, int interior)
        {
            g_origWalk(root, edx, group, fromGroup, rect, depth, interior);
            if (depth != 0 || g_inWalk || !Active()) return;
            WalkSafe(root, group, rect);
        }

        // --- the glass batches of the groups drawn now -------------------------------------------------------------
        struct DrawCtx
        {
            const GroupFrame* frame = nullptr;
            const SeeWindow* windows = nullptr;
            uint32_t count = 0;
        };
        DrawCtx g_draw;
        Deferral g_deferral;

        struct CachedBatch
        {
            const void* group = nullptr;
            uint32_t start = 0, count = 0, path = 0, groupIndex = 0;
            BatchWindows windows;
        };
        std::unordered_map<const void*, CachedBatch> g_batches;

        void ComputeBatch(const void* group, uint32_t start, uint32_t count, const SeeWindow* windows, uint32_t windowCount, BatchWindows& out)
        {
            const auto* g = static_cast<const uint8_t*>(group);
            const auto* indices = *reinterpret_cast<const uint16_t* const*>(g + client::kGroupIndices);
            const uint32_t indexCount = *reinterpret_cast<const uint32_t*>(g + client::kGroupIndexCount);
            const auto* vertices = *reinterpret_cast<const float* const*>(g + client::kGroupVertices);
            const uint32_t vertexCount = *reinterpret_cast<const uint32_t*>(g + client::kGroupVertexCount);
            if (vertexCount > (1u << 20)) return;
            out = WindowsOfBatch(indices, indexCount, vertices, vertexCount, start, count, windows, windowCount);
        }

        bool ComputeBatchSafe(const void* group, uint32_t start, uint32_t count, const SeeWindow* windows, uint32_t windowCount, BatchWindows& out)
        {
            __try { ComputeBatch(group, start, count, windows, windowCount, out); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool ReadRange(const void* record, uint32_t& start, uint32_t& count)
        {
            __try
            {
                start = *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(record) + client::kMobaStart);
                count = *reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(record) + client::kMobaCount);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        const BatchWindows* WindowsOf(const void* group, const void* record, uint32_t start, uint32_t count)
        {
            const GroupFrame& f = *g_draw.frame;
            if (const auto it = g_batches.find(record); it != g_batches.end())
                if (const CachedBatch& c = it->second;
                    c.group == group && c.start == start && c.count == count && c.path == f.root->path && c.groupIndex == f.group)
                    return &c.windows;
            if (g_batches.size() > 4096) g_batches.clear();
            CachedBatch& c = g_batches[record];
            c = CachedBatch{};
            c.group = group; c.start = start; c.count = count; c.path = f.root->path; c.groupIndex = f.group;
            if (!ComputeBatchSafe(group, start, count, g_draw.windows, g_draw.count, c.windows)) c.windows = BatchWindows{};
            return &c.windows;
        }

        // --- the deferred draw ---------------------------------------------------------------------------------------
        // The device's state as the batch's draw left it (the client's state flush done), and the draw's arguments as the
        // client's device draw works them out.
        // The shader's window lookup's inputs (c15..c20: the instance's frame and the group's grid; c180 the camera; s13 the
        // window table) at the batch's draw (its capture) and at its redraw (after its state is applied): "every see-through
        // window pixel is solid YELLOW" (the debug view, in game, 2026-10-21: the lookup found no window). Against them the
        // walk's own camera in the WMO's space (where c15..c17 put the camera: their .w) and the view's eye (c180's), Glass's
        // table, and whether the client's index buffer holds the batch's own indices (the split redraw draws them). The
        // panel's, the first deferred batch's, refreshed once a second.
        struct LookupProbe
        {
            bool valid = false, applied = false;
            float atDraw[7][4] = {}, atRedraw[7][4] = {};   // c15..c20, c180
            const void* tableAtDraw = nullptr;
            const void* tableAtRedraw = nullptr;
            const void* glassTable = nullptr;
            float cam[3] = {}, eye[3] = {};
            bool haveCam = false;
            int indices = -2;                                 // 1 the client's are the batch's, 0 not, -1 unread, -2 not split
            bool split = false;
            uint32_t open = 0, rest = 0;
        };
        LookupProbe g_lookupNow, g_lookupShown;

        void ReadLookup(IDirect3DDevice9* dev, float regs[7][4], const void*& table)
        {
            dev->GetPixelShaderConstantF(glass::kRoomConstant, &regs[0][0], 6);
            dev->GetPixelShaderConstantF(glass::kViewConstant, &regs[6][0], 1);
            IDirect3DBaseTexture9* t = nullptr;
            table = nullptr;
            if (SUCCEEDED(dev->GetTexture(glass::kTableSampler, &t)) && t) { table = t; t->Release(); }
        }

        bool Capture(void* device, const uint32_t* batch, Deferral::Entry& e)
        {
            const auto* d = static_cast<const uint8_t*>(device);
            auto* dev = *reinterpret_cast<IDirect3DDevice9* const*>(d + 0x397C);
            if (!dev) return false;
            const uint32_t mode = *reinterpret_cast<const uint32_t*>(d + client::kGxBaseVertexMode);
            const auto* stream = *reinterpret_cast<const uint8_t* const*>(d + client::kGxVertexStream);
            const auto* indexBuffer = *reinterpret_cast<const uint8_t* const*>(d + client::kGxIndexBuffer);
            if (!indexBuffer || (!mode && !stream) || batch[0] >= 6) return false;
            const uint32_t streamOffset = stream ? *reinterpret_cast<const uint32_t*>(stream + client::kGxBufIndex) : 0;
            const uint32_t streamStride = stream ? *reinterpret_cast<const uint32_t*>(stream + client::kGxBufItemSize) : 0;
            const uint32_t indexOffset = *reinterpret_cast<const uint32_t*>(indexBuffer + client::kGxBufIndex);
            const uint32_t primitive = reinterpret_cast<const uint32_t*>(client::kPrimConversion)[batch[0]];
            const uint32_t b[4] = { batch[0], batch[1], batch[2], batch[3] };
            if (!DrawArgsOf(b, mode, streamOffset, streamStride, indexOffset, primitive, e.args)) return false;
            if (!CaptureState(dev, e)) return false;   // (every pixel shader constant too: a state block keeps c0..c127)
            if (!g_lookupNow.valid)
            {
                g_lookupNow = LookupProbe{};
                g_lookupNow.valid = true;
                ReadLookup(dev, g_lookupNow.atDraw, g_lookupNow.tableAtDraw);
                g_lookupNow.glassTable = glass::TableTexture();
                if (g_draw.frame && g_draw.frame->haveCam)
                {
                    for (int k = 0; k < 3; ++k) { g_lookupNow.cam[k] = g_draw.frame->cam[k]; g_lookupNow.eye[k] = g_draw.frame->eye[k]; }
                    g_lookupNow.haveCam = true;
                }
            }
            IDirect3DSurface9* target = nullptr;
            IDirect3DSurface9* depth = nullptr;
            dev->GetRenderTarget(0, &target);
            dev->GetDepthStencilSurface(&depth);
            e.target = target;
            e.depth = depth;
            if (target) target->Release();
            if (depth) depth->Release();
            return true;
        }

        // The client's draw with its colour and depth writes off, then the capture; the writes put back whatever happens
        // (apart from DeferDraw: an entry holds vectors, and __try can't share a function with their unwinding).
        bool DrawSilently(void* device, void* edx, uint32_t* batch, int indexed, int& drawn, uint32_t colour, uint32_t depth, Deferral::Entry& e)
        {
            bool ok = false;
            __try
            {
                gxstate::Force(client::kRsColorWrite, 0);
                gxstate::Force(client::kRsDepthWrite, 0);
                drawn = 1;
                g_origDraw(device, edx, batch, indexed);
                ok = Capture(device, batch, e);
            }
            __finally
            {
                gxstate::Force(client::kRsColorWrite, colour);
                gxstate::Force(client::kRsDepthWrite, depth);
            }
            return ok;
        }

        // The draw with the client's colour and depth writes off (its own cache, so its flush sends them: no pixel and no
        // depth from the glass), the state captured after it, the writes put back for the next draw.
        void DeferDraw(void* device, void* edx, uint32_t* batch, int indexed, int& drawn)
        {
            uint32_t blending = 99;
            gxstate::Read(client::kRsBlendingMode, blending);
            if (g_deferral.OnDraw(indexed != 0, batch[0], batch[1], batch[2], blending) == Deferral::Draw::Pass)
            {
                ++g_now.passed;
                drawn = 1;
                g_origDraw(device, edx, batch, indexed);
                return;
            }
            uint32_t colour = 0, depth = 0;
            if (!gxstate::Read(client::kRsColorWrite, colour) || !gxstate::Read(client::kRsDepthWrite, depth))
            {
                g_deferral.CaptureFailed();
                ++g_now.captureFailed;
                drawn = 1;
                g_origDraw(device, edx, batch, indexed);
                return;
            }
            Deferral::Entry e{};
            const bool ok = DrawSilently(device, edx, batch, indexed, drawn, colour, depth, e);
            if (ok)
            {
                g_deferral.Captured(e);
                ++g_now.deferred;
                return;
            }
            // Drawn as it was after all: the writes are back, so this draw is the client's own.
            g_deferral.CaptureFailed();
            ++g_now.captureFailed;
            g_origDraw(device, edx, batch, indexed);
        }

        void __fastcall hkDraw(void* device, void* edx, uint32_t* batch, int indexed)
        {
            if (!g_deferral.Pending() || !batch)
            {
                g_origDraw(device, edx, batch, indexed);
                return;
            }
            int drawn = 0;
            __try { DeferDraw(device, edx, batch, indexed, drawn); }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_deferral.EndLoop();
                Fault("a glass batch's deferred draw");
                if (!drawn) g_origDraw(device, edx, batch, indexed);
            }
        }

        // Whether a pixel shader has the redraw (GlassShader's `if b11`), looked up once per shader object.
        std::unordered_map<const void*, bool> g_replayShaders;

        bool ShaderCode(IDirect3DPixelShader9* ps, std::vector<uint32_t>& tokens)
        {
            UINT bytes = 0;
            if (FAILED(ps->GetFunction(nullptr, &bytes)) || bytes < 8 || bytes > (1u << 20)) return false;
            tokens.resize(bytes / 4);
            UINT got = bytes;
            return SUCCEEDED(ps->GetFunction(tokens.data(), &got));
        }

        bool HasReplay(IDirect3DDevice9* dev)
        {
            IDirect3DPixelShader9* ps = nullptr;
            if (FAILED(dev->GetPixelShader(&ps)) || !ps) return false;
            auto it = g_replayShaders.find(ps);
            if (it == g_replayShaders.end())
            {
                std::vector<uint32_t> tokens;
                if (g_replayShaders.size() > 4096) g_replayShaders.clear();
                it = g_replayShaders.emplace(ps, ShaderCode(ps, tokens) && glass::HasReplaySwitch(tokens.data(), tokens.size())).first;
            }
            const bool has = it->second;
            ps->Release();
            return has;
        }

        bool Draw(IDirect3DDevice9* dev, const DrawArgs& a)
        {
            return SUCCEEDED(dev->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(a.primitive), static_cast<INT>(a.baseVertex), a.minIndex,
                                                       a.vertices, a.startIndex, a.primitives));
        }

        // The per-pixel redraw's lead masks ("yeah build the lead mask pls :D", the user, 2026-10-21; env/GlassModel
        // LeadMask): one texture a glass material with GlassLead.cdbc rectangles, built from its texture (the client's own
        // file, read and decoded here: nothing stored) the first time a batch of it is redrawn, at most one a frame, with
        // its mip chain; null where it has none or the file couldn't be read (the shader keeps the light's mask: c28.w 0).
        // Built again when the glass tables reload.
        struct LeadTexture { IDirect3DTexture9* tex = nullptr; bool tried = false; };
        std::unordered_map<uint32_t, LeadTexture> g_leadTextures;
        uint32_t g_leadGeneration = 0;
        unsigned g_leadBuilt = 0, g_leadFailed = 0;
        bool g_leadThisFrame = false;
        char g_leadWhy[200] = "";

        void ReleaseLead()
        {
            for (auto& kv : g_leadTextures)
                if (kv.second.tex) kv.second.tex->Release();
            g_leadTextures.clear();
            g_leadBuilt = g_leadFailed = 0;
            g_leadWhy[0] = 0;
        }

        // The mask's levels into a managed texture (L8, else A8R8G8B8 grey), null on failure.
        IDirect3DTexture9* LeadUpload(IDirect3DDevice9* dev, std::vector<uint8_t> level, uint32_t w, uint32_t h)
        {
            UINT levels = 1;
            for (uint32_t s = std::max(w, h); s > 1; s >>= 1) ++levels;
            IDirect3DTexture9* tex = nullptr;
            bool l8 = SUCCEEDED(dev->CreateTexture(w, h, levels, 0, D3DFMT_L8, D3DPOOL_MANAGED, &tex, nullptr)) && tex;
            if (!l8 && (FAILED(dev->CreateTexture(w, h, levels, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) || !tex)) return nullptr;
            std::vector<uint8_t> next;
            for (UINT k = 0; k < levels; ++k)
            {
                D3DLOCKED_RECT lr{};
                if (FAILED(tex->LockRect(k, &lr, nullptr, 0))) { tex->Release(); return nullptr; }
                for (uint32_t y = 0; y < h; ++y)
                {
                    uint8_t* row = static_cast<uint8_t*>(lr.pBits) + static_cast<size_t>(y) * lr.Pitch;
                    for (uint32_t x = 0; x < w; ++x)
                    {
                        const uint8_t v = level[static_cast<size_t>(y) * w + x];
                        if (l8) row[x] = v;
                        else { row[4 * x] = row[4 * x + 1] = row[4 * x + 2] = v; row[4 * x + 3] = 255; }
                    }
                }
                tex->UnlockRect(k);
                if (k + 1 < levels) { glass::LeadMip(level, w, h, next); level.swap(next); }
            }
            return tex;
        }

        IDirect3DTexture9* LeadTextureOf(IDirect3DDevice9* dev, uint32_t material)
        {
            if (!material) return nullptr;
            if (g_leadGeneration != glass::Generation()) { ReleaseLead(); g_leadGeneration = glass::Generation(); }
            LeadTexture& lt = g_leadTextures[material];
            if (lt.tried || g_leadThisFrame) return lt.tex;
            lt.tried = true;
            const glass::LeadRule* rule = glass::LeadOf(material);
            const glass::Material* m = glass::MaterialAt(material);
            const char* path = glass::TextureOf(material);
            if (!rule || !m || !path) return nullptr;   // no rectangles: the light's mask
            g_leadThisFrame = true;
            std::vector<uint8_t> file, mask;
            std::vector<float> rgb;
            uint32_t w = 0, h = 0;
            const char* why = !blp::ReadBytes(path, file) ? "can't read the file"
                            : !blp::DecodeTop(file.data(), file.size(), rgb, w, h) ? "can't decode it"
                            : !glass::LeadMask(rgb.data(), w, h, *m, *rule, mask) ? "no texel in its rectangles" : nullptr;
            if (!why && !(lt.tex = LeadUpload(dev, std::move(mask), w, h))) why = "can't make the texture";
            if (why)
            {
                ++g_leadFailed;
                std::snprintf(g_leadWhy, sizeof(g_leadWhy), "%s: %s", path, why);
                return nullptr;
            }
            ++g_leadBuilt;
            return lt.tex;
        }

        // What the redraw set and read back at its first batch (the panel's, refreshed once a second): so an in-game check
        // says whether the device got what the redraw meant ("see-through windows don't seem to work anymore now", the user,
        // 2026-10-21, with every count saying it was drawn).
        struct ReplayProbe
        {
            bool valid = false;
            int mode = 0;
            BOOL glassOn = FALSE, hasReplay = FALSE, b11 = FALSE;
            float c219[4] = {}, c28[4] = {}, c33[4] = {};
            DWORD test = 0, func = 0, ref = 0;             // the batch's own alpha test, as captured
            DWORD atoc = 0, multisample = 0;               // D3DRS_ADAPTIVETESS_Y (a driver's alpha-to-coverage switch), the target's MSAA
            DWORD srcBlend = 0, destBlend = 0, blend = 0, alphaTest = 0, zWrite = 0;   // as set for the see-through draw
            const void* shader = nullptr;
        };
        ReplayProbe g_probeNow, g_probeShown;
        int g_debugView = 0;                     // the panel's debug view (not saved)
        ULONGLONG g_probeTick = 0;
        SplitBuffer g_split;                     // the split redraw's index buffer

        // Whether the client's index buffer holds the batch's own indices where its draw reads them (the split redraw draws
        // its open windows' from those): 1 the same (count and sum), 0 not (or not 16-bit), -1 not readable (write-only);
        // split only on 1. Once per buffer and range.
        std::unordered_map<uint64_t, int> g_indexVerdict;

        int IndicesAgree(IDirect3DDevice9* dev, const Deferral::Entry& e)
        {
            IDirect3DIndexBuffer9* ib = nullptr;
            if (FAILED(dev->GetIndices(&ib)) || !ib) return -1;
            const uint64_t key = (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ib)) << 24) ^ e.args.startIndex ^ (static_cast<uint64_t>(e.args.primitives) << 48);
            int verdict = -1;
            if (const auto it = g_indexVerdict.find(key); it != g_indexVerdict.end()) verdict = it->second;
            else
            {
                D3DINDEXBUFFER_DESC d{};
                const UINT n = e.args.primitives * 3;
                void* p = nullptr;
                if (FAILED(ib->GetDesc(&d))) verdict = -1;
                else if (d.Format != D3DFMT_INDEX16 || n != e.open.size() + e.rest.size()) verdict = 0;
                else if ((d.Usage & D3DUSAGE_WRITEONLY) || (static_cast<size_t>(e.args.startIndex) + n) * 2 > d.Size) verdict = -1;
                else if (SUCCEEDED(ib->Lock(e.args.startIndex * 2, n * 2, &p, D3DLOCK_READONLY)) && p)
                {
                    uint64_t theirs = 0, ours = 0;
                    for (UINT k = 0; k < n; ++k) theirs += static_cast<const uint16_t*>(p)[k];
                    ib->Unlock();
                    for (const uint16_t v : e.open) ours += v;
                    for (const uint16_t v : e.rest) ours += v;
                    verdict = theirs == ours ? 1 : 0;
                }
                if (g_indexVerdict.size() > 1024) g_indexVerdict.clear();
                g_indexVerdict[key] = verdict;
            }
            ib->Release();
            return verdict;
        }

        void Probe(IDirect3DDevice9* dev, int mode, BOOL glassOn, BOOL hasReplay, DWORD test, DWORD func, DWORD ref, DWORD multisample)
        {
            if (g_probeNow.valid) return;
            g_probeNow = ReplayProbe{};
            g_probeNow.valid = true;
            g_probeNow.mode = mode;
            g_probeNow.glassOn = glassOn;
            g_probeNow.hasReplay = hasReplay;
            dev->GetPixelShaderConstantB(glass::kReplaySwitch, &g_probeNow.b11, 1);
            dev->GetPixelShaderConstantF(glass::kReplayConstant, g_probeNow.c219, 1);
            dev->GetPixelShaderConstantF(glass::kReplayDrawConstant, g_probeNow.c28, 1);
            dev->GetPixelShaderConstantF(glass::kReplayWindowConstant, g_probeNow.c33, 1);
            g_probeNow.test = test; g_probeNow.func = func; g_probeNow.ref = ref;
            dev->GetRenderState(D3DRS_SRCBLEND, &g_probeNow.srcBlend);
            dev->GetRenderState(D3DRS_DESTBLEND, &g_probeNow.destBlend);
            dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &g_probeNow.blend);
            dev->GetRenderState(D3DRS_ALPHATESTENABLE, &g_probeNow.alphaTest);
            dev->GetRenderState(D3DRS_ZWRITEENABLE, &g_probeNow.zWrite);
            dev->GetRenderState(D3DRS_ADAPTIVETESS_Y, &g_probeNow.atoc);
            g_probeNow.multisample = multisample;
            IDirect3DPixelShader9* ps = nullptr;
            if (SUCCEEDED(dev->GetPixelShader(&ps)) && ps) { g_probeNow.shader = ps; ps->Release(); }
        }

        // The glass again, over the room and its furniture: each from its state with every shader constant (SeeThroughDraw's
        // ApplyCaptured: a state block alone put back c0..c127, the glass's view and look from c180 lost: its window lookup
        // found no window, the debug view all yellow, 2026-10-21), the colour only (no alpha written), depth tested.
        // SeeThroughRedraw 1 (SeeThroughDraw's DrawPerPixel), with the glass's look and the redraw in its shader (b11, c219,
        // c28, c33: GlassShader): two draws split in the shader by its alpha (c28.y): what's opaque (the open windows' lead,
        // frame and tracery, every shut window with its fake room) with its depth, as the client would have drawn it; then the
        // open windows' glass texels over the room, blended at the opacity (SRCALPHA), no depth. Without the redraw in its
        // shader: opaque, as the client would have. SeeThroughRedraw 0 (as on 2026-10-20): the whole batch at the opacity
        // (BLENDFACTOR), its fake room off (c193.z 0), no depth. The device's state put back after.
        void Replay(Deferral::Entry* entries, size_t n)
        {
            auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice());
            if (!dev) { g_now.dropped += static_cast<unsigned>(n); return; }
            IDirect3DStateBlock9* saved = nullptr;
            if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) { g_now.dropped += static_cast<unsigned>(n); return; }
            IDirect3DSurface9* target = nullptr;
            IDirect3DSurface9* depth = nullptr;
            dev->GetRenderTarget(0, &target);
            dev->GetDepthStencilSurface(&depth);
            D3DSURFACE_DESC desc{};
            if (target) { target->GetDesc(&desc); target->Release(); }
            if (depth) depth->Release();
            const bool perPixel = g_settings.redraw != 0;
            __try
            {
                for (size_t i = 0; i < n; ++i)
                {
                    const Deferral::Entry& e = entries[i];
                    if (!e.state) continue;
                    if (e.target != target || e.depth != depth) { ++g_now.targetMoved; continue; }
                    // its state, then every pixel shader constant (a state block alone puts back c0..c127: the glass's view,
                    // look and window lookup are above; the lookup's inputs read in between, for the panel)
                    ApplyCaptured(dev, e, false);
                    if (g_lookupNow.valid && !g_lookupNow.applied)
                    {
                        ReadLookup(dev, g_lookupNow.atRedraw, g_lookupNow.tableAtRedraw);
                        g_lookupNow.applied = true;
                    }
                    ApplyConstants(dev, e);
                    BOOL glassOn = FALSE;
                    const bool glass = SUCCEEDED(dev->GetPixelShaderConstantB(glass::kSwitch, &glassOn, 1)) && glassOn;
                    // the batch's own alpha test (the stock alpha's: the shader's texkill in the per-pixel redraw)
                    DWORD test = FALSE, func = D3DCMP_ALWAYS, ref = 0;
                    dev->GetRenderState(D3DRS_ALPHATESTENABLE, &test);
                    dev->GetRenderState(D3DRS_ALPHAFUNC, &func);
                    dev->GetRenderState(D3DRS_ALPHAREF, &ref);
                    dev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
                    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
                    if (!perPixel)
                    {
                        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
                        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                        dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
                        dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
                        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_BLENDFACTOR);
                        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVBLENDFACTOR);
                        dev->SetRenderState(D3DRS_BLENDFACTOR, BlendFactor(g_settings.opacity));
                        // the glass's own look stays; its fake room goes (the real one is behind it)
                        float c193[4];
                        if (glass && SUCCEEDED(dev->GetPixelShaderConstantF(glass::kFirstConstant + 1, c193, 1)))
                        {
                            c193[2] = 0.0f;
                            dev->SetPixelShaderConstantF(glass::kFirstConstant + 1, c193, 1);
                        }
                        Probe(dev, 0, glassOn, FALSE, test, func, ref, desc.MultiSampleType);
                        if (Draw(dev, e.args)) ++g_now.redrawn;
                        continue;
                    }
                    if (!glass || !HasReplay(dev))
                    {
                        dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
                        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
                        Probe(dev, 1, glassOn, FALSE, test, func, ref, desc.MultiSampleType);
                        if (Draw(dev, e.args)) { ++g_now.redrawn; ++g_now.opaque; }
                        continue;
                    }
                    // split by window (SeeThroughDraw) when the client's index buffer is seen to hold the batch's indices, else whole
                    PerPixel in;
                    in.opacity = g_settings.opacity;
                    in.debug = g_debugView != 0;
                    in.lead = LeadTextureOf(dev, e.material);
                    const int agree = e.open.empty() ? -2 : IndicesAgree(dev, e);
                    in.split = agree == 1;
                    const PerPixelDrawn drawn = DrawPerPixel(dev, e, in, g_split);
                    if (g_lookupNow.valid && g_lookupNow.indices == -2)
                    {
                        g_lookupNow.indices = agree;
                        g_lookupNow.split = drawn.split;
                        g_lookupNow.open = drawn.open;
                        g_lookupNow.rest = drawn.rest;
                    }
                    Probe(dev, 1, glassOn, TRUE, test, func, ref, desc.MultiSampleType);
                    if (drawn.drawn) ++g_now.redrawn;
                }
            }
            __finally
            {
                saved->Apply();
                saved->Release();
            }
        }

        void Release(std::vector<Deferral::Entry>& entries)
        {
            for (Deferral::Entry& e : entries)
                if (e.state)
                {
                    static_cast<IDirect3DStateBlock9*>(e.state)->Release();
                    e.state = nullptr;
                }
        }

        bool ReplaySafe(Deferral::Entry* entries, size_t n)
        {
            __try { Replay(entries, n); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        void Flush()
        {
            std::vector<Deferral::Entry> entries = g_deferral.Take();
            if (entries.empty()) return;
            if (!ReplaySafe(entries.data(), entries.size())) Fault("the glass's redraw");
            Release(entries);
            if (g_now.redrawn && !g_everRedrawn++ && g_api)
                g_api->Log(WXL_LOG_INFO, kTag, "see-through windows: the first glass drawn over a room (%u batches)", g_now.redrawn);
        }

        void __fastcall hkScene(void* scene, void* edx, int pass)
        {
            g_origScene(scene, edx, pass);
            if (pass != 0 || !g_deferral.Count()) return;
            const void* world = nullptr;
            if (!ReadPtr(client::kM2Scene, world) || scene != world) return;
            Flush();
        }

        template <class F> bool Attach(const WXL_Api* api, const char* point, F detour, void** original)
        {
            return api->HookAttachByName(point, reinterpret_cast<void*>(detour), original, WXL_HOOK_DEFAULT_PRIORITY) != 0 ||
                   api->HookAttachByName(point, reinterpret_cast<void*>(detour), original, 1000) != 0;
        }

        int CheckGuards(const char*& why)
        {
            __try
            {
                const Guard* bad = FirstMismatch(kGuards, [](uintptr_t a, uint32_t) { return reinterpret_cast<const void*>(a); });
                if (bad) { why = bad->what; return 2; }
                return 1;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { why = "a fault reading the client's code"; return 2; }
        }

        char g_status[256] = "see-through windows: not started";
    }

    Settings& Config() { return g_settings; }

    void Install(const WXL_Api* api)
    {
        g_api = api;
        if (!api || !api->HookAttachByName)
        {
            std::snprintf(g_status, sizeof(g_status), "see-through windows: off (no hook interface)");
            return;
        }
        g_guards = CheckGuards(g_guardWhy);
        if (g_guards != 1)
        {
            std::snprintf(g_status, sizeof(g_status), "see-through windows: off (the client differs: %s)", g_guardWhy);
            api->Log(WXL_LOG_WARN, kTag, "%s", g_status);
            return;
        }
        g_hookedWalk = Attach(api, "Wmo.RenderThroughPortals", &hkWalk, reinterpret_cast<void**>(&g_origWalk));
        g_hookedDraw = Attach(api, "Gx.DeviceDraw", &hkDraw, reinterpret_cast<void**>(&g_origDraw));
        g_hookedScene = Attach(api, "M2.SceneDraw", &hkScene, reinterpret_cast<void**>(&g_origScene));
        if (!g_hookedWalk || !g_hookedDraw || !g_hookedScene)
        {
            std::snprintf(g_status, sizeof(g_status), "see-through windows: off (a hook failed: portal walk %d, device draw %d, model scene %d)",
                          g_hookedWalk ? 1 : 0, g_hookedDraw ? 1 : 0, g_hookedScene ? 1 : 0);
            api->Log(WXL_LOG_WARN, kTag, "%s", g_status);
            return;
        }
        std::snprintf(g_status, sizeof(g_status), "see-through windows: hooks in, %u guards hold", static_cast<unsigned>(sizeof(kGuards) / sizeof(kGuards[0])));
        api->Log(WXL_LOG_INFO, kTag, "%s", g_status);
    }

    void Update()
    {
        // Whatever a frame left (no model scene drawn after its buildings): released, not drawn.
        std::vector<Deferral::Entry> left = g_deferral.Take();
        g_now.dropped += static_cast<unsigned>(left.size());
        Release(left);
        g_last = g_now;
        g_now = Counts{};
        g_deferral.ResetCounts();
        g_deferral.EndLoop();
        g_frame.clear();
        if (g_roots.size() > 2048) g_roots.clear();   // only here: nothing of this frame points at them now
        g_budget = kMaxOpenPerFrame;
        g_meshBuilds = 0;
        g_leadThisFrame = false;
        // the redraw's read-back, shown for a second at a time
        const ULONGLONG now = GetTickCount64();
        if (g_probeNow.valid && now - g_probeTick >= 1000)
        {
            g_probeShown = g_probeNow;
            g_lookupShown = g_lookupNow;
            g_probeTick = now;
        }
        g_probeNow = ReplayProbe{};
        g_lookupNow = LookupProbe{};
        g_draw = DrawCtx{};
    }

    void OnDeviceLost()
    {
        std::vector<Deferral::Entry> left = g_deferral.Take();
        Release(left);
        g_replayShaders.clear();
    }

    void BeginGroupDraw(const void* root, int group, const void* instanceArg)
    {
        g_draw = DrawCtx{};
        if (g_frame.empty() || !Active() || !instanceArg || group < 0) return;
        const void* instance = static_cast<const uint8_t*>(instanceArg) - client::kDefInvMat;
        const auto it = g_frame.find(Key(instance, static_cast<uint32_t>(group)));
        if (it == g_frame.end() || !it->second.opened || !it->second.root) return;
        const GroupFrame& f = it->second;
        uint32_t count = 0;
        const SeeWindow* windows = f.root->windows.Group(f.group, count);
        if (!windows || count != f.states.size()) return;
        (void)root;
        g_draw.frame = &f;
        g_draw.windows = windows;
        g_draw.count = count;
    }

    void EndGroupDraw()
    {
        g_draw = DrawCtx{};
        g_deferral.EndLoop();
    }

    void Batch(const void* group, const void* record, uint32_t glassMaterial)
    {
        const bool glass = glassMaterial != 0;
        if (!g_draw.frame)
        {
            if (g_deferral.Pending()) g_deferral.EndLoop();
            return;
        }
        uint32_t start = 0, count = 0;
        if (!glass || !group || !record || !ReadRange(record, start, count))
        {
            g_deferral.BeginBatch(false, 0, 0);
            return;
        }
        ++g_now.glassBatches;
        const BatchWindows* b = WindowsOf(group, record, start, count);
        // (without the glass's look there's no mask to keep the lead opaque, nor a way to tell its windows apart: opaque)
        const bool glassLook = glass::Config().enabled && glass::PatchedVariants() > 0;
        // (the whole-glass redraw: every window of the batch open or unseen; the per-pixel one tells them apart)
        const bool deferrable = glassLook && b &&
                                (g_settings.redraw ? Deferrable(*b, g_draw.frame->states.data(), g_draw.frame->states.size())
                                                   : DeferrableWhole(*b, g_draw.frame->states.data(), g_draw.frame->states.size()));
        if (!deferrable) ++g_now.kept;
        float first = 0.0f;
        uint32_t bits = 0;
        if (deferrable && g_settings.redraw)
        {
            OpenBits(*b, g_draw.windows, g_draw.frame->states.data(), g_draw.frame->states.size(), first, bits);
            g_now.replayFirst = first;
            g_now.replayBits = bits;
        }
        g_deferral.BeginBatch(deferrable, start, count, first, bits, glassMaterial);
        if (deferrable && g_settings.redraw)
        {
            std::vector<uint16_t> open, rest;
            SplitByWindow(*b, g_draw.frame->states.data(), g_draw.frame->states.size(), open, rest);
            g_deferral.SplitPending(std::move(open), std::move(rest));
        }
    }

    void EndLoop() { g_deferral.EndLoop(); }

    void DrawPanelSection(const WXL_Api* api)
    {
        if (!api->UiCollapsingHeader("See-through windows (experimental): the real room behind the glass##seethrough")) return;
        api->UiCheckbox("see-through windows (the Lion's Pride Inn in Goldshire only, for now)##ston", &g_settings.enabled);
        api->UiSliderFloat("the glass's opacity over the room##stop", &g_settings.opacity, 0.0f, 1.0f);
        api->UiCheckbox("per-pixel redraw: the panes see-through, the lead, frame and shutters opaque, only where the room is seen "
                        "(SeeThroughRedraw 1; off: the whole glass at the opacity, every window with a room open, as on 2026-10-20)##stredraw",
                        &g_settings.redraw);
        if (g_settings.redraw)
            api->UiCheckbox("debug view (not saved): the redraw's pixels by what the shader decided: magenta an open window's glass (through "
                            "grey to green as it's less glass), green its lead and frame (opaque), cyan a shut window, yellow no window "
                            "found##stdebug", &g_debugView);
        api->UiText(StatusLine());
        char line[800];
        std::snprintf(line, sizeof(line), "Last frame: %u exterior groups walked, %u windows opened, %u shut (fake, far, unloaded or over budget; "
                      "%u of them: the room not seen through it from here), %u rooms added",
                      g_last.groups, g_last.opened, g_last.shut, g_last.blind, g_last.groupsAdded);
        api->UiText(line);
        std::snprintf(line, sizeof(line), "Glass batches: %u in opened groups, %u kept opaque, %u deferred, %u drawn over the room (%u opaque: no redraw "
                      "in their shader); %u drawn as they were", g_last.glassBatches, g_last.kept, g_last.deferred, g_last.redrawn, g_last.opaque, g_last.passed);
        api->UiText(line);
        std::snprintf(line, sizeof(line), "Faults: %u%s%s; captures failed %u, target changed %u, dropped %u", g_faults, g_faulted ? " (off: " : "",
                      g_faulted ? g_faultWhere : "", g_last.captureFailed, g_last.targetMoved, g_last.dropped);
        if (g_faulted) std::strncat(line, ")", sizeof(line) - std::strlen(line) - 1);
        api->UiText(line);
        if (g_settings.redraw)
        {
            std::snprintf(line, sizeof(line), "Lead masks (the per-pixel redraw's, from GlassLead.cdbc's panes): %u built, %u not%s%s", g_leadBuilt,
                          g_leadFailed, g_leadFailed ? ": " : "", g_leadWhy);
            api->UiText(line);
        }
        // what the redraw set and read back (once a second)
        if (g_probeShown.valid)
        {
            const ReplayProbe& q = g_probeShown;
            std::snprintf(line, sizeof(line), "The redraw's device state (read back): %s; glass b9 %d, redraw in its shader %d, b11 %d; c219 %.3g %.3g %.0f %.1f; "
                          "c28 %.0f %.0f %.3f %.0f, c33 %.0f; the batch's alpha test %lu (func %lu, ref %lu); blend %lu (src %lu, dest %lu), alpha test %lu, depth "
                          "write %lu; alpha-to-coverage switch 0x%08lX, target MSAA %lu; shader %p",
                          q.mode ? "per pixel" : "the whole glass", q.glassOn, q.hasReplay, q.b11, q.c219[0], q.c219[1], q.c219[2], q.c219[3], q.c28[0],
                          q.c28[1], q.c28[2], q.c28[3], q.c33[0], q.test, q.func, q.ref, q.blend, q.srcBlend, q.destBlend, q.alphaTest, q.zWrite, q.atoc, q.multisample,
                          q.shader);
            api->UiText(line);
        }
        if (g_lookupShown.valid)
        {
            const LookupProbe& q = g_lookupShown;
            const float (*d)[4] = q.atDraw;
            bool same = q.applied && q.tableAtRedraw == q.tableAtDraw;
            for (int r = 0; r < 7 && q.applied; ++r)
                for (int k = 0; k < 4; ++k) same = same && q.atRedraw[r][k] == d[r][k];
            std::snprintf(line, sizeof(line), "The lookup's inputs at the glass's draw: the grid c20.w %g (its corner %.1f %.1f %.1f, cell 1/%.2f, last %g %g %g); the camera "
                          "in the WMO's space by c15..c17 %.2f %.2f %.2f, by the walk %s%.2f %.2f %.2f; c180 the camera %.1f %.1f %.1f, the view's eye %.1f %.1f %.1f; "
                          "s13 %p (Glass's table %p); at the redraw: %s",
                          d[5][3], d[3][0], d[3][1], d[3][2], d[4][0] != 0.0f ? 1.0f / d[4][0] : 0.0f, d[5][0], d[5][1], d[5][2], d[0][3], d[1][3], d[2][3],
                          q.haveCam ? "" : "(none) ", q.cam[0], q.cam[1], q.cam[2], d[6][0], d[6][1], d[6][2], q.eye[0], q.eye[1], q.eye[2], q.tableAtDraw,
                          q.glassTable, !q.applied ? "not read" : same ? "the state block put them back" : "the state block didn't put them all back (put back from the capture)");
            api->UiText(line);
            std::snprintf(line, sizeof(line), "The redraw split by window: %s (%u triangles open, %u the rest); the client's index buffer %s",
                          q.split ? "yes" : "no: whole, the lookup alone", q.open, q.rest,
                          q.indices == 1 ? "holds the batch's own indices" : q.indices == 0 ? "differs from the batch's (drawn whole)"
                          : q.indices == -1 ? "can't be read (write-only: drawn whole)" : "not checked (nothing open to split)");
            api->UiText(line);
        }
        // each window on the screen and facing the camera last frame: open, or why not
        if (g_last.replayFirst >= 0.0f)
        {
            std::snprintf(line, sizeof(line), "The redraw: open windows from ordinal %.0f, bits 0x%04X (a shut window's glass and fake room as the client draws them)",
                          g_last.replayFirst, g_last.replayBits);
            api->UiText(line);
        }
        for (unsigned i = 0; i < g_last.noteCount; ++i)
        {
            const Counts::Note& n = g_last.notes[i];
            const Sight& s = n.sight;
            static const char* const kHit[3] = { "nothing of the room (past it, or out through a doorway)", "its inside", "a wall or ceiling from outside" };
            char rays[300] = "";
            if (n.why == Counts::kOpen || n.why == Counts::kBlind || n.why == Counts::kNotReached)
            {
                std::snprintf(rays, sizeof(rays), "; %d of 9 rays meet the room from inside (%d of %d needed, the middle one: %s)", s.front, kSightFront, 9,
                              kHit[s.middle]);
                if (s.firstMiss >= 0)
                {
                    char miss[120];
                    std::snprintf(miss, sizeof(miss), "; ray %d (%+.1f, %+.1f) meets %s", s.firstMiss, kSight[s.firstMiss][0], kSight[s.firstMiss][1],
                                  kHit[s.missHit]);
                    std::strncat(rays, miss, sizeof(rays) - std::strlen(rays) - 1);
                    if (s.missHit == RoomMesh::kBack)
                    {
                        std::snprintf(miss, sizeof(miss), " %.1f yd past the glass", s.missDepth);
                        std::strncat(rays, miss, sizeof(rays) - std::strlen(rays) - 1);
                    }
                }
            }
            static const char* const kWhy[] = { "open", "shut: fake (nothing modelled behind it)", "shut: too far", "shut: over the frame's budget",
                                                "shut: its room's group not loaded", "shut: its room's surfaces not read yet (one room a frame)",
                                                "shut: its room isn't seen through it from here", "shut: the walk didn't reach its room" };
            std::snprintf(line, sizeof(line), "  window %u: %s%s", n.ordinal, kWhy[n.why], rays);
            api->UiText(line);
        }
    }

    const char* StatusLine()
    {
        static char line[320];
        const char* state = !g_settings.enabled ? "off (SeeThroughWindows 0)"
                          : g_faulted ? "off after a fault"
                          : g_guards != 1 ? "off (the client differs)"
                          : (!g_hookedWalk || !g_hookedDraw || !g_hookedScene) ? "off (a hook is missing)" : "on";
        std::snprintf(line, sizeof(line), "%s; %s; last frame %u windows opened, %u glass batches deferred, %u redrawn; faults %u",
                      g_status, state, g_last.opened, g_last.deferred, g_last.redrawn, g_faults);
        return line;
    }
}
