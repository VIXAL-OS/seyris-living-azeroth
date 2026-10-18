"""Window detection and light placement (tools/gen_window_lights.py) on small WMOs built here, no map data:
which materials are windows, panes and which way they face (exterior groups; interior-only WMOs by open rays),
one light per facade, where the light stands, the placement transform, which places are lived in, the light
budget per 16-yd cell, and the rooms behind each light's glass (WindowRooms.cdbc: the shader's cells, their shares,
the buildings).

Run:  python -m unittest discover -s tests/windows -p "test_*.py"     (from the repository)
"""
import math
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
import gen_window_lights as g  # noqa: E402

BS = "\\"


def chunk(tag, payload):
    return tag[::-1].encode("latin1") + struct.pack("<I", len(payload)) + payload


def root(materials, textures, groups=1):
    """materials: (flags, emissive, texture index) each."""
    motx, offsets = b"", []
    for t in textures:
        offsets.append(len(motx))
        motx += t.encode("latin1") + b"\0"
    momt = b"".join(struct.pack("<16I", flags, 0, 0, offsets[t], emissive, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
                    for flags, emissive, t in materials)
    mohd = struct.pack("<16I", len(materials), groups, *([0] * 14))
    return chunk("MVER", struct.pack("<I", 17)) + chunk("MOHD", mohd) + chunk("MOTX", motx) + chunk("MOMT", momt)


def group(flags, quads):
    """quads: (material, four corners in winding order) each; two triangles (0,1,2), (0,2,3) per quad."""
    verts, idx, batches = [], [], []
    for material, corners in quads:
        base, start = len(verts), len(idx)
        verts += corners
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
        batches.append(struct.pack("<6hIHHHBB", 0, 0, 0, 0, 0, 0, start, 6, base, base + 3, 0, material))
    header = struct.pack("<3I6f", 0, 0, flags, 0, 0, 0, 0, 0, 0).ljust(0x44, b"\0")
    body = (chunk("MOVI", struct.pack("<%dH" % len(idx), *idx)) +
            chunk("MOVT", b"".join(struct.pack("<3f", *v) for v in verts)) +
            chunk("MONR", b"".join(struct.pack("<3f", 0, 0, 1) for _ in verts)) +
            chunk("MOBA", b"".join(batches)))
    return chunk("MVER", struct.pack("<I", 17)) + chunk("MOGP", header + body)


def wall_x(x, y0, y1, z0, z1, out=True):
    """A quad in the plane x, facing +x (out) or -x."""
    q = [(x, y0, z0), (x, y1, z0), (x, y1, z1), (x, y0, z1)]
    return q if out else q[::-1]


WINDOW = "dungeons\\textures\\windows\\mm_elwynn_wnd_ext__01.blp"
TEXTURES = [WINDOW, "dungeons\\textures\\walls\\stone.blp", "dungeons\\textures\\doodads\\stormwindlampglass.blp",
            "dungeons\\textures\\windows\\jlo_bloodelf_windows_abndn.blp"]
MATERIALS = [(0x10, 0xFFFFFFFF, 0),   # 0 a window: SIDN, white emissive
             (0x00, 0, 1),            # 1 a wall
             (0x10, 0xFF000000, 0),   # 2 SIDN but black: never lights up
             (0xD0, 0xFFFFFFFF, 2),   # 3 lamp glass
             (0x10, 0xFFFFFFFF, 3)]   # 4 an abandoned building's window


class Materials(unittest.TestCase):
    def test_only_lit_window_glass(self):
        info = g.parse_root(root(MATERIALS, TEXTURES))
        self.assertEqual(info["groups"], 1)
        self.assertEqual(g.window_materials(info), {0: WINDOW})

    def test_light_by_texture(self):
        self.assertEqual(g.light_for_texture("dungeons\\textures\\windows\\jlo_bloodelf_windows.blp"), g.ELF)
        self.assertEqual(g.light_for_texture("dungeons\\textures\\draenei\\jlo_draenei_window.blp"), g.DRAENEI)
        self.assertEqual(g.light_for_texture("dungeons\\textures\\dalaran\\eb_dalaran_windows1.blp"), g.DALARAN)
        self.assertEqual(g.light_for_texture(WINDOW), g.WARM)


class Panes(unittest.TestCase):
    def setUp(self):
        self.mats = {0: WINDOW}

    def test_exterior_window_faces_out(self):
        grp = g.parse_group(group(0x8, [(0, wall_x(5, -1, 1, 1, 3)), (1, wall_x(5, 2, 9, 0, 4))]))
        p = g.panes(grp, self.mats)
        self.assertEqual(len(p), 1)
        self.assertAlmostEqual(p[0].area, 4.0, places=4)
        self.assertEqual(tuple(round(c, 4) for c in p[0].normal), (1.0, 0.0, 0.0))
        self.assertEqual(tuple(round(c, 4) for c in p[0].center), (5.0, 0.0, 2.0))

    def test_trim_skylights_and_boxes_are_not_windows(self):
        tiny = wall_x(5, 0, 0.5, 0, 0.5)                                     # 0.25 yd^2
        sky = [(0, 0, 5), (2, 0, 5), (2, 2, 5), (0, 2, 5)]                   # facing up
        grp = g.parse_group(group(0x8, [(0, tiny), (0, sky), (0, wall_x(8, 0, 2, 0, 2)), (0, wall_x(8, 0, 2, 0, 2, out=False))]))
        p = g.panes(grp, self.mats)
        # The tiny piece and the skylight go; the double-sided pane is two pieces (they face apart), each a pane.
        self.assertEqual(sorted(round(q.normal[0]) for q in p), [-1, 1])

    def test_interior_groups_give_way_to_exterior_ones(self):
        ext = g.panes(g.parse_group(group(0x8, [(0, wall_x(5, -1, 1, 1, 3))])), self.mats)
        inner = g.panes(g.parse_group(group(0x2000, [(0, wall_x(4.8, -1, 1, 1, 3, out=False))])), self.mats)
        self.assertTrue(ext[0].exterior and not inner[0].exterior)
        kept = g.orient(ext + inner, None)
        self.assertEqual(len(kept), 1)
        self.assertTrue(kept[0].exterior)

    def test_interior_only_window_faces_where_the_rays_get_out(self):
        # A 6 x 6 x 4 room (interior group) with a window in its east wall, drawn facing in (the inside of the glass).
        room = [(1, [(0, 0, 0), (6, 0, 0), (6, 6, 0), (0, 6, 0)]), (1, [(0, 0, 4), (0, 6, 4), (6, 6, 4), (6, 0, 4)]),
                (1, wall_x(0, 0, 6, 0, 4)), (1, wall_x(6, 0, 6, 0, 4)),
                (1, [(0, 0, 0), (0, 0, 4), (6, 0, 4), (6, 0, 0)]), (1, [(0, 6, 0), (6, 6, 0), (6, 6, 4), (0, 6, 4)]),
                (0, wall_x(6, 2, 4, 1, 3, out=False))]
        grp = g.parse_group(group(0x2000, room))
        p = g.panes(grp, self.mats)
        self.assertEqual(len(p), 1)
        self.assertAlmostEqual(p[0].normal[0], -1.0, places=4)
        kept = g.orient(p, g.Occluder(g.triangles(grp)))
        self.assertEqual(len(kept), 1)
        self.assertAlmostEqual(kept[0].normal[0], 1.0, places=4)   # turned round: out of the room

    def test_a_window_between_rooms_is_dropped(self):
        rooms = [(1, [(-6, 0, 0), (6, 0, 0), (6, 6, 0), (-6, 6, 0)]), (1, [(-6, 0, 4), (-6, 6, 4), (6, 6, 4), (6, 0, 4)]),
                 (1, wall_x(-6, 0, 6, 0, 4)), (1, wall_x(6, 0, 6, 0, 4, out=False)),
                 (1, [(-6, 0, 0), (-6, 0, 4), (6, 0, 4), (6, 0, 0)]), (1, [(-6, 6, 0), (6, 6, 0), (6, 6, 4), (-6, 6, 4)]),
                 (0, wall_x(0, 2, 4, 1, 3))]
        grp = g.parse_group(group(0x2000, rooms))
        self.assertEqual(g.orient(g.panes(grp, self.mats), g.Occluder(g.triangles(grp))), [])


class Facades(unittest.TestCase):
    def panes(self, quads):
        return g.panes(g.parse_group(group(0x8, [(0, q) for q in quads])), {0: WINDOW})

    def test_neighbours_on_a_facade_share_a_light(self):
        p = self.panes([wall_x(5, -1, 1, 1, 3), wall_x(5, 2, 4, 1, 3), wall_x(5, 30, 32, 1, 3)])
        clusters = g.cluster(p)
        self.assertEqual(sorted(len(c.panes) for c in clusters), [1, 2])

    def test_a_wide_row_is_split(self):
        p = self.panes([wall_x(5, 3 * k, 3 * k + 2, 1, 3) for k in range(6)])   # 17 yd of windows
        for c in g.cluster(p):
            u0, u1, w0, w1 = c.extent()
            self.assertLessEqual(u1 - u0, g.MAX_WIDTH + 1e-6)
        self.assertEqual(len(g.cluster(p)), 2)   # 0-8 and 9-17

    def test_facing_other_ways_never_share(self):
        north = [(0, 10, 1), (-2, 10, 1), (-2, 10, 3), (0, 10, 3)]          # facing +y
        p = self.panes([wall_x(5, 8, 10, 1, 3), north])
        self.assertEqual(len(g.cluster(p)), 2)

    def test_the_light_stands_in_front_of_the_glass(self):
        c = g.cluster(self.panes([wall_x(5, -1, 1, 1, 3), wall_x(5, 2, 4, 1, 3)]))[0]
        pos, facing, hw, hh, radius, strength = g.light_of(c)
        self.assertEqual(tuple(round(v, 4) for v in pos), (5.0 + g.OUT, 1.5, 2.0))
        self.assertEqual(tuple(round(v, 4) for v in facing), (1.0, 0.0, 0.0))
        self.assertAlmostEqual(hw, 2.5, places=4)
        self.assertAlmostEqual(hh, 1.0, places=4)
        self.assertAlmostEqual(radius, min(g.RADIUS_MAX, g.RADIUS_MIN + math.sqrt(8.0)), places=4)
        self.assertTrue(0.6 <= strength <= 1.4)


class Placement(unittest.TestCase):
    def test_origin_and_up(self):
        pos, rot = (100.0, 50.0, 200.0), (0.0, 0.0, 0.0)
        point, direction = g.placement(pos, rot)
        self.assertEqual(tuple(round(v, 3) for v in point((0, 0, 0))), (round(g.MAX - 200.0, 3), round(g.MAX - 100.0, 3), 50.0))
        self.assertEqual(tuple(round(v, 6) for v in direction((0, 0, 1))), (0.0, 0.0, 1.0))

    def test_turning_keeps_lengths_and_the_vertical(self):
        for rot in ((0.0, 37.0, 0.0), (0.0, 180.0, 0.0), (5.0, 90.0, -3.0)):
            point, direction = g.placement((10.0, 20.0, 30.0), rot)
            d = direction((3.0, 4.0, 0.0))
            self.assertAlmostEqual(math.sqrt(sum(v * v for v in d)), 5.0, places=4)
            p0, p1 = point((0, 0, 0)), point((3.0, 4.0, 0.0))
            self.assertTrue(all(abs((p1[k] - p0[k]) - d[k]) < 1e-4 for k in range(3)))
            if rot[0] == 0.0 and rot[2] == 0.0:
                self.assertAlmostEqual(d[2], 0.0, places=5)


class Places(unittest.TestCase):
    AREAS = {
        1: ("Elwynn Forest", 0, 0x40, 0), 87: ("Goldshire", 1, 0x200000, 0), 9: ("Northshire Valley", 1, 0, 0),
        2: ("Tirisfal Glades", 0, 0, 0), 3: ("Deathknell", 2, 0x200000, 0), 4: ("Brill", 2, 0x200000, 0),
        5: ("Some Farm", 1, 0, 0), 6: ("Duskwood", 0, 0, 0), 7: ("Raven Hill", 6, 0, 0), 8: ("Darkshire", 6, 0, 0),
        10: ("Stormwind City", 0, 0x100, 0), 11: ("Trade District", 10, 0x8, 0),
    }
    HOUSE = "world\\wmo\\azeroth\\buildings\\humantwostory\\humantwostory.wmo"

    def lit(self, area, wmo=HOUSE):
        return g.lived_in(self.AREAS, area, wmo)[0]

    def test_towns_and_cities(self):
        self.assertTrue(self.lit(87))   # flags and the list
        self.assertTrue(self.lit(4))    # flags
        self.assertTrue(self.lit(9))    # the list (the abbey)
        self.assertTrue(self.lit(11))   # a capital's part
        self.assertTrue(self.lit(8))    # a town in a haunted zone

    def test_dark_places(self):
        self.assertFalse(self.lit(3))   # ruins, though flagged a town
        self.assertFalse(self.lit(7))   # abandoned
        self.assertFalse(self.lit(5))   # a farm: not a town
        self.assertFalse(self.lit(1))   # the zone itself

    def test_dark_buildings(self):
        for wmo in ("world\\wmo\\azeroth\\buildings\\duskwoodabandoned_barn\\duskwoodabandoned_barn.wmo",
                    "world\\wmo\\dungeon\\md_goldmine\\md_goldmine_varianta.wmo",
                    "world\\wmo\\transports\\passengership\\transportship_a.wmo",
                    "world\\wmo\\lorderon\\collidabledoodads\\arathihighlands\\humanruinedbuildings\\huruinedbuildingssmall02.wmo"):
            self.assertFalse(self.lit(87, wmo), wmo)


class Budget(unittest.TestCase):
    def row(self, x, y, hw=1.0, radius=8.0):
        r = [0, 0, 87, x, y, 50.0, 1.0, 0.0, 0.0, g.OUT, hw, 1.0, radius, g.WARM, 1.0, 0, "Goldshire: test"]
        return r

    def test_a_full_cell_keeps_the_biggest_windows(self):
        rows = [self.row(8.0, 8.0, hw=1.0 + 0.1 * k) for k in range(8)]
        g.thin(rows)
        casting = [r for r in rows if not r[g.R_FLAGS] & g.GLOW_ONLY]
        self.assertEqual(len(casting), g.CELL_CAP)
        self.assertEqual(min(r[g.R_HW] for r in casting), max(r[g.R_HW] for r in rows if r[g.R_FLAGS] & g.GLOW_ONLY) + 0.1)
        self.assertLessEqual(max(g.busiest_cells(rows).values()), g.CELL_CAP)

    def test_far_apart_all_cast(self):
        rows = [self.row(100.0 * k, 0.0) for k in range(8)]
        g.thin(rows)
        self.assertFalse(any(r[g.R_FLAGS] & g.GLOW_ONLY for r in rows))

    def test_cells_reached(self):
        self.assertEqual(sorted(g.cells_reached(8.0, 8.0, 4.0)), [(0, 0)])
        self.assertEqual(len(g.cells_reached(16.0, 16.0, 4.0)), 4)


class Rooms(unittest.TestCase):
    """Which rooms' cells a light's glass covers (WindowRooms.cdbc): the shader's cells about the building's origin,
    in each face's own frame, cut exactly; the shares of the light's glass, by building; the shader's fallback seed."""
    SW = (-8913.2, 554.6, 93.1)   # Stormwind's WMO origin (GlassBuildings)

    @staticmethod
    def quad(o, n, a0, a1, u0, u1):
        """Two triangles of the rectangle a0..a1 along, u0..u1 up the face of normal n, about o (world)."""
        a, u = g.cell_frame(n)
        c = [g.add(o, g.add(g.mul(a, x), g.mul(u, y))) for x, y in ((a0, u0), (a1, u0), (a1, u1), (a0, u1))]
        return [[c[0], c[1], c[2]], [c[0], c[2], c[3]]]

    def test_the_frame_is_the_shaders(self):
        # along (-n.y, n.x, 0), up n x along; a skylight along x (env/GlassModel RoomCell, the glass tests' vectors)
        self.assertEqual(g.cell_frame((1.0, 0.0, 0.0)), ((0.0, 1.0, 0.0), (0.0, 0.0, 1.0)))
        a, u = g.cell_frame((0.0, 1.0, 0.0))
        self.assertEqual((a, tuple(abs(v) for v in u)), ((-1.0, 0.0, 0.0), (0.0, 0.0, 1.0)))
        self.assertEqual(g.cell_frame((0.0, 0.0, 1.0)), ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0)))
        # the glass tests' hand points: facing +x 7.1 along and 3.3 up -> (2, 1); facing +y 4 yd west, 0.5 down -> (1, -1)
        for n, p, want in (((1.0, 0.0, 0.0), (0.0, 7.1, 3.3), (2, 1)), ((0.0, 1.0, 0.0), (-4.0, 0.0, -0.5), (1, -1))):
            a, u = g.cell_frame(n)
            self.assertEqual((math.floor(g.dot(p, a) * g.INV_W), math.floor(g.dot(p, u) * g.INV_H)), want)

    def test_a_cell_exactly(self):
        cells = g.room_cells(self.quad((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), 0.0, 3.4999, 0.0001, 3.1999), (1.0, 0.0, 0.0), (0.0, 0.0, 0.0))
        self.assertEqual(list(cells), [(0, 0)])
        self.assertAlmostEqual(cells[(0, 0)], 3.4999 * 3.1998, places=4)

    def test_a_window_across_cells_is_cut_by_them(self):
        # 2.5 .. 4.5 along, 2.7 .. 3.7 up: four cells, 1 x 0.5 yd each
        cells = g.room_cells(self.quad((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), 2.5, 4.5, 2.7, 3.7), (1.0, 0.0, 0.0), (0.0, 0.0, 0.0))
        self.assertEqual(sorted(cells), [(0, 0), (0, 1), (1, 0), (1, 1)])
        for k, want in (((0, 0), 0.5), ((1, 0), 0.5), ((0, 1), 0.5), ((1, 1), 0.5)):
            self.assertAlmostEqual(cells[k], want, places=3)

    def test_about_the_buildings_origin_stormwind_or_not(self):
        here = g.room_cells(self.quad((10.0, 20.0, 5.0), (0.6, 0.8, 0.0), -1.0, 2.0, 0.5, 2.5), (0.6, 0.8, 0.0), (0.0, 0.0, 0.0))
        o = g.add(self.SW, (10.0, 20.0, 5.0))
        there = g.room_cells(self.quad(o, (0.6, 0.8, 0.0), -1.0, 2.0, 0.5, 2.5), (0.6, 0.8, 0.0), self.SW)
        self.assertEqual(sorted(here), sorted(there))
        for k in here:
            self.assertAlmostEqual(here[k], there[k], places=3)

    def test_far_from_the_origin_about_its_anchor(self):
        # 40 yd east of the origin: about the anchor 64 yd east (k = 1, 0, 0): the cell (2, 1) + (211, 59), as the glass
        # tests' hand point; a window across the anchors' edge (x = 32) is cut there, into both anchors' cells
        n = (1.0, 0.0, 0.0)
        a, u = g.cell_frame(n)
        k = g.anchor_index((40.0, 7.1, 3.3))
        self.assertEqual(k, (1, 0, 0))
        r = g.sub((40.0, 7.1, 3.3), g.mul(k, g.ANCHOR))
        self.assertEqual((math.floor(g.dot(r, a) * g.INV_W) + 211, math.floor(g.dot(r, u) * g.INV_H) + 59), (213, 60))
        # a pane facing +y, 30..34 yd east, 1 yd tall: across x = 32
        q = self.quad((32.0, 5.0, 0.5), (0.0, 1.0, 0.0), -2.0, 2.0, 0.2, 1.2)
        cells = g.room_cells(q, (0.0, 1.0, 0.0), (0.0, 0.0, 0.0))
        near = {c: v for c, v in cells.items() if abs(c[0]) < 100}
        far = {c: v for c, v in cells.items() if abs(c[0]) >= 100}
        self.assertAlmostEqual(sum(near.values()), 2.0, places=4)   # 30..32 east: about the origin
        self.assertAlmostEqual(sum(far.values()), 2.0, places=4)    # 32..34: about the anchor at 64
        self.assertTrue(all(c[0] > 100 for c in far))

    def test_the_face_is_seen_from_outside(self):
        # wound the other way (its normal in), the same cells: the frame turns to `out`
        q = self.quad((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), 0.3, 5.0, 0.2, 4.0)
        flipped = [[t[0], t[2], t[1]] for t in q]
        a = g.room_cells(q, (1.0, 0.0, 0.0), (0.0, 0.0, 0.0))
        b = g.room_cells(flipped, (1.0, 0.0, 0.0), (0.0, 0.0, 0.0))
        self.assertEqual(sorted(a), sorted(b))
        self.assertAlmostEqual(sum(a.values()), 4.7 * 3.8, places=3)

    def test_a_lights_shares_by_building(self):
        # a light of three panes: clear glass in a city's group (its own row), leaded in another group (the
        # placement's row), stained (no room); each 2 x 1 yd
        clear, leaded, stained = "glass_clear.blp", "glass_leaded.blp", "glass_stained.blp"
        glass = {"kinds": {clear: 1, leaded: 4, stained: 2},
                 "buildings": {(0, 777, 3): (41, (0.0, 0.0, 0.0)), (0, 777, g.WHOLE): (40, (0.0, 0.0, 0.0))}}

        class P:
            def __init__(self, texture, group, a0):
                self.texture, self.group, self.normal = texture, group, (1.0, 0.0, 0.0)
                self.points = [p for t in Rooms.quad((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), a0, a0 + 2.0, 0.5, 1.5) for p in t]

        class C:
            panes = [P(clear, 3, 0.2), P(leaded, 9, 7.2), P(stained, 3, 4.0)]

        same = lambda v: v
        rows = g.light_rooms(C, same, same, 0, 777, "World\\wmo\\X\\House.wmo", glass)
        by = {}
        for b, x, y, share, seed in rows:
            by[b] = by.get(b, 0.0) + share
            self.assertEqual(seed, g.path_hash("world/wmo/x/house.wmo"))
        self.assertEqual(sorted(by), [40, 41])
        self.assertAlmostEqual(by[41], 1.0 / 3.0, places=5)   # the clear pane: its group's row
        self.assertAlmostEqual(by[40], 1.0 / 3.0, places=5)   # the leaded one: the placement's
        # no building at all: 0, about the placement's origin
        rows = g.light_rooms(C, same, same, 0, 999, "World\\wmo\\X\\House.wmo", glass)
        self.assertEqual({r[0] for r in rows}, {0})

    def test_path_hash_is_the_runtimes(self):
        self.assertEqual(g.path_hash("World/WMO/Azeroth/Buildings/Stormwind/Stormwind.wmo"), 4036435193)
        self.assertEqual(g.path_hash(""), 0)

    def test_the_shipped_glass_tables_read(self):
        data = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "data")
        if not os.path.exists(os.path.join(data, "GlassBuildings.cdbc")):
            self.skipTest("no data folder")
        from pathlib import Path
        glass = g.read_glass(Path(data))
        self.assertTrue(any(k in g.ROOM_KINDS for k in glass["kinds"].values()))
        self.assertGreater(len(glass["buildings"]), 1000)


if __name__ == "__main__":
    unittest.main()
