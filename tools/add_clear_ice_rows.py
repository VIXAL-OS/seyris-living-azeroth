#!/usr/bin/env python3
"""Add the ClearIce ground material (features/TerrainIce) to existing GroundMaterial.cdbc and
GroundMaterialSelector.cdbc files, keeping every row already there.

  GroundMaterial 30 "ClearIce": Flags 0x1 (the terrain draws it as clear ice), almost dry, takes no
  outside moisture, rigid, -10 degC. No SurfaceCover row: nothing lies on it (texture rows win over
  the global TerrainType=Snow row, so the snow cover stays off these textures).
  GroundMaterialSelector 30..38: the icy ground textures found by the map scan (waterfall-scan
  ice_summary.py), global scope. 39..41: the clear lake and sheet ice its name filter missed ("ice" inside a
  word: ZD_LakeIceB, the frozen lakes of Wintergrasp and Zul'Drak; DragonblightIceBlue; IG_GreenIceA). Left
  out on purpose: snow on ice (ZD_LakeIceC, the Dragonblight pack and melty ice) and frosted or dirty ice
  (Winterspring's, IG_GrayIceB, IG_DirtyIceD).

Usage:  python add_clear_ice_rows.py [dir]     (default: ../data; run it on <client>/DBFilesClient too)
Rows whose ID is already present are left alone, so running it twice changes nothing.
"""

import sys
from pathlib import Path

import cdbc_io as io

CLEAR_ICE = 30
ICE_TEXTURES = [
    r"Tileset\Expansion02\StormPeaks\SP_IceA.blp",
    r"Tileset\Expansion02\StormPeaks\SP_IceB.blp",
    r"Tileset\Winterspring Grove\WinterspringIceGlacier.blp",
    r"Tileset\Winterspring Grove\WinterspringIceGlacier2.blp",
    r"Tileset\Expansion02\IceCrown\IG_FrozenBonesA.blp",
    r"Tileset\Expansion02\IceCrown\IG_FrozenBonesB.blp",
    r"Tileset\Expansion02\IceCrown\IG_FrozenBonesC.blp",
    r"Tileset\Expansion02\IceCrown\IG_FrozenBonesD.blp",
    r"Tileset\Expansion02\Dragonblight\DB_FrozenBonesB.blp",
    r"Tileset\Expansion02\ZulDrak\ZD_LakeIceB.blp",
    r"Tileset\Expansion02\Dragonblight\DragonblightIceBlue.blp",
    r"Tileset\Expansion02\IceCrown\IG_GreenIceA.blp",
]


def decode(path, columns):
    _, raw, string_at = io.read(path.read_bytes())
    rows = []
    for r in raw:
        row = []
        for kind, value in zip(columns, r):
            row.append(io.as_float(value) if kind == "f" else io.as_int(value) if kind == "i" else string_at(value) if kind == "s" else value)
        rows.append(tuple(row))
    return rows


def main() -> None:
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent.parent / "data"
    mat_path, sel_path = folder / "GroundMaterial.cdbc", folder / "GroundMaterialSelector.cdbc"
    materials = decode(mat_path, io.GROUND_MATERIAL)
    selectors = decode(sel_path, io.GROUND_MATERIAL_SELECTOR)
    added = 0
    if not any(m[0] == CLEAR_ICE for m in materials):
        # ID, Name, Parent, RestMoisture, Stiffness, Flags, Absorbency, Temperature
        materials.append((CLEAR_ICE, "ClearIce", 0, 0.05, 1.0, 0x1, 0.0, -10.0))
        added += 1
    have = {s[0] for s in selectors}
    for k, texture in enumerate(ICE_TEXTURES):
        if CLEAR_ICE + k in have:
            continue
        # ID, ScopeType, ScopeID, TexturePath, GroundEffectID, TerrainType, LiquidType, MaterialID, Flags
        selectors.append((CLEAR_ICE + k, 0, 0, texture, 0, -1, 0, CLEAR_ICE, 0))
        added += 1
    mat_path.write_bytes(io.build(io.GROUND_MATERIAL, materials))
    sel_path.write_bytes(io.build(io.GROUND_MATERIAL_SELECTOR, selectors))
    print(f"{folder}: {added} row(s) added ({len(materials)} materials, {len(selectors)} selectors)")


if __name__ == "__main__":
    main()
