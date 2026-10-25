#!/usr/bin/env python3
"""The files the client will actually load: the stock archives plus a chosen patch chain, in load order.

Why (2026-10-25): the HD art packs (Project Reforged, ChromieCraft's Patch-T, Wrath HD's Patch-Y) replace window
textures, WMOs, map tiles and DBC tables our tables are made from. The generators (gen_glass.py, gen_area_remap.py,
gen_window_lights.py ...) read files; this tool says which version of a file a given install would load, and puts
exactly that version in a folder for them, so any pack combination regenerates cleanly.

A chain is a text file, one layer per line, LOWEST priority first, as the client loads them: the stock archives,
then the general patches in name order (digits before letters), then the locale ones (Data\\enUS\\patch-enUS-*.MPQ).
The last layer that has a file wins. A layer is
  - an MPQ, read through hdmpq (hd-compare's StormLib tool: `hdmpq list` and `hdmpq extract`), or
  - a folder of loose files laid out as in the archives (dungeons\\textures\\...), e.g. copies of the stock files
    extracted earlier, so nothing has to open the game's own MPQs while it runs.
After the path, optional tab-separated options:
  skip=<file>   paths (one per line, any case) this layer is taken NOT to have: a patch whose stock copies of some
                files the HD install leaves out (the compatibility patch's 12 window textures once LA fits them).
  prefix=<dir>  a folder layer's files sit under this archive folder (a flat folder of DBCs: prefix=DBFilesClient).
  flat          a folder layer's file names are whole archive paths with # for each backslash (waterscan
                --extract-list's and gen_glass.py's layout: dungeons#textures#windows#mm_stained_01.blp).
Lines starting with # are comments.

  python mpq_chain.py <chain> --which <path>...             which layer each path comes from
  python mpq_chain.py <chain> --extract <list> <out dir> [--flat]
                                                            each listed path's winning version to <out dir>\\<path>
                                                            (--flat: # for each backslash, gen_glass.py's layout;
                                                            list: one archive path per line); writes <out dir>\\
                                                            _chain.tsv (path, layer, size)
  options: --hdmpq <hdmpq.exe> (else $LA_HDMPQ, else hdmpq.exe on PATH), --cache <dir> for the MPQ listings
           (default: <out dir>\\_lists, or %TEMP%\\la-mpq-chain)

Reads only: never writes into an archive or a layer folder. hdmpq pauses by itself while a game that shares the
archive's drive runs.
"""
import csv
import hashlib
import os
import shutil
import subprocess
import sys

csv.field_size_limit(1 << 30)
IDLE = 0x40  # IDLE_PRIORITY_CLASS


def norm(path):
    return path.replace("/", "\\").lower().lstrip("\\")


class Layer:
    def __init__(self, path, skip=(), prefix="", flat=False):
        self.path = path
        self.flat = flat
        self.skip = set(skip)
        self.prefix = norm(prefix).rstrip("\\")
        self.is_dir = os.path.isdir(path)
        self._names = None  # lower-case archive path -> archive spelling (MPQ) or file path (folder)

    def __repr__(self):
        return f"Layer({self.path})"

    def names(self, hdmpq, cache):
        if self._names is None:
            self._names = {}
            if self.is_dir:
                for root, _dirs, files in os.walk(self.path):
                    for f in files:
                        full = os.path.join(root, f)
                        rel = norm(os.path.relpath(full, self.path))
                        if self.flat:
                            rel = rel.replace("#", "\\")
                        self._names[(self.prefix + "\\" + rel) if self.prefix else rel] = full
            else:
                for name in list_mpq(self.path, hdmpq, cache):
                    self._names[norm(name)] = name
            for s in self.skip:
                self._names.pop(s, None)
        return self._names


def list_mpq(mpq, hdmpq, cache):
    """Every named entry of an MPQ (hdmpq list, cached by the archive's path, size and time)."""
    st = os.stat(mpq)
    key = hashlib.sha1(f"{os.path.abspath(mpq).lower()}|{st.st_size}|{int(st.st_mtime)}".encode()).hexdigest()[:16]
    os.makedirs(cache, exist_ok=True)
    out = os.path.join(cache, f"{os.path.basename(mpq)}-{key}.tsv")
    if not os.path.exists(out):
        r = subprocess.run([hdmpq, "list", mpq, out + ".part"], capture_output=True, text=True,
                           creationflags=IDLE, cwd=os.path.dirname(os.path.abspath(hdmpq)))
        if r.returncode != 0 or not os.path.exists(out + ".part"):
            raise SystemExit(f"hdmpq list {mpq} failed: {r.stderr.strip()[:300]}")
        os.replace(out + ".part", out)
    names = []
    with open(out, encoding="utf-8", errors="replace", newline="") as f:
        for row in csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE):
            if row.get("named", "1") == "1" and not row["path"].startswith("("):
                names.append(row["path"])
    return names


def read_chain(path):
    layers = []
    base = os.path.dirname(os.path.abspath(path))
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = line.split("\t")
            p = parts[0].strip()
            skip = set()
            prefix = ""
            flat = False
            for opt in parts[1:]:
                opt = opt.strip()
                if opt == "flat":
                    flat = True
                elif opt.startswith("prefix="):
                    prefix = opt[7:]
                elif opt.startswith("skip="):
                    sp = opt[5:]
                    sp = sp if os.path.isabs(sp) else os.path.join(base, sp)
                    with open(sp, encoding="utf-8") as sf:
                        skip |= {norm(s.strip()) for s in sf if s.strip() and not s.startswith("#")}
            if not os.path.exists(p):
                raise SystemExit(f"chain {path}: no such layer {p}")
            layers.append(Layer(p, skip, prefix, flat))
    if not layers:
        raise SystemExit(f"chain {path}: no layers")
    return layers


def find_hdmpq(arg):
    for cand in (arg, os.environ.get("LA_HDMPQ"), shutil.which("hdmpq.exe"), shutil.which("hdmpq")):
        if cand and os.path.exists(cand):
            return cand
    return None


class Chain:
    def __init__(self, chain_file, hdmpq=None, cache=None):
        self.layers = read_chain(chain_file)
        self.hdmpq = find_hdmpq(hdmpq)
        if not self.hdmpq and any(not l.is_dir for l in self.layers):
            raise SystemExit("an MPQ layer needs hdmpq: --hdmpq <path> or LA_HDMPQ")
        self.cache = cache or os.path.join(os.environ.get("TEMP", "."), "la-mpq-chain")

    def winner(self, path):
        """(layer, its spelling of the path) for the version the client loads, or (None, None)."""
        key = norm(path)
        for layer in reversed(self.layers):
            names = layer.names(self.hdmpq, self.cache)
            if key in names:
                return layer, names[key]
        return None, None

    def extract(self, paths, out_dir, flat=False):
        """Each path's winning version to out_dir\\<path> (flat: # for each backslash). Returns {lower path: (layer path,
        size)}; missing ones absent."""
        os.makedirs(out_dir, exist_ok=True)
        by_layer = {}
        result = {}
        for p in paths:
            layer, spelled = self.winner(p)
            if layer is None:
                continue
            dest = os.path.join(out_dir, norm(p).replace("\\", "#") if flat else norm(p))
            by_layer.setdefault(layer, []).append((spelled, dest, norm(p)))
        for layer, items in by_layer.items():
            if layer.is_dir:
                for src, dest, key in items:
                    os.makedirs(os.path.dirname(dest), exist_ok=True)
                    shutil.copyfile(src, dest)
            else:
                lst = os.path.join(out_dir, "_extract-%s.txt" % hashlib.sha1(layer.path.lower().encode()).hexdigest()[:8])
                with open(lst, "w", encoding="utf-8") as f:
                    for spelled, dest, _key in items:
                        if os.path.exists(dest):
                            os.remove(dest)  # hdmpq skips an output of the right size: never keep another version's
                        f.write(f"{spelled}\t{dest}\n")
                r = subprocess.run([self.hdmpq, "extract", layer.path, lst], capture_output=True, text=True,
                                   creationflags=IDLE, cwd=os.path.dirname(os.path.abspath(self.hdmpq)))
                if r.returncode not in (0, 6):
                    raise SystemExit(f"hdmpq extract {layer.path} failed: {r.stderr.strip()[:300]}")
            for _s, dest, key in items:
                if os.path.exists(dest):
                    result[key] = (layer.path, os.path.getsize(dest))
        with open(os.path.join(out_dir, "_chain.tsv"), "w", encoding="utf-8", newline="") as f:
            f.write("path\tlayer\tsize\n")
            for key in sorted(result):
                f.write(f"{key}\t{result[key][0]}\t{result[key][1]}\n")
        return result


def main():
    args = sys.argv[1:]
    opts = {}
    for flag in ("--hdmpq", "--cache"):
        if flag in args:
            i = args.index(flag)
            opts[flag] = args[i + 1]
            del args[i:i + 2]
    if len(args) < 2:
        raise SystemExit(__doc__)
    chain = Chain(args[0], opts.get("--hdmpq"), opts.get("--cache"))
    if args[1] == "--which":
        for p in args[2:]:
            layer, _ = chain.winner(p)
            print(f"{p}\t{layer.path if layer else '(none)'}")
    elif args[1] == "--extract" and len(args) in (4, 5) and (len(args) == 4 or args[4] == "--flat"):
        with open(args[2], encoding="utf-8") as f:
            paths = [l.strip() for l in f if l.strip() and not l.startswith("#")]
        if "--cache" not in opts:
            chain.cache = os.path.join(args[3], "_lists")
        got = chain.extract(paths, args[3], flat=len(args) == 5)
        print(f"{len(got)} of {len(paths)} paths extracted to {args[3]}; {len(paths) - len(got)} in no layer")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
