#!/usr/bin/env python3
"""Settlement lights: warm light for the lanterns, chandeliers, candles and hearths of inns, taverns and other lived-in
buildings, added to DoodadLightProperties.cdbc and DoodadLightAssignment.cdbc. Every other row stays as it is.

Why: "make the glowing light inside every inn more inviting" (the user, 2026-10-17, after WoW: Forever's beta), then
"every inhabited building". These models carry no light of their own (no M2 light entries; checked in the 3.3.5a
files), and upstream's tables (Morfium-G's, 2026-10-13) don't list them, so they gave no light: the human inns' hanging
lanterns and candle chandeliers, the dwarven inns' and Ironforge's small lanterns, the orc inns' skull candles, the
Undercity's lanterns and torches, Silvermoon's, Dalaran's, the Exodar's and the tuskarr's lamps, a few braziers and
forges. Only models that look lit get a light (a flame emitter or a glowing material); unlit candles (candleoff*),
woodpiles without flames (firewoodpile03), the unlit lanterns generallantern02/04 and candelabratall02 stay dark.
A model another row already lights is never touched (the tool skips it).

Rows are model-path-wide, so a lantern lights up everywhere it stands, also where it isn't an inn:
  generalhanginglantern01   human inns (Goldshire, Menethil, Darkshire, Lakeshire, Southshore, Brill, Theramore,
                            Valiance Keep, Valgarde), Menethil's keep, and the furbolgs' barrow dens (Timbermaw Hold and
                            the hostile dens: the lanterns are lit there too)
  new_small_lantern01/02    dwarven inns and taverns, Ironforge, dwarven barracks and towers, Shattrath's World's End
  skullcandle01             orc inns; also troll temples and ogre or mountain caves, where the candle burns too
  undead_torch01/02         the Undercity; also the Plaguelands' and Northrend's Scourge ziggurats and necropolises,
                            whose torches are the same lit model (a table row can't tell the places apart)
Left out on purpose, as their rooms are already full (16+ lights per cell from upstream's torches and candles): the
wall candelabras candelabratallwall01/02 (barracks, Stormwind Keep and Cathedral), chandelier01, stormwindbrazier01,
and Dalaran's 229 wall sconces (dalaran_sconce_indoor_01..03).

Colours follow the model: warm 2200-3400 K for flames and lantern glass (kelvin() below), the Undercity's pale gold
glass, Silvermoon's gold, the Exodar's rose and violet. Glass lanterns are steady, flames flicker gently.

Positions: at the flame (the particle emitter, or the emitters' centre for a many-candle chandelier: one light, not
twelve) or at the centre of the glowing geometry (batches with an unlit or additive material, from the model's
00.skin), in model space. Radii stay small (5-12 yd) so busy rooms stay near the lights panel's 16 per 16-yd cell: the
Lion's Pride Inn's busiest cell holds 18 (12 before), Dalaran's 16, Ironforge's 10.

IDs: properties 101..199 and assignments 1001..1999 are this tool's; upstream's start at 1. A row in those ranges that
isn't ours stops the tool instead of being overwritten.
Usage:  python add_settlement_lights.py [dir]     (default: ../data; for the client: <client>/DBFilesClient, which
        keeps a timestamped .bak of each file it changes). Sets exactly these rows, so running it twice changes
        nothing. Then press "Reload light tables from disk" in the lights panel.
"""
import math
import sys
from pathlib import Path

import cdbc_io as io

POINT = 0
ORIGIN, EMITTER = 0, 3
FLICKER_OFF, FLICKER_SMOOTH, FLICKER_NOISE = 0, 1, 2
D = -1.0  # the panel's default
# Upstream's lights (data/DoodadLightProperties.cdbc) reused, so like things look alike.
TORCH_FLAME, LANTERN, NIGHTELF_LANTERN, CANDLE = 1, 2, 5, 7


def kelvin(k):
    """0xRRGGBB of a black body at k kelvin (Tanner Helland's fit, good to ~1% from 1000 to 40000 K)."""
    t = k / 100.0
    r = 255.0 if t <= 66 else 329.698727446 * (t - 60) ** -0.1332047592
    g = 99.4708025861 * math.log(t) - 161.1195681661 if t <= 66 else 288.1221695283 * (t - 60) ** -0.0755148492
    b = 255.0 if t >= 66 else (0.0 if t <= 19 else 138.5177312231 * math.log(t - 10) - 305.0447927307)
    c = [max(0, min(255, round(v))) for v in (r, g, b)]
    return (c[0] << 16) | (c[1] << 8) | c[2]


def light(id, name, color, intensity, radius, flicker=FLICKER_OFF, amount=D, speed=D):
    # ID Name Type Color Intensity Radius Falloff InnerAngle OuterAngle FlickerMode FlickerSpeed FlickerAmount Flags
    return (id, name, POINT, color, intensity, radius, D, 20.0, 35.0, flicker, speed, amount, 0)


LIGHTS = [
    light(101, "Inn lantern, glass (3000 K)", kelvin(3000), 1.1, 8.0),
    light(102, "Inn chandelier, candles (2800 K)", kelvin(2800), 1.3, 10.0, FLICKER_SMOOTH, 0.08, 0.8),
    light(103, "Small lantern (3000 K)", kelvin(3000), 0.85, 6.0),
    light(104, "Candelabra (2700 K)", kelvin(2700), 1.0, 6.5, FLICKER_SMOOTH, 0.10),
    light(105, "Hearth and forge glow (2200 K)", kelvin(2200), 1.3, 12.0, FLICKER_NOISE, 0.12, 0.5),
    light(106, "Undercity lantern (pale gold glass)", 0xF2E096, 1.0, 8.0),
    light(107, "Undercity torch (pale gold glass)", 0xF2E096, 0.7, 5.0),
    light(108, "Silvermoon lantern (gold)", 0xFFC860, 1.0, 8.0, FLICKER_NOISE, 0.05, 0.7),
    light(109, "Silvermoon sconce (warm white)", 0xFFD9A0, 0.75, 6.0, FLICKER_SMOOTH, 0.06),
    light(111, "Dalaran chandelier (3200 K)", kelvin(3200), 1.2, 10.0),
    light(112, "Exodar sconce (rose white)", 0xFFC4B8, 0.8, 8.0, FLICKER_SMOOTH, 0.05),
    light(113, "Draenei lantern (violet)", 0xDF85D8, 0.7, 7.0),
    light(114, "Tuskarr oil lamp (3400 K)", kelvin(3400), 0.9, 8.0, FLICKER_NOISE, 0.06, 0.7),
    light(115, "Hanging lamp, long chain (3000 K)", kelvin(3000), 1.1, 12.0),
    # Upstream's Torch flame reaches 20.8 yd; braziers stand in rows, so these stop at 12.
    light(116, "Brazier (2400 K)", kelvin(2400), 1.2, 12.0, FLICKER_SMOOTH, 0.15),
    # Upstream's Candle reaches 9.5 yd; skull candles also stand in rows in caves already full of candles.
    light(117, "Skull candle (2700 K)", kelvin(2700), 0.6, 5.0, FLICKER_SMOOTH, 0.12),
]


def assign(id, model, light_id, offset=(0.0, 0.0, 0.0), attach=ORIGIN, index=0):
    # ID ModelPath AttachType AttachIndex AttachName OffsetX OffsetY OffsetZ DirectionX DirectionY DirectionZ LightID Flags
    return (id, model, attach, index, "", *offset, 0.0, 0.0, -1.0, light_id, 0)


H = "world\\generic\\human\\passive doodads\\"
DW = "world\\generic\\dwarf\\passive doodads\\"
UD = "world\\generic\\undead\\passive doodads\\"
BE = "world\\expansion01\\doodads\\generic\\bloodelf\\"
DAL = "world\\expansion02\\doodads\\dalaran\\"
TS = "world\\expansion02\\doodads\\generic\\tuskarr\\"
ASSIGNMENTS = [
    # Human inns: the lanterns by the doors and over the bar, the candle chandeliers. Offsets: the glass's centre /
    # the candles' centre.
    assign(1001, H + "hanginglanterns\\generalhanginglantern01.m2", 101, (0.0, 0.0, 0.51)),
    assign(1002, "world\\dungeon\\goldshireinn\\innchandelier\\innchandelier.m2", 102, (0.037, 0.0, -1.775)),
    # Dwarven inns, taverns, barracks and Ironforge.
    assign(1004, DW + "lanterns\\new_small_lantern01.m2", 103, (0.0, 0.0, 1.05)),
    assign(1005, DW + "lanterns\\new_small_lantern02.m2", 103, (0.0, 0.0, 1.05)),
    assign(1006, DW + "lanterns\\ironforgehanginglantern_new.m2", 115, (-0.07, -0.04, -9.47)),
    assign(1007, "world\\generic\\darkirondwarf\\passive doodads\\lamps\\darkironhanginglamp02.m2", 115, (-0.04, -0.11, -13.04)),
    assign(1008, "world\\expansion02\\doodads\\wintertauren\\wt_lanternhanging01.m2", 101, (0.01, 0.0, 0.27)),
    # The blood elves' nine-candle candelabra: one light at the candles' centre.
    assign(1011, BE + "candles\\be_candleabra01.m2", 104, (-0.005, 0.018, 4.552)),
    # Candles: the orc inns' skull candles (small), the night elves' floating candles (upstream's Candle).
    assign(1012, "world\\generic\\orc\\passive doodads\\voodoostuff\\skullcandle01.m2", 117, attach=EMITTER, index=0),
    assign(1013, "world\\generic\\nightelf\\passive doodads\\candles\\ne_floatingcandles.m2", CANDLE, (0.025, 0.194, 0.84)),
    assign(1014, "world\\generic\\nightelf\\passive doodads\\lanterns\\nightelfhanginglantern.m2", NIGHTELF_LANTERN, attach=EMITTER, index=0),
    # Braziers (a shorter reach than upstream's Torch flame), and Orgrimmar's bonfires (upstream's Torch flame).
    assign(1015, "world\\generic\\orc\\passive doodads\\braziers\\smallbraziernoomni01.m2", 116, attach=EMITTER, index=0),
    assign(1017, DW + "braziers\\stormwinddwarfbrazier.m2", 116, attach=EMITTER, index=0),
    assign(1018, "world\\kalimdor\\orgrimmar\\passivedoodads\\orgrimmarbonfire\\orgrimmarbonfire01.m2", TORCH_FLAME, (-0.07, -0.116, 1.05)),
    # Smithies: the dwarven forge fire, Ironforge's coal beds.
    assign(1019, DW + "forgebonfire\\forgebonfire.m2", 105, (-0.008, 0.054, 0.624)),
    assign(1020, "world\\khazmodan\\ironforge\\passivedoodads\\hotcoals\\hotcoals.m2", 105, (0.05, -0.05, 1.0)),
    # The Undercity: lantern glass, chandeliers, wall torches.
    assign(1021, UD + "undercitylanterns\\undeadhanginglantern01.m2", 106, (-0.02, -0.01, -1.04)),
    assign(1022, UD + "undercitychandeliers\\undeadchandelier01.m2", 106, (0.0, 0.02, -0.58)),
    assign(1023, UD + "undercity torches\\undead_torch02.m2", 107, (-0.02, -1.96, 0.0)),
    assign(1024, UD + "undercity torches\\undead_torch01.m2", 107, (0.0, -2.04, 6.41)),
    # Silvermoon.
    assign(1025, BE + "lantern\\be_lantern01.m2", 108, attach=EMITTER, index=0),
    assign(1026, "world\\generic\\bloodelf\\passive doodads\\be_lantern_red_001.m2", 108, (-0.016, -0.033, 3.835)),
    assign(1027, BE + "sconce\\be_sconce01.m2", 109, (0.126, 0.0, 0.738)),
    # Dalaran: the chandeliers only. Its 229 wall sconces (dalaran_sconce_indoor_01..03) would put the inn's busiest
    # cells at 23 lights (16 drawn) and 9 cells over the limit, where upstream's candles and lanterns already light the rooms.
    assign(1031, DAL + "dalaran_chandelier01.m2", 111, (0.0, 0.0, -6.07)),
    assign(1032, DAL + "dalaran_chandelier02.m2", 111, (0.0, 0.0, -6.05)),
    assign(1033, DAL + "dalaran_chandelier01_short.m2", 111, (0.0, 0.0, -2.89)),
    assign(1034, DAL + "dalaran_chandelier02_short.m2", 111, (0.0, 0.0, -2.52)),
    # The Exodar and Azuremyst.
    assign(1035, "world\\expansion01\\doodads\\theexodar\\passivedoodads\\light_sconces\\exodar_sconce.m2", 112, attach=EMITTER, index=0),
    assign(1036, "world\\expansion01\\doodads\\generic\\draenei\\lanterns\\dr_lantern_01.m2", 113, (-0.22, 0.0, 0.18)),
    # Tuskarr villages (Kamagua, Moa'ki, Unu'pe, Kaskala).
    assign(1037, TS + "lights\\ts_lighthanging.m2", 114, (0.0, 0.01, 0.82)),
    assign(1038, TS + "lights\\ts_light.m2", 114, (0.15, 0.01, 0.72)),
    assign(1039, TS + "ts_lighthanging02.m2", 114, (0.15, 0.01, 0.72)),
    # Taunka and Mag'har lamp posts at their towns' doors (upstream's Lantern, as on the other lamp posts).
    assign(1040, "world\\expansion02\\doodads\\wintertauren\\wt_lamppost01.m2", LANTERN, (-0.07, -0.004, 1.14)),
    assign(1041, "world\\expansion01\\doodads\\generic\\ancientorc\\lampost\\ao_lamppost02.m2", LANTERN, (-0.003, -0.015, 0.426)),
]
PROPERTY_IDS = range(101, 200)
ASSIGNMENT_IDS = range(1001, 2000)


def read_table(path, kinds):
    _, raw, string_at = io.read(path.read_bytes())
    rows = []
    for r in raw:
        if len(r) != len(kinds):
            sys.exit(f"{path}: {len(r)} fields, expected {len(kinds)} (an old table: run upgrade_tables.py first)")
        rows.append([io.as_float(v) if k == "f" else io.as_int(v) if k == "i" else string_at(v) if k == "s" else v
                     for k, v in zip(kinds, r)])
    return rows


def merge(rows, ours, ids, label, key):
    """Our rows replace every row in our ID range. A row there that none of ours matches by `key` (name / model)
    may be someone else's: stop rather than overwrite it."""
    keys = {key(r) for r in ours}
    for r in rows:
        if r[0] in ids and key(r) not in keys:
            sys.exit(f"{label} ID {r[0]} ({key(r)!r}) is in this tool's ID range but isn't one of its rows: "
                     "renumber, or remove that row first")
    kept = [r for r in rows if r[0] not in ids]
    return sorted(kept + [list(r) for r in ours], key=lambda r: r[0])


def main():
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "data"
    ppath, apath = folder / "DoodadLightProperties.cdbc", folder / "DoodadLightAssignment.cdbc"
    props = read_table(ppath, io.DOODAD_LIGHT_PROPERTIES)
    assigns = read_table(apath, io.DOODAD_LIGHT_ASSIGNMENT)

    # Never a second light on a model another row already handles.
    norm = lambda p: p.lower().replace("/", "\\")
    theirs = {norm(r[1]) for r in assigns if r[0] not in ASSIGNMENT_IDS}
    ours = [a for a in ASSIGNMENTS if norm(a[1]) not in theirs]
    for a in ASSIGNMENTS:
        if norm(a[1]) in theirs:
            print(f"skipped {a[1]}: already has a row of its own")
    known = {r[0] for r in props if r[0] not in PROPERTY_IDS} | {l[0] for l in LIGHTS}
    for a in ours:
        if a[11] not in known:
            sys.exit(f"assignment {a[0]} refers to light {a[11]}, which the table doesn't have")

    new_props = merge(props, LIGHTS, PROPERTY_IDS, "DoodadLightProperties", key=lambda r: r[1])
    new_assigns = merge(assigns, ours, ASSIGNMENT_IDS, "DoodadLightAssignment", key=lambda r: norm(r[1]))
    for path, kinds, rows in ((ppath, io.DOODAD_LIGHT_PROPERTIES, new_props), (apath, io.DOODAD_LIGHT_ASSIGNMENT, new_assigns)):
        data = io.build(kinds, [tuple(r) for r in rows])
        old = path.read_bytes()
        if data == old:
            print(f"{path}: unchanged ({len(rows)} rows)")
            continue
        if path.parent.name.lower() == "dbfilesclient":
            backup = io.backup_path(path)
            backup.write_bytes(old)
            print(f"{path}: backup {backup.name}")
        path.write_bytes(data)
        print(f"{path}: written ({len(rows)} rows)")


if __name__ == "__main__":
    main()
