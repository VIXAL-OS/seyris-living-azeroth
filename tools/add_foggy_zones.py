#!/usr/bin/env python3
"""Foggy zones: sets Fogginess (0 .. 1: how foggy a place is by nature; drives the ground fog's area fog and boosts
its mist) on AreaClimate rows for the naturally misty zones, keeping every other row and field as it is.
The shipped table leaves Fogginess unset everywhere, so no zone had any area fog ("add the starter set pls", the user,
2026-10-13). The scale follows gen_default_climate.py's examples (Swamp of Sorrows 0.6, Duskwood 0.5).
  0.6  the Plaguelands, Duskwood, Dustwallow Marsh, the Swamp of Sorrows, Felwood, Grizzly Hills, Howling Fjord
  0.3  Tirisfal Glades, Silverpine Forest, Zul'Drak, Deadwind Pass, Sholazar Basin
A zone with a row already (the Swamp of Sorrows: 106) keeps its other fields; the rest get new rows (110..) that set
Fogginess only, inheriting the rest (temperatures -1000, the others -1). Zone IDs: AreaTable.dbc (3.3.5a).
Usage:  python add_foggy_zones.py [dir]     (default: ../data; for the client: <client>/DBFilesClient)
Sets the rows to exactly these values, so running it twice changes nothing. Then press "Reload AreaClimate" in the
climate panel.
"""
import sys
from pathlib import Path

import cdbc_io as io

AREA = 2
I, T = -1.0, -1000.0
STRONG, LIGHT = 0.6, 0.3

# (zone, AreaTable ID, fogginess, row ID for a new row)
ZONES = [
    ("Eastern Plaguelands", 139, STRONG, 110),
    ("Western Plaguelands", 28, STRONG, 111),
    ("Duskwood", 10, STRONG, 112),
    ("Dustwallow Marsh", 15, STRONG, 113),
    ("Swamp of Sorrows", 8, STRONG, 114),
    ("Felwood", 361, STRONG, 115),
    ("Grizzly Hills", 394, STRONG, 116),
    ("Howling Fjord", 495, STRONG, 117),
    ("Tirisfal Glades", 85, LIGHT, 118),
    ("Silverpine Forest", 130, LIGHT, 119),
    ("Zul'Drak", 66, LIGHT, 120),
    ("Deadwind Pass", 41, LIGHT, 121),
    ("Sholazar Basin", 3711, LIGHT, 122),
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
            if abs(existing[FOGGINESS] - fog) > 1e-6:
                existing[FOGGINESS] = fog
                changed += 1
            continue
        if any(r[0] == new_id for r in rows):
            sys.exit(f"{path}: row ID {new_id} is taken by another row (for {name})")
        rows.append([new_id, AREA, area, T, T, I, I, I, 0, fog, f"{name}: foggy by nature"])
        changed += 1
    rows.sort(key=lambda r: r[0])
    path.write_bytes(io.build(kinds, [tuple(r) for r in rows]))
    print(f"{path}: {changed} row(s) set ({len(rows)} rows)")


if __name__ == "__main__":
    main()
