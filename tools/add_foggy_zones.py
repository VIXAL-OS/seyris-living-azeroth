#!/usr/bin/env python3
"""Foggy zones: sets Fogginess (0 .. 1: how foggy a place is by nature; drives the ground fog's area fog and boosts
its mist) on AreaClimate rows for the naturally misty zones, keeping every other row and field as it is.
The shipped table leaves Fogginess unset everywhere, so no zone had any area fog ("add the starter set pls", the user,
2026-10-13). The scale follows gen_default_climate.py's examples (Swamp of Sorrows 0.6, Duskwood 0.5).
  0.6  the Plaguelands, Duskwood, Dustwallow Marsh, the Swamp of Sorrows, Felwood, Grizzly Hills, Howling Fjord,
       Zangarmarsh
  0.4  forests: Feralas, Teldrassil, Darkshore, Ashenvale, Azshara, Moonglade, Stranglethorn Vale, Un'Goro Crater,
       the Hinterlands, Terokkar Forest, Crystalsong Forest, the Ghostlands
  0.3  Tirisfal Glades, Silverpine Forest, Zul'Drak, Deadwind Pass, Sholazar Basin
  0.2  Elwynn Forest, Eversong Woods
A zone with a row already keeps its other fields, and its Fogginess too where the row sets one: upstream's tables
(Morfium-G's, "the owner's tested tables", 2026-10-13) tune many of these zones themselves, and theirs win. The rest
get new rows (300..) that set Fogginess only, inheriting the rest (temperatures -1000, the others -1); 300 and up stays
clear of upstream's IDs. Zone IDs: AreaTable.dbc (3.3.5a).
Usage:  python add_foggy_zones.py [dir]     (default: ../data; for the client: <client>/DBFilesClient)
Sets the rows to exactly these values, so running it twice changes nothing. Then press "Reload AreaClimate" in the
climate panel.
"""
import sys
from pathlib import Path

import cdbc_io as io

AREA = 2
I, T = -1.0, -1000.0
STRONG, FOREST, LIGHT, GENTLE = 0.6, 0.4, 0.3, 0.2

# (zone, AreaTable ID, fogginess, row ID for a new row)
ZONES = [
    ("Eastern Plaguelands", 139, STRONG, 310),
    ("Western Plaguelands", 28, STRONG, 311),
    ("Duskwood", 10, STRONG, 312),
    ("Dustwallow Marsh", 15, STRONG, 313),
    ("Swamp of Sorrows", 8, STRONG, 314),
    ("Felwood", 361, STRONG, 315),
    ("Grizzly Hills", 394, STRONG, 316),
    ("Howling Fjord", 495, STRONG, 317),
    ("Tirisfal Glades", 85, LIGHT, 318),
    ("Silverpine Forest", 130, LIGHT, 319),
    ("Zul'Drak", 66, LIGHT, 320),
    ("Deadwind Pass", 41, LIGHT, 321),
    ("Sholazar Basin", 3711, LIGHT, 322),
    # Forests ("can we also have forest zones added ... Feralas, Teldrassil, Darkshore, Ashenvale etc.", the user,
    # 2026-10-13): mist under the trees, between the misty zones and the light ones. A marsh with the strong ones, and
    # two gentle woods (morning mist, not gloom).
    ("Feralas", 357, FOREST, 323),
    ("Teldrassil", 141, FOREST, 324),
    ("Darkshore", 148, FOREST, 325),
    ("Ashenvale", 331, FOREST, 326),
    ("Azshara", 16, FOREST, 327),
    ("Moonglade", 493, FOREST, 328),
    ("Stranglethorn Vale", 33, FOREST, 329),
    ("Un'Goro Crater", 490, FOREST, 330),
    ("The Hinterlands", 47, FOREST, 331),
    ("Terokkar Forest", 3519, FOREST, 332),
    ("Crystalsong Forest", 2817, FOREST, 333),
    ("Ghostlands", 3433, FOREST, 334),
    ("Zangarmarsh", 3521, STRONG, 335),
    ("Elwynn Forest", 12, GENTLE, 336),
    ("Eversong Woods", 3430, GENTLE, 337),
]
FOGGINESS = 9  # column (ID, ScopeType, ScopeID, DayTemp, NightTemp, SeasonAmplitude, SeasonOffset, Humidity, Flags, Fogginess, Comment)


def main():
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "data"
    path = folder / "AreaClimate.cdbc"
    _, raw, string_at = io.read(path.read_bytes())
    kinds = io.AREA_CLIMATE
    rows = []
    for r in raw:
        row = []
        for kind, value in zip(kinds, r):
            row.append(io.as_float(value) if kind == "f" else io.as_int(value) if kind == "i" else string_at(value) if kind == "s" else value)
        rows.append(row)
    if rows and len(rows[0]) != len(kinds):
        sys.exit(f"{path}: {len(rows[0])} fields, expected {len(kinds)} (an old table: run upgrade_tables.py first)")
    changed = 0
    for name, area, fog, new_id in ZONES:
        existing = next((r for r in rows if r[1] == AREA and r[2] == area), None)
        if existing:
            if existing[FOGGINESS] < 0.0:  # unset: ours fills it; a value of its own (upstream's tuning) stays
                existing[FOGGINESS] = fog
                changed += 1
            continue
        if any(r[0] == new_id for r in rows):
            sys.exit(f"{path}: row ID {new_id} is taken by another row (for {name})")
        rows.append([new_id, AREA, area, T, T, I, I, I, 0, fog, f"{name}: foggy by nature", T, T])
        changed += 1
    rows.sort(key=lambda r: r[0])
    path.write_bytes(io.build(kinds, [tuple(r) for r in rows]))
    print(f"{path}: {changed} row(s) set ({len(rows)} rows)")


if __name__ == "__main__":
    main()
