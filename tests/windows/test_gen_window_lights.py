"""Window detection and light placement (tools/gen_window_lights.py) on small WMOs built here, no map data:
which materials are windows, panes and which way they face (exterior groups; interior-only WMOs by open rays),
one light per facade, where the light stands, the placement transform, which places are lived in, the light
budget per 16-yd cell, and the rooms behind each light's glass (WindowRooms.cdbc: the GlassWindows windows its
triangles are in, their shares, the buildings).

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
    """Which windows a light's glass is in (WindowRooms.cdbc): GlassWindows' rectangles in WMO space, each triangle by its
    middle; the shares of the light's glass, by building; frosted windows flagged; glass in no window not listed; the
    shader's fallback seed."""
    WMO = "World\\wmo\\X\\House.wmo"

    @staticmethod
    def quad(o, n, a0, a1, u0, u1):
        """Two triangles of the rectangle a0..a1 along, u0..u1 up the face of normal n, about o (WMO space)."""
        a, u = g.window_frame(n)
        c = [g.add(o, g.add(g.mul(a, x), g.mul(u, y))) for x, y in ((a0, u0), (a1, u0), (a1, u1), (a0, u1))]
        return [c[0], c[1], c[2], c[0], c[2], c[3]]

    @staticmethod
    def window(ordinal, kind, center, normal, hw, hh):
        return (ordinal, kind, center, normal, hw, hh)

    def glass(self, buildings=None):
        """The house's windows: group 3 a clear one (ordinal 4) and a leaded one (ordinal 7) on the wall facing +x, 1.5 yd
        apart; group 9 a frosted one (ordinal 12) and a clear one behind it (ordinal 13, 0.2 yd further in)."""
        x = (1.0, 0.0, 0.0)
        return {"windows": {g.norm_path(self.WMO): {
                    3: g.Windows([self.window(7, 4, (0.0, 4.5, 1.0), x, 1.0, 0.5), self.window(4, 1, (0.0, 1.0, 1.0), x, 1.0, 0.5)]),
                    9: g.Windows([self.window(12, 3, (0.0, 1.0, 5.0), x, 1.0, 0.5), self.window(13, 1, (-0.2, 1.0, 5.0), x, 1.0, 0.5)])}},
                "buildings": buildings if buildings is not None else {(0, 777, 3): 41, (0, 777, g.WHOLE): 40}}

    def light(self, *panes):
        class P:
            def __init__(self, group, points):
                self.group, self.points, self.normal = group, points, (1.0, 0.0, 0.0)

        class C:
            pass
        C.panes = [P(gi, pts) for gi, pts in panes]
        return C

    def test_the_frame_is_the_runtimes(self):
        # along (-n.y, n.x, 0) made unit, up n x along; a skylight along x (as gen_glass's windows: the same function)
        self.assertEqual(g.window_frame((1.0, 0.0, 0.0)), ((0.0, 1.0, 0.0), (0.0, 0.0, 1.0)))
        a, u = g.window_frame((0.0, 1.0, 0.0))
        self.assertEqual((a, u), ((-1.0, 0.0, 0.0), (0.0, 0.0, 1.0)))
        self.assertEqual(g.window_frame((0.0, 0.0, 1.0)), ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0)))

    def test_a_point_finds_the_window_holding_it(self):
        ws = self.glass()["windows"][g.norm_path(self.WMO)]
        # ordinals 4 and 7 (sorted by ordinal: indices 0 and 1)
        # (within 0.03 yd of the rectangle, as the runtime; within 0.3 yd of the plane for a window with no thickness)
        got = ws[3].at([(0.0, 1.0, 1.0), (0.0, 4.5, 1.4), (0.0, 2.02, 1.0), (0.0, 2.04, 1.0), (0.29, 1.0, 1.0), (0.31, 1.0, 1.0)])
        self.assertEqual([ws[3].ordinal[k] if k >= 0 else None for k in got], [4, 7, 4, None, 4, None])
        # two windows hold it: the lower ordinal (the shader's table lists a cell's windows so), whichever plane is nearer
        got = ws[9].at([(0.0, 1.0, 5.0), (-0.15, 1.0, 5.0), (-0.45, 1.0, 5.0)])
        self.assertEqual([ws[9].ordinal[k] for k in got], [12, 12, 13])

    def test_a_lights_triangles_go_to_the_windows_holding_them(self):
        # a light of five 2 x 1 yd panes: in the clear window, in the leaded one, in the frosted one; one beside them (stained,
        # in no window); one in a group with no windows at all
        x = (1.0, 0.0, 0.0)
        c = self.light((3, self.quad((0.0, 1.0, 1.0), x, -1.0, 1.0, -0.5, 0.5)), (3, self.quad((0.0, 4.5, 1.0), x, -1.0, 1.0, -0.5, 0.5)),
                       (9, self.quad((0.0, 1.0, 5.0), x, -1.0, 1.0, -0.5, 0.5)), (3, self.quad((0.0, 8.0, 1.0), x, -1.0, 1.0, -0.5, 0.5)),
                       (5, self.quad((0.0, 1.0, 9.0), x, -1.0, 1.0, -0.5, 0.5)))
        rows = g.light_rooms(c, 0, 777, self.WMO, self.glass())
        self.assertEqual(sorted((b, x_, y, f) for b, x_, y, s, seed, f in rows),
                         [(40, 12, 9, g.CELL_FROSTED), (41, 4, 3, 0), (41, 7, 3, 0)])   # CellX the ordinal, CellY the group
        for b, x_, y, share, seed, flags in rows:
            self.assertAlmostEqual(share, 0.2, places=6)                               # 2 of the light's 10 yd^2 each
            self.assertEqual(seed, g.path_hash("world/wmo/x/house.wmo"))
        # the room glass: 0.4 of the light's glass, in its windows' rows; the rest (frosted 0.2, stained and no window 0.4)
        self.assertAlmostEqual(sum(r[3] for r in rows if not r[5] & g.CELL_FROSTED), 0.4, places=6)

    def test_a_pane_across_two_windows_is_shared_by_its_triangles(self):
        # one pane of four triangles: two in the clear window, two in the leaded one (ordinals 4 and 7)
        x = (1.0, 0.0, 0.0)
        pts = self.quad((0.0, 1.0, 1.0), x, -1.0, 1.0, -0.5, 0.5) + self.quad((0.0, 4.5, 1.0), x, -0.5, 0.5, -0.5, 0.5)
        rows = g.light_rooms(self.light((3, pts)), 0, 777, self.WMO, self.glass())
        self.assertEqual([(r[1], round(r[3], 5)) for r in rows], [(4, round(2.0 / 3.0, 5)), (7, round(1.0 / 3.0, 5))])

    def test_buildings_and_the_default(self):
        x = (1.0, 0.0, 0.0)
        c = self.light((3, self.quad((0.0, 1.0, 1.0), x, -1.0, 1.0, -0.5, 0.5)), (9, self.quad((0.0, 1.0, 5.0), x, -1.0, 1.0, -0.5, 0.5)))
        rows = g.light_rooms(c, 0, 777, self.WMO, self.glass())
        self.assertEqual(sorted((r[0], r[2]) for r in rows), [(40, 9), (41, 3)])   # a city's group row, else the placement's
        rows = g.light_rooms(c, 0, 999, self.WMO, self.glass())
        self.assertEqual({r[0] for r in rows}, {0})                                 # none: the built-in house
        self.assertEqual(g.light_rooms(c, 0, 777, "world\\wmo\\x\\other.wmo", self.glass()), [])   # no windows in that WMO

    def test_a_sliver_is_folded_into_the_buildings_others(self):
        # 2 yd^2 in the clear window, a 0.002 yd^2 sliver in the leaded one: folded (the rooms keep the glass's share)
        x = (1.0, 0.0, 0.0)
        c = self.light((3, self.quad((0.0, 1.0, 1.0), x, -1.0, 1.0, -0.5, 0.5) + self.quad((0.0, 4.5, 1.0), x, 0.0, 0.002, -0.5, 0.5)))
        rows = g.light_rooms(c, 0, 777, self.WMO, self.glass())
        self.assertEqual([(r[1], r[3]) for r in rows], [(4, 1.0)])

    def test_path_hash_is_the_runtimes(self):
        self.assertEqual(g.path_hash("World/WMO/Azeroth/Buildings/Stormwind/Stormwind.wmo"), 4036435193)
        self.assertEqual(g.path_hash(""), 0)

    def test_read_glass(self):
        import tempfile
        from pathlib import Path
        d = Path(tempfile.mkdtemp())
        # GlassWindows (gen_glass.GLASS_WINDOWS) and GlassBuildings (gen_glass.GLASS_BUILDINGS)
        windows = ["u", "s", "u", "u", "u", "f", "f", "f", "f", "f", "f", "f", "f", "u", "u", "s", "f"]
        buildings = ["u", "u", "u", "f", "f", "f", "u", "u", "u", "u", "u", "s", "s"]
        (d / "GlassWindows.cdbc").write_bytes(g.io.build(windows, [
            (1, "world\\wmo\\x\\house.wmo", 3, 0, 4, 0.0, 4.5, 1.0, 1.0, 0.0, 0.0, 1.0, 0.5, 1, 1, "house: group 3 leaded 1 pane", 0.08),
            (2, "world\\wmo\\x\\house.wmo", 3, 1, 1, 0.0, 1.0, 1.0, 1.0, 0.0, 0.0, 1.0, 0.5, 1, 1, "house: group 3 clear 1 pane", 0.08)]))
        (d / "GlassBuildings.cdbc").write_bytes(g.io.build(buildings, [
            (40, 0, 777, 0.0, 0.0, 0.0, g.WHOLE, 1, 5, 0, 12, "world\\wmo\\x\\house.wmo", ""),
            (41, 0, 0, 0.0, 0.0, 0.0, g.WHOLE, 1, 5, 0, 0, "world\\wmo\\x\\house.wmo", "")]))
        glass = g.read_glass(d)
        self.assertEqual(glass["buildings"], {(0, 777, g.WHOLE): 40})    # a WMO's default names no placement
        ws = glass["windows"]["world\\wmo\\x\\house.wmo"][3]
        self.assertEqual((ws.ordinal, ws.kind), ([0, 1], [4, 1]))
        self.assertEqual(ws.at([(0.0, 1.0, 1.2)]).tolist(), [1])
        self.assertAlmostEqual(float(ws.thickness[0]), 0.08, places=6)
        # the runtime's lookup: within 0.03 yd of the rectangle and the window's thickness of its plane
        self.assertEqual(ws.at([(0.07, 1.0, 1.2), (0.09, 1.0, 1.2), (0.0, 2.02, 1.2), (0.0, 2.04, 1.2)]).tolist(), [1, -1, 1, -1])
        # two windows holding a point: the lower ordinal, as the shader's table lists them, whichever plane is nearer
        both = g.Windows([(5, 4, (0.0, 0.0, 1.0), (1.0, 0.0, 0.0), 1.0, 1.0, 0.3),
                          (2, 4, (0.25, 0.0, 1.0), (0.0, 1.0, 0.0), 1.0, 1.0, 0.3)])
        self.assertEqual([both.ordinal[k] for k in both.at([(0.0, 0.05, 1.0), (0.25, 0.01, 1.0)]).tolist()], [2, 2])

    def test_the_shipped_glass_tables_read(self):
        data = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "data")
        if not all(os.path.exists(os.path.join(data, t)) for t in g.GLASS_TABLES):
            self.skipTest("no data folder")
        from pathlib import Path
        glass = g.read_glass(Path(data))
        self.assertIn("world\\wmo\\azeroth\\buildings\\stormwind\\stormwind.wmo", glass["windows"])
        self.assertTrue(any(k in g.ROOM_KINDS for groups in glass["windows"].values() for ws in groups.values() for k in ws.kind))
        self.assertGreater(len(glass["buildings"]), 1000)


if __name__ == "__main__":
    unittest.main()
