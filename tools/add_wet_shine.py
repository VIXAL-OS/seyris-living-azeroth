#!/usr/bin/env python3
"""Wet surfaces' shine on the ground materials (ChromieCraft fork, 2026-10-25; features/WetSurfaces).

GroundMaterial gets its ninth column, WetShine (0 = never looks wet .. 1 = stone; -1 = not set: the layer's TerrainType
decides, env/WetSurfaceModel TerrainTypeShine: grass and leaves 0, dirt 0.8, stone 1). Rows that set it:

  Snow (1) 0 and ClearIce (30) 0: their own looks (the code leaves them out anyway; the rows say so for authors).
  Paved (80) 1: cobbles and flagstones shine most when wet.
  Mud (70) 0.9, SwampSorrowsMuckDark01 (13) 0.9, Soggy Ground (21) 0.9: wet ground already, a little more.
  Sand (51) 0.35, Ash (50) 0.4: loose ground drinks the rain; only a little sheen.
  WestfallSandBaseWet (11) 0.6: wet-painted sand.
  SilithisRockHighlight2 (10) 0.8: rock among the sand.
  BurningSteppsLavaTest02 (14) 0: lava (hot ground is left out by the code too).

Every other row keeps -1. The selectors are untouched: no new material lies anywhere it didn't.

Usage:  python add_wet_shine.py [dir]     (default: ../data; run it on <client>/DBFilesClient too)
Running it twice changes nothing. The changed file is backed up first (<name>.cdbc.<timestamp>.bak).
"""

import sys
from pathlib import Path

import cdbc_io as io

WET_SHINE = 8   # the column (io.GROUND_MATERIAL)
SHINE = {
    1: ("Snow", 0.0),
    30: ("ClearIce", 0.0),
    80: ("Paved", 1.0),
    70: ("Mud", 0.9),
    13: ("SwampSorrowsMuckDark01", 0.9),
    21: ("Soggy Ground", 0.9),
    51: ("Sand", 0.35),
    50: ("Ash", 0.4),
    11: ("WestfallSandBaseWet", 0.6),
    10: ("SilithisRockHighlight2", 0.8),
    14: ("BurningSteppsLavaTest02", 0.0),
}


def decode(path, columns):
    """Rows padded to the current layout (older files lack the newest columns: their "not set" values)."""
    _, raw, string_at = io.read(path.read_bytes())
    missing = io.TABLES["GroundMaterial"][1]
    rows = []
    for r in raw:
        row = []
        for kind, value in zip(columns, r):
            row.append(io.as_float(value) if kind == "f" else io.as_int(value) if kind == "i" else string_at(value) if kind == "s" else value)
        row += missing[len(row):]
        rows.append(row)
    return rows


def main() -> None:
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent.parent / "data"
    path = folder / "GroundMaterial.cdbc"
    data = path.read_bytes()
    fields, _, _ = io.read(data)
    materials = decode(path, io.GROUND_MATERIAL)
    changed = 0 if fields == len(io.GROUND_MATERIAL) else 1
    for row in materials:
        if row[0] not in SHINE:
            continue
        name, shine = SHINE[row[0]]
        if row[1] != name:
            print(f"  row {row[0]} is \"{row[1]}\", not \"{name}\": left alone")
            continue
        if abs(row[WET_SHINE] - shine) > 1e-6:
            row[WET_SHINE] = shine
            changed += 1
    if not changed:
        print(f"{folder}: up to date ({len(materials)} materials)")
        return
    backup = io.backup_path(path)
    backup.write_bytes(data)
    path.write_bytes(io.build(io.GROUND_MATERIAL, [tuple(r) for r in materials]))
    print(f"{folder}: {changed} change(s) ({len(materials)} materials, {fields} -> {len(io.GROUND_MATERIAL)} columns); backup {backup.name}")


if __name__ == "__main__":
    main()
