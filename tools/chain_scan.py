#!/usr/bin/env python3
"""A map scan as an install with art packs loads it: the stock scan (waterfall-scan's out folder) with every file a
pack in the chain replaces taken from that pack (mpq_chain.py), for the generators that read a scan (gen_glass.py,
gen_window_lights.py, see_through_survey.py, and waterfall-scan's flow_scan.py through its terrain-scan).

Why (2026-10-25): Project Reforged's D remodels 28 of the buildings Living Azeroth's glass and window tables describe
and rewrites 85 map tiles. Those tables are keyed on WMO geometry and placements, so with D they are regenerated from
D's files; without it the stock tables stay as they are.

  python chain_scan.py <stock scan> <chain> <out scan> [--wdt <scan>/wdt]

What it makes (the stock scan is never written; files the packs don't touch are hard links into it, so it costs
little disk on the same drive):
  wmo-windows/   every root and group file of the stock scan, the chain's version of each; plus the group files of
                 the roots a pack replaced (a remodel can have more groups); stale groups past a new root's count go
  adt_wmos.tsv   the placements, with every row from a map tile the chain serves from a pack read again from that
                 pack's tile (MWMO, MWID, MODF, as waterscan writes them); a pack tile's placements stay in the stock
                 order of their tile
  terrain-scan/  per tile, the pack's tile scanned again (waterfall-scan terrain_scan.scan: heights, liquid, areas)
  the rest       hard links (files) and junctions (folders). AreaTable.dbc and WMOAreaTable.dbc stay the stock ones:
                 Living Azeroth maps a pack's renumbered areas back to stock at run time (AreaRemap.cdbc), so the
                 tables keep the stock areas too.
  chain-scan.tsv what came from where (kind, path, layer).
"""
import collections
import csv
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import mpq_chain

BS = "\\"
WATERFALL_SCAN = Path(__file__).resolve().parents[3] / "waterfall-scan"  # BASE\waterfall-scan (terrain_scan.py)


def link(src, dst):
    try:
        os.link(src, dst)
    except OSError:
        shutil.copyfile(src, dst)


def junction(src, dst):
    if os.name == "nt":
        subprocess.run(["cmd", "/c", "mklink", "/J", str(dst), str(src)], check=True, capture_output=True)
    else:
        os.symlink(src, dst)


def flat_name(path):
    return path.lower().replace(BS, "#")


def archive_name(flat):
    return flat.replace("#", BS)


def is_group(name):
    return re.search(r"_\d{3}\.wmo$", name) is not None


def root_groups(data):
    """MOHD's group count of a WMO root."""
    at = 0
    while at + 8 <= len(data):
        tag = data[at:at + 4][::-1]
        size = struct.unpack_from("<I", data, at + 4)[0]
        if tag == b"MOHD":
            return struct.unpack_from("<I", data, at + 8 + 4)[0]
        at += 8 + size
    return None


def chunks(b):
    at = 0
    while at + 8 <= len(b):
        tag = b[at:at + 4][::-1]
        size = struct.unpack_from("<I", b, at + 4)[0]
        yield tag, at + 8, size
        at += 8 + size


def adt_wmo_rows(source, data):
    """adt_wmos.tsv rows of one tile, as waterscan writes them."""
    names, mwid, modf = {}, None, None
    for tag, p, size in chunks(data):
        if tag == b"MWMO":
            block = data[p:p + size]
            off = 0
            for part in block.split(b"\x00"):
                if part:
                    names[off] = part.decode("latin-1").lower()
                off += len(part) + 1
        elif tag == b"MWID":
            mwid = (p, size)
        elif tag == b"MODF":
            modf = (p, size)
    rows = []
    if not modf:
        return rows
    p, size = modf
    for at in range(p, p + size - 63, 64):
        name_id, uid = struct.unpack_from("<II", data, at)
        off = name_id
        if mwid and 4 * name_id + 4 <= mwid[1]:
            off = struct.unpack_from("<I", data, mwid[0] + 4 * name_id)[0]
        f = struct.unpack_from("<12f", data, at + 8)
        flags, dset = struct.unpack_from("<HH", data, at + 56)
        rows.append([source, names.get(off, "?"), str(uid)] + ["%.4f" % v for v in f] + [str(flags), str(dset)])
    return rows


def rescan_terrain(chain, stock, out, wdt, stock_layers, log):
    """terrain-scan/: hard links into the stock scan's, and every map tile a pack in the chain serves (not only those
    with WMO placements: a tile's liquid or heights alone matter to the river flow) scanned again from the pack's file
    (waterfall-scan terrain_scan.scan). A tile the stock scan has no copy of is skipped: its WDT doesn't list it, so the
    client never loads it. Returns how many tiles were scanned again."""
    sys.path.insert(0, str(WATERFALL_SCAN))
    import terrain_scan  # noqa: E402
    import numpy as np  # noqa: E402
    ts_src, ts_dst = stock / "terrain-scan", out / "terrain-scan"
    for root, _dirs, files in os.walk(ts_src):
        rel = Path(root).relative_to(ts_src)
        (ts_dst / rel).mkdir(parents=True, exist_ok=True)
        for f in files:
            if not (ts_dst / rel / f).exists():
                link(Path(root) / f, ts_dst / rel / f)
    maps = {d.lower(): d for d in os.listdir(ts_src)}
    tiles = {}
    for layer in chain.layers:
        if layer.path in stock_layers:
            continue
        for key in layer.names(chain.hdmpq, chain.cache):
            if key.startswith("world\\maps\\") and key.endswith(".adt") and key.count(BS) == 3:
                tiles[key] = None
    wanted = []
    for name in sorted(tiles):
        layer, _ = chain.winner(name)
        if layer is None or layer.path in stock_layers:
            continue
        parts = name.split(BS)  # world\maps\<map>\<map>_<col>_<row>.adt
        col, row = parts[3][:-4].rsplit("_", 2)[-2:]
        if (ts_src / maps.get(parts[2], parts[2]) / ("%s_%s.npz" % (col, row))).exists():
            wanted.append(name)
    adt_dir = out.parent / (out.name + "-adts")
    got = chain.extract(wanted, str(adt_dir), flat=True) if wanted else {}
    rescanned = 0
    for name in sorted(got):
        parts = name.split(BS)
        m = parts[2]
        col, row = parts[3][:-4].rsplit("_", 2)[-2:]
        mdir = maps.get(m, m)
        wdt_file = wdt / ("World#Maps#%s#%s.wdt" % (mdir, mdir))
        big = terrain_scan.big_alpha(str(wdt_file)) if wdt_file.exists() else False
        target = ts_dst / mdir / ("%s_%s.npz" % (col, row))
        if target.exists():
            target.unlink()
        np.savez_compressed(str(target), **terrain_scan.scan(str(adt_dir / flat_name(name)), big))
        rescanned += 1
        log.append(("terrain tile", name, got[name][0]))
    return rescanned


def main():
    args = sys.argv[1:]
    if "--terrain-only" in args:
        # redo step 4 on an existing out scan (python chain_scan.py <stock scan> <chain> <out scan> --terrain-only)
        args.remove("--terrain-only")
        stock, out = Path(args[0]), Path(args[2])
        chain = mpq_chain.Chain(args[1], cache=str(out.parent / (out.name + "-lists")))
        log = []
        n = rescan_terrain(chain, stock, out, stock / "wdt", {l.path for l in chain.layers if l.is_dir}, log)
        with open(out / "chain-scan.tsv", "a", encoding="utf-8", newline="") as fh:
            for k, p, l in log:
                fh.write(f"{k}\t{p}\t{l}\n")
        print(f"{out}: {n} terrain tiles scanned again")
        return
    wdt = None
    if "--wdt" in args:
        i = args.index("--wdt")
        wdt = Path(args[i + 1])
        del args[i:i + 2]
    if len(args) != 3:
        raise SystemExit(__doc__)
    stock, out = Path(args[0]), Path(args[2])
    wdt = wdt or stock / "wdt"
    chain = mpq_chain.Chain(args[1], cache=str(out.parent / (out.name + "-lists")))
    stock_layers = {l.path for l in chain.layers if l.is_dir}
    if out.exists():
        raise SystemExit(f"{out} exists: remove it first (it is made whole each time)")
    out.mkdir(parents=True)
    log = []

    # 1. Everything but the three parts below: hard links and junctions.
    for entry in sorted(stock.iterdir()):
        if entry.name in ("wmo-windows", "terrain-scan", "adt_wmos.tsv"):
            continue
        if entry.is_dir():
            junction(entry, out / entry.name)
        else:
            link(entry, out / entry.name)

    # 2. wmo-windows: the chain's version of every file, and the groups of every root a pack replaced.
    src, dst = stock / "wmo-windows", out / "wmo-windows"
    dst.mkdir()
    flats = sorted(os.listdir(src))
    for f in flats:
        link(src / f, dst / f)
    replaced = {}
    staging = out.parent / (out.name + "-extract")
    for f in flats:
        layer, _ = chain.winner(archive_name(f))
        if layer is not None and layer.path not in stock_layers:
            replaced[archive_name(f)] = layer.path
    got = chain.extract(sorted(replaced), str(staging), flat=True) if replaced else {}
    for name, (layer, _size) in got.items():
        f = flat_name(name)
        if (dst / f).exists():
            os.remove(dst / f)
        os.replace(staging / f, dst / f)
        log.append(("wmo", name, layer))
    # groups of replaced roots (the remodel's count), and stale ones past it
    more = []
    for name in sorted(got):
        if not name.endswith(".wmo") or is_group(name):
            continue
        n = root_groups((dst / flat_name(name)).read_bytes())
        if n is None:
            continue
        stem = name[:-4]
        for g in range(n):
            gname = "%s_%03d.wmo" % (stem, g)
            if gname not in got:
                layer, _ = chain.winner(gname)
                if layer is not None and (layer.path not in stock_layers or not (dst / flat_name(gname)).exists()):
                    more.append(gname)
        for g in range(n, 512):
            stale = dst / flat_name("%s_%03d.wmo" % (stem, g))
            if stale.exists():
                stale.unlink()
                log.append(("wmo stale group removed", archive_name(stale.name), ""))
            elif g > n + 8:
                break
    if more:
        got2 = chain.extract(sorted(set(more)), str(staging), flat=True)
        for name, (layer, _size) in got2.items():
            f = flat_name(name)
            if (dst / f).exists():
                os.remove(dst / f)
            os.replace(staging / f, dst / f)
            log.append(("wmo group of a replaced root", name, layer))

    # 3. adt_wmos.tsv: a pack's tiles read again.
    with open(stock / "adt_wmos.tsv", newline="", encoding="latin-1") as fh:
        reader = csv.reader(fh, delimiter="\t")
        header = next(reader)
        rows = list(reader)
    tiles = collections.OrderedDict()
    for r in rows:
        tiles.setdefault(r[0], []).append(r)
    pack_tiles = {}
    for t in tiles:
        if not t.endswith(".adt"):
            continue
        layer, _ = chain.winner(t)
        if layer is not None and layer.path not in stock_layers:
            pack_tiles[t] = layer.path
    adt_dir = out.parent / (out.name + "-adts")
    got_adts = chain.extract(sorted(pack_tiles), str(adt_dir), flat=True) if pack_tiles else {}
    new_rows = []
    changed_tiles = 0
    for t, trs in tiles.items():
        if t.lower() in got_adts:
            pr = adt_wmo_rows(t, (adt_dir / flat_name(t)).read_bytes())
            if [r[1:] for r in pr] != [r[1:] for r in trs]:
                changed_tiles += 1
            new_rows += pr
            log.append(("adt placements", t, got_adts[t.lower()][0]))
        else:
            new_rows += trs
    with open(out / "adt_wmos.tsv", "w", newline="", encoding="latin-1") as fh:
        w = csv.writer(fh, delimiter="\t", lineterminator="\n")
        w.writerow(header)
        w.writerows(new_rows)
    # WMOs a pack tile places that the stock scan has no root of: their roots and groups from the chain
    placed_new = sorted({r[1] for r in new_rows if r[0].lower() in got_adts and not (dst / flat_name(r[1])).exists()})
    new_roots = chain.extract(placed_new, str(staging), flat=True) if placed_new else {}
    groups_new = []
    for name in new_roots:
        os.replace(staging / flat_name(name), dst / flat_name(name))
        log.append(("wmo placed by a pack tile", name, new_roots[name][0]))
        n = root_groups((dst / flat_name(name)).read_bytes()) or 0
        groups_new += ["%s_%03d.wmo" % (name[:-4], g) for g in range(n)]
    for name, (layer, _size) in (chain.extract(groups_new, str(staging), flat=True) if groups_new else {}).items():
        os.replace(staging / flat_name(name), dst / flat_name(name))
        log.append(("wmo group placed by a pack tile", name, layer))

    # 4. terrain-scan: hard links, every pack tile scanned again.
    rescanned = rescan_terrain(chain, stock, out, wdt, stock_layers, log)

    with open(out / "chain-scan.tsv", "w", encoding="utf-8", newline="") as fh:
        fh.write("kind\tpath\tlayer\n")
        for k, p, l in log:
            fh.write(f"{k}\t{p}\t{l}\n")
    shutil.rmtree(staging, ignore_errors=True)
    print(f"{out}: {len(got)} WMO files from packs (+{len(more)} groups of replaced roots), {len(got_adts)} pack tiles "
          f"({changed_tiles} with other WMO placements), {rescanned} terrain tiles scanned again")


if __name__ == "__main__":
    main()
