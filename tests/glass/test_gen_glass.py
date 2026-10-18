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


if __name__ == "__main__":
    unittest.main()
