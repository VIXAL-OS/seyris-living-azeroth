"""Window glass (tools/gen_glass.py) on names, texels and small WMOs built here, no map data: which textures are
candidates and how they are classified (Stormwind's walls are not windows; broken, burnt and abandoned glass is left
out), the glass mask on texels, the panes of a WMO, which way is out (an exterior face, its interior twin, an
interior-only window), the room each pane lights, and the placement into world space.

Run:  python -m unittest discover -s tests/glass -p "test_*.py"     (from the repository)
"""
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


if __name__ == "__main__":
    unittest.main()
