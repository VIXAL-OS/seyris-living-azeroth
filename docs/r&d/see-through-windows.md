# See-through windows into real interiors

"Windows you can actually see through into a building's modelled interior" (the user and Simon, 2026-10-20). Research
on how the 3.3.5a client (build 12340) decides which WMO groups it draws, two ways to show the room behind a window,
how to draw the glass over it, the risks, and how many windows have a room behind them at all. The prototype is
`features/SeeThrough` (`SeeThroughWindows`, off by default), its rules `env/SeeThroughModel`, its tests
`tests/seethrough`, the count `tools/see_through_survey.py`.

Sources: wowdecomp (SatyPardus/wowdecomp b1c71b8, `WoW/Source/WorldClient/MapObj.cpp`, `World.cpp`,
`CWorldSceneRender.cpp`, `WorldScene.cpp`, `Engine/Source/Gx/CGxDeviceD3d`), its `OFFSET`/`STATIC` comments checked
against the exe's disassembly (`performance/wow-static-disassembly.txt`) and bytes (`SeeThroughModel.hpp` kGuards, tested
against `Wow.exe`), and wxl-core's hook-point table.

## 1. How the client decides which WMO groups to draw

### The data in memory

A WMO root is a `CMapObj`. Its chunks stay in the file's buffer (`m_pWmoData`, root+0x1CC); `CreateDataPointers`
points the root's fields into it at load:

| Field | Root offset | What |
|---|---|---|
| `m_portalVertexList` | 0x134 | MOPV: `C3Vector[]` |
| `m_portalList` | 0x138 | MOPT: `SMOPortal` (0x14: `startVertex` u16, `count` u16, `C4Plane plane`) |
| `m_portalRefList` | 0x13C | MOPR: `SMOPortalRef` (8: `portalIndex` u16, `groupIndex` u16, `side` i16, pad) |
| `m_portalsCount`, `m_portalRefCount` | 0x174, 0x178 | |
| `m_groupInfo`, count | 0x130, 0x16C | MOGI: `SMOGroupInfo` (0x20: flags, box min, box max, name offset) |
| `m_isGroupLoaded`, `m_mapObjGroupArray[512]` | 0x1E0, 0x1F8 | the group objects (`CMapObjGroup`) |

A group (`CMapObjGroup`) copies its MOGP header at load (asynchronously, after the root): flags at +0x30,
`m_portalStart` +0x50 and `m_portalCount` +0x54 (its refs are `m_portalRefList[start .. start + count)`), its MOBA
batches +0xF8, MOVI +0xE0 (count +0x154), MOVT +0xE8 (count +0x15C), its loaded bit +0x198. A portal is shared by the
two groups it joins: each has a ref to it, with opposite `side`.

A placed instance is a `CMapObjDef` (owner root +0xF4, placement matrix +0x70, inverse +0xB0) with one
`CMapObjDefGroup` per group (`m_defGroups`, +0x120: a `TSCGrowableArray<CMapObjDefGroup*, 4>`, count +0x130, -1 = the
dynamic array: count +0x124, data +0x128). The **visible list** is `CWorldScene::sortTable.m_mapObjDefGroup`, an
intrusive list through `CMapObjDefGroup::m_sortTableLink` (+0xB0); each listed group carries the frusta it was reached
through (`m_frustumList`, +0x6C).

Per-frame scratch: `s_portalExt[2048]` (0x00ADFF54, 0x1C each: the portal's screen rectangle and flags, valid while its
stamp equals `s_portalStamp`, 0x00D1C424, bumped each frame and for each new instance walked); the camera in the walked
WMO's space `s_camPosLocal` (0x00D1C42C), its placement `s_mapObjToWorld` (0x00ADFF10), the camera-relative view-projection
`s_viewProj` (0x00ADFE90) about `s_activeWorldView` (0x00CD8F5C), all set by `SetupMapObjDefCull` (0x007A6E00, which
also sets `s_cullStateValid`, 0x00CFBEBC).

### The walk, once per frame

`CWorldScene::Render` (0x0079A870), every frame, before anything is drawn:

- **Camera inside a WMO** (`s_viewerMapObjDef`, 0x00CD87A4): `CMapObjDef::RenderWithCallback` -> `CMapObj::RenderInterior`
  (0x007AD1F0, `s_interiorPass` = 1, 0x00CFBEC0) walks from the camera's groups. Portals into exterior groups add
  *exterior portal views*; the outdoors is then culled only through their union (`CullSortTable(frustumPortalView)`,
  `CullThroughPortal`).
- **Camera outside**: `CullSortTable` -> `CullMapObjDefGroups` (0x0079A160): each exterior group of each instance in the
  sort buckets that passes the frustum and occlusion tests goes to `CMapObjDef::CullMapObjDefGroupFromExterior`
  (0x007B3A10). That sets the group render callback (`CMapObj::gRenderCallback` 0x00D1BED8 =
  `CWorldScene::AddMapObjDefGroupToSortTable` 0x00799310, its parameter 0x00D1BEDC = the instance) and, for an exterior
  group (MOGP 0x8), calls `RenderThruPortalsExterior` (0x007AD350: `SetupMapObjDefCull`, `s_interiorPass` 0) ->
  **`CMapObj::RenderThruPortals`** (0x007AC060; wxl-core's hook point `Wmo.RenderThroughPortals`):

```
RenderThruPortals(group, fromGroup, CRect& ndc, depth, interior)       __thiscall, ret 14h
  depth > s_maxPortalDepth (10)        -> return
  GetGroup(group) null or flags 0x10000 -> return
  gRenderCallback(group, param)         -> AddMapObjDefGroupToSortTable: list it (once), add the current frustum
  for each ref of the group: skip dest 0xFFFF and fromGroup
     TransformPortal (once per stamp): its NDC rectangle (TransformAndClipVerts 0x007A85E0, occluders, clipped)
     camera on the wrong side of the plane (x side) -> skip
     no overlap with ndc                             -> skip
     from outside: dest exterior or 0x10000          -> skip
     FrustumPush; FrustumSet(s_frustumCorners, (clip + 1) / 2); RenderThruPortals(dest, group, clip, depth + 1); FrustumPop
```

Then `CullMapObjDefGroup` (0x0079A260) culls each listed interior group's doodads (furniture) against its first frustum.
Drawing comes after: `RenderMapObjDefGroups` (0x007964A0) walks the list, sets the instance's projection and lights, and
`CMapObj::RenderGroup` (0x007ABF50) draws the group once per frustum (`InteriorRender` 0x007AC9F0 / `ExteriorRender`
0x007AC6A0: per batch `CullBatch` 0x007A7630 against the frustum, a batch drawn once whatever the frusta). The models (the
furniture among them) draw later, in the world frame's `CM2Scene::Draw` pass 0 (0x00823CB0 on `World::s_m2Scene`,
0x00CD754C), then detail doodads, pass 2, the liquids, the weather, pass 1 (the see-through models).

So the client draws an interior group from outside only where a portal chain from an exterior group reaches it. Windows
aren't portals in WMO files (doors are), so the room behind a window is never in the list.

## 2. Option A: virtual portals in the WMO's arrays

Add each window as a portal at load: one MOPV quad, one MOPT plane, two MOPR refs (exterior side, interior side).

- **The arrays** live in the file's buffer. Growing them means new allocations, repointing the root's three pointers and
  counts, and freeing them when the root goes (`Wmo.FreeMapObj`). A group's refs must be contiguous, so the whole MOPR is
  rebuilt and every group's `m_portalStart`/`m_portalCount` rewritten, including groups that load later, asynchronously,
  from their MOGP header (`Wmo.GroupComplete` would have to patch each). `s_portalExt` is a static 2048: indices past it
  write over whatever follows.
- **Every portal consumer sees them**, not only the draw: `VectorIntersectPortals` (which group the camera and player are
  in), `DistFromClosestExtPortal` (the interior/exterior light blend on models near an exterior portal), the interior pass
  (from inside, each window becomes an exterior portal view, so the outdoors is culled through windows too: more drawn
  indoors), liquids' sounds, the minimap's indoor walk.
- **Crash risk**: high. A count off by one is an out-of-bounds read in the walk; a group loaded before the patch walks
  stale refs; a wrong `side` sign walks the wrong way.

Rejected for the prototype. Its one gain over the next option (the outdoors seen *out* of windows from inside) is better
done the same way as below, later.

## 3. Option B: forced visibility, through a window-sized frustum

When a window of a building is visible from outside within N yards, add the interior group behind it to the visible list.
Done crudely (`AddMapObjDefGroupToSortTable` with the full frustum) it draws the whole room every frame. Done as the walk
does a portal, it costs nothing more than a door does:

**The prototype: a virtual portal evaluated in a hook, nothing in the WMO changed.** After the client's
`RenderThruPortals` returns at depth 0 from outside (`s_interiorPass` 0, the callback the sort table's, the instance not
the camera's building, its root whitelisted, the group exterior), for each of the group's windows (GlassWindows.cdbc,
Flags 0x1) with an interior group behind it (below):

1. **Side and range**: the camera (`s_camPosLocal`) outside the window's plane by 0.05 yd; within `kRange` (60 yd) of the
   exterior group's box.
2. **The rectangle**: the window's quad (grown 0.1 yd) through `s_mapObjToWorld`, less `s_activeWorldView`, through
   `s_viewProj`, clipped in clip space to w > 0 and the four sides, its NDC bounds; overlapped with the walk's own `ndc`
   (as `TransformAndClipVerts`/`CalcScreenRectFromVerts` do for a portal, without the occluder test). CRect's order is
   {minY, minX, maxY, maxX} (checked in `CalcScreenRectFromVerts`' asm).
3. **The group must be enterable** (`GetGroup` reads no bounds): the root's groups loaded, the group object there and its
   loaded bit, the instance's group object for it (the callback reads it).
4. **The walk on**: the callback swapped for a trampoline (to see the room reached) then put back; `FrustumPush`,
   `FrustumSet(s_frustumCorners, (clip + 1) / 2)`, the next link of `RenderThruPortals` at depth 1 into the room (from the
   exterior group), the frustum index put back. The client's walk then does everything a doorway gets: the room listed
   with a window-sized frustum (its batches culled to it), its doodads culled to it, the rooms beyond its own doors
   clipped to it, its interior lighting (vertex colours) as through the door.

At most 48 windows a frame. A window not opened (no room, far, unloadable, over budget, its room not seen through it:
below) is *shut*.

### Which interior group is behind a window

From the window's middle, probe 1 yd then 2 yd inward along its normal (GlassWindows' normal points out); of the groups
the walk from outside may enter (not 0x8, not 0x10000), the one whose box (grown 0.25 yd) holds the probe, the smallest
such box; none: a *fake* window. This is `tools/gen_glass.py`'s `interior_room` (the room GlassPanes' floor light uses)
with a second, deeper probe for thick walls; at run time it uses the root's MOGI, so no new table. `InteriorBehind` and
the survey's `interior_behind` are twins; the tests check the Lion's Pride Inn's 17 windows against the survey.

### Is the room seen through it from here

"certain attic windows shouldn't be able to see through the roof" (the user, 2026-10-20, at the inn at night). A box says
nothing of where the room's walls and ceiling are: a dormer's probe lands in the upstairs room's box, but from the street
the line of sight through it passes over the room's ceiling (nothing behind the glass: the void) or meets the ceiling from
above, where the client culls it (its faces face into the room): opened, the room showed through the roof. So a candidate
opens only where **its middle ray and six of nine rays from the eye through the window** (its middle and a ring at half
its half sizes: inside the frame; an outer window's glass reaches past the room's opening, and its edge is mostly lead and
frame) **meet the room's own surfaces from inside first**: a triangle whose winding's front faces the eye
(`cross(b - a, c - a) . d < 0`, the walls', the floor's and the ceiling's inner side), not a back face and not nothing
(`SeesRoom`). The other three may meet the wall round a smaller or arched opening from outside, or leave the room by a
doorway or a stair's opening: the first build asked all nine, and the inn's west upstairs window (9, its arch's corner on
the wall, two rays out by the stair) never opened ("see-through windows don't seem to work anymore now", the user,
2026-10-21). Six of nine agrees best, over 30 street views of each of the 17 windows, with how much of each window's glass
(weighted by its texture's mask) shows the room from inside: no view where 60% of it does is shut, the dormers never open.
The lights panel lists each window on the screen last frame: open, or why it's shut (fake, far, the budget, not loaded,
its surfaces not read yet, not seen: how many rays met the room from inside, the middle one's, and the first that didn't
and what it met). The room's surfaces are
its group's rendered triangles (its MOBA batches; the collision-only ones after them are left out) but its glass (the
window's own inner face, `mm_elwynn_wnd_int__01` in the inn: its back is to the street), copied out of the group the first
time a window of it is a candidate (MOVT at +0xE8, MOVI at +0xE0, MOBA at +0xF8 and their counts, guarded at the group
walker 0x7D7F50; the root's MOMT and MOTX for which materials are glass, guarded at the root walker 0x7D7470) into a grid
of cells (`RoomMesh`: about 32 along its longest side), one build a frame (the inn's kitchen: 10,847 triangles). Each
frame the rays run against the actual camera, so the answer follows the viewing angle.

With the map scan's files, from street viewpoints in front of each window (8, 15 and 30 yd out, straight and 30 / 60
degrees aside, the eye 2 and 6 yd up; `seethrough_tests` prints them): the ground floor's windows open from most of them,
the back dormers (z 15.5) from none. Weighting rays by the glass mask of the texels they cross (offline, 2026-10-20), the
windows under the back eaves (5, 6) show about a third of their glass through the room's walls from outside, and the upper
floor's (7..11) a rim of about a fifth where the outer glass is wider than the room's opening.

## 4. Drawing the glass see-through

The pane is an opaque batch of the exterior group, drawn before the room (the room's groups join the list after it) and
long before the furniture (model pass 0). Drawn opaque it hides the room; drawn blended in place, the room drawn after it
would cover it. So it is **deferred**:

- **Which batches**: `features/WmoIce`'s hooks say which instance's group draws (`Wmo.RenderGroup`) and each batch passing
  `Wmo.CullBatch`. A glass batch (its texture in GlassMaterials) of a group whose windows were opened this frame is
  deferred when every triangle lies in a window (each triangle's middle in a window's lookup box, cached per batch; stray
  trim up to 0.25 yd^2) and at least one of its windows is open, with the glass's look on (LA's glass block: the redraw
  needs its mask and its window lookup). Its shut and unseen windows are told apart per pixel in the redraw (below), so a
  shut dormer no longer keeps the whole batch opaque. The inn's exterior glass is one batch over all 17 windows.
- **In the opaque pass**: at that batch's device draw (`Gx.DeviceDraw`, 0x006A3620, right after `CullBatch` let it
  through, its range checked against the MOBA record; blending mode 0 or 1 only), the client's own Gx cache is set to
  colour writes 0 and depth writes 0 (`render/GxState`: the client's flush sends them, so its cache and the device agree),
  the client draws it (no pixel, no depth), and the device's whole state is captured (`CreateStateBlock(D3DSBT_ALL)`: the
  shaders, every constant including LA's glass block, textures, samplers, streams, indices, viewport) with
  DrawIndexedPrimitive's arguments worked out as the device draw does (base vertex from the stream's offset / stride, or
  0; first index + the index buffer's offset / 2; min, max; count / 3). The writes go back for the next draw.
- **The redraw**: after `CM2Scene::Draw` pass 0 of `World::s_m2Scene` (the opaque models: the room's furniture is in),
  before detail doodads, liquids, weather and see-through models, each captured state is applied and the batch drawn
  again, one of two ways (`SeeThroughRedraw`). **0, the default** (the 2026-10-20 redraw, the one seen working in game):
  the whole batch blended at the opacity (`BLENDFACTOR` / `INVBLENDFACTOR`), its glass look kept and its fake room off
  (c193.z 0), no depth; a batch is deferred only with none of its windows shut, and every window with a room behind it
  in range opens (no line of sight). **1, per pixel**: with LA's glass look (b9: reflection, darkening, coolness) and
  its redraw switched on (b11, c219: `features/GlassShader`; the WMO edit moves the stock alpha write to r12.w and
  writes oC0.w last). Each pixel looks up its window (the window table the fake rooms use; the lookup forced even where
  the batch wouldn't look) and reads whether it's open from c219.w's bits (one a window from the lowest open ordinal
  c219.z, 16 of them, plus a half: frac of c219.w / 2^(k + 1) >= 0.5; without the half the first open window's value sat
  exactly on 0.5, and a GPU's exp a hair short of 2^-1 shut it, 2026-10-21): an open window has no fake room and hides
  none of its glow, and its alpha is 1 - the glass mask x (1 - `SeeThroughOpacity`), so the painted lead, frame and
  tracery stay opaque ("the tracery also becomes more transparent", the user, 2026-10-20); a shut or unseen one is
  exactly as the client draws it, alpha 1. Two draws, split by that alpha in the shader (c28.y: +1 keeps alpha >= c28.z,
  0.999, -1 the rest, by texkill; the device's alpha test is off): the opaque with its depth written, then the open
  windows' glass over the room, `SRCALPHA` / `INVSRCALPHA`, no depth. (The first per-pixel build split with the device's
  alpha test, >= 255 then < 255; a driver's alpha-to-coverage or transparency antialiasing, or a translation layer, can
  make an alpha test something else, so the split moved into the shader.) In game, 2026-10-21 (by day, in rain, at the
  inn's west front), the panel said windows 3, 4, 6, 9 and 14 open and the batch drawn over the room, yet nothing of the
  room showed. On WARP the inn's own exterior glass through the patched stock `ps_MapObjDiffuse_02`, with the shipped
  tables and its texture, from where the user stood (glass_tests' "inn"), does what the redraw means, pixel for pixel;
  but `mm_elwynn_wnd_ext__01`'s glass mask (GlassMaterials' row: 0.73 R + 2.44 G + 0.25 B - 0.94) counts little of its
  windows as glass: the shutters, the thick lead, the frame and the shaded lower half of each pane are under it. Over
  the ground-floor window (3: the shuttered diamond quadrant, mask mean 0.11, over 0.5 on 9% of its texels) the room is
  about 4% of what's seen from there, over the arched one (9: mean 0.30) about 23%, against 65% everywhere with the
  whole glass. The per-pixel redraw is right but shows little of the room, least of all by day when the room is dark
  against the lit glass; so it went behind `SeeThroughRedraw`, the 2026-10-20 redraw the default. Its debug view (the
  lights panel's, not saved: c28.x) colours each redrawn pixel by what the shader decided: an open window's glass
  magenta by its mask, through grey to green at its lead and frame (alpha 1), cyan a shut window, yellow no window
  found; the alpha is unchanged, so the colours show through the blend as the glass would. The panel also reads the
  device's state back at the first redrawn batch once a second (b9, b11, c219, c28, the batch's alpha test, the blend,
  alpha test and depth write as set, D3DRS_ADAPTIVETESS_Y, the drivers' alpha-to-coverage switch, the target's MSAA, the
  pixel shader). The batch's own alpha test (alpha-keyed glass) moves into the shader (texkill on the stock alpha
  against c219.y). Colour RGB only, depth test on. A shader without the redraw (`if b11` not in its code: a variant
  without the glass blocks) is drawn opaque, as the client would have. The device's state is captured before and put
  back after (the client's cache never sees it). The render target and depth surface must be the ones captured.
- **Sorting**: the glass lands after everything opaque and before everything see-through. Liquids, weather, particles and
  see-through models in front of it are drawn after it: right. Those *behind* it inside the room (a candle's flame, a
  hearth's particles) are drawn after it too, so they sit over the glass, not under it: a small error for additive flames.
  WMO see-through batches inside the room draw in the WMO pass, before the glass: right.

## 5. Risks

- **Frame rate**: each open window adds its room group (batches culled to the window's frustum) and its furniture, as an
  open door does; a 48-window cap; a state block captured and applied per deferred batch (the inn: one). The walk's own
  portal transforms are cached per frame. Untested in game.
- **Interior lighting**: interior groups are lit by their baked vertex colours and the client's indoor ambient, as seen
  through a door. By day the room is darker than the street, which is how it reads through a real window; at night the
  room's own baked light shows. Interior fog: the room joins as from a door (`s_curGroupIsInterior` 0): exterior fog.
- **Doors and portals already open**: a room already listed through a door gets a second frustum (the list keeps one
  entry; a batch draws once whatever the frusta); its doodads are culled against its first frustum only.
- **Back faces**: the room's inside faces of the windows (an interior group's glass, `mm_elwynn_wnd_int__01` in the inn)
  are one-sided and face in, so from outside they're culled. A two-sided inside face would stand opaque behind the
  deferred pane (the window stays dark); interior groups' glass isn't deferred in the prototype.
- **LOD**: WMO groups have none in 3.3.5a; doodads fade by distance as through a door.
- **Group-visibility caching**: `s_portalExt` rectangles are per stamp and portal index, untouched; the batch "drawn"
  bits (0xF0) are per frame. Our state is per frame (cleared on OnUpdate) and per instance and group.
- **What reads the glass's depth**: a deferred pane's see-through texels write no depth, so whatever reads the depth buffer
  there later sees the room's: LA's window glow (it keeps only pixels at the window's own depth) gives an opened window no
  glow at night, and the water's reflections see the room. Its opaque texels (lead, frame, shut windows) write theirs in
  the redraw's first draw.
- **Painted lead and frames**: kept opaque by the glass mask in the redraw (above), in the WMO variants' own shader:
  glass_tests reports the largest variant's slots against kGlassMinSlots.
- **The client's code**: the walk's statics, the frustum stack (32 deep; refused past 20), the callback, the device
  draw's fields, the Gx cache and where the walkers put the room's arrays are guarded (32 byte guards, never a hooked
  function's first bytes); every hook body is
  under `__try`/`__except` (the callback, the frustum index and the Gx writes put back in `__finally`); any fault switches
  the feature off for the session and the lights panel says where.
- **Option A's crash risk**: none taken: no client array is changed.

## 6. How many windows have a room behind them

`python tools/see_through_survey.py <waterfall-scan out> data` over GlassWindows.cdbc (2026-10-17 scan) and every WMO's
root and group files:

| | |
|---|---|
| WMOs with glass windows | 276 |
| windows of exterior groups (the see-through candidates) | 9,035 |
| ... with an interior group behind them | **3,556** (39%) |
| ... fake: nothing modelled behind | **5,479** (61%) |
| windows of interior groups (the room's own glass, seen from inside) | 7,524 |
| WMOs with at least one window with a room behind it | 167 |
| WMOs whose every exterior window has one | 109 |
| exterior glass batches | 453 |
| ... that could go see-through (every window they cover has a room, no stray glass) | 144, covering 1,033 windows |

By kind (exterior windows, real / fake): clear 246 / 71, leaded 567 / 581, frosted 1,765 / 1,672, stained 978 / 3,155.
The big cities are mostly fake (Dalaran 1,091 / 2,858, Stormwind 113 / 284, Ironforge 0 / 190, Silvermoon 171 / 153) and
their glass batches cover real and fake windows together, so at batch level few of theirs could go see-through (Dalaran
1 of 26, Silvermoon 0 of 18). Small buildings are the good case: the orc mage towers, Northrend's orc barracks and towers,
tuskarr huts, the human houses, and the Lion's Pride Inn (17 of 17, one batch).

## 7. The prototype and what's next

`SeeThroughWindows` 0 by default, `SeeThroughOpacity` 0.35, `SeeThroughRedraw` 0 (the whole glass; 1 per pixel), in
`[LivingAzeroth.Glass]` and the lights panel (under the glass; the per-pixel redraw's debug view there, not saved) with
last frame's counts: exterior groups walked, windows opened and shut, rooms added, glass batches in opened groups, kept
opaque, deferred and redrawn, faults. Whitelist: `goldshireinn.wmo` only. It needs the glass feature on (its batch hooks
name the glass batches).

Next, in order: try it in game at the inn; widen the whitelist to the 109 all-real WMOs; the same windows as portals
*outward* from inside (the street through the window), which wants the interior pass's exterior portal views and the
glass deferred from inside too. Done 2026-10-20: the lead and frames opaque (the glass mask in the redraw), a shut
window no longer keeping its batch opaque, and windows opened only where the room is seen through them; 2026-10-21 those
behind `SeeThroughRedraw` 1 until the game shows them working (its debug view and read-back say why not).
