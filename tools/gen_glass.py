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

Inputs (the map scan in waterfall-scan, see its README; nothing here opens an MPQ):
  <scan>/adt_wmos.tsv, <scan>/maps.tsv
  <scan>/wmo-windows/     WMO roots and groups (waterscan --extract-list <Data> <list> <scan>/wmo-windows)
  <textures>/             the candidate textures (waterscan --extract-list <Data> <this tool's --texture-list>)
Usage:
  python gen_glass.py <scan> --texture-list <out.txt>       the candidate textures to extract
  python gen_glass.py <scan> --group-list <out.txt>         the group files of WMOs with glass not extracted yet
  python gen_glass.py <scan> <textures> [data dir] [--sheets <dir>]
        writes GlassMaterials.cdbc and GlassPanes.cdbc (--sheets: contact sheets of each texture and its mask)
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


# --- the tables ----------------------------------------------------------------------------------------------------
GLASS_MATERIALS = ["u", "s", "u", "u", "f", "f", "f", "f", "f", "f", "f", "f", "f", "f", "f", "s"]
#  ID Texture Kind Flags ColorR ColorG ColorB LightR LightG LightB MaskR MaskG MaskB MaskBias Share Comment
GLASS_PANES = ["u", "u", "u", "f", "f", "f", "f", "f", "f", "f", "f", "f", "f", "f",
               "f", "f", "f", "u", "f", "f", "f", "f", "f", "f", "u", "s"]
#  ID MapID WmoID PosX PosY PosZ OutX OutY OutZ AlongX AlongY AlongZ HalfWidth HalfHeight
#  LightR LightG LightB Kind RoomMinX RoomMinY RoomMinZ RoomMaxX RoomMaxY RoomMaxZ Flags Comment


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
            rows.append([0, map_id, uid, *c, *out, *along, p.hw, p.hh, *light, kind, *rlo, *rhi, 0,
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


def main():
    argv, options = [], {}
    rest = sys.argv[1:]
    while rest:
        a = rest.pop(0)
        if a in ("--group-list", "--texture-list", "--sheets"):
            options[a] = rest.pop(0) if rest else sys.exit(f"{a} needs a path")
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


if __name__ == "__main__":
    main()
