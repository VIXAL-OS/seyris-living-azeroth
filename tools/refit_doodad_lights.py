#!/usr/bin/env python3
"""DoodadLightAssignment.cdbc for an install whose art packs replace light models (2026-10-25: Project Reforged D
remodels 30 of the 122 models the table lights: street lamps, lanterns, torches, braziers, candelabras).

A row puts one of our lights on a model: at its origin + an offset (AttachType 0), a bone (2) or a particle emitter
(3) by index -- indices into the stock model. A remodel has other bones and emitters, and Reforged's remodels carry
point lights of their own (made for St0ny's eight-lights exe patch), which Living Azeroth would light as well
(env/Lights ExtractLights: a model's own point lights, unless a row suppresses them): the lamp lit twice.

For each model the chain serves from a pack (mpq_chain.py), its rows are refitted from the pack's model file:
  - it has point lights of its own: each row goes to the pack light nearest where the row's anchor lands on the pack
    model (AttachType 0, the offset that light's position; our light's colour, radius and flicker kept), and the row
    suppresses the model's own lights (Flags 0x1): one light, ours, where Reforged put the lamp;
  - none: a bone or emitter row whose index the pack model still has stays (reported, to check in the lights panel);
    one whose index it lacks goes to the origin + the pack model's bounding box's upper middle (reported);
    an origin row stays.
Models the chain serves from the stock archives keep their rows as they are. Row IDs never change.

  python refit_doodad_lights.py <chain> <DoodadLightAssignment.cdbc in> <out folder> [--work <dir>] [--stock <dir>]
    --stock <dir>: the stock models (this tool's flat layout), to report how far each anchor moved.
Writes <out folder>/DoodadLightAssignment.cdbc and doodad-lights-refit.tsv.
"""
import math
import os
import struct
import sys
from pathlib import Path

import cdbc_io as io
import mpq_chain

DLA = ["u", "s", "u", "u", "s", "f", "f", "f", "f", "f", "f", "u", "u"]
#  ID ModelPath AttachType AttachIndex AttachName OffsetX/Y/Z DirectionX/Y/Z LightID Flags
ORIGIN, ATTACHMENT, BONE, EMITTER = 0, 1, 2, 3
SUPPRESS = 0x1
BS = "\\"


class M2:
    """The parts of a WotLK (264) M2 the rows anchor on, positions at rest in model space."""

    def __init__(self, data):
        if data[:4] != b"MD20":
            raise ValueError("not an MD20 model")
        u = lambda at: struct.unpack_from("<I", data, at)[0]
        vec = lambda at: struct.unpack_from("<3f", data, at)
        n, o = u(0x2C), u(0x30)
        self.bones = [vec(o + 88 * i + 76) for i in range(n)]                  # M2CompBone pivot
        n, o = u(0xF0), u(0xF4)
        self.attachments = {u(o + 40 * i): vec(o + 40 * i + 8) for i in range(n)}  # id -> position
        n, o = u(0x108), u(0x10C)
        self.lights = [vec(o + 156 * i + 4) for i in range(n) if struct.unpack_from("<H", data, o + 156 * i)[0] == 1]
        n, o = u(0x128), u(0x12C)
        self.emitters = [vec(o + 476 * i + 8) for i in range(n)]
        self.box = (vec(0xA0), vec(0xAC))

    def anchor(self, kind, index, offset):
        """Where a row's light lands on this model (None: the model has no such anchor)."""
        if kind == ORIGIN:
            base = (0.0, 0.0, 0.0)
        elif kind == BONE:
            base = self.bones[index] if index < len(self.bones) else None
        elif kind == EMITTER:
            base = self.emitters[index] if index < len(self.emitters) else None
        else:
            base = self.attachments.get(index)
        return None if base is None else tuple(b + o for b, o in zip(base, offset))

    def upper_middle(self):
        lo, hi = self.box
        return ((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, lo[2] + 0.8 * (hi[2] - lo[2]))


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def main():
    args = sys.argv[1:]
    opts = {}
    for flag in ("--work", "--stock"):
        if flag in args:
            i = args.index(flag)
            opts[flag] = args[i + 1]
            del args[i:i + 2]
    if len(args) != 3:
        raise SystemExit(__doc__)
    chain_file, table, out = args[0], Path(args[1]), Path(args[2])
    work = Path(opts.get("--work") or (out / "_models"))
    _fields, raw, s = io.read(table.read_bytes())
    rows = [[r[0], s(r[1]), r[2], r[3], s(r[4])] + [io.as_float(x) for x in r[5:11]] + [r[11], r[12]] for r in raw]
    chain = mpq_chain.Chain(chain_file, cache=str(work / "_lists"))
    stock_layers = {l.path for l in chain.layers if l.is_dir}
    models = sorted({r[1].lower().replace("/", BS) for r in rows})
    from_pack = {}
    for m in models:
        layer, _ = chain.winner(m)
        if layer is not None and layer.path not in stock_layers:
            from_pack[m] = layer.path
    got = chain.extract(sorted(from_pack), str(work), flat=True) if from_pack else {}
    report = ["ID\tmodel\tlayer\tbefore\tafter\twhy\tanchor moved (yd, with --stock)"]
    changed = 0
    for r in rows:
        m = r[1].lower().replace("/", BS)
        if m not in got:
            continue
        pack = M2((work / m.replace(BS, "#")).read_bytes())
        stock = None
        if "--stock" in opts:
            sp = Path(opts["--stock"]) / m.replace(BS, "#")
            stock = M2(sp.read_bytes()) if sp.exists() else None
        kind, index, offset = r[2], r[3], tuple(r[5:8])
        before = "type %d #%d off (%.2f, %.2f, %.2f) flags %d" % (kind, index, *offset, r[12])
        here = pack.anchor(kind, index, offset)
        why = ""
        if pack.lights:
            target = here if here is not None else pack.upper_middle()
            light = min(pack.lights, key=lambda p: dist(p, target))
            r[2], r[3], r[5:8] = ORIGIN, 0, list(light)
            r[12] |= SUPPRESS
            why = "the pack's model has %d point light(s) of its own: ours at the nearest, its own suppressed" % len(pack.lights)
        elif here is None:
            r[2], r[3], r[5:8] = ORIGIN, 0, list(pack.upper_middle())
            why = "the pack's model has no %s %d: the origin + its box's upper middle (check in the lights panel)" % (
                {BONE: "bone", EMITTER: "emitter", ATTACHMENT: "attachment"}[kind], index)
        else:
            why = "kept: the pack's model has the anchor (another mesh: check in the lights panel)"
        after = "type %d #%d off (%.2f, %.2f, %.2f) flags %d" % (r[2], r[3], *r[5:8], r[12])
        moved = ""
        if stock is not None:
            a0 = stock.anchor(kind, index, offset)
            a1 = pack.anchor(r[2], r[3], tuple(r[5:8]))
            moved = "%.2f" % dist(a0, a1) if a0 is not None and a1 is not None else "?"
        changed += before != after
        report.append(f"{r[0]}\t{m}\t{Path(got[m][0]).name}\t{before}\t{after}\t{why}\t{moved}")
    out.mkdir(parents=True, exist_ok=True)
    built = [tuple([r[0], r[1], r[2], r[3], r[4]] + list(r[5:11]) + [r[11], r[12]]) for r in rows]
    (out / "DoodadLightAssignment.cdbc").write_bytes(io.build(DLA, built))
    (out / "doodad-lights-refit.tsv").write_text("\n".join(report) + "\n", encoding="utf-8")
    print(f"{len(got)} of {len(models)} lit models come from packs; {len(report) - 1} rows refitted, {changed} changed -> "
          f"{out / 'DoodadLightAssignment.cdbc'}")


if __name__ == "__main__":
    main()
