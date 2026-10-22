# Custom tables (cdbc)

Plain WDBC files in the client's `DBFilesClient\` folder. Editor definitions (WDBC-editor XML) are
next to this file.

**Rules for every table here:**
- Columns are only ever **appended**. A file from before a column existed still loads: the missing
  column counts as "not set" (inherit), so adding a column never needs a converter. To get the new
  columns into a file for editing: `python tools/upgrade_tables.py <DBFilesClient folder>` (fills
  them with "not set", keeps a timestamped `.bak`). Layout changes that aren't appends ship with a
  converter in `tools/`.
- **−1 inherits** (takes the value from the next row up, as each table describes). **Signed columns**
  (temperatures, offsets) inherit at **−1000** and below instead, since −1 °C is a real value.

## WindProfile.cdbc — `WindProfile.xml`

Wind per scope. Lookup: Global < Map < Area (zone) < Area (sub-area); the more specific row wins
field by field. **A negative value inherits** from the scope above. No Global row = no wind.

| Field | Meaning |
|---|---|
| ScopeType | 0 = Global (one row), 1 = Map, 2 = Area |
| ScopeID | Map.dbc ID or AreaTable ID (ignored for Global) |
| GroundMin / GroundMax | strength range (0..1) at ground level |
| AloftMin / AloftMax | strength range at and above AloftHeight |
| AloftHeight | yards above ground where wind is fully unobstructed |
| WeatherInfluence | 0..1, how far bad weather pushes strength toward the max |
| GustStrength | 0..1, gust amplitude on top of the steady wind |
| GustFrequency | relative, 1 = default |
| TerrainWeight | 0..1, reserved for terrain shaping |
| LockedDirection | compass degrees the wind blows **toward** (0 north, 90 east) |
| LockStrength | 0..1, pull toward LockedDirection (0 = no lock) |
| Flags | reserved, 0 |
| Comment | free text for the editor (which place the row is); never read. `tools/fill_comments.py` fills empty ones with the place's name. Appended 2026-10-11 |

Test data generator: `tools/gen_test_wind.py`.

## GroundEffectDoodadWind.cdbc — `GroundEffectDoodadWind.xml`

Per-doodad grass overrides. **A row replaces the automatic values for that doodad.** Written by
the in-game "Save overrides" button (keeps a `.bak`).

| Field | Meaning |
|---|---|
| ID | GroundEffectDoodad.dbc ID |
| Flags | 0x1 no wind, 0x2 no push, 0x10 flip root/tip (0x4 droop when wet, 0x8 water current: reserved) |
| Stiffness | 0 flexible .. 1 rigid (scales wind and push) |
| RootV / TipV | manual bend mapping in texture V; -1 = automatic from the model geometry |

## GroundEffectDoodadDensity.cdbc — `GroundEffectDoodadDensity.xml`

How dense grass is drawn near the player (instanced grass only): each placed plant is drawn
`Multiplier` times, the extra copies made on the GPU on the plant's own terrain triangle. Full
density within half of `Radius`, fading to normal at `Radius`. The player's limits
(`GrassDensityMax*` in `config.md`) cap the result.

| Field | Meaning |
|---|---|
| ID | row id |
| ScopeType | 0 = Global, 1 = Map, 2 = Area |
| ScopeID | Map.dbc ID or AreaTable ID (ignored for Global) |
| DoodadID | GroundEffectDoodad.dbc ID; 0 = every doodad |
| GroundEffectID | GroundEffectTexture.dbc ID (what the map designer painted); 0 = any |
| Multiplier | plants drawn per placed plant (1 = no copies) |
| Radius | yards around the player |
| Spread | most yards a copy moves from its plant (copies never leave its terrain triangle) |

A float of **-1 takes that field from the next, less specific row**. No row at all = no copies.

**The most specific row wins** (workspace rule for scoped tables): place first, then what.
Places: the chunk's own area (sub-zone), its parent zones, the map, global. Within each place:
DoodadID + GroundEffectID → DoodadID → GroundEffectID → every doodad (0). Values are never
added or multiplied across rows. So an area-wide row (DoodadID 0) beats a global rule for one
doodad; to keep one doodad different inside such an area, give it its own Area row.

Area and ground effect come from the chunk each grass layer belongs to, not from where the
player stands, so a forest can be thinner even while you're outside it.

Generator: `tools/gen_default_density.py` (shipped baseline; `--examples` adds example rows).

## Ground materials: three tables

What the ground is made of is decided once and shared by every feature that cares (the cover
now; moisture, temperature, footsteps and texture swaps later). Three roles:

1. **GroundMaterialSelector.cdbc**: *where* a material lies.
2. **GroundMaterial.cdbc**: *what* a material is (properties only).
3. **Feature tables keyed by material**: SurfaceCover.cdbc today. A new feature adds a new table
   and leaves the others alone.

Looked up per terrain cell from what it's painted with: each texture layer's **texture**, that
layer's **ground effect**, and the ground effect's **TerrainType**. Layers are weighted by their
painted strength, so two materials meeting mix smoothly. **The F9 surface cover panel shows all of
this for the spot you stand on** ("here:" lines: texture, effect, TerrainType, area, material and
its values), so rows can be written on the spot.

Generator for all three: `tools/gen_default_ground_materials.py` (`--examples` adds an Icecrown
snow child material, wet Westfall sand, mud with muddy footprint outlines, and a "no snow on
Kalimdor" selector). After editing, press "Reload table" in the surface cover panel: it reloads
all three.

### GroundMaterialSelector.cdbc — `GroundMaterialSelector.xml`

| Field | Meaning |
|---|---|
| ID | row id |
| ScopeType | 0 = Global, 1 = Map, 2 = Area |
| ScopeID | Map.dbc ID or AreaTable ID (ignored for Global) |
| TexturePath | the painted texture's path, e.g. as the panel shows it; **empty = any**. Case and `/` vs `\` don't matter |
| GroundEffectID | GroundEffectTexture.dbc ID (the effect painted with the texture); **0 = any** |
| TerrainType | TerrainType.dbc ID; **-1 = any** (0 is Dirt). Stock: 0 Dirt, 1 Metallic, 2 Stone, 3 Snow, 4 Wood, 5 Grass, 6 Leaves, 7 Sand, 8 Soggy, 9 Dusty Grass, 10 None, 11 Water |
| LiquidType | **0 for terrain rows.** A LiquidType.dbc ID (as baked into ADTs and WMOs) makes this a **liquid row**: that liquid is this material. Only ScopeType/ScopeID and MaterialID count on liquid rows (place: area chain → map → global, the most specific wins) |
| MaterialID | the GroundMaterial; **0 = no material here** (switches a broader row off, e.g. no snow in one zone) |
| Flags | reserved, 0 |

**The most specific row wins** (workspace rule for scoped tables): place first, then what. Places:
the terrain cell's own area (sub-zone), its parent zones, the map, global. Within each place:
TexturePath → GroundEffectID → TerrainType → everything (empty texture, GroundEffectID 0,
TerrainType -1). So "snow everywhere" is one Global row with TerrainType 3; one texture that
should be a different material gets a TexturePath row; a zone that's different gets Area rows.

### GroundMaterial.cdbc — `GroundMaterial.xml`

| Field | Meaning |
|---|---|
| ID | material id (what selectors and feature tables refer to) |
| Name | for authoring and the panel |
| Parent | material whose values a −1 field takes (0 = none). Feature tables follow the same chain |
| RestMoisture | 0 = bone dry .. 1 = soaked: **what the texture already shows** (wet-painted sand ≈ 0.8). Moisture rests here; later, visuals react to the difference from it. Not used yet |
| Stiffness | resistance to deformation: **0 = gives way completely .. 1 = rigid** (the same meaning as GroundEffectDoodadWind's Stiffness). For a cover: feet press away (1 − Stiffness) of its depth, so mud at 0.4 keeps a floor in every print |
| Flags | 0x1 **clear ice** (the terrain draws it as deep, clear ice). 0x2 **sealed**: paved, cobbled, flagstones; water runs off, so **no mud forms** on it: a cover with a MoistureMin is held back where sealed ground is painted, fully from 30% of the spot, none below 5%. Children of a sealed material are sealed too. Flags are the material's own (not inherited), except 0x2 |
| Absorbency | 0..1, how much outside moisture reaches it: **rain** on open ground and **water nearby**. Rock and ice ≈ 0.1 (rain runs off), sand and soil ≈ 0.9. Appended: older files without it still load |
| Temperature | °C of the material itself; **−1000 = none** (signed). A heat source: **liquid materials** (magma) and **terrain materials** (e.g. a painted lava-stream texture, where it's painted at least 50%) warm (or cool) the ground toward it, by 5% of the difference at the source, fading out by 12 yd and 4 yd above it. Liquid and ground heat don't add up; the stronger one counts. Hot ground dries out by itself (heat drying). Appended |

A float of **−1 takes the value from the Parent** (and so on up). Defaults where no material in the
chain sets one: RestMoisture 0.3, Stiffness 0, Absorbency 0.5.

**Ground moisture** (runtime, not stored; the climate panel shows it where you stand): every
2 yd around you (±128 yd) the ground's moisture settles toward its **equilibrium**:
- the materials' RestMoisture (mixed by painted strength),
- wetter near a liquid: Absorbency × the liquid's RestMoisture × how close (full within 2 yd,
  none beyond 6) × how low (full up to 0.5 yd above the surface, none from 2 yd),
- drier in hot, dry air (from 20 °C, up to 60% below rest at 40 °C with no humidity).

It moves there over minutes: faster when warm and windy, slower in humid air, very slowly
below freezing. **Rain** wets ground that's open to the sky (no roof above), at Absorbency × the rain's
intensity. Submerged ground takes the liquid's RestMoisture.

**What a liquid does:** its liquid row's material (RestMoisture: how wet it makes the ground;
Temperature: heat), else built in by the liquid's category (LiquidType.dbc's Type column): water,
ocean and slime RestMoisture 1 and no temperature; magma RestMoisture 0 and 1000 °C, so the ground
around lava is hot and dry. The climate panel names the nearest liquid, its category and what it does. Nothing shows it on the terrain itself yet (that needs the terrain
shader patch); covers already look wetter where it's above rest.

### SurfaceCover.cdbc — `SurfaceCover.xml`

Which materials carry a cover (snow, sand, ...), how deep, and how it looks and behaves. The cover
is a real layer on top of the terrain: units sink in and carve trenches with a rim, which fill back
in over time. Grass and small doodads are buried under it.

| Field | Meaning |
|---|---|
| ID | row id |
| MaterialID | the GroundMaterial that carries this cover (one row per material) |
| CoverMaterial | the material of what lies on top (snow on grass is still Snow); **0 = the ground material itself**. Its Stiffness decides how far feet press the cover down |
| Depth | yards of cover; **0 = no cover** (switches a material off) |
| MaxSlope | degrees: steeper ground holds no cover |
| SlopeFade | degrees below MaxSlope over which the cover thins out |
| DriftNoise | depth variation (drifts and lumps), share of the depth (0.35 = ±35%) |
| EdgeBreakup | 0 = smooth, rounded edges where covered ground meets bare ground .. 1 = ragged, patchy ones |
| Rim | height of the rim pushed up beside a trench, share of the depth |
| RelaxSeconds | how long a trench takes to fill back in |
| TintColor | colour as 0xRRGGBB, used with TintStrength |
| TintStrength | 0 = the plain cover colour .. 1 = TintColor. TintColor comes from the same row |
| CoverTexture | a BLP drawn on the cover, tiled like the terrain's layers (one repeat per terrain cell), e.g. the same texture as the ground it covers. **Empty = from the parent material's row, `-` = none.** Any number of paths per table; up to 8 different ones are drawn at once in view (the ones most present around you; the panel lists them and says when more are around), and two per spot: where more meet, the two strongest take the others' share. TintColor/TintStrength still apply on top |
| Opacity | **reserved, not used yet** (see-through covers: slush, goo) |
| Flatten | **reserved, not used yet** (liquid-like covers that fill hollows) |
| ZOffset | yards the cover's base sits above (+) or **below (−)** the terrain. Below the terrain only what rises above it shows. Muddy footprint outlines: Depth 0.015, ZOffset −0.02 (the top stays 0.005 yd under the grass), Rim 4 (rims rise 0.055 yd above it), DriftNoise 0. Signed: **−1000 inherits** (exactly −1 does too, as older files wrote it) |
| Wetness | how wet the cover **looks** at rest: 0 = as its texture .. 1 = soaked: darker, and the specular gets stronger (up to ×3) and tighter. On top of it, the ground moisture above its rest (rain, water nearby) makes it look wetter still. The shine comes from the CoverTexture's specular mask, so a cover without a CoverTexture doesn't shine |
| Flags | 0x1 **prevent underwater**: the cover thins out over 0.3 yd below a terrain liquid's surface (rivers, lakes, sea; not WMO water). 0x2 **prevent on land**: cover only there (river and sea floors). 0x4 **use MCCV**: tinted by the terrain's vertex colours, like the terrain. 0x8 **ignore specular map**: no shine from the CoverTexture. Flags come from the row that decides Depth, except 0x8, which belongs to the CoverTexture named in the same row |
| MeltKeptShare | **snow covers only**: the share of the painted depth mild weather melts it down to (at 15 °C; less above 0 °C, nothing at or below). 0.6 if no row sets it. Appended |
| MeltGoneTemperature | **snow covers only**: °C at which painted snow melts away completely (from MeltKeptShare at 15 °C down to nothing here). 25 if no row sets it; set it low (e.g. 16) for "no snow in summer" in a zone or on a texture. Signed: **−1000 inherits**. Appended |
| MoistureMin | the cover only forms where the ground's **moisture** (0 bone dry .. 1 soaked; the climate panel shows it) is at least this, growing to its full Depth over the next 0.1: mud on riverbanks and in swamps, more of it after rain, drying away again. Checked every few seconds, so it lingers briefly after the ground dried. **Never on sealed ground** (GroundMaterial Flags 0x2: cobbles, paving), even where the mud's own texture shows between the stones. Its ruts fill in faster in rain (`SurfaceCoverRainFillsRuts`). **2 (no row sets it) = off**: the cover doesn't depend on moisture. Off by default on purpose: nothing becomes mud unless a row asks for it. Appended |

**Specular:** a CoverTexture's alpha is its specular mask, as on the terrain's layers. If a `_s.blp`
next to it exists (later expansions keep the mask there), that one is used instead. The shine uses the
terrain's own sun and specular colour, so a custom sky's sun applies too.

A float of **−1 takes that field from the parent material's row** (ZOffset: exactly −1), and so on
up the chain; a material without a row takes everything from its parents'. TintColor comes with
TintStrength's row, Flags with Depth's row. Fields no row sets use the defaults: MaxSlope 45,
SlopeFade 15, DriftNoise 0.35, EdgeBreakup 0.5, Rim 0.3, RelaxSeconds 30, no tint, no cover texture,
ZOffset 0, Wetness 0, no flags, MoistureMin off. No material, no row along its chain, or Depth 0 = no cover.

**Trench size:** like the client's own footprints. CreatureModelData's FootprintTextureLength/Width
(in inches) × the unit's scale, the mount's while mounted; the trench radius is 1.2 × the print's
longer side (a human's 0.33 yd print → 0.4 yd). Models with FootprintTextureID −1 (bats, birds and
other fliers that "walk" in the air) leave no trench.

**Trench shape:** pressed flat in the middle, a wall around 0.85 × the radius, a rim outside it. No wall
or rim is drawn narrower than `SurfaceCoverTrenchSoftness` cells of the near grid (1.5 × 0.25 yd), or a
walked trench's edge comes out as a staircase along the grid; trenches from ~1.25 yd up keep their own
widths, critters' smaller than the filter become shallower dimples. A foot flattens the rims earlier
steps pushed up, and pushes up none where the ground is already trodden, so a walked path is a trench
between two rims (not one raised band).

**The client's own footprints** (FootprintTextures, `showfootprints`) stay visible on top of every
cover. The client draws them after the whole scene, as copies of the terrain triangles under the foot,
depth-tested with only a tiny depth bias, so a cover even a few centimetres thick used to hide them
outside a freshly opened trench. During the client's own footprint draw each print lying on covered
terrain is now raised to the cover's surface at its centre (the trench floor it lies in), then put back.
The client still draws it with its own texture, fade and blending. The panel's "The client's footprints
on top of the cover" switches it off; its status line says whether the client checks passed
(features/CoverPrints).

**Prints on sand the client counts as dirt.** The client puts a print down only where the ground's TerrainType
(the dominant layer's ground effect's) has the footprint flag: Snow and Sand. Silithus paints its dunes with
ground effects of TerrainType Dirt, and so do Feralas's and Ashenvale's sand, most of Darkshore's sand 3 and
the dry muddy shores (Dirt or Soggy): no prints, so no rims around them either, only the trench share of
anything else painted there (Silithus's rock highlight). Where the cover is a print-ringing one
(Flags 0x10, at least half of it), the footstep's print now goes down as on sand; sounds keep the ground's own
TerrainType (`SurfaceCoverSandPrints`, the panel's "Sand: footprints where the client counts it as dirt").

Rim and RelaxSeconds apply per spot, like every other value (blended between materials). The panel's multipliers (Depth, Drift noise, Edge breakup, Rim, Relax time) scale the
table for testing or taste; at 1 the table applies exactly.

Shipped baseline: snow 0.35 yd everywhere it's painted (bare above ~40°), sand listed but off
(Depth 0, with a sandy tint ready).

**Paved ground and Elwynn's mud** (`tools/add_paved_ground.py`, applied to `data/`): GroundMaterial 80
**Paved** (Flags 0x2, sealed; nothing else set, so it reads like no material otherwise) on the old world's
cobbled and paved road textures (Elwynn, Duskwood, Stranglethorn, Tirisfall, Burning Steppes' bricks), so
no mud forms on them. Elwynn's road is cobblestones over ElwynnDirtBase2, whose mud (row 12, MoistureMin
0.6) grew on the road itself in rain. Row 12 is shallow now: Depth 0.02, ZOffset −0.025, Rim 1.75 (was
0.04, −0.08, 7): it stays 0.005 yd under the grass until feet push a rim up, 0.03 yd proud (was 0.24),
and only where the dirt is painted at about half or more.

**Ash and sand** (`tools/add_ash_sand_cover_rows.py`, applied to `data/`): thin layers that take trails
like snow. Two parent materials, **Ash** (50) and **Sand** (51), carry the shared cover row; each
texture's own material (Parent Ash or Sand) adds its CoverTexture (the ground's own texture), a tint
and, where it differs, the depth. Ash and Sand set no material properties, so moisture and heat read
those textures as before.
- Ash: Searing Gorge and Burning Steppes (BurningSteppsAsh01, AshCracks, Charcoal01) and the Bone
  Wastes (BoneWastesDirtBase, DirtCracked02, DirtHoles). 0.06–0.10 yd, bare above 35°, fills in over
  20 s.
- Sand: Tanaris (TanarisSandBase01, SandStones) and Silithus (SilithisSand01, Sandhighlight,
  SandDetail). 0.06–0.08 yd, bare above 30°, fills in over 10 s (the wind smooths it).
- Both: not under water; tinted to their texture's own colour ×1.68 at 0.2, which comes out about 8%
  lighter than the ground (loose material on top, so trails show the darker ground); the base 0.02
  (sand 0.015) yd below the terrain, so a fresh trench shows the ground itself, and the client's own
  footprints on it, for the first fifth of its fill-in time.

**Snow: fallen and painted** (runtime; the cover panel's "here (snow)" lines and the climate
panel's zone list show it). A cover is **snow** when its CoverMaterial is the fallback snow material
(what GroundMaterialSelector gives for **TerrainType 3** at the place, or globally) or a child of it.
- **Fallen snow** builds up while it snows on ground below 0 °C (0.2 yd an hour at full intensity, up
  to 0.6 yd) and melts above 0 °C (~1.5 cm per °C per day; its water soaks the ground). Snow falling
  on ground above 0 °C melts on arrival and soaks it, more slowly than rain. While it isn't
  snowing, fallen snow also **sublimates** (turns to vapour, no melt water), even below 0 °C: half
  of it in ~8 h in calm air at night, down to ~1 h in strong wind, dry air and sunshine. So
  always-cold zones don't keep fallen snow forever; their painted snow stays. It's kept
  per zone (a zone you enter while it snows catches up on half an hour of snowfall) and follows that
  zone near you, less where a spot is warmer than its zone (hot ground and liquids, sunny slopes),
  none under roofs and liquids. Where a **snow cover** is painted it adds to its depth and keeps its
  look. Elsewhere it takes the look of the layer's own row **if that row's CoverMaterial is snow, even
  with Depth 0** ("no snow until it snows, then it looks like this", per area or texture), else of
  the fallback snow's row, else built-in values (plain white, slope limits 45°/15°). It never lies
  on or right beside **other** covers (sand, ash, mud), and their look keeps to their ragged edge
  (fallen snow's look doesn't pale it).
- **Painted snow** keeps its Depth as a baseline. Warm weather melts it down, slowly (degree-days),
  toward a cap: all of it at 0 °C or below, MeltKeptShare at 15 °C, nothing at MeltGoneTemperature.
  It opens in small holes and patches first. Heat sources (hot ground, hot liquids) melt it down to
  nothing on top and less further away, leaving pits. Colder again (night), it grows back toward its
  cap (~4.5 cm an hour, faster while it snows). Melt water wets the ground.
- The **effective temperature** of a spot is the ground's (air, hot liquids and ground nearby,
  toward a hot material's own on lava textures) plus up to 4 °C of sun by day on ground facing it.
- Testing: the cover panel's **Deposit snow** adds fallen snow to your zone; the **Everywhere** mode
  paints nothing, so fallen snow lies anywhere with the plain look. The climate panel's **Set your
  zone's soak** does the same for moisture.

**Older files:**
- 21 columns (place keys inside SurfaceCover itself): the panel says so. Split them with
  `python tools/convert_surface_cover_v4.py <SurfaceCover.cdbc>`. It writes all three tables into
  `converted-v4/` next to the input: one material, selector and cover row per old row, same IDs,
  with Parents set so −1 fields resolve exactly as before.
- 19 columns (before ZOffset/Wetness): run `convert_surface_cover_v3.py` first.

## Doodad lights: two tables

Lights for placed models (doodads) without editing the models: add a light to a model that has
none, or replace a model's own light entries with better ones. Never named `Light*` (those are the
sky's). Our lights light the **terrain and the surface cover** per pixel (up to 24 at once, the
nearest to the camera); M2s, WMOs and characters keep the client's lighting for now. The **lights
panel** shows every placed model near you with its path, bones, attachment points and particle
emitters, and the **light editor** edits both tables live and saves them (old files kept as `.bak`).

Generator with examples: `tools/gen_doodad_lights.py` (a torch flame replacing `generaltorch01`'s
own light at its flame, a lantern, a downlight). After editing the files by hand, press "Reload
light tables from disk".

`tools/add_settlement_lights.py` adds warm lights (properties 101+, assignments 1001+) to the lit
lanterns, chandeliers, candles and hearths of inns and other lived-in buildings that have no light
of their own; its header lists which models, where they stand, and which were left out because
their rooms are already full.

`tools/gen_window_lights.py` writes `WindowLights.cdbc` (below) and the window lights' properties
(200..219, night only).

### DoodadLightProperties.cdbc — `DoodadLightProperties.xml`

What a light is. A value of **−1 takes the default** (the lights panel's settings).

| Field | Meaning |
|---|---|
| ID | light id (what assignments refer to) |
| Name | for the editor |
| Type | 0 = point, 1 = spot |
| Color | 0xRRGGBB |
| Intensity | × the colour. The stock torches are 1.4 × 0xFF9F49 |
| Radius | yards; the light is **exactly 0** here (the stock falloff reaches ~13 yd to 5%). Default 16 |
| Falloff | shape: 1 = smooth (default), higher = tighter around the source |
| InnerAngle, OuterAngle | spot: degrees from the axis at full strength / none beyond |
| FlickerMode | −1 default (smooth), 0 off, 1 smooth (a flame), 2 noise, 3 noise steps (no easing) |
| FlickerSpeed | × the default pace |
| FlickerAmount | 0..1, how much the light dips |
| Flags | 0x1 **bake occlusion**: walls and floors between it and a surface block it (the lights panel's baking). 0x2 **night only**: it shines only at night, fading in and out with the client's own lit windows (1 at 06:00, out by 07:00; in from 20:30, full at 21:30), and is left out by day. Both in the light editor |

### DoodadLightAssignment.cdbc — `DoodadLightAssignment.xml`

Where lights go. Several rows per model are fine (a brazier with two flames).

| Field | Meaning |
|---|---|
| ID | row id |
| ModelPath | the model's path as the panel shows it; case and `/` vs `\` don't matter |
| AttachType | 0 = the model's origin, 1 = attachment point (by its ID), 2 = bone (by index), 3 = particle emitter (by index: its position, e.g. a flame) |
| AttachIndex | the attachment point ID / bone index / emitter index |
| AttachName | **reserved** for named attachment points, empty |
| OffsetX/Y/Z | yards from the attachment, in the model's space (scaled with the doodad) |
| DirectionX/Y/Z | spot lights: where the cone points, in the model's space (0, 0, −1 = down) |
| LightID | a DoodadLightProperties ID; **0 = no light** (with the flag below: only suppress) |
| Flags | 0x1 **suppress the model's own light entries** (our lights only; the client's M2 lighting still has them) |

Every model the client has in its world scene counts: map doodads, WMO doodad sets, game objects
(campfires, portals) and creatures (a fire elemental's model path gets its light too). Positions
are the model at rest (bones not animated): exact for static doodads like torches; a swinging
lantern's light would stay at its rest position, and a creature's light follows the creature but
not its animation. The **radius scales with the model's scale** (a scaled-up fire reaches further).
An attachment the model doesn't have puts the light at the origin + offset (the lights panel
counts these).

## WindowLights.cdbc — `WindowLights.xml`

Warm light from the lit windows of towns and cities at night, and the window glow. Generated by
`tools/gen_window_lights.py` from the map scan (its header says how windows are found and which
places count as lived in); one row per facade's group of windows, in world coordinates. Without the
file there are no window lights and no window glow.

The client already fades a window material's own glow in at night (MOMT flag 0x10, "self-illuminated
day/night"). Each row puts a light where that glass is and says how big the glass is:

| Field | Meaning |
|---|---|
| ID | row id (sorted by map, then position) |
| MapID | Map.dbc ID: 0, 1, 530 or 571 (no instances) |
| AreaID | AreaTable ID where the window stands (for the editor and `--survey`; not read) |
| PosX/Y/Z | where the light stands: `Back` yd in front of the glass's middle |
| FacingX/Y/Z | the way the window faces, out of the building (unit). The light is a spot along it, tilted ~24° down, so it falls on the ground and the walls in front |
| Back | yd from the light back to the glass |
| HalfWidth, HalfHeight | the glass's extent along the facade and up it (the window glow's mask) |
| Radius | yd the light reaches; 0 = its DoodadLightProperties row's |
| LightID | the DoodadLightProperties row: colour, intensity, cone, flicker, flags (the shipped ones: 200 warm 2700 K, 201 blood elf gold, 202 draenei rose, 203 Dalaran 3200 K, 204 night elf, 205 orc firelight; all night only and baked) |
| Strength | × the light's intensity (bigger glass, brighter) |
| Flags | 0x1 **glow only**: the window glows but casts no light (the tool keeps every 16-yd cell of the lights' grid to 5 window lights, the biggest windows first) |
| Comment | the place and the building, for the editor; never read |

At run time (the lights panel's **Windows** section, `[LivingAzeroth.Windows]` in config.md) the
nearest window lights within the range join the light list at night. In a full cell of the lights'
grid they rank below torches and lanterns (×0.35), and at most `LightsPerCell` of them take a place,
so the cells already full in Stormwind, Booty Bay and the Undercity keep their lights. Each light
and its glow are scaled by the share of its glass whose room is lit (WindowRooms.cdbc, below). After
editing, press "Reload light tables from disk" in the lights panel.

### WindowRooms.cdbc — `WindowRooms.xml`

Which rooms are behind each window light's glass, so the light and its glow follow the fake rooms'
night (features/Glass: rooms go dark as the night goes on; `LightsFollowRooms` in config.md), and
which of it is lit frosted glass (the glass itself goes dark by the same schedule and flickers like a
hearth; `FrostedLightsOut`, `HearthFlicker`) and which stained glass (its faint room's lights out,
`StainedRooms`). Generated with WindowLights.cdbc by `tools/gen_window_lights.py`, from the same panes,
and from gen_glass.py's GlassWindows.cdbc (the windows: one room behind each), GlassBuildings.cdbc
(which building, its seed) and GlassMaterials.cdbc (each texture's kind), as installed. One row per
window a light's glass is in.

The shader's room is the window's (GlassWindows.cdbc, below: per WMO file and group). Each of a
light's glass triangles belongs to the window of the same WMO and group whose rectangle holds its
middle, in WMO space, as the shader finds it: within 0.03 yd of the rectangle's edges and the window's
Thickness of its plane, the lower ordinal if two hold it (a cell of the shader's table lists its
windows by ordinal and the shader takes the first), so a light dims with the window its glass shows. A
triangle of stained glass (its texture's kind) is looked up among the group's stained windows only, as a
stained batch's pixel is (they're in grids of their own); any other among the rest. Whether a room is lit is an exact hash of its window (its ordinal and group) and the
building's seed (whole numbers mod 289: the same on the GPU and the CPU) against the share of rooms
dark by the hour (RoomStyles); the runtime works the hash out per window at load.

| Field | Meaning |
|---|---|
| ID | row id (by window light, then building and window) |
| WindowID | the WindowLights row |
| BuildingID | the GlassBuildings row (a city's group's row, else the placement's); 0 = none: the built-in house and `Seed` |
| CellX, CellY | the window behind the glass: its GlassWindows **Ordinal** (CellX) and **Group** (CellY), its identity in its WMO (the names are from when a room was a cell of the glass) |
| Share | the share of the light's glass in this window (0..1). The light's glass in no window (painted into a wall, a dungeon's stained glass, not glass) is 1 − the light's shares: it always lights |
| Seed | the shader's seed for an instance GlassBuildings doesn't name: PathHash of the WMO's path (bits 0..9 and 10..19, as GlassBuildings.Seed) |
| Flags | 0x1 **frosted**: the window is lit frosted glass (its band is frosted glass's: `FrostedLightsOut`, a dungeon's never; its flicker the hearth's: `HearthFlicker`; its blurred room's lamp with it). 0x2 **stained**: stained glass's faint room (a room's band and `LampFlicker`; never titan glass lit from behind), read only while stained glass has rooms (`StainedRooms`, the glass and `FakeInteriors` on). 0x4: read only while it hasn't: the cell as it was before stained glass had rooms (a stained pane's glass found in a frosted or clear window beside it), so `StainedRooms` 0 dims every light exactly as before. 0x8: read only while it has (none on the shipped scan). 0 = a room behind clear or leaded glass (its lamp flickers only with `LampFlicker`). Appended |

At run time each light's share lit is its no-room share plus each window's share × how lit that room
is now (x 1 − the flicker's dip, in time with the glass: env/GlassModel's HearthDip on the glass's
hearth clock, each window's phase from its hash): dark (its hash at or below the building's dark share), lit, or part lit while it fades. A
room the threshold crosses as the clock runs fades over 1.5 s; after a jump of the time (the climate
panel's override, the night test) every room it crossed fades together over 1.5 s (a slider being
dragged holds them until it stops). The shader's rooms fade the same way. Without the file, or with
`LightsFollowRooms` 0, the lights stay as WindowLights has them. If GlassBuildings or GlassWindows
changes, rerun gen_window_lights.py (the building IDs and the windows' ordinals come from them).

## Window glass: seven tables

Window glass that looks like glass (features/Glass, `[LivingAzeroth.Glass]` in config.md): seen from
inside, the day coming through it (stained glass glowing in its own colours, brightest with the sun
beyond it); seen from outside by day, the client's painted glass a little darker with the sky in it and, behind clear panes, a hint of a room;
at night the client's own lit windows, or a lamp-lit room behind them. Sunlit panes also cast their
light on the room's floor and walls and through its air (coloured shafts). Six tables come from
`tools/gen_glass.py` (its header says how glass is found and how buildings are classed), the seventh
(SeeThroughBuildings) from `tools/see_through_survey.py --table`; all can be edited by hand. Without GlassMaterials and GlassPanes there's no glass look, floor light or shafts;
without RoomStyles and GlassBuildings every room is the same plain warm one (and lamp-lit at night);
without GlassLead the see-through windows' per-pixel redraw keeps the glass mask (GlassMaterials');
without SeeThroughBuildings no building's windows go see-through.
After editing, press "Reload light tables from disk" in the lights panel.

### GlassMaterials.cdbc — `GlassMaterials.xml`

Which WMO textures are glass, per texture (wherever it's used: dungeons and instances too). Nearly
every window material is opaque, with the frame, the lead and sometimes a wall painted in, so each
row also says which texels are glass.

| Field | Meaning |
|---|---|
| ID | row id |
| Texture | the texture's path as the WMO names it (case and `/` vs `\` don't matter) |
| Kind | 1 clear, 2 stained, 3 frosted (translucent: icy panes, hide, parchment), 4 leaded (clear in a lead grid) |
| Flags | 0x1 the texture is a window's **inside** face (`_int_`): the inside look wherever it is; 0x2 its **outside** face (`_ext_`); 0x4 **matte** (hide, parchment: no gloss or reflection); 0x8 painted into a wall (only the masked texels are glass); 0x10 its backlit pulse is **General Vezax's** heartbeat (`VezaxPulse` scales it; the others' is a shimmer) |
| ColorR/G/B | the glass texels' average colour (0..1) |
| LightR/G/B | the colour of the light it lets in, full value 1: stained glass its own colour, clear and leaded glass nearly white (a quarter of its tint), frosted half |
| MaskR/G/B, MaskBias | which texels are glass: saturate(dot(texel.rgb, Mask) + MaskBias), 1 = glass. Mask 0 0 0 and bias 1: the whole texture |
| Share | the share of the texture's texels that are glass (for review; not read) |
| Comment | the texture's name, kind and a few WMOs it's in; never read |
| Backlit | titan glass (gen_glass.py BACKLIT): its glow from behind, x the texel, where its building has no sky behind its glass (GlassBuildings 0x40: Ulduar, the Halls of Stone and of Lightning). 0 = not titan glass. Appended 2026-10-18 |
| Pulse | the glow's pulse: its amplitude, a share of the glow (Vezax's evil glass 0.05: a few percent; the others 0.012, a very slow shimmer). 0 = steady. Appended |
| PulseSeconds | the pulse's period (s): 6.5 for the evil glass, 37 .. 59 for the others (each its own). Appended |
| PulseR/G/B | the colour the pulse adds, at luminance 1 (the evil glass's deep purple; white: a plain brightening). Appended |
| NotGlassU, NotGlassV | a corner of the texture that isn't glass: its texels from (u, v) to the far corner of its tile keep the client's colour (no glass look, no room, no lights out), and its triangles are no window's or pane's. 0.5, 0.5 on the human window atlases (`mm_elwynn`, `mm_duskwood`, `mm_rr`, `mm_westfall` `_wnd_ext__01`/`_int__01`): their fourth quadrant is a smithy's forge mouth, four glowing fire slots (gen_glass.py NOT_GLASS). 1, 1 = none. Appended 2026-10-20 |

### GlassPanes.cdbc — `GlassPanes.xml`

Every window pane of every placed WMO with glass (all four continents and the instance maps the map
scan has; no lived-in filter: a hostile or empty village's glass is glass too), in world coordinates.
Panes with no room behind them (a facade's painted windows) are left out.

| Field | Meaning |
|---|---|
| ID | row id (sorted by map, then position) |
| MapID | Map.dbc ID |
| WmoID | the WMO placement's unique ID (for the editor) |
| PosX/Y/Z | the glass's middle |
| OutX/Y/Z | the way out of the building (unit) |
| AlongX/Y/Z | along the pane (unit; horizontal for an upright window, along x for a skylight). Up the pane is Out × Along |
| HalfWidth, HalfHeight | the glass's half extent along and up the pane (yd) |
| LightR/G/B | the colour of the light it lets in (its texture's) |
| Kind | its texture's kind (as GlassMaterials): how much sun gets through (clear 1, leaded 0.85, stained 0.8, frosted 0.55) |
| RoomMinX/Y/Z, RoomMaxX/Y/Z | the room it lights: the interior group behind it, as a world box. Its light lands only inside (widened by 0.5 yd) |
| Flags | 0x1 **no sky** behind it: an underground WMO's interior pane (Ironforge's, the tram's): it puts no sunlight on the floor and makes no shafts; 0x2 **backlit**: titan glass with no real sky behind it (Ulduar, the Halls of Stone and of Lightning; an interior group's pane, or an exterior group's whose out side doesn't open to the sky): a light behind it, along its inward normal tilted 30° down, puts its colours on the floor and in the air at any hour (its texture's Backlit and pulse), not the sun's (`TitanBacklit`) |
| Comment | the WMO and the texture; never read |
| Material | its texture's GlassMaterials ID (the backlit glow's strength and pulse). Appended 2026-10-18 |

At run time the sunlit panes nearest the camera (within `Range`) light the floor: the panes whose
room holds the camera first, then the nearest, up to 12. The light lands where the ray from a surface
toward the sun leaves through a pane, only behind the pane, within 30 yd of it, and inside its room's
box. A backlit titan pane (Flags 0x2) is the same with the light behind it in place of the sun, at any hour: the
floor and walls its tilted rays reach within 30 yd, in its room's box, lit in its colours. **Leaks:** the room's box is a box, so in an L-shaped or many-storey group the light can reach a
floor that a real wall or ceiling would shade (a storey below a high window, the far side of an
arch); a building's other groups get none of a pane's light (the cathedral's side aisles when the
window is in the nave). The coloured shafts use the panes whose room holds the camera (up to 16).

### GlassWindows.cdbc — `GlassWindows.xml`

Every window of every WMO file with glass (all maps the scan has), in the WMO's own space (its file's
coordinates, z up): one set of rows per WMO file, not per placement, so every instance and the
transports have them. Behind each window there is one room (clear and leaded glass's; frosted glass's
blurred, `FrostedRooms`; stained glass's faint and tinted, `StainedRooms`): the shader finds the window a
pixel of glass is in (by its WMO's path, its group and its point in WMO space; a stained batch among
the stained windows only) and looks into that window's room. The night's lights out goes by window too
(its ordinal and group hashed with the building's seed), and WindowRooms.cdbc names the same windows. Generated by `python tools/gen_glass.py <scan> --windows [data dir]` (no
textures needed) or with the other glass tables.

How the windows are made (gen_glass.py's header has the numbers): a group's pieces of glass
(connected, coplanar triangles) are one window when they face within ~10° of each other on one plane
(within 0.15 yd), side by side or stacked with at most 0.45 yd between their rectangles (the
mullions of one window; a facade's separate windows stand 0.6 yd or more apart), up to 6 yd wide and
7 yd tall. Clear and leaded glass may share a window (its kind is the one with more glass), frosted
glass only frosted, stained glass only stained (numbered after all the others, so their ordinals are
as they were before stained glass had rooms; not a dungeon's stained glass, and not a stained sheet
bigger than a window: one cut into tiles); windows painted into a wall texture have none. Pieces whose
rectangles overlap on one plane (a duplicated face, an arch over its window) are one window whatever
its size, so a pixel lies in one window; so are two that face within ~25° of each other (or back to
back: a sheet's two faces, facing out of the building) where one's middle lies in the other's lookup,
if their glass stays within 0.3 yd of one plane. A window wider or taller than 12 yd (a tall tower window, a
wall of hide panels) is cut into equal tiles of at most 6 × 7 yd, each a window: the tiles keep the
whole window's normal, so their rectangles share its frame and never overlap (a curved sheet's tiles
stand off their plane, up to 3 yd on the Exodar's crystal walls). A window seen from both sides in two
groups (its outside face in the exterior group, its inside face in its room's group) is two windows.
Trim under 0.15 yd² is left out.

| Field | Meaning |
|---|---|
| ID | row id (by WMO path, then ordinal) |
| Wmo | the WMO's path as the maps place it, lower case with backslashes (`world\wmo\azeroth\buildings\stormwind\stormwind.wmo`); the runtime matches it by its PathHash (FNV-1a of that string, as GlassBuildings' fallback seed) |
| Group | the WMO group's index (the `_NNN.wmo` number) |
| Ordinal | the window's number in its WMO, 0..n−1: by group, then its middle from the bottom up, then x, then y (to 0.01 yd). With Group, the window's identity: its room's hash, and WindowRooms.CellX and CellY |
| Kind | as GlassMaterials: 1 clear, 4 leaded (a room behind it), 3 frosted (a blurred room; lit frosted glass's lights out), 2 stained (a faint tinted room; in grids of their own) |
| CenterX/Y/Z | the middle of the window's rectangle, on its plane (WMO space) |
| NormalX/Y/Z | the window's unit normal, out of the building (an exterior group's glass as it winds; an inside face as its exterior twin faces; else where open rays get out, as GlassPanes' Out), else as its triangles wind |
| HalfWidth, HalfHeight | the rectangle's half sizes along and up the window's frame: along = (−n.y, n.x, 0) made unit, (1, 0, 0) when n.x² + n.y² < 0.0001 (a skylight); up = n × along. Every corner of the window's glass is inside, the float32 values rounded up by 0.002 yd |
| Flags | 0x1 the window's group is an exterior one (MOGP 0x8, not 0x2000) |
| Panes | how many pieces of glass make it (for the editor) |
| Comment | the WMO, the group, the kind and the panes (and a cut window's tile); never read |
| Thickness | how far off its plane (yd) the lookup finds the window: its glass's depth off the plane + 0.05, 0.08 .. 1 (a flat pane's 0.08, so a neighbour at a slant isn't in it). A pixel is in a window within 0.03 yd of its rectangle and this of its plane; where two hold it, the lower ordinal's. 0.3 without it. Appended |

On the 2026-10-17 scan: 16,559 windows in 276 WMOs and 840 groups (509 clear, 2,944 leaded, 7,119
frosted, 5,987 stained: Dalaran 2,558, Silvermoon 1,151, Stormwind 320, the abbeys, chapels and
cathedrals), at most 1,115 in one group; 2,544 windows of more than one piece, 2,131 tiles of 412 big
windows (stained glass's big sheets are left out). The lookup finds 98% of the windows at their middle (99% of
those with a room); the rest are panes at a slant over a lower-numbered window (a bay's side lights,
a siege engine's curved glass), where the pixel takes that window's room. WindowRooms: 988 rooms for
887 lights, 421 lit frosted windows at 333 lights, 1,549 stained windows at 1,191 lights (Dalaran 356);
28 cells read only without stained glass's rooms (Dalaran's stained panes beside frosted windows).

### RoomStyles.cdbc — `RoomStyles.xml`

How the rooms behind clear and leaded glass look, one row per pair of a building's **class** and its
place's **palette** in use (about 100). The class (from the WMO's file name, or a city group's name
or district) sets the layouts' weights, the brightness, the rooms dark by day and how late the lights
stay on; the palette (the culture by the path: vrykul, blood elf, Dalaran, goblin, Forsaken, orc...,
else the place: Duskwood, the Scarlet Crusade's towns, Forsaken zones, goblin ports, Northrend's and
Outland's humans) the colours, x the layouts' weights. Per window the shader hashes the window (its
GlassWindows ordinal and group) with the building's seed to pick the layout, its light, the lamp's place, the curtains' gap and the things on the shelves;
and, with an exact hash the window lights share, whether it's dark or lit.

| Field | Meaning |
|---|---|
| ID | row id (GlassBuildings.Style) |
| Class | 0 other, 1 house, 2 farm, 3 inn, 4 shop (smithy, bank, lab, trade), 5 temple, 6 keep (barracks, towers, walls, town halls, dungeons), 7 tower (mage towers: books), 8 city (a city's mixed buildings), 9 ship; Ironforge's parts: 10 forge (the Great Forge, the smithies), 11 commons (the ring road's shops, the bank, the inn, the halls, the High Seat, the gates), 12 quarter (the Military and Mystic Wards' homes), 13 cavern (the Forlorn Cavern). Read: the rooms' depth and furniture, and with the palette the texture set (a farm or ship of the human palette the rustic one, Ironforge's parts the dwarven); the panel's line |
| Palette | the generator's palette (PALETTES in gen_glass.py): the rooms' texture set (with the class) |
| TintR/G/B, Tint2R/G/B | the rooms' colour by day at the two ends of a building's range: each building (in a city, each 14-yd block) picks a point between them by its hash |
| LampR/G/B | the lamp at night (any brightness: the runtime makes a lit room as bright as the client's lit window) |
| CurtainR/G/B | the curtains' cloth |
| Brightness | x the rooms' light by day |
| Spread | buildings are brighter or dimmer by up to this share |
| DarkShare | the share of rooms dark by day (0..1) |
| Shelves, Curtains, Divider, Plain | the layouts' weights (any scale): a back wall with shelves and things on them; curtains half drawn (cloth just behind the glass at the sides, folded, a gap between); a dividing wall half way back with a doorway to a second, dimmer room; plain |
| LateHours | the night's lights go out this many hours later (an inn 2.5, a tower 2, a keep 1.5), earlier when negative (a shop -1, a farm -0.5) |
| NightCap | at most this share of its rooms dark at night (an inn 0.55, a tower 0.6) |
| Comment | the class and palette; never read |

**The night** (env/GlassModel `LightsOutShare`, by the client's clock or the climate panel's time):
behind the windows the client lights at night (their material is SIDN, self-lit), a room is lamp-lit
unless it's one of the dark ones. 8% are dark through the evening; from 21:00 the share rises to half
at midnight, 72% at 02:00 and 78% at 04:30; early risers bring it back to 60% by 06:00, when the
day takes over. LateHours shifts the evening's rise, NightCap caps it. Windows the client doesn't
light have dark rooms at night. In a dark place every room is dark at night. A room going dark (or lit
again) fades over 1.5 s, and the window lights in front of it with it (WindowRooms.cdbc). With
RoomVariety off every room is lit all night.

**Frosted glass** has no room, but the windows the client lights (SIDN, or unlit: drawn at full day and
night) keep the same hours by their building's style: a dark one keeps 15% of its glow at night, a lit
one flickers like a hearth (`FrostedLightsOut`, `HearthFlicker` in config.md; with FrostedLightsOut
the rooms' variety doesn't matter). A dungeon's (GlassBuildings 0x20) keeps the client's look.

**Ironforge** (gen_glass.py IRONFORGE_AREAS): each glass group's class by the client's own sub-zone for
it (WMOAreaTable.dbc, by the root's wmoID and the group's ID), a smithy by its name:

| Sub-zone (WMOAreaTable) | Class | Glass groups |
|---|---|---|
| The Great Forge | forge | 15 (and the smithy `InteriorSmithA01` on the ring road) |
| Ironforge (the ring road round the forge: Cataclysm's "The Commons"; the gates' halls) | commons | 23 (and the gate's outside, group 0, with no sub-zone) |
| Hall of Explorers, The High Seat | commons | 5, 1 |
| The Military Ward, The Mystic Ward | quarter | 7, 10 |
| The Forlorn Cavern | cavern | 5 |
| Tinker Town, Hall of Arms, Old Ironforge, Hall of Mysteries, the Vault, the Library | (no glass) | |

| Hour | house | farm | inn | shop | temple | keep | tower | city | forge | commons | quarter | cavern |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 22:00 | 22% | 29% | 8% | 36% | 8% | 8% | 8% | 15% | 4% | 8% | 8% | 36% |
| 00:00 | 50% | 55% | 15% | 61% | 36% | 29% | 22% | 43% | 4% | 15% | 36% | 61% |
| 03:00 | 74% | 76% | 55% | 77% | 70% | 66% | 60% | 73% | 4% | 25% | 45% | 77% |
| 05:30 | 66% | 60% | 55% | 60% | 70% | 70% | 60% | 72% | 4% | 25% | 45% | 60% |

### GlassBuildings.cdbc — `GlassBuildings.xml`

Every placement of a WMO with glass (all maps the scan has), each with its style and seed; a city's
one big WMO (Stormwind, Dalaran, Silvermoon, Booty Bay, Orgrimmar, Darnassus, Ironforge, the Exodar)
also has a row per group with glass (its own building: `shopB18`, `interior_inn`, `bankinterior`);
and one default per WMO and map for instances not in the table (transports, a placement the scan
missed). At run time features/WmoIce's group hook (`Wmo.RenderGroup`) gives each placed instance's
origin, its WMO's path and the group; the building is the row whose origin is within 1.5 yd and whose
WMO is the same, then its group's row if it has one, else the WMO's default, else a built-in house.

| Field | Meaning |
|---|---|
| ID | row id (placements by map and unique ID, their city groups after them, the defaults last) |
| MapID | Map.dbc ID |
| UniqueID | the placement's unique ID (MODF); 0 for a WMO's default |
| PosX/Y/Z | the placement's origin (its WMO's 0, 0, 0) in world coordinates (a WMO's default: 0, 0, 0, any instance) |
| Group | the WMO group's index for a city's group, else 4294967295 (the whole placement) |
| Style | its RoomStyles row |
| Seed | its hash seed (a CRC of the map, the unique ID and the group): two numbers 0..1023 (bits 0..9, 10..19) hashed with each window's ordinal and group (GlassWindows) |
| Flags | 0x1 a city (the shader's 14-yd blocks are its buildings: each its own tint and brightness); 0x2 a dark place (gen_window_lights' DARK_AREAS or DARK_PATHS: ruined, abandoned, haunted, dungeons, ships): no lit rooms at night; 0x4 empty: every room dark (a share of each class's buildings whose windows the client doesn't light: SIDN windows, or unlit frosted glass); 0x8 its WMO has windows the client lights at night (for the editor); 0x10 **underground**: no sky behind its glass (Ironforge's interior groups, the tram): the street's look whatever the camera's indoors, no sky, darkening or glint in it; 0x20 a **dungeon's** WMO (its path): its frosted windows keep the client's look (no lights out, no flicker); 0x40 **backlit**: its titan glass has no real sky behind it (Ulduar, the Halls of Stone and of Lightning; the placement's flag what most of its titan glass is, by area, a group row for each group the other way): that glass glows from behind in its own colours at any hour, the inside look with no sky, sun or glint (`TitanBacklit`) |
| AreaID | AreaTable ID at its origin (a city group's: at its middle), for the editor |
| Wmo | the WMO's path (case and slashes don't matter): the instance must be of this WMO |
| Comment | the WMO, its class and palette, the area; never read |

### GlassLead.cdbc — `GlassLead.xml`

Where a glass texture's panes are, for the see-through windows' per-pixel redraw (`SeeThroughRedraw`
1): its own mask, glass for every pane's inside, lit and shaded alike, opaque for the lead, the frame
with its mullions and tracery, and the shutters. GlassMaterials' mask is the light's: it counts only
the lit glass, so a pane's shaded lower half read as frame and the room hardly showed through the
inn's windows ("yeah build the lead mask pls :D", the user, 2026-10-21). One row per rectangle, by hand
from the texture's layout (gen_glass.py LEAD_LAYOUTS: the eight human window atlases share one, 19
rectangles). The mask itself is built at run time from the texture as the client reads it (never
stored): the texels inside the rectangles that aren't lead, i.e. not darker than Dark x the median
luminance of the lit glass in them (GlassMaterials' mask over a half) and not on a thin dark line (the
luminance closed over 5 x 5 texels, minus it, under Line x that median). A texture with no rows keeps
GlassMaterials' mask in the redraw. Since 2026-10-22 ("the GlassLead rectangles can't follow curved
tracery, so derive the lead mask from the texture itself"): stained glass's sheets (Northshire Abbey's
`mm_stained_01`, the Redridge chapel's `mm_redridge_stained_01`: their three designs a quadrant each and
the lattice window's four lights, 7 rows) and Northrend's arched human windows (`nr_hu_window`,
`nr_hu_window_half`, `nr_hu_interwindow`: one row, the whole texture), each with its own Dark (0.4 the
abbey's, 0.5 the others': the frame's wood and stone by their darkness against the glass). With one row
over the whole texture a texel must also be lit (GlassMaterials' mask over a half) or coloured (1 - its
least channel / its largest at least 0.3): the flat grey round Northrend's windows is as bright as their
glass's shade. 169 rows over 13 textures.

| Field | Meaning |
|---|---|
| ID | row id |
| Texture | the texture's path as GlassMaterials names it (case and slashes don't matter); it must be a GlassMaterials texture |
| U0, V0, U1, V1 | a rectangle of panes in the texture's uv (0..1; half-open; v down, as the texture's rows) |
| Dark | lead (and, over a whole texture, the frame): darker than this x the lit glass's median luminance (0.18; 0.4 .. 0.5 for stained glass and Northrend's windows) |
| Line | lead: a thin dark line deeper than this x that median (0.15) |
| Comment | the layout and the rectangle's number; never read |

### SeeThroughBuildings.cdbc — `SeeThroughBuildings.xml`

The buildings whose windows go see-through (features/SeeThrough: `SeeThroughWindows`, `SeeThroughFromInside`),
one row per WMO: from outside its real modelled room behind the glass, from inside the street through it
(docs/r&d/see-through-windows.md). Written by `tools/see_through_survey.py --table` from its SEE_THROUGH
list: the WMOs with the human window atlases (GlassLead's) whose glass can go see-through and whose rooms
the gate opens from most street views with few flips (the survey of 2026-10-22: the inns, the two-storey
houses, the mage towers, the farmhouses, Redridge's prison camp headquarters; 24), and since stained glass
(2026-10-22) the chapel (Lakeshire's, Tyr's Hand's, New Hearthglen's), Northshire Abbey and New Hearthglen's,
Northrend's human barracks (two), its town hall, the Isle of Conquest's keep and Valgarde's inn (from
outside only) and Wintergarde's small house: 33. A WMO not here keeps its windows as they are. Every placement of a WMO
counts, on every map.

| Field | Meaning |
|---|---|
| ID | row id |
| Wmo | the WMO's path (case and slashes don't matter) |
| Flags | 0x1 from outside (its windows open onto the room behind them), 0x2 from inside (its rooms' own windows open onto the street); 0 = neither (as if the row weren't there) |
| Comment | which building; never read |

## CrystalMaterials.cdbc — `CrystalMaterials.xml`

Which textures are crystal (`CrystalLook`, features/Crystal): a model's or a building's batch whose first texture is
one of these, drawn opaque or alpha-keyed, looks like a deep, semi-transparent gem, as the ice does (facets and
sparkles inside it, its colour deeper toward the core, a rim, the sun's glint, an inner glow stronger at night). The
crystals' own see-through shells, additive glows and environment-map passes keep the client's look, and so do crystals
drawn only see-through or glowing. By file name, wherever the texture is used (any folder, M2s and WMOs alike).
Generated by `python tools/gen_crystals.py [data dir]` from its reviewed list (its header says how the list was made;
`--check <folder of BLPs>` shows what each mask keeps). After editing, press "Reload CrystalMaterials" in the climate
panel (or "Reload light tables from disk" in the lights panel).

**-1 inherits** the row's family's value (features/Crystal's kFamilies); the mask's fields are signed and inherit at
**-1000** and below (no mask).

| Field | Meaning |
|---|---|
| ID | row id |
| Texture | the texture's file name (case, the folder and `.blp` don't matter) |
| Family | its look: 1 draenei violet, 2 naaru gold, 3 Shattrath teal, 4 blood red, 5 fel green, 6 nether purple, 7 Crystalsong white, 8 mana blue, 9 arcane violet, 10 ruby, 11 prism (the texture's own hue; give it a glow colour). 0: the row is ignored |
| Flags | reserved, 0 |
| Weight | how fully it takes the look, 0..1 (0.8 for crystals under their own alpha shells) |
| TintR/G/B | the colour the body deepens toward at the core (x its own colour) |
| GlowR/G/B | the inner glow's colour |
| Glow | the glow's strength by day (x `CrystalGlow`; half for a material the client draws unlit) |
| NightGlow | added to it at night |
| Deepen | how far the body deepens toward the tint face on, 0..1 |
| Rim | the rim of its own colour at a grazing angle, 0..1 |
| FacetSize | yards a repeat of the facets inside a hand-sized crystal (a gem 1-2, a big crystal 3); a bigger crystal's grow with its size, up to 8 times (features/Crystal's CellScale, from the size of the pieces its batch is made of), x `CrystalCellScale` |
| MaskR/G/B, MaskBias | which texels take the look on a texture they share with foliage, rock or a frame: saturate(dot(colour, Mask) / (luminance + 0.04) + MaskBias), on the lit colour (a test of hue); -1000: all of them |
| Comment | the texture, its family and where it's used; never read |
| Model | M2s only: the model's path must contain this (case and slashes don't matter); empty: any model and any WMO. For a texture shared with what isn't crystal (Terokkar's atlas: its trees too) |

## AreaClimate.cdbc — `AreaClimate.xml`

The air's baseline per place: temperature through the day and the year, and humidity. Read by the
ground moisture (drying) now; later by snow melting and freezing, frost, fog and breath.

| Field | Meaning |
|---|---|
| ID | row id |
| ScopeType | 0 = Global, 1 = Map, 2 = Area |
| ScopeID | Map.dbc ID or AreaTable ID (ignored for Global) |
| DayTemp | °C at the warmest time of day (15:00). Signed: −1000 inherits |
| NightTemp | °C at the coldest (05:00). Signed: −1000 inherits |
| SeasonAmplitude | °C between midsummer and midwinter (Elwynn ~12, jungle ~2, Outland 0) |
| SeasonOffset | months the seasons are shifted (6 = flipped, a "southern" continent) |
| Humidity | 0 = dry air .. 1 = saturated: humid places dry slowly (later: fog, dust) |
| Flags | reserved, 0 |
| Fogginess | 0 .. 1: how foggy the place is by nature; drives area fog, boosts mist. −1 inherits. Appended 2026-10-08 |
| Comment | free text for the editor (which place the row is); never read. `tools/fill_comments.py` fills empty ones with the place's name. Appended 2026-10-11 |
| CloudBase | world z: a cloud deck in the air from here up to CloudTop over the whole view (the fog renderer's own pass): a mountain zone's upper slopes, peaks and buildings in cloud. Its Fogginess sets the deck's density, and the place gets no ground fog of its own. ≤ −1000 inherits / none. Appended 2026-10-13 (ChromieCraft fork; tools/add_mountain_clouds.py) |
| CloudTop | world z: the cloud deck's top; ≤ −1000: CloudBase + the fog panel's deck thickness. Appended 2026-10-13 (ChromieCraft fork) |

Lookup: Global < Map < Area (zone) < Area (sub-area); the more specific row wins **field by field**.
No row at all: 18 °C day, 8 °C night, ±6 °C over the year, humidity 0.5, fogginess 0.

**Temperature** = night .. day by the time of day (the sky's clock) + the season (from the server's
game date: warmest around 20 July, shifted by SeasonOffset) + the weather (rain −3 °C, snow −6 °C,
sandstorm +2 °C, × its intensity). The climate panel shows each part, and can override the weather
and the time of day for testing.

Generator: `tools/gen_default_climate.py` (one Global row; `--examples` adds Northrend, Outland,
Winterspring, Dun Morogh, Tanaris, Stranglethorn, Swamp of Sorrows and Duskwood).

## FogSettings.cdbc — `FogSettings.xml`

How the ground fog behaves and looks, per place and fog kind. Optional: without the file (or for
any field no row sets) the module's built-in values for the kind apply.

| Field | Meaning |
|---|---|
| ID | row id |
| ScopeType | 0 = Global, 1 = Map, 2 = Area |
| ScopeID | Map.dbc ID or AreaTable ID (ignored for Global) |
| Kind | 0 = all kinds, 1 mist, 2 water fog, 3 steam, 4 area fog, 5 haze |
| Strength | multiplies the kind's target from its rules (0 = never here) |
| Density | extinction per yd at the ground at full amount (mist ~0.06) |
| Thickness | yd: layer height at full amount (the pool's depth for heavy fog) |
| BuildUp / BurnOff | half-lives (s) toward a higher / lower target |
| WindThin | wind strength (0..1) above which it can't settle |
| WindDrift | how strongly the wind moves it (0..1) |
| Patchiness | noise amount (0 smooth .. 1 patchy) |
| Weight | 0 = rises and thins upward .. 1 = heavy: flat-topped, flows downhill, pools in hollows |
| ColorMode | 0 = the zone's stock fog colour (Color is ignored), 1 = hue (the stock colour's brightness with Color's hue and saturation: follows day and night), 2 = replace (Color as is); −1 inherits. No row sets a mode but one sets a Color: hue |
| ColorR/G/B | 0 .. 1 |
| Saturation | 0 = grey .. 1 = as is (on top of the mode) |
| Brightness | multiplier |
| Flags | reserved, 0 |
| Comment | free text for the editor (which place and kind the row is); never read. `tools/fill_comments.py` fills empty ones ("Duskwood - area fog"). Appended 2026-10-11 |

All floats: −1 takes the field from the next row. Lookup (workspace rule, most specific wins):
place first -- the area, its parent zones, the map, global -- and within each place the kind's
own row before the Kind 0 row. Values never add up.

Colour per kind: a kind's own row can set its colour (e.g. whiter steam), the Kind 0 row is the
fallback for every kind. Where kinds mix, the fog shows their colours by each one's share there.
The fog panel's "colour test" applies values live to every kind, to find them by eye before writing
a row.

Generator: `tools/gen_default_fog_settings.py` (no default rows; `--examples` adds colour rows for
Swamp of Sorrows and Duskwood).
