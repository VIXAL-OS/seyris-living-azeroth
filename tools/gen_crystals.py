#!/usr/bin/env python3
"""Crystals: which textures are crystal and in which family, as data (data/CrystalMaterials.cdbc, read by features/Crystal;
docs/cdbc-tables.md).

Why: "Can we make crystals (like in draenei and blood elf areas and whatnot) semi-transparent like ice? iirc some
crystals already have that effect but not all" (the user, 2026-10-18).

How the list was made (the map scan of 2026-10-18: every M2 placed by an ADT, every doodad of every placed WMO's sets,
every placed WMO's materials, every crystal-named M2 in the archives, each model's batches from its skin):
- Candidates by name (crystal, crystral, gem, shard, prism, geode, quartz, amethyst, ruby, diamond, naaru...) among the
  models and WMO textures, plus every texture of the placed models in the crystal zones' folders (Netherstorm, Blade's
  Edge, Azuremyst and Bloodmyst, the Exodar, Eversong and Silvermoon, Shattrath, Tempest Keep, Crystalsong, Sholazar,
  Ulduar, Dalaran...). Each texture was looked at (thumbnail sheets) and kept when its crystal texels are its point.
- Left out: the caves' "crystal" walls and rock (rock with a sheen), the naaru trims and walls (metal and stone), Dragonblight's
  shards (dark rock), the runestones and obelisks (stone), atlases where the crystal is a speck (the oracles' eggs, the
  titan generators, the crystal balls' frames, Bloodmyst's floating crystal's frame), and the ice (features/ModelIce,
  features/WmoIce already give Dragonblight's, Icecrown's, Utgarde's and the Nexus's ice shards the ice).
- At run time only a batch drawn opaque or alpha-keyed takes the look: the crystals' own alpha shells, additive glows
  and environment-map passes stay as they are, and so do crystals drawn only see-through or glowing (Dalaran's floating
  crystals, Zul'Drak's gems, the blood elves' power orbs, Tempest Keep's outer crystals, the Exodar's naaru crystals).

Families (features/Crystal's kFamilies hold their looks): 1 draenei violet, 2 naaru gold, 3 Shattrath teal, 4 blood red,
5 fel green, 6 nether purple, 7 Crystalsong white, 8 mana blue, 9 arcane violet, 10 ruby, 11 prism (the texture's own
hue; its glow colour comes with the row).

A row's -1 takes its family's value. Weight 0.8: a crystal under its own alpha shells (the Exodar's, Shattrath's and the
fel formations'), so the look and the shells don't add up to too much. A mask keeps the look to a texture's crystal
texels where it shares the texture with foliage, rock or a frame: kept = saturate(dot(colour, Mask) / (luminance + 0.04)
+ MaskBias), on the lit colour (so a hue test, whatever the light); review it with --check. A row naming a model
(MODEL_ONLY) applies only to that model's batches (and to no WMO).

Usage: python tools/gen_crystals.py [data dir] [--check <folder of the textures' BLPs>]
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import cdbc_io as io  # noqa: E402

CRYSTAL_MATERIALS = ["u", "s", "u", "u", "f", "f", "f", "f", "f", "f", "f", "f", "f", "f", "f", "f", "f", "f", "f", "f", "s", "s"]
#  ID Texture Family Flags Weight TintR TintG TintB GlowR GlowG GlowB Glow NightGlow Deepen Rim FacetSize
#  MaskR MaskG MaskB MaskBias Comment Model

DRAENEI, NAARU, SHATTRATH, BLOOD, FEL, NETHER, CRYSTALSONG, MANA, ARCANE, RUBY, PRISM = range(1, 12)
FAMILY_NAMES = {DRAENEI: "draenei violet", NAARU: "naaru gold", SHATTRATH: "Shattrath teal", BLOOD: "blood red",
                FEL: "fel green", NETHER: "nether purple", CRYSTALSONG: "Crystalsong white", MANA: "mana blue",
                ARCANE: "arcane violet", RUBY: "ruby", PRISM: "prism"}

# Masks (r, g, b, bias): the hue of the crystal texels against what shares the texture with them.
BLUE = (-2.0, -2.0, 4.0, -0.4)        # blue against a grey or gold frame
PALE_BLUE = (-4.0, -4.0, 8.0, -0.3)   # pale blue-white crystal against green foliage, brown bark, grey stone
COOL = (-2.0, 0.0, 2.0, 0.8)          # blue and white against gold
TEAL = (-4.0, 2.0, 2.0, -0.3)         # teal against bronze, stone, wood
RED = (4.0, -2.0, -2.0, -0.9)         # red against stone and gold
GREEN = (-2.0, 4.0, -2.0, -0.5)       # green gems against violet rock, grey stone
PURPLE = (2.0, -4.0, 2.0, -0.9)       # vivid purple against grey stone
NETHER_GEMS = (1.5, -3.0, 1.5, -1.3)  # Netherstorm's violet gems against its violet-grey rock: only the vivid ones
NETHER_GREEN = (-2.5, 5.0, -2.5, -0.7)

# Textures shared with what isn't crystal at all: only these models' batches take the look (M2s; part of the path).
# Terokkar's atlas is its trees', bushes' and stumps' too (13,852 of them): only its crystal formations, so the block
# doesn't run on a forest's pixels the mask would only leave alone.
MODEL_ONLY = {"terrokarfoliage01": "terokkarcrystal"}

# texture (file name), family, weight (-1: 1), mask or None, glow colour or None (-1: the family's), where (comment)
TEXTURES = [
    # draenei violet: Azuremyst's Silvermyst crystals, the Exodar, the draenei buildings, the nether collectors
    ("jlo_draenei_crystal01", DRAENEI, -1, None, None, "Silvermyst's small crystals (Azuremyst), draenei buildings' crystal (WMO)"),
    ("jlo_draenei_crystal02", DRAENEI, -1, None, None, "draenei buildings, Auchindoun, the Black Temple's facade (WMO)"),
    ("jlo_draenei_crystal03", DRAENEI, -1, None, None, "the Exodar, the nether collectors (WMO)"),
    ("jlo_draenei_crystal03_edit", DRAENEI, -1, None, None, "the Exodar's pure naaru crystals (narru_crystal_pure, 218 in the Exodar and Tempest Keep)"),
    ("jlo_draenei_crystalwall", DRAENEI, -1, None, None, "the Exodar's and Tempest Keep's crystal walls (WMO)"),
    ("jlo_draenei_crystalwall_02", DRAENEI, -1, None, None, "the Exodar, the nether collectors, Tempest Keep (WMO)"),
    ("jlo_naaru_crystalrock", DRAENEI, -1, None, None, "the Exodar's and Tempest Keep's crystal rock (WMO)"),
    ("silvermystcrystalouter", DRAENEI, -1, None, None, "Silvermyst's big crystals (Azuremyst), under their spec shell"),
    ("jg_draenei_crystal04_edit2", DRAENEI, 0.8, None, None, "the Exodar's big and thin crystals (891), Tempest Keep's; under their alpha shells"),
    ("jg_draenei_crystal04_edit", DRAENEI, 0.8, None, None, "the same crystals' second texture"),
    # naaru gold: the Exodar's yellow crystals, Ulduar's crystal giant
    ("jg_draenei_crystal04_edit2_yellow", NAARU, 0.8, None, None, "the Exodar's yellow crystals (279), under their alpha shells"),
    ("jg_draenei_crystal04_edit2_yellow2", NAARU, 0.8, None, None, "the Exodar's yellow crystals"),
    ("jlo_draenei_crystal01_yellow", NAARU, -1, None, None, "the Exodar (WMO)"),
    ("jlo_draenei_crystal02_yellow", NAARU, -1, None, None, "the Exodar (WMO)"),
    ("mountaingiantcrystal_crystal_gold", NAARU, -1, None, None, "Ulduar's crystal giants (ul_mountaingiantcrystal)"),
    # Shattrath teal
    ("shattrath_crystal_orig", SHATTRATH, 0.8, None, None, "Shattrath's crystals on its buildings (295) and Hellfire's outposts, under their alpha shells"),
    ("jac_shatrath_crystal_gem_01", SHATTRATH, -1, None, None, "Shattrath's and the Black Temple's crystal gems (WMO)"),
    ("shattrath_crystal", SHATTRATH, -1, TEAL, None, "Shattrath's and Hellfire's draenei buildings: the crystal half of the texture (WMO)"),
    ("crystalhanging", SHATTRATH, -1, TEAL, None, "Outland's hanging crystals: not the rope"),
    # blood red: Bloodmyst, Silvermyst's pink crystals, Tempest Keep's red
    ("bloodmyst_crystal_001", BLOOD, -1, None, None, "Bloodmyst's crystals (Bloodmyst Isle: 110 with the floating ones and the apparatus)"),
    ("bloodmystcrystal_corrupted", BLOOD, -1, None, None, "Bloodmyst's corrupted crystals"),
    ("bloodmyst_tree_crystal", BLOOD, -1, None, None, "Bloodmyst's crystal trees' crystal"),
    ("bloodmyst_crystal_core_01", BLOOD, -1, None, None, "Bloodmyst's power core"),
    ("silvermystcrystal_001_pink", BLOOD, -1, None, None, "Silvermyst's pink crystals (Azuremyst)"),
    ("jlo_draenei_crystal01red", BLOOD, -1, None, None, "Tempest Keep's red crystal (WMO)"),
    ("jlo_draenei_crystal03_red", BLOOD, -1, None, None, "Tempest Keep's arcatraz wing (WMO)"),
    ("torgo_draenei_crystal01", BLOOD, -1, None, None, "Tempest Keep (WMO)"),
    ("torgo_draenei_crystal02", BLOOD, -1, None, None, "Tempest Keep (WMO)"),
    ("torgo_draenei_crystal03", BLOOD, -1, None, None, "Tempest Keep (WMO)"),
    ("torgo_draenei_crystalwall", BLOOD, -1, None, None, "Tempest Keep (WMO)"),
    ("torgo_draenei_crystalwall_02", BLOOD, -1, None, None, "Tempest Keep (WMO)"),
    ("torgo_naaru_crystalrock", BLOOD, -1, None, None, "Tempest Keep (WMO)"),
    # fel green: Blade's Edge (Ogri'la), Hellfire, Shadowmoon (the chromatic formations), Silvermoon's demon crystals
    ("hellfirecrystal01", FEL, 0.8, None, None, "Shadowmoon's crystal formations (1,435), Ogri'la's (Blade's Edge, 47), Hellfire's (18); under their shells"),
    ("be_demoncrystal_001", FEL, 0.8, None, None, "Silvermoon's demon crystals (140), under their alpha shell"),
    ("zigguratcrystal", FEL, -1, GREEN, None, "the Scourge's ziggurat crystals"),
    ("blktemple_crystal", FEL, -1, GREEN, None, "the Black Temple and Shadowmoon's battlements: the crystal (WMO)"),
    # nether purple: Netherstorm's gems in its rock
    ("netherstorm_rock_gems", NETHER, -1, NETHER_GREEN, (0.45, 1.0, 0.5), "Netherstorm's rocks: the green gems (ns_rock_01)"),
    ("netherstorm_rock_gems_02", NETHER, -1, NETHER_GREEN, (0.45, 1.0, 0.5), "Netherstorm's rocks: the green gems (ns_rock_02)"),
    ("netherstorm_rock_gems_03", NETHER, -1, NETHER_GEMS, None, "Netherstorm's rocks: the violet gems (ns_rock_09)"),
    ("netherstorm_rock_gems_04", NETHER, -1, NETHER_GEMS, None, "Netherstorm's rocks: the violet gems (ns_rock_10)"),
    # Crystalsong white, and the other white crystals
    ("crystalsong_magical_combine01", CRYSTALSONG, -1, None, None, "Crystalsong's crystal trees (695)"),
    ("crystalsong_magical_combine03", CRYSTALSONG, -1, None, None, "Crystalsong's crystal trees (227)"),
    ("crystalsong_magicleaves03", CRYSTALSONG, -1, None, None, "Crystalsong's crystal bushes (620)"),
    ("crystalsong_magictrunk01b", CRYSTALSONG, -1, None, None, "Crystalsong's crystal bushes' trunks (323)"),
    ("jg_draenei_crystal_white_edit2", CRYSTALSONG, 0.8, None, None, "Nagrand's Diamond Mountain crystals (120), under their alpha shells"),
    ("jg_draenei_crystal_white_edit", CRYSTALSONG, 0.8, None, None, "the same crystals' second texture"),
    ("jlt_draenei_crystal02white", CRYSTALSONG, -1, None, None, "Diamond Mountain (WMO)"),
    ("sholazarcrystalwhite", CRYSTALSONG, -1, None, None, "Sholazar's white crystals (52)"),
    # mana blue: Dalaran, Silvermoon's and Karazhan's diamonds, the cave mines' crystals, Tempest Keep's blue
    ("maraudondiamond01", MANA, -1, None, None, "diamonds: Silvermoon (135), Karazhan (126), Maraudon (420 in all)"),
    ("cavecrystal01_64", MANA, -1, None, None, "the cave mines' crystal formations (103)"),
    ("eb_dalaran_tinycrystal", MANA, -1, COOL, None, "Dalaran's small crystals: not their gold mounts (WMO)"),
    ("dal_crystalcieling", MANA, -1, BLUE, None, "Dalaran's crystal ceiling: not its gold frame (WMO)"),
    ("dalaran_fountaincrystal_01", MANA, -1, None, None, "Dalaran's fountain crystal (WMO)"),
    ("jlo_draenei_crystal01_blue", MANA, -1, None, None, "Tempest Keep's factory wing (WMO)"),
    ("jlo_draenei_crystal03_blue", MANA, -1, None, None, "Tempest Keep's factory wing (WMO)"),
    ("nex_rock_crystal", MANA, -1, None, None, "the Nexus's blue crystal rock (WMO)"),
    ("terrokarfoliage01", MANA, -1, PALE_BLUE, None, "Terokkar's crystal formations (1,784): their crystal corner of the atlas"),
    ("humancrystal01", MANA, -1, None, None, "Stranglethorn's crystallized humans"),
    ("trollcrystal01", MANA, -1, None, None, "Stranglethorn's crystallized trolls"),
    # arcane violet: amethysts (Maraudon, and the Exodar's and Silvermoon's decor), Gundrak, Ironforge, the Scourge
    ("maraudoncrystalpurple02", ARCANE, -1, None, None, "amethysts: Maraudon, the Exodar (650), Karazhan, Silvermoon (2,032 in all)"),
    ("maraudoncrystalpurple01", ARCANE, -1, None, None, "amethysts (339)"),
    ("gundrak_crystal_01", ARCANE, -1, None, None, "Gundrak's crystals"),
    ("mithril_crystal", ARCANE, -1, None, None, "Ironforge's throne room crystal roof"),
    ("creaturescourgecrystal_purple", ARCANE, -1, PURPLE, None, "the Scourge's crystals (necropolises: 175): the crystal, not the stone"),
    ("mountaingiantcrystal_crystal_purple", ARCANE, -1, None, None, "crystal giants (Crystalsong, Gundrak's entrance)"),
    ("g_jewelblack", ARCANE, -1, None, None, "jewelcrafting's purple gems (where drawn opaque)"),
    # ruby
    ("maraudoncrystalred02", RUBY, -1, None, None, "rubies: Maraudon (1,329), Karazhan"),
    ("maraudoncrystalred", RUBY, -1, None, None, "rubies (243)"),
    ("ungorocrystalred", RUBY, -1, None, None, "Un'Goro's red crystals, the museum gem (Blackrock)"),
    ("sholazar_deep_red", RUBY, -1, None, None, "Sholazar's geode crystals (91)"),
    ("silithus_crystal_color_red", RUBY, -1, None, None, "Silithus's floating red crystals (100)"),
    ("g_jewelred", RUBY, -1, None, None, "jewelcrafting's rubies (372)"),
    ("eb_dalaran_tinycrystalred", RUBY, -1, RED, None, "Dalaran's and its prison's red crystals: not their gold mounts (WMO)"),
    ("jewelcrafting_jewel", RUBY, -1, RED, None, "Dalaran's jewelcrafting shop: the ruby, not its frame (WMO)"),
    # prism: the texture's own hue (its glow colour here)
    ("ungorocrystalblue", PRISM, -1, None, (0.30, 0.70, 1.00), "Un'Goro's blue crystals (373), and in the Exodar and Dalaran"),
    ("ungorocrystalgreen", PRISM, -1, None, (0.45, 1.00, 0.30), "Un'Goro's green crystals (116)"),
    ("ungorocrystalyellow", PRISM, -1, None, (1.00, 0.85, 0.30), "Un'Goro's yellow crystals (194)"),
    ("silithus_crystal_color", PRISM, -1, None, (0.70, 0.45, 1.00), "Silithus's crystal formations, clumps, nubs and spikes (900), and in Ulduar"),
    ("cavecrystal03_64", PRISM, -1, None, (0.70, 1.00, 0.35), "the cave crystal rocks (gemrock01)"),
    ("g_jewelblue01", PRISM, -1, None, (0.25, 0.45, 1.00), "jewelcrafting's sapphires (115)"),
    ("g_jewelblueheart", PRISM, -1, None, (0.35, 0.55, 1.00), "jewelcrafting's blue heart gem"),
    ("ce_saronitecrystal03", PRISM, -1, None, (0.25, 1.00, 0.85), "Ulduar's saronite crystals (WMO)"),
    ("ce_saronitecrystal05", PRISM, -1, None, (0.25, 1.00, 0.85), "Ulduar's saronite crystals (WMO)"),
    ("ul_crystal_orb", PRISM, -1, TEAL, (0.35, 1.00, 0.90), "Ulduar's crystal lamps (3,950): the orb, not the bronze"),
]


def rows():
    out = []
    for i, (texture, family, weight, mask, glow, comment) in enumerate(TEXTURES, start=1):
        m = mask if mask else (-1000.0, -1000.0, -1000.0, -1000.0)
        g = glow if glow else (-1.0, -1.0, -1.0)
        out.append((i, texture, family, 0, float(weight), -1.0, -1.0, -1.0, g[0], g[1], g[2], -1.0, -1.0, -1.0, -1.0, -1.0,
                    m[0], m[1], m[2], m[3], f"{texture}: {FAMILY_NAMES[family]}; {comment}", MODEL_ONLY.get(texture, "")))
    return out


def check(folder):
    """Per masked texture, the share of its texels the look keeps (a review aid; needs Pillow)."""
    from PIL import Image
    import numpy as np
    for texture, family, weight, mask, glow, comment in TEXTURES:
        hits = [p for p in pathlib.Path(folder).iterdir() if p.name.lower().endswith(texture + ".blp") and
                p.name.lower().replace("#", "/").split("/")[-1] == texture + ".blp"]
        if not hits:
            print(f"{texture:36s} (no file)")
            continue
        rgb = np.asarray(Image.open(hits[0]).convert("RGB"), dtype=np.float32) / 255.0
        if mask is None:
            print(f"{texture:36s} whole")
            continue
        lum = rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
        kept = np.clip(rgb @ np.array(mask[:3], dtype=np.float32) / (lum + 0.04) + mask[3], 0.0, 1.0)
        print(f"{texture:36s} kept {kept.mean():.2f} (texels over half: {(kept > 0.5).mean():.2f})")


def main():
    args = sys.argv[1:]
    if "--check" in args:
        at = args.index("--check")
        check(args[at + 1])
        del args[at:at + 2]
    folder = pathlib.Path(args[0]) if args else pathlib.Path(__file__).resolve().parent.parent / "data"
    data = io.build(CRYSTAL_MATERIALS, rows())
    path = folder / "CrystalMaterials.cdbc"
    old = path.read_bytes() if path.exists() else None
    if data == old:
        print(f"{path}: unchanged ({len(TEXTURES)} crystal textures)")
        return
    path.write_bytes(data)
    print(f"{path}: written ({len(TEXTURES)} crystal textures)")


if __name__ == "__main__":
    main()
