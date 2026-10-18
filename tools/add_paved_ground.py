#!/usr/bin/env python3
"""Paved ground, and shallow mud ruts on Elwynn's dirt (ChromieCraft fork, 2026-10-17).

In a thunderstorm Elwynn's road grew a dark, jagged, raised trail behind every walker ("it also looks like it's
trying to draw mud trenches???", the user). The road is ElwynnCobblestoneBase painted over ElwynnDirtBase2, the dirt
showing at 7..67% between the stones, and that dirt carries upstream's mud (SurfaceCover row 12: MoistureMin 0.6), so
wet weather made mud on the cobbles; its rims (Rim 7 x 0.04 yd over a base 0.08 yd down) stood 0.24 yd proud.

  GroundMaterial 80 "Paved": Flags 0x2 (sealed: water runs off, no mud forms; features/SurfaceCoverFill holds a
  moisture-gated cover back by the share painted with it, blend::MudAllowed). Every other field -1, so moisture,
  stiffness and heat read exactly as with no material, and no SurfaceCover row: nothing else changes on it.
  GroundMaterialSelector 80..84: the cobbled and paved road textures of the old world, global scope (texture rows win
  over TerrainType rows). Northrend's flagstones and titan roads are left out: some lie in snow, and a texture row
  would take them off the global snow row.
  SurfaceCover row 12 (ElwynnDirtBase2, the mud upstream's tables put there): Depth 0.04 -> 0.02, ZOffset -0.08 ->
  -0.025, Rim 7 -> 1.75. The mud still sits just under the grass (0.005 yd) until feet push a rim up; a rim now
  stands 0.03 yd proud (0.02 x 2.75 - 0.025) instead of 0.24, and only where the dirt is painted at about half or
  more. Everything else in the row as it was.

Usage:  python add_paved_ground.py [dir]     (default: ../data; run it on <client>/DBFilesClient too)
Running it twice changes nothing.
"""

import sys
from pathlib import Path

import cdbc_io as io

PAVED = 80
FLAG_SEALED = 0x2
PAVED_TEXTURES = [
    r"Tileset\Elwynn\ElwynnCobblestoneBase.blp",
    r"Tileset\Duskwood\DuskwoodCobblestone.blp",
    r"Tileset\Stranglethorn\StranglethornCobblestoneRoad.blp",
    r"Tileset\Tirisfall\TirisfallStoneRoad01.blp",
    r"Tileset\BurningStepps\BurningSteppsBrick01.blp",
]
ELWYNN_MUD = 12
# SurfaceCover columns (io.SURFACE_COVER): 3 Depth, 8 Rim, 15 ZOffset.
ELWYNN_MUD_VALUES = {3: 0.02, 8: 1.75, 15: -0.025}


def decode(path, columns):
    _, raw, string_at = io.read(path.read_bytes())
    rows = []
    for r in raw:
        row = []
        for kind, value in zip(columns, r):
            row.append(io.as_float(value) if kind == "f" else io.as_int(value) if kind == "i" else string_at(value) if kind == "s" else value)
        rows.append(row)
    return rows


def main() -> None:
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent.parent / "data"
    mat_path, sel_path, cov_path = folder / "GroundMaterial.cdbc", folder / "GroundMaterialSelector.cdbc", folder / "SurfaceCover.cdbc"
    materials = decode(mat_path, io.GROUND_MATERIAL)
    selectors = decode(sel_path, io.GROUND_MATERIAL_SELECTOR)
    covers = decode(cov_path, io.SURFACE_COVER)
    changed = 0
    if not any(m[0] == PAVED for m in materials):
        # ID, Name, Parent, RestMoisture, Stiffness, Flags, Absorbency, Temperature
        materials.append([PAVED, "Paved", 0, -1.0, -1.0, FLAG_SEALED, -1.0, -1000.0])
        changed += 1
    have = {s[0] for s in selectors}
    for k, texture in enumerate(PAVED_TEXTURES):
        if PAVED + k in have:
            continue
        # ID, ScopeType, ScopeID, TexturePath, GroundEffectID, TerrainType, LiquidType, MaterialID, Flags
        selectors.append([PAVED + k, 0, 0, texture, 0, -1, 0, PAVED, 0])
        changed += 1
    for row in covers:
        if row[1] != ELWYNN_MUD:
            continue
        for column, value in ELWYNN_MUD_VALUES.items():
            if abs(row[column] - value) > 1e-6:
                row[column] = value
                changed += 1
    mat_path.write_bytes(io.build(io.GROUND_MATERIAL, [tuple(r) for r in materials]))
    sel_path.write_bytes(io.build(io.GROUND_MATERIAL_SELECTOR, [tuple(r) for r in selectors]))
    cov_path.write_bytes(io.build(io.SURFACE_COVER, [tuple(r) for r in covers]))
    print(f"{folder}: {changed} change(s) ({len(materials)} materials, {len(selectors)} selectors, {len(covers)} cover rows)")


if __name__ == "__main__":
    main()
