// See-through windows, the rules apart from the client (features/SeeThrough is the runtime; tests/seethrough checks
// these offline; docs/r&d/see-through-windows.md is the research behind them).
//
// "Windows you can actually see through into a building's modelled interior" (the user and Simon, 2026-10-20). From
// outside, the client draws a WMO's interior groups only where its portal walk (CMapObj::RenderThruPortals) reaches
// them, through the doors: windows aren't portals, so the room behind a window is never drawn. features/SeeThrough opens
// a window of a building SeeThroughBuildings.cdbc names as a portal of its own, inside the client's walk and without touching the
// WMO's portal arrays: when the walk from outside reaches an exterior group, each of its windows that has an interior
// group behind it (InteriorBehind), faces the camera and lies on the screen is projected to a screen rectangle
// (ScreenRect), and the client's own walk runs on into that group through that rectangle, as through a doorway (the
// room drawn clipped to the window, its furniture with it, and the rooms beyond its own doors), where the room is really
// seen through the window (SeesRoom: not over its ceiling or through its walls). The glass batch in front is then drawn
// after the room and its furniture (the deferral below), its open windows' glass texels at the opacity and their lead,
// frame and tracery opaque, so the room shows through the glass. A window with no room behind it (a "fake" window: the
// glass is painted on a shell), or whose room couldn't be drawn this frame, stays as it was: opaque, its fake room in it.
// From inside (SeeThroughFromInside) the room's own windows become ways out of the client's walk from inside (one more
// exterior portal view each: ViewOf, PlanInside) where the street is really seen through them (LookOut), and the room's
// glass is deferred the same way.
#pragma once

#include "GlassRooms.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
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
        // CMapObjGroup (the group walker 0x7D7F50 fills them: MOVI, MOVT, MOBA and their counts, offsets/game/WMO.hpp)
        constexpr size_t kGroupFlags = 0x30, kGroupLoadedFlags = 0x198;   // loaded: bit 0 (GetGroup's test)
        constexpr size_t kGroupIndices = 0xE0, kGroupIndexCount = 0x154, kGroupVertices = 0xE8, kGroupVertexCount = 0x15C;
        constexpr size_t kGroupBatches = 0xF8, kGroupBatchCount = 0x16C;
        // SMOBatch (MOBA, 0x18 each)
        constexpr size_t kMobaStart = 0x0C, kMobaCount = 0x10;   // uint32 first index, uint16 index count
        constexpr size_t kMobaStride = 0x18, kMobaMaterial = 0x17;   // uint8 its material
        // the root's materials (the root walker 0x7D7470): MOTX and its size, MOMT (0x40 each: +0x0C its texture's MOTX
        // offset) and their count
        constexpr size_t kRootMotx = 0x124, kRootMotxSize = 0x164, kRootMaterials = 0x160, kRootMaterialCount = 0x19C;
        constexpr size_t kMomtStride = 0x40, kMomtTexture = 0x0C;
        constexpr uint32_t kMaxMaterials = 4096;
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
        // The interior batch loop (Wmo.IntRender 0x7AC9F0) draws a transition batch (its index under MOGP's
        // transBatchCount, the group object's +0x5C) twice over the same range: lit as the exterior batches with the Gx
        // blending mode 9 (GxBlend_SrcAlphaOpaque, x its vertex colour's alpha: 0x7ACCB0 .. 0x7ACD1D), then as the
        // interior ones with mode 7 (GxBlend_InvSrcAlphaAdd, + x 1 - that alpha: 0x7ACDF7 .. 0x7ACE62); every other batch
        // once, with its material's mode (0x7ACF6F .. 0x7ACFDE). Kharanos's dwarven house's room glass is one: "it can't
        // see outside even though it should be able to" (the user, 2026-10-23).
        constexpr uint32_t kBlendTransExterior = 9, kBlendTransInterior = 7;
        // MOGP flags the walk from outside never enters (exterior 0x8, 0x10000)
        constexpr uint32_t kSkippedGroups = 0x10008, kExteriorGroup = 0x8;
        // From inside (SeeThroughFromInside): the client's ways out of the camera's building. Its walk from inside
        // (s_interiorPass 1) records each portal into an exterior group (or one with 0x40, 0x100, 0x40000) as an exterior
        // portal view (CMapObj::AddExteriorPortalView 0x007A8F20): a CPortalView pushed to s_exteriorPortalViews and merged
        // into the world view (the sky's scissor, and with its depth >= 0 the sky and the weather) and, into an exterior
        // group, the frustum view (the rectangle the outdoors is culled through; its depth < 0: no outdoors at all).
        constexpr uintptr_t kPushExteriorPortalView = 0x00795D00;   // __cdecl(CPortalView*): s_exteriorPortalViews.Add
        constexpr uintptr_t kMergeIntoWorldRect = 0x00790AB0;       // __cdecl(CPortalView*): s_unk_ADF570.Merge
        constexpr uintptr_t kMergeIntoFrustumRect = 0x00790AD0;     // __cdecl(CPortalView*): frustumPortalView.Merge
        constexpr uintptr_t kExteriorPortalViews = 0x00CDD0E8;      // TSGrowableArray<CPortalView>: its count at +4
        constexpr uintptr_t kWorldView = 0x00ADF570;                // CWorldScene::s_unk_ADF570 (CPortalView)
        constexpr uintptr_t kFrustumView = 0x00ADF58C;              // CWorldScene::frustumPortalView (CPortalView)
        constexpr size_t kArrayCount = 0x4, kViewDepth = 0x10;      // a TSGrowableArray's count; a CPortalView's depth
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
        // Its call (0x4F911D) by the opcode alone: Modern Water points it at a thunk of its own (kOpaqueM2PassSite),
        // which calls the scene's draw (M2.SceneDraw, hooked here) and then composites its fog. With the call's
        // target in the guard, the feature switched itself off wherever Modern Water ran (2026-10-20).
        {"world frame: the model scene's pass 0", 0x004F9113, 11,
         {0x6A, 0x00, 0x8B, 0xCF, 0x89, 0x97, 0x44, 0x01, 0x00, 0x00, 0xE8}},
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
        // The room's surfaces for the line of sight through a window (RoomMesh): the group's and the root's chunks where
        // the walkers put them.
        {"group walker: MOVI at +0xE0, its index count (size / 2) at +0x154", 0x007D7F76, 17,
         {0x89, 0xB1, 0xE0, 0x00, 0x00, 0x00, 0x8B, 0x50, 0x04, 0xD1, 0xEA, 0x89, 0x91, 0x54, 0x01, 0x00, 0x00}},
        {"group walker: MOVT at +0xE8, its count (size / 12) at +0x15C", 0x007D7F94, 18,
         {0x89, 0xB1, 0xE8, 0x00, 0x00, 0x00, 0xF7, 0x67, 0x04, 0xC1, 0xEA, 0x03, 0x89, 0x91, 0x5C, 0x01, 0x00, 0x00}},
        {"group walker: MOBA at +0xF8, its count (size / 24) at +0x16C", 0x007D7FEC, 18,
         {0x89, 0xB1, 0xF8, 0x00, 0x00, 0x00, 0xF7, 0x67, 0x04, 0xC1, 0xEA, 0x04, 0x89, 0x91, 0x6C, 0x01, 0x00, 0x00}},
        {"root walker: MOTX at +0x124, its size at +0x164", 0x007D7499, 15,
         {0x89, 0xB7, 0x24, 0x01, 0x00, 0x00, 0x8B, 0x48, 0x04, 0x89, 0x8F, 0x64, 0x01, 0x00, 0x00}},
        {"root walker: MOMT at +0x160, its count (size / 64) at +0x19C", 0x007D74B0, 18,
         {0x89, 0xB7, 0x60, 0x01, 0x00, 0x00, 0x8B, 0x50, 0x04, 0xC1, 0xEA, 0x06, 0x89, 0x97, 0x9C, 0x01, 0x00, 0x00}},
        // From inside: the ways out (an exterior portal view per portal into an exterior group) and what reads them.
        {"portal walk from inside: an exterior portal view for a portal into a group with 0x50148", 0x007AC3C0, 28,
         {0xF7, 0xC7, 0x48, 0x01, 0x05, 0x00, 0x74, 0x53, 0x8B, 0x45, 0xE8, 0x8B, 0x4D, 0xFC, 0x81, 0xE7, 0x08, 0x00, 0x01, 0x00,
          0x57, 0x56, 0x53, 0x50, 0xE8, 0x43, 0xCB, 0xFF}},
        {"exterior portal view: its depth at +0x10, PushExteriorPortalView, MergeIntoWorldRect", 0x007A904C, 26,
         {0xE8, 0x7F, 0xE0, 0xFF, 0xFF, 0x8D, 0x4D, 0xCC, 0xD9, 0x5D, 0xDC, 0x51, 0xE8, 0xA3, 0xCC, 0xFE, 0xFF, 0x8D, 0x55, 0xCC,
          0x52, 0xE8, 0x4A, 0x7A, 0xFE, 0xFF}},
        {"PushExteriorPortalView: s_exteriorPortalViews.Add", 0x00795D06, 13,
         {0x50, 0x6A, 0x01, 0xB9, 0xE8, 0xD0, 0xCD, 0x00, 0xE8, 0xDD, 0xE3, 0xFF, 0xFF}},
        {"MergeIntoWorldRect: s_unk_ADF570.Merge", 0x00790AB6, 11, {0x50, 0xB9, 0x70, 0xF5, 0xAD, 0x00, 0xE8, 0xEF, 0xFA, 0xFF, 0xFF}},
        {"MergeIntoFrustumRect: frustumPortalView.Merge", 0x00790AD6, 11, {0x50, 0xB9, 0x8C, 0xF5, 0xAD, 0x00, 0xE8, 0xCF, 0xFA, 0xFF, 0xFF}},
        {"scene render: the frustum view's depth gates the outdoors", 0x0079AA33, 6, {0xD9, 0x05, 0x9C, 0xF5, 0xAD, 0x00}},
        {"scene render: the outdoors culled through the frustum view's rectangle", 0x0079AA4B, 16,
         {0x68, 0x8C, 0xF5, 0xAD, 0x00, 0xD9, 0x1D, 0x80, 0x87, 0xCD, 0x00, 0xE8, 0x35, 0xFD, 0xFF, 0xFF}},
        {"scene render: the world view's depth gates the sky; the exterior views' count", 0x0079AC41, 19,
         {0xD8, 0x1D, 0x80, 0xF5, 0xAD, 0x00, 0xDF, 0xE0, 0xF6, 0xC4, 0x41, 0x74, 0x76, 0x39, 0x1D, 0xEC, 0xD0, 0xCD, 0x00}},
        // Between rooms (2026-10-23, Phase C): the walk keeps its interior argument (cleared for a group with 0x48) in a
        // global the interior batch loop reads when it draws (put back after a window's walk into a room), and a group's
        // name (the panel's) where GetGroupName reads it.
        {"portal walk: its interior argument into s_curInteriorWalk (0xCFBEB8)", 0x007AC0A3, 28,
         {0x8B, 0x45, 0x18, 0x3B, 0xC6, 0x74, 0x0A, 0xF6, 0xC1, 0x48, 0x74, 0x05, 0x33, 0xC0, 0x89, 0x45, 0x18, 0x39, 0x35, 0xC0,
          0xBE, 0xCF, 0x00, 0xA3, 0xB8, 0xBE, 0xCF, 0x00}},
        {"interior batch loop: its lighting by s_curInteriorWalk", 0x007ACA65, 5, {0xA1, 0xB8, 0xBE, 0xCF, 0x00}},
        {"GetGroupName: a loaded group's name at +0xD8", 0x007AEAEF, 22,
         {0x8B, 0x84, 0x81, 0xF8, 0x01, 0x00, 0x00, 0xF6, 0x80, 0x98, 0x01, 0x00, 0x00, 0x01, 0x74, 0x0A, 0x8B, 0x80, 0xD8, 0x00,
          0x00, 0x00}},
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
    /// The buildings whose windows open: SeeThroughBuildings.cdbc's rows (features/Glass loads it with the glass tables;
    /// tools/see_through_survey.py --table writes it from its SEE_THROUGH list, the WMOs the survey and the gate's sweeps
    /// pick: 2026-10-22, "more buildings"), by WMO path, each with its Flags: from outside (the room behind its windows),
    /// from inside (the street through them). Before 2026-10-22 one hard-coded entry: Goldshire's Lion's Pride Inn.
    constexpr uint32_t kSeeFromOutside = 0x1, kSeeFromInside = 0x2;
    /// Between rooms (2026-10-23, Phase C: "windows between two interiors: Ironforge first, the whole city is interior
    /// groups; shop windows face indoor streets"): a window of an interior group that looks into another interior group (a
    /// shop's from the street, the street's from the shop) opens onto it, as a window onto a room from outside does. The
    /// table's rows may name a group (its Group column, -1 or missing: the whole WMO), so a city takes it street by street.
    constexpr uint32_t kSeeBetween = 0x4;

    /// How far the camera may be from an exterior group's box, and from the window's middle (yd), for a window to open
    /// (the window's own distance since 2026-10-22: a city's one exterior group can hold the camera in its box).
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
        float outer = 0.0f;                 // a room's own window (from inside): how deep its opening is, to the building's
                                            // outer glass behind it (OuterBehind; 0: none)
        bool unlisted = false;              // from outside: its room isn't one the table opens from outside (ListOutside)
    };
    constexpr uint32_t kWindowExterior = 0x1;   // GlassWindows.Flags
    // "Northshire Abbey framerate looks awesome but it does flicker between fake rooms and outside" (the user, 2026-10-22,
    // in the nave). The abbey's nave windows stand 2.5 to 2.7 yd inside its outer windows: from the nave at a slant a
    // window's rays leave its glass and meet the outer wall beside the outer window from behind (the embrasure between
    // them isn't modelled), so they counted against it and each window opened only within some 25 degrees of its axis:
    // walking about the nave the windows came and went one after another. A room's own window with the building's outer
    // glass behind it (an exterior window facing the same way, within kOuterReach yd behind it, their rectangles
    // overlapping: OuterBehind) has an opening that deep, and from inside a ray that meets the building's shell from
    // behind before the outer glass's plane (and kLiningMargin past it: the wall round a recessed outer pane), facing
    // along the window or along its width, is in that opening (its lining, culled as the client draws it from there):
    // passed, as kReveal passes a room's from outside; and past that, the building's own outside met from the front (another
    // wing's face: the abbey's library windows saw it with four rays of eight) is the street seen out of it. A window with
    // none (one onto a blind wall) keeps the old rule.
    constexpr float kOuterFacing = 0.9f, kOuterReach = 6.0f, kLiningMargin = 1.0f;
    /// The depth to the building's outer glass behind a room's own window (`w`, its group an interior one): the nearest
    /// exterior window of the same WMO in `all` facing within kOuterFacing of it, its middle 0 .. kOuterReach yd behind
    /// it (along its normal, out of the building), their rectangles overlapping; 0: none.
    float OuterBehind(const SeeWindow& w, uint32_t path, const std::vector<glass::Window>& all);

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
    // Dalaran (2026-10-24, Phase D): one WMO for the city, 3,949 windows in its exterior groups, 1,091 of them with a room
    // behind, and most of those rooms are never seen through their glass (one large volume, or behind thick walls). A whole
    // WMO's row from outside would open every one of them where the gate lets it, flickering ones too. So the table's group
    // rows say which rooms open from outside: a window opens from outside only when its own exterior group's flags (the
    // WMO's rows and its group's) or its room's have kSeeFromOutside. A building listed whole has it in every group, so it
    // stays as it was.
    /// Marks each window of `outside` (BuildRoot's) whose group's and room's flags (`groupFlags`: each group's, the WMO's
    /// rows' and its own: glass::SeeThroughGroupFlags) both lack kSeeFromOutside as unlisted, the rest not. How many are
    /// listed (a fake window counts as listed when its group is).
    unsigned ListOutside(RootWindows& outside, const std::vector<uint32_t>& groupFlags);

    // --- the room seen through a window ---------------------------------------------------------------------------------
    // "certain attic windows shouldn't be able to see through the roof" (the user, 2026-10-20, the Lion's Pride Inn at
    // night). The room behind a window is the interior group whose box holds a point behind its middle; a box says
    // nothing of where the room's walls and ceiling are. A dormer's line of sight from the street goes over the room's
    // ceiling or meets it from above, where the client culls its faces (they face into the room): opened, the room showed
    // through the roof. A window opens only where the line of sight through it meets the room's own surfaces from inside
    // first: its middle ray and most of kSight's rays (kSightFront of 9) from the eye through the window meet a face of the
    // room facing them (its winding's front: the walls', the ceiling's and the floor's inner side). The rest may meet the
    // wall round a smaller or arched opening from outside, or leave the room through a doorway: with every one of the nine
    // required, the inn's west upstairs window (its arch's corner on the wall, two rays through the stair's opening) never
    // opened ("see-through windows don't seem to work anymore now", the user, 2026-10-21).
    /// One room group's surfaces, apart from the client (the runtime copies them out of the group): its rendered triangles
    /// but its glass (a window's inner face is the same window, its back to the street), in a grid of cells (WMO space).
    class RoomMesh
    {
    public:
        enum Hit : uint8_t { kNone = 0, kFront = 1, kBack = 2 };
        /// From the group's vertices (MOVT: three floats a vertex), its indices (MOVI) and the index ranges to take (its
        /// batches but the glass ones: first index, count). False (and empty) when an index or a range is out of bounds.
        bool Build(const float* vertices, uint32_t vertexCount, const uint16_t* indices, uint32_t indexCount,
                   const std::vector<std::pair<uint32_t, uint32_t>>& ranges);
        /// The same with a tag for each range's triangles (`tags`, one a range: kGlassTag for a glass batch; missing: 0).
        bool Build(const float* vertices, uint32_t vertexCount, const uint16_t* indices, uint32_t indexCount,
                   const std::vector<std::pair<uint32_t, uint32_t>>& ranges, const std::vector<uint8_t>& tags);
        /// The first triangle along o + t d (t in (tmin, tmax)): kFront when it faces the ray's origin (its winding's front:
        /// cross(b - a, c - a) . d < 0), kBack when it faces away, kNone. Its t in `t`, its tag in `tag` and its unit normal
        /// (toward its front) in `normal` when one is met.
        Hit First(const float o[3], const float d[3], float tmin, float tmax, float* t = nullptr, uint8_t* tag = nullptr,
                  float* normal = nullptr) const;
        /// The same over every triangle (the tests' reference).
        Hit FirstBrute(const float o[3], const float d[3], float tmin, float tmax, float* t = nullptr) const;
        size_t Triangles() const { return m_tris.size() / 9; }
        bool Empty() const { return m_tris.empty(); }

    private:
        std::vector<float> m_tris;                // a, b, c: nine floats a triangle
        std::vector<uint8_t> m_tags;              // each triangle's tag (Build's)
        std::vector<uint32_t> m_cellStart, m_refs;   // cell i's triangles: m_refs[m_cellStart[i] .. m_cellStart[i + 1])
        float m_lo[3] = {}, m_cell = 1.0f;
        int m_n[3] = {};
        bool TestCell(int cell, const float o[3], const float d[3], float tmin, float& best, int& bestTri) const;
    };
    /// The grid's cells: about kGridCells along the room's longest side, none smaller than kGridLeast yd.
    constexpr int kGridCells = 32;
    constexpr float kGridLeast = 0.5f;
    /// Where the rays cross a window (its half sizes' shares along and up): its middle and a ring round it, inside the
    /// frame (an outer window's glass reaches past the room's opening behind it; its edge is mostly lead and frame).
    constexpr float kSight[9][2] = { { 0, 0 }, { -0.5f, -0.5f }, { 0, -0.5f }, { 0.5f, -0.5f }, { -0.5f, 0 },
                                     { 0.5f, 0 }, { -0.5f, 0.5f }, { 0, 0.5f }, { 0.5f, 0.5f } };
    /// How far past the glass (yd) a ray looks for the room.
    constexpr float kSightReach = 120.0f;
    /// The rays that must meet the room from inside: the middle one and this many of the nine in all. Over the inn's 17
    /// windows from 30 street views each, this agrees best with how much of each window's glass (by its texture's mask)
    /// shows the room from inside: no view where 60% of it does is shut; the dormers over the ceiling never open.
    constexpr int kSightFront = 6;
    /// What the rays through a window met (the panel's: why a window is shut).
    struct Sight
    {
        int front = 0, back = 0, none = 0;
        RoomMesh::Hit middle = RoomMesh::kNone;
        int firstMiss = -1;                       // the first ray (kSight's index) that didn't meet a front face
        RoomMesh::Hit missHit = RoomMesh::kNone;  // ... what it met (kBack) or nothing
        float missDepth = 0.0f;                   // ... how far past the glass (yd)
        int rays = 0;
        bool onGlass = false;                     // through its glass's own points (SightPoints), not its rectangle's
    };
    /// Whether the room is seen through the window from `eye` (WMO space), as before 2026-10-21: the middle kSight ray and
    /// kSightFront of them in all, from the glass on, meet `room`'s surfaces first from inside (kFront). What they met in
    /// `out`.
    bool SeesRoom(const SeeWindow& w, const float eye[3], const RoomMesh& room, Sight* out = nullptr);

    // "Ok interestingly it works at some angles and goes back to the fake room at others lol" (the user, 2026-10-21, before
    // the inn's west front: its arched window 9 open, then a step aside shut, "4 of 9 rays meet ..."). Its rectangle's
    // corner rays met the wall round the arch from outside, and rays through the room on out by the stair's opening counted
    // against it. So: the rays go through points on the window's own glass; only a ray meeting the room's surface from
    // outside (a wall or the ceiling from outside: the dormer over the ceiling) counts against it, one meeting nothing of
    // the room (on through a doorway, the stair's opening, past it) for neither, the portal walk draws what's there; and the
    // verdict holds: opening and shutting have their own thresholds, and a change stands only after kGateHold seconds.
    /// Where the rays cross a window: points on its own glass (inside its glass triangles, the group's glass batches),
    /// the one nearest its middle first, then each the farthest from those before; none (n 0): kSight's on its rectangle.
    constexpr int kSightPoints = 9;
    constexpr float kSightInner = 0.6f;   // the points within this share of the half sizes (the glass's edge: its frame)
    struct SightPoints { float p[kSightPoints][3] = {}; int n = 0; };
    /// Each of the group's windows' (`windows`) sight points from its vertices (MOVT), indices (MOVI) and glass batches'
    /// ranges (first index, count): a triangle belongs to the window its middle is in (WindowOf); it's cut in sixteen and
    /// the middles of the pieces are the candidates (inside the glass, off its edges).
    void GlassSightPoints(const float* vertices, uint32_t vertexCount, const uint16_t* indices, uint32_t indexCount,
                          const std::vector<std::pair<uint32_t, uint32_t>>& glassRanges, const SeeWindow* windows, uint32_t count,
                          std::vector<SightPoints>& out);
    /// The rays from `eye` through the window's sight points (`points` with n > 0, else kSight's) past the glass: what
    /// each met first in `out` (front: the room from inside; back: its surface from outside; none: nothing of it).
    void LookThrough(const SeeWindow& w, const float eye[3], const RoomMesh& room, const SightPoints* points, Sight& out);
    /// The verdict on what the rays met, `wasOpen` its last. To open: at least kOpenFront rays see the room from inside,
    /// more than meet it from outside (at most kOpenBack of those) and more than meet nothing. To stay open: kStayFront,
    /// more than from outside (at most kStayBack), and at most kStayNone fewer than meet nothing. Over the inn's 17 windows
    /// from 252 street views each (5 .. 30 yd, 60 degrees aside, the eye 2 .. 11 yd up): the dormers over the ceiling
    /// meet nothing with most rays (they go over the room) and see it with at most four; the arched window 9 sees it with
    /// at least three, and opens from every view (it was 230 of 252). Since 2026-10-22 (LookIn: the rays followed on through
    /// the building) a majority, five, must see in to open: a dormer seen from high and far met its room from inside with
    /// four, the void with two and its cavity's own walls with three, and opened; no other view opens with four.
    constexpr int kOpenFront = 5, kStayFront = 3, kOpenBack = 2, kStayBack = 3, kStayNone = 2;
    bool SightOpen(const Sight& s, bool wasOpen);
    /// A window's held verdict across frames: a new one stands once it has held kGateHold seconds.
    constexpr float kGateHold = 0.35f;
    struct Gate
    {
        bool open = false, seen = false;   // the verdict that stands; whether there is one yet
        bool pending = false;              // the other verdict, held for `held` seconds so far
        float held = 0.0f;
    };
    /// The frame's verdict `want` after `dt` seconds: the first stands at once, a change after kGateHold. The verdict that
    /// stands.
    bool Hold(Gate& g, bool want, float dt);

    // "dormers still switch between the fake rooms and the real ones" (the user, 2026-10-22, mounted before the inn's
    // dormer front, the camera 14.5 yd up: 15 at its threshold, 16 and 17 open with four of nine rays meeting nothing of the
    // room). A dormer's rays that meet nothing of its room go on into the attic and out where the roof's inside isn't
    // modelled (the shell is one-sided: from inside the client culls it, so the void or the sky shows through), while
    // window 9's went on through the stair's opening into the next room, which the walk draws. So a ray that meets nothing
    // of the room is followed on through every group of the building, as the client draws them: faces one-sided, glass
    // see-through from behind.
    /// A group's surfaces for the line of sight: its mesh (every rendered batch, its glass tagged kGlassTag) and whether
    /// it's an exterior group (the building's shell) or an interior one (a room).
    struct SightMesh { const RoomMesh* mesh = nullptr; bool exterior = false; };
    constexpr uint8_t kGlassTag = 1;
    /// How far past a window a ray from outside looks for anything at all (yd): meeting nothing, it shows the void.
    constexpr float kVoidReach = 60.0f;
    /// Between rooms the room beyond may be a hall far larger than that (2026-10-24, "Ironforge windows now seem to work from
    /// outside but not inside": from the herbalist's on the Great Forge's ring the rays cross the forge and meet its far
    /// side 66 .. 180 yd out, past kVoidReach, so they counted as the void): a window's rays look as far as the room's box
    /// reaches from the eye (RoomReach), at most kRoomReach.
    constexpr float kRoomReach = 300.0f;
    /// From outside, the rays from `eye` through the window's sight points (`points` with n > 0, else kSight's) past its
    /// own outer pane, through the building (`groups`: every group's mesh), what each meets first that the client draws
    /// from that side: a pane seen from behind is see-through and passed (the window's own inner pane, a dormer's sloped one
    /// from above), so is another outer pane from inside. Front (seeing in): a room's surface from inside, its own or the
    /// next room's through a doorway or a stair's opening, or a room's pane from inside. Back (against): any surface from
    /// behind, a room's from outside or the shell's from inside (the roof, an outer wall: the void shows through it), or
    /// nothing at all within kVoidReach. None: the building's outside from outside (out by an opening).
    void LookIn(const SeeWindow& w, const float eye[3], const SightMesh* groups, size_t count, const SightPoints* points, Sight& out,
                float reach = kVoidReach);
    /// Between rooms: how far LookIn's rays look for the room beyond -- its box's farthest corner from `eye`, grown by
    /// kNearMargin; at least kVoidReach, at most kRoomReach.
    float RoomReach(const float eye[3], const GroupInfo& room);
    // Stained glass (2026-10-22: the Redridge chapel, Northshire Abbey, Northrend's town hall and barracks): their walls are
    // a yard and more thick, the room's opening behind the glass smaller than the glass, so a ray through the glass near its
    // edge met the inner face of the wall round that opening from behind (the chapel's tall windows: a third of their rays)
    // and counted against the room; so did the sides of a narrow window's deep opening (Northrend's barracks: its upper
    // windows a foot wide, their rays at a slant). The client culls those faces and draws the room past them; so a room's
    // surface from behind within kReveal yd of the window's outer pane, standing in the window's own wall or as a side of
    // its opening (facing along its normal or along it, within kRevealFacing), is passed. A dormer's ceiling from above
    // faces up the window, as a sill or a lintel does: still against.
    constexpr float kReveal = 2.0f;
    constexpr float kRevealFacing = 0.7f;
    /// The groups a window's rays go through (2026-10-22, more buildings: a city's one WMO has hundreds): those whose box
    /// meets the box round its room (`room`: the interior group behind it from outside, its own group from inside; -1
    /// none) and its middle, grown by kNearMargin yd. A ray that leaves them all leaves the building round the room.
    constexpr float kNearMargin = 6.0f;
    void NearGroups(const SeeWindow& w, int room, const GroupInfo* groups, size_t count, std::vector<uint16_t>& out);

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
        kShut = 2,     // on the screen and facing, but no room drawn behind it (fake, out of range, the budget, not loaded,
                       // its room not seen through it: SeesRoom)
    };
    struct WindowPlan
    {
        WindowState state = kUnseen;
        Rect clip;         // kOpened candidates: the walk's rectangle for it (the window's & the walk's)
        bool candidate = false;   // facing, on the screen, in range, with a room: open it (budget allowing)
        Rect screen;       // from inside (PlanInside): the window's own screen rectangle, and its farthest depth
        float depth = 0.0f;
    };
    /// The plan for one exterior group's windows: the camera (WMO space), the group's box, the walk's rectangle, the
    /// matrices; `budget` windows may still open this frame (it goes down by each candidate), the nearest first (since
    /// 2026-10-24: a city's group has hundreds of windows, and in their order the nearest could find it spent). A window
    /// that isn't listed (ListOutside) takes none of it and is left kUnseen, not even projected (Dalaran's 3,826 cost about
    /// a millisecond a frame projected), so the whole-glass redraw mustn't draw a batch with one see-through (BatchUnlisted).
    /// Candidates' states are kShut until the runtime has opened them (kOpened).
    void PlanWindows(const SeeWindow* windows, uint32_t count, const float camLocal[3], const float boxLo[3], const float boxHi[3],
                     const Rect& walk, const float model[16], const float eye[3], const float viewProj[16], unsigned& budget,
                     std::vector<WindowPlan>& out);
    /// The client's frustum rectangle for a walk rectangle: (clip + 1) / 2, as {minY, minX, maxY, maxX} (CRect's order).
    void FrustumRect(const Rect& clip, float out[4]);
    /// A walk rectangle from the client's CRect ({minY, minX, maxY, maxX}) and back.
    Rect FromCRect(const float c[4]);
    void ToCRect(const Rect& r, float out[4]);

    // --- from inside: the street through the room's own windows ---------------------------------------------------------
    // "windows should be see-through from the inside as well" (the user, 2026-10-21, standing in the inn's ground floor).
    // From inside, the client draws the outdoors only through its ways out: its walk from the camera's groups records each
    // portal into an exterior group as an exterior portal view (CMapObj::AddExteriorPortalView: the portal's screen
    // rectangle and its farthest depth), and the outdoors (the terrain, other buildings, the models, the liquids, the
    // horizon) is culled through the rectangle round them, the sky drawn in it, as through an open door; with none, no
    // outdoors at all. Windows aren't portals, so from inside the street never shows behind the glass. features/SeeThrough
    // adds each window of a room the walk reaches (GlassWindows' rows of interior groups: the room's own glass) that faces
    // the camera from inside, lies on the screen within the walk's rectangle and is in range as an exterior portal view of
    // its own, through the client's own PushExteriorPortalView and merges, where the street is really seen through it
    // (LookOut), and draws its glass again over what's behind it, as from outside.
    /// A WMO's interior groups' windows (GlassWindows rows without Flags 0x1: a room's own glass), by group then ordinal;
    /// no room behind them (interior -1).
    RootWindows BuildInside(uint32_t path, const std::vector<glass::Window>& all, size_t groupCount);
    /// The same with the root's MOGI (`groups`, `count`): a window of a group `between` flags (kSeeBetween) gets the room
    /// beyond it as its `interior` -- its glazed partner's group (GlazedPartner), else RoomBeyond's; -1: none, a fake --
    /// counted real or fake; the other groups' windows as above.
    RootWindows BuildInside(uint32_t path, const std::vector<glass::Window>& all, const GroupInfo* groups, size_t count,
                            const std::vector<uint8_t>& between);
    /// The interior group beyond an interior group's window (its normal points out of its own group): the smallest box (not
    /// its own group's, not an exterior or skipped one) holding a point kProbes past its middle along its normal; -1: none.
    int RoomBeyond(const SeeWindow& w, const GroupInfo* groups, size_t count);
    /// A window glazed on both sides (2026-10-23, Ironforge): the group of the pane on its other side -- an interior window
    /// of another group (not skipped) facing back at it (normals opposite within kPartnerFacing), its middle within
    /// kPartnerReach and not behind it -- or -1. Between rooms that group is the room beyond the window: the boxes of a
    /// city's halls overlap, and the smallest holding the point past the glass was the wrong one for 16 of Ironforge's
    /// windows (the herbalist's on the Great Forge's ring onto the Forlorn Cavern's box, not the ring).
    constexpr float kPartnerFacing = 0.9f, kPartnerReach = 1.2f, kPartnerBehind = 0.05f;
    int GlazedPartner(const SeeWindow& w, uint32_t path, const std::vector<glass::Window>& all, const GroupInfo* groups, size_t count);
    /// Whether the camera (WMO space) is inside a window's plane (its normal points out of the building) by kFacing.
    bool FacingIn(const SeeWindow& w, const float camLocal[3]);
    /// How far (yd) the camera may be from a window's middle for the street to open through it, and how many a frame.
    constexpr float kInsideRange = 40.0f;
    constexpr unsigned kMaxInsidePerFrame = 16;
    /// The client's CPortalView (0x1C; Win32): its screen rectangle (0..1, CRect's order {minY, minX, maxY, maxX}), its
    /// farthest depth along the view (the outdoor cull's occluders start 33 1/3 yd past it), no vertices (a null pointer).
    struct PortalView
    {
        float rect[4] = {};
        float maxViewDepth = 0.0f;
        uint32_t verts = 0, vertCount = 0;
    };
    static_assert(sizeof(PortalView) == 0x1C, "the client's CPortalView");
    /// The view AddExteriorPortalView makes of a portal, for a window: its screen rectangle (NDC) as (x + 1) / 2 in
    /// CRect's order, its depth (at least 0, as CalcPortalFarthestDistance's).
    PortalView ViewOf(const Rect& screen, float depth);
    /// The farthest of a polygon's (WMO space) depths along the view: its clip w through `model`, less `eye`, `viewProj`
    /// (CalcPortalFarthestDistance's distance from the camera's plane); false when none is finite.
    bool FarthestDepth(const float (*corners)[3], int count, const float model[16], const float eye[3], const float viewProj[16], float& out);
    /// One interior group's windows from inside: facing in (FacingIn), on the screen and within the walk's rectangle
    /// (`walk`): kShut, with its own screen rectangle and depth; within kInsideRange of the camera with `budget` left (it
    /// goes down by each), the nearest first: a candidate. `fakesFree` (a group between rooms without ways out): a window
    /// with no room beyond it takes none of the budget (2026-10-23: the Great Forge's ring has 73 windows, 33 of them onto
    /// nothing, and in the windows' order the nearest shop's could find the budget spent).
    void PlanInside(const SeeWindow* windows, uint32_t count, const float camLocal[3], const Rect& walk, const float model[16],
                    const float eye[3], const float viewProj[16], unsigned& budget, std::vector<WindowPlan>& out, bool fakesFree = false);
    /// The rays from `eye` through the window's sight points (`points` with n > 0, else kSight's) on past its own pane,
    /// through the building (`groups`: every group's mesh, as LookIn), and what each meets first that the client draws from
    /// that side (a room's pane from behind is see-through). Front: it leaves by the building's own glass (an exterior
    /// group's pane either way), or meets nothing (out in the open): the street is seen. Back: it meets a surface from
    /// behind, a wall or the roof from inside (culled, so the street would show through them: a dormer's sloped inner pane
    /// seen from below). None: it meets a surface from the front (drawn as it is: the window's reveal behind its glass, the
    /// building's outside past an opening, another room). SightOpen and Hold decide as from outside: to open four see the
    /// street and more than meet a surface either way, at most two from behind. With the building's outer glass behind it
    /// (`w.outer`), the shell met from behind within that opening (its lining) is passed.
    void LookOut(const SeeWindow& w, const float eye[3], const SightMesh* groups, size_t count, const SightPoints* points, Sight& out);

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
        std::vector<uint16_t> indices;   // its triangles' vertex indices (MOVI's, the batch's range)
        std::vector<int16_t> window;     // each triangle's window (an index into the group's windows), -1 a stray
    };
    /// From the group's index and vertex arrays (MOVI, MOVT: three floats a vertex) and the batch's range.
    BatchWindows WindowsOfBatch(const uint16_t* indices, uint32_t indexCount, const float* vertices, uint32_t vertexCount,
                                uint32_t start, uint32_t count, const SeeWindow* windows, uint32_t windowCount);
    /// Strays up to this area (yd^2) don't stop a batch going see-through (trim at a frame's edge).
    constexpr float kTrimArea = 0.25f;
    /// Whether a batch may be drawn see-through this frame: all its triangles read and in windows (but kTrimArea), at least
    /// one of its windows opened (else there's nothing to see through). Its shut and unseen windows stay as the client
    /// draws them, opaque with their fake room: the redraw tells the windows apart per pixel (OpenBits, GlassShader's b11).
    bool Deferrable(const BatchWindows& b, const uint8_t* states, size_t stateCount);
    /// The same for the whole-glass redraw (SeeThroughRedraw 0, as on 2026-10-20): every window it covers open or unseen too
    /// (its glass goes see-through as one, so a shut window would show the void behind it).
    bool DeferrableWhole(const BatchWindows& b, const uint8_t* states, size_t stateCount);
    /// Whether any of a batch's windows (`windows`: the group's, `count` of them) isn't listed from outside (ListOutside):
    /// such a batch is never drawn whole (its unlisted windows are left unseen by PlanWindows, not shut).
    bool BatchUnlisted(const BatchWindows& b, const SeeWindow* windows, size_t count);
    /// Frosted glass (2026-10-23: "if I set the blur to exactly 0 it doesn't work, any value above 0 does"): its windows open
    /// with SeeThroughFrosted on (`frosted`) and the per-pixel redraw; with the blur (SeeThroughFrostBlur) above 0 over the
    /// frame's blurred copy (`copies`), unless the blur can't run here (then shut, as before), at 0 as through clear glass (no
    /// copy). Off: shut, the client's fake rooms as before.
    struct FrostPlan { bool opens = false, copies = false; };
    FrostPlan PlanFrost(bool frosted, bool perPixel, float blur, bool blurFailed);
    /// The per-pixel redraw split by window (SeeThroughDraw): its open windows' triangles (`open`, kOpened) and the rest
    /// (shut, unseen and strays), as vertex indices (the batch's own, MOVI's), so the open ones are drawn see-through
    /// whatever the shader's window lookup finds: in game it found none (every pixel the debug view's yellow, 2026-10-21).
    /// Both empty when `b` isn't valid.
    void SplitByWindow(const BatchWindows& b, const uint8_t* states, size_t stateCount, std::vector<uint16_t>& open,
                       std::vector<uint16_t>& rest);
    /// The windows c219.w holds (GlassShader's kReplayBits): 16, so the shader's test of a bit keeps a margin a GPU's
    /// approximate exp can't cross (OpenBit's).
    constexpr uint32_t kOpenBits = 16;
    /// A deferred batch's open windows for the redraw (c219.zw): a first ordinal (`windows`: the group's, `states` theirs)
    /// and a bit for each open window within kOpenBits of it (its ordinal - first). An open window past them is redrawn
    /// opaque, as a shut one. The first is the lowest open ordinal that takes in the most open windows; with the camera
    /// (`camLocal`, the WMO's space), the most of those that take in the open window nearest it (2026-10-24, Dalaran: a
    /// glass batch of the city's covers hundreds of windows, 748 in one, their ordinals far apart, and from the lowest
    /// open one the window in front of the camera could be past the sixteen). Ties: the lowest.
    void OpenBits(const BatchWindows& b, const SeeWindow* windows, const uint8_t* states, size_t count, float& first, uint32_t& bits,
                  const float* camLocal = nullptr);

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
    /// D3DRS_BLENDFACTOR for a glass opacity (0..1): its byte in all four channels (the whole-glass redraw's).
    uint32_t BlendFactor(float opacity);
    /// The redraw's c219 (GlassShader's kReplayConstant) for a glass opacity (0..1; not finite: opaque), the batch's own
    /// alpha test as the device had it (D3DRS_ALPHATESTENABLE, ALPHAFUNC, ALPHAREF: greater-or-equal its reference / 255,
    /// greater half a step more; off, or any other test, -1: none) and its open windows (OpenBits: c219.z the first
    /// ordinal, c219.w the bits + 0.5).
    void ReplayConstants(float opacity, bool alphaTest, uint32_t alphaFunc, uint32_t alphaRef, float first, uint32_t bits, float out[4]);
    constexpr uint32_t kD3dCmpGreater = 5, kD3dCmpGreaterEqual = 7;   // D3DCMPFUNC
    /// The shader's test of a window's bit (GlassShader's replayOpen, op for op, its exp's result scaled by `expError`):
    /// k = ordinal - first; open when k >= 0 and frac(c219.w x 2^-(k + 1)) >= 0.5, c219.w = bits + 0.5. With the half the
    /// first open window's value is 0.75, not 0.5: before it the test sat on its boundary, and a GPU's exp a hair short of
    /// 2^-1 shut the first open window ("see-through windows don't seem to work anymore now", the user, 2026-10-21).
    bool OpenBit(float ordinal, const float c219[4], float expError = 1.0f);

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
            float first = 0.0f;        // its open windows (OpenBits: Captured takes BeginBatch's)
            uint32_t bits = 0;
            uint32_t material = 0;     // its glass material (features/Glass: index + 1; its lead mask), BeginBatch's
            uint32_t batchStart = 0, batchCount = 0;   // its MOBA range, BeginBatch's (the panel's)
            bool trans = false;        // a transition batch's interior-lit draw (its exterior-lit one silenced)
            std::vector<uint16_t> open, rest;   // its triangles split by window (SplitByWindow), BeginBatch's; empty: unsplit
            std::vector<float> ps, vs;          // the pixel and vertex shaders' float constants at its draw (SeeThroughDraw's)
        };
        /// Pass: drawn as the client draws it. Defer: drawn with the writes off and its state captured for the redraw.
        /// Silent: drawn with the writes off, not captured (a transition batch's exterior-lit draw: its interior-lit one,
        /// next, is the one deferred; both would put the client's glass over what's behind it).
        enum class Draw { Pass, Defer, Silent };
        /// Why the pending batch's draw was drawn as it was (the panel's: a batch that didn't go see-through, and why).
        enum class Why : uint8_t { kNone, kNotTriangles, kRange, kBlending, kFull, kTransSecond };
        /// A batch passed the loop's cull test: `deferrable` (Deferrable, a glass batch of an opened group) with its
        /// range (MOBA's first index and count), its open windows (OpenBits) and its glass material, else a batch to draw
        /// as it is. Any earlier pending batch is dropped.
        void BeginBatch(bool deferrable, uint32_t start, uint32_t count, float first = 0.0f, uint32_t bits = 0, uint32_t material = 0);
        /// The pending batch's triangles split by window (SplitByWindow), after BeginBatch; Captured hands them over.
        void SplitPending(std::vector<uint16_t> open, std::vector<uint16_t> rest) { m_open = std::move(open); m_rest = std::move(rest); }
        /// The batch loop ended (nothing pending past it).
        void EndLoop() { m_pending = false; m_transFirst = false; }
        /// A device draw: defer it when it's the pending batch's (indexed triangles, the same range), the client draws it
        /// opaque or alpha-keyed (blending mode 0 or 1), and there's room; else draw it. Once per batch; a transition
        /// batch's pair (mode 9, then 7: client::kBlendTransExterior, kBlendTransInterior) as one: the first Silent, the
        /// second deferred (before 2026-10-23 the pair passed: the client's glass over the street from inside).
        Draw OnDraw(bool indexed, uint32_t primitive, uint32_t start, uint32_t count, uint32_t blendingMode);
        /// The last OnDraw's reason when it passed the pending batch's draw (kNone: it didn't, or nothing was pending).
        Why LastWhy() const { return m_why; }
        /// The deferred draw's state was captured (`entry`; its open windows the pending batch's).
        void Captured(const Entry& entry);
        void CaptureFailed() { ++m_failed; }
        /// Every entry, in order, for the redraw (and the list emptied).
        std::vector<Entry> Take();
        size_t Count() const { return m_entries.size(); }
        bool Pending() const { return m_pending; }
        unsigned Failed() const { return m_failed; }
        unsigned Passed() const { return m_passed; }   // pending batches drawn as they are (another blending, full, ...)
        unsigned Silenced() const { return m_silenced; }   // transition batches' exterior-lit draws silenced
        void ResetCounts() { m_failed = m_passed = m_silenced = 0; }

    private:
        bool m_pending = false;
        uint32_t m_start = 0, m_count = 0;
        float m_first = 0.0f;
        uint32_t m_bits = 0, m_material = 0;
        bool m_transFirst = false;   // the pending batch's exterior-lit draw silenced: its interior-lit one is next
        bool m_trans = false;        // the deferred draw was a transition batch's second (Captured's)
        Why m_why = Why::kNone;
        std::vector<uint16_t> m_open, m_rest;
        std::vector<Entry> m_entries;
        unsigned m_failed = 0, m_passed = 0, m_silenced = 0;
    };
}
