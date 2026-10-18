"""Window glass (tools/gen_glass.py) on names, texels and small WMOs built here, no map data: which textures are
candidates and how they are classified (Stormwind's walls are not windows; broken, burnt and abandoned glass is left
out), the glass mask on texels, the panes of a WMO, which way is out (an exterior face, its interior twin, an
interior-only window), the room each pane lights, the placement into world space, and the windows (GlassWindows: which
panes make one window, its rectangle, tiles, ordinals), and titan glass lit from behind (which panes face the sky,
the terrain, a titan hall on Ulduar's map and elsewhere).

Run:  python -m unittest discover -s tests/glass -p "test_*.py"     (from the repository)
"""
import math
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
import gen_glass as g  # noqa: E402

BS = "\\"


def chunk(tag, payload):
    return tag[::-1].encode("latin1") + struct.pack("<I", len(payload)) + payload


def root(textures, groups):
    motx, offsets = b"", []
    for t in textures:
        offsets.append(len(motx))
        motx += t.encode("latin1") + b"\0"
    momt = b"".join(struct.pack("<16I", 0, 0, 0, offsets[i], 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) for i in range(len(textures)))
    mohd = struct.pack("<16I", len(textures), groups, *([0] * 14))
    return chunk("MVER", struct.pack("<I", 17)) + chunk("MOHD", mohd) + chunk("MOTX", motx) + chunk("MOMT", momt)


def group(flags, box, quads):
    """quads: (material, four corners in winding order) each."""
    verts, idx, batches = [], [], []
    for material, corners in quads:
        base, start = len(verts), len(idx)
        verts += corners
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
        batches.append(struct.pack("<6hIHHHBB", 0, 0, 0, 0, 0, 0, start, 6, base, base + 3, 0, material))
    header = struct.pack("<3I6f", 0, 0, flags, *box[0], *box[1]).ljust(0x44, b"\0")
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
WINDOW_IN = "dungeons\\textures\\windows\\mm_elwynn_wnd_int__01.blp"
STONE = "dungeons\\textures\\walls\\mm_strmwnd_wall_01.blp"


class Names(unittest.TestCase):
    def test_candidates(self):
        self.assertTrue(g.is_candidate(WINDOW))
        self.assertTrue(g.is_candidate("dungeons\\textures\\stormwind\\mm_strmwnd_cwindows_02.blp"))
        self.assertTrue(g.is_candidate("world\\expansion02\\doodads\\vrykul\\vrykul_window04.blp"))
        self.assertTrue(g.is_candidate("x\\mm_redridge_stained_01.blp"))
        self.assertTrue(g.is_candidate("x\\eb_dal_allianceglass.blp"))
        self.assertFalse(g.is_candidate(STONE))                         # strmwnd: Stormwind, not a window
        self.assertFalse(g.is_candidate("x\\mm_browndoor_01.blp"))      # "wnd" inside a word
        self.assertFalse(g.is_candidate("x\\stormwindlampglass.blp"))   # the street lamps' glass
        self.assertFalse(g.is_candidate("x\\eb_dalaran_glass_axe.blp")) # a shop sign

    def test_classify(self):
        self.assertEqual(g.classify("x\\mm_strmwnd_cwindows_02.blp")[0], g.STAINED)
        self.assertEqual(g.classify("x\\mm_windows_16.blp")[0], g.STAINED)
        self.assertEqual(g.classify("x\\vrykul_window04.blp")[0], g.FROSTED)
        self.assertEqual(g.classify(WINDOW)[:2], (g.LEADED, g.OUTSIDE_FACE))
        self.assertEqual(g.classify(WINDOW_IN)[:2], (g.LEADED, g.INSIDE_FACE))
        self.assertEqual(g.classify("x\\mm_ogrmr_window_01.blp")[1] & g.MATTE, g.MATTE)
        self.assertEqual(g.classify("x\\jlo_bloodelf_windows_abndn.blp")[0], 0)   # abandoned
        self.assertEqual(g.classify("x\\am_bbay_windowsburn.blp")[0], 0)          # burnt
        self.assertEqual(g.classify("x\\some_new_window.blp"), (0, "unreviewed"))
        self.assertEqual(g.classify("x\\shattrath_window.blp")[0], 0)             # stone, not glass


class Mask(unittest.TestCase):
    def test_dark_frame_bright_glass(self):
        import numpy as np
        frame = np.tile([0.15, 0.1, 0.05], (300, 1))
        glass = np.tile([0.6, 0.7, 0.8], (700, 1))
        px = np.vstack([frame, glass])
        plane, bias, share = g.mask_plane(px, "auto", None)
        m = lambda c: min(1.0, max(0.0, float(np.dot(c, plane)) + bias))
        self.assertEqual(m([0.6, 0.7, 0.8]), 1.0)
        self.assertEqual(m([0.15, 0.1, 0.05]), 0.0)
        self.assertAlmostEqual(share, 0.7, places=2)
        avg, light = g.glass_colours(px, plane, bias, g.CLEAR)
        self.assertAlmostEqual(avg[2], 0.8, places=3)
        self.assertAlmostEqual(max(light), 1.0, places=5)
        self.assertGreater(min(light), 0.7)   # clear glass lets in nearly white light

    def test_hint_picks_the_red_glass_over_brighter_gold_lead(self):
        import numpy as np
        red = np.tile([0.75, 0.1, 0.12], (600, 1))
        gold = np.tile([0.85, 0.75, 0.3], (400, 1))
        px = np.vstack([red, gold])
        plane, bias, _share = g.mask_plane(px, "hint", g.RED)
        m = lambda c: min(1.0, max(0.0, float(np.dot(c, plane)) + bias))
        self.assertEqual(m([0.75, 0.1, 0.12]), 1.0)
        self.assertEqual(m([0.85, 0.75, 0.3]), 0.0)
        _avg, light = g.glass_colours(px, plane, bias, g.STAINED)
        self.assertGreater(light[0], 0.99)
        self.assertLess(light[1], 0.3)        # stained glass lets in its own colour


class Panes(unittest.TestCase):
    def build(self, files):
        d = Path(tempfile.mkdtemp())
        (d / "wmo-windows").mkdir()
        for name, data in files.items():
            (d / "wmo-windows" / name.replace(BS, "#")).write_bytes(data)
        return d

    def test_window_with_inside_twin_and_room(self):
        # A house: the exterior shell (group 0) has a window in the wall x = 0 facing out (+x); the room (group 1,
        # interior) has the same window's inside face, facing in (-x).
        wmo = "world\\wmo\\test\\house.wmo"
        ext = group(0x8, ((-6, -4, 0), (0.5, 4, 5)), [(0, wall_x(0.0, -1, 1, 1, 3, out=True))])
        room = group(0x2000, ((-6, -4, 0), (0.2, 4, 5)), [(1, wall_x(-0.1, -1, 1, 1, 3, out=False))])
        scan = self.build({wmo: root([WINDOW, WINDOW_IN], 2), wmo[:-4] + "_000.wmo": ext, wmo[:-4] + "_001.wmo": room})
        panes, why = g.wmo_panes(scan, wmo)
        self.assertEqual(why, "")
        self.assertEqual(len(panes), 1)       # one window, not two
        p = panes[0]
        self.assertAlmostEqual(p.out[0], 1.0, places=5)
        self.assertAlmostEqual(p.hw, 1.0, places=5)
        self.assertAlmostEqual(p.hh, 1.0, places=5)
        for got, want in zip(p.room[0] + p.room[1], (-6, -4, 0, 0.2, 4, 5)):
            self.assertAlmostEqual(got, want, places=5)   # the interior group's box: the room it lights

    def test_interior_only_window_faces_out_by_rays(self):
        # A hut that is all interior (Orgrimmar's): a closed box with a window in its +x wall; its face points in.
        wmo = "world\\wmo\\test\\hut.wmo"
        quads = [(1, wall_x(0.0, -1, 1, 1, 3, out=False))]
        walls =[wall_x(-6.0, -4, 4, 0, 5, out=True), [(-6, 4, 0), (0, 4, 0), (0, 4, 5), (-6, 4, 5)],
                 [(-6, -4, 5), (0, -4, 5), (0, -4, 0), (-6, -4, 0)], [(-6, -4, 5), (-6, 4, 5), (0, 4, 5), (0, -4, 5)],
                 [(-6, -4, 0), (0, -4, 0), (0, 4, 0), (-6, 4, 0)],
                 [(0, -4, 0), (0, -1, 0), (0, -1, 5), (0, -4, 5)], [(0, 1, 0), (0, 4, 0), (0, 4, 5), (0, 1, 5)]]
        quads += [(0, w) for w in walls]
        hut = group(0x2000, ((-6, -4, 0), (0, 4, 5)), quads)
        scan = self.build({wmo: root([STONE, "x\\mm_ogrmr_window_01.blp"], 1), wmo[:-4] + "_000.wmo": hut})
        panes, _why = g.wmo_panes(scan, wmo)
        self.assertEqual(len(panes), 1)
        self.assertAlmostEqual(panes[0].out[0], 1.0, places=5)   # out of the hut, though its face points in

    def test_exterior_only_window_without_room_is_left_out(self):
        wmo = "world\\wmo\\test\\facade.wmo"
        ext = group(0x8, ((-1, -4, 0), (0.5, 4, 5)), [(0, wall_x(0.0, -1, 1, 1, 3, out=True))])
        scan = self.build({wmo: root([WINDOW], 1), wmo[:-4] + "_000.wmo": ext})
        panes, _why = g.wmo_panes(scan, wmo)
        self.assertEqual(panes, [])

    def test_broken_glass_has_no_panes(self):
        wmo = "world\\wmo\\test\\ruin.wmo"
        ext = group(0x8, ((-1, -4, 0), (0.5, 4, 5)), [(0, wall_x(0.0, -1, 1, 1, 3, out=True))])
        scan = self.build({wmo: root(["x\\jlo_bloodelf_windows_abndn.blp"], 1), wmo[:-4] + "_000.wmo": ext})
        self.assertEqual(g.wmo_panes(scan, wmo), ([], "no glass"))

    def test_window_painted_into_a_wall_has_no_panes(self):
        wmo = "world\\wmo\\test\\elfwall.wmo"
        ext = group(0x8, ((-6, -4, 0), (0.5, 4, 5)), [(0, wall_x(0.0, -4, 4, 0, 5, out=True))])
        room = group(0x2000, ((-6, -4, 0), (0.2, 4, 5)), [])
        scan = self.build({wmo: root(["x\\jlo_bloodelf_wallwindow_01.blp"], 2), wmo[:-4] + "_000.wmo": ext, wmo[:-4] + "_001.wmo": room})
        self.assertEqual(g.wmo_panes(scan, wmo), ([], "no glass"))

    def test_skylight_axes(self):
        u, w = g.pane_axes((0.0, 0.0, 1.0))
        self.assertAlmostEqual(abs(u[0]), 1.0, places=5)
        self.assertAlmostEqual(abs(w[1]), 1.0, places=5)
        u, w = g.pane_axes((1.0, 0.0, 0.0))
        self.assertAlmostEqual(u[1], 1.0, places=5)   # along the wall
        self.assertAlmostEqual(w[2], 1.0, places=5)   # up it

    def test_a_pane_keeps_its_triangles(self):
        grp = g.wl.parse_group(group(0x8, ((-6, -4, 0), (0.5, 4, 5)), [(0, wall_x(0.0, -1, 1, 1, 3, out=True))]))
        p, = g.group_panes(grp, 0, {0: WINDOW}, True)
        self.assertEqual(len(p.points), 6)                 # two triangles, three corners each
        self.assertEqual(p.areas, {WINDOW: p.area})


CLEAR_TEX = "dungeons\\textures\\windows\\am_bbay_windows.blp"
FROST_TEX = "dungeons\\textures\\windows\\mm_irnfg_windows_01.blp"
ROOM_MATS, FROST_MATS = {0: WINDOW, 2: CLEAR_TEX}, {1: FROST_TEX}   # materials 0 leaded, 1 frosted, 2 clear


def wall_y(y, x0, x1, z0, z1):
    """A quad in the plane y, facing +y."""
    return [(x1, y, z0), (x0, y, z0), (x0, y, z1), (x1, y, z1)]


def windows_of(groups):
    """The windows of groups built here ({group index: (flags, quads)}), as wmo_windows makes them from a scan."""
    sorts = [[], []]
    for gi, (flags, quads) in sorted(groups.items()):
        grp = g.wl.parse_group(group(flags, ((-50, -50, -50), (50, 50, 50)), quads))
        exterior = bool(flags & 0x8) and not flags & 0x2000
        for k, mats in enumerate((ROOM_MATS, FROST_MATS)):
            sorts[k] += g.group_panes(grp, gi, mats, exterior, g.W_MIN_AREA, tile=False)
    return g.windows_of(sorts)


def corners_inside(test, w):
    """Every corner of the window's glass inside its rectangle, in the runtime's frame of its stored normal."""
    a, u = g.wl.window_frame(w.normal)
    for p in w.panes:
        for q in p.points:
            r = g.wl.sub(q, w.center)
            test.assertLessEqual(abs(g.wl.dot(r, a)), w.hw)
            test.assertLessEqual(abs(g.wl.dot(r, u)), w.hh)


class Windows(unittest.TestCase):
    """GlassWindows: which panes make one window (mullions, a facade's separate windows, the size cap, kinds), the
    rectangle in the runtime's frame (float32, every corner inside), big sheets cut into tiles, the ordinals, the twin
    face's way out and the table's rows."""

    def test_mullioned_panes_are_one_window(self):
        # four lights of one window facing +x, 0.15 yd mullions between them
        quads = [(0, wall_x(0.0, y0, y0 + 0.8, z0, z0 + 1.0)) for y0 in (-0.875, 0.075) for z0 in (1.0, 2.15)]
        ws = windows_of({0: (0x8, quads)})
        self.assertEqual(len(ws), 1)
        w = ws[0]
        self.assertEqual((len(w.panes), w.kind, w.exterior), (4, g.LEADED, True))
        self.assertEqual(tuple(round(c, 4) for c in w.center), (0.0, 0.0, 2.075))
        self.assertEqual(tuple(round(c, 4) for c in w.normal), (1.0, 0.0, 0.0))
        self.assertAlmostEqual(w.hw, 0.875 + g.W_MARGIN, places=5)
        self.assertAlmostEqual(w.hh, 1.075 + g.W_MARGIN, places=5)
        corners_inside(self, w)

    def test_a_facades_windows_08_yd_apart_stay_two(self):
        ws = windows_of({0: (0x8, [(0, wall_x(0.0, 0.0, 1.5, 1, 3)), (0, wall_x(0.0, 2.3, 3.8, 1, 3))])})
        self.assertEqual([len(w.panes) for w in ws], [1, 1])
        # stacked 0.8 apart (two storeys): two as well; 0.3 apart (a transom): one
        self.assertEqual(len(windows_of({0: (0x8, [(0, wall_x(0.0, 0, 1.5, 1, 3)), (0, wall_x(0.0, 0, 1.5, 3.8, 5))])})), 2)
        self.assertEqual(len(windows_of({0: (0x8, [(0, wall_x(0.0, 0, 1.5, 1, 3)), (0, wall_x(0.0, 0, 1.5, 3.3, 4))])})), 1)

    def test_corner_to_corner_or_off_the_plane_is_not_one_window(self):
        diagonal = [(0, wall_x(0.0, 0, 1, 1, 2)), (0, wall_x(0.0, 1.1, 2.1, 2.1, 3.1))]   # near, but neither beside nor above
        self.assertEqual(len(windows_of({0: (0x8, diagonal)})), 2)
        behind = [(0, wall_x(0.0, 0, 1, 1, 2)), (0, wall_x(-0.3, 1.1, 2.1, 1, 2))]        # 0.3 yd behind its plane
        self.assertEqual(len(windows_of({0: (0x8, behind)})), 2)
        turned = [(0, wall_x(0.0, 0, 1, 1, 2)), (0, [(0.1, 1.1, 1), (0.3, 2.1, 1), (0.3, 2.1, 2), (0.1, 1.1, 2)])]
        self.assertEqual(len(windows_of({0: (0x8, turned)})), 2)                          # 11 degrees apart

    def test_a_long_row_never_merges_past_the_cap(self):
        # twenty 1-yd lights 0.2 yd apart: 23.8 yd of glass
        ws = windows_of({0: (0x8, [(0, wall_x(0.0, 1.2 * k, 1.2 * k + 1.0, 1, 3)) for k in range(20)])})
        self.assertGreater(len(ws), 3)
        self.assertEqual(sum(len(w.panes) for w in ws), 20)
        for w in ws:
            self.assertLessEqual(2 * w.hw, g.W_WIDTH + 2 * g.W_MARGIN + 1e-6)
            self.assertIsNone(w.tile)
            corners_inside(self, w)

    def test_frosted_never_merges_with_clear(self):
        ws = windows_of({0: (0x8, [(2, wall_x(0.0, 0, 1, 1, 2)), (1, wall_x(0.0, 1.1, 2.1, 1, 2))])})
        self.assertEqual(sorted(w.kind for w in ws), [g.CLEAR, g.FROSTED])
        # clear and leaded glass do share a window: the kind with more glass
        ws = windows_of({0: (0x8, [(2, wall_x(0.0, 0, 1, 1, 2)), (0, wall_x(0.0, 1.1, 2.6, 1, 2))])})
        self.assertEqual([(len(w.panes), w.kind) for w in ws], [(2, g.LEADED)])

    def test_overlapping_and_duplicated_faces_are_one_window(self):
        # an exact copy shares the original's corners: one pane already; a copy 0.01 yd in front of it: one window
        self.assertEqual([len(w.panes) for w in windows_of({0: (0x8, [(0, wall_x(0.0, 0, 2, 1, 3))] * 2)})], [1])
        dup = [(0, wall_x(0.0, 0, 2, 1, 3)), (0, wall_x(0.01, 0, 2, 1, 3))]
        self.assertEqual([len(w.panes) for w in windows_of({0: (0x8, dup)})], [2])
        # overlapping pieces too big together for the cap: still one window (a pixel finds one)
        ws = windows_of({0: (0x8, [(0, wall_x(0.0, 0, 5, 0, 6)), (0, wall_x(0.0, 3, 8, 0, 6))])})
        self.assertEqual([len(w.panes) for w in ws], [2])

    def test_twins_are_one_window(self):
        # a sheet's two faces 0.02 yd apart, one winding out and one in (an exterior group's glass as it winds), on
        # both sides of a house: one window each, facing away from the group's glass's middle (out of the house)
        quads = [(0, wall_x(0.0, 0, 2, 1, 3)), (0, wall_x(-0.02, 0, 2, 1, 3, out=False)),
                 (0, wall_x(-6.0, 0, 2, 1, 3, out=False)), (0, wall_x(-5.98, 0, 2, 1, 3))]
        ws = windows_of({0: (0x8, quads)})
        self.assertEqual(sorted((round(w.center[0], 2), w.normal, len(w.panes)) for w in ws),
                         [(-5.99, (-1.0, 0.0, 0.0), 2), (-0.01, (1.0, 0.0, 0.0), 2)])
        for w in ws:
            corners_inside(self, w)

        # a window and a piece of glass standing in it at 15 degrees (a facet): one; at 40 degrees: two
        def piece(degrees):
            n = (math.cos(math.radians(degrees)), math.sin(math.radians(degrees)), 0.0)
            a = g.wl.window_frame(n)[0]
            return [g.wl.add((0.02, 1.0, 0.0), g.wl.add(g.wl.mul(a, x), (0.0, 0.0, z)))
                    for x, z in ((-0.4, 1.5), (0.4, 1.5), (0.4, 2.5), (-0.4, 2.5))]
        self.assertEqual(len(windows_of({0: (0x8, [(0, wall_x(0.0, 0, 2, 1, 3)), (0, piece(15))])})), 1)
        self.assertEqual(len(windows_of({0: (0x8, [(0, wall_x(0.0, 0, 2, 1, 3)), (0, piece(40))])})), 2)

    def test_a_flat_panes_lookup_is_thin(self):
        w, = windows_of({0: (0x8, [(0, wall_x(0.0, 0, 2, 1, 3))])})
        self.assertEqual(w.thickness, g.f32_up(g.W_SLAB_MIN))
        self.assertTrue(w.holds((0.07, 1.0, 2.0)))
        self.assertFalse(w.holds((0.1, 1.0, 2.0)))                          # a neighbour at a slant 0.1 yd off isn't in it
        self.assertTrue(w.holds((0.0, 2.0 + g.W_LOOKUP, 2.0)))
        self.assertFalse(w.holds((0.0, 2.0 + g.W_LOOKUP + 0.01, 2.0)))

    def test_the_frame_is_the_runtimes(self):
        # along (-n.y, n.x, 0) made unit; up n x along; x for a skylight (n.x^2 + n.y^2 < 1e-4)
        self.assertEqual(g.wl.window_frame((1.0, 0.0, 0.0)), ((0.0, 1.0, 0.0), (0.0, 0.0, 1.0)))
        self.assertEqual(g.wl.window_frame((0.0, 1.0, 0.0)), ((-1.0, 0.0, 0.0), (0.0, 0.0, 1.0)))
        self.assertEqual(g.wl.window_frame((0.0, 0.0, 1.0)), ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0)))
        self.assertEqual(g.wl.window_frame((0.005, 0.0, 0.99998))[0], (1.0, 0.0, 0.0))
        # a skylight: its rectangle along x and y
        sky = [(0, 0, 5), (2, 0, 5), (2, 1, 5), (0, 1, 5)]
        w, = windows_of({0: (0x8, [(0, sky)])})
        self.assertEqual(w.normal, (0.0, 0.0, 1.0))
        self.assertEqual(tuple(round(c, 4) for c in w.center), (1.0, 0.5, 5.0))
        self.assertAlmostEqual(w.hw, 1.0 + g.W_MARGIN, places=5)
        self.assertAlmostEqual(w.hh, 0.5 + g.W_MARGIN, places=5)
        # a window facing (0.6, 0.8, 0) far from the origin, tilted a little: every corner inside, all float32
        n = g.wl.unit((0.6, 0.8, 0.05))
        a, u = g.wl.window_frame(n)
        o = (523.17, -311.9, 41.3)
        quad = [g.wl.add(o, g.wl.add(g.wl.mul(a, x), g.wl.mul(u, y))) for x, y in ((-0.9, -1.3), (0.9, -1.3), (0.9, 1.3), (-0.9, 1.3))]
        w, = windows_of({0: (0x8, [(0, quad)])})
        self.assertGreater(g.wl.dot(w.normal, n), 0.99999)
        corners_inside(self, w)
        for v in w.center + w.normal + (w.hw, w.hh):
            self.assertEqual(g.f32(v), v)            # as the table stores them
        self.assertAlmostEqual(w.hw, 0.9 + g.W_MARGIN, places=3)
        self.assertAlmostEqual(w.hh, 1.3 + g.W_MARGIN, places=3)

    def test_a_big_sheet_is_cut_into_tiles(self):
        # 3 x 15 yd of glass (a tall tower window): three tiles 5 yd tall, the sheet's normal, side by side, all the glass
        ws = windows_of({0: (0x8, [(0, wall_x(0.0, 0, 3, 0, 15))])})
        self.assertEqual([w.tile for w in ws], [(1, 3), (2, 3), (3, 3)])
        self.assertAlmostEqual(sum(w.area for w in ws), 45.0, places=4)
        for w in ws:
            self.assertEqual(w.normal, (1.0, 0.0, 0.0))
            self.assertAlmostEqual(w.hh, 2.5 + g.W_MARGIN, places=4)
            corners_inside(self, w)
        for lower, upper in zip(ws, ws[1:]):
            self.assertLessEqual(abs(upper.center[2] - lower.center[2]) - (upper.hh + lower.hh), 1e-6)
            self.assertGreaterEqual(abs(upper.center[2] - lower.center[2]) - (upper.hh + lower.hh), -2 * g.W_MARGIN - 1e-6)
        # 10 yd tall (one pane, under W_CUT): one window, over the merging cap but whole
        self.assertEqual([w.tile for w in windows_of({0: (0x8, [(0, wall_x(0.0, 0, 3, 0, 10))])})], [None])

    def test_ordinals_are_steady_and_unique(self):
        groups = {0: (0x8, [(0, wall_x(0.0, 0, 1, 4, 5)), (0, wall_x(0.0, 3, 4, 1, 2)), (0, wall_x(0.0, 6, 7, 1, 2))]),
                  2: (0x8, [(1, wall_y(0.0, 0, 1, -3, -2))])}
        ws = windows_of(groups)
        self.assertEqual([w.ordinal for w in ws], [0, 1, 2, 3])
        # by group, then the middle bottom to top, then x, then y
        self.assertEqual([(w.group, round(w.center[1], 1), round(w.center[2], 1)) for w in ws],
                         [(0, 3.5, 1.5), (0, 6.5, 1.5), (0, 0.5, 4.5), (2, 0.0, -2.5)])
        # the panes in any order: the same windows, the same numbers
        shuffled = {gi: (fl, list(reversed(q))) for gi, (fl, q) in groups.items()}
        self.assertEqual([(w.ordinal, w.center) for w in windows_of(shuffled)], [(w.ordinal, w.center) for w in ws])

    def build(self, files, placed):
        d = Path(tempfile.mkdtemp())
        (d / "wmo-windows").mkdir()
        for name, data in files.items():
            (d / "wmo-windows" / name.replace(BS, "#")).write_bytes(data)
        (d / "maps.tsv").write_text("0\tAzeroth\n", newline="")
        rows = ["source\twmo\tuniqueId\tpx\tpy\tpz\trx\try\trz"]
        for uid, wmo in enumerate(placed):
            rows.append("world\\maps\\azeroth\\azeroth_32_32.adt\t%s\t%d\t17000.0\t50.0\t17100.0\t0\t0\t0" % (wmo, uid + 1))
        (d / "adt_wmos.tsv").write_text("\n".join(rows) + "\n", newline="")
        return d

    def test_the_inside_face_faces_out_and_the_table(self):
        # a house: the window's outside face in the exterior group 0, its inside face (facing in) in the interior group 1;
        # stained glass beside it has no window
        wmo = "World\\WMO\\Test\\House.wmo"
        stained = "x\\mm_stained_01.blp"
        ext = group(0x8, ((-6, -4, 0), (0.5, 4, 5)), [(0, wall_x(0.0, -1, 1, 1, 3, out=True)), (2, wall_x(0.0, 2, 3, 1, 3))])
        room = group(0x2000, ((-6, -4, 0), (0.2, 4, 5)), [(1, wall_x(-0.1, -1, 1, 1, 3, out=False))])
        scan = self.build({wmo: root([WINDOW, WINDOW_IN, stained], 2), wmo[:-4] + "_000.wmo": ext, wmo[:-4] + "_001.wmo": room},
                          [wmo.lower(), wmo.lower()])
        ws, why = g.wmo_windows(scan, wmo.lower())
        self.assertEqual(why, "")
        self.assertEqual([(w.group, w.ordinal, w.exterior) for w in ws], [(0, 0, True), (1, 1, False)])
        self.assertEqual(ws[1].normal, (1.0, 0.0, 0.0))     # out of the house, though its triangles face in
        rows, report = g.windows_table(scan)
        self.assertEqual(len(rows), 2)                      # per WMO file, not per placement
        r = rows[0]
        self.assertEqual(len(r), len(g.GLASS_WINDOWS))
        self.assertEqual(r[:5], [1, "world\\wmo\\test\\house.wmo", 0, 0, g.LEADED])
        self.assertEqual(r[13:16], [g.WIN_EXTERIOR, 1, "house: group 0 leaded 1 pane"])
        self.assertEqual(r[16], g.f32_up(g.W_SLAB_MIN))                      # a flat pane's lookup: thin
        self.assertEqual(rows[1][13], 0)
        _, raw, string_at = g.io.read(g.io.build(g.GLASS_WINDOWS, [tuple(x) for x in rows]))
        self.assertEqual(string_at(raw[0][1]), "world\\wmo\\test\\house.wmo")
        self.assertEqual([g.io.as_float(v) for v in raw[0][5:13]], r[5:13])   # float32 exactly
        self.assertEqual(g.io.as_float(raw[0][16]), r[16])


def root_with_groups(textures, names, lit=(), unlit=(), wmo_id=0, group_flags=None):
    """A root with MOGI and MOGN (the groups' names, their flags: exterior unless `group_flags` says), its materials
    SIDN-lit where `lit` says and unlit (MOMT 0x1) where `unlit` does, MOHD's wmoID."""
    motx, offsets = b"", []
    for t in textures:
        offsets.append(len(motx))
        motx += t.encode("latin1") + b"\0"
    momt = b"".join(struct.pack("<16I", (0x10 if i in lit else 0) | (0x1 if i in unlit else 0), 0, 0, offsets[i],
                                0x00FFC080 if i in lit else 0, *([0] * 11)) for i in range(len(textures)))
    mohd = struct.pack("<16I", len(textures), len(names), 0, 0, 0, 0, 0, 0, wmo_id, *([0] * 7))
    mogn, mogi = b"\0\0", b""
    for k, n in enumerate(names):
        mogi += struct.pack("<I6fi", group_flags[k] if group_flags else 0x8, -5, -5, 0, 5, 5, 5, len(mogn))
        mogn += n.encode("latin1") + b"\0"
    return (chunk("MVER", struct.pack("<I", 17)) + chunk("MOHD", mohd) + chunk("MOTX", motx) + chunk("MOMT", momt) +
            chunk("MOGN", mogn) + chunk("MOGI", mogi))


class Rooms(unittest.TestCase):
    """Who lives behind the glass: the class and palette of a placement, its seed, the styles, the night's schedule, the
    layouts' thresholds and the buildings table of a small scan."""

    def test_classes_by_path(self):
        B = "world\\wmo\\azeroth\\buildings\\"
        cases = {
            B + "goldshireinn\\goldshireinn.wmo": g.INN, B + "redridge_inn\\redridge_inn.wmo": g.INN,
            B + "duskwood_blacksmith\\duskwood_blacksmith.wmo": g.SHOP, B + "humantwostory\\humantwostory.wmo": g.HOUSE,
            B + "human_farm\\farm.wmo": g.FARM, B + "guardtower\\guardtower.wmo": g.KEEP, B + "magetower\\magetower.wmo": g.TOWER,
            B + "chapel\\redridgechapel.wmo": g.TEMPLE, B + "stormwind\\stormwind.wmo": g.CITY,
            "world\\wmo\\transports\\transport_ship\\transportship.wmo": g.SHIP,
            "world\\wmo\\northrend\\buildings\\vrykul\\nd_vrykul_dwelling_03\\nd_vrykul_dwelling_03_a.wmo": g.HOUSE,
            "world\\wmo\\dungeon\\md_ruinedkeep\\somewhere.wmo": g.KEEP, "world\\wmo\\x\\monsterlogmachine_01.wmo": g.OTHER,
        }
        for path, cls in cases.items():
            self.assertEqual(g.room_class(path), cls, path)

    def test_inn_is_a_word(self):
        self.assertEqual(g.words_class("shop_skinning"), g.SHOP)      # skinning has "inn" inside it
        self.assertEqual(g.words_class("TowerInnards"), g.KEEP)       # so do innards
        self.assertEqual(g.words_class("innbedroom2"), g.INN)
        self.assertEqual(g.words_class("WallInn01"), g.INN)
        self.assertEqual(g.words_class("The Slaughtered Lamb"), g.INN)

    def test_city_groups(self):
        self.assertEqual(g.group_class("Trade District", "Stormwind City"), g.SHOP)
        self.assertEqual(g.group_class("shopB18", ""), g.SHOP)
        self.assertEqual(g.group_class("city01", "Mage Quarter"), g.TOWER)
        self.assertEqual(g.group_class("HB02", "Stormwind City"), g.CITY)
        self.assertEqual(g.group_class("interior_inn", ""), g.INN)

    def test_palettes_by_culture_and_place(self):
        H = "world\\wmo\\azeroth\\buildings\\humantwostory\\humantwostory.wmo"
        self.assertEqual(g.palette_of("world\\wmo\\northrend\\buildings\\vrykul\\nd_vrykul_longhouse.wmo", 571), g.VRYKUL)
        self.assertEqual(g.palette_of("world\\wmo\\lorderon\\buildings\\silvermoon\\bloodelf_smallhouse\\x.wmo", 530), g.ELVEN)
        self.assertEqual(g.palette_of("world\\wmo\\northrend\\dalaran\\nd_dalaran.wmo", 571), g.DALARAN)
        self.assertEqual(g.palette_of("world\\wmo\\azeroth\\buildings\\stranglethorn_bootybay\\bootybay.wmo", 0), g.GOBLIN)
        self.assertEqual(g.palette_of("world\\wmo\\northrend\\buildings\\forsaken\\nd_forsaken_house.wmo", 571), g.FORSAKEN)
        self.assertEqual(g.palette_of(H, 0, ["Darkshire", "Duskwood"]), g.DUSK)
        self.assertEqual(g.palette_of(H, 0, ["Brill", "Tirisfal Glades"]), g.FORSAKEN)
        self.assertEqual(g.palette_of(H, 0, ["Tyr's Hand", "Eastern Plaguelands"]), g.SCARLET)
        self.assertEqual(g.palette_of(H, 1, ["Ratchet", "The Barrens"]), g.GOBLIN)
        self.assertEqual(g.palette_of(H, 571, ["Valgarde", "Howling Fjord"]), g.NORTHERN)
        self.assertEqual(g.palette_of(H, 530, ["Honor Hold", "Hellfire Peninsula"]), g.HELLFIRE)
        self.assertEqual(g.palette_of(H, 0, ["Goldshire", "Elwynn Forest"]), g.HUMAN)

    def test_seeds_are_steady_and_distinct(self):
        self.assertEqual(g.seed_of(0, 10047), 3682108440)            # a CRC: the same on every run
        self.assertEqual(g.seed_of(0, 10047, 93), 1961690026)
        self.assertNotEqual(g.seed_of(0, 10047), g.seed_of(571, 10047))
        # the runtime's PathHash (tests/glass GlassTests.cpp checks the same number)
        self.assertEqual(g.hash_path("World/WMO/Azeroth/Buildings/Stormwind/Stormwind.wmo"), 4036435193)

    def test_styles(self):
        v = g.style_values(g.SHOP, g.GOBLIN)
        self.assertEqual(len(v), 21)
        weights = v[15:19]
        self.assertAlmostEqual(weights[0], 0.55 * 2.2, places=4)      # a goblin shop: cluttered shelves
        self.assertAlmostEqual(v[12], round(1.1 * 1.1, 4), places=4)  # brightness: class x palette
        self.assertEqual(v[19:], (-1.0, 1.0))                          # shops close early
        self.assertEqual(g.style_values(g.INN, g.HUMAN)[19:], (2.5, 0.55))

    def test_layout_thresholds(self):
        self.assertEqual([round(t, 6) for t in g.layout_thresholds((0.25, 0.35, 0.15, 0.25))], [0.25, 0.6, 0.75])
        self.assertEqual(g.layout_thresholds((0, 0, 0, 0)), [0.0, 0.0, 0.0])
        self.assertEqual([round(t, 6) for t in g.layout_thresholds((0, 1, 0, 0))], [0.0, 1.0, 1.0])

    def test_lights_out(self):
        # The same points as env/GlassModel's LightsOutShare (tests/glass GlassTests.cpp).
        for hour, want in ((20, 0.08), (22.5, 0.29), (0, 0.5), (2, 0.72), (4.5, 0.78), (6, 0.6)):
            self.assertAlmostEqual(g.lights_out(hour), want, places=5)
        self.assertAlmostEqual(g.lights_out(0, 2.5, 0.55), 0.15, places=5)   # an inn stays lit late
        self.assertAlmostEqual(g.lights_out(3, 2.5, 0.55), 0.55, places=5)
        self.assertGreater(g.lights_out(22, -1.0), g.lights_out(22) + 0.1)  # a shop closes early

    def test_dark_places_and_empty_buildings(self):
        self.assertTrue(g.dark_place([], "world\\wmo\\x\\ruined_house.wmo"))
        self.assertTrue(g.dark_place(["Pyrewood Village", "Silverpine Forest"], "world\\wmo\\x\\house.wmo"))
        self.assertFalse(g.dark_place(["Goldshire", "Elwynn Forest"], "world\\wmo\\x\\house.wmo"))
        self.assertFalse(g.empty_building(5, g.HOUSE, True, False))     # the client lights its windows: lived in
        self.assertFalse(g.empty_building(5, g.INN, False, False))
        self.assertTrue(g.empty_building(5, g.HOUSE, False, False))     # 5 < 80 of 1000
        self.assertFalse(g.empty_building(500, g.HOUSE, False, False))

    def test_buildings_table(self):
        d = Path(tempfile.mkdtemp())
        (d / "wmo-windows").mkdir()
        house = "world\\wmo\\test\\humantwostory.wmo"
        city = "world\\wmo\\test\\stormwind.wmo"
        files = {
            house: root_with_groups([WINDOW], ["Room"], lit=(0,)),
            city: root_with_groups([STONE, WINDOW], ["city01", "shopB18"]),
            city[:-4] + "_000.wmo": group(0x8, ((-6, -4, 0), (0.5, 4, 5)), [(0, wall_x(0.0, -1, 1, 1, 3, out=True))]),
            city[:-4] + "_001.wmo": group(0x8, ((-6, -4, 0), (0.5, 4, 5)), [(1, wall_x(0.0, -1, 1, 1, 3, out=True))]),
        }
        for name, data in files.items():
            (d / "wmo-windows" / name.replace(BS, "#")).write_bytes(data)
        (d / "maps.tsv").write_text("0\tAzeroth\n", newline="")
        rows = ["source\twmo\tuniqueId\tpx\tpy\tpz\trx\try\trz"]
        for uid, wmo, pos in ((1, house, (17000.0, 50.0, 17100.0)), (2, house, (17040.0, 50.0, 17100.0)), (3, city, (16000.0, 60.0, 16000.0))):
            rows.append("world\\maps\\azeroth\\azeroth_32_32.adt\t%s\t%d\t%.1f\t%.1f\t%.1f\t0\t0\t0" % (wmo, uid, *pos))
        (d / "adt_wmos.tsv").write_text("\n".join(rows) + "\n", newline="")
        styles, buildings, report = g.buildings_table(d)
        placed = [r for r in buildings if r[2] != 0 and r[6] == g.WHOLE]
        groups = [r for r in buildings if r[2] != 0 and r[6] != g.WHOLE]
        defaults = [r for r in buildings if r[2] == 0]
        self.assertEqual(len(placed), 3)
        self.assertEqual(len(groups), 1)                        # only the city's group with glass
        self.assertEqual(groups[0][6], 1)
        self.assertEqual(len(defaults), 2)                      # one per WMO and map
        by_uid = {r[2]: r for r in placed}
        # the placement's origin, as gen_window_lights places it (MAX - z, MAX - x, y)
        self.assertAlmostEqual(by_uid[1][3], 32.0 * 1600.0 / 3.0 - 17100.0, places=3)
        self.assertAlmostEqual(by_uid[1][4], 32.0 * 1600.0 / 3.0 - 17000.0, places=3)
        self.assertAlmostEqual(by_uid[1][5], 50.0, places=3)
        style_class = {s[0]: s[1] for s in styles}
        self.assertEqual(style_class[by_uid[1][7]], g.HOUSE)
        self.assertEqual(style_class[by_uid[3][7]], g.CITY)
        self.assertEqual(style_class[groups[0][7]], g.SHOP)    # shopB18
        self.assertTrue(by_uid[3][9] & g.B_CITY and groups[0][9] & g.B_CITY)
        self.assertTrue(by_uid[1][9] & g.B_SELF_LIT)           # its window material is SIDN-lit
        self.assertFalse(by_uid[1][9] & g.B_EMPTY)             # ... so somebody lives there
        self.assertNotEqual(by_uid[1][8], by_uid[2][8])         # two houses, two seeds
        self.assertEqual(by_uid[1][11], house)
        self.assertEqual(sorted({r[0] for r in buildings}), list(range(1, len(buildings) + 1)))

    def test_ironforge_classes(self):
        # the client's sub-zone for a group (WMOAreaTable), a smithy by its name; the Commons when unknown
        self.assertEqual(g.ironforge_class("Interior_K306", "The Great Forge"), g.FORGE)
        self.assertEqual(g.ironforge_class("InteriorSmithA01", "Ironforge"), g.FORGE)
        self.assertEqual(g.ironforge_class("Ironforge", "Ironforge"), g.COMMONS)
        self.assertEqual(g.ironforge_class("lib_int", "The High Seat"), g.COMMONS)
        self.assertEqual(g.ironforge_class("Ironforge", "Hall of Explorers"), g.COMMONS)
        self.assertEqual(g.ironforge_class("B_warriors", "The Military Ward"), g.QUARTER)
        self.assertEqual(g.ironforge_class("Rogue", "The Mystic Ward"), g.QUARTER)
        self.assertEqual(g.ironforge_class("Interior_D", "The Forlorn Cavern"), g.CAVERN)
        self.assertEqual(g.ironforge_class("", ""), g.COMMONS)
        # their hours: the forge never sleeps, the Commons mostly lit, the wards' homes gently, the cavern darker
        at3 = [g.lights_out(3, g.CLASSES[c].late, g.CLASSES[c].cap) for c in (g.FORGE, g.COMMONS, g.QUARTER, g.CAVERN)]
        self.assertEqual([round(x, 2) for x in at3], [0.04, 0.25, 0.45, 0.77])
        self.assertLess(at3[2], g.lights_out(3))                     # gentler than a house's
        self.assertGreater(g.lights_out(23, g.CLASSES[g.CAVERN].late, g.CLASSES[g.CAVERN].cap), g.lights_out(23))

    def test_lit_frosted_glass_and_dungeons(self):
        frosted = "dungeons\\textures\\windows\\mm_irnfg_windows_01.blp"
        info = g.parse_root_all(root_with_groups([frosted, WINDOW, frosted], ["a"], lit=(2,), unlit=(0, 1)))
        self.assertEqual(sorted(g.lit_frosted(info)), [0, 2])         # unlit or SIDN frosted; not the unlit leaded one
        self.assertEqual(g.lit_frosted(g.parse_root_all(root_with_groups([frosted], ["a"]))), {})
        self.assertTrue(g.is_dungeon("world\\wmo\\dungeon\\ol_tempestkeep\\tk_exterior.wmo"))
        self.assertFalse(g.is_dungeon("world\\wmo\\khazmodan\\cities\\ironforge\\ironforge.wmo"))

    def test_ironforge_buildings(self):
        # a small Ironforge: an interior group (the Great Forge's, by the WMOAreaTable) and an exterior one (the Forlorn
        # Cavern's, made up: an outside group keeps the sky), its glass frosted and unlit; and a house before it
        d = Path(tempfile.mkdtemp())
        (d / "wmo-windows").mkdir()
        frosted = "dungeons\\textures\\windows\\mm_irnfg_windows_01.blp"
        house = "world\\wmo\\test\\humantwostory.wmo"
        city = "world\\wmo\\khazmodan\\cities\\ironforge\\ironforge.wmo"

        def group_id(flags, gid):
            data = bytearray(group(flags, ((-6, -4, 0), (0.5, 4, 5)), [(0, wall_x(0.0, -1, 1, 1, 3, out=True))]))
            at = data.index(b"PGOM") + 8
            struct.pack_into("<I", data, at + 56, gid)
            return bytes(data)
        files = {
            house: root_with_groups([WINDOW], ["Room"], lit=(0,)),
            city: root_with_groups([frosted], ["Interior_K306", "Ironforge"], unlit=(0,), wmo_id=208, group_flags=[0x2000, 0x8]),
            city[:-4] + "_000.wmo": group_id(0x2000, 13193),
            city[:-4] + "_001.wmo": group_id(0x8, 3602),
        }
        for name, data in files.items():
            (d / "wmo-windows" / name.replace(BS, "#")).write_bytes(data)
        (d / "maps.tsv").write_text("0\tAzeroth\n", newline="")
        rows = ["source\twmo\tuniqueId\tpx\tpy\tpz\trx\try\trz"]
        for uid, wmo, pos in ((1, house, (17000.0, 50.0, 17100.0)), (7706, city, (16000.0, 60.0, 16000.0))):
            rows.append("world\\maps\\azeroth\\azeroth_32_32.adt\t%s\t%d\t%.1f\t%.1f\t%.1f\t0\t0\t0" % (wmo, uid, *pos))
        (d / "adt_wmos.tsv").write_text("\n".join(rows) + "\n", newline="")
        # WMOAreaTable.dbc: 28 fields; (ID, WMOID, NameSetID, WMOGroupID, ..., AreaTableID [10], AreaName [11])
        strings = b"\0The Great Forge\0The Forlorn Cavern\0"
        recs = []
        for i, (gid, at) in enumerate(((13193, 1), (3602, 17))):
            r = [0] * 28
            r[0], r[1], r[3], r[10], r[11] = i + 1, 208, gid, 1537, at
            recs.append(struct.pack("<28I", *r))
        (d / "WMOAreaTable.dbc").write_bytes(b"WDBC" + struct.pack("<4I", len(recs), 28, 112, len(strings)) + b"".join(recs) + strings)
        styles, buildings, report = g.buildings_table(d)
        cls = {s[0]: s[1] for s in styles}
        groups = {r[6]: r for r in buildings if r[2] == 7706 and r[6] != g.WHOLE}
        placed = {r[2]: r for r in buildings if r[2] != 0 and r[6] == g.WHOLE}
        self.assertEqual(cls[groups[0][7]], g.FORGE)
        self.assertEqual(cls[groups[1][7]], g.CAVERN)
        self.assertTrue(groups[0][9] & g.B_UNDERGROUND)              # inside: no sky behind its glass
        self.assertFalse(groups[1][9] & g.B_UNDERGROUND)             # the outside group keeps it
        self.assertFalse(placed[7706][9] & g.B_UNDERGROUND)
        self.assertTrue(placed[7706][9] & g.B_SELF_LIT)              # its frosted glass is unlit: lit windows
        self.assertFalse(any(r[9] & g.B_DUNGEON for r in buildings))
        # the styles: the house's first (its ID as before Ironforge's classes), Ironforge's after
        self.assertEqual(cls[placed[1][7]], g.HOUSE)
        self.assertEqual(placed[1][7], 1)
        self.assertTrue(all(sid > max(s for s, c in cls.items() if c < g.FORGE) for sid, c in cls.items() if c >= g.FORGE))


TITAN_TEX = "dungeons\\textures\\ulduar\\ce_uldr_stainedglass01.blp"
EVIL_TEX = "dungeons\\textures\\ulduar\\ce_evilglass.blp"
HALL = "world\\wmo\\dungeon\\ulduar\\testhall.wmo"


def niche(y0, y1):
    """A closed stone box in front of a pane in the wall x = 0 (y0 .. y1, z 1 .. 3): a lattice that keeps the sky out."""
    lo, hi = (0.0, y0 - 0.5, 0.5), (2.0, y1 + 0.5, 3.5)
    x0, x1, ya, yb, za, zb = lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]
    return [[(x0, ya, zb), (x1, ya, zb), (x1, yb, zb), (x0, yb, zb)], [(x0, ya, za), (x0, yb, za), (x1, yb, za), (x1, ya, za)],
            [(x0, ya, za), (x1, ya, za), (x1, ya, zb), (x0, ya, zb)], [(x0, yb, za), (x0, yb, zb), (x1, yb, zb), (x1, yb, za)],
            wall_x(x1, ya, yb, za, zb, out=False)]


class Titan(unittest.TestCase):
    """Titan glass lit from behind: the textures' columns, which panes face the sky, the terrain's heights, the panes and
    buildings of a titan hall on Ulduar's map and the same hall elsewhere."""

    def test_backlit_values(self):
        b = g.backlit_values(EVIL_TEX)
        self.assertEqual(b[:3], (1.7, 0.05, 6.5))
        self.assertAlmostEqual(sum(c * w for c, w in zip(b[3:], g.LUMA)), 1.0, places=6)   # the pulse's colour at luminance 1
        self.assertGreater(b[5], b[3])
        self.assertGreater(b[3], b[4])                                   # deep purple: blue, then red, then green
        self.assertEqual(g.backlit_values(TITAN_TEX)[3:], (1.0, 1.0, 1.0))   # a shimmer: white
        self.assertLessEqual(g.backlit_values(TITAN_TEX)[1], 0.02)
        self.assertEqual(g.backlit_values(WINDOW), (0.0, 0.0, 0.0, 1.0, 1.0, 1.0))
        self.assertEqual(set(g.BACKLIT), {"ce_evilglass", "ce_uldr_stainedglass01", "ce_uldr_stainedglass03", "ce_uldr_warroomstainedglass",
                                          "eb_stainedglass", "eb_uldr_redwindow", "wr_window"})
        self.assertTrue(all(g.classify("x\\%s.blp" % t)[0] == g.STAINED for t in g.BACKLIT))
        self.assertEqual(sorted(g.TITAN_MAPS), [599, 602, 603])

    def test_sky_rays(self):
        stone = [q for quad in niche(-1, 1) for q in ((quad[0], quad[1], quad[2]), (quad[0], quad[2], quad[3]))]
        open_air = g.wl.Occluder([])
        self.assertEqual(g.sky_rays(open_air, (0.0, 0.0, 2.0), (1.0, 0.0, 0.0)), len(g.SKY_RAYS))      # a wall in the open
        self.assertEqual(g.sky_rays(open_air, (0.0, 0.0, 2.0), (0.0, 0.0, 1.0)), len(g.SKY_RAYS))      # a skylight
        self.assertEqual(g.sky_rays(open_air, (0.0, 0.0, 2.0), (0.0, 0.0, -1.0)), 0)                   # facing the ground
        self.assertEqual(g.sky_rays(g.wl.Occluder(stone), (0.0, 0.0, 2.0), (1.0, 0.0, 0.0)), 0)        # in a closed niche
        self.assertEqual(g.sky_rays(open_air, (0.0, 0.0, 2.0), (1.0, 0.0, 0.0), below=lambda q: True), 0)   # under the ground
        # an awning 3 yd over the window's outside, 8 yd deep: the steep rays meet it, the lowest (15 degrees) get away
        roof = [((0.0, -50.0, 5.0), (8.0, -50.0, 5.0), (8.0, 50.0, 5.0)), ((0.0, -50.0, 5.0), (8.0, 50.0, 5.0), (0.0, 50.0, 5.0))]
        n = g.sky_rays(g.wl.Occluder(roof), (0.0, 0.0, 2.0), (1.0, 0.0, 0.0))
        self.assertEqual(n, 3)
        self.assertLess(n, g.SKY_OPEN)                       # ... too few: not facing the sky

    def test_terrain(self):
        import numpy as np
        d = Path(tempfile.mkdtemp())
        (d / "terrain-scan" / "UlduarRaid").mkdir(parents=True)
        h = np.full((129, 129), 50.0, np.float32)
        h[:, 64:] = 70.0
        holes = np.zeros((128, 128), bool)
        holes[10, 10] = True
        np.savez(d / "terrain-scan" / "UlduarRaid" / "31_31.npz", ter_h=h, holes=holes)
        t = g.Terrain(d, "ulduarraid")
        tile, cell = g.wl.TILE, g.wl.CHUNK / 8.0
        x, y = tile - 20.5 * cell, tile - 20.5 * cell      # cell (20, 20): ground at 50
        self.assertAlmostEqual(t.height(x, y), 50.0, places=4)
        self.assertTrue(t.below((x, y, 40.0)))
        self.assertFalse(t.below((x, y, 60.0)))
        self.assertAlmostEqual(t.height(x, tile - 63.5 * cell), 60.0, places=3)   # half-way up the step, bilinear
        self.assertIsNone(t.height(tile - 10.5 * cell, tile - 10.5 * cell))       # a hole
        self.assertIsNone(t.height(-5000.0, -5000.0))                              # no tile there
        self.assertIsNone(g.Terrain(d, "azeroth").height(x, y))                    # no such map

    def hall(self, map_id, mapdir, uid):
        """A titan hall: an exterior group (0) with a big titan pane in the wall x = 0 facing the open sky (y -3 .. 3) and a
        small one in a closed niche (y 10 .. 12); an interior group (1, the hall behind) with Vezax's glass on a wall of its
        own, the void behind it; a plain leaded window in the open (not titan glass)."""
        d = Path(tempfile.mkdtemp())
        (d / "wmo-windows").mkdir()
        ext_quads = [(1, wall_x(0.0, -3, 3, 1, 3)), (1, wall_x(0.0, 10, 12, 1, 3)), (3, wall_x(0.0, -9, -7, 1, 3))]
        ext_quads += [(0, q) for q in niche(10, 12)]
        files = {
            HALL: root_with_groups([STONE, TITAN_TEX, EVIL_TEX, WINDOW], ["outside", "HallOfWar"], group_flags=[0x8, 0x2000]),
            HALL[:-4] + "_000.wmo": group(0x8, ((-6, -20, 0), (2.5, 20, 10)), ext_quads),
            HALL[:-4] + "_001.wmo": group(0x2000, ((-6, -20, 0), (0.2, 20, 10)), [(2, wall_x(-3.0, -16, -14, 1, 3))]),
        }
        for name, data in files.items():
            (d / "wmo-windows" / name.replace(BS, "#")).write_bytes(data)
        (d / "maps.tsv").write_text("%d\t%s\n" % (map_id, mapdir), newline="")
        rows = ["source\twmo\tuniqueId\tpx\tpy\tpz\trx\try\trz",
                "world\\maps\\%s\\%s_32_32.adt\t%s\t%d\t16000.0\t60.0\t16000.0\t0\t0\t0" % (mapdir.lower(), mapdir.lower(), HALL, uid)]
        (d / "adt_wmos.tsv").write_text("\n".join(rows) + "\n", newline="")
        return d

    def test_titan_hall_on_ulduar(self):
        d = self.hall(603, "UlduarRaid", 2712187)
        light_of = {TITAN_TEX: (g.STAINED, (0.7, 1.0, 0.6), 11), EVIL_TEX: (g.STAINED, (1.0, 0.7, 0.9), 10), WINDOW: (g.LEADED, (1.0, 1.0, 1.0), 40)}
        rows, by_map, wmos, _why = g.panes_table(d, light_of)
        by_tex = {}
        for r in rows:
            by_tex.setdefault(r[25].split(": ")[1], []).append(r)
        self.assertEqual(by_map[603], 4)
        open_pane, niche_pane = sorted(by_tex["ce_uldr_stainedglass01"], key=lambda r: -r[12])   # the big one first
        self.assertFalse(open_pane[24] & g.PANE_BACKLIT)          # it faces the open sky: daylight
        self.assertTrue(niche_pane[24] & g.PANE_BACKLIT)          # a closed niche in front: lit from behind
        evil, = by_tex["ce_evilglass"]
        self.assertTrue(evil[24] & g.PANE_BACKLIT)                # an interior group's: the void behind it is no sky
        leaded, = by_tex["mm_elwynn_wnd_ext__01"]
        self.assertFalse(leaded[24] & g.PANE_BACKLIT)             # not titan glass
        self.assertEqual((open_pane[26], evil[26], leaded[26]), (11, 10, 40))   # each its texture's GlassMaterials ID
        # the look per group: the outside group's titan glass mostly faces the sky (12 of 16 yd2): daylight; the hall's is
        # backlit; the placement goes with the most glass (daylight), the hall a group row the other way
        styles, buildings, report = g.buildings_table(d)
        placed = [r for r in buildings if r[2] == 2712187 and r[6] == g.WHOLE]
        groups = {r[6]: r for r in buildings if r[2] == 2712187 and r[6] != g.WHOLE}
        default = [r for r in buildings if r[2] == 0]
        self.assertEqual(len(placed), 1)
        self.assertFalse(placed[0][9] & g.B_BACKLIT)
        self.assertEqual(sorted(groups), [1])
        self.assertTrue(groups[1][9] & g.B_BACKLIT)
        self.assertEqual((groups[1][7], groups[1][8]), (placed[0][7], placed[0][8]))   # the placement's style and seed
        self.assertEqual(groups[1][9] & ~g.B_BACKLIT, placed[0][9])
        self.assertFalse(default[0][9] & g.B_BACKLIT)
        self.assertTrue(placed[0][9] & g.B_DUNGEON)

    def test_the_same_hall_elsewhere_keeps_the_daylight(self):
        d = self.hall(571, "Northrend", 77)
        light_of = {TITAN_TEX: (g.STAINED, (0.7, 1.0, 0.6), 11), EVIL_TEX: (g.STAINED, (1.0, 0.7, 0.9), 10), WINDOW: (g.LEADED, (1.0, 1.0, 1.0), 40)}
        rows, by_map, _wmos, _why = g.panes_table(d, light_of)
        self.assertEqual(by_map[571], 4)
        self.assertFalse(any(r[24] & g.PANE_BACKLIT for r in rows))
        _styles, buildings, _report = g.buildings_table(d)
        self.assertFalse(any(r[9] & g.B_BACKLIT for r in buildings))
        self.assertFalse(any(r[6] != g.WHOLE for r in buildings if r[2] != 0))


if __name__ == "__main__":
    unittest.main()
