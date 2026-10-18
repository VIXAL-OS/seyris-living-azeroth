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
- **Clear ice** on icy ground, ice models and WMO ice, with styles for frozen waterfalls, shoreline floes, glaciers and
  sea ice.
- **Fields:** the shore pass runs on a background worker thread.
- **Diagnostics:** the frame profiler's "other" time is split up, and a periodic status line goes to the log.
- **Lit windows at night** (17 October 2026): warm light in front of the lit windows of towns and cities and a
  soft glow on the glass, following the client's own window fade (`WindowLights.cdbc`,
  `tools/gen_window_lights.py`, `[LivingAzeroth.Windows]`).
- **Sun shafts** (17 October 2026): light shafts through trees, fog and the cloud deck's gaps, by what's in the air,
  the time of day and the weather (`env/ShaftModel`, `features/SunShafts`, `[LivingAzeroth.Shafts]`).
- **Window glass** (17 October 2026): every glass window looks like glass (lit by the day from inside, stained glass
  glowing in its colours; darker with the sky in it from outside, a room behind clear panes), the sun through sunlit
  panes on the room's floor, and coloured shafts in its air (`GlassMaterials.cdbc`, `GlassPanes.cdbc`,
  `tools/gen_glass.py`, `env/GlassModel`, `features/Glass`, `features/GlassShafts`, `[LivingAzeroth.Glass]`). The
  buildings' pixel edit (`features/ModelIce`) no longer reads its matches after changing the text they point into.
- **Rooms behind the glass** (17 October 2026): each building's rooms its own tint and brightness by its kind and
  place, per window a layout (shelves, curtains, a dividing wall with a doorway, plain), and at night lamp-lit rooms
  behind the client's lit windows that go dark as the night goes on (`RoomStyles.cdbc`, `GlassBuildings.cdbc`,
  `tools/gen_glass.py`, a hook on `Wmo.RenderGroup` in `features/WmoIce`).
- **Tests** under `tests/`: cover tables and prints, lights, shore, storms, windows, sun shafts, glass.

Shader-level GPU checks and a few offline harnesses live outside this repository for now.
