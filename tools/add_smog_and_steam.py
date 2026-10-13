#!/usr/bin/env python3
"""Smog and steam for the ground fog: smoky places get a smoky area fog, hot springs steam. Sets the rows below in
existing AreaClimate.cdbc, FogSettings.cdbc, GroundMaterial.cdbc and GroundMaterialSelector.cdbc files and keeps every
other row as it is ("can we have smoke in smoky zones like burning steppes/searing gorge? ... more like smog? the geyser
fields in borean tundra should have steam also", "smoke in stratholme", the user, 2026-10-13).

Smog  the area fog kind (env/Fog: a place's Fogginess, more at night), with a smoke look in FogSettings: a tall
      (14 yd), thin, patchy layer that rises instead of pooling (Weight 0), drifts with the wind and only clears in a
      strong one. The Steppes, the Gorge and the mountain: the stock fog's brightness (it follows the time of day) in a
      brown-grey hue, darker.
        0.6  Burning Steppes (46), Stratholme (map 329), the Culling of Stratholme (map 595), and the Stratholme
             subzone of the Eastern Plaguelands (2279: the city's gates; the zone is 0.6 already, only the look
             changes there)
        0.5  Searing Gorge (51), Blackrock Mountain (25)
      Stratholme's smoke takes the stock fog colour itself (the burning city's glow), a little greyer and no darker:
      smoke lit by the fires in front of the fire-lit sky. A darker soot grey only dimmed the already dark streets ("it's
      fucking DARK in here lol", the user, 2026-10-13).
Steam hot springs: their water (LiquidType 1, 5, 9 -- the map scan found Slow Water, 5, in all four) is the HotSpring
      material, 70 degC, so the steam rule's hot-spring term is full there (env/Fog reads the liquid's own temperature;
      the temperature field only warms ground by a share of a liquid's heat). Steam is wisps, not a sheet: thin
      (0.035/yd over 3 yd) and broken up (patchiness 0.95), so the water's reflections show between them, and whiter
      than the sky's fog colour (its brightness, little of its hue, a bit brighter). The first look (0.1/yd over 4 yd,
      the stock colour) hid Steam Springs' lake under a flat purple-grey sheet at dusk ("i would think steam would be a
      bit whiter", the user, 2026-10-13):
        The Geyser Fields (4035) and Steam Springs (4117), Borean Tundra; Frostfire Hot Springs (2246), Winterspring;
        Golakka Hot Springs (543), Un'Goro Crater (only the whiter colour: warm air, the built-in thin steam).
      The Geyser Fields have hardly any terrain water (one chunk: the geysers are models), so the place gets a steam
      haze too: area fog at Fogginess 0.5, a near-white, low (6 yd), very patchy layer that comes and goes faster.

IDs: AreaClimate 140..146, FogSettings 110..115 (smog) and 120..124 (steam), GroundMaterial 201 (HotSpring),
GroundMaterialSelector 200..211. Zone, area and map IDs: AreaTable.dbc and Map.dbc (3.3.5a).

Usage:  python add_smog_and_steam.py [dir]     (default: ../data; for the client: <client>/DBFilesClient)
Sets the rows to exactly these values, so running it twice changes nothing. Then press "Reload AreaClimate" (climate
panel), "Reload FogSettings" (fog panel) and "Reload table" (surface cover panel); the hot springs' steam needs the
module that reads a liquid's own temperature.
"""
import sys
from pathlib import Path

import cdbc_io as io

I, T = -1.0, -1000.0
GLOBAL, MAP, AREA = 0, 1, 2
ALL, MIST, WATER_FOG, STEAM, AREA_FOG, HAZE = 0, 1, 2, 3, 4, 5
STOCK, HUE, REPLACE = 0, 1, 2

HOT_SPRING = 201
HOT_SPRING_TEMPERATURE = 70.0
WATER_LIQUIDS = (1, 5, 9)  # LiquidType.dbc: Water, Slow Water, Fast Water

# (name, scope, scope ID, fogginess, AreaClimate ID, FogSettings ID, look)
BROWN = dict(color_mode=HUE, color=(0.45, 0.37, 0.31), saturation=0.6, brightness=0.75)
EMBER = dict(color_mode=STOCK, saturation=0.7, brightness=1.0)
SMOKY = [
    ("Burning Steppes", AREA, 46, 0.6, 140, 110, BROWN),
    ("Searing Gorge", AREA, 51, 0.5, 141, 111, BROWN),
    ("Blackrock Mountain", AREA, 25, 0.5, 142, 112, BROWN),
    ("Stratholme", MAP, 329, 0.6, 143, 113, EMBER),
    ("The Culling of Stratholme", MAP, 595, 0.6, 144, 114, EMBER),
    ("Stratholme (Eastern Plaguelands)", AREA, 2279, 0.6, 145, 115, EMBER),
]
SMOG = dict(density=0.025, thickness=14.0, wind_thin=1.0, wind_drift=0.8, patchiness=0.8, weight=0.0)

# (name, area, steam look: cold air, selector IDs from)
SPRINGS = [
    ("The Geyser Fields", 4035, True, 200),
    ("Steam Springs", 4117, True, 203),
    ("Frostfire Hot Springs", 2246, True, 206),
    ("Golakka Hot Springs", 543, False, 209),
]
COLD_STEAM_IDS = {4035: 121, 4117: 122, 2246: 123}
WARM_STEAM_ID = 124
STEAM_LOOK = dict(color_mode=HUE, color=(0.95, 0.97, 1.0), saturation=0.25, brightness=1.3)
GEYSER_FIELDS, GEYSER_CLIMATE, GEYSER_HAZE = 4035, 146, 120


def fog_row(id, scope, scope_id, kind, comment, strength=I, density=I, thickness=I, build_up=I, burn_off=I,
            wind_thin=I, wind_drift=I, patchiness=I, weight=I, color_mode=-1, color=(I, I, I), saturation=I,
            brightness=I):
    return (id, scope, scope_id, kind, strength, density, thickness, build_up, burn_off, wind_thin, wind_drift,
            patchiness, weight, color_mode, color[0], color[1], color[2], saturation, brightness, 0, comment)


def climate_row(id, scope, scope_id, fogginess, comment):
    # ID, ScopeType, ScopeID, DayTemp, NightTemp, SeasonAmplitude, SeasonOffset, Humidity, Flags, Fogginess, Comment
    return (id, scope, scope_id, T, T, I, I, I, 0, fogginess, comment)


def decode(path, columns):
    _, raw, string_at = io.read(path.read_bytes())
    rows = []
    for r in raw:
        row = []
        for kind, value in zip(columns, r):
            row.append(io.as_float(value) if kind == "f" else io.as_int(value) if kind == "i" else string_at(value) if kind == "s" else value)
        rows.append(tuple(row))
    if rows and len(rows[0]) != len(columns):
        sys.exit(f"{path}: {len(rows[0])} fields, expected {len(columns)} (an old table: run upgrade_tables.py first)")
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


def climate(rows, row):
    """A place's Fogginess: on its own row if it has one already (other fields kept), else the new row."""
    existing = next((r for r in rows if r[1] == row[1] and r[2] == row[2] and r[0] != row[0]), None)
    if existing:
        return upsert(rows, existing[:9] + (row[9],) + existing[10:], io.AREA_CLIMATE)
    return upsert(rows, row, io.AREA_CLIMATE)


def main() -> None:
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "data"
    names = {"AreaClimate": io.AREA_CLIMATE, "FogSettings": io.FOG_SETTINGS, "GroundMaterial": io.GROUND_MATERIAL,
             "GroundMaterialSelector": io.GROUND_MATERIAL_SELECTOR}
    tables = {name: decode(folder / f"{name}.cdbc", columns) for name, columns in names.items()}
    changed = 0

    for name, scope, scope_id, fogginess, climate_id, fog_id, look in SMOKY:
        changed += climate(tables["AreaClimate"], climate_row(climate_id, scope, scope_id, fogginess, f"{name}: smoky"))
        changed += upsert(tables["FogSettings"], fog_row(fog_id, scope, scope_id, AREA_FOG, f"{name}: smog",
                                                         **SMOG, **look), io.FOG_SETTINGS)

    # Hot springs: the water is a HotSpring (70 degC) there.
    changed += upsert(tables["GroundMaterial"], (HOT_SPRING, "HotSpring", 0, 1.0, I, 0, I, HOT_SPRING_TEMPERATURE), io.GROUND_MATERIAL)
    for name, area, cold, first_id in SPRINGS:
        for n, liquid in enumerate(WATER_LIQUIDS):
            # ID, ScopeType, ScopeID, TexturePath, GroundEffectID, TerrainType, LiquidType, MaterialID, Flags
            changed += upsert(tables["GroundMaterialSelector"], (first_id + n, AREA, area, "", 0, -1, liquid, HOT_SPRING, 0),
                              io.GROUND_MATERIAL_SELECTOR)
        if cold:
            changed += upsert(tables["FogSettings"], fog_row(COLD_STEAM_IDS[area], AREA, area, STEAM, f"{name}: steam in cold air",
                                                             density=0.035, thickness=3.0, patchiness=0.95, **STEAM_LOOK), io.FOG_SETTINGS)
        else:
            changed += upsert(tables["FogSettings"], fog_row(WARM_STEAM_ID, AREA, area, STEAM, f"{name}: steam, whiter",
                                                             **STEAM_LOOK), io.FOG_SETTINGS)
    # The Geyser Fields' steam haze.
    changed += climate(tables["AreaClimate"], climate_row(GEYSER_CLIMATE, AREA, GEYSER_FIELDS, 0.5, "The Geyser Fields: steam haze"))
    changed += upsert(tables["FogSettings"], fog_row(GEYSER_HAZE, AREA, GEYSER_FIELDS, AREA_FOG, "The Geyser Fields: steam haze",
                                                     density=0.035, thickness=6.0, build_up=60.0, burn_off=180.0, wind_thin=0.9,
                                                     wind_drift=0.9, patchiness=0.9, weight=0.0, color_mode=HUE,
                                                     color=(0.95, 0.97, 1.0), saturation=0.3, brightness=1.15), io.FOG_SETTINGS)

    for name, columns in names.items():
        rows = sorted(tables[name], key=lambda r: r[0])
        (folder / f"{name}.cdbc").write_bytes(io.build(columns, rows))
    print(f"{folder}: {changed} row(s) set (" + ", ".join(f"{len(tables[n])} {n}" for n in names) + ")")


if __name__ == "__main__":
    main()
