#!/usr/bin/env python3
"""Ash and sand covers: thin layers that characters leave trails in, like the snow cover (features/
SurfaceCover). Sets the rows below in existing GroundMaterial.cdbc, GroundMaterialSelector.cdbc and
SurfaceCover.cdbc files and keeps every other row as it is.

Where (global texture rows; the terrain textures as a map scan of the zones found them):
  Ash   Searing Gorge and Burning Steppes (one tileset): BurningSteppsAsh01 (most of both zones),
        BurningSteppsAshCracks, BurningSteppsCharcoal01. Terokkar's Bone Wastes: BoneWastesDirtBase,
        BoneWastesDirtCracked02, BoneWastesDirtHoles. Not the rock, lava, road, stone tile and bone
        textures. The Blasted Lands (dirt, rock, charred rock) and Hellfire (Outland dirt and rock)
        have no ash textures and stay bare.
  Sand  Tanaris: TanarisSandBase01, TanarisSandStones. Silithus: SilithisSand01, SilithisSandhighlight,
        SilithisSandDetail. These rows were already on (0.05..0.1 yd); now one sand behaviour. Not
        Tanaris' cracked ground, rock and dirt-path textures, Silithus' rock highlight row (its own
        0.01 yd dusting stays) or Westfall's sand rows.
  Sand  beaches painted with Darkshore's sand: DarkshoreSand3 and DarkshoreSandRocks (Darkshore's coast, and
        Azuremyst Isle's beaches use them too: "sand not working on azuremyst isle tho", the user). Not its
        wet strip at the water's edge (DarkshoreSandWaves): the cover's paler tint would read as dry sand there.
  Sand  in the Caverns of Time only (area 2300; rows scoped to it): TanarisRockBaseBurn and
        TanarisRockBase01, which paint its floor over the sand (the client's sand footprints walk
        on them there). Elsewhere in Tanaris they are its rock and stay bare.

How: two parent materials carry the shared cover row, and each texture's material (Parent = Ash or
Sand) adds its own CoverTexture (the terrain's own texture, so the cover looks like the ground it
lies on), a tint and, where it differs, the depth. Ash and Sand set no material properties, so
ground moisture, heat and everything else read these textures exactly as before; the existing
BurningSteppsAsh01 material keeps its warmth (35 degC) and rest moisture.
  Ash   0.10 yd (cracked crust 0.06, charcoal and holes 0.08), bare above 35 degrees (thinning from
        23), fills back in over 20 s (snow: 30), soft (feet press to the ground).
  Sand  0.08 yd (stony and detail sand 0.06), bare above 30 degrees (from 20), fills back in over 10 s:
        the wind smooths it.
  Sand  no trenches: feet leave still rings of sand around each of the client's prints instead (flag 0x10;
        "Simon is of the opinion that sand shouldn't have trenches but should rather have unmoving 'ripples'
        centered on each footprint", the user, 2026-10-11).
  Both  not under water (flag 0x1); the base sits 0.02 yd (sand 0.015) below the terrain, so for the
        first fifth of the fill-in time a fresh trench shows the ground itself (and the client's
        own footprints on it) before the refill rises over it.
  Tint  every texture's own average colour x 1.68 (capped at 255) at strength 0.2: with the cover's
        0.93 texture brightness that comes out ~8% lighter than the ground, same hue, a little
        softer -- loose ash or sand on top, so trails show the darker ground. Tanaris' sand is
        already near white: there it only pales a little. The averages were measured from the
        client's textures (3.3.5a, the _s variants have the same colours).

IDs: materials, selectors and cover rows 50 (Ash) and 51 (Sand), 52..56 (the new ash textures), 57, 58
(the Caverns of Time's), 59, 60 (Darkshore's sand), 61..67 (the coasts the map survey found), 70 (Mud) and 71..75
(mud shores; tint at half strength, mud is darker);
the existing rows changed in place: materials 2, 3, 6, 7, 8, 9 (Parent Sand) and 15 (Parent Ash), cover
rows 2, 3, 6, 7, 8, 9 and 14. Westfall's own sand row (6: 0.05 yd, trenches, no tint) becomes a sand row like
the rest; its wet strip (11, WestfallSandBaseWet) keeps its own row: wet sand holds sharp prints.

Usage:  python add_ash_sand_cover_rows.py [dir]     (default: ../data; for the client: <client>/DBFilesClient)
Sets the rows to exactly these values, so running it twice changes nothing. Then press "Reload table"
in the surface cover panel.
"""

import sys
from pathlib import Path

import cdbc_io as io

ASH, SAND, MUD = 50, 51, 70
I, T = -1.0, -1000.0  # "take it from the parent" (floats), "not set" (signed columns)
PREVENT_UNDERWATER = 0x1
USE_VERTEX_COLOR = 0x4  # tinted by the terrain's MCCV like the ground (Northrend's grey sands get their colour from it)
RIPPLE_PRINTS = 0x10  # feet carve no trench; still rings around each print instead (sand)
TINT_GAIN, TINT_STRENGTH = 1.68, 0.2

# Texture, its average colour (0xRRGGBB, measured), material ID, selector ID (None: the row exists),
# cover row ID, depth (I: the parent's), parent.
TEXTURES = [
    (r"Tileset\BurningStepps\BurningSteppsAsh01.blp",               0x6F6B5C, 15, None, 14, I,    ASH),
    (r"Tileset\BurningStepps\BurningSteppsAshCracks.blp",           0x4D4C44, 52, 52,   52, 0.06, ASH),
    (r"Tileset\BurningStepps\BurningSteppsCharcoal01.blp",          0x464646, 53, 53,   53, 0.08, ASH),
    (r"Tileset\Expansion01\BoneWastes\BoneWastesDirtBase.blp",      0x576362, 54, 54,   54, I,    ASH),
    (r"Tileset\Expansion01\BoneWastes\BoneWastesDirtCracked02.blp", 0x6B807D, 55, 55,   55, 0.06, ASH),
    (r"Tileset\Expansion01\BoneWastes\BoneWastesDirtHoles.blp",     0x3B4347, 56, 56,   56, 0.08, ASH),
    (r"Tileset\Tanaris\TanarisSandBase01.blp",                      0xECD8A2, 2,  None, 2,  I,    SAND),
    (r"Tileset\Tanaris\TanarisSandStones.blp",                      0xF1E8C2, 3,  None, 3,  0.06, SAND),
    (r"Tileset\Silithis\SilithisSand01.blp",                        0x685635, 7,  None, 7,  I,    SAND),
    (r"Tileset\Silithis\SilithisSandhighlight.blp",                 0xA6946B, 8,  None, 8,  I,    SAND),
    (r"Tileset\Silithis\SilithisSandDetail.blp",                    0x4E3E27, 9,  None, 9,  0.06, SAND),
    (r"Tileset\Darkshore\DarkshoreSand3.blp",                       0x565656, 59, 59,   59, I,    SAND),
    (r"Tileset\Darkshore\DarkshoreSandRocks.blp",                   0x767D7B, 60, 60,   60, 0.06, SAND),
    # The coasts the full map survey found (waterfall-scan/beach_survey.py, 2026-10-11: "also look for other beaches,
    # there should be lots of coasts everywhere"), the textures that are sand by look:
    (r"Tileset\Westwood\WestfallSandBase.blp",                      0xB0995F, 6,  None, 6,  I,    SAND),  # Westfall, Stranglethorn's coasts, Dustwallow's Dreadmurk Shore
    (r"Tileset\Darkshore\DarkshoreSand.blp",                        0x636363, 61, 61,   61, I,    SAND),  # Darkshore, Azshara, Ashenvale's and Tanaris' coasts
    (r"Tileset\Expansion02\BoreanTundra\BT_SandA.blp",              0x6C675C, 62, 62,   62, I,    SAND),  # Borean Tundra, Strand of the Ancients, Isle of Conquest
    (r"Tileset\Expansion02\BoreanTundra\BT_SandC.blp",              0x4C4B46, 63, 63,   63, I,    SAND),
    (r"Tileset\Expansion02\BoreanTundra\BT_SandE.blp",              0x52514C, 64, 64,   64, 0.06, SAND),  # with pebbles
    (r"Tileset\Expansion02\SholazarBasin\SB_SandB.blp",             0xB4A892, 65, 65,   65, I,    SAND),  # Sholazar's river and lake sand
    (r"Tileset\Ashenvale\AshenvaleSand.blp",                        0x5B6243, 66, 66,   66, I,    SAND),  # the Zoram Strand
    (r"Tileset\Feralas\FeralasSand.blp",                            0x9B7A49, 67, 67,   67, 0.06, SAND),  # Un'Goro's and Feralas' lake shores, pebbly
    # Mud on dry shores rings like sand ("mud would make sense to have the footprint rings the way sand does. Pebbles
    # not so much", the user, 2026-10-11). Not the pebble shores (Howling Fjord's ShoreC, Tirisfal's, Zul'Drak's
    # gravelly mud) nor the grassy and mossy mucks; the underwater mud rows (12, 13) stay as they are.
    (r"Tileset\Desolace\DesolaceMudSolid.blp",                      0x7C550D, 71, 71,   71, I,    MUD),   # Desolace's mud flats
    (r"Tileset\Expansion02\HowlingFjord\HFjords_ShoreD.blp",        0x2B241A, 72, 72,   72, I,    MUD),   # Howling Fjord's muddy shore
    (r"Tileset\Silverpine\SilverpineShore.blp",                     0x373E2C, 73, 73,   73, I,    MUD),   # Silverpine's shore silt
    (r"Tileset\Swamp of Sorrows\SwampSorrowsDirt02.blp",            0xB7A84F, 74, 74,   74, I,    MUD),   # the Misty Reed Strand
    (r"Tileset\Loch Modan\LochModanMud.blp",                        0x3F361C, 75, 75,   75, I,    MUD),   # the Loch's shores
]

# The same, for one area only (selector ScopeType 2 = Area, ScopeID = the AreaTable ID): "it would also be dope if
# we could have sand effects in the caverns of time" (the user, 2026-10-11). Its floor (area 2300) is painted with
# Tanaris' rock-base textures over the sand.
AREA_SCOPE, CAVERNS_OF_TIME = 2, 2300
SCOPED = [
    (r"Tileset\Tanaris\TanarisRockBaseBurn.blp",                    0x573A14, 57, 57,   57, I,    SAND, AREA_SCOPE, CAVERNS_OF_TIME),
    (r"Tileset\Tanaris\TanarisRockBase01.blp",                      0xD2851F, 58, 58,   58, I,    SAND, AREA_SCOPE, CAVERNS_OF_TIME),
]


def own_flags(parent):
    # A row with its own Depth decides Flags too (the rest take the parent's).
    return PREVENT_UNDERWATER | (RIPPLE_PRINTS | USE_VERTEX_COLOR if parent in (SAND, MUD) else 0)


def tint_of(average):
    return sum(min(255, round(((average >> shift) & 0xFF) * TINT_GAIN)) << shift for shift in (16, 8, 0))


def material(id, name, parent=0):
    # ID, Name, Parent, RestMoisture, Stiffness, Flags, Absorbency, Temperature: no properties of its own
    return (id, name, parent, I, I, 0, I, T)


def cover(id, material_id, depth=I, max_slope=I, slope_fade=I, drift=I, breakup=I, rim=I, relax=I,
          tint_color=0, tint_strength=I, cover_texture="", z_offset=I, wetness=I, flags=0):
    # ID MaterialID CoverMaterial Depth MaxSlope SlopeFade DriftNoise EdgeBreakup Rim RelaxSeconds TintColor
    # TintStrength CoverTexture Opacity Flatten ZOffset Wetness Flags MeltKeptShare MeltGoneTemperature
    return (id, material_id, 0, depth, max_slope, slope_fade, drift, breakup, rim, relax, tint_color,
            tint_strength, cover_texture, I, I, z_offset, wetness, flags, I, T)


PARENT_COVERS = [
    cover(ASH, ASH, depth=0.10, max_slope=35.0, slope_fade=12.0, drift=0.25, breakup=0.6, rim=0.4, relax=20.0,
          tint_strength=0.0, z_offset=-0.02, wetness=0.0, flags=PREVENT_UNDERWATER),
    cover(SAND, SAND, depth=0.08, max_slope=30.0, slope_fade=10.0, drift=0.35, breakup=0.5, rim=0.5, relax=10.0,
          tint_strength=0.0, z_offset=-0.015, wetness=0.0, flags=PREVENT_UNDERWATER | RIPPLE_PRINTS | USE_VERTEX_COLOR),
    # Mud: a thin damp layer of the ground's own texture, rings around prints that last twice as long as sand's.
    cover(MUD, MUD, depth=0.05, max_slope=25.0, slope_fade=10.0, drift=0.2, breakup=0.5, rim=0.3, relax=20.0,
          tint_strength=0.0, z_offset=-0.02, wetness=0.25, flags=PREVENT_UNDERWATER | RIPPLE_PRINTS | USE_VERTEX_COLOR),
]


def decode(path, columns):
    _, raw, string_at = io.read(path.read_bytes())
    rows = []
    for r in raw:
        row = []
        for kind, value in zip(columns, r):
            row.append(io.as_float(value) if kind == "f" else io.as_int(value) if kind == "i" else string_at(value) if kind == "s" else value)
        rows.append(tuple(row))
    return rows


def upsert(rows, row, columns):
    """Replaces the row with row[0]'s ID (or adds it). Returns 1 if that changes the file's bytes."""
    def stored(r):  # as the file stores it (floats are 32-bit there)
        return tuple(io.as_float(io.encode("f", v)) if kind == "f" else v for kind, v in zip(columns, r))
    for k, r in enumerate(rows):
        if r[0] == row[0]:
            if stored(r) == stored(row):
                return 0
            rows[k] = row
            return 1
    rows.append(row)
    return 1


def main() -> None:
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent.parent / "data"
    paths = {name: folder / f"{name}.cdbc" for name in ("GroundMaterial", "GroundMaterialSelector", "SurfaceCover")}
    materials = decode(paths["GroundMaterial"], io.GROUND_MATERIAL)
    selectors = decode(paths["GroundMaterialSelector"], io.GROUND_MATERIAL_SELECTOR)
    covers = decode(paths["SurfaceCover"], io.SURFACE_COVER)

    changed = 0
    changed += upsert(materials, material(ASH, "Ash"), io.GROUND_MATERIAL)
    changed += upsert(materials, material(SAND, "Sand"), io.GROUND_MATERIAL)
    changed += upsert(materials, material(MUD, "Mud"), io.GROUND_MATERIAL)
    for row in PARENT_COVERS:
        changed += upsert(covers, row, io.SURFACE_COVER)
    for texture, average, material_id, selector_id, cover_id, depth, parent, scope, scope_id in (
            [t + (0, 0) for t in TEXTURES] + SCOPED):
        name = texture.rsplit("\\", 1)[-1][:-4]
        old = next((m for m in materials if m[0] == material_id), None)
        if old:
            # An existing material: only its Parent changes (its name and properties stay).
            changed += upsert(materials, (old[0], old[1], parent) + tuple(old[3:]), io.GROUND_MATERIAL)
        else:
            changed += upsert(materials, material(material_id, name, parent), io.GROUND_MATERIAL)
        if selector_id is not None:
            # ID, ScopeType, ScopeID, TexturePath, GroundEffectID, TerrainType, LiquidType, MaterialID, Flags
            changed += upsert(selectors, (selector_id, scope, scope_id, texture, 0, -1, 0, material_id, 0), io.GROUND_MATERIAL_SELECTOR)
        elif not any(s[7] == material_id and s[3].lower() == texture.lower() for s in selectors):
            raise SystemExit(f"{paths['GroundMaterialSelector']}: no selector row puts material {material_id} on {texture}")
        # Depth (and with it Flags) where it differs from the parent; everything else from the parent.
        changed += upsert(covers, cover(cover_id, material_id, depth=depth, tint_color=tint_of(average),
                                        tint_strength=TINT_STRENGTH / 2 if parent == MUD else TINT_STRENGTH,
                                        cover_texture=texture,
                                        flags=own_flags(parent) if depth != I else 0), io.SURFACE_COVER)

    paths["GroundMaterial"].write_bytes(io.build(io.GROUND_MATERIAL, materials))
    paths["GroundMaterialSelector"].write_bytes(io.build(io.GROUND_MATERIAL_SELECTOR, selectors))
    paths["SurfaceCover"].write_bytes(io.build(io.SURFACE_COVER, covers))
    print(f"{folder}: {changed} row(s) set ({len(materials)} materials, {len(selectors)} selectors, {len(covers)} cover rows)")


if __name__ == "__main__":
    main()
