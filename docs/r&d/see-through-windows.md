# See-through windows into real interiors

"Windows you can actually see through into a building's modelled interior" (the user and Simon, 2026-10-20). Research
on how the 3.3.5a client (build 12340) decides which WMO groups it draws, two ways to show the room behind a window,
how to draw the glass over it, the risks, and how many windows have a room behind them at all; and (section 8) how the
client draws the outdoors from inside, and the street through the room's own windows. The prototype is
`features/SeeThrough` (`SeeThroughWindows` and `SeeThroughFromInside`, off by default), its rules `env/SeeThroughModel`,
its tests `tests/seethrough`, the count `tools/see_through_survey.py`.

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
2026-10-21). Six of nine agreed best, over 30 street views of each of the 17 windows, with how much of each window's glass
(weighted by its texture's mask) shows the room from inside: no view where 60% of it does is shut, the dormers never open.

**The gate** (2026-10-21): "Ok interestingly it works at some angles and goes back to the fake room at others lol" (the
user, before the west front: window 9 open, then a step aside shut, "4 of 9 rays meet ..."). Its rectangle's corner rays
met the wall round the arch from outside, and rays that crossed the room and left it by the stair's opening counted
against it. Now (`LookThrough`, `SightOpen`, `Hold`): the rays go through nine points **on the window's own glass**
(`GlassSightPoints`: inside its glass triangles, from the group's glass batches; within the middle 60% of its rectangle,
off the frame; the middle's nearest first, then each the farthest from those before), not its rectangle's corners; a ray
meeting the room's surface **from outside** (a wall or the ceiling from outside: the dormer over the ceiling) counts
against, one meeting it from inside for, one meeting **nothing** of it (on through a doorway, the stair's opening, past
it) neither, but it mustn't outnumber them: to open four see the room and more than meet it from outside (at most two)
or nothing; to stay open three, more than from outside (at most three), at most two fewer than meet nothing. A dormer's
rays mostly go over the room (nothing) and see it with at most four. And the verdict holds: a change stands after 0.35 s
(`kGateHold`), so a window at the edge doesn't flicker. Over the inn's windows from 252 street views each (5 to 30 yd
out, 60 degrees aside either way, the eye 2 to 8 yd up and a mounted camera's 11): window 9 opens from 244 (it was 230),
3 from 227 (221), the north front's 1, 2, 10 and 11 from 241 to 252, the dormers 15 to 17 from none; walking past the
west and north fronts to 60 degrees aside (8 and 14 yd out, on foot and mounted, a frame each 1/60 s), 9, 14 and the
north front's windows never change their verdict (the old rule: 9 four times, 1 nine), 3 three times (the old rule
twenty): it shuts past about 50 degrees on its one side, where its deep reveal hides the room (its rays meet the wall
round it from outside). **Followed on** (2026-10-22): "dormers still switch between the fake rooms and the real ones" (the
user, mounted before the dormer front, the camera 14.5 yd up and 32.5 yd out: 15 shut at 4 / 1 / 4 seeing the room, meeting
it from outside, meeting nothing; 16 and 17 open at 5 / 0 / 4). A dormer's rays that meet nothing of its room go on into
the attic and out where the roof's inside isn't modelled: the shell is one-sided, the client culls it from inside, and the
void or the sky shows through; window 9's went on through the stair's opening into the next room, which the walk draws.
So a ray that meets nothing of the room is now followed on through every group of the building as the client draws them
(`LookIn`: each group's rendered triangles, its glass tagged; faces one-sided; a pane seen from behind see-through and
passed, the window's own inner pane and a dormer's sloped one from above among them): a room's surface from inside, its own
or the next room's through a doorway or a stair's opening, or a room's pane from its front, sees in; any surface from
behind (a room's from outside, the shell's roof or walls from inside) or nothing at all within 60 yd is against; the
building's outside from outside (past an opening) neither. And a majority must see in to open (five of nine; four still
keep an open window open): from high and far a dormer met its room with four, the void with two and its cavity's own
walls with three. From the user's viewpoint the rule before gives what the panel showed (15 4 / 1 / 4, 16 and 17 5 / 0 /
4); now 15 is 4 / 5 / 0, 16 and 17 5 / 4 / 0, all shut, and 4, 5, 6 open with nine of nine. Over the street sweep every
window opens from as many views or more (3 from 252 of 252, it was 227; 9 252, 244; 5 216, 134; 6 221, 202; 8 238, 188;
the north front's 244 .. 252), the dormers from none; high and close before the dormer front (4 to 12 yd out, 9 to 15 yd
up) 4 from 119 of 140 as before, 5 and 6 from 138 (100, 131), the dormers from none (17 was 4); high and farther out (18
to 36 yd) 4, 5, 6 from every view, the dormers from none (17 was 8, 15 1). Walking past every front, on foot, mounted and
at 9, 12 and 15 yd up and at the user's 32.5 yd, no window changes its verdict (the rule before: 3 three times, 5
nineteen, 6 thirteen, 15 and 16 twice, 17 six) and the dormers never open. The lights panel lists each window on the screen last frame: open, or why it's shut (fake, far,
the budget, not loaded, its surfaces not read yet, not seen: how many of its rays saw the room from inside, met it from
outside, met nothing, and whether the verdict is held). The surfaces are each group's rendered triangles (its MOBA
batches; the collision-only ones after them are left out), its glass tagged (since 2026-10-22; before, the room's alone
with its glass left out), copied out of the group the first time a window needs them (MOVT at +0xE8, MOVI at +0xE0, MOBA
at +0xF8 and their counts, guarded at the group walker 0x7D7F50; the root's MOMT and MOTX for which materials are glass,
guarded at the root walker 0x7D7470) into a grid of cells (`RoomMesh`: about 32 along its longest side), one build a frame
(the inn's kitchen: 10,847 triangles; the inn's twelve groups over its first twelve frames, a window shut until all are
read). Each frame the rays run against the actual camera, so the answer follows the viewing angle.

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
  again, one of two ways (`SeeThroughRedraw`). **0** (the 2026-10-20 redraw, the one first seen working in game): the
  whole batch blended at the opacity (`BLENDFACTOR` / `INVBLENDFACTOR`), its glass look kept and its fake room off
  (c193.z 0), no depth; a batch is deferred only with none of its windows shut, and every window with a room behind it
  in range opens (no line of sight). **1, per pixel, the default**: with LA's glass look (b9: reflection, darkening,
  coolness) and its redraw switched on (b11, c219: `features/GlassShader`; the WMO edit moves the stock alpha write to
  r12.w and writes oC0.w last). Each pixel looks up its window (the window table the fake rooms use; the lookup forced
  even where the batch wouldn't look) and reads whether it's open from c219.w's bits (one a window from the lowest open
  ordinal c219.z, 16 of them, plus a half: frac of c219.w / 2^(k + 1) >= 0.5; without the half the first open window's
  value sat exactly on 0.5, and a GPU's exp a hair short of 2^-1 shut it, 2026-10-21): an open window has no fake room
  and hides none of its glow, and its alpha is 1 - the glass mask x (1 - `SeeThroughOpacity`), so the painted lead,
  frame and tracery stay opaque ("the tracery also becomes more transparent", the user, 2026-10-20); a shut or unseen
  one is exactly as the client draws it, alpha 1. Two draws, split by that alpha in the shader (c28.y: +1 keeps alpha >=
  c28.z, 0.999, -1 the rest, by texkill; the device's alpha test is off): the opaque with its depth written, then the
  open windows' glass over the room, `SRCALPHA` / `INVSRCALPHA`, no depth. (The first per-pixel build split with the
  device's alpha test, >= 255 then < 255; a driver's alpha-to-coverage or transparency antialiasing, or a translation
  layer, can make an alpha test something else, so the split moved into the shader.) In game, 2026-10-21 (by day, in
  rain, at the inn's west front), the panel said windows 3, 4, 6, 9 and 14 open and the batch drawn over the room, yet
  nothing of the room showed. On WARP the inn's own exterior glass through the patched stock `ps_MapObjDiffuse_02`, with
  the shipped tables and its texture, from where the user stood (glass_tests' "inn"), does what the redraw means, pixel
  for pixel; but `mm_elwynn_wnd_ext__01`'s glass mask (GlassMaterials' row: 0.73 R + 2.44 G + 0.25 B - 0.94) counts
  little of its windows as glass: the shutters, the thick lead, the frame and the shaded lower half of each pane are
  under it. Over the ground-floor window (3: the shuttered diamond quadrant, mask mean 0.11, over 0.5 on 9% of its
  texels) the room is about 4% of what's seen from there, over the arched one (9: mean 0.30) about 23%, against 65%
  everywhere with the whole glass. The per-pixel redraw was right but showed little of the room, least of all by day
  when the room is dark against the lit glass; so it went behind `SeeThroughRedraw` for a build. **The lead mask**
  ("yeah build the lead mask pls :D", the user, 2026-10-21) is the redraw's own: GLASS every pane's inside, lit and
  shaded alike; OPAQUE the lead, the frame with its mullions and tracery, the shutters and the post. Where the texture's
  panes are is data (GlassLead.cdbc: rectangles in its uv by hand from its layout; the eight human window atlases share
  one, 19 rectangles); the mask is built at run time from the texture as the client reads it (decoded, never stored: the
  fork ships no image data): the texels in the rectangles that aren't lead, not darker than 0.18 x the median luminance
  of the lit glass in them and not on a thin dark line (the luminance closed over 5 x 5 texels, minus it, under 0.15 x
  that median: the lattice's lead, lighter in Duskwood's grey atlas, still counts), uploaded as an L8 texture with its
  mips and bound at s15 for the redraw (c28.w 1; the shader mixes it into the mask register, 3 slots). On WARP with the
  inn's own glass from where the user stood, the room is about 14% of what's seen over window 3 (4% with the light's
  mask), 38% over the arched 9 (23%), 33% over 14 (11%): every pane's glass at the opacity's 65%, the shutters, lattice
  lead and frames (most of window 3's cell) opaque; each lead and frame pixel alpha 1, window 5 shut. Rectangles can't
  follow arched frames (Northrend's `nr_hu_window`) or a sheet's border strips; those textures keep the light's mask
  until they get rows. With it the per-pixel redraw is the default again. **Why the per-pixel redraw never showed the
  room in game** (2026-10-21, the debug view: "every see-through window pixel is solid YELLOW", no window found): the
  capture's state block (`CreateStateBlock(D3DSBT_ALL)`) puts back only the pixel shader's c0..c127 (WARP; glass_tests'
  "sequence" plays the game's order, the inn placed as Goldshire's, the camera turned, Glass's own RoomRegisters, the
  batch drawn silently, captured, the state clobbered, applied: every pixel yellow, the lost constants c128 and up, the
  view's rows c181..c183 among them, so the lookup's point was the camera's for every pixel). The capture now keeps
  every shader constant apart (`features/SeeThroughDraw` CaptureState, ApplyCaptured: the pixel shader's 224, the vertex
  shader's 256), and the same harness then decides every pixel as a redraw right after the client's draw does. Where the
  client's index buffer can be read and holds the batch's own indices, the redraw also draws its open windows' triangles
  apart (c33.x: open whatever the lookup finds). The lights panel reads the lookup's inputs back (c15..c20, c180, s13 at
  the draw and after the state block) and says whether the redraw was split. Its debug view (the lights panel's, not
  saved: c28.x) colours each redrawn pixel by what the shader decided: an open window's glass magenta by its mask,
  through grey to green at its lead and frame (alpha 1), cyan a shut window, yellow no window found; the alpha is
  unchanged, so the colours show through the blend as the glass would. The panel also reads the device's state back at
  the first redrawn batch once a second (b9, b11, c219, c28, the batch's alpha test, the blend, alpha test and depth
  write as set, D3DRS_ADAPTIVETESS_Y, the drivers' alpha-to-coverage switch, the target's MSAA, the pixel shader). The
  batch's own alpha test (alpha-keyed glass) moves into the shader (texkill on the stock alpha against c219.y). Colour
  RGB only, depth test on. A shader without the redraw (`if b11` not in its code: a variant without the glass blocks) is
  drawn opaque, as the client would have. The device's state is captured before and put back after (the client's cache
  never sees it). The render target and depth surface must be the ones captured.
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
  deferred pane (the window stays dark); interior groups' glass isn't deferred from outside (from inside it is: section
  8, where the outer pane is the one culled).
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

`SeeThroughWindows` 0 by default, `SeeThroughOpacity` 0.35, `SeeThroughRedraw` 1 (per pixel; 0 the whole glass), in
`[LivingAzeroth.Glass]` and the lights panel (under the glass; the per-pixel redraw's debug view there, not saved) with
last frame's counts: exterior groups walked, windows opened and shut, rooms added, glass batches in opened groups, kept
opaque, deferred and redrawn, faults. The buildings: SeeThroughBuildings.cdbc's (sections 9 and 10; before 2026-10-22
`goldshireinn.wmo` alone, in the code). It needs the glass feature on (its batch hooks name the glass batches).

Next, in order: try it in game at the inn; widen the whitelist to the 109 all-real WMOs; the same windows as portals
*outward* from inside (the street through the window), which wants the interior pass's exterior portal views and the
glass deferred from inside too (section 8: `SeeThroughFromInside`). Done 2026-10-20: the lead and frames opaque (the
glass mask in the redraw), a shut window no longer keeping its batch opaque, and windows opened only where the room is
seen through them; 2026-10-21 the lead mask (GlassLead.cdbc), every pane's glass see-through and the frames opaque, the
per-pixel redraw the default again.

## 8. From inside: the street through the windows

"windows should be see-through from the inside as well" (the user, 2026-10-21, standing in the inn's ground floor). From
inside, the windows are the room's own glass (an interior group's batch, `mm_elwynn_wnd_int__01` in the inn: GlassWindows'
rows without Flags 0x1, 7,524 of them in all, 17 in the inn: the kitchen's 18..25, exterior02's 26..29, the hall's 0, the
common room's 30, room01's 31 and 32, room02's 33), drawn opaque with nothing behind them: from inside the client draws the
outdoors only through its ways out, and windows aren't ways out.

### What the client draws of the outdoors from inside

Every frame `CWorldScene::Update` (0x00795400) resets two views (CPortalView, 0x1C: a CRect in 0..1 in CRect's order
{minY, minX, maxY, maxX}, a depth, vertices, a count) to empty with depth -1: the **world view** `s_unk_ADF570`
(0x00ADF570, depth at 0x00ADF580) and the **frustum view** `frustumPortalView` (0x00ADF58C, depth 0x00ADF59C), and empties
`s_exteriorPortalViews` (0x00CDD0E8, a TSGrowableArray: count at +4) and `s_portalViews` (0x00CDD0F8). `LocateViewer3`
(0x00795D40) finds the camera's building and groups; a camera group with 0x40140 (exterior-lit, exterior sky, skybox) gets
a full-screen world view at once (an open courtyard's sky).

- **The walk from inside** (`CWorldScene::Render` 0x0079A870: `s_viewerMapObjDef` set -> `CMapObjDef::RenderWithCallback`
  0x007B3B20 -> `CMapObj::RenderInterior` 0x007AD1F0: `SetupMapObjDefCull`, `s_interiorPass` 1, `FrustumPush`, then
  `RenderThruPortals` 0x007AC060 (`Wmo.RenderThroughPortals`) from each of the camera's groups at depth 0 with the whole
  screen). The walk is the same function as from outside: the render callback for each group reached (it joins the sort
  table with the frustum it was reached through), each portal on the camera's side whose screen rectangle overlaps the
  walk's, `FrustumPush` / `FrustumSet(s_frustumCorners, (clip + 1) / 2)` / recurse / `FrustumPop`, to depth 10. Its one
  difference from inside (0x007AC3C0): a portal into a group with 0x50148 (exterior 0x8, 0x10000, exterior-lit 0x40,
  exterior sky 0x100, skybox 0x40000) gets **`CMapObj::AddExteriorPortalView`** (0x007A8F20, wxl-core's `Wmo.PortalRectAccum`):
  once a frame per portal (its `s_portalExt` flag 0x4), the portal's vertices through `TransformAndClipVerts` (0x007A85E0,
  nudged 0.01 yd along its normal), their screen rectangle (`CalcScreenRectFromVerts`, NDC) as (x + 1) / 2, its depth
  `CalcPortalFarthestDistance` (0x007A70D0: its farthest vertex's distance from the camera's plane, at least 0), pushed
  with `PushExteriorPortalView` (0x00795D00, `__cdecl(CPortalView*)`: the array's Add), merged into the world view
  (`MergeIntoWorldRect` 0x00790AB0: the rectangles' union, the larger depth) and, into an exterior group (0x10008), into the
  frustum view (`MergeIntoFrustumRect` 0x00790AD0); the walk doesn't go on into an exterior group (the outdoor cull does
  it), but goes on into a 0x40/0x100/0x40000 one. The portal's own rectangle is pushed, not its overlap with the walk's.
  A walked group with 0x40000 sets `s_interiorSkybox` (0x00CD861C).
- **The outdoors' gate** (0x0079AA33): the frustum view's depth < 0 (no way out reached): `sortTable.Clear()`, nothing of
  the outdoors at all. Else `s_occlusionNearDepth` (0x00CD8780) = its depth + 33 1/3 yd and `CullSortTable(frustumPortalView)`
  (0x0079A790): one `FrustumPush`, `FrustumSet(rect)` (0x00790AF0: the camera's frustum corners narrowed to the union
  rectangle), then over the 64 distance buckets the terrain chunks (`CullChunks`, their M2 doodads with
  `CullDoodadsExterior`), the other buildings' exterior groups (`CullMapObjDefGroups` -> `CullMapObjDefGroupFromExterior`
  0x007B3A10: their own walks from outside, `s_interiorPass` 0, the camera's building's shell among them), the liquids, the
  entities, the doodads, the horizon (`CullHorizon`), against that one frustum; terrain occluders only past the near depth
  (and `CWorldOcclusion::AddVolume` clips its volumes at the frustum view's depth). Then `CullThroughPortal` (0x00799F80) the
  pending exterior groups with the full screen, `CullMapObjDefGroup` (0x0079A260) the listed interior groups' furniture
  against their first frustum.
- **The sky and the fill** (0x0079AC41): the world view's depth < 0: the screen cleared to the interior fog's colour, no
  sky. Else cleared to black; after the terrain, the buildings and the horizon, `CreateInversePortalList` (0x007968D0: the
  screen less every exterior view's rectangle, as at most about 60 rectangles; past that it stops with the ones so far)
  and `RenderPortalList(s_exteriorPortalViews, 1)` (0x00796C10) fill them at the far plane in the interior fog's colour
  (depth tested: only where nothing was drawn), the interior skybox if any, and `DayNight::RenderSky` (0x007F09B0) with the
  scissor on the world view's rectangle, its depth range 0.99902 .. 1 (behind everything drawn). The weather (`Rain::Render`
  0x0078AE20, Snow 0x0078BA60, Sand 0x0078C3E0, after model pass 0) draws only with the world view's depth >= 0; its drops
  start above the weather grid's height (`CMap::QueryHeight` from the drop: a roof), so none fall under a roof.
- **Fog and light**: a WMO group's batches take fog mode 2 - interior (`SetShaderFogFromDayNight` 0x007A8440: interior
  groups `m_fogInterior`, exterior ones `m_fog`); the terrain and the outdoors `m_fog` (`ApplyFogState` 0x00781610). With
  the camera in one of the WMO's fog volumes (`QueryCameraFog`: a WMO with more than one MFOG), DayNight moves `m_fog`'s
  start, end and density toward the WMO's fog by how far in (full at 25 yd), its colour kept: the inn has two MFOG entries,
  the second's end 83 yd, so from inside the street through any way out is fogged by about 83 yd, the same through a door
  or a window. The far clip (`World::s_farClip` 0x00CD7748) doesn't change indoors. Interior groups keep their baked vertex
  colours and the indoor ambient; the outdoors its own light. Models within 25 yd of an exterior portal blend indoor and
  outdoor light (`DistFromClosestExtPortal` 0x007D77C0): windows aren't portals, so the furniture by a window stays
  indoor-lit, as before.
- **The frustum stack**: `frustumStack[32]` (0x00CDB168), `frustumIndex` (0x00CD8798); `FrustumPush` copies the top,
  `FrustumSet(corners, rect)` narrows it, `FrustumPop` drops it. The walk pushes one per portal (at most 10 deep); the
  outdoor cull pushes one (`CullSortTable`, `CullThroughPortal`), each building's exterior group one more
  (`CullMapObjDefGroupFromExterior`). `s_cullStateValid` (0x00CFBEBC) says `SetupMapObjDefCull` ran (the WMO's camera and
  matrices), from inside as from outside.

So a way out from inside costs what an open door costs: the outdoors culled through the union of the ways out's
rectangles (one frustum: two windows at the screen's sides cull through the rectangle spanning both, as two doors would),
drawn whole and then overdrawn by the room's walls where they're nearer, the sky in that rectangle, the weather turned on.
Nothing in the walk needs to change for a window to be a way out: only one more exterior portal view.

### A window as a way out

In the walk from inside (`s_interiorPass` 1, the render callback the sort table's, its instance the camera's building,
in the whitelist), after the client's own `RenderThruPortals` for a group returns (any depth: our hook sees every call,
the recursion too), each of the group's own windows (GlassWindows' rows of that interior group) that faces the camera from
inside (`FacingIn`: the camera inside its plane by 0.05 yd; GlassWindows' normal points out of the building), whose quad
(grown 0.1 yd) is on the screen and overlaps the walk's rectangle for the group, within 40 yd (`kInsideRange`), at most 16
a frame, and decided once a frame (at the first reach that sees it), becomes a way out where the street is really seen
through it (below): the view `AddExteriorPortalView` would make of a portal there (`ViewOf`: the quad's screen rectangle
as (x + 1) / 2, its farthest clip w as the depth), pushed and merged through the client's own `PushExteriorPortalView`,
`MergeIntoWorldRect` and `MergeIntoFrustumRect`, as for a portal into an exterior group. The client then culls and draws
the outdoors through it, its sky and its weather, as through a door; nothing else changes (no frustum pushed, no callback
swapped, no array of the WMO's touched). The room's glass batch (one in the kitchen: windows 18..25, no strays) is deferred
and drawn again after model pass 0 exactly as from outside (`SeeThroughRedraw`, `SeeThroughOpacity`: its open windows'
panes see-through by the lead mask, which GlassLead.cdbc has for `mm_elwynn_wnd_int__01`, their lead and frame opaque, a
shut one as the client draws it), the shader's window lookup over the kitchen's own window grid (Glass's table has every
group's windows), Glass's inside look kept.

**Is the street seen through it from here** (`LookOut`): from inside a ray that leaves the room's glass can meet the
building's own outside from behind (a wall, the roof, a dormer's cheeks: their faces face out, so the client culls them
from inside and the street would show through them) or leave by the building's own outer pane (`mm_elwynn_wnd_ext__01`,
0.4 yd out in the inn, one-sided, MOMT 0x11, so from inside it's culled too and doesn't hide the street). The rays go from
the eye through the window's own glass points (`GlassSightPoints` over the room's glass batch) on past it, through every
group of the building as from outside (2026-10-22; before, the room's surfaces and the shell's: the same answers but one
window's two views), the room's own inner pane passed. Leaving by the building's glass (an outer pane either way, one in
the inner pane's own plane too: window 32's), or meeting nothing at all, sees the street; meeting a surface from behind
(the shell's or a room's) is against; meeting one from the front (the window's reveal behind its glass, the building's
outside past an opening, another room) neither: it's drawn as it is. `SightOpen` and `Hold` decide as from outside (five
see the street, more than either other kind, at most two from behind; held 0.35 s). The dormers' sloped inner panes (0,
25, 29: up into the dormer, its roof and cheeks from inside) are the windows it shuts; see the tests below.

### Risks

- **Cost**: as a door's. A window in view adds the outdoors culled through its rectangle (and widens the union with any
  door's), its sky and weather; with no window or door in view nothing changes. At most 16 windows a frame; the rays (nine
  a window) against the room's and the shell's grids. The meshes are built one a frame (the kitchen, the shell's walls and
  glass, group 11's: five frames before the first window can open).
- **Two-sided outer panes**: the inn's outer glass is one-sided; a WMO whose exterior glass has MOMT 0x4 would stand opaque
  between the room's glass and the street (the gate counts it as a way out). A wider whitelist must check that, or defer
  the outer pane from inside too.
- **Sorting**: the weather's drops, the liquids, the detail doodads (grass), the particles and the see-through models outside
  are drawn after the redraw: over the glass, not under it (the same as from outside for a candle's flame).
- **The fill's 60 rectangles**: many ways out at once can stop `CreateInversePortalList` early; the fill then covers some
  ways out's background, and the sky drawn after it over the union rectangle shows there anyway (its depth range is in
  front of the fill's).
- **Modern Water**: its thunk round model pass 0 (our redraw runs inside it, before its fog composite) sees what it sees
  through a door: the outdoors' depth behind a see-through pane (no depth written there), so its fog lands on the glass by
  the street's distance. Its code isn't here to read; every state it can see is one the client makes itself with a door in
  view.
- **LA's own**: the ground fog, the sun shafts and the window glow switch off indoors (the client's interior blend >=
  0.999), so the street through a window has none of them, as through a door; near the door, where the blend is partial,
  they draw as before. The storm's flashes follow the same blend. The coloured shafts and floor light are the room's own.
- **Video Cinema**: its screen is drawn at the scene's clear (its depth tested by what comes after): outside, through a
  way out, it shows as any outdoor object.
- **Interior lighting**: unchanged (the room's vertex colours and ambient; the street its own light; the furniture by a
  window indoor-lit).
- **Guards**: the walk's statics and the frustum stack's were guarded already; added: the walk's exterior-view call from
  inside (0x007AC3C0), AddExteriorPortalView's view and its depth at +0x10 with its push and merge (0x007A904C), the push's
  array (0x00795D06), the merges' views (0x00790AB6, 0x00790AD6), the scene render's gate on the frustum view and its cull
  through it (0x0079AA33, 0x0079AA4B), and its sky gate and the exterior views' count (0x0079AC41); none in a hooked
  function's first bytes (wxl-core's `Wmo.PortalRectAccum`, `Wmo.PortalTraverse`, `CullSortTable` and the scene render
  among them). Any fault in the walk from inside switches the feature off for the session.

### The prototype

`SeeThroughFromInside` (`[LivingAzeroth.Glass]`, 0 by default; its own box in the lights panel's see-through section; it
doesn't need `SeeThroughWindows`). With it off nothing of this runs. The panel says, last frame: the rooms walked with
windows of their own, the windows opened as ways out, shut (not seen through, far, the budget), the ways out in all and
the client's own (its doors and openings), whether the outdoors is drawn, and each window's verdict with its rays.

**Offline** (2026-10-21). `seethrough_tests`: the new guards against `Wow.exe` (none in a hooked function's first bytes,
the statics and calls they name the runtime's); `PlanInside`, `ViewOf` (the CPortalView layout, (x + 1) / 2, the depth)
and `LookOut` on a test room (glazed outside: open from three eyes; no outside: open; a blind wall: shut, met from behind
0.45 yd past the glass; a window painted on the wall: shut); the inn's rooms' 17 windows through the real reader
(`BuildInside` over `glass::Load` on the shipped tables); with the map scan's files, each room's windows from eyes all over
it (1.5 yd apart, 1.8 to 4.5 yd over its floor) against its surfaces and the shell's walls and glass: the ground floor's
18, 19, 20 and 21 open from 90, 93, 96 and 99.6% of them; the upper ones 22, 23, 24 from 78, 85 and 68%; the rooms
upstairs' 26..28 and 30..33 from 92 to 100%; the dormers' sloped inner panes 25 and 29 from 0.2 and 3% (their rays meet
the dormer's roof and cheeks from inside) and the hall's 0 from 21% (where its rays leave by the dormer's upright pane).
From the kitchen at (0, 6, 3) looking at its south wall, 18 and 19 open with nine of nine rays each; their views
(0.442..0.561 tall, 0.744..0.985 and 0.023..0.262 wide) merge into one rectangle across the screen, and the street 20 yd
past each of their 18 glass points lies inside it, so the outdoor cull takes it in. `glass_tests` "inside": the game's
sequence on WARP for the kitchen's own glass (group 5's batch, `mm_elwynn_wnd_int__01`, the inn as Goldshire's, Glass's
constants as indoors over the kitchen's window grid): right after the client's draw 911 pixels of the open window 19 and
924 of the shut 18, none without a window; through the state block alone all yellow, as before the capture fix; with every
constant put back the same 911 / 924, whole and split by window; the plain view over two backdrops: what's behind shows
through all 242 of 19's glass pixels and none of its 328 lead and frame pixels or of 18's 924 (the old "blue" test counted
154 of 18's: the inside look of the glass is sky-blue by day; both cases now test what changes with the backdrop).

**Only the game can confirm**: that the client draws the street, the sky and the weather through a window as through the
door (the panel: ways out in all against the client's own, and "the outdoors drawn through them" with no door on the
screen); the frame rate with the street drawn from inside; the outer panes' culling from inside (MOMT 0x11: one-sided) and
nothing of the shell left standing in the view; the street's fog (the inn's MFOG, about 83 yd) and light; rain seen outside
and none falling in the room; Modern Water's fog over the glass; that Glass's inside look and the lead mask hold on the
real device; the dormers' sloped panes shut and their outside's back faces not seen through.

## 9. More buildings (2026-10-22)

"More buildings for see-through windows", starting with the human buildings that use the window atlases the lead masks
cover (`mm_elwynn`, `mm_duskwood`, `mm_rr`, `mm_westfall` `_wnd_ext__01` / `_int__01`). Which buildings is now data:
**SeeThroughBuildings.cdbc** (cdbc-tables.md: one row per WMO path, its Flags 0x1 from outside, 0x2 from inside; written by
`tools/see_through_survey.py --table` from its SEE_THROUGH list; features/Glass loads it with the glass tables, so the
lights panel's reload picks up an edit). The runtime's check is the table's flags for the root's path; a WMO not in it is
as it was.

**Which.** 79 WMOs use a human atlas; 47 have every glass batch see-through-able by the per-pixel redraw (its strays trim,
a room behind at least one window: `human_survey`), 56 at least one. Each candidate went through the gates offline
(seethrough_tests' survey: `SEETHROUGH_SURVEY`, a list of WMO paths; the map scan's files and the shipped GlassWindows):
from outside the street sweep (each window with a room, 5 to 30 yd out, 60 degrees aside, 2 to 11 yd up: 252 views),
high and close (4 to 12 yd out, 9 to 15 yd up), slow walks past each front (8 and 14 yd out, 3, 7 and 12 yd up, 7 yd a
second, a frame each 1/60 s, the verdict held as the runtime holds it), and from inside each room's windows from eyes all
over it. The eye stands on the building's lowest point (a house on the terrain), or for a city's WMO on the street under
each window. Each window's rays go through its near groups (`NearGroups`: the groups whose box meets the box round its room
and its middle, grown 6 yd; a city's WMO has 286), as the runtime's.

| WMO | windows with a room | street views open | windows open from most | never (dormers, recesses) | high and close | flips on walks (windows, most) | inside: windows, views open, never |
|---|---|---|---|---|---|---|---|
| goldshireinn | 17 | 79% | 14 | 3 | 76% | 0 (0, 0) | 17, 82%, 0 |
| duskwood_inn | 17 | 78% | 14 | 3 | 78% | 0 (0, 0) | 17, 83%, 0 |
| redridge_inn | 17 | 78% | 14 | 3 | 78% | 0 (0, 0) | 17, 83%, 0 |
| westfall_inn | 17 | 78% | 14 | 3 | 78% | 0 (0, 0) | 17, 83%, 0 |
| oldstrat_inn | 17 | 79% | 14 | 3 | 76% | 0 (0, 0) | 59, 32%, 40 |
| humantwostory | 8 | 99% | 8 | 0 | 97% | 4 (1, 4) | 8, 98%, 0 |
| duskwood_humantwostory | 8 | 99% | 8 | 0 | 96% | 4 (1, 4) | 8, 97%, 0 |
| redridge_humantwostory | 8 | 97% | 8 | 0 | 96% | 4 (1, 4) | 8, 91%, 0 |
| westfall_humantwostory | 8 | 97% | 8 | 0 | 96% | 4 (1, 4) | 8, 92%, 0 |
| oldstrat_humantwostory | 8 | 99% | 8 | 0 | 97% | 4 (1, 4) | 8, 98%, 0 |
| magetower | 8 | 100% | 8 | 0 | 100% | 0 (0, 0) | 8, 97%, 0 |
| cot_magetower01 | 8 | 100% | 8 | 0 | 100% | 0 (0, 0) | 8, 96%, 0 |
| duskwood_magetower | 8 | 100% | 8 | 0 | 100% | 0 (0, 0) | 8, 96%, 0 |
| duskwood_magetowerpurple | 8 | 100% | 8 | 0 | 100% | 0 (0, 0) | 8, 96%, 0 |
| netherpvp_magetower | 8 | 100% | 8 | 0 | 100% | 0 (0, 0) | 8, 96%, 0 |
| redridge_magetower | 8 | 100% | 8 | 0 | 100% | 0 (0, 0) | 8, 96%, 0 |
| westfall_magetower | 8 | 100% | 8 | 0 | 100% | 0 (0, 0) | 8, 96%, 0 |
| townhall | 8 | 1% | 0 | 6 | 0% | 4 (2, 2) | 2, 52%, 0 |
| townhall_nowall | 8 | 5% | 0 | 6 | 0% | 20 (2, 10) | 2, 51%, 0 |
| duskwood_townhall | 8 | 1% | 0 | 6 | 0% | 4 (2, 2) | 2, 53%, 0 |
| duskwood_townhall_nowall | 8 | 5% | 0 | 6 | 0% | 20 (2, 10) | 2, 52%, 0 |
| redridge_townhall | 8 | 1% | 0 | 6 | 0% | 4 (2, 2) | 2, 53%, 0 |
| redridge_townhall_nowall | 8 | 5% | 0 | 6 | 0% | 20 (2, 10) | 2, 51%, 0 |
| westfall_townhall_nowall | 8 | 5% | 0 | 6 | 0% | 20 (2, 10) | 2, 52%, 0 |
| farm | 3 | 64% | 2 | 0 | 62% | 6 (2, 4) | 2, 97%, 0 |
| duskwood_human_farm | 3 | 64% | 2 | 0 | 61% | 6 (2, 4) | 2, 97%, 0 |
| humanfarm_ab | 3 | 64% | 2 | 0 | 61% | 6 (2, 4) | 2, 97%, 0 |
| redridge_human_farm | 3 | 64% | 2 | 0 | 63% | 6 (2, 4) | 2, 98%, 0 |
| westfall_human_farm | 3 | 64% | 2 | 0 | 59% | 5 (2, 3) | 2, 94%, 0 |
| oldstrat_farm | 3 | 64% | 2 | 0 | 62% | 6 (2, 4) | 2, 97%, 0 |
| barn | 3 | 0% | 0 | 3 | 0% | 0 (0, 0) | 0, 0%, 0 |
| duskwood_barn | 3 | 0% | 0 | 3 | 0% | 0 (0, 0) | 0, 0%, 0 |
| redridge_barn | 3 | 0% | 0 | 3 | 0% | 0 (0, 0) | 0, 0%, 0 |
| westfall_barn | 3 | 0% | 0 | 3 | 0% | 0 (0, 0) | 0, 0%, 0 |
| oldstrat_barn | 3 | 0% | 0 | 3 | 0% | 0 (0, 0) | 0, 0%, 0 |
| goldshireblacksmith | 2 | 0% | 0 | 1 | 5% | 0 (0, 0) | 2, 0%, 0 |
| duskwood_blacksmith | 2 | 0% | 0 | 1 | 5% | 0 (0, 0) | 2, 0%, 0 |
| blacksmith_ab | 2 | 0% | 0 | 1 | 5% | 0 (0, 0) | 2, 0%, 0 |
| redridge_blacksmith | 2 | 0% | 0 | 1 | 5% | 0 (0, 0) | 2, 0%, 0 |
| westfall_blacksmith | 2 | 0% | 0 | 1 | 5% | 0 (0, 0) | 2, 0%, 0 |
| nh_blacksmith | 2 | 0% | 0 | 1 | 5% | 0 (0, 0) | 2, 0%, 0 |
| westfall_guardtower | 2 | 0% | 0 | 2 | 0% | 0 (0, 0) | 0, 0%, 0 |
| prisonhq_redridge | 10 | 65% | 6 | 0 | 83% | 25 (6, 6) | 10, 69%, 0 |
| nd_human_house01 | 12 | 0% | 0 | 11 | 0% | 0 (0, 0) | 10, 0%, 10 |
| stormwind | 113 | 2% | 0 | 84 | 2% | 72 (14, 10) | 662, 10%, 276 |

(Stormwind's row is with its eyes on its streets; with them at its lowest, its canals, the same 2%.) Chosen, 24: the five
inns (the Lion's Pride Inn and its Duskwood, Redridge and Westfall variants, old Stratholme's: 17 windows, 14 open from most
street views, the three dormers never, no flips; from inside 82%, Stratholme's 32% over its 59 room windows, 40 of them
between its rooms), the five two-storey houses (97 to 99% of street views; one window shutting once a walk as the view
turns steep), the seven mage towers (every view, no flips), the six farmhouses (the two downstairs windows from most views,
the attic's hardly: the dormer case; a window shutting once a walk), and Redridge's prison camp headquarters (10 windows,
65%; six shutting once a walk at the steep end). Left out: the town halls (8 windows with a room but seen from 1 to 5% of
views, the doors' rooms behind thick walls; 2 windows flipping up to 10 times), the barns, smithies, the Westfall guard
tower and Northrend's human house (their windows' rooms never seen through them: the probe's room isn't the one behind
the glass), the closed variants (no rooms), the vrykul buildings (not human; a later pass), the dungeons', and
**Stormwind**: one WMO for the city, 113 of its 397 windows with an interior group behind, mostly a few large volumes (its
houses aren't modelled inside); seen from 2% of street views, 14 windows changing their verdict up to 10 times on walks.

**Cost.** GlassPanes' panes of the chosen WMOs within 60 yd of busy spots, facing them (a pane is about a window): before
Goldshire's inn 5 (18 within 60 yd), Goldshire's crossroads 3, Lakeshire 2, Darkshire 15 over three buildings (the inn and
two houses), Southshore 7 over three, the busiest spot anywhere about 21; Stormwind 0 (not chosen). So a busy street opens
some 5 to 20 windows a frame from outside, each its room (and the rooms beyond its doors) as a door would, within the caps:
48 windows a frame from outside, 16 ways out from inside, 24 deferred glass batches, one mesh build a frame (a window stays
shut until its building's near groups are read: the inn's twelve over its first twelve frames), the mesh cache emptied past
128 groups (each about 50 to 400 KB). The rays: nine a window, through its near groups, only for windows facing and on the
screen. The range is now the window's own (60 yd) as well as its group's box (a city's group can hold the camera).

**The panel** names each window's building and group (its WMO's file name: "duskwood_inn group 4 window 3: open") and says
how many buildings the table names.

**Only the game can confirm**: the frame rate in Darkshire and Southshore with a few buildings' rooms open; that each
placement's rooms are its own (every placement of a chosen WMO opens, wherever it stands: a ruined or empty village's
too); the farmhouses' and houses' rooms by night and day; the new buildings' interior glass (the same atlases) with the
lead mask.

## 10. Stained glass (2026-10-22)

"Cathedrals and chapels with modelled interiors (Northshire Abbey, the Redridge/Lakeshire chapel, others), Silvermoon and
Scarlet-style churches outside dungeons; plus Northrend's arched human windows (nr_hu_window: Valgarde, Westguard Keep, New
Hearthglen), which have the same curved-tracery problem ... The coloured glass should stay coloured over the room (tint, not
just blend); and from inside, the street through the coloured glass. Dungeons and raids stay out."

**Which.** 69 WMOs outside the dungeons' folders (gen_glass's `is_dungeon`) use stained glass (GlassMaterials kind 2) or a
Northrend human window (`nr_hu_*`); 22 have at least one such glass batch over windows with a room behind them. Through the
same survey as section 9, with one more measure: of the walks past each front (six a window), how many changed its verdict
more than twice (open, shut, open again: a flicker; opening as the front comes round and shutting as it goes is two):

| WMO | windows with a room | street views open | windows open from most | never | high and close | flips on walks (windows, most); walks flickering | inside: windows, views open, never |
|---|---|---|---|---|---|---|---|
| redridgechapel | 11 | 96% (48% before the reveal) | 11 | 0 | 97% | 14 (2, 11); 0 of 66 | 11, 71%, 0 |
| nsabbey, nh_abbey | 46 | 58% (16%) | 31 | 4 | 51% | 246 (37, 12); 0 of 276 | 47, 33%, 1 |
| nd_human_barracks, its Borean twin | 20 | 61% (20%) | 14 | 6 | 63% | 6 (2, 3); 0 of 120 (14 with the wall's face alone) | 8, 41%, 0 |
| nd_human_townhall | 6 | 66% (41%) | 5 | 0 | 70% | 34 (5, 9); 0 of 36 | 6, 15%, 0 |
| nd_human_bg_keep | 12 | 55% (3%) | 8 | 4 | 57% | 6 (2, 3); 0 of 72 | 16, 3%, 9 |
| nd_human_house03 | 2 | 50% | 1 | 1 | 50% | 0 | 1, 100%, 0 |
| cathedral, nh_cathedral (the Scarlet cathedral) | 16 of 60 | 1% | 0 | 11 | 0% | 1 | 11, 5%, 4 |
| scarlet_monestary (its outside) | 8 of 27 | 14% | 0 | 4 | 15% | 5 | 3, 1%, 1 |
| nd_human_inn | 5 | 17% (its ground-floor window 79%) | 1 | 4 | 11% | 2 (1, 2); 0 of 30 | 10, 7%, 5 |
| nd_human_house01, house02 | 12, 45 | 1%, 8% | 0, 2 | | | | |
| bloodelf/highelf small houses | 8 .. 14 | 19 .. 36% | 0 .. 6 | | | 79 .. 110 (most 12); 2 of 84 | none in their rooms |
| night elf small houses | 5 | 67% | 3 | 0 | 62% | 23 (3, 9); 0 | 5, 36%, 0 |
| sporehut_01 | 19 | 0% | 0 | 19 | | | 11, 6%, 7 |

**The reveal.** Before it the chapel's tall windows opened from 48% of street views, a third of their rays "against": past
the outer glass they met the inner face of the wall round the window's opening from behind, the opening behind the glass
being smaller than the glass (the chapel's walls are about a yard thick, the town hall's 1.1 to 1.6). The client culls that
face and draws the room past it, so the gate now passes a room's surface from behind within `kReveal` (2 yd) of the window's
outer pane when it stands in the window's own wall (facing along its normal within `kRevealFacing`, 0.7). Northrend's
barracks' upper windows (a foot wide) still flickered on 14 of 120 walks: at a slant their rays met the sides of the deep
opening from behind; a face along the window (`along`) is passed too. A face up the window (a dormer's ceiling seen from
above, a sill, a lintel) still counts against: the inn's dormers 15 to 17 stay shut from every sweep (seethrough_tests' gate
test, unchanged), and its other windows open from more views (82% of street views, 79% before; the two-storey houses 98 to
99%; the prison camp's headquarters 71%, 65% before, its upper windows changing at most 10 times over six walks, never more
than twice in one). `RoomMesh::First` gives the face's normal for it.

**Chosen, 9** (SeeThroughBuildings.cdbc, 33 rows now): the chapel (Lakeshire's; the same WMO stands in Tyr's Hand, New
Hearthglen and old Hillsbrad, as the human farm's copies stand in old Stratholme: the table is per WMO), Northshire Abbey and
New Hearthglen's (their 48 fake windows over the nave's blind walls stay shut), Northrend's barracks (Westguard Keep's;
Valiance and Wintergarde Keep's twin), from outside and inside; its town hall (Amberpine Lodge, Farshire, Wintergarde) and the
Isle of Conquest's Alliance keep from outside only (their rooms' own windows open onto the street from 15% and 3% of eyes
inside); Wintergarde's small house (one window of two); Valgarde's and Westguard Keep's inn from outside only (its
ground-floor window from 79% of street views; its four upper ones never, their rays meeting the roof from inside). Left out: the Scarlet cathedral (Tyr's Hand's, New Hearthglen's: 16
of its 60 windows have a room, the nave's volume, seen from 1% of views), the Scarlet Monastery's outside, Northrend's other
houses, Silvermoon's small houses (their windows have no room glass inside, 19 to 36% of views, flickering), the
night elves' houses and Sporeggar's huts (not churches; a later phase may take the houses). Silvermoon's churches are its
city's one WMO (no window with a room).

**The lead mask from the texture.** GlassLead's rectangles can't follow an arch. The same runtime rule (env/GlassModel
LeadMask: not darker than Dark x the lit glass's median luminance, not on a thin dark line) now reads the panes out of the
texture: Northrend's arched windows have one row, the whole texture, with Dark 0.5 (their wooden frame is about half the
glass's luminance), and over a whole texture a texel must also be lit (the light's mask over a half) or coloured (1 - its
least channel / its largest at least `kLeadColour`, 0.3), since the flat grey round each window is as bright as its glass's
shade; `nr_hu_window` 35% glass, the lit glass 95% of it, the grey round it 0%, a pane 78%. The stained sheets (the abbey's
and the chapel's are one layout, recoloured) have their three designs a quadrant each (their lead is their frame) and the
lattice window's four lights, not the stone cross between them (grey stone as bright as the sheet's white glass; by colour
it read 24 .. 56% glass): Dark 0.4 the abbey's, 0.5 the chapel's (its stone is dark red): 57% and 47% glass, the lit glass
96%, the cross 0%. glass_tests' "lead" checks each (and dumps them with GLASS_DUMP); no image data is stored.

**The tint.** Blended at `SeeThroughOpacity`, stained glass lay over the room as a coloured veil and the room kept its own
colours. Now, before the glass is blended over it, what's behind an open window's glass is multiplied by the glass's colour:
T = lerp(1, the texel's hue at full value, `SeeThroughStainedTint`, 0.8), weighted by the lead mask (the lead and frame 1),
`ZERO / SRCCOLOR`: light through coloured glass (`TintOf`, its pixel shader's twin). From inside the same: the street through
the coloured glass. It's a pass of its own (`features/SeeThroughDraw`: its own 13-instruction pixel shader, the client's
vertex shader's TEXCOORD0, the batch's texture at s0, the lead mask at s15, c223), not the stock WMO variants: their largest
uses 2032 of its 2048 slots and is unchanged. Split by window, it's drawn over the open windows' triangles; whole (the
client's indices unreadable), the see-through draw's pixels (its texkill keeps only the open windows' glass) are first
marked in the stencil's top bit, the bit cleared under the batch before and by the tint after, and the tint goes only there;
without a stencil in the depth surface there's no tint. Only stained glass (kind 2) is tinted; Northrend's amber windows are
leaded glass and blend as the human windows do. The lights panel counts the stained batches redrawn and tinted each way.

**Proofs (WARP).** glass_tests' sequence harness now takes a scene (`SequenceScene`: a WMO's group and glass batch, its
placement, the camera, its open windows): besides the inn's two, the chapel from the street (window 15 open, 13 and 17 shut),
from inside its nave (4 open, 2 and 6 shut, the street behind) and Northrend's town hall's west front (0 and 4 open, 1 shut),
each placed turned 33 degrees, through the game's order (the client's draw, the capture, the state clobbered, applied, the
redraw). Each: every pixel decided as right after the client's draw, whole and split; the state block alone every pixel
yellow; what's behind changes with the backdrop through all of the open windows' glass and through none of their lead and
frame or the shut windows (the chapel 431 of 431, 0 of 36, 0 of 1675). The tint, over a grey backdrop: every open window's
glass pixel changed (the chapel's 431 of 431, its channels' spread 82 to 122; from inside 405 of 405, 73 to 107), not one of
the lead, frame and shut windows' pixels a level apart (the stencil filled with ones first, so its clearing is what keeps
them), whole through the stencil and split alike (21 and 0 pixels apart, at the windows' edges). The tint's shader alone:
the backdrop x `TintOf` to a level over eight texels and masks. With GLASS_DUMP each scene's views by day and at night over a
room's checker (debug, tinted, untinted).

**Only the game can confirm**: the tint by day and night, in the chapel and from its nave; whether the client's depth surface
has a stencil (the whole draw's tint needs one; the panel says which way each was tinted); the abbey's frame rate (46 windows
with rooms, its nave a big room); the reveal on the existing buildings (more views open, none expected to flicker).
