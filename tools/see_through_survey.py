#!/usr/bin/env python3
"""See-through windows: which glass windows have a real, modelled room behind them (docs/r&d/see-through-windows.md).

"Windows you can actually see through into a building's modelled interior" (the user and Simon, 2026-10-20). From
outside, the client draws a WMO's interior groups only where its portal walk reaches them, and windows aren't portals;
features/SeeThrough opens a window as a portal of its own when there is an interior group behind it. This counts, over
every window in GlassWindows.cdbc, which have one:

- A window of an exterior group (GlassWindows Flags 0x1) has a room when an interior group's box holds a point PROBES yd
  behind its middle (the first probe that finds one; the smallest such box; an interior group is one without MOGP 0x8 or
  0x10000, the flags the client's walk from outside skips). This is gen_glass.py's interior_room (ROOM_PROBE) with a
  second, deeper probe for thick walls, and exactly what env/SeeThroughModel's InteriorBehind does at run time.
- A window with none is "fake": the glass is painted over a solid shell (a facade, a tower with no inside).
- A window of an interior group is that room's own glass seen from inside (or an interior skylight): not a see-through
  candidate from outside.
- A glass batch of an exterior group can be drawn see-through (features/SeeThrough defers it) only when every one of its
  triangles lies in a window (within TRIM yd^2 of strays) and every window it touches has a room: a batch that also
  covers a fake window would show the void behind it. Counted per batch, and the windows those batches cover.

The buildings whose windows go see-through are data (SeeThroughBuildings.cdbc, 2026-10-22: "more buildings"), written
from SEE_THROUGH below by --table: the WMOs with the human window atlases GlassLead covers whose glass can go see-through
(every glass batch's strays trim, a room behind its windows) and that the gate opens from most street views with few flips
(seethrough_tests' survey, SEETHROUGH_SURVEY). Left out, with the survey's reason: the town halls (their rooms seen from 1
to 5% of street views, flips on walks), the barns, the smithies, the Westfall guard tower and Northrend's human house
(their rooms never seen through their windows), the closed variants (no rooms), Stormwind (one WMO for the city: 113 of its
397 windows have an interior group behind, mostly a few large volumes, seen from 2% of street views), the dungeons'.

Inputs: <scan> (waterfall-scan's out folder: wmo-windows/ roots and groups, as gen_glass.py) and the data folder with
GlassWindows.cdbc. Nothing here opens an MPQ.
Usage:  python see_through_survey.py <scan> [data dir] [--wmo <path>]   (--wmo: one WMO's windows, one per line)
        python see_through_survey.py --table [data dir]                 (writes SeeThroughBuildings.cdbc from SEE_THROUGH)
"""
import collections
import math
import sys
from pathlib import Path

import cdbc_io as io
import gen_glass as gg
import gen_window_lights as wl

PROBES = (1.0, 2.0)     # yd behind the window's middle
MARGIN = 0.25           # yd: an interior group's box grown by this (gen_glass's interior_room)
SKIPPED = 0x10008       # MOGP flags the client's walk from outside never enters (exterior 0x8, 0x10000)
TRIM = 0.25             # yd^2: a batch's glass outside every window, at most this, still see-through
LOOKUP = 0.05           # yd: a triangle's middle this far outside a window's rectangle is still in it
SLAB = 0.3              # yd: ... and within its Thickness (at least this) of its plane
EXTERIOR_WINDOW = 0x1   # GlassWindows.Flags

# SeeThroughBuildings.cdbc (features/SeeThrough): the buildings whose windows go see-through, and how (Flags: 0x1 from
# outside, the room behind the glass; 0x2 from inside, the street through it). From the survey of 2026-10-22 (the street
# sweep, high and close views, walks past every front, the inside sweep: docs/r&d/see-through-windows.md section 9).
FROM_OUTSIDE, FROM_INSIDE = 0x1, 0x2
SEE_THROUGH = [
    # the inns: 17 windows, 14 open from most street views, the dormers never; no flips; from inside 82% (Stratholme's 32%:
    # its 59 interior windows, 40 of them between its rooms)
    (r"world\wmo\azeroth\buildings\goldshireinn\goldshireinn.wmo", "the Lion's Pride Inn (Goldshire)"),
    (r"world\wmo\azeroth\buildings\duskwood_inn\duskwood_inn.wmo", "the Scarlet Raven Tavern (Darkshire)"),
    (r"world\wmo\azeroth\buildings\redridge_inn\redridge_inn.wmo", "the Lakeshire Inn"),
    (r"world\wmo\azeroth\buildings\westfall_inn\westfall_inn.wmo", "the inn in Westfall's style (Southshore and others)"),
    (r"world\wmo\buildings\oldstrat_inn.wmo", "the inn of old Stratholme"),
    # the two-storey houses: 8 windows, 97 to 99% of street views, one window changing 4 times over the walks
    (r"world\wmo\azeroth\buildings\humantwostory\humantwostory.wmo", "the human two-storey house"),
    (r"world\wmo\azeroth\buildings\duskwood_humantwostory\duskwood_humantwostory.wmo", "the two-storey house, Duskwood's"),
    (r"world\wmo\azeroth\buildings\redridge_humantwostory\redridge_humantwostory.wmo", "the two-storey house, Redridge's"),
    (r"world\wmo\azeroth\buildings\westfall_humantwostory\westfall_humantwostory.wmo", "the two-storey house, Westfall's"),
    (r"world\wmo\buildings\oldstrat_humantwostory.wmo", "the two-storey house of old Stratholme"),
    # the mage towers: 8 windows, every street view, no flips
    (r"world\wmo\azeroth\buildings\magetower\magetower.wmo", "the human mage tower"),
    (r"world\wmo\azeroth\buildings\magetower\cot_magetower01.wmo", "the mage tower, the Caverns of Time's"),
    (r"world\wmo\azeroth\buildings\duskwood_magetower\duskwood_magetower.wmo", "the mage tower, Duskwood's"),
    (r"world\wmo\azeroth\buildings\duskwood_magetower\duskwood_magetowerpurple.wmo", "the mage tower, Duskwood's purple"),
    (r"world\wmo\azeroth\buildings\duskwood_magetower\netherpvp_magetower.wmo", "the mage tower, Hellfire's battleground"),
    (r"world\wmo\azeroth\buildings\redridge_magetower\redridge_magetower.wmo", "the mage tower, Redridge's"),
    (r"world\wmo\azeroth\buildings\westfall_magetower\westfall_magetower.wmo", "the mage tower, Westfall's"),
    # the farmhouses: 3 windows, the two downstairs from most street views, the attic's hardly; a few changes on the walks
    (r"world\wmo\azeroth\buildings\human_farm\farm.wmo", "the human farmhouse"),
    (r"world\wmo\azeroth\buildings\duskwood_human_farm\duskwood_human_farm.wmo", "the farmhouse, Duskwood's"),
    (r"world\wmo\azeroth\buildings\duskwood_human_farm\humanfarm_ab.wmo", "the farmhouse, Arathi Basin's"),
    (r"world\wmo\azeroth\buildings\redridge_human_farm\redridge_human_farm.wmo", "the farmhouse, Redridge's"),
    (r"world\wmo\azeroth\buildings\westfall_human_farm\westfall_human_farm.wmo", "the farmhouse, Westfall's"),
    (r"world\wmo\buildings\oldstrat_farm.wmo", "the farmhouse of old Stratholme"),
    # Redridge's prison camp headquarters: 10 windows with a room, 65% of street views
    (r"world\wmo\azeroth\buildings\prison_camp\prisonhq_redridge.wmo", "the prison camp's headquarters (Redridge)"),
]
SEE_THROUGH_COLUMNS = ["u", "s", "u", "s"]   # ID Wmo Flags Comment


def see_through_table():
    """SeeThroughBuildings.cdbc's rows: every SEE_THROUGH building, from outside and from inside."""
    return [[i + 1, wmo, FROM_OUTSIDE | FROM_INSIDE, comment] for i, (wmo, comment) in enumerate(SEE_THROUGH)]


def frame(n):
    """The window's axes (env/GlassRooms' WindowFrame): along = unit(-n.y, n.x, 0), x for a skylight; up = n x along."""
    if n[0] * n[0] + n[1] * n[1] < 1e-4:
        along = (1.0, 0.0, 0.0)
    else:
        l = math.hypot(n[0], n[1])
        along = (-n[1] / l, n[0] / l, 0.0)
    return along, wl.cross(n, along)


def interior_behind(center, normal, groups):
    """The interior group behind a window (index), or None: InteriorBehind's twin."""
    for depth in PROBES:
        p = tuple(center[k] - normal[k] * depth for k in range(3))
        best, volume = None, None
        for gi, (flags, lo, hi, _name) in enumerate(groups):
            if flags & SKIPPED:
                continue
            if all(lo[k] - MARGIN <= p[k] <= hi[k] + MARGIN for k in range(3)):
                v = (hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2])
                if best is None or v < volume:
                    best, volume = gi, v
        if best is not None:
            return best
    return None


def read_windows(data):
    """{wmo path: [window dict]} from GlassWindows.cdbc."""
    _fields, rows, s = io.read((Path(data) / "GlassWindows.cdbc").read_bytes())
    f = io.as_float
    out = collections.defaultdict(list)
    for r in rows:
        out[s(r[1])].append({"group": r[2], "ordinal": r[3], "kind": r[4], "center": (f(r[5]), f(r[6]), f(r[7])),
                             "normal": (f(r[8]), f(r[9]), f(r[10])), "hw": f(r[11]), "hh": f(r[12]), "flags": r[13],
                             "thickness": f(r[16]) if len(r) > 16 else 0.0})
    return out


def window_of(point, windows):
    """The first window (by ordinal) whose lookup box holds a point, or None."""
    for w in windows:
        along, up = frame(w["normal"])
        d = wl.sub(point, w["center"])
        if (abs(wl.dot(d, along)) <= w["hw"] + LOOKUP and abs(wl.dot(d, up)) <= w["hh"] + LOOKUP and
                abs(wl.dot(d, w["normal"])) <= max(w["thickness"], SLAB) + LOOKUP):
            return w
    return None


def batches(scan, wmo, info, group, windows):
    """[(batch index, windows touched (ordinals), stray triangles, stray area)] of a group's glass batches."""
    gp = gg.flat(scan, "%s_%03d.wmo" % (wmo[:-4], group))
    if not gp.exists():
        return None
    g = wl.parse_group(gp.read_bytes())
    mats = gg.glass_materials(info)
    out = []
    for bi, (start, count, _vmin, _vmax, _flags, mat) in enumerate(g["batches"]):
        if mat not in mats:
            continue
        touched, strays, area = set(), 0, 0.0
        for t in range(start, start + count - 2, 3):
            p = [g["vertices"][g["indices"][t + k]] for k in range(3)]
            mid = tuple(sum(q[k] for q in p) / 3.0 for k in range(3))
            w = window_of(mid, windows)
            if w is None:
                strays += 1
                area += 0.5 * wl.length(wl.cross(wl.sub(p[1], p[0]), wl.sub(p[2], p[0])))
            else:
                touched.add(w["ordinal"])
        out.append((bi, touched, strays, area))
    return out


def survey(scan, data, only=None):
    windows = read_windows(data)
    totals = collections.Counter()
    kinds = collections.Counter()
    per_wmo = []
    for wmo in sorted(windows):
        if only and wmo != only:
            continue
        rp = gg.flat(scan, wmo)
        if not rp.exists():
            totals["WMOs without a root in the scan"] += 1
            continue
        root = rp.read_bytes()
        groups = gg.group_infos(root)
        info = gg.parse_root_all(root)
        real = fake = inside = 0
        room = {}
        for w in windows[wmo]:
            if not (w["flags"] & EXTERIOR_WINDOW):
                inside += 1
                continue
            g = interior_behind(w["center"], w["normal"], groups)
            room[w["ordinal"]] = g
            if g is None:
                fake += 1
            else:
                real += 1
            kinds[(gg.KIND_NAMES[w["kind"]], g is not None)] += 1
            if only:
                print("  group %3d window %4d %-7s -> %s" % (w["group"], w["ordinal"], gg.KIND_NAMES[w["kind"]],
                                                            "room %d %s" % (g, groups[g][3]) if g is not None else "none (fake)"))
        # the exterior groups' glass batches
        good = bad = covered = 0
        for group in sorted({w["group"] for w in windows[wmo] if w["flags"] & EXTERIOR_WINDOW}):
            gw = sorted((w for w in windows[wmo] if w["group"] == group), key=lambda w: w["ordinal"])
            found = batches(scan, wmo, info, group, gw)
            if found is None:
                totals["exterior groups without a file in the scan"] += 1
                continue
            for bi, touched, strays, area in found:
                ok = bool(touched) and area <= TRIM and all(room.get(o) is not None for o in touched)
                if ok:
                    good += 1
                    covered += len(touched)
                else:
                    bad += 1
                if only:
                    print("  group %3d batch %3d: windows %s, %d stray triangles (%.2f yd2): %s" %
                          (group, bi, sorted(touched), strays, area, "see-through" if ok else "kept opaque"))
        totals["exterior windows"] += real + fake
        totals["with a room behind"] += real
        totals["fake"] += fake
        totals["interior groups' windows"] += inside
        totals["exterior glass batches"] += good + bad
        totals["batches that can go see-through"] += good
        totals["windows those batches cover"] += covered
        totals["WMOs"] += 1
        if real:
            totals["WMOs with a room behind a window"] += 1
        if real and not fake:
            totals["WMOs whose every exterior window has one"] += 1
        per_wmo.append((real, fake, inside, good, bad, wmo))
    return totals, kinds, per_wmo


def main():
    args = [a for a in sys.argv[1:]]
    if args and args[0] == "--table":
        data = Path(args[1]) if len(args) > 1 else Path(__file__).resolve().parent.parent / "data"
        rows = see_through_table()
        print(f"{len(rows)} see-through buildings")
        gg.write(data / "SeeThroughBuildings.cdbc", io.build(SEE_THROUGH_COLUMNS, [tuple(r) for r in rows]))
        return
    only = None
    if "--wmo" in args:
        k = args.index("--wmo")
        only = args[k + 1].lower().replace("/", "\\")
        del args[k:k + 2]
    if not args:
        raise SystemExit(__doc__)
    scan = Path(args[0])
    data = Path(args[1]) if len(args) > 1 else Path(__file__).resolve().parent.parent / "data"
    totals, kinds, per_wmo = survey(scan, data, only)
    for k in ("WMOs", "exterior windows", "with a room behind", "fake", "interior groups' windows",
              "WMOs with a room behind a window", "WMOs whose every exterior window has one", "exterior glass batches",
              "batches that can go see-through", "windows those batches cover",
              "WMOs without a root in the scan", "exterior groups without a file in the scan"):
        print("%-46s %d" % (k, totals[k]))
    print("by kind (exterior windows): " + ", ".join("%s %d real / %d fake" % (n, kinds[(n, True)], kinds[(n, False)])
                                                      for n in ("clear", "leaded", "frosted", "stained")))
    if not only:
        print("most windows with a room behind (real, fake, interior groups' own, batches see-through / kept):")
        for real, fake, inside, good, bad, wmo in sorted(per_wmo, reverse=True)[:25]:
            print("  %5d %5d %5d  %3d/%-3d %s" % (real, fake, inside, good, bad, wmo))
        print("most fake windows:")
        for real, fake, inside, good, bad, wmo in sorted(per_wmo, key=lambda r: -r[1])[:10]:
            print("  %5d %5d %5d  %3d/%-3d %s" % (real, fake, inside, good, bad, wmo))


if __name__ == "__main__":
    main()
