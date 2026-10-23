# Cross-pollination with Lumenveil

Lumenveil (github.com/lumenveil/Lumenveil, GPL v2, first released 2026-10-21) is a graphics mod for the 3.3.5a client
built on CoAVolFog (jealous-sound, GPL v2). It loads through its own `version.dll`, wraps the Direct3D 9 device and
redirects some of the client's draw calls. It overlaps Living Azeroth and Modern Water (fog, shafts, lamp light,
water), so the two shouldn't run together. "Queue everything that they have and we don't; the best idea here is to
cross-pollinate" (the user, 2026-10-22).

Licences: Living Azeroth and Modern Water are GPL v3. Lumenveil's code can come over only if it is "GPL v2 or later"
(not settled yet: ask them). Ideas can come over either way. Its lamp colour and flicker rules come from Everwood (MIT),
which we can use directly.

## The queue (after the see-through phases B–E)

| # | Feature | Lumenveil | Here now | Notes |
|---|---|---|---|---|
| 1 | Darker nights | darker nights, with a slider | done 2026-10-22: `NightDarkness` (0.35), through the day-night colours the storms shade; less in capital cities, where it's freezing and indoors | Lowers the night ambient and the sky, so lamps and lit windows matter more. |
| 2 | Spell light | spells, enchants and glowing projectiles light their surroundings | none | Collect light sources from spell-visual models and projectiles each frame into our point-light grid (models + terrain + cover). Cap the count; colours from the model's own lights or particle colours. |
| 3 | Wet surfaces | stone and soil darken and shine in rain, puddles mirror torches, grass doesn't | wetness field, mud | Add the shine and the puddles: specular on wet stone and soil, puddle masks in low ground from the moisture field, lamps reflected in them. |
| 4 | Belt lantern | a lantern on the player (key) | none | Small: one point light on the player at night, with a toggle. |
| 5 | Shaft memory off-screen | remembers what the camera has seen, so shafts don't flicker at the screen edge | shafts from the visible sun, terrain occlusion | Check whether ours flicker at the edges first; if they do, keep a low-resolution occlusion history. |
| 6 | Distant night lights | towns visible from far away at night | point lights to 256 yd | Light sprites or impostors for lamps beyond the light range, fading with distance fog. |
| 7 | Longer view | old continents' view distance lifted, distance fog follows; development build draws land beyond it | Modern Water's far clip, FarFog | Raise the far clip where memory allows; land beyond the view distance is a bigger job (far terrain). |
| 8 | Relief | parallax depth on walls, floors and models from height maps made in game | none | Height from the texture at runtime. The WMO shaders are nearly full (2032/2048 slots), so this needs its own pass or the new client. Theirs is off by default and glitchy on walls. |
| 9 | Per-zone looks | 37 zones tuned for dawn, day, dusk and night in clear, rain, snow and sandstorm, partly by an automated loop | AreaClimate, FogSettings, per-zone fog | A region panel for zone × time × weather, and a capture helper (see 10) to tune from screenshots. |
| 10 | Compare and snapshot keys | one key for all effects off/on; one key saves place, time, weather and camera to a file | snapshot done 2026-10-22: `SnapshotKey` (F11) and a panel button write `Logs\la-snapshots\<date>-<time>.txt`, which the WARP harnesses load as their viewpoint; the compare key not yet | Both help bug reports: a snapshot replaces guessing the camera position from screenshots. |
| 11 | Water extras | reflections that stay put as you turn, sun glitter, rain rings, lamps reflected at night | Modern Water: waves, wind, foam, ripples | Check what Modern Water already has, then add what's missing. |
| 12 | Sandstorms | per-zone sandstorm looks (and WoW Forever's Tanaris sandstorms, "phenomenal", 2026-10-21) | the server sends them (AzerothCore game_weather: Tanaris storm 15% every season, Silithus 15–25%; a desert zone's storm is a sandstorm), the client draws its light tan particles; LA knows the type (ClientWeather type 3) but only dims the shafts | Dense drifting dust in the sand's colour from the fog field, visibility falling with intensity, streaks blowing with the wind, a darker yellow-brown sky (Storm's band shading for type 3), sand settling in the surface cover, and a wind sound. Asked for 2026-10-22. |
| 13 | Doorway drifts | none (our own idea, 2026-10-23) | the surface cover is kept out of buildings' rooms (`features/CoverRooms.hpp`), so indoor floors are clean | A deliberate drift blown in through the door: a thin strip of cover drawn on the WMO floor just inside exterior portals and doorways, fading inward, placed from the portal outlines the see-through walk already knows, only where the ground outside has cover (snow, sand, ash), with its own setting. "Cool but not at the cost of glitchy indoor floor cover" (the user, 2026-10-23): clean floors come first. |

## What we can offer them

Storms with lightning and thunder (DSP), cloud decks, snow and sand footprints and ground cover, ice and crystals, the
glass system (fake rooms, lit windows on a schedule, real see-through windows from outside and inside), and video
screens (Video Cinema, AzerothPlex).
