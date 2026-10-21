#!/usr/bin/env python3
"""Sea smoke: thinner water fog where cold air lies over a sea all night. Sets the rows below in an existing
FogSettings.cdbc and keeps every other row as it is ("inside of daggercap bay is WAY too smokey", the user, 2026-10-20,
swimming in the bay at night).

The water fog rule (env/Fog) is full once the water is 8 degC warmer than the air. The water's temperature is the
mean of the day's and the night's (env/Climate), so in Northrend (-2 by day, -12 at night) every night is 5 degC
short of that: about 0.5 on every bay, under Howling Fjord's area fog. Swimming puts the camera inside both layers,
and the bay looked like the inside of a smoke bank. Here the water fog is half as strong; add_foggy_zones.py takes
the zone's own fogginess down to 0.3 too. (Not Patchiness: the renderer reads only the module's own noise setting, not
the kinds'.)
        Howling Fjord (495; Daggercap Bay is one of its areas)

IDs: FogSettings 340.. (300 and up stays clear of upstream's IDs). Zone IDs: AreaTable.dbc (3.3.5a).

Usage:  python add_sea_smoke.py [dir]     (default: ../data; for the client: <client>/DBFilesClient)
Sets the rows to exactly these values, so running it twice changes nothing. Then press "Reload FogSettings" (fog
panel); "Fast-forward to the targets" (fog panel, Testing) shows the new amounts at once instead of over the kind's
burn-off.
"""
import sys
from pathlib import Path

import cdbc_io as io
from add_smog_and_steam import AREA, WATER_FOG, decode, fog_row, upsert

# (name, area, FogSettings ID)
SEAS = [
    ("Howling Fjord", 495, 340),
]
SEA_SMOKE = dict(strength=0.5)


def main() -> None:
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "data"
    path = folder / "FogSettings.cdbc"
    rows = decode(path, io.FOG_SETTINGS)
    changed = 0
    for name, area, fog_id in SEAS:
        changed += upsert(rows, fog_row(fog_id, AREA, area, WATER_FOG, f"{name}: sea smoke, thinner", **SEA_SMOKE),
                          io.FOG_SETTINGS)
    rows.sort(key=lambda r: r[0])
    path.write_bytes(io.build(io.FOG_SETTINGS, rows))
    print(f"{path}: {changed} row(s) set ({len(rows)} rows)")


if __name__ == "__main__":
    main()
