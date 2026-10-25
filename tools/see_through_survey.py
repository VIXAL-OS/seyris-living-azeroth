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
    # (the orcs' great halls, from outside only, out since whole rooms, 2026-10-24: as one their rooms open from 19 and 14% of
    # street views and flicker in 50 and 46% of the camera's swings: Blackrock's and the swamp's (Stonard) great hall)
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
# Dalaran (2026-10-24, Phase D: stained glass, frosted glass and windows between rooms at once). One WMO for the city
# (nd_dalaran.wmo, 91 groups, placed once over Crystalsong Forest; no building of its own has glass): 3,949 windows in its
# exterior groups, 1,091 of them with a room behind; 529 in its rooms. Its rows name rooms, each with its own flags (a
# fourth member): from outside (FROM_OUTSIDE on a room: the windows of the exterior groups into it, env/SeeThroughModel
# ListOutside), from inside (FROM_INSIDE: the room's own windows onto the street) and between rooms (FROM_BETWEEN).
# From seethrough_tests' survey of it (SEETHROUGH_SURVEY with SEETHROUGH_SURVEY_VERBOSE, its lines added up by room), by
# the Phase C rule: 35% of the views or eyes open or more, and no window changing more than twice on any walk. The rooms'
# names are the client's (WMOAreaTable, by each group's MOGP ID; the MOGN names are the artists').
# Chosen. From outside: Cartier & Co. Fine Jewelry (19, 70% of street views, stained), two upper rooms of the Legerdemain
# Lounge (57, 58: 55 and 54%, frosted and stained), the Threads of Fate's side room (11, 40%) and the Purple Parlor (89,
# 35%, stained). From inside onto the street: Cartier & Co. (19, 95% of eyes), the Violet Gate's two halls (53, 54: 88
# and 87%, frosted and stained) and the Agronomical Apothecary (18, 49%). Between rooms: the Underbelly's Circle of
# Wills through its window into Cantrips & Crows (71 onto 70, 94%, frosted).
# Surveyed again the same day once Dalaran's stone surrounds (eb_dalaran_window3, gen_glass NO_WINDOWS) were no windows: they
# had made 1,910 of its windows and held some rooms under the rule. Added from outside: Curiosities & Moore (6, 7 and
# upstairs 9: 73, 67 and 35% of street views), the Threads of Fate (10 and upstairs 13: 74 and 35%), the Legerdemain
# Lounge's hall (55: 40%) and the Silver Enclave's hall (78: 60%, its one window), none of their windows changing more than
# twice on any walk. Still out from outside: the Violet Gate's halls (53, 54: 67 and 45%, but two windows each flicker),
# the Visitor Center and the Wonderworks (8, 12: 29%), Vargoth's Retreat (90: 35%, a window flickering), the Violet
# Citadel (36: 38%, a window flickering). The rows' percentages are this survey's; below, the first one's reasons.
# Then, the same day, from Curiosities & Moore's raised walkway (Dalaran 49.7, 58.1, the camera 16 yd behind and 9 up):
# "Curiosities' outside windows seem to flicker a bit" (snapshots 20261024-162235 and -162259: window 1948 open with 7 of
# its 9 rays seeing its room from one camera, shut with none from the other, 12 yd along). The walks pass a front once,
# so a window open in a narrow cone of views changes twice and passes; a camera swinging about a player crosses its edge
# every swing. The survey now also swings the camera round players standing on every level before each window
# (SEETHROUGH_SWINGS: streets, walkways, stairs; the camera 16 yd out at 30 degrees or 10 at 20, 60 degrees either way
# and back at 30 a second, stopped by walls), and the rule takes each listed window too: it changes more than twice in a
# swing (a narrow band of views opening or shutting it) in at most 10% of its runs. Out again: Curiosities & Moore
# upstairs (9: window 1948, 17% of its runs; it opens from 19% of its street views), the Threads of Fate upstairs (13,
# the same building: 1932, 21%; 19%) and the Lounge's hall (55: its one window 1971, 13%; 39%). Kept: Curiosities &
# Moore (6, 7), the Threads of Fate (10) and the Silver Enclave's hall (78), their windows at most 6%; Phase D's rooms
# at most 4% (Cartier & Co., the Lounge's upper rooms; the Purple Parlor's worst window is 18%, not its 2.5% then: below)
# but for the Threads of Fate's side room (11: window 1967, 14%; 40% of its street views), left for a decision.
# Then, the same day: "Threads of Fate side room works fine afaict" (11, its window 1967 at 14%) and "Weirdly the Violet
# Gate windows work fine from the inside but not from the outside" (53, 54: listed from inside only). A room's row opens
# all its windows, so one flickering window held a whole room out; the table now has window rows too (SEE_THROUGH_WINDOWS:
# the Window column, a window's ordinal in its WMO; its Flags the ways it may open, 0 none, a mask over its room's). The
# rule by window: one is shut when its camera swings flicker in more than 15% of its runs, or any walk changes it more
# than twice; its room opens from outside when the rest open from 35% of their street views or more. 15%: 1967 at 14.0%
# (7 of 50 runs) the user finds fine; 1948 at 16.7% (9 of 54) is the one the user saw flicker, and runs between them are
# one in fifty apart. Opened from outside so: the Violet Gate's halls (53: 1955, 1956 and 1957 shut, the rest 64% of their
# street views; 54: 1952, 1953, 1954, 1985, 1988, 1990 and 1992 shut, the rest, 1958 and 1961, 65%), Curiosities & Moore
# upstairs (9: 1948 shut, 43%), the Threads of Fate upstairs (13: 1932 shut, 43%), the Lounge's hall (55: its one window,
# 1971 at 13%, 40%) and Archmage Vargoth's Retreat (90: 4078, 4125, 4127, 4150 and 4151 shut, the rest 36%). Shut in a
# room already open: the Purple Parlor's 3905, 3961 and 3964 (89: 17 .. 18% of 22 .. 24 runs; the rest 36%). Still out:
# the Violet Citadel (36: 2100, 2101 and 2102 shut, the rest 23%), the Visitor Center and the Wonderworks (8, 12: none
# shut, 29%).
# Left out from outside: Curiosities & Moore (7: 34.8%, a hair under; 6: 29%), the Threads of Fate's main room (10:
# 29%), the Visitor Center, the Wonderworks and their upper floors (8, 9, 12, 13: 20%), the Violet Gate (53, 54: 15%), the
# Silver Enclave's hall (78: 31%, 13 windows flickering), the Agronomical Apothecary (18: 63%, but its one window
# flickers), Archmage Vargoth's Retreat (90: 35%, a window flickering), the Violet Citadel (36: 12%, 5 flickering) and the
# shops whose rooms show through their windows from no street view at all (21, 23, 25 .. 34, 0, 1, 4, 16, 42, 44, 84).
# From inside: the Purple Parlor and Vargoth's Retreat (89, 90: 46 and 50%, 8 and 7 windows flickering), the Silver
# Enclave's hall (78: 38%, a window flickering), the Violet Citadel (36: 33%, 17 flickering), and the banks' and shops'
# rooms whose windows the street is seen through from 0 .. 29% of eyes (15, 35, 21, 29, 39, 71 ...). Between rooms the
# city has only 10 windows onto another room (519 onto the street or nothing): the Violet Gate's stairwell (14, onto 53
# and 54: no floor to stand on, so no eyes) and the Legerdemain Lounge's upstairs window onto its hall (56 onto 55: 3
# eyes, too few to judge) are left out.
# Whole rooms, the same day ("in general can we have all windows in a room behave the same? like either they're all see
# through or they're not, and if they're not then have their lights either all on or off", the user before Curiosities &
# Moore from its walkway; then "Threads of Fate is actually more flickery now than it used to be sadly"). A room's windows
# on the screen now open together, only when every one is seen through, and shut together at once when one isn't
# (features/SeeThrough, WholeRooms). The window rows above no longer fit (a window shut by its row would hold its whole
# room shut whenever it's on the screen) and are gone; rooms are judged whole. seethrough_tests' SEETHROUGH_ROOMS: each
# room's street views open as one (every one of its windows on the screen, looking at the window, opens) and its camera
# swings as a viewer sees them (a frame changes when any of its windows on the screen does; the camera looking at the
# room). The rule: open as one from 35% of its street views or more, and as one at most 25% of its swings changing it
# more than twice. 25%: the Threads of Fate's side room (11), "works fine afaict", flickered in 26.7% of its swings as a
# viewer saw it window by window (20.0% as one), the cut just under it. Kept from outside: Curiosities & Moore's other
# room (7: 50% of street views as one, 7% of swings), the Threads of Fate's side room (11: 62%, 20%), the Lounge's hall
# (55: 40%, 20%), its upper rooms (57, 58: 100%, 7.5 and 6%) and the Silver Enclave's hall (78: 60%, 4%). Out from
# outside: Curiosities & Moore (6: 61%, but 43% of swings flicker as one: a window that never opens, 1984, comes and goes
# from the screen), the Threads of Fate (10: 62%, 29%), and the rooms that open as one from almost nowhere (a window of
# theirs never seen through: Curiosities & Moore and the Threads of Fate upstairs, 9 and 13, 1%; Cartier & Co., 19, 0.4%;
# the Violet Gate's halls, 53 and 54, 7 and 2%; the Purple Parlor and Vargoth's Retreat, 89 and 90, 0%). From inside, the
# rooms looked through eight ways from each eye, as one: the Apothecary 85%, Cartier & Co. 97%, the Violet Gate's halls
# 100%, the Circle of Wills onto Cantrips & Crows 100%: all kept.
DAL = r"world\wmo\northrend\dalaran\nd_dalaran.wmo"
SEE_THROUGH_GROUPS += [
    (DAL, 7, "Dalaran: Curiosities & Moore's other room, from outside (as one 50% of street views, 7% of swings flickering), stained glass",
     FROM_OUTSIDE),
    (DAL, 11, "Dalaran: the Threads of Fate's side room, from outside (as one 62%, 20%), stained glass", FROM_OUTSIDE),
    (DAL, 55, "Dalaran: the Legerdemain Lounge's hall, from outside (as one 40%, 20%), stained glass", FROM_OUTSIDE),
    (DAL, 57, "Dalaran: an upper room of the Legerdemain Lounge, from outside (as one 100%, 7.5%), stained glass", FROM_OUTSIDE),
    (DAL, 58, "Dalaran: the Legerdemain Lounge's other upper room, from outside (as one 100%, 6%), stained glass", FROM_OUTSIDE),
    (DAL, 78, "Dalaran: the Silver Enclave's hall, from outside (as one 60%, 4%), stained glass", FROM_OUTSIDE),
    (DAL, 18, "Dalaran: the Agronomical Apothecary, from inside onto the street (as one 85% of views), stained glass", FROM_INSIDE),
    (DAL, 19, "Dalaran: Cartier & Co. Fine Jewelry, from inside onto the street (as one 97%), stained glass", FROM_INSIDE),
    (DAL, 53, "Dalaran: the Violet Gate's hall, from inside onto the street (as one 100%), stained glass", FROM_INSIDE),
    (DAL, 54, "Dalaran: the Violet Gate's other hall, from inside onto the street (as one 100%), stained glass", FROM_INSIDE),
    (DAL, 71, "Dalaran: the Underbelly's Circle of Wills, through its window into Cantrips & Crows (70; as one 100%), frosted glass", FROM_BETWEEN),
]
# Single windows (2026-10-24): (wmo, its group, its ordinal in the WMO, comment, Flags: the ways it may open, 0 none). A
# window's row only takes ways away from its room's and group's rows (env/SeeThroughModel MaskWindows); with whole rooms
# a window shut by its row holds its room shut whenever it's on the screen. None since whole rooms (above).
SEE_THROUGH_WINDOWS = []
SEE_THROUGH_COLUMNS = ["u", "s", "u", "s", "i", "i"]   # ID Wmo Flags Comment Group (-1: the whole WMO) Window (-1: none)


def see_through_table():
    """SeeThroughBuildings.cdbc's rows: every SEE_THROUGH building, from outside and from inside unless its entry says
    (a third member: its Flags), the whole WMO (Group -1); then every SEE_THROUGH_GROUPS group, between rooms unless its
    entry says (a fourth member: its Flags); then every SEE_THROUGH_WINDOWS window (Window its ordinal; the rest -1)."""
    rows = [[i + 1, entry[0], entry[2] if len(entry) > 2 else FROM_OUTSIDE | FROM_INSIDE, entry[1], -1, -1] for i, entry in enumerate(SEE_THROUGH)]
    rows += [[len(rows) + i + 1, entry[0], entry[3] if len(entry) > 3 else FROM_BETWEEN, entry[2], entry[1], -1]
             for i, entry in enumerate(SEE_THROUGH_GROUPS)]
    rows += [[len(rows) + i + 1, wmo, flags, comment, group, ordinal]
             for i, (wmo, group, ordinal, comment, flags) in enumerate(SEE_THROUGH_WINDOWS)]
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
        print(f"{len(rows)} rows: {len(SEE_THROUGH)} buildings, {len(SEE_THROUGH_GROUPS)} group rows, {len(SEE_THROUGH_WINDOWS)} window rows")
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
