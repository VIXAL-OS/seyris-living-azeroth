#!/usr/bin/env python3
"""Window lights: warm light spilling out of the lit windows of inhabited buildings at night, as data
(data/WindowLights.cdbc, read by features/Windows for env/Lights and features/WindowGlow; docs/cdbc-tables.md).

Why: "warm light spilling out of inhabited buildings' windows at night" (the user, 2026-10-17). The client already
fades a self-lit window material in at night, but it lights nothing around it.

How a window is found (3.3.5a WMO files):
- Its material (MOMT) has the SIDN flag (0x10, "self-illuminated day/night": the client fades its emissive colour
  in at night, CMapObj material update 0x7A8520), a non-black emissive colour, and a window texture (window, wnd,
  stained, glass; not lamp glass or a shop sign). Burnt and abandoned window textures (abndn, burn) are left out.
- Its triangles: the batches (MOBA) drawn with that material. The panes are the connected pieces facing one way;
  glass facing every way at once (a glazed box), trim under MIN_AREA and skylights (facing more than 45 degrees up
  or down) are not windows, and glass too big for one light (a tower's band of windows) is cut into TILE_SIZE tiles.
- Which way is out: in exterior groups (MOGP flag 0x8) the winding, which agrees with the vertex normals (MONR) on
  every pane checked; a WMO's interior groups then hold the same windows seen from inside and are ignored. A WMO
  with windows only in interior groups (Orgrimmar's huts, the whole city one WMO) faces each pane where OPEN_RAYS
  get away while the other side is closed (orient()).
- Neighbouring panes on one facade merge into one light (at most MAX_WIDTH wide and MAX_HEIGHT tall), so a row of
  windows is one light, not one per pane: the light budget is 16 per 16-yd cell (lights panel), and busy cells in
  Stormwind, Booty Bay and the Undercity were already full before windows. thin() then keeps every 16-yd cell to
  CELL_CAP window lights, the biggest windows first; the others only glow (GLOW_ONLY).
- The light sits OUT yards in front of the window's middle, a spot facing out (and tilted down at run time) so it
  lights the ground and the walls in front, not the room behind. Its reach and strength grow with the glass's area.

Which buildings: the four continents only (no instance maps), and only where the window stands in an inhabited
place: an area that is a town or city by AreaTable.dbc's own flags (town 0x200000, capital 0x100, city part 0x8,
rest zones 0x400000/0x800000; a zone's flags don't make every farm in it a town), a part of a capital, or one of
TOWNS below (lived-in places the flags miss, hostile towns that are lived in). Never in DARK_AREAS (ruined,
abandoned or haunted places), never on a WMO whose path says ruin, abandoned, burnt, destroyed, crypt, cave, mine
and the like (DARK_PATHS), never on dungeon or transport (ship) WMOs. A city floating above another place's ground
(Dalaran) takes its own area (WMO_TOWNS). The same spirit as tools/add_settlement_lights.py, whose lanterns light
inns and homes but not the barrow dens' unlit candles: only what looks lived in is lit.

Inputs (the map scan in waterfall-scan, see its README; nothing here opens an MPQ):
  <scan>/adt_wmos.tsv        every WMO placement (waterscan)
  <scan>/maps.tsv, <scan>/AreaTable.dbc
  <scan>/terrain-scan/<map>/<col>_<row>.npz   per-chunk area IDs (terrain_scan.py)
  <scan>/wmo-windows/        the placed WMOs' roots and the groups of those with window materials, made with
                             waterscan --extract-list <Data> <list> <scan>/wmo-windows (the lists:
                             <scan>/wmo-windows-roots.txt, then this tool's --group-list output)
Usage:
  python gen_window_lights.py <scan> --group-list <out.txt>   the group files to extract (after the roots)
  python gen_window_lights.py <scan> [data dir] [--survey]    write WindowLights.cdbc and the window rows of
        DoodadLightProperties.cdbc (properties 200..219, this tool's; add_settlement_lights.py owns 101..199).
        --survey also prints every area with windows, lit or not, to review TOWNS and DARK_AREAS.
In the client (with a Living Azeroth build that reads WindowLights.cdbc): copy both files into DBFilesClient, then
press "Reload light tables from disk" (lights panel). Run on the 2026-10-17 scan: 2,808 lit windows in 78 places,
1,858 casting light, at most 5 window lights reaching any 16-yd cell.
"""
import collections
import csv
import math
import os
import struct
import sys
from pathlib import Path

import cdbc_io as io
from add_settlement_lights import kelvin, merge, read_table

BS = "\\"
TILE = 1600.0 / 3.0
CHUNK = TILE / 16.0
MAX = 32.0 * TILE
CONTINENTS = {"azeroth": 0, "kalimdor": 1, "expansion01": 530, "northrend": 571}

# --- window materials -------------------------------------------------------------------------------------------
SIDN = 0x10
WINDOW_WORDS = ("window", "wnd", "stained", "glass")
NOT_WINDOWS = ("lampglass", "glass_axe")          # Stormwind's street lamps, Dalaran's axe shop sign
DARK_TEXTURES = ("abndn", "abandon", "burn", "broken", "ruin")

# --- places --------------------------------------------------------------------------------------------------------
AREA_TOWN, AREA_CAPITAL, AREA_CITY_PART, AREA_REST_H, AREA_REST_A = 0x200000, 0x100, 0x8, 0x400000, 0x800000
LIVED_IN = AREA_TOWN | AREA_CAPITAL | AREA_CITY_PART | AREA_REST_H | AREA_REST_A
# Lived-in places whose AreaTable flags don't say so (area names, enUS; --survey lists the rest). Hostile towns that
# are lived in (the Scarlet Crusade's Hearthglen and Tyr's Hand, the Venture Co.'s bay) count; farms don't.
TOWNS = {
    "Stormwind City", "Ironforge", "Darnassus", "Orgrimmar", "Thunder Bluff", "Undercity", "Silvermoon City",
    "The Exodar", "Shattrath City", "Dalaran", "Booty Bay", "Gadgetzan", "Ratchet", "Everlook", "Theramore Isle",
    "Menethil Harbor", "Southshore", "Tarren Mill", "Lakeshire", "Darkshire", "Goldshire", "Sentinel Hill",
    "Valgarde", "Kamagua", "Moa'ki Harbor", "Unu'pe", "Wintergarde Keep", "Amberpine Lodge", "Westfall Brigade Encampment",
    "Conquest Hold", "Agmar's Hammer", "Brunnhildar Village", "Dun Niffelem", "Bouldercrag's Refuge", "K3",
    "Argent Tournament Grounds", "The Argent Vanguard", "Wyrmrest Temple", "Fort Wildervar", "Camp Winterhoof",
    "Light's Hope Chapel", "Stormwind Harbor", "Old Town", "Trade District", "Mage Quarter", "Cathedral Square",
    "Dwarven District", "The Canals", "The Park", "Valley of Heroes", "Stormwind Keep", "Northshire Valley",
    "Farstrider Retreat", "Saltheril's Haven", "Steamwheedle Port", "Mirage Raceway", "Northwatch Hold",
    "Stonetalon Peak", "Talonbranch Glade", "Eastvale Logging Camp", "Steelgrill's Depot", "Amber Ledge",
    "Fizzcrank Airstrip", "Aldrassil", "Tower of Azora", "Violet Stand", "Westguard Inn",
    "Hearthglen", "Tyr's Hand", "New Hearthglen", "Venture Bay",
}
# Ruined, abandoned or haunted places: dark even where the flags call them a town (Deathknell's houses are
# ruins; Stromgarde Keep is held by the Syndicate and ogres; Pyrewood's villagers turn into worgen at night).
DARK_AREAS = {
    "Deathknell", "Stromgarde Keep", "Raven Hill", "Raven Hill Cemetery", "Moonbrook", "Andorhal", "Darrowshire",
    "Corin's Crossing", "Caer Darrow", "Scholomance", "Stratholme", "Northdale",
    "Ruins of Lordaeron", "Ruins of Alterac", "Alterac Valley", "Dalaran Crater", "Thondroril River",
    "Deatholme", "Dawnstar Spire", "Suncrown Village", "Goldenmist Village", "Andilien Estate", "Windrunner Village",
    "Windrunner Spire", "Sanctum of the Moon", "Elrendar Crossing", "Zeb'Nowa", "Amani Pass", "Shadowfang Keep",
    "Pyrewood Village", "Ambermill", "Fenris Isle", "Agamand Mills", "Brightwater Lake", "Solliden Farmstead",
    "Gilneas", "Gilneas City", "The Greymane Wall", "Thalanaar", "Uther's Tomb", "Ruins of Mathystra", "Mardenholde Keep",
    "Gol'Bolar Quarry", "The Bulwark", "Felstone Field", "Dalson's Tears", "Writhing Haunt",
    "Gahrron's Withering", "Crown Guard Tower", "Eastwall Tower", "Northpass Tower", "Light's Shield Tower",
    "Scarlet Point", "Onslaught Harbor", "Onslaught Base Camp", "Naxxanar", "Garm's Bane", "Forlorn Rowe",
    "Ruins of the Scarlet Enclave", "Ruins of Silvermoon", "The Dead Scar", "Farshire", "Icemist Village",
}
# WMOs that float above another place's ground: their windows belong to this town, not to the terrain below.
WMO_TOWNS = {"nd_dalaran.wmo": ("Dalaran", 571)}
DARK_PATHS = ("ruin", "abandon", "burnt", "burned", "destroyed", "broken", "haunt", "crypt", "cave", "mine",
              "tomb", "scourge", "plague", "wreck", "necropolis", "ziggurat", "scarlet", "onslaught", BS + "dungeon" + BS,
              BS + "transports" + BS, "transport", "ship")

# --- one light per facade ------------------------------------------------------------------------------------------
MAX_WIDTH = 8.0      # yd along the facade a cluster may span
MAX_HEIGHT = 9.0     # yd up the facade (two or three storeys of windows: one light)
COPLANAR = 1.2       # yd: panes this far in front of / behind a cluster's plane still join it
SAME_FACING = 0.9    # cos: panes facing within ~25 degrees join
OUT = 0.75           # yd: the light stands this far in front of the glass
MIN_AREA = 0.6       # yd^2 of glass: smaller pieces are trim, not a window
TILE_SIZE = 4.0      # yd: glass too big for one light (a tower's band of windows) is split into tiles this size
RADIUS_MIN, RADIUS_MAX = 6.0, 10.0   # yd: a light's reach, by its glass (light_of)
CELL, CELL_CAP = 16.0, 5             # the lights' grid (16-yd cells): at most this many window lights reach one

# --- the lights (DoodadLightProperties 200..219: this tool's) ------------------------------------------------------
SPOT, FLICKER_SMOOTH = 1, 1
BAKE, NIGHT_ONLY = 0x1, 0x2          # PropertiesFlag (features/DoodadLightTable.hpp)
D = -1.0
WARM, ELF, DRAENEI, DALARAN, NIGHTELF, ORC = 200, 201, 202, 203, 204, 205


def window_light(id, name, color, intensity, radius):
    # ID Name Type Color Intensity Radius Falloff InnerAngle OuterAngle FlickerMode FlickerSpeed FlickerAmount Flags
    # A spot facing out of the window: full within 30 degrees of its axis, nothing past 72 (the wall it's on stays
    # dark). A faint, slow flicker: a hearth or candles inside. Baked occlusion keeps it out of the house across a
    # narrow street; night only: it follows the client's own window fade.
    return (id, name, SPOT, color, intensity, radius, 1.3, 30.0, 72.0, FLICKER_SMOOTH, 0.4, 0.05, BAKE | NIGHT_ONLY)


LIGHTS = [
    window_light(WARM, "Window, warm (2700 K), night only", kelvin(2700), 0.9, 10.0),
    window_light(ELF, "Window, blood elf (gold), night only", 0xFFC860, 0.85, 10.0),
    window_light(DRAENEI, "Window, draenei (rose white), night only", 0xFFC4B8, 0.8, 10.0),
    window_light(DALARAN, "Window, Dalaran (3200 K), night only", kelvin(3200), 0.9, 10.0),
    window_light(NIGHTELF, "Window, night elf (moonlit), night only", 0xC8E8F0, 0.7, 10.0),
    window_light(ORC, "Window, orc and troll (firelight 2300 K), night only", kelvin(2300), 0.9, 10.0),
]
PROPERTY_IDS = range(200, 220)


def light_for_texture(texture):
    t = texture.lower()
    if "bloodelf" in t or "bldelf" in t or "highelf" in t or "silvermoon" in t:
        return ELF
    if "draenei" in t or "exodar" in t:
        return DRAENEI
    if "dalaran" in t or "dal_" in t:
        return DALARAN
    if "_ne_" in t or "nightelf" in t:
        return NIGHTELF
    if "ogrmr" in t or "orc" in t or "troll" in t:
        return ORC
    return WARM


# --- WMO files -----------------------------------------------------------------------------------------------------
def chunks(b, start=0, end=None):
    end = len(b) if end is None else end
    at = start
    while at + 8 <= end:
        tag = b[at:at + 4][::-1].decode("latin1")
        size, = struct.unpack_from("<I", b, at + 4)
        yield tag, at + 8, size
        at += 8 + size


def parse_root(b):
    """MOHD group count, the textures (MOTX) and materials (MOMT: flags, shader, blend, texture 1, emissive)."""
    info = {"groups": 0, "motx": b"", "materials": []}
    for tag, p, size in chunks(b):
        if tag == "MOHD":
            info["groups"], = struct.unpack_from("<I", b, p + 4)
        elif tag == "MOTX":
            info["motx"] = b[p:p + size]
        elif tag == "MOMT":
            for i in range(size // 64):
                flags, shader, blend, t1, sidn = struct.unpack_from("<5I", b, p + i * 64)
                info["materials"].append({"flags": flags, "shader": shader, "blend": blend, "texture": t1, "sidn": sidn})
    return info


def texture_name(info, offset):
    m = info["motx"]
    if offset >= len(m):
        return ""
    return m[offset:m.index(b"\0", offset)].decode("latin1").lower()


def window_materials(info):
    """{material index: texture} of the materials that light up as windows at night."""
    out = {}
    for i, m in enumerate(info["materials"]):
        t = texture_name(info, m["texture"])
        if not (m["flags"] & SIDN) or not (m["sidn"] & 0xFFFFFF):
            continue
        if any(w in t for w in WINDOW_WORDS) and not any(w in t for w in NOT_WINDOWS + DARK_TEXTURES):
            out[i] = t
    return out


def parse_group(b):
    g = {"flags": 0, "indices": (), "vertices": [], "normals": [], "batches": []}
    for tag, p, size in chunks(b):
        if tag != "MOGP":
            continue
        g["flags"], = struct.unpack_from("<I", b, p + 8)
        for t, d, s in chunks(b, p + 0x44, p + size):
            if t == "MOVI":
                g["indices"] = struct.unpack_from("<%dH" % (s // 2), b, d)
            elif t == "MOVT":
                g["vertices"] = [struct.unpack_from("<3f", b, d + 12 * i) for i in range(s // 12)]
            elif t == "MONR":
                g["normals"] = [struct.unpack_from("<3f", b, d + 12 * i) for i in range(s // 12)]
            elif t == "MOBA":
                # bbox (6 x int16), startIndex u32, count u16, minIndex u16, maxIndex u16, flags u8, material u8
                g["batches"] = [struct.unpack_from("<6hIHHHBB", b, d + 24 * i)[6:] for i in range(s // 24)]
    return g


# --- geometry ------------------------------------------------------------------------------------------------------
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def length(a): return math.sqrt(dot(a, a))
def vsum(vs):
    out = (0.0, 0.0, 0.0)
    for v in vs:
        out = add(out, v)
    return out
def unit(a):
    n = length(a)
    return mul(a, 1.0 / n) if n > 1e-9 else (0.0, 0.0, 0.0)


class Pane:
    """A piece of window glass: its middle, the way it faces (out of the building, once oriented), its glass area,
    its corners (to measure a facade cluster's extent), and whether its group is an exterior one."""
    def __init__(self, center, normal, area, points, texture, exterior=True):
        self.center, self.normal, self.area, self.points, self.texture = center, normal, area, points, texture
        self.exterior = exterior


def triangles(group):
    """Every triangle of a group (WMO space), for the open-sky test."""
    v, idx = group["vertices"], group["indices"]
    return [(v[idx[k]], v[idx[k + 1]], v[idx[k + 2]]) for k in range(0, len(idx) - 2, 3)
            if max(idx[k], idx[k + 1], idx[k + 2]) < len(v)]


def panes(group, materials):
    """The window panes of one group (WMO space, z up): connected, coplanar window triangles, facing the way their
    winding (and MONR) says. In an exterior group that is out of the building; an interior group's panes are
    oriented by orient() (Orgrimmar's buildings are all interior groups; Stormwind's rooms see the inner side)."""
    exterior = bool(group["flags"] & 0x8)
    tris = []
    for start, count, _mn, _mx, _fl, material in group["batches"]:
        if material in materials:
            for k in range(start, start + count - 2, 3):
                i = group["indices"][k:k + 3]
                if len(i) == 3:
                    tris.append((i, materials[material]))
    out = []
    # Connected pieces: triangles sharing a vertex index, and facing the same way (a window's frame and glass in
    # one batch are still one window; the two sides of a double-sided pane are not).
    parent = list(range(len(tris)))
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    v = group["vertices"]
    normals = []
    for (i, _t) in tris:
        normals.append(cross(sub(v[i[1]], v[i[0]]), sub(v[i[2]], v[i[0]])))
    by_vertex = collections.defaultdict(list)
    for k, (i, _t) in enumerate(tris):
        for x in i:
            by_vertex[x].append(k)
    for ks in by_vertex.values():
        for a in ks[1:]:
            if dot(unit(normals[a]), unit(normals[ks[0]])) > 0.7:
                ra, rb = find(a), find(ks[0])
                if ra != rb:
                    parent[ra] = rb
    pieces = collections.defaultdict(list)
    for k in range(len(tris)):
        pieces[find(k)].append(k)
    def centroid(k):
        i = tris[k][0]
        return mul(add(add(v[i[0]], v[i[1]]), v[i[2]]), 1.0 / 3.0)

    def pane_of(ks):
        area = 0.0
        center = (0.0, 0.0, 0.0)
        facing = (0.0, 0.0, 0.0)
        points = []
        for k in ks:
            a = 0.5 * length(normals[k])
            center = add(center, mul(centroid(k), a))
            facing = add(facing, normals[k])
            area += a
            points += [v[x] for x in tris[k][0]]
        # Glass facing every way at once (a glazed lantern box, a pane and its back in one piece) faces nowhere.
        if area < MIN_AREA or length(facing) < 0.8 * 2.0 * area:
            return None
        n = unit(facing)
        if abs(n[2]) > 0.7071:   # skylights and floor glass
            return None
        return Pane(mul(center, 1.0 / area), n, area, points, tris[ks[0]][1], exterior)

    for ks in pieces.values():
        p = pane_of(ks)
        if p is None:
            # Not one flat window; a band of glass round a tower may still hold some: try it in tiles.
            p = Pane((0.0, 0.0, 0.0), unit(vsum(normals[k] for k in ks)), 0.0, [], "", exterior)
            if length(p.normal) < 0.5:
                continue
        u, w = facade_axes(p.normal)
        us = [dot(q, u) for q in p.points] or [0.0]
        ws = [dot(q, w) for q in p.points] or [0.0]
        if p.area > 0.0 and max(us) - min(us) <= MAX_WIDTH and max(ws) - min(ws) <= MAX_HEIGHT:
            out.append(p)
            continue
        # Too big for one light (a tower's band of windows, a wall of glass): one pane per TILE x TILE yd of it.
        tiles = collections.defaultdict(list)
        for k in ks:
            c = centroid(k)
            tiles[(math.floor(dot(c, u) / TILE_SIZE), math.floor(dot(c, w) / TILE_SIZE))].append(k)
        for part in tiles.values():
            q = pane_of(part)
            if q is None:
                continue
            qu, qw = facade_axes(q.normal)
            us = [dot(x, qu) for x in q.points]
            ws = [dot(x, qw) for x in q.points]
            # A single long triangle (a spire's glass strip) is still too big: not a window.
            if max(us) - min(us) <= 1.5 * MAX_WIDTH and max(ws) - min(ws) <= 1.5 * MAX_HEIGHT:
                out.append(q)
    return out


class Occluder:
    """The WMO's own triangles in a 4-yd grid (WMO x, y), for rays a few yards long."""
    CELL = 4.0

    def __init__(self, tris):
        import numpy as np
        self.np = np
        t = np.array(tris, dtype=np.float64).reshape(-1, 3, 3) if tris else np.zeros((0, 3, 3))
        self.v0, self.e1, self.e2 = t[:, 0], t[:, 1] - t[:, 0], t[:, 2] - t[:, 0]
        lo, hi = t.min(axis=1), t.max(axis=1)
        self.grid = collections.defaultdict(list)
        for k in range(len(t)):
            for ix in range(int(math.floor(lo[k, 0] / self.CELL)), int(math.floor(hi[k, 0] / self.CELL)) + 1):
                for iy in range(int(math.floor(lo[k, 1] / self.CELL)), int(math.floor(hi[k, 1] / self.CELL)) + 1):
                    self.grid[(ix, iy)].append(k)
        self.grid = {k: np.array(v) for k, v in self.grid.items()}

    def blocked(self, origin, direction, reach):
        """Whether a ray from `origin` hits a triangle within `reach` yd (Moller-Trumbore)."""
        np = self.np
        end = add(origin, mul(direction, reach))
        cells = [self.grid.get((ix, iy)) for ix in range(int(math.floor(min(origin[0], end[0]) / self.CELL)),
                                                         int(math.floor(max(origin[0], end[0]) / self.CELL)) + 1)
                 for iy in range(int(math.floor(min(origin[1], end[1]) / self.CELL)),
                                 int(math.floor(max(origin[1], end[1]) / self.CELL)) + 1)]
        cells = [c for c in cells if c is not None]
        if not cells:
            return False
        k = np.unique(np.concatenate(cells))
        d = np.array(direction)
        e1, e2 = self.e1[k], self.e2[k]
        p = np.cross(d, e2)
        det = np.einsum("ij,ij->i", e1, p)
        ok = np.abs(det) > 1e-9
        inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        s = np.array(origin) - self.v0[k]
        u = np.einsum("ij,ij->i", s, p) * inv
        q = np.cross(s, e1)
        v = (q @ d) * inv
        t = np.einsum("ij,ij->i", e2, q) * inv
        return bool(np.any(ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1e-4) & (t < reach)))


# Rays from each side of an interior group's pane: elevations and turns away from straight out (degrees), from
# OPEN_START yd off the glass. Inside a hut or a room they hit its walls within OPEN_REACH yd; out in the street most
# of them get away.
OPEN_RAYS = [(e, a) for e in (0.0, 15.0, 30.0) for a in (-35.0, 0.0, 35.0)]
OPEN_REACH = 10.0
OPEN_START = 1.0


def open_rays(occluder, pane, facing):
    """How many OPEN_RAYS leave the pane on the `facing` side without hitting the building."""
    u = unit(cross((0.0, 0.0, 1.0), facing))
    flat = unit((facing[0], facing[1], 0.0))
    start = add(pane.center, mul(facing, OPEN_START))
    n = 0
    for elevation, turn in OPEN_RAYS:
        e, a = math.radians(elevation), math.radians(turn)
        horizontal = add(mul(flat, math.cos(a)), mul(u, math.sin(a)))
        d = unit(add(mul(horizontal, math.cos(e)), (0.0, 0.0, math.sin(e))))
        n += 0 if occluder.blocked(start, d, OPEN_REACH) else 1
    return n


def orient(all_panes, occluder):
    """The panes facing out of the building. A WMO with windows in exterior groups: those, as their winding (and
    MONR) face; its interior groups' window faces are the same windows seen from inside. A WMO whose windows are all
    in interior groups (Orgrimmar's huts): the panes with a clear outside, the side where OPEN_RAYS get away (7 of
    9) while the other side is closed (at most 2) -- turned round if it's behind them. Both sides closed is a
    window between rooms, both open free-standing glass: dropped. A window seen from both sides is kept once."""
    exterior = [p for p in all_panes if p.exterior]
    if exterior:
        return exterior
    out = []
    for p in all_panes:
        front, back = open_rays(occluder, p, p.normal), open_rays(occluder, p, mul(p.normal, -1.0))
        if front >= 7 and back <= 2:
            out.append(p)
        elif back >= 7 and front <= 2:
            p.normal = mul(p.normal, -1.0)
            out.append(p)
    kept = []
    for p in sorted(out, key=lambda q: -q.area):
        if not any(dot(p.normal, q.normal) > SAME_FACING and length(sub(p.center, q.center)) < 1.0 for q in kept):
            kept.append(p)
    return kept


def facade_axes(normal):
    """Along the facade (horizontal) and up it, for a window facing `normal`."""
    u = unit(cross((0.0, 0.0, 1.0), normal))
    return u, cross(normal, u)


class Cluster:
    def __init__(self, pane):
        self.panes = [pane]
        self.normal = pane.normal
        self.origin = pane.center

    def extent(self, extra=None):
        u, w = facade_axes(self.normal)
        pts = [p for pane in self.panes for p in pane.points] + (extra.points if extra else [])
        us = [dot(sub(p, self.origin), u) for p in pts]
        ws = [dot(sub(p, self.origin), w) for p in pts]
        return min(us), max(us), min(ws), max(ws)

    def accepts(self, pane):
        if dot(pane.normal, self.normal) < SAME_FACING:
            return False
        if abs(dot(sub(pane.center, self.origin), self.normal)) > COPLANAR:
            return False
        u0, u1, w0, w1 = self.extent(pane)
        return u1 - u0 <= MAX_WIDTH and w1 - w0 <= MAX_HEIGHT

    def add(self, pane):
        self.panes.append(pane)
        total = sum(p.area for p in self.panes)
        self.normal = unit(vsum(mul(p.normal, p.area) for p in self.panes))
        self.origin = mul(vsum(mul(p.center, p.area) for p in self.panes), 1.0 / total)

    @property
    def area(self):
        return sum(p.area for p in self.panes)

    def texture(self):
        by = collections.Counter()
        for p in self.panes:
            by[p.texture] += p.area
        return by.most_common(1)[0][0]


def cluster(all_panes):
    """Greedy: the biggest panes first, each joining the nearest cluster on its facade that stays within
    MAX_WIDTH x MAX_HEIGHT, else starting its own."""
    clusters = []
    for pane in sorted(all_panes, key=lambda p: (-p.area, p.center)):
        best, best_d = None, None
        for c in clusters:
            if c.accepts(pane):
                d = length(sub(pane.center, c.origin))
                if best is None or d < best_d:
                    best, best_d = c, d
        if best:
            best.add(pane)
        else:
            clusters.append(Cluster(pane))
    return clusters


def light_of(c):
    """(position, facing, half width, half height, radius, strength) in WMO space. The rectangle is the glass's
    extent on the facade (WindowGlow's mask), centred where the light's back-projection meets the facade. Reach and
    strength grow with the glass: one big window (~6 yd^2) reaches ~8.5 yd at strength 1."""
    u, w = facade_axes(c.normal)
    u0, u1, w0, w1 = c.extent()
    middle = add(c.origin, add(mul(u, 0.5 * (u0 + u1)), mul(w, 0.5 * (w0 + w1))))
    strength = max(0.6, min(1.4, 0.6 + 0.4 * math.sqrt(c.area / 6.0)))
    radius = max(RADIUS_MIN, min(RADIUS_MAX, RADIUS_MIN + math.sqrt(c.area)))
    return add(middle, mul(c.normal, OUT)), c.normal, 0.5 * (u1 - u0), 0.5 * (w1 - w0), radius, strength


# --- placement (noggit's convention; checked against all 1,559 tilted MODF extents, waterfall-scan tilt_check.py) ---
def _rot(axis, d):
    c, s = math.cos(math.radians(d)), math.sin(math.radians(d))
    if axis == "y":
        return [[c, 0, s], [0, 1, 0], [-s, 0, c]]
    if axis == "z":
        return [[c, s, 0], [-s, c, 0], [0, 0, 1]]
    return [[1, 0, 0], [0, c, s], [0, -s, c]]


def _matmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def placement(pos, rot):
    """A function WMO space (z up) -> world, and one for directions."""
    m = _matmul(_matmul(_rot("y", rot[1] - 90.0), _rot("z", rot[0])), _rot("x", -rot[2]))
    def direction(v):
        f = (v[0], v[2], -v[1])
        r = [m[i][0] * f[0] + m[i][1] * f[1] + m[i][2] * f[2] for i in range(3)]
        return (-r[2], -r[0], r[1])
    def point(v):
        f = (v[0], v[2], -v[1])
        r = [pos[i] + m[i][0] * f[0] + m[i][1] * f[1] + m[i][2] * f[2] for i in range(3)]
        return (MAX - r[2], MAX - r[0], r[1])
    return point, direction


# --- areas ---------------------------------------------------------------------------------------------------------
def read_areas(path):
    b = path.read_bytes()
    n, _f, rs, _ss = struct.unpack_from("<4I", b, 4)
    strings = 20 + n * rs
    out = {}
    for i in range(n):
        row = 20 + i * rs
        aid, map_id, parent = struct.unpack_from("<3I", b, row)
        flags, = struct.unpack_from("<I", b, row + 16)
        off, = struct.unpack_from("<I", b, row + 44)
        out[aid] = (b[strings + off:b.index(b"\0", strings + off)].decode("latin1").strip(), parent, flags, map_id)
    return out


class AreaMap:
    def __init__(self, scan):
        self.scan = scan
        self.tiles = {}
        self.dirs = {d.lower(): d for d in os.listdir(scan / "terrain-scan")}

    def at(self, mapdir, x, y):
        import numpy as np
        row, col = int(32 - x / TILE), int(32 - y / TILE)
        key = (mapdir, col, row)
        if key not in self.tiles:
            p = self.scan / "terrain-scan" / self.dirs.get(mapdir, mapdir) / ("%d_%d.npz" % (col, row))
            self.tiles[key] = np.load(p)["area"] if p.exists() else None
        a = self.tiles[key]
        if a is None:
            return 0
        cv = min(15, max(0, int(((32 - row) * TILE - x) / CHUNK)))
        cu = min(15, max(0, int(((32 - col) * TILE - y) / CHUNK)))
        return int(a[cv, cu])


def lived_in(areas, area, wmo):
    """(lit?, why) for a window of `wmo` standing in `area`."""
    path = wmo.lower()
    for w in DARK_PATHS:
        if w in path:
            return False, "path: " + w.strip(BS)
    chain, a = [], area
    while a and a in areas and len(chain) < 6:
        chain.append(a)
        a = areas[a][1]
    names = [areas[a][0] for a in chain]
    if names and (names[0] in DARK_AREAS or (names[0] not in TOWNS and any(n in DARK_AREAS for n in names))):
        return False, "dark area"
    if any(n in TOWNS for n in names):
        return True, "town list"
    # The area's own flags (a zone's don't make every farm in it a town); a capital's parts count too.
    if (areas.get(area, ("", 0, 0, 0))[2] & LIVED_IN) or (len(chain) > 1 and areas[chain[1]][2] & (AREA_CAPITAL | AREA_CITY_PART)):
        return True, "area flags"
    return False, "not a town"


# --- the table -----------------------------------------------------------------------------------------------------
WINDOW_LIGHTS = ["u", "u", "u", "f", "f", "f", "f", "f", "f", "f", "f", "f", "f", "u", "f", "u", "s"]
#  ID MapID AreaID PosX PosY PosZ FacingX FacingY FacingZ Back HalfWidth HalfHeight Radius LightID Strength Flags Comment
GLOW_ONLY = 0x1      # Flags: the window glows (WindowGlow) but casts no light (thin() left it out of a full cell)
R_X, R_Y, R_HW, R_HH, R_RADIUS, R_FLAGS = 3, 4, 10, 11, 12, 15


def cells_reached(x, y, radius, size=CELL):
    """The size-yd world cells (x, y aligned to multiples of size, as the lights' grid is) a light reaches."""
    out = []
    for cx in range(int(math.floor((x - radius) / size)), int(math.floor((x + radius) / size)) + 1):
        for cy in range(int(math.floor((y - radius) / size)), int(math.floor((y + radius) / size)) + 1):
            nx = max(cx * size, min(x, (cx + 1) * size)) - x
            ny = max(cy * size, min(y, (cy + 1) * size)) - y
            if nx * nx + ny * ny <= radius * radius:
                out.append((cx, cy))
    return out


def thin(rows):
    """Keeps the light budget: the biggest windows first, a window casts light only while every 16-yd cell it
    reaches holds fewer than CELL_CAP window lights; the others only glow (GLOW_ONLY)."""
    counts = collections.Counter()
    for r in sorted(rows, key=lambda r: -(r[R_HW] * r[R_HH])):
        cells = [(r[1],) + c for c in cells_reached(r[R_X], r[R_Y], r[R_RADIUS])]
        if any(counts[c] >= CELL_CAP for c in cells):
            r[R_FLAGS] |= GLOW_ONLY
            continue
        for c in cells:
            counts[c] += 1


def placements(scan):
    seen = {}
    with open(scan / "adt_wmos.tsv", newline="") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            mapdir = row["source"].lower().split(BS)[2]
            if mapdir not in CONTINENTS or row["uniqueId"] in seen:
                continue
            seen[row["uniqueId"]] = (mapdir, row["wmo"].lower(),
                                     tuple(float(row[k]) for k in ("px", "py", "pz")),
                                     tuple(float(row[k]) for k in ("rx", "ry", "rz")))
    return seen


def flat(scan, name):
    return scan / "wmo-windows" / name.replace(BS, "#")


def build(scan, survey=False):
    areas = read_areas(scan / "AreaTable.dbc")
    amap = AreaMap(scan)
    per_wmo = {}
    rows, report = [], collections.Counter()
    lit_by_area, dark_by_area = collections.Counter(), collections.Counter()
    for uid, (mapdir, wmo, pos, rot) in sorted(placements(scan).items(), key=lambda kv: int(kv[0])):
        if wmo not in per_wmo:
            per_wmo[wmo] = []
            rp = flat(scan, wmo)
            if rp.exists():
                info = parse_root(rp.read_bytes())
                mats = window_materials(info)
                if mats:
                    found, tris = [], []
                    for g in range(info["groups"]):
                        gp = flat(scan, "%s_%03d.wmo" % (wmo[:-4], g))
                        if gp.exists():
                            group = parse_group(gp.read_bytes())
                            found += panes(group, mats)
                            tris += triangles(group)
                    if found and not any(p.exterior for p in found):
                        found = orient(found, Occluder(tris))
                    else:
                        found = orient(found, None)
                    per_wmo[wmo] = [(light_of(c), c.texture()) for c in cluster(found)]
        if not per_wmo[wmo]:
            continue
        point, direction = placement(pos, rot)
        town = WMO_TOWNS.get(wmo.split(BS)[-1])
        town_area = next((a for a, v in areas.items() if town and v[0] == town[0] and v[3] == town[1]), 0)
        for (p, n, hw, hh, radius, strength), texture in per_wmo[wmo]:
            wp, wn = point(p), unit(direction(n))
            area = town_area if town_area else amap.at(mapdir, wp[0], wp[1])
            lit, why = lived_in(areas, area, wmo)
            name = areas.get(area, ("?", 0, 0, 0))[0]
            (lit_by_area if lit else dark_by_area)[(mapdir, name, why)] += 1
            if not lit:
                continue
            report[(mapdir, name)] += 1
            rows.append([0, CONTINENTS[mapdir], area, wp[0], wp[1], wp[2], wn[0], wn[1], wn[2], OUT, hw, hh, radius,
                         light_for_texture(texture), strength, 0, "%s: %s" % (name, wmo.split(BS)[-1][:-4])])
    thin(rows)
    rows.sort(key=lambda r: (r[1], math.floor(r[3] / 64), math.floor(r[4] / 64), r[3], r[4], r[5]))
    for i, r in enumerate(rows):
        r[0] = i + 1
    if survey:
        print("lit:")
        for k, n in sorted(lit_by_area.items()):
            print("  %4d  %s / %s (%s)" % (n, *k))
        print("dark:")
        for k, n in sorted(dark_by_area.items()):
            print("  %4d  %s / %s (%s)" % (n, *k))
    return rows, report


def busiest_cells(rows, size=CELL):
    """Window lights reaching each cell (glow-only rows don't count)."""
    cells = collections.Counter()
    for r in rows:
        if not r[R_FLAGS] & GLOW_ONLY:
            for c in cells_reached(r[R_X], r[R_Y], r[R_RADIUS], size):
                cells[(r[1],) + c] += 1
    return cells


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        sys.exit(__doc__)
    scan = Path(args[0])
    if "--group-list" in sys.argv:
        out = Path(sys.argv[sys.argv.index("--group-list") + 1])
        names = []
        for mapdir, wmo, _p, _r in placements(scan).values():
            rp = flat(scan, wmo)
            if rp.exists():
                info = parse_root(rp.read_bytes())
                if window_materials(info):
                    names += ["%s_%03d.wmo" % (wmo[:-4], g) for g in range(info["groups"])]
        out.write_text("".join(n + "\r\n" for n in sorted(set(names))), newline="")
        print(f"{out}: {len(set(names))} group files")
        return
    folder = Path(args[1]) if len(args) > 1 else Path(__file__).resolve().parent.parent / "data"
    rows, report = build(scan, survey="--survey" in sys.argv)
    cells = busiest_cells(rows)
    casting = [r for r in rows if not r[R_FLAGS] & GLOW_ONLY]
    print(f"{len(rows)} lit windows in {len(report)} places: {len(casting)} cast light, {len(rows) - len(casting)} only glow; "
          f"busiest 16-yd cell: {max(cells.values()) if cells else 0} window lights")
    by_place = collections.Counter()
    for r in rows:
        by_place[(r[16].split(":")[0], r[1], not r[R_FLAGS] & GLOW_ONLY)] += 1
    places = sorted({(k[0], k[1]) for k in by_place}, key=lambda k: -(by_place[k + (True,)] + by_place[k + (False,)]))
    for name, map_id in places[:60 if "--survey" in sys.argv else 25]:
        print(f"  {by_place[(name, map_id, True)]:4d} lights, {by_place[(name, map_id, False)]:3d} glow only  {name} (map {map_id})")

    wpath = folder / "WindowLights.cdbc"
    data = io.build(WINDOW_LIGHTS, [tuple(r) for r in rows])
    ppath = folder / "DoodadLightProperties.cdbc"
    props = read_table(ppath, io.DOODAD_LIGHT_PROPERTIES)
    pdata = io.build(io.DOODAD_LIGHT_PROPERTIES,
                     [tuple(r) for r in merge(props, LIGHTS, PROPERTY_IDS, "DoodadLightProperties", key=lambda r: r[1])])
    for path, new in ((wpath, data), (ppath, pdata)):
        old = path.read_bytes() if path.exists() else None
        if new == old:
            print(f"{path}: unchanged")
            continue
        if old is not None and path.parent.name.lower() == "dbfilesclient":
            backup = io.backup_path(path)
            backup.write_bytes(old)
            print(f"{path}: backup {backup.name}")
        path.write_bytes(new)
        print(f"{path}: written")


if __name__ == "__main__":
    main()
