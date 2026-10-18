#!/usr/bin/env python3
"""Window glass: which WMO textures are glass and what kind, and where the panes stand, as data
(data/GlassMaterials.cdbc and data/GlassPanes.cdbc, read by features/Glass; docs/cdbc-tables.md).

Why: "make the window glass look like real glass ... every glass window, not only stained glass" (the user,
2026-10-17). Nearly every window material is opaque (MOMT blend 0: 2 of ~1,100 are not), so the glass is a look the
pixel shader gives the glass texels of those textures, and the panes are rectangles for the light that comes in.

GlassMaterials (one row per texture). Found by name, then reviewed by eye (KINDS below, from contact sheets of every
candidate texture):
- A candidate's file name says window, wndw, stained, glass or skylight, or has "wnd" as a word of its own
  (mm_elwynn_wnd_ext__01: yes; strmwnd, Stormwind's walls and trim, and mm_browndoor: no). Not the street lamps' glass
  or Dalaran's axe sign. Glass is glass wherever it is: no lived-in filter, no self-lit (SIDN) requirement.
- Each candidate is classified by hand in KINDS: clear, stained, frosted (translucent: icy panes, hide and parchment
  "windows", which also get MATTE: no gloss) or leaded (clear glass in a lead grid). Not glass (stone frames, metal
  shutters, openings painted black) and broken, burnt or abandoned glass (abndn, burn, ruined places) are left out
  (SKIP). A candidate not in either is reported as unreviewed and left out.
- Which texels are glass (the frame, the lead and the wall around a window painted into one are not): the texture's
  texels split into two colour clusters (k-means); the glass is the brighter one when they differ in brightness
  (frames and lead are dark), else the one nearest the hint colour in KINDS. The shader's mask is one plane through
  colour space, saturate(dot(texel.rgb, MaskRGB) + MaskBias), half-way between the clusters, soft over 60% of the gap.
- The glass's average colour (the texels by mask), and the colour of the light it lets in: stained glass its own colour
  at full value; clear, leaded and frosted glass nearly white, with a quarter (frosted: half) of its tint.

GlassPanes (one row per window pane of every placed WMO with glass, all maps the scan has, no lived-in filter):
- The pane: the connected, coplanar glass triangles of one group (a curved band of glass is cut where it turns, and a
  very large sheet into TILE yd tiles); its middle, its rectangle on its own plane (along it horizontally and up it;
  skylights along x and y), and the way out of the building.
- Which way is out: an exterior group's (MOGP 0x8) pane faces out by its winding. An interior group's pane is the
  same window seen from inside when an exterior pane stands within TWIN yd facing the other way; then out is behind
  it. Otherwise open rays decide, as for the window lights (gen_window_lights.orient: the side whose rays get away);
  if they can't, an interior face faces in.
- The room: the interior group the window lets light into (the inside face's group, or the interior group holding a
  point ROOM_PROBE yd behind an exterior pane), as a world box. The light lands only in that box (features/Glass's
  leak guard). Panes with no room behind them (a facade's painted windows, an exterior-only shell) are left out.
- In world coordinates (the placement convention of gen_window_lights.placement), with the placement's unique ID; its
  light is its texture's x the share of the texture that is glass (at least 0.35). Windows painted into a wall texture
  have no panes (their triangles are the whole wall).

RoomStyles and GlassBuildings: the rooms behind the glass differ by building. "Per building: pick the tint and
brightness from the building's identity ... Room types that follow the building and the zone" (the user, 2026-10-17).
- A placement's class (room_class) from its WMO's file name: inn, house, farm, shop (smithies, banks, labs), temple,
  keep (barracks, towers, walls, town halls; dungeons), tower (mage towers: books), ship, other. A city's one big WMO
  (CITY_WMOS: Stormwind, Dalaran, Silvermoon, Booty Bay...) is a city, and each of its groups with glass is a building
  of its own, by the group's name (shopB18, interior_inn, bankinterior) or its district (DISTRICTS), else a city's.
- Its palette (palette_of) from its culture (the path: vrykul, elven, Dalaran, goblin, Forsaken, orc...), else its
  place: the area and its parents (Duskwood, the Scarlet Crusade's towns, Forsaken zones, goblin ports...), else the
  continent (Northrend's humans colder, Outland's outposts ruddy), else human (warm).
- A class (CLASSES: the layouts' weights, brightness, rooms dark by day, how late its lights stay on) and a palette
  (PALETTES: two tints, the lamp, the curtains, brightness, weights x these) make a style, one row per pair in use.
- Each placement (and city group) its style, a seed (a CRC of the map, its unique ID and group: steady across runs),
  flags: a city (the shader's 14-yd blocks are its buildings), a dark place (gen_window_lights' DARK_AREAS or
  DARK_PATHS: no lit rooms at night), empty (every room dark: a share of each class's buildings the client doesn't
  light at night), self-lit (its WMO has windows the client lights at night). And one default per WMO and map for its
  instances the table doesn't place (transports).
- The night's schedule is the runtime's (env/GlassModel LightsOutShare; lights_out here is the same, for the report).
- Frosted glass has no room, but the windows the client lights (SIDN, or unlit: at full day and night) go dark by the
  same schedule ("Give Living Azeroth's frosted window glass ... a night lights-out schedule and a hearth-fire flicker",
  the user, 2026-10-18): a WMO with such glass counts as lit (never empty). A dungeon's WMO (its path) is flagged so
  its frosted windows keep the client's look.
- Ironforge, underground and never fully asleep: each glass group's class by the client's own sub-zone for it
  (WMOAreaTable.dbc: the Great Forge and smithies a forge, always lit; the ring road, the halls and the gates the
  Commons, mostly lit; the Military and Mystic Wards' homes quarters, dimming late and gently; the Forlorn Cavern a
  cavern, darker), and its interior groups flagged underground (no sky behind their glass: the street's look, and
  their panes light no floor). IRONFORGE_AREAS below is the mapping.

Inputs (the map scan in waterfall-scan, see its README; nothing here opens an MPQ):
  <scan>/adt_wmos.tsv, <scan>/maps.tsv, <scan>/AreaTable.dbc, <scan>/terrain-scan/ (areas)
  <scan>/WMOAreaTable.dbc   (the client's DBFilesClient/WMOAreaTable.dbc, from patch-enUS-3.MPQ: Ironforge's sub-zones;
                            without it Ironforge's groups are classed by their names, as a city's)
  <scan>/wmo-windows/     WMO roots and groups (waterscan --extract-list <Data> <list> <scan>/wmo-windows)
  <textures>/             the candidate textures (waterscan --extract-list <Data> <this tool's --texture-list>)
Usage:
  python gen_glass.py <scan> --texture-list <out.txt>       the candidate textures to extract
  python gen_glass.py <scan> --group-list <out.txt>         the group files of WMOs with glass not extracted yet
  python gen_glass.py <scan> <textures> [data dir] [--sheets <dir>]
        writes GlassMaterials.cdbc, GlassPanes.cdbc, RoomStyles.cdbc and GlassBuildings.cdbc (--sheets: contact sheets
        of each texture and its mask)
  python gen_glass.py <scan> --rooms [data dir]             only RoomStyles.cdbc and GlassBuildings.cdbc
"""
import collections
import csv
import math
import os
import re
import struct
import sys
from pathlib import Path

import cdbc_io as io
import gen_window_lights as wl

BS = "\\"
CONTINENTS = {0: "Eastern Kingdoms", 1: "Kalimdor", 530: "Outland", 571: "Northrend"}

# --- which textures are glass --------------------------------------------------------------------------------------
CANDIDATE = re.compile(r"window|wndw|stained|glass|skylight|(^|[_\d])wnd([_\d]|$)")
NOT_CANDIDATES = ("lampglass", "glass_axe")
CLEAR, STAINED, FROSTED, LEADED = 1, 2, 3, 4
KIND_NAMES = {CLEAR: "clear", STAINED: "stained", FROSTED: "frosted", LEADED: "leaded"}
# Flags (GlassMaterials.Flags)
INSIDE_FACE = 0x1    # the texture is a window's inside face (_int_): the inside look whatever the group
OUTSIDE_FACE = 0x2   # ... its outside face (_ext_): the outside look whatever the group
MATTE = 0x4          # translucent membrane (hide, parchment): no gloss, no reflection
PAINTED = 0x8        # a window painted into a wall texture: only the masked texels are glass

# Reviewed by eye, 2026-10-17 (contact sheets of all 138 candidates): (kind, flags, mask mode, hint colour or None).
# Mask modes: "auto" (brighter cluster, else nearest the hint), "hint" (the cluster nearest the hint colour), "all".
RED, BLUE, GOLD, PINK, ICE = (0.75, 0.1, 0.12), (0.3, 0.5, 0.9), (0.9, 0.7, 0.2), (0.85, 0.4, 0.7), (0.75, 0.85, 0.95)
KINDS = {
    # Elwynn, Duskwood, Redridge and Westfall houses (and the Northrend vrykul dwellings reuse Elwynn's and the pirate
    # ones): leaded panes and grids, a "lit" orange quarter; _ext_ the outside face, _int_ the inside one.
    "mm_elwynn_wnd_ext__01": (LEADED, OUTSIDE_FACE, "auto", None),
    "mm_elwynn_wnd_int__01": (LEADED, INSIDE_FACE, "auto", None),
    "mm_duskwood_wnd_ext__01": (LEADED, OUTSIDE_FACE, "auto", None),
    "mm_duskwood_wnd_int__01": (LEADED, INSIDE_FACE, "auto", None),
    "mm_rr_wnd_ext__01": (LEADED, OUTSIDE_FACE, "auto", None),
    "mm_rr_wnd_int__01": (LEADED, INSIDE_FACE, "auto", None),
    "mm_westfall_wnd_ext__01": (LEADED, OUTSIDE_FACE, "auto", None),
    "mm_westfall_wnd_int__01": (LEADED, INSIDE_FACE, "auto", None),
    # Stormwind's houses and towers, Stratholme, Shadowfang
    "mm_strmwnd_windows_01": (LEADED, 0, "auto", None),
    "mm_strmwnd_windows_02": (LEADED, 0, "auto", None),
    "mm_strmwnd_wwindows_01": (LEADED, 0, "auto", None),
    "mm_strmwnd_hwindows_01": (LEADED, 0, "auto", None),
    "mm_strmwnd_mwindows_01": (STAINED, 0, "auto", None),
    "mm_strath_wwindows_01": (LEADED, 0, "auto", None),
    "mm_strath_windows_13": (STAINED, 0, "auto", None),
    "mm_strath_windows_14": (STAINED, 0, "auto", None),
    "mm_shdwfang_wnd_01": (LEADED, 0, "auto", None),
    "bm_shdwfang_wnd_ext_01": (LEADED, OUTSIDE_FACE, "auto", None),
    # Northrend humans (Valgarde, the Borean towns, Wintergarde)
    "nr_hu_window": (LEADED, 0, "auto", None),
    "nr_hu_window_half": (LEADED, 0, "auto", None),
    "nr_hu_window_door": (LEADED, 0, "auto", None),
    "nr_hu_interwindow": (LEADED, INSIDE_FACE, "auto", None),
    "nr_hu_interwindow_blue": (LEADED, INSIDE_FACE, "auto", None),
    # Stained glass: the Cathedral of Light, Northshire Abbey, the Redridge chapel, the Scarlet Monastery, Dalaran,
    # Ulduar, Karazhan, Silvermoon and the Sunwell, the draenei, the night elves, Azjol-Nerub, Icecrown...
    "mm_strmwnd_cwindows_02": (STAINED, 0, "auto", None),
    "mm_stained_01": (STAINED, 0, "auto", None),
    "mm_redridge_stained_01": (STAINED, 0, "auto", None),
    "mm_windows_11": (STAINED, 0, "auto", None),
    "mm_windows_112": (STAINED, 0, "auto", None),
    "mm_windows_13": (STAINED, 0, "auto", None),
    "mm_windows_14": (STAINED, 0, "auto", None),
    "mm_windows_15": (STAINED, 0, "auto", None),
    "mm_windows_16": (STAINED, 0, "auto", None),
    "mm_windows_17": (STAINED, 0, "auto", None),
    "eb_dal_allianceglass": (STAINED, 0, "auto", None),
    "eb_dalaran_windows1": (STAINED, 0, "auto", None),
    "eb_dalaran_portalwindow": (STAINED, 0, "auto", None),
    "dal_window_04": (STAINED, 0, "auto", None),
    "dal_prisonwindow": (STAINED, 0, "auto", None),
    "eb_dalaran_window3": (FROSTED, 0, "auto", None),
    "ce_evilglass": (STAINED, 0, "auto", None),
    "ce_uldr_stainedglass01": (STAINED, 0, "auto", None),
    "ce_uldr_stainedglass03": (STAINED, 0, "auto", None),
    "ce_uldr_warroomstainedglass": (STAINED, 0, "auto", None),
    "eb_stainedglass": (STAINED, 0, "auto", None),
    "eb_uldr_redwindow": (STAINED, 0, "auto", None),
    "mm_khzn_window_01": (STAINED, 0, "auto", None),
    "mm_khzn_window_02": (STAINED, 0, "auto", None),
    "jlo_kzn_intwindow": (STAINED, 0, "auto", None),
    "bm_kzn_intwindow01": (STAINED, 0, "auto", None),
    "bldelf_window": (STAINED, 0, "hint", RED),
    "bldelf_windowbtm": (STAINED, 0, "hint", RED),
    "bldelf_windowtop": (STAINED, 0, "hint", RED),
    "wd_bldelfwindowbtm1": (STAINED, 0, "hint", RED),
    "wd_bldelfwindowtop1": (STAINED, 0, "hint", RED),
    "jlo_bloodelf_windows": (STAINED, 0, "hint", RED),
    "jlo_bloodelf_wallwindow_01": (STAINED, PAINTED, "hint", RED),
    "wall_windowdark": (STAINED, PAINTED, "hint", RED),
    "jlo_highelf_windows": (STAINED, 0, "hint", BLUE),
    "jlo_highelf_wallwindow_01": (STAINED, PAINTED, "hint", BLUE),
    "jlo_draenei_window_01": (STAINED, 0, "auto", None),
    "jlo_draenei_window_02": (STAINED, 0, "auto", None),
    "jlo_draenei_window_03": (STAINED, 0, "auto", None),
    "jlo_draenei_window_04": (STAINED, 0, "auto", None),
    "jlo_draenei_window_05": (STAINED, 0, "auto", None),
    "jlo_draenei_window_06": (STAINED, 0, "auto", None),
    "jlo_draenei_window_07": (STAINED, 0, "auto", None),
    "mm_ne_windows_01": (STAINED, 0, "auto", None),
    "mm_ne_ds_windows_01": (STAINED, 0, "auto", None),
    "mm_drnss_skylight_01": (STAINED, 0, "auto", None),
    "jlo_azjnub_window01": (STAINED, 0, "auto", None),
    "jlo_azjnub_window02": (STAINED, 0, "auto", None),
    "jlo_azjnub_window03": (STAINED, 0, "auto", None),
    "jlo_azjnub_windoworange": (STAINED, 0, "auto", None),
    "jlo_icec_window": (STAINED, 0, "auto", None),
    "fskn_windows01": (STAINED, 0, "auto", None),
    "wr_window": (STAINED, 0, "auto", None),
    "zangr_window": (STAINED, 0, "auto", None),
    "zangr_windows": (STAINED, 0, "auto", None),
    "jlo_spore_windows": (STAINED, 0, "auto", None),
    "mm_gob_window_01": (STAINED, 0, "auto", None),
    "bm_brspire_windows01": (STAINED, 0, "auto", None),
    # Clear glass: portholes, ships, Booty Bay, gnomes and goblins, the Westfall lighthouse
    "am_bbay_shipwindow": (CLEAR, 0, "auto", None),
    "am_bbay_windows": (CLEAR, 0, "auto", None),
    "am_bbay_windows_pirate": (CLEAR, 0, "auto", None),
    "jlo_bbay_shipwindow": (CLEAR, 0, "auto", None),
    "jlo_bbay_windows": (CLEAR, 0, "auto", None),
    "js_bbay_shipwindow": (CLEAR, 0, "auto", None),
    "js_bbay_windows": (CLEAR, 0, "auto", None),
    "mm_gnome_window_01": (CLEAR, 0, "auto", None),
    "mm_goblin_windows": (CLEAR, 0, "auto", None),
    "westfarmwindow": (CLEAR, 0, "auto", None),
    "wd_trapper_windows": (CLEAR, 0, "auto", None),
    # Frosted and translucent: the vrykul's icy panes (Storm Peaks cliff villages), Ironforge's and the Northrend
    # dwarves' glowing panes, the winter orcs', the draenei's pale glass; hide and parchment (matte).
    "vrykul_window04": (FROSTED, 0, "auto", None),
    "vrykul_window3": (FROSTED, 0, "auto", None),
    "jac_window01": (FROSTED, PAINTED, "hint", ICE),
    "jlo_irondwarf_windows_01": (FROSTED, 0, "auto", None),
    "mm_irnfg_windows_01": (FROSTED, 0, "auto", None),
    "mm_irnfg_swindows_01": (FROSTED, 0, "auto", None),
    "mm_irnfg_windows_02": (FROSTED, 0, "auto", None),
    "jlo_worc_window01": (FROSTED, 0, "auto", None),
    "jlo_worc_window02": (FROSTED, 0, "auto", None),
    "jlo_draenei_window": (FROSTED, 0, "auto", None),
    "mm_orc_bkrwindow_01": (FROSTED, 0, "auto", None),
    "mm_orc_smpwindow_01": (FROSTED, 0, "auto", None),
    "tuskarr_windowsb": (FROSTED, 0, "auto", None),
    "mm_ogrmr_window_01": (FROSTED, MATTE, "auto", None),
    "draeniwindow": (FROSTED, MATTE, "auto", None),
    "jlo_wtauren_window01": (FROSTED, MATTE, "auto", None),
    "tuskarr_windowsa": (FROSTED, MATTE, "hint", GOLD),
}
# Candidates that aren't glass, and broken, burnt or abandoned glass.
SKIP = {
    "bi_grimbatol_windows_02": "an opening painted black", "bridgewindow": "wood and iron", "dal_windowcutout": "stone lattice",
    "eb_dal_prison_window3": "a slit in stone", "drak_skylight01": "patterned stone", "frostdwarf_windowwall_01": "stone",
    "jlo_irondwarf_windowwall_01": "stone and iron", "mm_window_frame_01": "stone frame", "jlo_kzn_intwindow02": "dark shutter",
    "nd_utgardeext_windowarchtrim": "stone trim", "nd_utgardepin_windowarchtrim": "stone trim",
    "rg_hfire_rmp_window_oct": "iron", "rg_hfire_rmp_windows_thin": "iron", "shattrath_window": "stone",
    "tower_window_01": "openings in a stone wall", "wr_round_window": "stone rosette", "wr_round_window_metal": "metal rosette",
    "bm_zeppelin_windowbase01": "wood",
    "am_bbay_windowsburn": "burnt", "bldelf_windowbtmabdn": "abandoned", "bldelf_windowtopabdn": "abandoned",
    "jlo_bloodelf_windows_abndn": "abandoned", "mm_aduskwood_wnd_int__01": "abandoned (broken panes)",
    "mm_orc_awindow_01": "abandoned (torn)", "tuskarr_windowsbdestroyed": "destroyed", "wall_windowabandn": "abandoned",
    "silvermyst_inn_window01": "a ruined inn", "azr_window01": "ruins", "cer_window01": "ruins",
}
BROKEN_WORDS = ("abndn", "abandn", "abandon", "burn", "broken", "destroyed", "ruin")


def base_name(path):
    b = path.lower().replace("/", BS).split(BS)[-1]
    return b[:-4] if b.endswith(".blp") else b


def is_candidate(path):
    b = base_name(path)
    return bool(CANDIDATE.search(b)) and not any(n in b for n in NOT_CANDIDATES)


def classify(path):
    """(kind, flags, mode, hint) of a texture, or (0, reason) when it isn't glass we use."""
    b = base_name(path)
    if b in SKIP:
        return 0, SKIP[b]
    if any(w in b for w in BROKEN_WORDS):
        return 0, "broken, burnt or abandoned"
    if b in KINDS:
        return KINDS[b]
    return 0, "unreviewed" if is_candidate(path) else "not a window texture"


# --- texels: mask and colour ---------------------------------------------------------------------------------------
LUMA = (0.2126, 0.7152, 0.0722)
SOFT = 0.6          # the mask goes from 0 to 1 over this share of the gap between the clusters


def texels(path, side=128):
    from PIL import Image
    import numpy as np
    im = Image.open(path)
    im.load()
    im = im.convert("RGB").resize((side, side))
    return np.asarray(im, dtype=np.float64).reshape(-1, 3) / 255.0


def two_clusters(px):
    """k-means, k = 2, started at the darkest and brightest tenth: (centres, labels)."""
    import numpy as np
    lum = px @ np.array(LUMA)
    order = np.argsort(lum)
    tenth = max(1, len(px) // 10)
    c = np.array([px[order[:tenth]].mean(axis=0), px[order[-tenth:]].mean(axis=0)])
    for _ in range(12):
        d = ((px[:, None, :] - c[None, :, :]) ** 2).sum(axis=2)
        lab = d.argmin(axis=1)
        for k in range(2):
            if (lab == k).any():
                c[k] = px[lab == k].mean(axis=0)
    return c, lab


def mask_plane(px, mode, hint):
    """(MaskRGB, MaskBias, glass share): the shader's mask saturate(dot(rgb, MaskRGB) + MaskBias)."""
    import numpy as np
    if mode == "all":
        return (0.0, 0.0, 0.0), 1.0, 1.0
    c, lab = two_clusters(px)
    lum = c @ np.array(LUMA)
    if mode == "hint" or (mode == "auto" and abs(lum[0] - lum[1]) < 0.08):
        h = np.array(hint if hint else px.mean(axis=0))
        if hint is None:   # no hint: the bigger cluster is the glass
            g = int((lab == 1).sum() >= (lab == 0).sum())
        else:
            g = int(((c[1] - h) ** 2).sum() < ((c[0] - h) ** 2).sum())
        axis = c[g] - c[1 - g]
        if float(axis @ axis) < 1e-6:
            return (0.0, 0.0, 0.0), 1.0, 1.0
        a = axis / (float(axis @ axis) * SOFT)
        mid = 0.5 * (c[0] + c[1])
    else:             # the brighter cluster, along brightness: the lit and the day quarters of an atlas both count
        a = np.array(LUMA) / (abs(lum[1] - lum[0]) * SOFT)
        mid = 0.5 * (c[0] + c[1])
    bias = 0.5 - float(mid @ a)
    m = np.clip(px @ a + bias, 0.0, 1.0)
    return tuple(float(x) for x in a), bias, float(m.mean())


def glass_colours(px, plane, bias, kind):
    """(average glass colour, the colour of the light it lets in: full value 1)."""
    import numpy as np
    m = np.clip(px @ np.array(plane) + bias, 0.0, 1.0)
    w = m.sum()
    avg = (px * m[:, None]).sum(axis=0) / w if w > 1e-6 else px.mean(axis=0)
    peak = max(float(avg.max()), 1e-3)
    hue = avg / peak
    keep = {STAINED: 1.0, FROSTED: 0.5}.get(kind, 0.25)
    light = 1.0 - keep + keep * hue
    light = light / max(float(light.max()), 1e-3)
    return tuple(float(x) for x in avg), tuple(float(x) for x in light)


# --- WMO files -----------------------------------------------------------------------------------------------------
def parse_root_all(b):
    """MOHD group count, MOTX, MOMT (flags, shader, blend, texture 1)."""
    return wl.parse_root(b)


def parse_group_box(b):
    """MOGP flags and its box (WMO space)."""
    for tag, p, size in wl.chunks(b):
        if tag == "MOGP":
            flags, = struct.unpack_from("<I", b, p + 8)
            box = struct.unpack_from("<6f", b, p + 12)
            return flags, (box[:3], box[3:])
    return 0, ((0, 0, 0), (0, 0, 0))


def glass_materials(info, panes=False):
    """{material index: texture path} of the root's glass materials. For panes, not the windows painted into a wall
    texture: their triangles are the whole wall."""
    out = {}
    for i, m in enumerate(info["materials"]):
        t = wl.texture_name(info, m["texture"])
        c = classify(t) if t else (0, "")
        if c[0] and not (panes and c[1] & PAINTED):
            out[i] = t
    return out


EXTERIOR, INDOOR = 0x8, 0x2000
MIN_AREA = 0.15      # yd^2: smaller glass is trim
FLAT = 0.97          # cos: triangles of one pane face within ~14 degrees of each other
TILE = 6.0           # yd: a sheet of glass larger than 2 x TILE across is cut into TILE x TILE yd panes
TWIN = 1.2           # yd: an interior face this close to an exterior one, facing the other way, is the same window
ROOM_PROBE = 1.0     # yd behind an exterior pane: the interior group holding this point is its room
SKY = 0.7071         # |normal z| above this: a skylight (its rectangle along x and y)


def pane_axes(n):
    """Along the pane (horizontal; x for a skylight) and across it (up the pane; y for a skylight)."""
    if abs(n[2]) > SKY:
        u = wl.unit((1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0))
        u = wl.unit(wl.sub(u, wl.mul(n, wl.dot(u, n))))
    else:
        u = wl.unit(wl.cross((0.0, 0.0, 1.0), n))
    return u, wl.cross(n, u)


class Pane:
    __slots__ = ("center", "normal", "area", "hw", "hh", "u", "w", "texture", "group", "exterior", "out", "room")

    def __init__(self, center, normal, area, hw, hh, u, w, texture, group, exterior):
        self.center, self.normal, self.area, self.hw, self.hh = center, normal, area, hw, hh
        self.u, self.w, self.texture, self.group, self.exterior = u, w, texture, group, exterior
        self.out = normal if exterior else None
        self.room = None


def group_panes(g, gi, materials, exterior):
    """The glass panes of one group: connected triangles of glass facing within FLAT of each other."""
    v, idx = g["vertices"], g["indices"]
    tris = []
    for start, count, _mn, _mx, _fl, material in g["batches"]:
        if material in materials:
            for k in range(start, start + count - 2, 3):
                i = idx[k:k + 3]
                if len(i) == 3 and max(i) < len(v):
                    n = wl.cross(wl.sub(v[i[1]], v[i[0]]), wl.sub(v[i[2]], v[i[0]]))
                    a = 0.5 * wl.length(n)
                    if a > 1e-6:
                        tris.append((i, materials[material], wl.unit(n), a))
    parent = list(range(len(tris)))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    by_vertex = collections.defaultdict(list)
    for k, t in enumerate(tris):
        for x in t[0]:
            by_vertex[tuple(round(c, 3) for c in v[x])].append(k)
    for ks in by_vertex.values():
        for a in ks[1:]:
            if wl.dot(tris[a][2], tris[ks[0]][2]) > FLAT:
                ra, rb = find(a), find(ks[0])
                if ra != rb:
                    parent[ra] = rb
    pieces = collections.defaultdict(list)
    for k in range(len(tris)):
        pieces[find(k)].append(k)

    def centroid(k):
        i = tris[k][0]
        return wl.mul(wl.add(wl.add(v[i[0]], v[i[1]]), v[i[2]]), 1.0 / 3.0)

    def make(ks):
        area = sum(tris[k][3] for k in ks)
        if area < MIN_AREA:
            return None
        n = wl.unit(wl.vsum(wl.mul(tris[k][2], tris[k][3]) for k in ks))
        if wl.length(n) < 0.5:
            return None
        u, w = pane_axes(n)
        pts = [v[x] for k in ks for x in tris[k][0]]
        us, ws = [wl.dot(p, u) for p in pts], [wl.dot(p, w) for p in pts]
        c = wl.mul(wl.vsum(wl.mul(centroid(k), tris[k][3]) for k in ks), 1.0 / area)
        mu, mw = 0.5 * (min(us) + max(us)), 0.5 * (min(ws) + max(ws))
        # the rectangle's middle on the pane's plane through its centroid
        mid = wl.add(c, wl.add(wl.mul(u, mu - wl.dot(c, u)), wl.mul(w, mw - wl.dot(c, w))))
        texture = collections.Counter()
        for k in ks:
            texture[tris[k][1]] += tris[k][3]
        return Pane(mid, n, area, 0.5 * (max(us) - min(us)), 0.5 * (max(ws) - min(ws)), u, w,
                    texture.most_common(1)[0][0], gi, exterior)

    out = []
    for ks in pieces.values():
        p = make(ks)
        if p is None:
            continue
        if p.hw <= TILE and p.hh <= TILE:
            out.append(p)
            continue
        tiles = collections.defaultdict(list)
        for k in ks:
            c = centroid(k)
            tiles[(math.floor(wl.dot(c, p.u) / TILE), math.floor(wl.dot(c, p.w) / TILE))].append(k)
        for part in tiles.values():
            q = make(part)
            if q is not None:
                out.append(q)
    return out


def inside_box(p, box, margin=0.0):
    lo, hi = box
    return all(lo[i] - margin <= p[i] <= hi[i] + margin for i in range(3))


def box_volume(box):
    lo, hi = box
    return max(0.0, hi[0] - lo[0]) * max(0.0, hi[1] - lo[1]) * max(0.0, hi[2] - lo[2])


def wmo_panes(scan, wmo):
    """The WMO's glass panes, each with its way out and its room (WMO space), or [] (no glass or groups missing)."""
    rp = flat(scan, wmo)
    if not rp.exists():
        return [], "root missing"
    info = parse_root_all(rp.read_bytes())
    mats = glass_materials(info, panes=True)
    if not mats:
        return [], "no glass"
    groups, panes, tris = [], [], []
    missing = 0
    for gi in range(info["groups"]):
        gp = flat(scan, "%s_%03d.wmo" % (wmo[:-4], gi))
        if not gp.exists():
            groups.append(None)
            missing += 1
            continue
        b = gp.read_bytes()
        flags, box = parse_group_box(b)
        g = wl.parse_group(b)
        groups.append((flags, box))
        panes += group_panes(g, gi, mats, bool(flags & EXTERIOR) and not (flags & INDOOR))
        tris += wl.triangles(g)
    if missing == info["groups"]:
        return [], "groups missing"
    ext = [p for p in panes if p.exterior]
    grid = collections.defaultdict(list)
    for p in ext:
        grid[tuple(int(math.floor(c / 4.0)) for c in p.center)].append(p)

    def twin(p):
        cx = [int(math.floor(c / 4.0)) for c in p.center]
        best, bd = None, TWIN
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for q in grid.get((cx[0] + dx, cx[1] + dy, cx[2] + dz), ()):
                        d = wl.length(wl.sub(q.center, p.center))
                        if d < bd and wl.dot(q.normal, p.normal) < -0.7:
                            best, bd = q, d
        return best

    def interior_room(point):
        best = None
        for gi, gb in enumerate(groups):
            if gb and not (gb[0] & EXTERIOR) and inside_box(point, gb[1], 0.25):
                if best is None or box_volume(gb[1]) < box_volume(groups[best][1]):
                    best = gi
        return best

    occluder = None
    kept = []
    taken = set()
    for p in panes:
        if p.exterior:
            continue
        q = twin(p)
        if q is not None:
            # the window seen from inside: one pane, out as the exterior face says, lighting this group
            q.room = p.group
            taken.add(id(q))
            continue
        # an interior face with no exterior twin: which way is out?
        if occluder is None:
            occluder = wl.Occluder(tris)
        probe = wl.Pane(p.center, p.normal, p.area, [], p.texture)
        front, back = wl.open_rays(occluder, probe, p.normal), wl.open_rays(occluder, probe, wl.mul(p.normal, -1.0))
        if front >= 7 and back <= 2:
            p.out = p.normal          # it faces out (Orgrimmar's huts: the whole city is interior groups)
        else:
            p.out = wl.mul(p.normal, -1.0)
        p.room = p.group
        kept.append(p)
    for p in ext:
        if p.room is None:
            p.room = interior_room(wl.sub(p.center, wl.mul(p.out, ROOM_PROBE)))
        if p.room is not None:
            kept.append(p)
    for p in kept:
        p.room = groups[p.room][1] if groups[p.room] else None
    return [p for p in kept if p.room is not None], ("%d groups missing" % missing if missing else "")


# --- the rooms behind the glass: who lives there ----------------------------------------------------------------------
# A building's class comes from its WMO's path (in a city's one big WMO, from each group's name or its district), its
# palette from its culture (the path) and its place (the area, the continent). A class and a palette make a style
# (RoomStyles.cdbc); every placement of a WMO with glass gets one and a seed (GlassBuildings.cdbc). features/Glass's
# shader then picks, per window, the layout (by the style's weights), whether the room is dark, its light and the rest
# from a hash of the window's cell and the seed.
OTHER, HOUSE, FARM, INN, SHOP, TEMPLE, KEEP, TOWER, CITY, SHIP, FORGE, COMMONS, QUARTER, CAVERN = range(14)
CLASS_NAMES = ["other", "house", "farm", "inn", "shop", "temple", "keep", "tower", "city", "ship",
               "forge", "commons", "quarter", "cavern"]
SHELVES, CURTAINS, DIVIDER, PLAIN = range(4)


class Klass:
    """weights (shelves, curtains, divider, plain), brightness, rooms dark by day, the share of such buildings nobody
    lives in (only those the client doesn't light at night), late hours (its lights go out later; < 0 earlier), the
    most of its rooms dark at night."""
    def __init__(self, weights, brightness, dark, empty, late, cap):
        self.weights, self.brightness, self.dark, self.empty, self.late, self.cap = weights, brightness, dark, empty, late, cap


CLASSES = {
    HOUSE: Klass((0.25, 0.35, 0.15, 0.25), 1.0, 0.10, 0.08, 0.0, 1.0),
    FARM: Klass((0.15, 0.30, 0.10, 0.45), 0.95, 0.12, 0.10, -0.5, 1.0),   # early to bed
    INN: Klass((0.20, 0.30, 0.25, 0.25), 1.15, 0.04, 0.0, 2.5, 0.55),     # lit late, never mostly dark
    SHOP: Klass((0.55, 0.10, 0.15, 0.20), 1.1, 0.06, 0.03, -1.0, 1.0),    # shelves; closed early
    TEMPLE: Klass((0.10, 0.10, 0.20, 0.60), 1.05, 0.05, 0.0, 1.0, 0.7),   # candles
    KEEP: Klass((0.20, 0.10, 0.30, 0.40), 0.9, 0.12, 0.05, 1.5, 0.7),     # the watch
    TOWER: Klass((0.60, 0.15, 0.10, 0.15), 1.0, 0.10, 0.05, 2.0, 0.6),    # books; mages read late
    CITY: Klass((0.30, 0.30, 0.15, 0.25), 1.0, 0.08, 0.0, 0.5, 0.9),
    SHIP: Klass((0.15, 0.35, 0.10, 0.40), 0.9, 0.10, 0.0, 1.0, 0.7),
    OTHER: Klass((0.20, 0.20, 0.20, 0.40), 1.0, 0.10, 0.05, 0.0, 1.0),
    # Ironforge's parts (IRONFORGE_AREAS): a city that never fully sleeps.
    FORGE: Klass((0.55, 0.10, 0.15, 0.20), 1.1, 0.02, 0.0, 4.0, 0.04),     # the Great Forge, the smithies: never sleep
    COMMONS: Klass((0.45, 0.15, 0.15, 0.25), 1.05, 0.04, 0.0, 2.5, 0.25),  # shops, the bank, the inn, the halls: mostly lit
    QUARTER: Klass((0.25, 0.35, 0.15, 0.25), 1.0, 0.08, 0.0, 1.0, 0.45),   # the wards' homes: late, and gently
    CAVERN: Klass((0.20, 0.25, 0.20, 0.35), 0.8, 0.25, 0.0, -1.0, 0.9),    # the Forlorn Cavern: darker, and earlier
}


class Palette:
    """The rooms' tints by day (two ends: a building's hash picks between them), the lamp at night, the curtains'
    cloth, a brightness, how far buildings differ, the layouts' weights x these, more rooms dark by day."""
    def __init__(self, name, tint, tint2, lamp, curtain, brightness=1.0, spread=0.15, weights=(1, 1, 1, 1), dark=0.0):
        self.name, self.tint, self.tint2, self.lamp, self.curtain = name, tint, tint2, lamp, curtain
        self.brightness, self.spread, self.weights, self.dark = brightness, spread, weights, dark


def kelvin_rgb(k):
    """A lamp's colour at k kelvin (Tanner Helland's fit), full value 1."""
    t = k / 100.0
    r = 1.0 if t <= 66 else min(1.0, 1.29293618606 * (t - 60) ** -0.1332047592)
    g = (0.39008157876 * math.log(t) - 0.63184144378) if t <= 66 else 1.12989086089 * (t - 60) ** -0.0755148492
    b = 1.0 if t >= 66 else (0.0 if t <= 19 else 0.54320678911 * math.log(t - 10) - 1.19625408914)
    c = [max(0.0, min(1.0, x)) for x in (r, g, b)]
    m = max(c)
    return tuple(round(x / m, 3) for x in c)


HUMAN, DUSK, NORTHERN, SCARLET, ELVEN, DALARAN, NIGHTELF, DRAENEI, VRYKUL, GOBLIN, FORSAKEN, ORC, DWARF, HIDE, \
    HELLFIRE, SPORE, ARCANE, CABIN = range(18)
PALETTES = {
    HUMAN: Palette("human: warm", (0.86, 0.71, 0.53), (0.78, 0.70, 0.60), kelvin_rgb(2700), (0.55, 0.20, 0.16)),
    DUSK: Palette("Duskwood: dim, grey", (0.66, 0.60, 0.52), (0.58, 0.58, 0.60), kelvin_rgb(2400), (0.30, 0.28, 0.36),
                  brightness=0.72, dark=0.10),
    NORTHERN: Palette("Northrend human: colder daylight", (0.80, 0.78, 0.76), (0.70, 0.74, 0.82), kelvin_rgb(2800),
                      (0.25, 0.32, 0.50), brightness=0.95),
    SCARLET: Palette("Scarlet Crusade: stone, crimson", (0.84, 0.74, 0.62), (0.80, 0.78, 0.74), kelvin_rgb(2900),
                     (0.62, 0.12, 0.10)),
    ELVEN: Palette("blood elf: gold and crimson, drapes", (0.95, 0.78, 0.48), (0.86, 0.50, 0.42), (1.0, 0.78, 0.38),
                   (0.70, 0.16, 0.14), brightness=1.1, weights=(1, 1.8, 1, 0.8)),
    DALARAN: Palette("Dalaran: violet, drapes and books", (0.74, 0.62, 0.92), (0.88, 0.80, 0.96), (0.92, 0.80, 1.0),
                     (0.42, 0.22, 0.62), brightness=1.1, weights=(1.3, 1.6, 1, 0.8)),
    NIGHTELF: Palette("night elf: moonlit violet", (0.70, 0.70, 0.92), (0.62, 0.78, 0.80), (0.78, 0.90, 1.0),
                      (0.36, 0.30, 0.56), weights=(1, 1.4, 1, 1)),
    DRAENEI: Palette("draenei: rose white and blue", (0.86, 0.80, 0.92), (0.72, 0.82, 0.95), (1.0, 0.80, 0.76),
                     (0.42, 0.34, 0.66)),
    VRYKUL: Palette("vrykul: firelit wood and furs", (0.72, 0.50, 0.32), (0.62, 0.48, 0.38), kelvin_rgb(1900),
                    (0.48, 0.38, 0.28), brightness=0.8, weights=(0.5, 1.6, 1, 1)),
    GOBLIN: Palette("goblin: cluttered and warm", (0.92, 0.72, 0.44), (0.82, 0.76, 0.56), kelvin_rgb(2500),
                    (0.30, 0.50, 0.30), brightness=1.1, weights=(2.2, 0.8, 1, 0.6)),
    FORSAKEN: Palette("Forsaken: dim green", (0.55, 0.66, 0.50), (0.50, 0.56, 0.52), (0.62, 0.95, 0.55),
                      (0.30, 0.28, 0.24), brightness=0.6, dark=0.15),
    ORC: Palette("orc and troll: firelight, hides", (0.86, 0.60, 0.40), (0.74, 0.56, 0.44), kelvin_rgb(2300),
                 (0.56, 0.42, 0.30), weights=(1, 1.3, 1, 1)),
    DWARF: Palette("dwarf and gnome: forge-warm", (0.90, 0.66, 0.42), (0.80, 0.70, 0.58), kelvin_rgb(2400),
                   (0.24, 0.30, 0.52), weights=(1.4, 1, 1, 1)),
    HIDE: Palette("tauren, tuskarr, furbolg: hides and fire", (0.78, 0.60, 0.42), (0.70, 0.62, 0.50), kelvin_rgb(2100),
                  (0.60, 0.48, 0.34), brightness=0.9, weights=(0.6, 1.5, 1, 1)),
    HELLFIRE: Palette("Outland outposts: ruddy", (0.88, 0.64, 0.50), (0.80, 0.60, 0.56), kelvin_rgb(2600),
                      (0.50, 0.22, 0.18), brightness=0.95),
    SPORE: Palette("Zangarmarsh: glowing teal", (0.56, 0.80, 0.70), (0.62, 0.72, 0.86), (0.60, 1.0, 0.85),
                   (0.40, 0.30, 0.50), brightness=0.9),
    ARCANE: Palette("Netherstorm and the Lost Ones: arcane blue", (0.62, 0.64, 0.92), (0.80, 0.62, 0.92), (0.75, 0.80, 1.0),
                    (0.30, 0.26, 0.56)),
    CABIN: Palette("ships: cabins", (0.78, 0.58, 0.38), (0.70, 0.60, 0.48), kelvin_rgb(2400), (0.62, 0.56, 0.44),
                   brightness=0.9),
}

# A city's one big WMO: its buildings are its groups (and the shader's 14-yd blocks within them).
CITY_WMOS = {"stormwind.wmo", "ironforge.wmo", "darnassis.wmo", "ogrimmar.wmo", "silvermoon_city_01.wmo", "nd_dalaran.wmo",
             "bootybay.wmo", "ol_draenei_city.wmo", "undercity.wmo", "shattrath.wmo"}
# A city's districts (area names) -> the class of their buildings, for groups whose names don't say.
DISTRICTS = {
    "Trade District": SHOP, "Dwarven District": SHOP, "The Bazaar": SHOP, "The Royal Exchange": SHOP, "The Drag": SHOP,
    "Magus Commerce Exchange": SHOP, "Runeweaver Square": SHOP, "Valley of Strength": SHOP, "The Commons": SHOP,
    "Mage Quarter": TOWER, "The Violet Citadel": TOWER, "Cathedral Square": TEMPLE, "Court of the Sun": TEMPLE,
    "Valley of Wisdom": TEMPLE, "Valley of Spirits": TEMPLE, "Old Town": HOUSE, "The Canals": HOUSE, "Murder Row": HOUSE,
    "Walk of Elders": HOUSE, "The Eventide": HOUSE, "Stormwind Keep": KEEP, "Valley of Heroes": KEEP,
    "Farstriders' Square": KEEP, "Valley of Honor": KEEP,
}
# Ironforge: each glass group's class by the client's own sub-zone for it (WMOAreaTable.dbc's name for the root's wmoID
# and the group's MOGP ID), a group named for a smithy the forge's. "Ironforge" itself is the ring road round the Great
# Forge (the Commons, as Cataclysm names it: the shops, the bank, the Stonefire Tavern) and the gates' halls. Tinker
# Town, the Hall of Arms, Old Ironforge and the Hall of Mysteries have no glass.
IRONFORGE = "ironforge.wmo"
IRONFORGE_AREAS = {
    "The Great Forge": FORGE,
    "Ironforge": COMMONS, "The Commons": COMMONS, "Vault of Ironforge": COMMONS, "The High Seat": COMMONS,
    "Hall of Explorers": COMMONS, "The Library": COMMONS,
    "The Military Ward": QUARTER, "The Mystic Ward": QUARTER, "Tinker Town": QUARTER, "Hall of Arms": QUARTER,
    "Hall of Mysteries": QUARTER, "Old Ironforge": QUARTER,
    "The Forlorn Cavern": CAVERN,
}
IRONFORGE_SMITHY = ("smith",)
# Underground: no sky behind the glass of their interior groups (the Undercity has no glass).
UNDERGROUND_WMOS = {"ironforge.wmo", "subway.wmo"}


def ironforge_class(group_name, subzone):
    """An Ironforge glass group's class: a smithy the forge's, else its sub-zone's (the Commons when unknown)."""
    if any(w in (group_name or "").lower() for w in IRONFORGE_SMITHY):
        return FORGE
    return IRONFORGE_AREAS.get(subzone, COMMONS)


def is_dungeon(wmo):
    """A dungeon's WMO (its path): its frosted windows keep the client's look."""
    return BS + "dungeon" + BS in wmo.lower().replace("/", BS)


def lit_frosted(info):
    """The frosted glass materials the client lights: SIDN with a colour, or unlit (MOMT 0x1: at full day and night)."""
    out = {}
    for i, m in enumerate(info["materials"]):
        t = wl.texture_name(info, m["texture"])
        if t and classify(t)[0] == FROSTED and ((m["flags"] & wl.SIDN and m["sidn"] & 0xFFFFFF) or m["flags"] & 0x1):
            out[i] = t
    return out


def read_wmo_areas(scan):
    """{(wmoID, MOGP group ID): sub-zone name} from <scan>/WMOAreaTable.dbc (name set 0), or {} without it."""
    p = scan / "WMOAreaTable.dbc"
    if not p.exists():
        return {}
    b = p.read_bytes()
    n, f, rs, _ss = struct.unpack_from("<4I", b, 4)
    strings = 20 + n * rs
    out = {}
    for i in range(n):
        r = struct.unpack_from("<%dI" % f, b, 20 + i * rs)
        if r[2] == 0 and r[11]:
            out[(r[1], r[3])] = b[strings + r[11]:b.index(b"\0", strings + r[11])].decode("utf-8", "replace")
    return out


def root_wmo_id(b):
    """MOHD's wmoID (WMOAreaTable's WMOID)."""
    for tag, p, size in wl.chunks(b):
        if tag == "MOHD":
            return struct.unpack_from("<I", b, p + 32)[0] if size >= 36 else 0
    return 0


def group_id(b):
    """MOGP's ID (WMOAreaTable's WMOGroupID), 0 when the header is short."""
    for tag, p, size in wl.chunks(b):
        if tag == "MOGP":
            return struct.unpack_from("<I", b, p + 56)[0] if size >= 60 else 0
    return 0


# Class by words in a path's file name (or a group's name), in this order. The first match wins.
CLASS_WORDS = [
    (SHIP, ("transport", "pirateship", "zeppelin", "_ship", "ship_")),
    (INN, ()),   # INN_WORDS
    (TEMPLE, ("chapel", "abbey", "cathedral", "cath", "temple", "church", "monast", "monest", "shrine", "sanctum", "naaru")),
    (SHOP, ("blacksmith", "smith", "shop", "bank", "auction", "market", "apothecary", "lab", "trade", "merchant",
            "financ", "jewel", "alchemy", "tailor", "enchant", "skinning", "exchange", "winecheese", "store", "medical",
            "engineering")),
    (TOWER, ("magetower", "mage", "library", "druidtower", "draenitower", "lo_tower", "spire", "citadel", "khzn", "karazhan",
             "portalroom", "observatory")),
    (KEEP, ("barracks", "guardtower", "snowtower", "keep", "fort", "castle", "garrison", "townhall", "hall", "prison",
            "wall", "gate", "siege", "stronghold", "command", "si:7", "tower", "warrior")),
    (FARM, ("farm", "barn", "silo", "mill")),
    (HOUSE, ("house", "twostory", "hut", "dwelling", "hovel", "2story", "loghome", "loft", "longhouse", "cliffhang",
             "home", "cottage", "trapper", "bedroom", "room", "medium", "lodge")),
]
# An inn: "inn" as a word of its own or at a word's end (goldshireinn, wet_inn, WallInn01, innbedroom), not inside one
# (skinning, TowerInnards); a tavern, the Slaughtered Lamb, a pub.
INN_WORDS = re.compile(r"(?<![a-z])inn|inn(?![a-z])|tavern|lamb|(?<![a-z])pub(?![a-z])")


def words_class(name):
    """The class a name's words say (CLASS_WORDS), or None."""
    n = name.lower()
    for cls, words in CLASS_WORDS:
        if cls == INN:
            if INN_WORDS.search(n):
                return INN
            continue
        if any(w in n for w in words):
            return cls
    return None


def room_class(wmo):
    """A placement's class from its WMO's path."""
    path = wmo.lower().replace("/", BS)
    base = path.split(BS)[-1]
    if base in CITY_WMOS:
        return CITY
    if BS + "transports" + BS in path:
        return SHIP
    c = words_class(base[:-4] if base.endswith(".wmo") else base)
    if c is not None:
        return c
    return KEEP if BS + "dungeon" + BS in path else OTHER


def group_class(name, district, city_default=CITY):
    """A city group's class: its name's words, else its district's, else the city's."""
    c = words_class(name or "")
    if c is not None and c != SHIP:
        return c
    return DISTRICTS.get(district, city_default)


ELVEN_ZONES = {"Eversong Woods", "Silvermoon City", "Ghostlands", "Isle of Quel'Danas", "Netherstorm"}
NIGHTELF_ZONES = {"Teldrassil", "Darnassus", "Darkshore", "Ashenvale", "Feralas", "Moonglade", "Winterspring", "Felwood"}
DRAENEI_ZONES = {"Azuremyst Isle", "Bloodmyst Isle", "The Exodar", "Shattrath City", "Telredor"}
FORSAKEN_ZONES = {"Tirisfal Glades", "Silverpine Forest", "Undercity", "Tarren Mill", "Vengeance Landing", "New Agamand",
                  "Venomspite", "Brill", "The Sepulcher"}
GOBLIN_AREAS = {"Booty Bay", "Ratchet", "Gadgetzan", "Steamwheedle Port", "Mirage Raceway", "Everlook", "Venture Co. Base Camp",
                "Venture Bay", "Mudsprocket", "Toshley's Station", "Area 52", "Lost Rigger Cove", "Bouldercrag's Refuge"}
SCARLET_AREAS = {"Tyr's Hand", "Hearthglen", "New Hearthglen", "Scarlet Monastery", "Ruins of the Scarlet Enclave",
                 "Scarlet Point", "Onslaught Harbor", "Onslaught Base Camp"}
PALETTE_WORDS = [
    (VRYKUL, ("vrykul",)), (DALARAN, ("dalaran",)), (ELVEN, ("bloodelf", "highelf", "silvermoon", "sunwell", "bldelf")),
    (NIGHTELF, ("nightelf", "darnassis", "darnassus")), (DRAENEI, ("draenei", "draeni", "shattrath")),
    (GOBLIN, ("goblin", "bootybay", "venture", "gnome")), (FORSAKEN, ("forsaken", "undercity")),
    (SCARLET, ("scarlet", "newhearthglen", "onslaught")),
    (ORC, ("orc", "ogrimmar", "troll")), (HIDE, ("tauren", "tuskarr", "furbolg")),
    (DWARF, ("dwarf", "dwarven", "khazmodan", "ironforge")), (SPORE, ("spore", "zangarmushroom")),
    (ARCANE, ("nethercollector", "manaforge", "lostones")), (CABIN, ("transports", "pirateship")),
]


def palette_of(wmo, map_id, area_names=()):
    """A placement's palette: its culture by its WMO's path, else by its place (the area and its parents, the map)."""
    path = wmo.lower().replace("/", BS)
    for pal, words in PALETTE_WORDS:
        if any(w in path for w in words):
            return pal
    names = set(area_names)
    if names & GOBLIN_AREAS:
        return GOBLIN
    if names & SCARLET_AREAS:
        return SCARLET
    if "Duskwood" in names:
        return DUSK
    if names & FORSAKEN_ZONES:
        return FORSAKEN
    if names & ELVEN_ZONES:
        return ELVEN
    if names & NIGHTELF_ZONES:
        return NIGHTELF
    if names & DRAENEI_ZONES:
        return DRAENEI
    if map_id == 571:
        return NORTHERN
    if map_id == 530:
        return HELLFIRE
    return HUMAN


def style_values(cls, pal):
    """A style's columns (RoomStyles after ID, Class, Palette; without the Comment)."""
    k, p = CLASSES[cls], PALETTES[pal]
    weights = [round(w * m, 4) for w, m in zip(k.weights, p.weights)]
    return (*p.tint, *p.tint2, *p.lamp, *p.curtain, round(k.brightness * p.brightness, 4), p.spread,
            round(min(1.0, k.dark + p.dark), 4), *weights, k.late, k.cap)


def layout_thresholds(weights):
    """The cumulative thresholds the runtime makes of a style's weights (env/GlassModel LayoutThresholds)."""
    total = sum(max(0.0, w) for w in weights)
    out, run = [], 0.0
    for w in weights[:3]:
        run += max(0.0, w) / total if total > 0 else 0.0
        out.append(min(1.0, run))
    return out


LIGHTS_OUT = [(21.0, 0.08), (24.0, 0.5), (26.0, 0.72), (28.5, 0.78), (30.0, 0.6)]


def lights_out(hour, late=0.0, cap=1.0):
    """The share of rooms dark at night by the hour, as env/GlassModel's LightsOutShare (the runtime's)."""
    hour = hour % 24.0
    t = (hour + 24.0 if hour < 12.0 else hour) - late
    share = LIGHTS_OUT[0][1]
    if t >= LIGHTS_OUT[-1][0]:
        share = LIGHTS_OUT[-1][1]
    else:
        for (t0, s0), (t1, s1) in zip(LIGHTS_OUT, LIGHTS_OUT[1:]):
            if t0 <= t < t1:
                share = s0 + (s1 - s0) * (t - t0) / (t1 - t0)
                break
    return min(share, max(0.0, min(1.0, cap)))


def seed_of(map_id, uid, group=None):
    """A placement's (or a city group's) seed: steady across runs."""
    import zlib
    return zlib.crc32(("%d:%d" % (map_id, uid) + ("" if group is None else ":%d" % group)).encode("ascii")) & 0xFFFFFFFF


def dark_place(area_names, wmo):
    """Ruined, abandoned, haunted (gen_window_lights' DARK_PATHS / DARK_AREAS, as it reads them): no lit rooms."""
    path = wmo.lower()
    if any(w in path for w in wl.DARK_PATHS):
        return True
    names = list(area_names)
    return bool(names) and (names[0] in wl.DARK_AREAS or (names[0] not in wl.TOWNS and any(n in wl.DARK_AREAS for n in names)))


def empty_building(seed, cls, lived_in, dark):
    """Nobody lives here (every room dark): never where the client lights the windows at night, an inn, a temple or a
    city; else the class's share of its buildings (three times as many in a dark place)."""
    if lived_in or cls in (INN, TEMPLE, CITY, SHIP):
        return False
    share = CLASSES[cls].empty * (3.0 if dark else 1.0)
    return (seed % 1000) < share * 1000.0


# Flags (GlassBuildings.Flags)
B_CITY, B_DARK_PLACE, B_EMPTY, B_SELF_LIT, B_UNDERGROUND, B_DUNGEON = 0x1, 0x2, 0x4, 0x8, 0x10, 0x20
WHOLE = 0xFFFFFFFF
# Flags (GlassPanes.Flags)
PANE_NO_SKY = 0x1   # no sky behind it (an underground WMO's interior group): no floor light, no shafts


def group_infos(b):
    """MOGI: per group (flags, box min, box max, name) of a root's bytes."""
    mogn, mogi = b"", []
    for tag, p, size in wl.chunks(b):
        if tag == "MOGN":
            mogn = b[p:p + size]
        elif tag == "MOGI":
            for i in range(size // 32):
                fl = struct.unpack_from("<I", b, p + 32 * i)[0]
                box = struct.unpack_from("<6f", b, p + 32 * i + 4)
                nm = struct.unpack_from("<i", b, p + 32 * i + 28)[0]
                mogi.append((fl, box[:3], box[3:], nm))
    out = []
    for fl, lo, hi, nm in mogi:
        name = mogn[nm:mogn.index(b"\0", nm)].decode("latin1") if 0 <= nm < len(mogn) and b"\0" in mogn[nm:] else ""
        out.append((fl, lo, hi, name))
    return out


def area_chain(areas, area):
    names, a = [], area
    while a and a in areas and len(names) < 6:
        names.append(areas[a][0])
        a = areas[a][1]
    return names


def buildings_table(scan, areas=None, amap=None):
    """(styles, buildings, report): RoomStyles rows and GlassBuildings rows over every placement of a WMO with glass,
    a city WMO's groups with glass, and one default per WMO and map."""
    if areas is None:
        p = scan / "AreaTable.dbc"
        areas = wl.read_areas(p) if p.exists() else {}
    if amap is None and (scan / "terrain-scan").exists():
        amap = wl.AreaMap(scan)
    styles, rows = {}, []
    report = collections.Counter()
    per_wmo = {}
    wmo_areas = read_wmo_areas(scan)

    # Styles in the order they're first used, those of the classes added later (Ironforge's) after all the others, so
    # the earlier styles keep their IDs: style_id gives a key, the IDs are made at the end.
    def style_id(cls, pal):
        if (cls, pal) not in styles:
            styles[(cls, pal)] = len(styles)
        return (cls, pal)

    def wmo_info(wmo):
        if wmo not in per_wmo:
            rp = flat(scan, wmo)
            info, glass_groups, groups, lit, subzones = None, set(), [], False, {}
            if rp.exists():
                b = rp.read_bytes()
                info = parse_root_all(b)
                mats = glass_materials(info)
                if mats:
                    # lit: the client lights its windows at night (SIDN), or draws its frosted glass at full (unlit)
                    lit = bool(wl.window_materials(info)) or bool(lit_frosted(info))
                    groups = group_infos(b)
                    wid = root_wmo_id(b)
                    if wmo.split(BS)[-1] in CITY_WMOS:
                        for gi in range(info["groups"]):
                            gp = flat(scan, "%s_%03d.wmo" % (wmo[:-4], gi))
                            if not gp.exists():
                                continue
                            gb = gp.read_bytes()
                            if any(bt[5] in mats for bt in wl.parse_group(gb)["batches"]):
                                glass_groups.add(gi)
                                subzones[gi] = wmo_areas.get((wid, group_id(gb)), "")
                else:
                    info = None
            per_wmo[wmo] = (info, sorted(glass_groups), groups, lit, subzones)
        return per_wmo[wmo]

    town_area = {}
    for wmo, (town, mid) in wl.WMO_TOWNS.items():
        town_area[wmo] = next((a for a, v in areas.items() if v[0] == town and v[3] == mid), 0)
    defaults = set()
    for (mapdir, uid_s), (map_id, md, wmo, pos, rot) in sorted(placements(scan).items(), key=lambda kv: (kv[1][0], int(kv[0][1]))):
        info, glass_groups, groups, lit, subzones = wmo_info(wmo)
        if info is None:
            continue
        uid = int(uid_s)
        point, _direction = wl.placement(pos, rot)
        o = point((0.0, 0.0, 0.0))
        base = wmo.split(BS)[-1]

        def area_at(x, y):
            if base in town_area and town_area[base]:
                return town_area[base]
            return amap.at(md, x, y) if amap is not None and map_id in (0, 1, 530, 571) else 0
        area = area_at(o[0], o[1])
        names = area_chain(areas, area)
        cls, pal = room_class(wmo), palette_of(wmo, map_id, names)
        dark = dark_place(names, wmo)
        seed = seed_of(map_id, uid)
        # (a dungeon's; underground as a whole when it isn't a city: a city's groups say which are)
        where = (B_DUNGEON if is_dungeon(wmo) else 0) | (B_UNDERGROUND if base in UNDERGROUND_WMOS and cls != CITY else 0)
        flags = (B_CITY if cls == CITY else 0) | (B_DARK_PLACE if dark else 0) | (B_SELF_LIT if lit else 0) | where
        if empty_building(seed, cls, lit, dark):
            flags |= B_EMPTY
        rows.append([0, map_id, uid, *o, WHOLE, style_id(cls, pal), seed, flags, area, wmo,
                     "%s (%s, %s): %s" % (base[:-4], CLASS_NAMES[cls], PALETTES[pal].name.split(":")[0], names[0] if names else "?")])
        report[("placement", cls)] += 1
        report[("palette", pal)] += 1
        if flags & B_EMPTY:
            report["empty"] += 1
        if dark:
            report["dark place"] += 1
        # A city's groups with glass: each its own building, by its name or its district.
        for gi in glass_groups:
            fl, lo, hi, gname = groups[gi] if gi < len(groups) else (0, (0, 0, 0), (0, 0, 0), "")
            c = point(tuple(0.5 * (a + b) for a, b in zip(lo, hi)))
            garea = area_at(c[0], c[1])
            gnames = area_chain(areas, garea)
            subzone = subzones.get(gi, "")
            if base == IRONFORGE and wmo_areas:
                gcls = ironforge_class(gname, subzone)
            else:
                gcls = group_class(gname, gnames[0] if gnames else "", CITY)
            gpal = palette_of(wmo, map_id, gnames or names)
            gdark = dark_place(gnames or names, wmo)
            gseed = seed_of(map_id, uid, gi)
            under = base in UNDERGROUND_WMOS and (fl & INDOOR) and not (fl & EXTERIOR)
            gflags = B_CITY | (B_DARK_PLACE if gdark else 0) | (B_SELF_LIT if lit else 0) | (B_UNDERGROUND if under else 0) | \
                (B_DUNGEON if is_dungeon(wmo) else 0)
            rows.append([0, map_id, uid, *o, gi, style_id(gcls, gpal), gseed, gflags, garea, wmo,
                         "%s group %d %s (%s%s): %s" % (base[:-4], gi, gname or "-", CLASS_NAMES[gcls],
                                                       ", " + subzone if subzone else "", gnames[0] if gnames else "?")])
            report[("group", gcls)] += 1
        # One default per WMO and map, for its instances not placed in the map (transports) or not found.
        if (map_id, wmo) not in defaults:
            defaults.add((map_id, wmo))
            dpal = palette_of(wmo, map_id)
            rows.append([0, map_id, 0, 0.0, 0.0, 0.0, WHOLE, style_id(cls, dpal), seed_of(map_id, 0, abs(hash_path(wmo)) % 100000),
                         (B_CITY if cls == CITY else 0) | (B_SELF_LIT if lit else 0) | where, 0, wmo,
                         "%s: the default (%s)" % (base[:-4], CLASS_NAMES[cls])])
    rows.sort(key=lambda r: (r[2] == 0, r[1], r[2], r[6] if r[6] != WHOLE else -1, r[11]))
    for i, r in enumerate(rows):
        r[0] = i + 1
    # The styles' IDs: first used first, Ironforge's classes after the rest.
    ids = {k: i + 1 for i, k in enumerate(sorted(styles, key=lambda k: (k[0] >= FORGE, styles[k])))}
    for r in rows:
        r[7] = ids[r[7]]
    style_rows = []
    for (cls, pal), sid in sorted(ids.items(), key=lambda kv: kv[1]):
        style_rows.append([sid, cls, pal, *style_values(cls, pal), "%s, %s" % (CLASS_NAMES[cls], PALETTES[pal].name)])
    return style_rows, rows, report


def hash_path(wmo):
    """PathHash (env/GlassModel): FNV-1a of the lower-case backslashed path."""
    h = 2166136261
    for ch in wmo.lower().replace("/", BS).encode("latin1"):
        h = ((h ^ ch) * 16777619) & 0xFFFFFFFF
    return h if wmo else 0


# --- the tables ----------------------------------------------------------------------------------------------------
GLASS_MATERIALS = ["u", "s", "u", "u", "f", "f", "f", "f", "f", "f", "f", "f", "f", "f", "f", "s"]
#  ID Texture Kind Flags ColorR ColorG ColorB LightR LightG LightB MaskR MaskG MaskB MaskBias Share Comment
GLASS_PANES = ["u", "u", "u", "f", "f", "f", "f", "f", "f", "f", "f", "f", "f", "f",
               "f", "f", "f", "u", "f", "f", "f", "f", "f", "f", "u", "s"]
#  ID MapID WmoID PosX PosY PosZ OutX OutY OutZ AlongX AlongY AlongZ HalfWidth HalfHeight
#  LightR LightG LightB Kind RoomMinX RoomMinY RoomMinZ RoomMaxX RoomMaxY RoomMaxZ Flags Comment
ROOM_STYLES = ["u", "u", "u"] + ["f"] * 21 + ["s"]
#  ID Class Palette TintR TintG TintB Tint2R Tint2G Tint2B LampR LampG LampB CurtainR CurtainG CurtainB
#  Brightness Spread DarkShare Shelves Curtains Divider Plain LateHours NightCap Comment
GLASS_BUILDINGS = ["u", "u", "u", "f", "f", "f", "u", "u", "u", "u", "u", "s", "s"]
#  ID MapID UniqueID PosX PosY PosZ Group Style Seed Flags AreaID Wmo Comment


def flat(scan, name):
    return scan / "wmo-windows" / name.replace(BS, "#")


def placements(scan):
    """uniqueId -> (map id, map dir, wmo, pos, rot), every map the scan has."""
    ids = {}
    with open(scan / "maps.tsv", newline="") as f:
        for row in csv.reader(f, delimiter="\t"):
            if len(row) >= 2:
                ids[row[1].lower()] = int(row[0])
    seen = {}
    with open(scan / "adt_wmos.tsv", newline="") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            mapdir = row["source"].lower().split(BS)[2]
            if mapdir not in ids or (mapdir, row["uniqueId"]) in seen:
                continue
            seen[(mapdir, row["uniqueId"])] = (ids[mapdir], mapdir, row["wmo"].lower(),
                                               tuple(float(row[k]) for k in ("px", "py", "pz")),
                                               tuple(float(row[k]) for k in ("rx", "ry", "rz")))
    return seen


def all_roots(scan):
    return sorted({v[2] for v in placements(scan).values()})


def materials_table(scan, textures, sheets=None):
    """(rows, report) of GlassMaterials: every candidate texture of every root."""
    cands = collections.defaultdict(set)
    for wmo in all_roots(scan):
        rp = flat(scan, wmo)
        if not rp.exists():
            continue
        info = parse_root_all(rp.read_bytes())
        for m in info["materials"]:
            t = wl.texture_name(info, m["texture"])
            if t and is_candidate(t):
                cands[t].add(wmo.split(BS)[-1][:-4])
    rows, report = [], collections.Counter()
    skipped = []
    previews = []
    for t in sorted(cands, key=base_name):
        c = classify(t)
        if not c[0]:
            skipped.append((base_name(t), c[1]))
            continue
        kind, flags, mode, hint = c
        path = textures / t.replace(BS, "#")
        if not path.exists():
            skipped.append((base_name(t), "texture not extracted"))
            continue
        px = texels(path)
        plane, bias, share = mask_plane(px, mode, hint)
        avg, light = glass_colours(px, plane, bias, kind)
        report[kind] += 1
        rows.append([0, t, kind, flags, *avg, *light, *plane, bias, share,
                     "%s (%s): %s" % (base_name(t), KIND_NAMES[kind], ", ".join(sorted(cands[t])[:3]))])
        previews.append((path, plane, bias, base_name(t)))
    for i, r in enumerate(rows):
        r[0] = i + 1
    if sheets:
        contact_sheets(previews, Path(sheets))
    return rows, report, skipped


def contact_sheets(previews, out):
    """Each texture beside its glass (the mask's texels, the rest dark), for review."""
    from PIL import Image, ImageDraw
    import numpy as np
    out.mkdir(parents=True, exist_ok=True)
    T, COLS, ROWS = 128, 4, 5
    for s in range(0, len(previews), COLS * ROWS):
        sheet = Image.new("RGB", (COLS * 2 * T, ROWS * (T + 12)), (30, 30, 30))
        d = ImageDraw.Draw(sheet)
        for k, (path, plane, bias, name) in enumerate(previews[s:s + COLS * ROWS]):
            im = Image.open(path)
            im.load()
            rgb = np.asarray(im.convert("RGB").resize((T, T)), dtype=np.float64) / 255.0
            m = np.clip(rgb @ np.array(plane) + bias, 0, 1)[:, :, None]
            shown = rgb * (0.15 + 0.85 * m) + np.array([0.0, 0.25, 0.0]) * m * 0.3
            x, y = (k % COLS) * 2 * T, (k // COLS) * (T + 12)
            sheet.paste(Image.fromarray((rgb * 255).astype(np.uint8)), (x, y))
            sheet.paste(Image.fromarray((np.clip(shown, 0, 1) * 255).astype(np.uint8)), (x + T, y))
            d.text((x + 2, y + T), name[:34], fill=(255, 255, 0))
        sheet.save(out / ("masks%02d.png" % (s // (COLS * ROWS))))


def panes_table(scan, light_of):
    """(rows, report) of GlassPanes over every placement."""
    per_wmo, why_none = {}, collections.Counter()
    rows = []
    by_map = collections.Counter()
    wmos_with = set()
    for (mapdir_uid), (map_id, mapdir, wmo, pos, rot) in sorted(placements(scan).items(), key=lambda kv: (kv[1][0], int(kv[0][1]))):
        if wmo not in per_wmo:
            per_wmo[wmo], why = wmo_panes(scan, wmo)
            if why:
                why_none["some groups missing" if why[0].isdigit() else why] += 1
        panes = per_wmo[wmo]
        if not panes:
            continue
        point, direction = wl.placement(pos, rot)
        uid = int(mapdir_uid[1])
        wmos_with.add(wmo)
        for p in panes:
            c = point(p.center)
            out = wl.unit(direction(p.out))
            along = wl.unit(direction(p.u))
            # the room's box in world space: the box round its eight corners
            lo, hi = p.room
            corners = [point((x, y, z)) for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
            rlo = [min(q[i] for q in corners) for i in range(3)]
            rhi = [max(q[i] for q in corners) for i in range(3)]
            kind, light = light_of[p.texture]
            # an underground WMO's interior pane: no sky behind it (no floor light, no shafts)
            no_sky = PANE_NO_SKY if wmo.split(BS)[-1] in UNDERGROUND_WMOS and not p.exterior else 0
            rows.append([0, map_id, uid, *c, *out, *along, p.hw, p.hh, *light, kind, *rlo, *rhi, no_sky,
                         "%s: %s" % (wmo.split(BS)[-1][:-4], base_name(p.texture))])
            by_map[map_id] += 1
    rows.sort(key=lambda r: (r[1], math.floor(r[3] / 64), math.floor(r[4] / 64), r[3], r[4], r[5]))
    for i, r in enumerate(rows):
        r[0] = i + 1
    return rows, by_map, len(wmos_with), why_none


def group_list(scan):
    names = []
    for wmo in all_roots(scan):
        rp = flat(scan, wmo)
        if not rp.exists():
            continue
        info = parse_root_all(rp.read_bytes())
        if glass_materials(info):
            names += ["%s_%03d.wmo" % (wmo[:-4], g) for g in range(info["groups"])
                      if not flat(scan, "%s_%03d.wmo" % (wmo[:-4], g)).exists()]
    return sorted(set(names))


def texture_list(scan):
    out = set()
    for wmo in all_roots(scan):
        rp = flat(scan, wmo)
        if rp.exists():
            info = parse_root_all(rp.read_bytes())
            for m in info["materials"]:
                t = wl.texture_name(info, m["texture"])
                if t and is_candidate(t):
                    out.add(t)
    return sorted(out)


def write(path, data):
    old = path.read_bytes() if path.exists() else None
    if data == old:
        print(f"{path}: unchanged")
        return
    if old is not None and path.parent.name.lower() == "dbfilesclient":
        backup = io.backup_path(path)
        backup.write_bytes(old)
        print(f"{path}: backup {backup.name}")
    path.write_bytes(data)
    print(f"{path}: written")


def write_rooms(scan, folder):
    """RoomStyles.cdbc and GlassBuildings.cdbc, with a report: the placements per class and palette, the layouts'
    shares per class, the night's schedule."""
    srows, brows, report = buildings_table(scan)
    placed = sum(n for k, n in report.items() if isinstance(k, tuple) and k[0] == "placement")
    print(f"{len(srows)} room styles; {len(brows)} building rows: {placed} placements, "
          f"{sum(n for k, n in report.items() if isinstance(k, tuple) and k[0] == 'group')} city groups, "
          f"{sum(1 for r in brows if r[2] == 0)} WMO defaults; {report['empty']} empty, {report['dark place']} in dark places")
    print("  placements by class: " + ", ".join(f"{CLASS_NAMES[c]} {report[('placement', c)]}" for c in range(len(CLASS_NAMES))
                                                 if report[("placement", c)]))
    print("  city groups by class: " + ", ".join(f"{CLASS_NAMES[c]} {report[('group', c)]}" for c in range(len(CLASS_NAMES))
                                                 if report[("group", c)]))
    print("  placements by palette: " + ", ".join(f"{PALETTES[p].name.split(':')[0]} {report[('palette', p)]}"
                                                  for p in sorted(PALETTES) if report[("palette", p)]))
    ironforge = collections.Counter((CLASS_NAMES[next(s[1] for s in srows if s[0] == r[7])], r[12].split("(")[1].split(")")[0].split(", ", 1)[-1])
                                    for r in brows if r[11].split(BS)[-1] == IRONFORGE and r[6] != WHOLE)
    if ironforge:
        print("  Ironforge's glass groups (class, sub-zone): " + ", ".join("%s %s %d" % (c, z, n) for (c, z), n in sorted(ironforge.items())))
    print("  underground rows %d, dungeon rows %d" % (sum(1 for r in brows if r[9] & B_UNDERGROUND), sum(1 for r in brows if r[9] & B_DUNGEON)))
    for c in sorted(CLASSES):
        k = CLASSES[c]
        total = sum(k.weights)
        print("  %-6s layouts: shelves %2.0f%%, curtains %2.0f%%, divider %2.0f%%, plain %2.0f%%; dark at 22:00 %2.0f%%, "
              "00:00 %2.0f%%, 03:00 %2.0f%%, 05:30 %2.0f%%" % (CLASS_NAMES[c], *(100 * w / total for w in k.weights),
                                                              *(100 * lights_out(h, k.late, k.cap) for h in (22, 0, 3, 5.5))))
    write(folder / "RoomStyles.cdbc", io.build(ROOM_STYLES, [tuple(r) for r in srows]))
    write(folder / "GlassBuildings.cdbc", io.build(GLASS_BUILDINGS, [tuple(r) for r in brows]))


def main():
    argv, options = [], {}
    rest = sys.argv[1:]
    while rest:
        a = rest.pop(0)
        if a in ("--group-list", "--texture-list", "--sheets"):
            options[a] = rest.pop(0) if rest else sys.exit(f"{a} needs a path")
        elif a == "--rooms":
            options[a] = True
        else:
            argv.append(a)
    if not argv:
        sys.exit(__doc__)
    scan = Path(argv[0])
    for flag, fn in (("--group-list", group_list), ("--texture-list", texture_list)):
        if flag in options:
            out = Path(options[flag])
            names = fn(scan)
            out.write_text("".join(n + "\r\n" for n in names), newline="")
            print(f"{out}: {len(names)} files")
            return
    if "--rooms" in options:
        write_rooms(scan, Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parent.parent / "data")
        return
    if len(argv) < 2:
        sys.exit(__doc__)
    textures = Path(argv[1])
    folder = Path(argv[2]) if len(argv) > 2 else Path(__file__).resolve().parent.parent / "data"
    sheets = options.get("--sheets")
    mrows, by_kind, skipped = materials_table(scan, textures, sheets)
    print(f"{len(mrows)} glass textures: " + ", ".join(f"{by_kind[k]} {KIND_NAMES[k]}" for k in sorted(by_kind)))
    reasons = collections.Counter(r for _n, r in skipped)
    print(f"{len(skipped)} candidates left out: " + ", ".join(f"{n} {r}" for r, n in reasons.most_common()))
    for n, r in skipped:
        if r in ("unreviewed", "texture not extracted"):
            print(f"  {r}: {n}")
    # A pane's light: its texture's, x how much of the texture is glass (lead and frames let none through).
    light_of = {r[1]: (r[2], tuple(x * max(0.35, min(1.0, r[14])) for x in (r[7], r[8], r[9]))) for r in mrows}
    prows, by_map, wmos, why_none = panes_table(scan, light_of)
    print(f"{len(prows)} glass panes with a room behind them, in {wmos} WMOs:")
    for m, n in sorted(by_map.items(), key=lambda kv: -kv[1]):
        if m in CONTINENTS or n >= 50:
            print(f"  {n:6d}  map {m} {CONTINENTS.get(m, '')}")
    print("  instance and other maps: %d panes on %d maps" % (sum(n for m, n in by_map.items() if m not in CONTINENTS),
                                                              len([m for m in by_map if m not in CONTINENTS])))
    if why_none:
        print("WMOs without panes: " + ", ".join(f"{n} {w}" for w, n in why_none.most_common()))
    write(folder / "GlassMaterials.cdbc", io.build(GLASS_MATERIALS, [tuple(r) for r in mrows]))
    write(folder / "GlassPanes.cdbc", io.build(GLASS_PANES, [tuple(r) for r in prows]))
    write_rooms(scan, folder)


if __name__ == "__main__":
    main()
