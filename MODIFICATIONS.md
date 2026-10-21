# Modifications

This is a modified version of [Seyris Living Azeroth](https://github.com/Morfium-G/seyris-living-azeroth) by
Morfium-G, licensed under GPL-3.0 (see [LICENSE](LICENSE)). The modifications are released under the same license.

- **Who:** VIXAL-OS, with AI coding assistants (Claude and Codex).
- **When:** 7–12 October 2026.
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
  the count in `tools/see_through_survey.py`). Only Goldshire's Lion's Pride Inn for now. With `SeeThroughRedraw` 1
  (experimental; 0, the default, is the constant blend above) only the glass texels go see-through, the painted lead,
  frame and tracery stay opaque, and a window opens only where its room is really seen through it (not over its ceiling
  or through its walls); a shut window keeps its glass and fake room (20 October 2026). The lights panel has its debug
  view (each redrawn pixel coloured by what the shader decided) and reads the device's state back at the redraw
  (21 October 2026).
- **Glass** (20 October 2026): the human window textures' fourth quadrant, a smithy's forge mouth (four glowing fire
  slots), is no longer glass: no glass look, no room behind it, no lights out (`GlassMaterials.cdbc` `NotGlassU`,
  `NotGlassV`; `tools/gen_glass.py` `NOT_GLASS`). Goldshire's, Darkshire's, Lakeshire's, Westfall's and New Hearthglen's
  smithies and the Venture Co.'s log machines.
- **Tests** under `tests/`: cover tables and prints, lights, shore, storms, windows, sun shafts, glass, crystals,
  see-through windows.

Shader-level GPU checks and a few offline harnesses live outside this repository for now.
