#!/usr/bin/env python3
"""Mountain cloud: cloud on the upper slopes and peaks of mountain zones ("cloudy air at the tops of the mountains in
storm peaks? like not in any high altitude but just mountainous zones", the user, 2026-10-13). Sets the rows below in
existing AreaClimate.cdbc and FogSettings.cdbc files and keeps every other row as it is.

How: AreaClimate's CloudBase and CloudTop (world z) put a cloud deck in the air between them over the whole view
(features/GroundFog: its own pass), so the zone's upper slopes, peaks and the buildings on them are in cloud, from the
ground and in flight; its Fogginess is how thick the cloud gets. The place then has no ground fog of its own (env/Fog):
on the ground it only reached ~128 yd and came in in blotches ("fog has kinda janky loading-in effects at height", the
user, 2026-10-13), and never covered buildings (the Temple of Storms, Icecrown Citadel). A FogSettings area fog row
gives the deck its colour: near-white at the sky's brightness.

Bases: about the height of the zone's highest 15% of terrain (85th percentile of its chunks' MCVT heights, 3.3.5a
ADTs; the 90th where a town sits high: Dun Morogh, Ironforge's gate at ~500; Winterspring); every town checked stays
below (Everlook 722, Brunnhildar 822, Dun Niffelem 758, K3 403, Sun Rock 105, Sylvanaar 173). Tops: 50 yd above the
zone's highest terrain, so the summits are in cloud too ("the tops of the mountains in storm peaks"; the Temple of
Storms at 1888, Stormcrest 1207):
    Storm Peaks 1040..1950    Icecrown 655..1200    Alterac Mountains 177..368    Dun Morogh 560..946
    Winterspring 963..1472    Stonetalon Mountains 310..634    Blade's Edge Mountains 342..609
Fogginess 0.8 (0.7 in Dun Morogh, Stonetalon and Blade's Edge), set only where the place's row doesn't set one
(upstream tunes many zones' Fogginess; theirs wins). CloudBase is always this tool's.

IDs: AreaClimate 350..356 (new rows; a place with a row already keeps it), FogSettings 330..336.
Usage:  python add_mountain_clouds.py [dir]     (default: ../data; for the client: <client>/DBFilesClient)
Sets the rows to exactly these values, so running it twice changes nothing. Then press "Reload AreaClimate" (climate
panel) and "Reload FogSettings" (fog panel).
"""
import sys
from pathlib import Path

import cdbc_io as io

I, T = -1.0, -1000.0
AREA = 2
AREA_FOG = 4
HUE = 1
FOGGINESS, CLOUD_BASE, CLOUD_TOP = 9, 11, 12  # AreaClimate columns

# (zone, AreaTable ID, cloud base z, cloud top z, fogginess, AreaClimate ID for a new row, FogSettings ID)
ZONES = [
    ("Storm Peaks", 67, 1040.0, 1950.0, 0.8, 350, 330),
    ("Icecrown", 210, 655.0, 1200.0, 0.8, 351, 331),
    ("Alterac Mountains", 36, 177.0, 368.0, 0.8, 352, 332),
    ("Dun Morogh", 1, 560.0, 946.0, 0.7, 353, 333),
    ("Winterspring", 618, 963.0, 1472.0, 0.8, 354, 334),
    ("Stonetalon Mountains", 406, 310.0, 634.0, 0.7, 355, 335),
    ("Blade's Edge Mountains", 3522, 342.0, 609.0, 0.7, 356, 336),
]
# ID, ScopeType, ScopeID, Kind, Strength, Density, Thickness, BuildUp, BurnOff, WindThin, WindDrift, Patchiness, Weight,
# ColorMode, ColorR, ColorG, ColorB, Saturation, Brightness, Flags, Comment
CLOUD_LOOK = dict(density=0.03, thickness=30.0, build_up=30.0, burn_off=300.0, wind_thin=2.0, wind_drift=0.9,
                  patchiness=0.85, weight=0.0, color=(0.95, 0.97, 1.0), saturation=0.2, brightness=1.4)


def decode(path, columns):
    _, raw, string_at = io.read(path.read_bytes())
    rows = []
    for r in raw:
        rows.append([io.as_float(v) if k == "f" else io.as_int(v) if k == "i" else string_at(v) if k == "s" else v
                     for k, v in zip(columns, r)])
    if rows and len(rows[0]) != len(columns):
        sys.exit(f"{path}: {len(rows[0])} fields, expected {len(columns)} (an old table: run upgrade_tables.py first)")
    return rows


def same(a, b):
    return all(abs(x - y) < 1e-5 if isinstance(x, float) and isinstance(y, float) else x == y for x, y in zip(a, b))


def main() -> None:
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "data"
    climate = decode(folder / "AreaClimate.cdbc", io.AREA_CLIMATE)
    fog = decode(folder / "FogSettings.cdbc", io.FOG_SETTINGS)
    changed = 0
    for name, area, base, top, fogginess, climate_id, fog_id in ZONES:
        row = next((r for r in climate if r[1] == AREA and r[2] == area), None)
        if row is None:
            if any(r[0] == climate_id for r in climate):
                sys.exit(f"AreaClimate row ID {climate_id} is taken by another row (for {name})")
            row = [climate_id, AREA, area, T, T, I, I, I, 0, I, f"{name}: mountain cloud", T, T]
            climate.append(row)
            changed += 1
        before = list(row)
        if row[FOGGINESS] < 0.0:
            row[FOGGINESS] = fogginess
        row[CLOUD_BASE] = base
        row[CLOUD_TOP] = top
        changed += 0 if same(before, row) else 1
        c = CLOUD_LOOK
        look = [fog_id, AREA, area, AREA_FOG, I, c["density"], c["thickness"], c["build_up"], c["burn_off"], c["wind_thin"],
                c["wind_drift"], c["patchiness"], c["weight"], HUE, *c["color"], c["saturation"], c["brightness"], 0,
                f"{name}: mountain cloud"]
        old = next((r for r in fog if r[0] == fog_id), None)
        if old is None:
            fog.append(look); changed += 1
        elif not same(old, look):
            fog[fog.index(old)] = look; changed += 1
    (folder / "AreaClimate.cdbc").write_bytes(io.build(io.AREA_CLIMATE, [tuple(r) for r in sorted(climate, key=lambda r: r[0])]))
    (folder / "FogSettings.cdbc").write_bytes(io.build(io.FOG_SETTINGS, [tuple(r) for r in sorted(fog, key=lambda r: r[0])]))
    print(f"{folder}: {changed} row(s) set ({len(climate)} AreaClimate, {len(fog)} FogSettings)")


if __name__ == "__main__":
    main()
