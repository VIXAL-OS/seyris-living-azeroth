# Config (`WarcraftXL.ini`)

Read through `wxl-seyris-tools` (needs settings 1.1.0) from `WTF\WXL\WarcraftXL.ini`, or from
`WarcraftXL.ini` in the client root if one exists there. Section `[LivingAzeroth]`, and the
sub-sections `[LivingAzeroth.Fog]`, `[LivingAzeroth.FogWake]`, `[LivingAzeroth.Storm]`, `[LivingAzeroth.Windows]`,
`[LivingAzeroth.Shafts]` and `[LivingAzeroth.Glass]` (an ini has no real nesting; the dotted names keep them together). Missing keys are written with their default and a comment on
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
| WindShelter | 1 | Roofs and overhangs shelter the ground below from wind. |
| WindLee | 1 | Walls and cliffs cast a wind shadow on their downwind side. |
| DebugWindArrows | 0 | Debug: draw the wind as arrows around the player. |

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

## `[LivingAzeroth.Shafts]`: sun shafts

Light shafts through trees, fog and the gaps of the cloud deck: a post pass right after the ground fog (before rain,
see-through models, name text and the UI, so those never take part), at a quarter of the resolution. Each pixel
gathers the sunlit air along the line toward the sun's place on the screen: the sky and lit air far off are bright, a
trunk or a wall near you shades the line behind it, and where branches break that line into gaps the shafts come a
little brighter. Snow and other bright surfaces don't glow: what lights up comes from the scene's depth and the sun's
direction, not from how bright the picture is.

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
density, with the sun off the screen too. The fog panel's **Sun shafts** section changes all of this at runtime and
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
  shelves and a counter, inns tables and a bar, keeps racks and banners, homes beds, tables and wardrobes). Stained and
  frosted glass has no room, as before.
- **At night** the windows the client lights (self-lit materials) show a lamp-lit room, about as bright as the client's own
  lit window it replaces, the lamp's light falling off across the room and the curtains glowing with it behind them. More
  of them go dark as the night goes on: 8% in the evening, half at midnight, three quarters by 03:00, a few early risers
  before dawn; inns stay lit late, shops close early. Ruined, abandoned and haunted places (and ships and dungeons: the
  window lights' dark places) stay dark. A room going dark or lit again fades over 1.5 s (after a jump of the climate
  panel's time, every room it crossed together), and the window lights and their glow in front of it go with it
  (`[LivingAzeroth.Windows]` LightsFollowRooms): each light dims with the windows its glass is in, the same windows the
  glass finds. A dark room's whole window goes dark with it, its lead and frame as much as its glass (no glowing lattice).
  The lamp-lit rooms take `RoomShareNight` of the glass, their edges about a pixel soft.
- **Frosted glass** (the dwarves' glowing panes, the vrykul's icy ones, the draenei's, the winter orcs', the orcs' hide
  windows...) has no room behind it, but the windows the client lights (its self-lit materials, and the unlit ones drawn
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
  every window lit).
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
| FakeInteriors | 1 | Seen from outside: a room behind clear and leaded panes (with OutsideReflection). |
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
