# Config (`WarcraftXL.ini`)

Read through `wxl-seyris-tools` (needs settings 1.1.0) from `WTF\WXL\WarcraftXL.ini`, or from
`WarcraftXL.ini` in the client root if one exists there. Section `[LivingAzeroth]`, and the
sub-sections `[LivingAzeroth.Fog]`, `[LivingAzeroth.FogWake]`, `[LivingAzeroth.Storm]`, `[LivingAzeroth.Windows]`,
`[LivingAzeroth.Shafts]`, `[LivingAzeroth.Glass]`, `[LivingAzeroth.SpellLights]`, `[LivingAzeroth.Memory]`, `[LivingAzeroth.Lantern]` and
`[LivingAzeroth.DistantLights]` (an ini has no real nesting; the dotted names keep them together). Missing keys are written with their default and a comment on
first launch.

The ini sets what each launch starts with. The debug panels (F9) still change everything at
runtime, without writing back. Without `wxl-seyris-tools` the defaults below apply and the log
says why.

| Key | Default | Meaning |
|---|---|---|
| InstancedGrass | 1 | Draw grass with the instanced renderer (much faster at long grass distances). 0 = the client's own path. |
| InstancedGrassBatched | 1 | Instanced grass draws all visible grass layers together at the end of the grass pass (much less CPU time). 0 = one layer at a time. Turns itself off if the GPU can't read float textures in vertex shaders. |
| InstancedGrassBuildBudgetMs | 3 | Milliseconds per frame spent preparing grass coming into view; the rest is drawn the client's way until a later frame. |
| InstancedGrassMemoryMB | 256 | Most memory (MB) the instanced grass may use. It shares the client's 32-bit address space; too high can crash the client. At the limit, grass not seen for about a second is dropped. |
| GrassDensityMaxMultiplier | 4 | Instanced grass only: your limit on grass density (plants drawn per placed plant, 1 = never denser, up to 8). How dense grass is comes from `GroundEffectDoodadDensity.cdbc`; this only caps it, like a graphics setting. |
| GrassDensityMaxRadius | 150 | Your limit on how far around you grass is made denser, in yards. |
| GrassDensityMaxSpread | 3 | Your limit on how far an extra plant may move from its plant, in yards. |
| GrassThinning | 1 | Instanced grass only: far grass draws fewer plants (an even sample), which saves a lot of GPU time at long grass distances. |
| GrassThinStart | 150 | Yards from the camera within which every plant is drawn. |
| GrassThinEnd | 800 | Yards from the camera where thinning reaches `GrassThinMinShare`. |
| GrassThinMinShare | 0.25 | Share of plants drawn at `GrassThinEnd` and beyond (0.01 to 1). |
| GrassDistanceCap | 10000 | Highest grass distance (`groundEffectDist`) the client accepts. The exe's own cap is 140. Only raised, never lowered: if the exe or another module already allows at least this much, it's left alone. |
| AntiAliasing | 1 | Our own anti-aliasing (FXAA), drawn before the UI. It only runs with the client's multisampling off, which readable depth (for upcoming effects like fog) needs anyway. |
| AntiAliasingSubpixel | 0.75 | FXAA subpixel smoothing, 0 (sharpest) .. 1 (softest). |
| GrassMotion | 1 | Grass sways in the wind and parts around characters. |
| GrassSway | 0.35 | Grass tip movement in yards at full wind. |
| GrassFlutter | 0.08 | Extra per-blade shimmer in yards. |
| GrassFlutterSpeed | 3 | Shimmer speed in radians per second. |
| GrassStiffBase | 0.15 | Bottom fraction of each blade that never moves (0..0.9). |
| GrassPushStrength | 0.5 | How far grass leans away from characters, in yards. |
| GrassPushRadiusScale | 2 | Push radius as a multiple of a character's bounding radius. |
| GrassMinPushRadius | 0.5 | Smallest push radius in yards. |
| GrassMountedRadiusScale | 2 | Extra push radius factor while mounted. |
| SurfaceCover | 1 | Snow (and other) cover on the ground that characters sink into and carve trenches in. Where and how comes from `SurfaceCover.cdbc`. |
| SurfaceCoverLevels | 5 | How far the cover reaches: 1..5 rings of 40, 80, 160, 320, 640 yards. |
| SurfaceCoverTrenches | 1 | Characters and creatures carve trenches in the cover. |
| SurfaceCoverDepth | 1 | Your multiplier on the cover depth from `SurfaceCover.cdbc` (1 = as the table says). |
| SurfaceCoverFillBudgetMs | 2 | Milliseconds per frame spent filling the cover around you (on login, teleports, moving). Lower = smoother, slower to appear. |
| SurfaceCoverLiftEffects | 1 | Effects at units' feet (auras and other spell effects on the Base attachment) sit on top of the cover instead of under it (within 40 yards of you). |
| SurfaceCoverTrenchSoftness | 1.5 | No trench wall or rim narrower than this many 0.25-yard cells, or walked trenches get jagged, stair-stepped edges (0 = the original widths, 0..4). |
| SurfaceCoverRainFillsRuts | 0.75 | How much faster mud ruts fill in in heavy rain, 0..1 (0.75 = in a quarter of their time; 0 = rain changes nothing). |
| SurfaceCoverSandPrints | 1 | Footprints, and the sand rims around them, on sand the game counts as dirt (Silithus, Feralas, Ashenvale, much of Darkshore, dry muddy shores), as in Tanaris. Only the prints: footstep sounds stay as they are. |
| TerrainWetness | 1 | The terrain itself looks darker where the ground is wet (rain, water nearby). The client's own terrain shaders are patched for it when they're created. |
| TerrainWetnessStrength | 0.45 | How much darker soaked ground gets, 0..1: the terrain and the surface cover alike. |
| CrystalLook | 1 | Crystals look like deep, semi-transparent gems, as the ice does: facets and sparkles inside them that shift as you move, their colour deeper toward the core, a bright rim, the sun's glint, and a soft inner glow in their colour, stronger at night. The draenei's (Azuremyst, Bloodmyst, the Exodar, Tempest Keep), the fel-green ones of Blade's Edge, Hellfire and Shadowmoon, Netherstorm's gems, Shattrath's, Terokkar's, Crystalsong's, Dalaran's and the caves', Maraudon's, Un'Goro's, Silithus's and Sholazar's, jewels (`CrystalMaterials.cdbc`; needs dynamic shadows on). Crystals already see-through or glowing stay as they are, and the ice keeps the ice. The depth is faked: they still draw opaque (see features/Crystal.hpp for why). |
| CrystalStrength | 1 | How fully crystals take that look, 0..1. |
| CrystalGlow | 1 | Your multiplier on the crystals' inner glow (0 = none, up to 3). |
| CrystalCellScale | 1 | Your multiplier on the size of the facets inside crystals, 0.25..4. They already grow with the crystal: a hand-sized shard keeps small ones, a building-sized crystal (the Exodar's giant ones) gets them up to 8 times bigger, and deeper; they follow each crystal's own size, never the camera's distance. |
| WindShelter | 1 | Roofs and overhangs shelter the ground below from wind. |
| WindLee | 1 | Walls and cliffs cast a wind shadow on their downwind side. |
| TreeShadows | 1 | Tree shadows follow the wind (`wxl-treewind`'s sway). At shadow quality (`extShadowQuality`) 3-4 the client caches the shadows of everything static, nearly every tree among them, in maps round you and draws one again only after you've walked 2 yards (the 40-yard map), 4 (160) or 32 (640), a ninth of it a frame, so a swaying tree's shadow keeps a pose and steps as you walk. This draws the near map (40 yards) again, whole, right after the client's own, through the client's own shadow code, while a tree or bush casts into it (looked for twice a second otherwise). Quality 5 redraws every map every frame itself, at a much higher cost; 0-2 have no tree shadows, and without `wxl-treewind` nothing sways, so it does nothing. The F9 "tree shadows" panel shows what it does and its cost; the profiler row is "ours: tree shadows" (its model and building draws count under "shadow maps"). |
| TreeShadowsRate | 30 | Most redraws a second of the near trees' shadow map (0 = every frame). |
| TreeShadowsFar | 0 | The trees 40 to 160 yards away follow the wind too: their shadow map is drawn again as well (16 times the area of the near one, so it costs more). |
| TreeShadowsFarRate | 10 | Most redraws a second of the far trees' shadow map (0 = every frame). |
| DebugWindArrows | 0 | Debug: draw the wind as arrows around the player. |
| SnapshotKey | 122 | The key that writes a snapshot for a bug report (2026-10-22, "one key (and a panel button) that writes your exact situation to a file so bug reports don't need guessing"): where you are (map, zone, area, position, facing), the camera (its position, where it looks, its field of view, and in the nearest building's own space too: the building the camera is in, or the nearest one the see-through walks visited, with the windows open there), the time of day, the server's weather and Living Azeroth's, the settings that matter (see-through, fog, storm, glass), the frame rate and the see-through, fog and glass status lines, to `Logs\la-snapshots\<date>-<time>.txt` in the game folder, announced in chat. A Windows virtual-key code: 122 = F11, 123 = F12, 0 = no key; the overview page and the see-through section have a button too. The key still reaches the game. glass_tests (`GLASS_SNAPSHOT`) and seethrough_tests (`SEETHROUGH_SNAPSHOT`) draw a snapshot's view again. |
| CompareKey | 119 | The compare key (2026-10-25, from Lumenveil's list: "one key for all effects off/on"): one press switches every Living Azeroth visual effect off, so you see the scene as the client draws it, and the next press switches them back on; a line in chat says which, the snapshot (F11) and the 30-second log line record it. Each effect goes off through its own switch, as its panel's checkbox would (the ground fog and fog wake, storms, darker nights, our point lights with the window lights, spell lights and the lantern on them, the window glow, distant lights, sun shafts, glass and see-through windows, crystals, clear ice, waterfall hats, wet ground, the surface cover, tree shadows, grass motion, denser and thinned grass, the doodad fade, anti-aliasing, mount wings' depth: `debug/Compare.cpp`), so each runs its own off path. The values it found come back on the second press; a switch you changed in a panel meanwhile keeps your change. Nothing is read from or written to this file by the key: your saved settings stay as they are. The instanced grass renderer stays on (it draws the client's own plants alike), and so does the raised grass distance (a patch of the client's code at start-up). A Windows virtual-key code: 119 = F8 (the client binds nothing to it; the core's panel is F9, the snapshot F11), 0 = no key; the overview page has a button too. The key still reaches the game. |

## `[LivingAzeroth.Fog]`: the ground fog renderer

Where fog forms, and each kind's density, thickness, colour and so on, come from
`FogSettings.cdbc` ([cdbc-tables.md](cdbc-tables.md)); these are your limits and quality.

| Key | Default | Meaning |
|---|---|---|
| Enabled | 1 | Draw the ground fog. |
| DensityScale | 1 | Your multiplier on every fog kind's density (1 = as `FogSettings.cdbc` says). |
| MaxDistance | 250 | How far the fog is drawn, in yards. |
| FarFog | 1 | Beyond 128 yards the fog continues at each zone's typical level (no wall where the near fog ends). |
| HalfResolution | 1 | Draw the fog at half resolution, upscaled by depth (sharp at silhouettes): about a quarter of the GPU time. |
| StepBudget | 48 | Steps per pixel through the fog (quality, not the look): more = smoother far away, slower. |
| StepLength | 0.25 | Step length inside the fog near you, in yards (grows with distance). Smaller = finer, slower. |
| Dither | 0 | 0 = smooth (may show faint bands) .. 1 = grainy. |
| FloorReach | 2 | Light fog starts on the lowest ground this many 2-yard cells around (0..4): higher keeps it off walls, thins it on slopes. |
| Patchiness | 0.6 | How patchy the fog is, 0 (smooth layers) .. 1. |
| PatchSize | 64 | Size of the patch pattern's tile in yards (the patches are about 1/8 of it). |
| Brightness | 1 | Your multiplier on the fog's colour brightness. |
| Lights | 1 | Our lights (torches, braziers, lamps) light the fog around them. |
| LightStrength | 1 | Your multiplier on our lights' light in the fog. |
| LightRange | 128 | Lights farther than this from the camera (yards) don't light the fog. Lower = cheaper. |
| SmallDoodadsUseNextDistance | 1 | Doodads with a radius under 1 yd (mugs, plates) use the next size's view distance instead of a fixed 30 yd (more doodads drawn). |
| DoodadFadeFromCategory | 3 | Doodads of size categories below this don't fade in but pop in, so the ground fog covers them (fading ones show unfogged): 0 = stock, 5 = none fade. |
| HandoverWidth | 64 | Yards over which the near fog fades into the far fog at the near field's edge (~128 yd): wider = softer seam, less near detail. |
| BeyondMaxDistance | 1 | Past MaxDistance, the far fog continues to the terrain drawn there (one long step, cheap). |
| PoolSoftness | 0.6 | How soft heavy fog's edges are, 0 (crisp top, sharp sides) .. 1: its top blurs over this share of its depth, and pools thinner than this many yards fade out. |
| PoolSpeed | 1 | How fast heavy fog (each kind's Weight share) runs downhill into hollows: 1 = default, 0 = it stays where it formed. The light share never runs off. |
| SteepStart | 30 | Degrees: from this slope a growing share of the ground-hugging fog (mist, area fog, haze) runs off; it doesn't hang on steep ground. Water fog and steam keep their own weight. |
| SteepFull | 45 | Degrees: from this slope all of it runs off. |
| WaterWritesDepth | 1 | While fog is drawn, water writes depth so lakes are fogged at their surface (not their bed). |

## `[LivingAzeroth.FogWake]`: characters and creatures pushing the fog aside

| Key | Default | Meaning |
|---|---|---|
| Enabled | 1 | Characters and creatures push the ground fog aside and leave a swirling trail. |
| PushMoving | 0.9 | How much fog a moving unit pushes out, 0..1. |
| PushStanding | 0.35 | How much fog a standing unit pushes out, 0..1. |
| FillSeconds | 3 | Seconds until a trail fills back in. |
| Radius | 2.25 | Wake radius as a multiple of the unit's radius (at least 0.6 yards). |
| Widening | 1 | How much the trail widens as it fades, times its radius. |
| Height | 1 | How high the push reaches, as a multiple of the unit's height. |
| Rim | 0.25 | Fog piled up at the wake's edge, as a share of the push. |
| Swirl | 1 | How far the fog's pattern is dragged and curled along the path. |
| RainMixing | 1 | Rain mixes the fog back into a wake, 0..1: at 1 heavy rain leaves almost no wake (a twentieth of the push, filling in five times as fast; light rain a little less of one), 0 = rain changes nothing. Calm, dry fog is unaffected. |
| WindMixing | 0.5 | A fresh to strong wind at the ground mixes the fog back too, 0..1 (calm and light winds change nothing). |

The grass distance itself is still the client's own setting (`groundEffectDist`, options or
console); the cap only decides how high it may go.

## `[LivingAzeroth.Storm]`: rain building up into thunder

The server leads: rain grades below 0.40 are light rain (the build-up), below 0.70 medium, from
0.70 heavy (AzerothCore's cut-offs). Light and medium rain darken the sky and the light, bring
gusts and haze and the odd flash in the clouds, more the heavier the rain; heavy rain brings
strikes.

The fog doesn't darken with the sky: rain and mist scatter light, so the air near you stays lighter
and cooler than the sky above, a pale blue-grey veil, bright by day and dim at night
(`StormFogBrightness`, `StormFogCool`). The client's distance fog, the ground fog and the haze all
take this one colour.

Snow: the client shows the same weather light in snow as in rain. Where no zone light reaches (the
west of Winterspring by Frostfire Hot Springs, parts of Dun Morogh) that is the continent's default
one, a flat grey (fog #525452, the sky #6F6F6F to #525352) whose fog ends at 278 yards and starts 139
yards behind you: half the view gone at 70 yards, a dark grey wall over the snow. In falling snow
those greys turn a pale blue-white, bright by day (#B0B9C1 for the fog at the defaults) and as the
client has them at night, the sky and its clouds with them (`SnowFogBrightness`, `SnowFogWhite`),
and the fog starts at you and ends no nearer than `SnowFogDistance`. A zone's own coloured weather
light keeps its colour (Winterspring's lilac, Dun Morogh's sky blue). The haze still thickens with
heavier snow, in the same bright colour.

Thunder: a near strike (under 250 yards) cracks like the Goblin weather machine's lightning half the
time (else Shaman thunder or Call Lightning's impact; under 150 yards a second crack under it, as loud:
one recording can't play above full volume, two together are louder), a farther one is the Blasted
Lands bolt, the farthest (from 1200 yards) only a deep rumble, which also rolls after the nearer ones.
Through the client's sound kits each plays at a loudness by distance ((190 / yards)^0.38: 0.9 at 250
yards, 0.65 at 600, 0.53 at 1000; x `ThunderVolume`, never above full) from the strike's
direction; if that call isn't available, Lua PlaySound plays the row nearest that loudness (the log
says which). The storm panel auditions every recording.

Experimental, `ThunderFilters`: thunder through distance filters in the client's own sound engine (FMOD Ex), the way
it gives Death Knight voices their filter. The farther a strike, the more muffled (a low-pass from 20 kHz down to
about 900 Hz at 2000 yards), the lower (pitch 0.88 at 2000 yards, by the sound's own playback rate, so a far rumble
also lasts a little longer) and the more it rolls (an echo from 200 yards out, 150 to 400 ms apart and decaying by 0.3
to 0.5; the rumble's longer, 250 to 450 ms and 0.45 to 0.6, at every distance). Cracks within 150 yards stay crisp:
no filter at all. `FilterStrength` scales how far the low-pass and the pitch-down go, `EchoAmount` the echo's
repeats. Six filter chains are kept and shared, each switched off once its echo has died away. It follows the client's
own `Sound_EnableDSPEffects`. Everything it relies on in the client is checked byte for byte first; if anything
differs, FMOD refuses, every chain is busy or anything faults, thunder plays unfiltered as before and the log says
why. The storm panel's audition plays a recording "as if at" the slider's distance, with or without its filters.

| Key | Default | Meaning |
|---|---|---|
| Enabled | 1 | Rain builds up into storms. |
| Darkness | 0.35 | How much darker the sky and the light get in full storm, 0..1 (less in lighter rain). |
| NightDarkness | 0.35 | How much darker nights are, 0..1 at full night, so lamps and lit windows matter (2026-10-22, "darker night ambient and sky so lamps and lit windows matter, with a slider"): the night's ambient light, sky, clouds and water darker and the fog a little, through the client's own day-night colours (the band hook the storms shade), so the fog, the sky and the models stay consistent. Each colour by its weight: the ambient fully (0.35: 65% of the client's), the sky 0.6 .. 0.85 (top most), the moon's direct light, clouds and water 0.6 .. 0.7, the fog 0.5, the sun's colours not at all. By the sky's night (none by day, half at dusk); half as much in a capital city and its parts (AreaTable's capital flags: lamps everywhere already) and where it's freezing (from 2 degC down to half at -4: snow lights the night); indoors (the client's verdict, smoothed) 0.3 of it: a room keeps its own light. Storms on or off. The storm panel shows it now and each part. 0 = the client's nights exactly. |
| Flashes | 1 | Flashes in the clouds (the sky brightens, no bolt), from light rain on. |
| FlashStrength | 1 | Your multiplier on every flash's brightness. |
| Frequency | 1 | Your multiplier on how often flashes and strikes come (1 = about one a minute in light rain, every 10-20 s in heavy rain). |
| Thunder | 1 | Heavy rain: lightning strikes, lighting the sky and (near ones) the ground. |
| ThunderSounds | 1 | A strike's crack, after the sound's travel time (about 3 s per km), quieter the farther. |
| ThunderSoundDistance | 1600 | Strikes farther than this (yards) are silent. |
| ThunderVolume | 1.5 | Your multiplier on the thunder's loudness (at 1.5 a strike within 600 yards plays its recording at full volume, one at 1000 yards at about 80%). No recording plays above full volume, so near strikes (under 150 yards) layer a second crack under the first. |
| ThunderRumble | 1 | A deep rumble rolls after near and mid cracks, and is all you hear of very far strikes. |
| ThunderNative | 1 | Play thunder through the client's sound kits: a loudness per distance, from the strike's direction. 0 = Lua PlaySound (fixed volumes, no direction). |
| StrikeLight | 1 | Your multiplier on the point light at a strike within light range, 0 = none. |
| Gusts | 1 | Your multiplier on the storm's extra gusts, 0 = none. |
| Haze | 0.3 | Extra haze in full storm, 0..1 (on top of the rain's own), 0 = none. |
| StormFogBrightness | 1.3 | The fog in full storm by day (the distance fog; the ground fog and the haze take its colour), x the client's own fog colour's brightness: rain lights the air. At night 3/4 of this. The storm's darkening never takes the fog below it. |
| StormFogCool | 0.6 | How much of the storm fog's colour gives way to a pale blue-grey in full storm, 0..1 (0 = its own colour, as bright). |
| SnowFogBrightness | 2.2 | Falling snow by day: the client's grey weather fog, sky and clouds, x their own brightness (up to a luminance of 0.75; at night as they are). A zone's own coloured weather light keeps its colour. 1 = the client's grey. |
| SnowFogWhite | 0.6 | How much of that grey gives way to a pale blue-white in snow, 0..1 (0 = its own grey, as bright). |
| SnowFogDistance | 400 | In snow the client's distance fog ends no nearer than this (yards) and starts no nearer than the camera (its weather fog ends at 278 and starts 139 behind you). Kalimdor and the Eastern Kingdoms (Outland and Northrend fog by density); 0 = the client's own. |
| Bolts | 0 | Experimental: a lightning bolt model at strikes (our own models in the client's scene). |
| BoltSize | 6 | The bolt's size, x Call Lightning's (about 11.5 yards tall). |
| BoltRange | 900 | Strikes farther than this (yards) get no bolt model. |
| Scorch | 0 | Experimental: a scorch mark on the terrain where a near strike lands. |
| ScorchSeconds | 45 | Seconds the scorch mark takes to fade. |
| ThunderFilters | 1 | Experimental: thunder through distance filters in the client's own sound engine: farther strikes muffled, a little lower and rolling; near cracks stay crisp. Needs `ThunderNative`. Anything unexpected and thunder plays unfiltered (the log says why). |
| FilterStrength | 1 | Your multiplier on how far the low-pass and the pitch-down go with distance, 0..2 (0 = none, 2 = twice as far). |
| EchoAmount | 1 | Your multiplier on the thunder's echo (its repeats' loudness), 0..2 (0 = none). |

## `[LivingAzeroth.Windows]`: lit windows at night

The lit windows of towns and cities (`WindowLights.cdbc`, [cdbc-tables.md](cdbc-tables.md)) cast a
warm light on the ground and the walls in front of them, and their glass glows. Both follow the
client's own lit windows: full from 21:30 to 06:00, fading over the hour before and after, off by
day. And they follow the rooms behind the glass (`[LivingAzeroth.Glass]`): as those go dark through
the night, a window whose rooms are dark stops lighting the street, and a light over a row of windows
dims as its rooms go out one by one (`WindowRooms.cdbc`: which windows each light's glass is in, found
as the shader finds them, with its hash and clock, so the light goes out with the room you see). A room
going dark or lit again fades over 1.5 s, the light and the glow with it; after a jump of the climate
panel's time every room it crossed fades together, and while its slider is being dragged they wait
for it to stop. Lit frosted glass goes dark the same way (`[LivingAzeroth.Glass]` FrostedLightsOut),
and a lit one's light flickers in time with the hearth behind its glass (HearthFlicker). Stained
glass keeps its light all night. The lights panel's
**Windows** section changes all of this at runtime and shows the night factor, the client's Full
Screen Glow, what's drawn, and how many lights near are lit, dimmed or dark now.

| Key | Default | Meaning |
|---|---|---|
| Lights | 1 | Warm light spills from the lit windows at night. |
| LightStrength | 1 | Your multiplier on the window lights' brightness. |
| LightRange | 160 | Window lights farther than this from the camera (yards) are left out. |
| MaxLights | 96 | At most this many window lights at once (the nearest). |
| LightsPerCell | 4 | At most this many window lights in one cell of the lights' grid (16 lights per 16-yd cell by default). They rank after torches and lanterns, so a full cell keeps those. |
| Glow | 1 | A soft glow (bloom) around the lit window glass. Only the glass glows: not snow or bright ground (it must be the window, at the window's depth, bright and coloured), and never name text or the UI (drawn before them). |
| GlowStrength | 1 | Your multiplier on the window glow. |
| GlowRange | 220 | Windows farther than this (yards) don't glow; the glow fades out over the last third. |
| WithClientGlow | 1 | While the client's own Full Screen Glow (`ffxGlow`) is on: 0 = the window glow at full strength, 1 = half, 2 = off. |
| LightsFollowRooms | 1 | The window lights and their glow follow the rooms behind the glass: a window whose rooms are dark lights nothing, a row dims as its rooms go out (needs `WindowRooms.cdbc`; with the glass, its rooms or `LitRoomsAtNight` off, or `RoomVariety` or `LightsOut` off, every room is lit and so is every light). Lit frosted glass the same way (`FrostedLightsOut`), its light flickering in time with the hearth behind the glass (`HearthFlicker`; the row's own flicker then gives way). 0 = every lit window at full all night. |

## `[LivingAzeroth.SpellLights]`: spells light what's round them (experimental)

Spells, glowing projectiles and weapon enchants give a point light each, through our point lights (the terrain, the
surface cover, models and buildings: the lights panel's "our point lights" must be on), in the effect's own colours:
the model's own point light where it has one (22 of the 1,776 spell visual files have one; a white one, as Fireball's,
takes the effect's hue), else the colours of its particles, ribbons and geometry as they look in game (their colour
tracks times their textures' glow), leant halfway toward the school its file's name gives (fire, frost, holy, arcane,
shadow, fel, nature, lightning). As big and as bright as the effect (its file's size times its scale: a Frostbolt's
light reaches about 7 yards, a Consecration's 14), the light follows it every frame, flares with it and fades out with
it: as its particles die out, as the client fades it, and where it was once it's gone (about a third of a second).
A unit's auras at one spot merge into one light at the square root of their summed light (the candle ring's rule;
lights of different strength add in quadrature, so a faint one barely changes a bright one), and spell lights that
overlap share their glow the same way, so an area spell's dozens of pieces light the ground in its colour without
washing it out to one flat glare. The nearest and brightest `MaxLights` a frame are drawn.

A steady spell gives a steady light: the light follows its effect over time, not frames (the particles' count smoothed
over a quarter of their life, the brightness and hue eased; a spell still flares at once), lights merged stay merged a
little past `MergeDistance`, the lights kept stay kept unless another is clearly better, a light joining or leaving the
kept set fades in or out (about a fifth of a second), and lights fade out over the last fifth of `Range`. Spell lights
have their own 16 places in each 16-yard cell of our lights' grid, after the torches' and lanterns' (a cell full of
torches never drops one, and they never drop a torch); more than fit wait for room, a clearly better one (a missile
flying in) taking the place of the weakest within a few frames.

Only spell visuals, missiles and weapon enchants give spell lights: the files under `spells\` and the enchants'. The
game's own effects are left out (the loot sparkle on a herb or ore node you track, a chest or a quest object; breath in
cold air, footstep sprays, blood, level-up), and so are game objects' own models (portals, nodes, chests, mailboxes,
lamps), creatures and doodads: those keep their own lights as before (none, with the model lights off). Anything flying
as a missile counts (a fire arrow by its flames).

Which models are spell visuals, and what is read from the client, every frame and read only:
`features/SpellLights.hpp` (the scene's newest models at the head of its list, the client's two missile lists, and
the light scan's walk; every client instruction it relies on is checked first, else it stays off and says why). The
lights panel's **Spell lights** section changes all of this at runtime and lists what is followed and drawn and what
it costs; the snapshot (F11) and the 30-second log line carry the same status, the frame profiler has a row of its own
("ours: spell lights").

| Key | Default | Meaning |
|---|---|---|
| Enabled | 0 | Experimental: spells light what's round them through our point lights: missiles in flight and their impacts, spell effects on characters and on the ground, weapon enchants' glows. 0 = none (the light scan keeps every model's own lights, as before). |
| Brightness | 1 | Your multiplier on every spell light's brightness. |
| Radius | 7 | How far a spell light reaches for an effect about a yard across (yards); bigger effects reach up to 2.4 times as far, a weapon enchant half as far. |
| MaxLights | 24 | At most this many spell lights a frame (0 .. 64), the nearest and brightest first: keeps raids and battlegrounds cheap. The lights kept stay kept unless another is clearly better; one leaving fades out (drawn meanwhile: at most twice this). Of those followed, only the nearest 3 x this (at least 48) are looked at closely each frame. |
| Range | 80 | Spell lights farther than this from the camera (yards) are left out; they fade out over the last fifth of it. |
| MergeDistance | 1.5 | Spell lights closer than this (yards) merge into one with the square root of their summed light; two merged stay merged up to 1.3 times this apart. |
| Enchants | 1 | Weapon enchants' glows light what's round them too, weakly. |
| EnchantStrength | 0.35 | Your multiplier on the enchants' light (they're small and always on). |

## `[LivingAzeroth.Lantern]`: a lantern at your belt

From Lumenveil's list (the idea only: "a lantern on the player (key)"). One warm light (2400 K, a little under a torch's)
at your right hip, following you every frame, through our point lights (the lights panel's "our point lights" must be
on): the ground, the walls, models, the snow and the fog round you light up in the dark. It lights itself at night and
in dark places (the client says you're indoors: a cave, a crypt, a mine) and goes out where it's light: by day outdoors
it gives nothing, however it was lit, and where our lights already light the place (a hearth and candles round you,
a lamp-lit street: their light within 15 yards) it gives way and stays out. The light round you is smoothed over a second
and a half, so walking past a torch in a mine doesn't put it out. In a building's interiors it only fills where the
baked light is darker than its own, as every light of ours does, so a lit room's walls look as they did.

It follows the same rules as every light of ours: it ranks first in every cell of the lights' grid it reaches (a full
cell drops its weakest torch instead), in the scene's places, never a spell light's; it never merges with a torch beside
it; their sum goes through the same knee on every surface; it's indoor when you are (off the terrain, the cover and the
fog outside, as a cellar's torch). Its flicker is gentle and only in its colour, so the WMO interiors' baked light doesn't
pulse with it.

The key lights it and puts it out, with a line in chat. With `Automatic` on, the key's choice holds until the light
changes (nightfall or daybreak, into or out of a dark place), then the lantern looks after itself again. The lights
panel's **Lantern** section changes all of this at runtime and says why it shines or doesn't; the snapshot (F11) and
the 30-second log line carry the same status.

| Key | Default | Meaning |
|---|---|---|
| Enabled | 1 | A lantern at your belt: one warm light at your hip that follows you, through our point lights, lighting the ground, walls, models and the fog round you in the dark. By day outdoors it gives nothing, however it was lit, and it gives way where torches and lamps already light the place. |
| Key | 118 | The key that lights the lantern and puts it out; a line in chat says which. A Windows virtual-key code: 118 = F7 (the client binds nothing to it), 0 = no key; the Lantern section has a button too. The key still reaches the game. |
| Automatic | 1 | The lantern lights itself at night and in dark places (caves, crypts, mines, indoors without light) and goes out where it's light. 0 = only the key lights it. |
| Strength | 1 | Your multiplier on the lantern's light. |
| Radius | 8 | How far the lantern's light reaches, in yards. |

## `[LivingAzeroth.DistantLights]`: towns' lights from far away at night

From Lumenveil's list (the idea only: "towns visible from far away at night"). Our point lights reach 256 yards, fading
over the last third; past that a town at night was dark, its lamps' models not drawn that far and nothing lighting its
streets. Now, from two thirds of that range out, each lit window (`WindowLights.cdbc`, as much as its rooms are lit, seen
from in front) and each lamp, torch, brazier and fire (the light scan's far list: every couple of seconds it also notes
the lights of the models out to `MaxDistance`, their `DoodadLightAssignment` rows' and their own, each model file looked
up once) is a small glow in its own colour: a bright core and a faint halo, never under a pixel and a quarter across. They
fade in as the point lights fade out, with the night (the client's own window fade), and out with the client's distance
fog, which they pierce farther than the land (`FogPierce`), and before the far clip. Glows close together at that
distance merge (the candle ring's rule: the square root of their summed light), and the brightest, nearest `MaxLights`
are drawn.

They're drawn in one batch at the weather render's entry, after the fog, the sun shafts and the window glow, with their
own small pixel shader (nothing in the world's shaders changes), tested against the scene's depth so hills and walls in
front hide them; not with the camera indoors or under water. The lights panel's **Distant lights** section changes all of
this at runtime and shows what's drawn and what it costs; the snapshot and the 30-second log line carry the same status.

| Key | Default | Meaning |
|---|---|---|
| Enabled | 1 | At night, past our point lights' reach, the lamps and lit windows of towns are small glows in their own colours, so a town reads from far away. |
| Windows | 1 | Lit windows glow (`WindowLights.cdbc`), as much as their rooms are lit. |
| Lamps | 1 | Lamps, torches, braziers and fires glow: the models with lights the light scan finds out to `MaxDistance` (needs our point lights on). |
| Strength | 1 | Your multiplier on the glows' brightness. |
| Size | 1 | Your multiplier on the glows' size (a lamp's core is about a yard across). |
| MaxDistance | 1500 | No glows farther than this (yards), nor past the client's far clip. |
| MaxLights | 1024 | At most this many glows (0 .. 4096), the brightest and nearest first, after glows close together have merged. |
| FogPierce | 1.5 | How far into the client's distance fog the glows carry, in fog depths (its end minus its start): at 1.5 a glow at the fog's end keeps half its light. |

## `[LivingAzeroth.Memory]`: what uses the client's memory

The client is 32-bit and large-address-aware: it runs out at about 4 GB of address space, whatever RAM the computer
has, and usually before that, when no free block is left as big as something it asks for (a texture, a model's
buffer). HD texture and model packs are the usual cause. The memory readout says what uses the space, from inside the
process (`debug/Memory`):

- **The address space**, walked with `VirtualQuery` every `WalkSeconds`, a quarter of a millisecond a frame
  (`env/AddressSpace`): what's committed, reserved and free; the largest free block and how many free blocks are 64 MB
  or more; each image (the client, WarcraftXL, each extension, Direct3D, the graphics driver, Windows' own DLLs), each
  mapped file, and private memory (heaps and `VirtualAlloc`, where Direct3D keeps its copies of managed textures) with
  its largest allocations. Windows' own counters for the process (private bytes, working set) as the external memory
  log has them, and Direct3D's available texture memory.
- **The client's caches**, read every `ClientCachesSeconds` in one pass (`env/ClientMemory`, every instruction it
  relies on checked first): its textures, sized as the client makes them, by kind (characters, items, creatures, the
  world, terrain, the composited character skins...) and by format (compressed or not), the largest named; each is a
  managed texture, in video memory and, as Direct3D's system-memory copy, in this address space too. The GxTex reuse
  cache, the vertex and index pools, the M2 models (their files are kept in memory) and the WMO roots.
- **Living Azeroth's own use by feature**: its heap (every allocation charged to the feature that made it) and its
  Direct3D textures, surfaces and buffers. The other modules can't be told apart inside the heap; they show as their
  images.

On the overview page (its memory section), in the log every 30 seconds (the line after the status line, which now
starts with the zone and sub-zone, to line the external memory log up with where you were), and in every snapshot
(F11: everything, the largest textures, images and allocations named). Below `WarnFreeMB` free or a largest block under
`WarnLargestFreeMB`, a line in chat says so and a snapshot is written, so a crash for lack of memory leaves evidence.

| Key | Default | Meaning |
|---|---|---|
| Readout | 1 | The memory readout above. 0 = off (no walks, no reads, no warnings). |
| WalkSeconds | 5 | Seconds between walks of the address space (each walk is spread over frames, about a quarter of a millisecond in each). |
| ClientCachesSeconds | 30 | Seconds between reads of the client's texture and model caches (one pass in one frame, a millisecond or two with thousands of textures; at least 5). 0 = only for snapshots and warnings. |
| WarnFreeMB | 400 | Below this much free address space (MB), a line in chat warns once and a snapshot is written. 0 = no warning for it. |
| WarnLargestFreeMB | 64 | The same when the largest free block falls below this (MB): a fragmented address space fails before it's full. 0 = no warning for it. |
| WarnMinutes | 10 | At most one warning in this many minutes, unless it gets worse (100 MB less free, or the largest free block halved). |

## `[LivingAzeroth.Shafts]`: sun shafts

Light shafts through trees, fog and the gaps of the cloud deck: a post pass right after the ground fog (before rain,
see-through models, name text and the UI, so those never take part), at a quarter of the resolution. Each pixel
gathers the sunlit air along the line toward the sun's place on the screen: the sky and the fogged-out far end of the
view are bright, a trunk or a wall near you shades the line behind it, and where branches break that line into gaps
the shafts come a little brighter. Snow and other bright surfaces don't glow: what lights up comes from the scene's
depth and the sun's direction, not from how bright the picture is. A surface counts only a little, for the lit air in
front of it, and not at all where it stands in front of the sun's glow or its air is in the shadow of what stands
toward the sun: a mountain in front of the sun stays dark, and the light streams round its edges into the sky.

They radiate from the sun you see in the sky (or at night the moon), read from the client's sky each frame. That's not
where the client's light comes from: it lights the world from a fixed north-west bearing 20 to 37 degrees up, while it
draws the sun climbing to near the zenith at noon (at 15:00 the sun is 54 degrees up, the light 28). What lights the
world (the shafts under a cloud deck, the glass) keeps the light's direction, so it agrees with the scene's shading.

How strong they are follows what's in the air where you are (the place's fogginess, the ground fog's area fog, mist and
haze) and the time of day: strongest in fog or mist in the low sun of morning and evening, about three quarters of
that at noon, faint in clear air, fading with the sky in the evening (gone by about 22:00). Less in rain, none in heavy
rain, a storm, under water or indoors. They fade as the sun leaves the screen or sets (the sky's sun is up from about
06:10 to 20:30, the moon from about 22:20 to 03:25). Under a cloud deck (mountain zones with an AreaClimate CloudBase)
its gaps let the shafts through, and below it shafts fall from its gaps along your view, worked out from the deck's own
density, with the sun off the screen too; the air in a mountain's shadow stays dark (a shadow test against what's on
the screen, toward the light; where the test loses sight of what's in the way, off the screen, that light is halved).
The fog panel's **Sun shafts** section changes all of this at runtime and
shows the strength's parts, the visible sun and the light on the screen, which one the shafts radiate from, and the
cost; the frame profiler has a row for their GPU time ("ours: sun shafts").

| Key | Default | Meaning |
|---|---|---|
| Enabled | 1 | Sun shafts. |
| Strength | 1 | Your multiplier on their brightness. |
| Quality | 1 | 0 = a quarter of the resolution, one blur pass; 1 = a quarter, smoothed by a second short pass; 2 = half the resolution, smoothed (about four times the cost). |
| Samples | 32 | Taps per pixel along the line toward the sun (8 .. 64): more = smoother, longer shafts, slower. |
| Length | 0.85 | How far the shafts reach: the share of the way to the sun each pixel looks (0.2 .. 1). |
| ThroughCloudDeck | 1 | Under a cloud deck: the shafts come through its gaps, and below it shafts fall from its gaps (a short march through its density toward the sun). Needs the deck's smooth mode (`[LivingAzeroth.Fog]` CloudDeckSmooth 1) for the gaps; without it the deck dims them evenly. |
| Moon | 0 | Faint shafts from the moon at night, radiating from where the sky shows it (up from about 22:20 to 03:25): your multiplier, 0 = none. |
| TerrainBlocks | 1 | What stands in the sun's way blocks the shafts: a mountain in front of the sun stays dark and the light streams round its edges, and below a cloud deck the air in a mountain's shadow stays dark. 0 = the old look, where a fogged hillside glows like lit air (exactly as before). |

## `[LivingAzeroth.Glass]`: window glass

Window glass that looks like glass, every glass window and not only stained glass (`GlassMaterials.cdbc`, docs/cdbc-tables.md:
109 textures by default, clear, stained, frosted and leaded; per texture, so dungeons and instances get it too). Only the
glass texels of a window texture change, not its frame, lead or the wall painted round it; nothing else is touched (name
text, the UI, characters).
- **Seen from inside**: the day coming through the glass, the glass's own colour a little more saturated, brightest with
  the sun beyond the pane (frosted glass spreads it), dimmer under cloud, in rain, in a storm's dark and at dusk. Never
  darker than the client's own colour, so at night its lit windows stay as they are.
- **Seen from outside by day**: the client's own window texture is the artist's painted glass, warm and bright, as if
  lamplit; by day it moves part of the way to glass: darker (clear glass 45%, leaded 27%, stained and frosted 9%:
  `OutsideDarkening`), cooler (`DayCoolness`: less saturated and toward the sky's hue at its own brightness, more toward
  the top of the pane and at a slant; frosted 0.6 x, stained 0.25 x, so stained glass keeps its colours), with the sky in
  it (its fog colour, deeper toward the zenith; 24% head on for clear glass, 20% leaded, 10% stained and frosted, rising
  toward all of it at a grazing angle: `ReflectionF0`) and the sun's glint. Behind clear and leaded panes **one room per
  window** (interior mapping, `RoomShareDay` of the glass): each window (`GlassWindows.cdbc`: a WMO's panes joined into
  windows, per group) has a box behind its opening, about the window's width or a little wider and 3 to 5 yards deep by
  the building's kind (a shop and a farm shallow, an inn and a temple deep), seen in depth as you move: its floor, side
  walls, back wall and ceiling in the client's own interior textures by the building's people and place (`RoomTextures`:
  Stormwind's plaster and timber, Westfall's planks, dwarven stone, orcish hides, goblin planks, vrykul timber and furs,
  elven, night elf, Forsaken, draenei), at the world's scale and filtered (no shimmer), the floor darker and the ceiling
  dimmest, the side walls lit from the window, the corners darker; furniture at its depth by the building's kind and the
  window (`RoomDetail`: shelves, a hearth, a wardrobe, a painting, a weapon rack or a banner against the back wall; a
  table and chairs, a bed, a counter, a desk, barrels, pews or a candle stand in the middle), its edges a pixel soft;
  curtains half drawn in some. The room is seen in the day's light (the sky's and the sun's colour: orange at dusk) and
  its own colours (the glass's cooling isn't on it), a patch of sun on its floor by the window; the painted glass in
  front of it and its cooler hue keep it subdued. Its textures, furniture and corners fade to its plain shading from 45
  to 90 yards. On WARP Stormwind's leaded shop window keeps 89 to 97% of the stock's light by day (its glass 84 to 93%),
  Westfall's clear farm window 91 to 95%.
- **At dusk** (after the sun's day ends at 18:00, before the client lights its windows from 20:30) the painted glass's
  warmth is back (no cooling or darkening), and the room behind it is still seen by the day's light (before, it was black
  until the windows lit). At night the client's own lit windows.
- **The rooms behind the glass differ by building** (`RoomStyles.cdbc`, `GlassBuildings.cdbc`, docs/cdbc-tables.md): each
  building's tint and brightness from its own seed and its style, the style from its kind (inn, house, farm, shop, temple,
  keep, mage tower, ship; in a city's one big WMO each group, and each 14-yd block, its own) and its place's palette
  (human warm, Duskwood dim, Northrend's humans colder, elven gold and crimson, Dalaran violet, night elf moonlit, draenei,
  vrykul firelit wood and furs, goblin cluttered and warm, Forsaken dim green, orc firelight, dwarf forge-warm, Outland's
  outposts ruddy, Zangarmarsh teal...). A few rooms are dark, and nobody lives in a few buildings. Each window is its own
  room: its depth, furniture, lamp and curtains picked by its hash, weighted by the building's kind and place (shops
  shelves and a counter, inns tables and a bar, keeps racks and banners, homes beds, tables and wardrobes). Frosted and
  stained glass have rooms of their own (below).
- **At night** the windows the client lights (self-lit materials) show a lamp-lit room, about as bright as the client's own
  lit window it replaces, the lamp's light falling off across the room and the curtains glowing with it behind them. More
  of them go dark as the night goes on: 8% in the evening, half at midnight, three quarters by 03:00, a few early risers
  before dawn; inns stay lit late, shops close early. Ruined, abandoned and haunted places (and ships and dungeons: the
  window lights' dark places) stay dark. A room going dark or lit again fades over 1.5 s (after a jump of the climate
  panel's time, every room it crossed together), and the window lights and their glow in front of it go with it
  (`[LivingAzeroth.Windows]` LightsFollowRooms): each light dims with the windows its glass is in, the same windows the
  glass finds. A dark room's whole window goes dark with it, its lead and frame as much as its glass (no glowing lattice).
  The lamp-lit rooms take `RoomShareNight` of the glass, their edges about a pixel soft. A dark room isn't a black void
  (`DarkRoomLight`, 0.12 of a lit room's light): the moon's and the night's cool light comes in at its window (the
  client's own night light's hue), brighter by the glass, its furniture dim shapes, its corners darker; its window's glow
  and light stay off, and the glass in front reflects the night sky. Lit rooms keep their lamp alone; a window the client
  doesn't light has no lamp, so all its rooms take the moon's light.
- **Frosted glass** (the dwarves' glowing panes, the vrykul's icy ones, the draenei's, Dalaran's, the winter orcs', the
  orcs' hide windows...) has a heavily blurred room behind it (`FrostedRooms`): the frosted glass dominates, the room's
  textures at a low mip, its furniture soft dark shapes, its faces' shades evened out (no crisp corner), and at night its
  lamp a soft glow behind the glass. The windows the client lights (its self-lit materials, and the unlit ones drawn
  at full day and night) go dark through the night the same way (`FrostedLightsOut`): the same windows, hash and fades, each
  building's hours by its kind (inns late, shops early), the glass's own glow down to 15% when dark (a banked fire), and
  the window lights in front with it. A lit one flickers gently, a hearth behind it (`HearthFlicker`: by up to 8% at
  night, three slow waves at the torches' rates at half their pace, each window its own phase), its window light on the
  ground in time with it. Ruined and abandoned places go dark at night; a dungeon's windows (Tempest Keep, the Caverns
  of Time, the tram) keep the client's look.
- **Ironforge** never fully sleeps: each part keeps its own hours (by the client's own sub-zones, WMOAreaTable): the
  Great Forge and the smithies stay lit (at most 4% dark), the Commons (its shops, the bank, the inn, the halls, the High
  Seat, the Hall of Explorers, the gates) mostly (a quarter dark at most, late), the homes and quarters of the Military
  and Mystic Wards dim late and gently (at most 45%), and the Forlorn Cavern is darker (an hour early, up to 90%).
  Tinker Town has no glass. Its streets are interior groups with no sky: its glass has the street's look whatever the
  camera, with no daylight, sky or glint in it, and its panes put no sunlight on its floors (`IronforgeAlwaysLit` keeps
  every window lit). The blurred rooms behind its frosted glass have no daylight either: lamp-lit at any hour by its
  parts' hours, a dark one in the city's warm ambient.
- **Stained glass** (Dalaran's, Silvermoon's, the chapels', abbeys' and cathedrals') has a faint room behind it
  (`StainedRooms`, 0.45 of the clear glass's share): the room tinted by the glass's own colours, a hint of depth by day,
  mostly the glass's glow at night. Its lights go out with the rooms' (`LightsOut`), the glass's glow and the window
  lights in front with them: Dalaran's lit windows over stained glass now dim through the night. Its windows are numbered
  after every other window (`GlassWindows.cdbc`), and only a stained batch finds them. A dungeon's stained glass (Tempest
  Keep's walls and floors, Azjol-Nerub's roof, the Sunwell, Karazhan) and a sheet of it bigger than a window (a dome, a
  wall of glass) have no room; titan glass lit from behind stays as it is.
- **Titan glass lit from behind** (`TitanBacklit`): in Ulduar, the Halls of Stone and the Halls of Lightning the titans'
  stained glass has no real sky behind it, so it no longer waits for the sun: it glows on its own in its own colours at
  any hour (the same at noon and at midnight, never below the client's own colour, no sky reflection or sun glint), each
  texture at its own strength (`BacklitStrength` on top), and a light behind each pane, along its inward normal tilted
  30° down, puts the pane's colours on the floor and walls and in the room's air (the floor light and the coloured
  shafts, the same leak guards: the pane's room, 30 yd). General Vezax's glass in the Descent into Madness pulses slowly
  and faintly, a few percent over six and a half seconds, deep purple (`VezaxPulse`); the others shimmer by about 1% over
  half a minute or more. A pane counts as facing the sky only in an exterior group with its out side open to the sky
  (long rays against the WMO and the terrain); the look goes by WMO group, as most of its titan glass is. The groups that
  keep the daylight look are outdoors: Ulduar's siege-yard facades (`ul_detail_01`, the tram's outer architecture) and
  the Colossal Forge's wall windows; Wyrmrest's `wr_window` elsewhere keeps it too. Every pane with a room behind it is
  backlit: 929 (Ulduar 874, the Halls of Stone 44, the Halls of Lightning 11). The Yogg-Saron vision of Stormwind Keep
  keeps the cathedral glass's daylight look (it isn't titan glass).
- **Indoors, with the sun on the windows** (or a titan pane's light behind it, at any hour): the panes' light on the room's floor and walls (where the ray toward the sun
  leaves through a pane, in the glass's colour; up to 12 panes, kept inside each pane's room) and **coloured shafts** in
  the room's air (a short march along the view against up to 16 panes whose room you're in, at a quarter of the
  resolution, added right after the sun shafts). Both use `GlassPanes.cdbc`.

The lights panel's **Glass** section changes all of this at runtime and shows the light through the glass, how many
shader variants carry the blocks, the panes in use and the shafts' cost, and the titan glass (backlit panes near the camera and
lighting the floor, backlit batches drawn, Vezax's pulse now); the frame profiler has a row for the shafts' GPU
time ("ours: glass shafts"). The look itself is part of the buildings' own draws.

| Key | Default | Meaning |
|---|---|---|
| Enabled | 1 | Window glass (everything below). |
| InsideGlow | 1 | Seen from inside: the light coming through the glass. |
| OutsideReflection | 1 | Seen from outside by day: the glass darker and cooler, the sky in it, the sun's glint. |
| FakeInteriors | 1 | Seen from outside: a room behind the panes, clear and leaded glass's (frosted glass's with `FrostedRooms`, stained glass's with `StainedRooms`; with OutsideReflection). |
| ColouredShafts | 1 | Indoors: shafts of light in the room's air through sunlit windows, in the glass's colours. |
| FloorLight | 1 | Indoors: the sunlit panes' light on the room's floor and walls. |
| InsideStrength | 1 | Your multiplier on the light coming through the glass. |
| ReflectionStrength | 1 | Your multiplier on the reflection and the glint. |
| InteriorStrength | 1 | Your multiplier on the light of the rooms behind clear glass (1 = by day a little darker than the glass's sunlit face, near the window brighter, deeper in darker). |
| OutsideDarkening | 0.45 | Seen from outside by day: how much darker clear glass is than the client's own painted window; leaded 0.6 x this, stained and frosted 0.2 x. 0 = the client's own brightness. |
| DayCoolness | 0.25 | Seen from outside by day: how far the warm painted glass moves toward cool glass (less saturated, toward the sky's hue; more toward the top of the pane and at a slant); frosted 0.6 x, stained 0.25 x. The warmth comes back over dusk. 0 = the painted colour. |
| ReflectionF0 | 0.2 | Seen from outside by day: the sky's share in the glass head on (clear 1.2 x this, leaded 1 x, stained and frosted 0.5 x), rising toward all of it at a grazing angle; at night 0.1 x the kind's (clear 1.2, leaded 1, stained 0.8, frosted 0.5) whatever this is. Real glass's 0.04 beside a sunlit wall reads as no reflection. |
| RoomShareDay | 0.65 | By day and at dusk, how much of clear glass is the room behind it (leaded 0.85 x): one room per window, its floor, walls and ceiling seen in depth, the light falling in at the window and on its floor. |
| RoomShareNight | 0.75 | At night, how much of clear glass is the lamp-lit room behind it (leaded 0.85 x). |
| RoomDetail | 1 | Furniture in the rooms, at its depth so it shifts against the back wall as you move: shelves, a hearth, a wardrobe or a painting at the back; a table and chairs, a bed or a counter in the middle; by the building's kind (shops shelves and a counter, inns tables and a bar, keeps racks and banners). 0 = empty rooms. |
| RoomTextures | 1 | The rooms' floors, walls and ceilings in the client's own interior textures, by the building's people and place. 0 = plain shaded walls. |
| ShaftStrength | 1 | Your multiplier on the coloured shafts. |
| FloorStrength | 1 | Your multiplier on the light on the floor. |
| Range | 70 | Yards from the camera: panes farther away give no floor light or shafts. |
| RoomVariety | 1 | The rooms differ by building: tint, brightness, a few dark, by the building's kind and place. 0 = one warm room everywhere, lit all night (no lights out). |
| LitRoomsAtNight | 1 | At night a lamp-lit room behind the windows the client lights. 0 = dark rooms at night. |
| LightsOut | 1 | More of those rooms go dark as the night goes on (inns late, shops early; with RoomVariety), each fading over 1.5 s, the window lights in front with them. 0 = all lit all night. |
| RoomLayouts | 1 | Curtains half drawn just behind the glass in some rooms, as often as the building's kind and place say (homes and elven halls more, shops fewer). 0 = none. |
| RoomVarietyStrength | 1 | Your multiplier on how far the buildings' tints and brightness differ and on the rooms dark by day. |
| LitRoomStrength | 1 | Your multiplier on the lamp-lit rooms' brightness (1 = about the client's own lit window). |
| LightsOutStrength | 1 | Your multiplier on the share of rooms dark at night. |
| LayoutStrength | 1 | Your multiplier on the furniture (at most 1: all of it) and the curtains' folds. The rooms' textures, furniture and corners fade to their plain shading from 45 to 90 yards. |
| FrostedLightsOut | 1 | Lit frosted windows go dark through the night as rooms do (each building's hours; Ironforge's parts their own), fading over 1.5 s, the window lights with them. 0 = lit all night. |
| HearthFlicker | 0.08 | How much a lit frosted window's glow (and its window light) dips at night: a hearth behind it, each window its own phase. 0 = steady. |
| FlickerSpeed | 1 | Your multiplier on the hearths' pace (about 0.6 to 1.8 dips a second). |
| LampFlicker | 0 | The same flicker for the lamp-lit rooms behind clear and leaded glass and their window lights (0.03 is a candle's breath). 0 = steady lamps: their window lights keep their own faint flicker. |
| IronforgeAlwaysLit | 0 | Ironforge's windows lit all night (they still flicker). |
| TitanBacklit | 1 | Titan stained glass with no sky behind it (Ulduar, the Halls of Stone and of Lightning) is lit from behind: it glows in its own colours at any hour, never below the client's own colour, no sky or glint in it, and a light behind each pane casts its colours on the floor and in the air. Panes facing real sky keep the daylight look. 0 = the sun's light, as other glass. |
| BacklitStrength | 1 | Your multiplier on the titan glass's glow and the light it casts (each texture has its own strength, GlassMaterials' Backlit). |
| VezaxPulse | 1 | Your multiplier on the slow pulse of General Vezax's glass (1 = a few percent over 6.5 s, deep purple). 0 = steady. |
| FrostedRooms | 1 | Behind frosted glass (Ironforge's, the dwarves', the vrykul's, the draenei's, Dalaran's frosted panes) a heavily blurred room: its lamp a soft glow at night, its furniture soft dark shapes, the frosted glass dominating. Its lights go out with the glass's (`FrostedLightsOut`); underground in Ironforge no daylight in it, lamp-lit at any hour. A dungeon's frosted glass keeps the client's look. 0 = no room behind frosted glass (exactly as before). |
| StainedRooms | 1 | Behind stained glass (Dalaran's, Silvermoon's, the chapels' and cathedrals') a faint room tinted by the glass's colours: a hint of depth by day, mostly the glass's own glow at night, going dark with the rooms (`LightsOut`), the window lights in front with it. Not a dungeon's (its stained glass is architecture), and titan glass lit from behind stays as it is. 0 = no room behind stained glass and its window lights as before (exactly). |
| DarkRoomLight | 0.12 | At night a room whose lamp is out still shows faintly: the moon's and the night's cool light at its window (the client's own night light's hue), its furniture dim shapes, its corners darker; this share of a lit room's light (0.08 .. 0.15 reads as clearly dark but readable; underground the city's warm ambient). The window's glow and light stay off. 0 = dark rooms black (exactly as before). |
| SeeThroughWindows | 0 | Experimental: from outside, the building's real modelled room seen through its windows, its furniture and the rooms beyond its own doors with it, the glass drawn over it (the buildings `SeeThroughBuildings.cdbc` names: the human inns, two-storey houses, mage towers and farms, and since 2026-10-22 the chapels, Northshire Abbey and Northrend's halls with their stained or arched windows, 33 WMOs; features/SeeThrough, docs/r&d/see-through-windows.md). Each window with an interior group behind it opens like a doorway in the client's own portal walk, within 60 yd of its group, where the room is really seen through it from the camera (with `SeeThroughRedraw` 1: rays through the window meet the room's own walls, floor or ceiling from inside first: not over its ceiling or through its walls); a glass batch with an open window is drawn again after the room and its furniture, its open windows see-through. A window with nothing modelled behind it (a fake window), or whose room isn't seen through it, keeps its glass and fake room. Off for the session after any fault (the lights panel says where). 0 = the windows as before (exactly). |
| SeeThroughRedraw | 1 | With `SeeThroughWindows`: how the glass is drawn over the room. 1 = per pixel: every pane's glass at the opacity, lit and shaded alike, the painted lead, frame, tracery and shutters opaque (the window texture's lead mask: GlassLead.cdbc's panes, the mask built from the texture as the game loads it), and a window opens only where its room is really seen through it (not over its ceiling or through its walls), a shut window keeping its glass and fake room. 0 = the whole glass at `SeeThroughOpacity`, its lead and frame too, and every window with a room behind it in range open (as on 2026-10-20). The lights panel has the per-pixel redraw's debug view (not saved), the lead masks built, and reads the device's state back at the redraw (2026-10-21). |
| SeeThroughOpacity | 0.35 | With `SeeThroughWindows`: how much the glass covers the room behind it (0.35 = the glass 35%, the room 65%; 1 = opaque, 0 = clear). The glass keeps its look (reflection, tint, darkening) but not its fake room; with `SeeThroughRedraw` 1 only its glass texels (the glass mask) are see-through, its painted lead, frame and tracery stay opaque (2026-10-20). |
| SeeThroughStainedTint | 0.8 | With `SeeThroughRedraw` 1: how much stained glass colours what's seen through it, the room from outside or the street from inside (2026-10-22: "the coloured glass should stay coloured over the room (tint, not just blend); and from inside, the street through the coloured glass"). Before the glass is blended over it, what's behind an open window's glass is multiplied by the glass's own colour (the texel's hue at full value, this much of the way from white; none on the lead and frame), as light through coloured glass is, in a pass of its own (its own small pixel shader, not the stock WMO variants). 0.8 = most of the way to the glass's colours; 0 = none: the glass only laid over it at `SeeThroughOpacity`. The lights panel counts the batches tinted. |
| SeeThroughFrosted | 1 | With `SeeThroughRedraw` 1: frosted glass goes see-through like the other glass (2026-10-22, Phase B: "the real room softly blurred behind frosted panes"), the room from outside or the street from inside seen through it, as soft as `SeeThroughFrostBlur` says. 0 = frosted windows stay shut, their fake rooms as before (exactly). |
| SeeThroughFrostBlur | 0.5 | With `SeeThroughFrosted`: how soft frosted glass makes what's seen through it. Once a frame, before the first frosted batch is drawn again, the frame so far (the room or the street drawn) is copied at a quarter of its size and blurred (two 9-tap passes, its own pixel shader; the taps 0.5 + 3 x this quarter-size pixels apart: 0.5 is about 16 pixels at 1080p), and the open windows' frosted glass is laid over that copy instead of the sharp room (where the glass is in front, by the depth the opaque draw wrote, as the tint). One copy a frame whatever the number of windows; none in a frame without frosted glass redrawn. 0 = no blur and no copy: the room as through clear glass, still under the frosted glass at `SeeThroughOpacity` (2026-10-23: "if I set the blur to exactly 0 it doesn't work, any value above 0 does"; before, 0 kept frosted windows shut). Where the blur can't run (a render target it can't copy, its shaders not made) frosted windows stay shut unless it's 0, and the lights panel says why. The lights panel also has two diagnostics, not saved: the blurred copy alone in the open glass, and the room unblurred. |
| SeeThroughFromInside | 0 | Experimental: from inside a building (the same buildings), the street seen through the room's own windows, as the client shows it through an open door ("windows should be see-through from the inside as well", the user, 2026-10-21). In the client's walk from the camera's groups, each window of a room it reaches (GlassWindows' rows of interior groups) that faces the camera from inside, is on the screen and within 40 yd becomes a way out of its own (an exterior portal view: the outdoors culled through the rectangle round the ways out, the sky drawn in it), where the street is really seen through it (rays through its glass leave by the building's own window glass, not through a wall or the roof from inside), and the room's glass is drawn again over what's outside (`SeeThroughRedraw`, `SeeThroughOpacity`). It doesn't need `SeeThroughWindows`. The lights panel lists the ways out, the client's own and the windows', and each window's verdict. 0 = from inside the windows as before (exactly). |
| WholeRooms | 1 | Each room's windows behave as one ("in general can we have all windows in a room behave the same? like either they're all see through or they're not, and if they're not then have their lights either all on or off", the user, 2026-10-24). With `SeeThroughWindows` or `SeeThroughFromInside`, a room's windows on the screen open together, and only when the room is really seen through every one of them (each window's rays, as before); otherwise they all keep their glass and fake rooms. A room opens once all its windows have passed for 0.35 s and shuts as soon as one of them fails, so no window ever shows the void or the sky through a wall; a window too far, or over the frame's budget, keeps its whole room shut. A room's key: from outside the room behind the window; from inside, the room you're in for the windows onto the street, the room beyond for a window onto another room. Behind the glass (any building), a room's fake rooms are lit or dark together at night, their window lights with them, and go dark together as the night goes on (`GlassWindows.cdbc`'s Room column: the room behind each window); their furniture still differs from window to window. 0 = each window on its own (exactly as before). |
| SeeThroughBetweenRooms | 1 | With `SeeThroughFromInside`: inside a building made of rooms and indoor streets (2026-10-23, Phase C: Ironforge, whose whole city is interior groups and whose shop windows face its indoor streets; the rows `SeeThroughBuildings.cdbc` marks 0x4, for a WMO or one group of it), a window from one room into another (a shop's from the street, the street's from the shop) opens onto it where that room is really seen through it (its rays meet the room's own surfaces from inside, as from outside), the client's walk going on into that room through the window's rectangle, and the window's glass is drawn again over it (frosted or stained as its kind says). The frame's budget from inside (16 windows) holds for both kinds. 0 = those windows as before. |
