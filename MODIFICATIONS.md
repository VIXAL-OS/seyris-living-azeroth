# Modifications

This is a modified version of [Seyris Living Azeroth](https://github.com/Morfium-G/seyris-living-azeroth) by
Morfium-G, licensed under GPL-3.0 (see [LICENSE](LICENSE)). The modifications are released under the same license.

- **Who:** VIXAL-OS, with AI coding assistants (Claude and Codex).
- **When:** 7–25 October 2026.
- **What it's based on:** upstream `main` at `b16c590`. Every change is its own commit on top of it, so the git
  history is the full record of what changed and when.
- **Target:** the 3.3.5a client (build 12340), played on ChromieCraft.

## What changed
- **Startup and compatibility** for this client, and a rain signal other WarcraftXL modules can read
  (`chromie.rain-signal` v1; the modified Modern Water module uses it for raindrops on the water).
- **Surface cover** (snow, sand, ash, mud):
  - Speed: only changed cells are uploaded, empty cover is skipped, and the light scan is spread over several frames.
  - Looks: a soft outer edge that dissolves in patches fixed to the ground, a depth pull against z-fighting far off,
    and cover textures that reach the cover's edge.
  - Sand, ash and mud rows for deserts, volcanic zones and coasts.
  - The client's own footprints are drawn on top of the cover.
  - On sand, mud, building floors and the snowy caves, a still rim of displaced ground forms around each footprint,
    following that print's own outline (the client's six footprint shapes, traced by
    `tools/gen_footprint_outlines.py`).
  - Footprints on sand the client counts as dirt (18 October 2026): Silithus's dunes, Feralas's and Ashenvale's
    sand and the dry muddy shores have ground effects of TerrainType Dirt or Soggy, where the client puts no print
    down, so their rims never formed. Where the cover there is sand, the footstep's print goes down as on sand
    (`SurfaceCoverSandPrints`).
  - No cover inside buildings (23 October 2026): the cover is drawn right after the terrain, before the buildings, so
    where a building's floor lies within its depth over the terrain it showed through the floor, and only while a way
    out was on screen (Everlook's observatory and igloos, Tanaris's goblin houses). Where the terrain lies inside a
    building's room (its interior groups' boxes, and under a surface of that room) the cover now stays out, from every
    angle; the band outside the walls where a foundation lies under the terrain keeps its cover (`features/CoverRooms.hpp`,
    `features/SurfaceCoverRooms.cpp`, the buildings noted by the hook on `Wmo.RenderGroup`). The surface cover's status
    lines go into the snapshot (F11) too.
- **Clear ice** on icy ground, ice models and WMO ice, with styles for frozen waterfalls, shoreline floes, glaciers and
  sea ice.
- **Fields:** the shore pass runs on a background worker thread.
- **Diagnostics:** the frame profiler's "other" time is split up, and a periodic status line goes to the log.
- **Lit windows at night** (17 October 2026): warm light in front of the lit windows of towns and cities and a
  soft glow on the glass, following the client's own window fade (`WindowLights.cdbc`,
  `tools/gen_window_lights.py`, `[LivingAzeroth.Windows]`).
- **Sun shafts** (17 October 2026): light shafts through trees, fog and the cloud deck's gaps, by what's in the air,
  the time of day and the weather (`env/ShaftModel`, `features/SunShafts`, `[LivingAzeroth.Shafts]`). From 18 October
  they radiate from the sun (or moon) the client draws in the sky, read from its sky each frame, not from its lower,
  stylized light direction (`env/SkyPath`).
- **Window glass** (17 October 2026): every glass window looks like glass (lit by the day from inside, stained glass
  glowing in its colours; darker with the sky in it from outside, a room behind clear panes), the sun through sunlit
  panes on the room's floor, and coloured shafts in its air (`GlassMaterials.cdbc`, `GlassPanes.cdbc`,
  `tools/gen_glass.py`, `env/GlassModel`, `features/Glass`, `features/GlassShafts`, `[LivingAzeroth.Glass]`). The
  buildings' pixel edit (`features/ModelIce`) no longer reads its matches after changing the text they point into.
- **Rooms behind the glass** (17 October 2026): each building's rooms its own tint and brightness by its kind and
  place, per window a layout (shelves, curtains, a dividing wall with a doorway, plain), and at night lamp-lit rooms
  behind the client's lit windows that go dark as the night goes on (`RoomStyles.cdbc`, `GlassBuildings.cdbc`,
  `tools/gen_glass.py`, a hook on `Wmo.RenderGroup` in `features/WmoIce`). A dark room hides the client's glow from
  the whole window, so lit leaded windows no longer sparkle at night. By day (18 October 2026) the glass seen from
  outside keeps the client's painted glass (a little darker, the sky in it) and the room behind it is only a soft hint:
  as bright as the glass, its walls a gradient, no hard edges, rooms meeting without a step.
- **Thunder through distance filters** (18 October 2026, experimental): farther thunder muffled, a little lower and
  rolling, through the client's own FMOD Ex: its sound kits' DSP chain callback (as for Death Knight voices) joins each
  layer to one of a few filter chains of our own (`env/ThunderDspModel`, `env/ThunderDsp`, `[LivingAzeroth.Storm]`
  `ThunderFilters`). Built on the SE2/FMOD findings in Morfium-G's
  [wxl-seyris-dehardcoding](https://github.com/Morfium-G/wxl-seyris-dehardcoding) (`LiveDsp.cpp`, `DspParamTable.hpp`).
- **Rain and storms** (17 October 2026): rain and wind mix the ground fog back into the units' wakes (none to speak of in
  heavy rain); no mud on paved or cobbled ground (GroundMaterial Flags 0x2, `tools/add_paved_ground.py`) and shallow
  mud ruts on Elwynn's dirt that fill in faster in rain; trench walls and rims no narrower than the near grid can draw,
  and feet flattening the rims of earlier steps; thunder through the client's own sound kits (a loudness by distance,
  from the strike's direction; Lua PlaySound as the fallback) with new recordings (`env/StormModel`). In a storm the
  fog stays lighter than the darkened sky and turns a pale blue-grey (18 October 2026). In falling snow the client's
  grey weather fog, sky and clouds turn a bright pale blue-white by day, and its fog no longer starts behind the camera
  or ends nearer than 400 yards (18 October 2026).
- **Crystals** (18 October 2026): the draenei's, Bloodmyst's, the fel-green, Netherstorm's, Shattrath's, Terokkar's,
  Crystalsong's, Dalaran's, Maraudon's, Un'Goro's, Silithus's and Sholazar's crystals look like deep, semi-transparent
  gems, as the ice does: facets inside them at depth, their colour deeper toward the core, a rim, the sun's glint and an
  inner glow stronger at night; the crystals' own see-through shells and glows are left alone (`CrystalMaterials.cdbc`,
  `tools/gen_crystals.py`, `features/Crystal`, the ice block in `features/ModelIce`, `CrystalLook`). The facets inside
  grow with each crystal's size, and deepen with it: a shard's stay small, the Exodar's giant crystals' are up to 8 times
  bigger (19 October 2026; `CrystalCellScale`).
- **See-through windows** (20 October 2026, experimental, off by default): from outside, the real modelled room behind a
  window seen through its glass, its furniture with it. A window with an interior group behind it opens like a doorway
  in the client's own portal walk (a hook on `Wmo.RenderThroughPortals`; nothing in the WMO changes), and its glass is
  drawn again after the room, blended at a constant opacity (`env/SeeThroughModel`, `features/SeeThrough`,
  `[LivingAzeroth.Glass]` `SeeThroughWindows`, `SeeThroughOpacity`; the research in `docs/r&d/see-through-windows.md`,
  the count in `tools/see_through_survey.py`). The buildings are data (`SeeThroughBuildings.cdbc`, 22 October 2026; before,
  Goldshire's Lion's Pride Inn alone): the human inns, two-storey houses, mage towers and farmhouses and Redridge's prison
  camp headquarters, 24 WMOs the survey's sweeps pick; not Stormwind, whose houses aren't modelled inside. With `SeeThroughRedraw` 1
  (the default; 0 is the constant blend above) only the glass goes see-through, every pane's inside, lit and shaded
  alike (the texture's lead mask: `GlassLead.cdbc`'s panes, the mask built at run time from the texture; 21 October
  2026), the painted lead, frame, tracery and shutters stay opaque, and a window opens only where its room is really seen
  through it (not over its ceiling or through its walls); a shut window keeps its glass and fake room (20 October 2026).
  The lights panel has its debug view (each redrawn pixel coloured by what the shader decided) and reads the device's
  state back at the redraw (21 October 2026). The capture keeps every shader constant apart from its state block, which
  put back only the pixel shader's c0..c127: in game the per-pixel redraw found no window (21 October 2026). A window
  opens where rays through points on its own glass see the room, a ray meeting nothing of it (on through a doorway)
  neutral and only one meeting it from outside against, with separate thresholds to open and to stay open and a 0.35 s
  hold, so the inn's arched window no longer flips at ordinary street angles (21 October 2026). From inside
  (`SeeThroughFromInside`, off by default, 21 October 2026): the street through the room's own windows, each one that
  faces the camera from inside and is really seen out of (its rays leave by the building's own window glass, not through
  a wall or the roof) added as one more of the client's exterior portal views, so the outdoors, its sky and its weather
  come through it as through an open door; the room's glass is drawn again over the street as from outside. A ray through
  a window that meets nothing of its room is followed on through the whole building (the next room through a doorway
  counts as seeing in, the roof or a wall from inside, or the void, against), and a majority must see in, so the dormers no
  longer open onto the void through the roof from high viewpoints (22 October 2026). Stained glass (22 October 2026): the
  Redridge chapel (Lakeshire's, Tyr's Hand's, New Hearthglen's), Northshire Abbey and New Hearthglen's, and Northrend's
  human halls with their arched windows (the barracks of Westguard, Valiance and Wintergarde Keep, the town hall, the Isle
  of Conquest's keep, Wintergarde's small house, Valgarde's inn) join the table, 33 WMOs; their lead masks are found in the texture itself,
  the curved tracery, the lead and the wooden or stone frame opaque (`GlassLead.cdbc`'s rows for them: the whole texture or
  its designs, each with its own darkness for the frame); what's seen through stained glass, the room or the street, takes
  the glass's colour (`SeeThroughStainedTint`, a pass of its own before the glass is blended over it); and the gate passes
  the inside of a thick wall round a window's opening, and the sides of a narrow one, so the chapel's tall windows open
  from 96% of street views instead of 48% (the inns' from 82% instead of 79%; the dormers still never). From inside, a
  window with the building's outer glass behind it has an opening that deep (Northshire Abbey's nave windows stand 2.7 yd
  inside its outer ones): the shell met from behind within it is passed, so the nave's windows no longer come and go as
  you walk about it ("it does flicker between fake rooms and outside"); and the tint goes by the depth the redraw's opaque
  draw writes, needing neither the client's index buffer nor a stencil, which the game's depth surface lacked (22 October
  2026). Frosted glass (22 October 2026): the room or the street behind it softly blurred (`SeeThroughFrostBlur`: the
  frame copied at a quarter of its size once a frame and blurred in a pass of its own, laid behind the open windows'
  frosted glass by the depth; frosted windows stay shut where it can't run; `SeeThroughFrosted` turns it off, and the blur at
  0 shows the room as through clear glass, 23 October), and 14 more WMOs: the dwarven houses
  (Kharanos, Thelsamar), the Taunka's longhouse, the orcs' huts, great halls, two-storey and zeppelin houses, and the
  vrykul longhouses, 47 in all. From inside, a room's glass the client draws as a transition batch (twice: lit as the
  outside, then as the inside, blended by its vertex alpha) is redrawn too: before, it stayed the client's glass, as in
  Kharanos's dwarven houses (23 October 2026).
- **See-through windows between rooms** (23 October 2026, experimental): inside Ironforge, whose whole city is one
  building of rooms and indoor streets, a shop's or a room's frosted window onto a hall (and two halls' onto their rooms)
  opens onto the room beyond it from inside where that room is seen through it, the client's walk going on into it
  through the window, the glass drawn again over it (`SeeThroughBetweenRooms`; `SeeThroughBuildings.cdbc` rows can now
  name one group of a WMO: 13 of Ironforge's, chosen by a survey of every window's views and walks). After the first
  in-game report the Great Forge's ring joined (into the shops round it, where players stand before their windows), a
  window's room is the group of the pane across its glass where it is glazed on both sides, and the frame's budget goes
  to the nearest windows first. Since 24 October the shops round the ring open out onto the forge too: their rays look as
  far as the room beyond reaches (the forge's far side lies 66 .. 180 yd off), not at most 60 yd.
- **See-through windows in Dalaran** (24 October 2026, experimental): Dalaran is one building for the whole city, so it
  opens room by room, the rooms a survey of every window's views and walks chose. From the street: Cartier & Co., two
  upper rooms of the Legerdemain Lounge, the Threads of Fate's side room and the Purple Parlor. From inside onto the
  street: Cartier & Co., the Violet Gate's two halls and the Agronomical Apothecary. Between rooms: the Underbelly's
  Circle of Wills into Cantrips & Crows. Stained glass is tinted (where `GlassLead.cdbc` has no row for a stained texture,
  its lead mask now comes from the whole texture) and frosted glass lies over the blurred copy. `SeeThroughBuildings.cdbc`'s
  group rows can now open a room from outside; from outside the frame's budget goes to the nearest windows first, and
  a city's windows into other rooms aren't even projected. The redraw's sixteen windows a glass batch take in the open
  window nearest the camera.
- **See-through windows from inside at a slant** (24 October 2026): a room's window in a thick wall now opens onto the
  street where the view through it meets the sides of its own deep opening (Northshire Abbey's nave from an archway: "the
  windows *mostly* work except at weird angles"); the stone of the opening is drawn behind the glass, the street past it.
- **Window light that stays put** (24 October 2026): the sun through windows onto the floor and into the air took the
  panes nearest the camera, so a camera swinging round the player swapped them and a patch of light came and went
  ("Dalaran windows cast light ... but only in certain angles", in the Silver Enclave). It now takes the panes that light
  the most where the player stands.
- **Dalaran's window surrounds are stone again** (24 October 2026): the gold-framed stone round Dalaran's stained windows
  (and its blind niches) was taken for frosted glass, so from inside the Violet Gate the borders went see-through and,
  with sixteen windows a frame, the stained panes in them came and went ("the Violet Gate windows' *borders* are
  transparent when seen from the inside, and the actual glass flickers"). They are no windows now (no room behind them,
  nothing seen through them): the Violet Gate's halls have their four stained panes each, steadily open and tinted. The
  floor light through them, as in the Silver Enclave's hall, stays. Without the stone counted, more of Dalaran's rooms
  open from the street: Curiosities & Moore, the Threads of Fate and the Silver Enclave's hall. Their upstairs rooms and
  the Legerdemain Lounge's hall stay shut from outside: a window of each opens only in a narrow cone of views, so a camera
  swinging round a player on Curiosities' walkway made it flicker ("Curiosities' outside windows seem to flicker a bit").
- **Window light that fades** (24 October 2026): a patch of sun on the floor (and a coloured shaft) fades in or out over
  0.4 s when its window joins or leaves the light's set, instead of popping, and a window takes another's place only when
  it lights the player's spot half as well again; sixteen windows' patches at once, not twelve ("lots of pop-in when i
  move", in Stormwind's Wizard's Sanctum).
- **Stormwind's Trade District shops** (25 October 2026): from the Trade District's street, three of its shops are seen
  through their windows, each shop's windows together (Stormwind's streets are indoor spaces to the game, so these open
  as Ironforge's shops do, between rooms, with `SeeThroughFromInside` and `SeeThroughBetweenRooms`). The other shops'
  windows look at the back of a wall a few yards behind the glass and stay as they were. In Stormwind and the other big
  cities the fake rooms behind the windows are now lit and dark shop by shop, not a whole district at once.
- **A room's windows as one** (24 October 2026): "in general can we have all windows in a room behave the same?" Seen
  through from outside or inside, a room's windows now open together, and only when the room is really seen through every
  one of them; otherwise they all keep their glass (`WholeRooms`, on by default). Behind the glass, in any building, a
  room's fake rooms are lit or dark together, furnished alike, and go dark together as the night goes on, their window
  lights with them; so are the panes of one window (a vrykul tower's window at Valkyrion showed a lit room behind one pane
  and a dark one behind the other). Rooms that would come and go as one stay shut: in Dalaran, Curiosities & Moore and the
  Threads of Fate's main rooms and upper floors, Cartier & Co., the Violet Gate, the Purple Parlor and Vargoth's Retreat
  from outside ("Threads of Fate is actually more flickery now"), and the orcs' great halls. A snapshot now names the
  building actually in front of the camera, not the last one the see-through windows looked at.
- **Dalaran's rooms open window by window** (24 October 2026): a room opened from the street with all its windows, so one
  window that flickers kept a whole room shut ("the Violet Gate windows work fine from the inside but not from the
  outside"). The see-through table can now keep single windows shut while their room opens. The Violet Gate's two halls
  now open from the street too, and so do Curiosities & Moore's and the Threads of Fate's upstairs rooms, the
  Legerdemain Lounge's hall and Archmage Vargoth's Retreat. Twenty windows that flicker as the camera swings round the
  player stay the client's glass, three of them in the Purple Parlor. The Threads of Fate's side room stays as it is.
- **Darker nights** (22 October 2026): the night's ambient light, sky, clouds and water darker so lamps and lit windows
  matter (`[LivingAzeroth.Storm]` `NightDarkness`, 0.35; 0 = the client's nights exactly), through the client's own
  day-night colours as the storms shade them, so the fog, the sky and the models stay consistent; half as much in capital
  cities and where it's freezing, under a third indoors.
- **Many small flames together** (23 October 2026): a ring of candles no longer blows the floor out to yellow-white
  (Felwood's Shadow Hold: 223 candles round a pit). Separate small flames merged together keep the square root of their
  summed light, and every shader (terrain, surface cover, models, WMOs, WMO interiors) compresses our summed light
  smoothly above a knee, the hue kept; one light, and Goldshire's inn and Ironforge, stay within a few percent of before.
  The snapshot (F11) also writes the lights' settings and counts and the light at the player's feet and at the camera.
- **Tree shadows that follow the wind** (24 October 2026): `wxl-treewind` sways the trees, but at shadow quality 3-4 the
  client keeps the shadows of everything static in three cached maps round the player and draws one again only after
  the player has walked 2, 4 or 32 yards, a ninth of it a frame, so the trees' shadows held a pose and stepped as you
  walked ("they only move with a low framerate and only when my character moves"). Right after the client's own
  `CShadowCache::RenderBands`, the near map's shown texture is drawn again, whole, through the client's own frustum,
  query and render callbacks, centred where the client drew it, while a tree or bush casts into it (`TreeShadows`,
  `TreeShadowsRate`; the 160-yard map too with `TreeShadowsFar`), every client byte it relies on checked first
  (`env/TreeShadowModel.hpp`, `features/TreeShadows.cpp`); its status in the panel, the 30-second log line and the
  snapshot; `tests/treeshadows`.
- **Spell lights** (24 October 2026, experimental, off by default): spells, glowing projectiles and weapon enchants light
  what's round them through our point lights (the terrain, the cover, models and buildings), in the effect's own colours
  (its own light, else its particles', ribbons' and geometry's as they look, leant toward its school's colour), as big
  and bright as the effect, following it every frame and fading out with it (`[LivingAzeroth.SpellLights]`). The spell
  visuals are found read only, every frame: the M2 scene's newest models at the head of its list, the client's missile
  lists and the light scan's walk, by their files (`SpellVisualEffectName.dbc`, `ItemVisualEffects.dbc`) and creation
  flags; every client instruction it relies on is checked first. Merged and overlapping spell lights keep the square
  root of their summed light (the candle ring's rule), so an area spell's field doesn't wash the ground out, and a cap
  (the nearest and brightest 24) keeps raids cheap: about 0.6 ms a frame with 300 spell visuals near, 0.2 ms in a
  dungeon pull. Its status in the lights panel, the log line, the snapshot and the frame profiler;
  `tests/spelllights`. From Lumenveil's list, the idea only (its code is GPL v2).
  - Steady and only spells (24 October 2026). Spell lights flickered though the spells didn't, and a tracked
    Icethorn in the Storm Peaks lit its snow and tree bright blue-white. The light follows its effect over wall time:
    each emitter's live particles are smoothed over a quarter of their life (the count steps by one as particles are
    born and die), the brightness eases up and down (still flaring at once in a spell's first moments), the hue slower;
    a model the client animates only every few frames counts as drawn between them. Merged and overlapping lights add in
    quadrature, so a faint one coming or going barely moves a bright one; two merged stay merged a little past the merge
    distance; the cap keeps what it kept unless another is clearly better, and a light leaving or joining the kept set
    fades out or in; lights fade out toward the range's end. In env/Lights spell lights no longer merge with torches,
    never leave the pool for a nearer one, and have their own 16 places in each cell of the grid, which their selection
    never overfills (a better one in a full cell hurries the weakest out in a few frames): a full cell of torches no
    longer drops a spell light, nor a spell light a torch. Only spell visuals, missiles and enchants are spell lights:
    files under `spells\`, not the client's own effects (`SpellVisualEffectName.dbc` rows named HARDCODED: the loot
    sparkle, `Particles\LootFX.m2`, the client hangs on a tracked herb or ore node, a chest or a quest object, which was
    the Icethorn's light; breath, level-up), not `particles\` (footsteps, blood, the sparkle), not the doodads,
    creatures and weapons the table also lists (wisps, mage portals, the Saronite deposit), not a game object's own
    model (`GameObjectDisplayInfo.dbc`: instance portals) unless it hangs on a unit; those keep the light scan's doodad
    rules. Anything flying as a missile still counts, by its particles and own lights alone unless its file is a spell's.
- **Snapshot key** (22 October 2026): one key (`SnapshotKey`, F11) or a panel button writes where you are, the camera
  (in the nearest building's own space too), the time, the weather, the settings that matter, the frame rate and the
  panels' status to `Logs\la-snapshots\` in the game folder, for bug reports; the glass and see-through test harnesses
  draw a snapshot's view again (`GLASS_SNAPSHOT`, `SEETHROUGH_SNAPSHOT`).
- **Glass** (20 October 2026): the human window textures' fourth quadrant, a smithy's forge mouth (four glowing fire
  slots), is no longer glass: no glass look, no room behind it, no lights out (`GlassMaterials.cdbc` `NotGlassU`,
  `NotGlassV`; `tools/gen_glass.py` `NOT_GLASS`). Goldshire's, Darkshire's, Lakeshire's, Westfall's and New Hearthglen's
  smithies and the Venture Co.'s log machines.
- **Panel text** (22 October 2026): the panels' lines wrap at the window's edge instead of running off it where the
  WarcraftXL core has `UiTextWrapped` (core v1.1.272 appended it to the extension API; `debug/UiText.hpp` checks the
  table's `structSize` first), else they're drawn as before. The lines themselves are unchanged.
- **Memory readout** (25 October 2026, before HD texture packs): what uses the 32-bit client's address space, from
  inside the process: the address space walked a slice a frame (free space, the largest free block, each image and
  mapped file, private memory and its largest allocations), Direct3D's texture memory, the client's own texture, pool,
  M2 and WMO caches (read as the decomp lays them out, its instructions checked first), and Living Azeroth's own heap
  and Direct3D resources by feature (`operator new` counted in this module alone). On the overview page, in the log
  every 30 s with the zone, and in snapshots; a chat warning and a snapshot when it runs low
  (`[LivingAzeroth.Memory]`).
- **Tests** under `tests/`: cover tables and prints, lights, shore, storms, windows, sun shafts, glass, crystals,
  see-through windows, tree shadows, spell lights, the memory readout.

Shader-level GPU checks and a few offline harnesses live outside this repository for now.
