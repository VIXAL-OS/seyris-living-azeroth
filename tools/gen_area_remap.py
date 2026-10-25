#!/usr/bin/env python3
"""AreaRemap.cdbc: keeps every place's zone when an art pack renumbers the client's areas (read by env/WorldQuery's
area chain, so AreaClimate, FogSettings, the wind, the grass and the regional weather all keep their rows).

Why (2026-10-25): Project Reforged's M and S move 685 WMOAreaTable rows -- caves and dungeon entrances: Fargodeep
Mine, Blackrock Mountain, Gnomeregan's and the Deadmines' entrances, the Caverns of Time -- to 43 new AreaTable rows
(6001-6043) with no parent zone, so inside them the zone's climate and fog rows stopped applying. And it gives three
dungeons' own areas (Uldaman, the Deadmines, Maraudon: their instance maps' areas) a parent zone on the continent,
so inside the instance the continent zone's rows would start applying.

What a row says: when the client reports area AreaID, and its AreaTable record is the one the pack ships (on
continent ContinentID, with parent ParentID), the chain goes on at StockAreaID instead of that parent: the stock
area the pack's area replaced (0 = the chain ends there, as in the stock table). On a stock client (or a pack that
reuses the id for something else) the record doesn't match, so the row does nothing: one file works with and
without the packs.

How the rows are found, from the stock tables and the pack's (the files the install will load: mpq_chain.py):
  1. A new area (not in the stock AreaTable) with no parent: the stock area most of the WMOAreaTable rows now
     pointing at it pointed at before; when those rows had none (area 0: the client used the ground's area), the stock
     area of the same name on the same continent.
  2. A stock area whose chain the pack changed so that it no longer reads as the stock chain (stock ids only, in
     order: a new starting zone put between a sub-area and its zone is fine): the stock parent.
Then every WMOAreaTable row and every area of the pack's tables is checked: its chain with the rows applied, stock
ids only, equals the stock chain of the area the stock tables give it. The report lists any that don't.

  python gen_area_remap.py <stock dbc folder> <pack dbc folder> [data dir] [--report <file>]
    folders holding AreaTable.dbc and WMOAreaTable.dbc (any case), e.g. from
    mpq_chain.py <chain> --extract <list> <folder>. Writes <data dir>/AreaRemap.cdbc (default: ../data).
"""
import collections
import os
import sys

import cdbc_io as io

AREA_REMAP = ["u", "u", "u", "u", "u", "s"]
#  ID AreaID ContinentID ParentID StockAreaID Comment
# AreaTable.dbc (3.3.5a): ID 0, ContinentID 1, ParentAreaID 2, ..., AreaName_Lang enUS 11.
A_ID, A_MAP, A_PARENT, A_NAME = 0, 1, 2, 11
# WMOAreaTable.dbc: ID 0, WMOID 1, NameSetID 2, WMOGroupID 3, ..., AreaTableID 10.
W_ID, W_WMO, W_AREA = 0, 1, 10
MAX_CHAIN = 8  # world::kMaxAreaChain
# A new area whose WMO groups had no area in the stock table (the client used the ground's) and whose name the stock
# table doesn't have: the stock area of the ground it sits in, by hand. (name, continent) -> stock area name.
HINTS = {
    ("Frostmane Hovel", 0): "Coldridge Valley",  # Reforged's 6009: the Frostmane trolls' ice cave above Coldridge Valley
}


def find(folder, name):
    for f in os.listdir(folder):
        if f.lower() == name.lower():
            return os.path.join(folder, f)
    for root, _d, files in os.walk(folder):
        for f in files:
            if f.lower() == name.lower():
                return os.path.join(root, f)
    raise SystemExit(f"{folder}: no {name}")


def read_dbc(path):
    with open(path, "rb") as f:
        data = f.read()
    _fields, rows, string_at = io.read(data)
    return rows, string_at


class Areas:
    def __init__(self, folder):
        rows, s = read_dbc(find(folder, "AreaTable.dbc"))
        self.rec = {r[A_ID]: r for r in rows}
        self.name = {}
        for r in rows:
            try:
                self.name[r[A_ID]] = s(r[A_NAME])
            except (ValueError, UnicodeDecodeError):
                self.name[r[A_ID]] = ""
        wrows, _ = read_dbc(find(folder, "WMOAreaTable.dbc"))
        self.wmo = {r[W_ID]: r for r in wrows}

    def chain(self, area, remap=None):
        """As env/WorldQuery's AreaChain: the area and its parents, most specific first; with remap rows applied."""
        out = []
        while area and len(out) < MAX_CHAIN:
            if area in out:
                break
            out.append(area)
            r = self.rec.get(area)
            if r is None:
                break
            nxt = r[A_PARENT]
            row = remap.get(area) if remap else None
            if row and row["map"] == r[A_MAP] and row["parent"] == r[A_PARENT]:
                nxt = row["stock"]
            if nxt == area:
                break
            area = nxt
        return out


def label(areas, a):
    return f"{areas.name.get(a, '?')} {a}" if a else "none"


def main():
    args = sys.argv[1:]
    report = None
    if "--report" in args:
        i = args.index("--report")
        report = args[i + 1]
        del args[i:i + 2]
    if len(args) < 2:
        raise SystemExit(__doc__)
    data_dir = args[2] if len(args) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data")
    stock, pack = Areas(args[0]), Areas(args[1])
    lines = []

    def say(s=""):
        print(s)
        lines.append(s)

    remap = {}  # pack area -> {map, parent, stock, why}

    # 1. New areas with no parent, from the WMOAreaTable rows moved to them.
    moved = collections.defaultdict(collections.Counter)
    for k, w in pack.wmo.items():
        s = stock.wmo.get(k)
        if s is not None and s[W_AREA] != w[W_AREA] and w[W_AREA] not in stock.rec:
            moved[w[W_AREA]][s[W_AREA]] += 1
    new_roots = sorted(a for a, r in pack.rec.items() if a not in stock.rec and r[A_PARENT] == 0)
    for a in new_roots:
        r = pack.rec[a]
        src = [(s, n) for s, n in moved.get(a, {}).items() if s and s in stock.rec]
        src.sort(key=lambda sn: -sn[1])
        why = ""
        target = 0
        if src:
            target = src[0][0]
            why = f"{sum(moved[a].values())} WMO group(s) moved from it"
        else:
            same = [s for s, r0 in stock.rec.items() if stock.name.get(s) == pack.name.get(a) and r0[A_MAP] == r[A_MAP]]
            same.sort(key=lambda s: (stock.rec[s][A_PARENT] == 0, s))  # a sub-area before a top-level one
            if same:
                target = same[0]
                why = f"same name on the same continent ({sum(moved.get(a, {}).values())} WMO group(s) had no area)"
            elif (pack.name.get(a), r[A_MAP]) in HINTS:
                hint = HINTS[(pack.name.get(a), r[A_MAP])]
                same = [s for s, r0 in stock.rec.items() if stock.name.get(s) == hint and r0[A_MAP] == r[A_MAP]]
                if same:
                    target = same[0]
                    why = f"by hand: the stock client gave its {sum(moved.get(a, {}).values())} WMO group(s) the ground's area, {hint}"
        if not target:
            say(f"  no stock area for new area {a} \"{pack.name.get(a, '')}\" (map {r[A_MAP]}): left as it is")
            continue
        remap[a] = {"map": r[A_MAP], "parent": r[A_PARENT], "stock": target, "why": why}

    # 2. Stock areas whose chain the pack changed so it no longer reads as the stock one.
    for a in sorted(stock.rec):
        if a not in pack.rec or a in remap:
            continue
        want = stock.chain(a)
        got = [x for x in pack.chain(a, remap) if x in stock.rec]
        if got != want:
            r = pack.rec[a]
            remap[a] = {"map": r[A_MAP], "parent": r[A_PARENT], "stock": stock.rec[a][A_PARENT],
                        "why": f"the pack's parent {label(pack, r[A_PARENT])}, the stock table's {label(stock, stock.rec[a][A_PARENT])}"}

    # Check: every WMO group's area and every area.
    bad = []
    ground = collections.Counter()  # WMO groups the stock table gave no area (the client used the ground's): any
    for k, w in pack.wmo.items():
        s = stock.wmo.get(k)
        if s is None:
            continue
        if s[W_AREA] == 0:
            if w[W_AREA]:
                ground[w[W_AREA]] += 1
            continue
        want = stock.chain(s[W_AREA])
        got = [x for x in pack.chain(w[W_AREA], remap) if x in stock.rec]
        if got != want:
            bad.append(("WMOAreaTable", k, w[W_AREA], got, want))
    for a in pack.rec:
        if a in stock.rec:
            want = stock.chain(a)
            got = [x for x in pack.chain(a, remap) if x in stock.rec]
            if got != want:
                bad.append(("AreaTable", a, a, got, want))
    rows = []
    for i, a in enumerate(sorted(remap), 1):
        m = remap[a]
        tail = " < ".join(label(stock, x) for x in stock.chain(m["stock"])) if m["stock"] else "(chain ends)"
        comment = f"{pack.name.get(a, '')} {a} -> {tail}"
        rows.append((i, a, m["map"], m["parent"], m["stock"], comment))
    os.makedirs(data_dir, exist_ok=True)
    out = os.path.join(data_dir, "AreaRemap.cdbc")
    with open(out, "wb") as f:
        f.write(io.build(AREA_REMAP, rows))

    moved_rows = sum(sum(c.values()) for c in moved.values())
    say(f"{len(rows)} remap row(s) -> {out}")
    say(f"  {len([a for a in remap if a not in stock.rec])} new area(s) without a parent, {moved_rows} WMO group row(s) moved to new areas")
    say(f"  {len([a for a in remap if a in stock.rec])} stock area(s) whose pack parent changes their chain")
    for r in rows:
        say(f"  {r[1]:>5} map {r[2]:>3} parent {r[3]:>4} -> {r[4]:>4}  {r[5]}  ({remap[r[1]]['why']})")
    say(f"check: {len(pack.wmo)} WMO group rows and {len(pack.rec)} areas; {len(bad)} whose chain (stock ids) differs from the stock one")
    if ground:
        say(f"  {sum(ground.values())} WMO group row(s) had no area in the stock table (the client used the ground's) and have "
            f"one in the pack's; their chains now: " + "; ".join(
                f"{n} x {' < '.join(label(stock, x) for x in pack.chain(a, remap) if x in stock.rec) or label(pack, a)}"
                for a, n in ground.most_common()))
    for b in bad[:40]:
        say(f"  {b[0]} {b[1]} (area {b[2]}): {b[3]} vs stock {b[4]}")
    if report:
        with open(report, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
