// See-through windows, the rules apart from the client (features/SeeThrough is the runtime; tests/seethrough checks
// these offline; docs/r&d/see-through-windows.md is the research behind them).
//
// "Windows you can actually see through into a building's modelled interior" (the user and Simon, 2026-10-20). From
// outside, the client draws a WMO's interior groups only where its portal walk (CMapObj::RenderThruPortals) reaches
// them, through the doors: windows aren't portals, so the room behind a window is never drawn. features/SeeThrough opens
// a window of a building in its whitelist as a portal of its own, inside the client's walk and without touching the
// WMO's portal arrays: when the walk from outside reaches an exterior group, each of its windows that has an interior
// group behind it (InteriorBehind), faces the camera and lies on the screen is projected to a screen rectangle
// (ScreenRect), and the client's own walk runs on into that group through that rectangle, as through a doorway (the
// room drawn clipped to the window, its furniture with it, and the rooms beyond its own doors). The glass batch in front
// is then drawn after the room and its furniture, blended at a constant opacity (the deferral below), so the room shows
// through it. A batch covering a window with no room behind it (a "fake" window: the glass is painted on a shell), or
// one whose room couldn't be drawn this frame, stays as it was: opaque.
#pragma once

#include "GlassRooms.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wxl_livingazeroth::seethrough
{
    // --- the client (3.3.5a, build 12340; wowdecomp b1c71b8's CMapObj, CMapObjDef, CMapObjGroup, CWorldScene and
    //     CGxDeviceD3d, checked against the exe's bytes by kGuards) ------------------------------------------------------
    namespace client
    {
        // The portal walk (named hook point Wmo.RenderThroughPortals): __thiscall(root, group, fromGroup, CRect* ndc,
        // depth, interior), ret 14h. At depth 0 from outside (s_interiorPass 0) its group is an exterior one; it calls the
        // group render callback for the group, then walks the group's portals (MOPR refs [m_portalStart, + m_portalCount)
        // into MOPT/MOPV) into interior groups, each through FrustumPush, FrustumSet(s_frustumCorners, (clip + 1) / 2)
        // and itself at depth + 1, then FrustumPop.
        constexpr uintptr_t kRenderThruPortals = 0x007AC060;
        constexpr uintptr_t kFrustumPush = 0x00791950;      // __cdecl(void): frustumIndex++, a copy of the one below
        constexpr uintptr_t kFrustumSet = 0x00790E20;       // __cdecl(const C3Vector corners[8], const CRect* rect 0..1)
        constexpr uintptr_t kFrustumPop = 0x0078FB50;       // __cdecl(void): frustumIndex--
        constexpr uintptr_t kFrustumCorners = 0x00CDB108;   // CWorldScene::s_frustumCorners[8]
        constexpr uintptr_t kInteriorPass = 0x00CFBEC0;     // s_interiorPass: the walk from inside (the camera's building)
        constexpr uintptr_t kCullStateValid = 0x00CFBEBC;   // s_cullStateValid: SetupMapObjDefCull ran
        constexpr uintptr_t kCamPosLocal = 0x00D1C42C;      // s_camPosLocal: the camera in the walked WMO's own space
        constexpr uintptr_t kMapObjToWorld = 0x00ADFF10;    // s_mapObjToWorld: its placement (row vectors, row-major)
        constexpr uintptr_t kViewProj = 0x00ADFE90;         // s_viewProj: camera-relative world -> clip (row vectors)
        constexpr uintptr_t kActiveWorldView = 0x00CD8F5C;  // CWorldScene::s_activeWorldView: the origin of that
        constexpr uintptr_t kRenderCallback = 0x00D1BED8;   // CMapObj::gRenderCallback: __cdecl(group, param)
        constexpr uintptr_t kRenderParam = 0x00D1BEDC;      // CMapObj::gRenderUserParam: the placed instance (CMapObjDef)
        constexpr uintptr_t kAddToSortTable = 0x00799310;   // CWorldScene::AddMapObjDefGroupToSortTable: the callback
        constexpr uintptr_t kViewerMapObjDef = 0x00CD87A4;  // CWorldScene::s_viewerMapObjDef: the building the camera is in
        constexpr uintptr_t kM2Scene = 0x00CD754C;          // World::s_m2Scene: the world's model scene
        constexpr uintptr_t kPrimConversion = 0x00A2F8D4;   // Gx primitive -> D3DPRIMITIVETYPE, uint32[6]
        // CMapObj (a WMO root, the walk's `this`)
        constexpr size_t kRootPath = 0x1C;                  // its path, inline
        constexpr size_t kRootGroupInfo = 0x130;            // MOGI: SMOGroupInfo[ ], 0x20 each: flags, box min xyz, max xyz
        constexpr size_t kRootGroupCount = 0x16C;
        constexpr size_t kRootGroupsLoaded = 0x1E0;         // m_isGroupLoaded
        constexpr size_t kRootGroups = 0x1F8;               // CMapObjGroup* [512]
        constexpr uint32_t kMaxGroups = 512;
        constexpr size_t kGroupInfoStride = 0x20;
        // CMapObjGroup
        constexpr size_t kGroupFlags = 0x30, kGroupLoadedFlags = 0x198;   // loaded: bit 0 (GetGroup's test)
        constexpr size_t kGroupIndices = 0xE0, kGroupIndexCount = 0x154, kGroupVertices = 0xE8, kGroupVertexCount = 0x15C;
        // SMOBatch (MOBA)
        constexpr size_t kMobaStart = 0x0C, kMobaCount = 0x10;   // uint32 first index, uint16 index count
        // CMapObjDef (a placed instance); Wmo.RenderGroup gets its +0xB0 (m_invMat)
        constexpr size_t kDefInvMat = 0xB0, kDefOwner = 0xF4;
        constexpr size_t kDefGroups = 0x120, kDefGroupsCount = 0x130;    // TSCGrowableArray<CMapObjDefGroup*, 4>: count
        constexpr size_t kDefGroupsDynCount = 0x124, kDefGroupsDynData = 0x128;   // -1 at +0x130: the dynamic array's
        // CGxDeviceD3d (the Gx.DeviceDraw hook's `this`): its draw's arguments
        constexpr size_t kGxContext = 0xF58, kGxDisabled = 0xF5C, kGxBaseVertexMode = 0x224;
        constexpr size_t kGxVertexStream = 0x2870, kGxIndexBuffer = 0x28BC, kGxBufIndex = 0x18, kGxBufItemSize = 0x0C;
        // Gx render states (the client's cache: render/GxState.hpp)
        constexpr int kRsBlendingMode = 6, kRsDepthWrite = 15, kRsColorWrite = 16;
        constexpr uint32_t kPrimTriangles = 3, kD3dTriangleList = 4;
        // MOGP flags the walk from outside never enters (exterior 0x8, 0x10000)
        constexpr uint32_t kSkippedGroups = 0x10008, kExteriorGroup = 0x8;
    }

    /// Bytes of the client's code the runtime relies on (wowdecomp's offsets as the exe has them). Never a hooked
    /// function's first bytes (a detour may hold them): the instructions inside that use the fields and statics above.
    struct Guard
    {
        const char* what;
        uintptr_t   address;
        uint32_t    size;
        uint8_t     bytes[28];
    };
    inline constexpr Guard kGuards[] = {
        {"portal walk: reads s_interiorPass, sets s_curGroupIsInterior", 0x007AC0B4, 11,
         {0x39, 0x35, 0xC0, 0xBE, 0xCF, 0x00, 0xA3, 0xB8, 0xBE, 0xCF, 0x00}},
        {"portal walk: calls the group render callback with its parameter", 0x007AC0D9, 19,
         {0xA1, 0xD8, 0xBE, 0xD1, 0x00, 0x3B, 0xC6, 0x74, 0x0D, 0x8B, 0x0D, 0xDC, 0xBE, 0xD1, 0x00, 0x51, 0x53, 0xFF, 0xD0}},
        {"portal walk: the side test reads s_camPosLocal", 0x007AC231, 6, {0xD8, 0x0D, 0x30, 0xC4, 0xD1, 0x00}},
        {"portal walk: the side test reads s_camPosLocal.x", 0x007AC245, 6, {0xD8, 0x0D, 0x2C, 0xC4, 0xD1, 0x00}},
        {"portal walk: tests s_cullStateValid", 0x007AC3FB, 7, {0x83, 0x3D, 0xBC, 0xBE, 0xCF, 0x00, 0x00}},
        {"portal walk: FrustumPush, FrustumSet(s_frustumCorners, rect)", 0x007AC45D, 19,
         {0xE8, 0xEE, 0x54, 0xFE, 0xFF, 0x8D, 0x45, 0x98, 0x50, 0x68, 0x08, 0xB1, 0xCD, 0x00, 0xE8, 0xB0, 0x49, 0xFE, 0xFF}},
        {"portal walk: recurses, then FrustumPop", 0x007AC48D, 10, {0xE8, 0xCE, 0xFB, 0xFF, 0xFF, 0xE8, 0xB9, 0x36, 0xFE, 0xFF}},
        {"portal projection: s_mapObjToWorld", 0x007A8645, 5, {0x68, 0x10, 0xFF, 0xAD, 0x00}},
        {"portal projection: s_activeWorldView, s_viewProj", 0x007A873C, 11,
         {0xD8, 0x25, 0x5C, 0x8F, 0xCD, 0x00, 0x68, 0x90, 0xFE, 0xAD, 0x00}},
        {"FrustumPush: frustumIndex", 0x00791950, 13, {0xA1, 0x98, 0x87, 0xCD, 0x00, 0x83, 0xC0, 0x01, 0xA3, 0x98, 0x87, 0xCD, 0x00}},
        {"FrustumPop", 0x0078FB50, 8, {0x83, 0x2D, 0x98, 0x87, 0xCD, 0x00, 0x01, 0xC3}},
        {"FrustumSet(corners, rect): its frame", 0x00790E20, 9, {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xF8, 0x00, 0x00, 0x00}},
        {"sort-table callback: the instance's group array", 0x00799313, 16,
         {0x8B, 0x45, 0x0C, 0x83, 0xEC, 0x0C, 0x83, 0xB8, 0x30, 0x01, 0x00, 0x00, 0xFF, 0x75, 0x0E, 0x8B}},
        {"sort-table callback: the instance's owner and group number", 0x0079933A, 12,
         {0x8B, 0x80, 0xF4, 0x00, 0x00, 0x00, 0x56, 0x8B, 0x31, 0x8B, 0x56, 0x50}},
        {"GetGroup: loaded flag, group array, the group's loaded bit", 0x007AEA83, 28,
         {0x83, 0xB9, 0xE0, 0x01, 0x00, 0x00, 0x00, 0x74, 0x19, 0x8B, 0x45, 0x08, 0x8B, 0x84, 0x81, 0xF8, 0x01, 0x00, 0x00, 0xF6,
          0x80, 0x98, 0x01, 0x00, 0x00, 0x01, 0x75, 0x08}},
        {"GetGroupFlags: MOGI", 0x007AE7C2, 12, {0x8B, 0x45, 0x08, 0x8B, 0x89, 0x30, 0x01, 0x00, 0x00, 0xC1, 0xE0, 0x05}},
        {"RenderInterior: MOGI's count and stride", 0x007AD337, 11, {0x83, 0xC7, 0x20, 0x3B, 0x9E, 0x6C, 0x01, 0x00, 0x00, 0x72, 0x9E}},
        {"scene render: reads s_viewerMapObjDef", 0x0079AA21, 6, {0x8B, 0x0D, 0xA4, 0x87, 0xCD, 0x00}},
        {"world frame: World::s_m2Scene", 0x004F90EC, 6, {0x8B, 0x3D, 0x4C, 0x75, 0xCD, 0x00}},
        {"world frame: the model scene's pass 0", 0x004F9113, 15,
         {0x6A, 0x00, 0x8B, 0xCF, 0x89, 0x97, 0x44, 0x01, 0x00, 0x00, 0xE8, 0x8E, 0xAB, 0x32, 0x00}},
        {"device draw: context, disabled", 0x006A3626, 20,
         {0x83, 0xBE, 0x58, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x84, 0xDE, 0x00, 0x00, 0x00, 0x83, 0xBE, 0x5C, 0x0F, 0x00, 0x00, 0x00}},
        {"device draw: the base vertex from the stream", 0x006A3658, 22,
         {0x39, 0x9E, 0x24, 0x02, 0x00, 0x00, 0x75, 0x10, 0x8B, 0x8E, 0x70, 0x28, 0x00, 0x00, 0x8B, 0x41, 0x18, 0x33, 0xD2, 0xF7,
          0x71, 0x0C}},
        {"device draw: the first index from the index buffer", 0x006A369B, 17,
         {0x8B, 0x86, 0xBC, 0x28, 0x00, 0x00, 0x8B, 0x48, 0x18, 0x8B, 0x45, 0x0C, 0xD1, 0xE9, 0x03, 0x4F, 0x04}},
        {"device draw: the primitive table", 0x006A36BC, 7, {0x8B, 0x0C, 0x85, 0xD4, 0xF8, 0xA2, 0x00}},
        {"device draw: DrawIndexedPrimitive (vtable +0x148)", 0x006A36C8, 6, {0x8B, 0x88, 0x48, 0x01, 0x00, 0x00}},
        {"primitive table: triangles are a triangle list", 0x00A2F8E0, 4, {0x04, 0x00, 0x00, 0x00}},
        {"Gx state dirty: the render-state cache", 0x00685979, 8, {0x8B, 0xF9, 0x8B, 0x8F, 0xF4, 0x28, 0x00, 0x00}},
    };

    /// The first guard of `guards` whose bytes differ (read through `read(address, size)`, a pointer to that many bytes or
    /// null), or null when all match.
    template <class Read, size_t N> const Guard* FirstMismatch(const Guard (&guards)[N], Read read)
    {
        for (const Guard& g : guards)
        {
            const uint8_t* at = static_cast<const uint8_t*>(read(g.address, g.size));
            if (!at) return &g;
            for (uint32_t i = 0; i < g.size; ++i)
                if (at[i] != g.bytes[i]) return &g;
        }
        return nullptr;
    }

    // --- the buildings ----------------------------------------------------------------------------------------------
    /// The prototype's whitelist: the WMOs whose windows open (lower case, backslashes, as GlassWindows names them).
    /// Goldshire's Lion's Pride Inn: one exterior group (4) with all 17 of its windows in one glass batch, each with a
    /// room behind it (tools/see_through_survey.py).
    inline constexpr const char* kWhitelist[] = {
        "world\\wmo\\azeroth\\buildings\\goldshireinn\\goldshireinn.wmo",
    };
    /// Whether a WMO (its glass::PathHash) is in kWhitelist.
    bool Whitelisted(uint32_t pathHash);

    /// How far the camera may be from an exterior group's box (yd) for its windows to open.
    constexpr float kRange = 60.0f;
    /// At most this many windows opened a frame, and glass batches deferred a frame.
    constexpr unsigned kMaxOpenPerFrame = 48, kMaxDeferred = 24;
    /// The window's quad grown by this (yd) when it is projected: its frame's edge, so the room isn't cut a pixel short.
    constexpr float kQuadGrow = 0.1f;
    /// The camera must stand this far (yd) outside a window's plane for it to open.
    constexpr float kFacing = 0.05f;

    // --- the room behind a window ----------------------------------------------------------------------------------
    /// A group as the root's MOGI has it.
    struct GroupInfo
    {
        uint32_t flags = 0;
        float lo[3] = {}, hi[3] = {};
    };
    /// Probes behind the window's middle (yd), the first that finds a room wins, and how much an interior group's box
    /// is grown (tools/gen_glass.py's interior_room with a second, deeper probe for thick walls).
    constexpr float kProbes[2] = { 1.0f, 2.0f };
    constexpr float kBoxMargin = 0.25f;
    /// The interior group behind a window (its middle, its normal out of the building): of the groups the walk from outside
    /// may enter (not kSkippedGroups), the one with the smallest box holding a probe, or -1 (a fake window).
    int InteriorBehind(const float center[3], const float normal[3], const GroupInfo* groups, size_t count);

    /// One window of an exterior group (GlassWindows Flags 0x1), with the room behind it.
    struct SeeWindow
    {
        uint32_t ordinal = 0, group = 0, kind = 0;
        int interior = -1;                  // the interior group behind it, -1: none (fake)
        float center[3] = {}, normal[3] = { 1, 0, 0 }, along[3] = { 0, 1, 0 }, up[3] = { 0, 0, 1 };
        float halfWidth = 0.5f, halfHeight = 0.5f, thickness = 0.0f;
    };
    constexpr uint32_t kWindowExterior = 0x1;   // GlassWindows.Flags

    /// One WMO's exterior windows, by group then ordinal; byGroup[g] = (first, count) into windows.
    struct RootWindows
    {
        uint32_t path = 0;
        std::vector<SeeWindow> windows;
        std::vector<std::pair<uint32_t, uint32_t>> byGroup;
        unsigned real = 0, fake = 0;
        const SeeWindow* Group(uint32_t group, uint32_t& count) const;
    };
    /// A WMO's windows from the loaded table (glass::Windows().All()) and its root's MOGI (`groups`, `count`).
    RootWindows BuildRoot(uint32_t path, const std::vector<glass::Window>& all, const GroupInfo* groups, size_t count);

    // --- a window on the screen --------------------------------------------------------------------------------------
    /// The quad (WMO space), grown by `grow` yd along and up: corners counter-clockwise from (-along, -up).
    void Corners(const SeeWindow& w, float grow, float out[4][3]);
    /// A screen rectangle in normalised device coordinates (-1..1, y up), as the walk's CRect holds it.
    struct Rect { float minX = 0, minY = 0, maxX = 0, maxY = 0; };
    /// The screen rectangle of a polygon (WMO space): through `model` (WMO -> world), less `eye` (the camera-relative
    /// origin), through `viewProj` (-> clip; row vectors), clipped to the view (in front of the near plane: w > 0, and
    /// within -w..w on x and y), as TransformAndClipVerts and CalcScreenRectFromVerts do for a portal. False when none of
    /// it is on the screen.
    bool ScreenRect(const float (*corners)[3], int count, const float model[16], const float eye[3], const float viewProj[16], Rect& out);
    /// a & b, false when that is thinner than 0.001 either way (the walk's own test).
    bool Overlap(const Rect& a, const Rect& b, Rect& out);
    /// Whether the camera (WMO space) is outside the window's plane by kFacing.
    bool Facing(const SeeWindow& w, const float camLocal[3]);
    /// How far a point is from a box (0 inside).
    float DistanceToBox(const float p[3], const float lo[3], const float hi[3]);

    /// What becomes of each window of an exterior group this frame.
    enum WindowState : uint8_t
    {
        kUnseen = 0,   // facing away or off the screen: its glass hides nothing
        kOpened = 1,   // its room is drawn through it
        kShut = 2,     // on the screen and facing, but no room drawn behind it (fake, out of range, the budget, not loaded)
    };
    struct WindowPlan
    {
        WindowState state = kUnseen;
        Rect clip;         // kOpened candidates: the walk's rectangle for it (the window's & the walk's)
        bool candidate = false;   // facing, on the screen, in range, with a room: open it (budget allowing)
    };
    /// The plan for one exterior group's windows: the camera (WMO space), the group's box, the walk's rectangle, the
    /// matrices; `budget` windows may still open this frame (it goes down by each candidate). Candidates' states are
    /// kShut until the runtime has opened them (kOpened).
    void PlanWindows(const SeeWindow* windows, uint32_t count, const float camLocal[3], const float boxLo[3], const float boxHi[3],
                     const Rect& walk, const float model[16], const float eye[3], const float viewProj[16], unsigned& budget,
                     std::vector<WindowPlan>& out);
    /// The client's frustum rectangle for a walk rectangle: (clip + 1) / 2, as {minY, minX, maxY, maxX} (CRect's order).
    void FrustumRect(const Rect& clip, float out[4]);
    /// A walk rectangle from the client's CRect ({minY, minX, maxY, maxX}) and back.
    Rect FromCRect(const float c[4]);
    void ToCRect(const Rect& r, float out[4]);

    // --- which windows a glass batch is in ------------------------------------------------------------------------------
    /// The window (index into `windows`) whose lookup box holds a point: within kLookup yd of its rectangle and its
    /// thickness (at least kSlab) of its plane; the first by ordinal; -1 none.
    constexpr float kLookup = 0.05f, kSlab = 0.3f;
    int WindowOf(const float p[3], const SeeWindow* windows, uint32_t count);
    /// A glass batch's windows: each triangle's middle looked up (WindowOf); the strays (in no window) and their area.
    struct BatchWindows
    {
        std::vector<uint16_t> windows;   // indices into the group's windows, sorted, unique
        uint32_t triangles = 0, strays = 0;
        float strayArea = 0.0f;
        bool valid = false;              // read: the batch's triangles were all there
    };
    /// From the group's index and vertex arrays (MOVI, MOVT: three floats a vertex) and the batch's range.
    BatchWindows WindowsOfBatch(const uint16_t* indices, uint32_t indexCount, const float* vertices, uint32_t vertexCount,
                                uint32_t start, uint32_t count, const SeeWindow* windows, uint32_t windowCount);
    /// Strays up to this area (yd^2) don't stop a batch going see-through (trim at a frame's edge).
    constexpr float kTrimArea = 0.25f;
    /// Whether a batch may be drawn see-through this frame: all its triangles read and in windows (but kTrimArea), and
    /// none of its windows shut; at least one opened (else there's nothing to see through).
    bool Deferrable(const BatchWindows& b, const uint8_t* states, size_t stateCount);

    // --- the deferred glass ---------------------------------------------------------------------------------------------
    /// DrawIndexedPrimitive's arguments for a Gx batch, as the client's device draw (0x6A3620) works them out: the batch
    /// (CGxBatch: primitive, first index, index count, min and max vertex), the device's base-vertex mode (non-zero: 0),
    /// its vertex stream's offset and stride, its index buffer's offset (bytes), and the primitive table's entry.
    struct DrawArgs
    {
        uint32_t primitive = 0, baseVertex = 0, minIndex = 0, vertices = 0, startIndex = 0, primitives = 0;
    };
    bool DrawArgsOf(const uint32_t batch[4], uint32_t baseVertexMode, uint32_t streamOffset, uint32_t streamStride, uint32_t indexOffset,
                    uint32_t d3dPrimitive, DrawArgs& out);
    /// D3DRS_BLENDFACTOR for a glass opacity (0..1): its byte in all four channels.
    uint32_t BlendFactor(float opacity);

    /// The deferral's bookkeeping between the batch loop's cull test (BeginBatch), the device draw (OnDraw, Captured) and
    /// the model scene's first pass (Take), apart from the device (`state` is the runtime's captured device state).
    class Deferral
    {
    public:
        struct Entry
        {
            void* state = nullptr;     // the device's state at the batch's draw (an IDirect3DStateBlock9*)
            DrawArgs args;
            const void* target = nullptr;   // the render target and depth surface it drew into
            const void* depth = nullptr;
        };
        enum class Draw { Pass, Defer };
        /// A batch passed the loop's cull test: `deferrable` (Deferrable, a glass batch of an opened group) with its
        /// range (MOBA's first index and count), else a batch to draw as it is. Any earlier pending batch is dropped.
        void BeginBatch(bool deferrable, uint32_t start, uint32_t count);
        /// The batch loop ended (nothing pending past it).
        void EndLoop() { m_pending = false; }
        /// A device draw: defer it when it's the pending batch's (indexed triangles, the same range), the client draws it
        /// opaque or alpha-keyed (blending mode 0 or 1), and there's room; else draw it. Once per batch.
        Draw OnDraw(bool indexed, uint32_t primitive, uint32_t start, uint32_t count, uint32_t blendingMode);
        /// The deferred draw's state was captured (`entry`), or not (`ok` false: the runtime drew it after all).
        void Captured(const Entry& entry);
        void CaptureFailed() { ++m_failed; }
        /// Every entry, in order, for the redraw (and the list emptied).
        std::vector<Entry> Take();
        size_t Count() const { return m_entries.size(); }
        bool Pending() const { return m_pending; }
        unsigned Failed() const { return m_failed; }
        unsigned Passed() const { return m_passed; }   // pending batches drawn as they are (another blending, full, ...)
        void ResetCounts() { m_failed = m_passed = 0; }

    private:
        bool m_pending = false;
        uint32_t m_start = 0, m_count = 0;
        std::vector<Entry> m_entries;
        unsigned m_failed = 0, m_passed = 0;
    };
}
