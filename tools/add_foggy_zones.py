#!/usr/bin/env python3
"""Foggy zones: sets Fogginess (0 .. 1: how foggy a place is by nature; drives the ground fog's area fog and boosts
its mist) on AreaClimate rows for the naturally misty zones, keeping every other row and field as it is.
The shipped table leaves Fogginess unset everywhere, so no zone had any area fog ("add the starter set pls", the user,
2026-10-13). The scale follows gen_default_climate.py's examples (Swamp of Sorrows 0.6, Duskwood 0.5).
  0.6   the Plaguelands, Duskwood, Dustwallow Marsh, the Swamp of Sorrows, Felwood, Howling Fjord, Zangarmarsh
  0.4   the Ghostlands (blighted)
  0.3   Tirisfal Glades, Silverpine Forest, Zul'Drak, Deadwind Pass
  0.15  MISTY: ordinary forests and jungles -- Elwynn, Ashenvale, Feralas, Teldrassil, Darkshore, Azshara, Moonglade,
        Un'Goro Crater, the Hinterlands, Terokkar, Crystalsong, Stranglethorn Vale, Grizzly Hills, Sholazar Basin,
        Eversong Woods, Redridge Mountains, and the snowy Winterspring and Dun Morogh. "Normal non-haunted forest zones should feel misty rather than foggy" (the
        user, 2026-10-17): little area fog (0.09 by day, 0.15 at night), so the ground mist, which comes with damp
        mornings, evenings and rain, carries the look. Haunted, blighted, marsh, smoke and sea-fog zones keep theirs.
A zone with a row already keeps its other fields, and its Fogginess too where the row sets one: upstream's tables
(Morfium-G's, 2026-10-13) tune many of these zones themselves, and theirs win -- except for the zones in OVERRIDE,
where the user asked for something else (Duskwood and Burning Steppes at upstream's 1.0 and 0.8 "felt fine"). The rest
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
STRONG, FOREST, LIGHT, MISTY = 0.6, 0.4, 0.3, 0.15

# (zone, AreaTable ID, fogginess, row ID for a new row)
ZONES = [
    ("Eastern Plaguelands", 139, STRONG, 310),
    ("Western Plaguelands", 28, FOREST, 311),  # "western plaguelands possibly also too foggy" (the user, 2026-10-18): upstream 0.7
    ("Duskwood", 10, STRONG, 312),
    ("Dustwallow Marsh", 15, STRONG, 313),
    ("Swamp of Sorrows", 8, STRONG, 314),
    ("Felwood", 361, STRONG, 315),
    ("Howling Fjord", 495, STRONG, 317),
    ("Tirisfal Glades", 85, LIGHT, 318),
    ("Silverpine Forest", 130, LIGHT, 319),
    ("Zul'Drak", 66, LIGHT, 320),
    ("Deadwind Pass", 41, LIGHT, 321),
    ("Ghostlands", 3433, FOREST, 334),
    ("Zangarmarsh", 3521, STRONG, 335),
    # Ordinary forests and jungles: misty, not foggy (the user, 2026-10-17, after Ashenvale at dusk: 0.4 was "a bit
    # thiq", 0.25 "much better"). On 2026-10-13 they were 0.4 ("can we also have forest zones added ... Feralas,
    # Teldrassil, Darkshore, Ashenvale etc."), Grizzly Hills 0.6, Sholazar 0.3, Elwynn and Eversong 0.2.
    ("Elwynn Forest", 12, MISTY, 336),
    ("Ashenvale", 331, MISTY, 326),
    ("Feralas", 357, MISTY, 323),
    ("Teldrassil", 141, MISTY, 324),
    ("Darkshore", 148, MISTY, 325),
    ("Azshara", 16, MISTY, 327),
    ("Moonglade", 493, MISTY, 328),
    ("Un'Goro Crater", 490, MISTY, 330),
    ("The Hinterlands", 47, MISTY, 331),
    ("Terokkar Forest", 3519, MISTY, 332),
    ("Crystalsong Forest", 2817, MISTY, 333),
    ("Stranglethorn Vale", 33, MISTY, 329),
    ("Grizzly Hills", 394, MISTY, 316),
    ("Sholazar Basin", 3711, MISTY, 322),
    ("Eversong Woods", 3430, MISTY, 337),
    ("Redridge Mountains", 44, MISTY, 338),
    # Snowy, but misty too (the user, 2026-10-17); upstream had Winterspring 0.8, Dun Morogh 0.7.
    ("Winterspring", 618, MISTY, 339),
    ("Dun Morogh", 1, MISTY, 340),
]
# Zones whose value here wins even over a row of upstream's own: every misty forest (upstream has Elwynn 0.5, Redridge
# 0.45, Stranglethorn 0.3), and Zangarmarsh, where upstream's row 109 has 1.0, the top of the scale ("very pretty but
# possibly too foggy lol", the user, 2026-10-14, flying over Umbrafen).
OVERRIDE = {3521, 28} | {area for _, area, fog, _ in ZONES if fog == MISTY}
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
            # unset: ours fills it; a value of its own (upstream's tuning) stays, unless overridden
            if (existing[FOGGINESS] < 0.0 or area in OVERRIDE) and abs(existing[FOGGINESS] - fog) > 1e-6:  # (as stored: float32)
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
