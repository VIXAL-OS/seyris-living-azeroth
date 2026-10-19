// Crystals look like deep, semi-transparent gems, the way the ice does: the draenei's crystals (Azuremyst, Bloodmyst, the
// Exodar, Tempest Keep), Blade's Edge's, Hellfire's and Shadowmoon's fel-green formations, Netherstorm's gems, Shattrath's
// teal crystals, Crystalsong's white crystal trees, Dalaran's and the caves' blue crystals, Maraudon's amethysts, rubies
// and diamonds (also in the Exodar, Silvermoon and Karazhan), Un'Goro's, Silithus's and Sholazar's crystals, jewels.
// "Can we make crystals (like in draenei and blood elf areas and whatnot) semi-transparent like ice?" (the user,
// 2026-10-18).
//
// Which batches: CrystalMaterials.cdbc names the crystal textures (tools/gen_crystals.py; docs/cdbc-tables.md), by file
// name, and each its family (the colours below) and its own tweaks. A batch takes the look when its first texture is one
// and it draws opaque or alpha-keyed: the crystals' alpha shells, additive glows and env-map passes stay as the client
// draws them (they're see-through or glowing already), so a model's base crystal batch gets the depth under its shells.
//  - M2s: features/ModelIce's batch-draw hooks read the batch about to draw (the draw context's batch record: its
//    material's blend mode and flags, its first texture's name) once per batch record and model (ClassifyM2Batch).
//    The ice models keep the ice.
//  - WMOs: features/WmoIce's batch hooks, per material (its MOMT blend mode and its first texture), as for the ice.
//
// The look is the ice block's (features/ModelIce: behind b7, in the M2 and WMO pixel shaders' dynamic-shadow variants)
// with the crystal's constants and its own detail texture at s14 (BuildDetail: flat-shaded facets, the facets' edges and
// sparkles, sampled at two depths under the surface along the view, so they sit inside the crystal and shift as it
// turns): the texture's own colour as the body, darker and deeper in its family's hue toward the core (face on; c79), a
// Fresnel rim of its own colour (c80.w) and the sky's reflection at grazing angles, the sun's glint, and an inner glow in
// its family's colour, stronger at night (c81). A row's mask (c78) keeps the look to the texels of its colour on an atlas
// texture (Terokkar's crystals on their foliage, Netherstorm's gems on their rock).
// Registers (on top of the ice block's, ModelIce.hpp): c78 (mask rgb, 1 - its bias), c79 (tint rgb, deepening), c80 (.x
// the highlight lit as the surface is, .w the rim), c81 (glow rgb, the facets' share of it). All zeros (and what the ice's batches set) leave the ice's look
// exactly as it was.
//
// No real transparency: the batches still draw opaque. Blending them would show through them only what was drawn before
// them (the terrain, the WMOs; not the doodads behind them drawn later, which would come and go as the client re-sorts,
// nor the water), their own far faces would show or not by triangle order, and with depth writes on they'd hide the
// particles and see-through models behind them. The depth is faked, as the ice's.
#pragma once

#include "wxl/PluginApi.h"

#include <d3d9.h>

#include <cstdint>
#include <string>
#include <vector>

struct WXL_SeyrisCdbcApi;

namespace wxl_livingazeroth::crystal
{
    enum Family : uint32_t
    {
        kNoFamily = 0, kDraenei = 1, kNaaru = 2, kShattrath = 3, kBlood = 4, kFel = 5, kNether = 6, kCrystalsong = 7,
        kMana = 8, kArcane = 9, kRuby = 10, kPrism = 11, kFamilyCount = 12
    };

    /// A family's look: the tint the body deepens toward at the core, how far (at full depth); the rim's strength; the
    /// glow's colour and strength by day and its extra at night; the facets' size (yd a repeat of the detail); the
    /// colour the facets' edges and sparkles light toward.
    struct FamilyLook
    {
        const char* name;
        float tint[3];
        float deepen;
        float rim;
        float glow[3];
        float dayGlow, nightGlow;
        float facetYards;
        float highlight[3];
    };
    const FamilyLook& FamilyOf(uint32_t family);

    /// One CrystalMaterials row, its family's values filled in where the row inherits (-1; the mask at -1000 and below).
    struct Material
    {
        uint32_t id = 0, family = kPrism, flags = 0;
        std::string texture;          // the key: the file name, lower case, no extension
        std::string model;            // M2s only: the model's path must contain this (lower case, backslashes), "" any (WMOs: none)
        float weight = 1.0f;          // x the look (an atlas or a crystal under its own alpha shells: less)
        float tint[3] = { 1, 1, 1 };
        float glow[3] = { 1, 1, 1 };
        float dayGlow = 0.0f, nightGlow = 0.0f, deepen = 0.0f, rim = 0.0f, facetYards = 2.0f;
        float mask[4] = { 0, 0, 0, 1 };  // kept = saturate(dot(colour, mask.rgb) / (luminance + 0.04) + mask.w)
    };

    struct Settings
    {
        int   enabled = 1;      // CrystalLook
        float strength = 1.0f;  // CrystalStrength: x the look, 0..1
        float glow = 1.0f;      // CrystalGlow: x the inner glow, 0..3
    };
    Settings& Config();

    /// (Re)loads CrystalMaterials.cdbc (the lights panel's reload button too).
    void Load(const WXL_SeyrisCdbcApi* cdbc);
    /// The rows as a table would give them (tests): -1 inherits, as from the file.
    void SetMaterials(const std::vector<Material>& rows);
    /// A row as read, its inherited fields filled in from its family.
    Material Resolve(Material raw);
    uint32_t Generation();
    const char* TableStatus();

    /// The table's row for a texture path (any case, slashes, with or without .blp): index + 1, 0 when it isn't crystal.
    uint32_t MaterialOf(const char* texturePath);
    const Material* MaterialAt(uint32_t indexPlusOne);
    size_t MaterialCount();
    /// A file name's key: lower case, the last path part, no extension.
    std::string Key(const char* texturePath);

    /// Whether a batch drawn with this blend mode (the client's EGxBlend: 0 opaque, 1 alpha-keyed, 2 alpha, 3/4 additive,
    /// 5/6 modulate) takes the look.
    inline bool BlendTakesLook(uint32_t blend) { return blend <= 1; }
    constexpr uint16_t kUnlitBit = 0x8000;   // in a cached class: the client draws it unlit (its glow is halved)

    /// The M2 batch about to draw (the draw context the batch-draw hooks get): its batch record (the cache key; null when
    /// unreadable).
    const void* M2BatchKey(const void* ctx);
    /// ... and its class: the table's row + 1 of its first texture when it draws opaque or alpha-keyed (and its model is
    /// the row's, if it names one), | kUnlitBit when its material is unlit; 0 when it doesn't take the look or can't be read.
    uint16_t ClassifyM2Batch(const void* ctx, const char* modelPath);
    /// The same from the record's values (tests and the WMO side): blend mode, material flags (0x1 unlit), texture, the
    /// model's path (null: a WMO's material, which a row naming a model doesn't take).
    uint16_t Classify(uint32_t blend, uint32_t materialFlags, const char* texturePath, const char* modelPath = nullptr);

    constexpr int kConstant = 78;    // c78..c81
    constexpr int kRegisters = 4;
    /// One crystal batch's constants: the shared look (terrainice::FillModelConstants, c180..c189) adjusted in place, c179,
    /// c190 (as ModelIce's StyleConstants) and c78..c81, for its material, the settings and the night (0 day .. 1).
    void Constants(const Material& m, const Settings& s, float night, bool unlit, float* look, float c179[4], float c190[4],
                   float crystal[4 * kRegisters]);

    /// The detail texture's texels (A8R8G8B8, size x size, tiling): r = the facets' edges, g = sparkles, b = the facets'
    /// shading (flat per facet, a slight slope across each), a = 0.
    void BuildDetail(std::vector<uint32_t>& texels, int size);
    /// The detail texture (created on first use for this device, with its mip levels), or null.
    IDirect3DTexture9* DetailTexture(IDirect3DDevice9* device);

    /// On (CrystalLook), with a strength, and the table has rows.
    bool Active();
    /// Once per frame (OnUpdate, after features/Windows): the night factor the glow goes by.
    void Update();
    float Night();
    /// A crystal batch drawn (counted for the panel).
    void NoteBatch(bool wmo);

    const char* StatusLine();
    /// The panel section (the climate panel, under the ice).
    void DrawPanelSection(const WXL_Api* api);
}
