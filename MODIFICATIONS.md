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
- **Tests** under `tests/`: cover tables and prints, lights, shore, storms, windows.

Shader-level GPU checks and a few offline harnesses live outside this repository for now.
