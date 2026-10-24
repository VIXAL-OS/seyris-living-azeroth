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

Stained glass (2026-10-22, "Phase A"): the chapels and cathedrals with modelled rooms behind their stained glass, not a
dungeon's (gen_glass.is_dungeon), and Northrend's arched human windows (nr_hu_window), surveyed the same way once the gate
passed the inside of a thick wall round a window's opening (env/SeeThroughModel kReveal): the Redridge chapel (96% of
street views), Northshire Abbey and New Hearthglen's (58%), Northrend's barracks (61%), its town hall (66%), the Isle of
Conquest's keep (55%), Wintergarde's small house and the inn (Valgarde's: its ground-floor window), none flickering on the
walks. Left out: the Scarlet cathedral (Tyr's Hand's, New Hearthglen's: 16 of its 60 windows have a room, seen from 1% of
views), the Scarlet Monastery's outside (14%), Northrend's other human houses (1 and 8%), Silvermoon's houses (19 to 36%, a
window changing more than twice on a walk), the night elves' houses and Sporeggar's huts (not churches; 67% and 0%).

Frosted glass (2026-10-22, "Phase B"): the WMOs with frosted glass over windows with rooms (40), surveyed the same way:
the dwarven houses (55% of street views, from inside 97%), the Taunka's longhouse (49%, 87%), the orcs' medium huts (100%,
57%), great halls (53%, outside) and two-storey and zeppelin houses (inside only: 77, 78%), and the vrykul longhouses
(their glass clear and leaded; 47%, outside). Left out: the iron dwarves' buildings (64 and 35%, a window changing more
than twice on 4 of 42 walks), the winter orcs' (0 .. 12%), the tuskarr's huts and the draenei's towers (0%: their rooms
never seen), the dwarven inns, barracks, smithies and guard towers (no room behind their windows), Ironforge, Dalaran and
the Exodar (each a city's one WMO).

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
    # stained glass (2026-10-22): the chapel, 11 windows, 96% of street views, from inside 71%; the abbey, 46 of its 94
    # windows with a room (the rest over its nave's walls), 58%, from inside 33% (its 55 windows of its rooms, many between
    # them, as old Stratholme's inn's)
    (r"world\wmo\azeroth\buildings\chapel\redridgechapel.wmo", "the chapel (Lakeshire, Tyr's Hand, New Hearthglen)"),
    (r"world\wmo\azeroth\buildings\nsabbey\nsabbey.wmo", "Northshire Abbey (and Tyr's Hand's)"),
    (r"world\wmo\northrend\dragonblight\newhearthglen\nh_abbey.wmo", "New Hearthglen's abbey"),
    # Northrend's arched human windows: the barracks, 20 windows, 61%, from inside 41%; the town hall, 66%, and the Isle of
    # Conquest's keep, 55%, from outside only (from inside 15% and 3%); Wintergarde's small house, one of its two windows
    (r"world\wmo\northrend\buildings\human\nd_human_barracks\nd_human_barracks.wmo", "the barracks (Westguard Keep)"),
    (r"world\wmo\northrend\buildings\human\nd_human_boreanalliancetown_barracks\nd_human_boreanalliancetown_barracks.wmo",
     "the barracks (Valiance Keep, Wintergarde Keep)"),
    (r"world\wmo\northrend\buildings\human\nd_human_townhall\nd_human_townhall.wmo",
     "the town hall (Amberpine Lodge, Farshire, Wintergarde Keep)", FROM_OUTSIDE),
    (r"world\wmo\northrend\buildings\human\nd_human_bg_keep\nd_human_bg_keep.wmo", "the keep (the Isle of Conquest)", FROM_OUTSIDE),
    (r"world\wmo\northrend\buildings\human\nd_human_house03\nd_human_house03.wmo", "the small house (Wintergarde Keep)"),
    # ... and the inn (Valgarde's, Westguard Keep's): its ground-floor window from 79% of street views, its four upper ones
    # never (their rays meet the roof from inside), no flicker; from outside only (from inside 7%)
    (r"world\wmo\northrend\buildings\human\nd_human_inn\nd_human_inn.wmo", "the inn (Valgarde, Westguard Keep)", FROM_OUTSIDE),
    # frosted glass (2026-10-22, Phase B; its windows open only over the blurred copy, features/SeeThrough SeeThroughFrostBlur):
    # the dwarven houses, 4 windows, 55% of street views, from inside 97%; the Taunka's longhouse, 49%, 87%; the orcs'
    # medium huts, every street view, from inside 57%; their great halls from outside (53%; no windows of their rooms);
    # their two-storey houses and zeppelin houses from inside only (77, 78%; from the street 15 and 3%: their rooms behind
    # thick walls). And the vrykul longhouses (clear and leaded glass, not frosted), from outside (47%; from inside 7%).
    (r"world\wmo\khazmodan\buildings\dwarven_2story_hovel\mud_2story\mud_2story.wmo", "the dwarven house (Thelsamar, Dun Modr)"),
    (r"world\wmo\khazmodan\buildings\dwarven_2story_hovel\snow_2story\snow_2story.wmo", "the dwarven house, Dun Morogh's (Kharanos, Amberstill)"),
    (r"world\wmo\northrend\buildings\wintertauren_longhouse\wintertauren_snow_longhouse_b.wmo", "the Taunka's longhouse (Camp Tunka'lo)"),
    (r"world\wmo\kalimdor\buildings\orcmedium\orcmedium.wmo", "the orcs' hut (Kargath)"),
    (r"world\wmo\kalimdor\buildings\orcmedium\swamporcmedium.wmo", "the orcs' hut, the swamp's (Stonard)"),
    (r"world\wmo\kalimdor\buildings\orcgreathall\blackrockorcgreathall.wmo", "the orcs' great hall, Blackrock's", FROM_OUTSIDE),
    (r"world\wmo\kalimdor\buildings\orcgreathall\swamporcgreathall.wmo", "the orcs' great hall, the swamp's (Stonard)", FROM_OUTSIDE),
    (r"world\wmo\kalimdor\buildings\orctwostory\orctwostory.wmo", "the orcs' two-storey house (Ratchet, the Barrens)", FROM_INSIDE),
    (r"world\wmo\kalimdor\buildings\orctwostory\blackrockorctwostory.wmo", "the orcs' two-storey house, Blackrock's", FROM_INSIDE),
    (r"world\wmo\kalimdor\buildings\orczeppelinhouse\orczeppelinhouse.wmo", "the zeppelin house (Durotar)", FROM_INSIDE),
    (r"world\wmo\kalimdor\buildings\orczeppelinhouse\orczeppelinhouse_durotar01.wmo", "the zeppelin house, Durotar's", FROM_INSIDE),
    (r"world\wmo\kalimdor\buildings\orczeppelinhouse\orczeppelinhouse_gromgol.wmo", "the zeppelin house, Grom'gol's", FROM_INSIDE),
    (r"world\wmo\northrend\buildings\vrykul\nd_vrykul_longhouse.wmo", "the vrykul longhouse (Skorn)", FROM_OUTSIDE),
    (r"world\wmo\northrend\buildings\vrykul\nd_vrykul_longhousesnow.wmo", "the vrykul longhouse, snowed (Brunnhildar, Valkyrion)", FROM_OUTSIDE),
]
# Between rooms (2026-10-23, Phase C: "windows between two interiors: Ironforge first (the whole city is interior groups;
# shop windows face indoor streets)"): a group's windows that look into another interior group open onto it from inside
# (features/SeeThrough SeeThroughBetweenRooms, env/SeeThroughModel RoomBeyond). One row per group (the table's Group column),
# so a city goes street by street. From seethrough_tests' survey with SEETHROUGH_BETWEEN (its eyes in the group within 20 yd
# of a window, LookIn into the room beyond; walks through the group, the verdict held): Ironforge's 818 room windows, 500 of
# them onto another room, open from 30% of the eyes, 64 of 804 walks changing a window more than twice. Taken: the groups
# whose windows open from 35% of their eyes or more with no window changing more than twice on any walk (13 of 64); the
# Military and Mystic Wards' halls (61, 97, 37: 23 .. 48%, 1 .. 8 flickers) and the rest left out until in-game snapshots
# say more. The Great Forge (group 65, MOGN "Interior_K306": the forge and the ring of shops round it, the gryphon master,
# the smiths, the enchanters, the herbalist; its 73 windows onto them, 33 onto nothing) joined after the first in-game
# report (2026-10-23, "Ironforge windows don't work", the user on the ring before the herbalist's windows): from where
# players stand, 2 .. 4.5 yd before its ground-floor shop windows, 59 .. 100% of the eyes open them, and of 219 walks
# along the fronts one changed a window more than twice (SEETHROUGH_FRONTS). (Group 60, MOGN "The Great Forge", is the
# Commons' hall by the Stonefire Tavern: 17%, left out.) And the shops round the ring out onto it (2026-10-24, "Ironforge
# windows now seem to work from outside but not inside"): their rays cross the forge to its far side, 66 .. 180 yd, past
# the void's reach, until LookIn looked as far as the room's box (RoomReach); since, the herbalist's from 71% of the eyes
# (was 17%), and the shops taken as the first ones were: from 35% of their eyes or more, no window changing more than
# twice on any walk, nor on the walks along their fronts. Left out: 67 (20%, flickers), 72 (18%), 74 (4%), 89 (19%).
FROM_BETWEEN = 0x4
IF = r"world\wmo\khazmodan\cities\ironforge\ironforge.wmo"
SEE_THROUGH_GROUPS = [
    # (wmo, group, comment): the group's name (MOGN) and the rooms its windows look into
    (IF, 12, "Ironforge: the hall by the Hall of Explorers (12), onto its rooms 22 and 28 (43%)"),
    (IF, 21, "Ironforge: L_guilds02, onto the hall 61 (66%)"),
    (IF, 24, "Ironforge: a room off the hall 97 (56%)"),
    (IF, 27, "Ironforge: R_library, onto the hall 61 (65%)"),
    (IF, 31, "Ironforge: T_warriors, onto the Forlorn Cavern 78 (54%)"),
    (IF, 32, "Ironforge: the Military Ward's room, onto the hall 16 (36%)"),
    (IF, 45, "Ironforge: Interior_F302, onto 79 (39%)"),
    (IF, 47, "Ironforge: InteriorSmithA01 (the smithy), onto the halls 8 and 16 (68%)"),
    (IF, 48, "Ironforge: a room off the hall 8 (65%)"),
    (IF, 51, "Ironforge: Interior_H112, onto the hall 16 (38%)"),
    (IF, 55, "Ironforge: Interior_F303, onto the hall 95 (41%)"),
    (IF, 75, "Ironforge: the rogues' room, onto the hall 95 (36%)"),
    (IF, 78, "Ironforge: the Forlorn Cavern's hall (78), onto T_warriors 31 (42%)"),
    (IF, 65, "Ironforge: the Great Forge (65), from its ring into the shops round it (its ground-floor shop windows from 59 .. 100% of the eyes 2 .. 4.5 yd before them)"),
    (IF, 26, "Ironforge: the enchanters' shop on the Great Forge's ring (26), out onto the forge (72%)"),
    (IF, 66, "Ironforge: the leatherworkers' shop on the Great Forge's ring (66), out onto the forge (63%)"),
    (IF, 68, "Ironforge: the miners' shop on the Great Forge's ring (68), out onto the forge (66%)"),
    (IF, 69, "Ironforge: the tailors' upper room on the Great Forge's ring (69), out onto the forge (42%)"),
    (IF, 71, "Ironforge: the herbalist's and first aid shop on the Great Forge's ring (71), out onto the forge (71%)"),
    (IF, 76, "Ironforge: an upper room on the Great Forge's ring (76), out onto the forge (66%)"),
]
SEE_THROUGH_COLUMNS = ["u", "s", "u", "s", "i"]   # ID Wmo Flags Comment Group (-1: the whole WMO)


def see_through_table():
    """SeeThroughBuildings.cdbc's rows: every SEE_THROUGH building, from outside and from inside unless its entry says
    (a third member: its Flags), the whole WMO (Group -1); then every SEE_THROUGH_GROUPS group, between rooms."""
    rows = [[i + 1, entry[0], entry[2] if len(entry) > 2 else FROM_OUTSIDE | FROM_INSIDE, entry[1], -1] for i, entry in enumerate(SEE_THROUGH)]
    rows += [[len(rows) + i + 1, wmo, FROM_BETWEEN, comment, group] for i, (wmo, group, comment) in enumerate(SEE_THROUGH_GROUPS)]
    return rows


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
        print(f"{len(rows)} rows: {len(SEE_THROUGH)} buildings, {len(SEE_THROUGH_GROUPS)} groups between rooms")
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
