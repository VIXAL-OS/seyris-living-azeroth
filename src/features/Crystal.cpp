#include "Crystal.hpp"

#include "../env/CdbcLoad.hpp"
#include "../wxl_seyris/CdbcApi.hpp"
#include "Windows.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace wxl_livingazeroth::crystal
{
    namespace
    {
        // The families' looks. The tint is what the body deepens toward at the core (x its own colour: a crystal's
        // light goes further through it face on), so a pale texture turns its family's hue and a saturated one deepens
        // in its own; the prism family keeps the texture's own hue (white) and its rows bring their glow colour.
        const FamilyLook kFamilies[kFamilyCount] = {
            // name            tint                 deepen rim    glow                 day    night  facet  highlight
            { "none",          { 1.0f, 1.0f, 1.0f }, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f,  0.0f,  2.0f, { 1.0f, 1.0f, 1.0f } },
            { "draenei",       { 0.80f, 0.74f, 1.0f }, 0.60f, 0.35f, { 0.50f, 0.42f, 1.0f }, 0.05f, 0.22f, 3.0f, { 0.94f, 0.92f, 1.08f } },
            { "naaru gold",    { 1.0f, 0.86f, 0.60f }, 0.50f, 0.35f, { 1.0f, 0.78f, 0.38f }, 0.06f, 0.25f, 3.0f, { 1.08f, 1.02f, 0.90f } },
            { "shattrath teal",{ 0.70f, 1.0f, 0.94f }, 0.50f, 0.30f, { 0.35f, 1.0f, 0.88f }, 0.05f, 0.22f, 2.5f, { 0.92f, 1.08f, 1.02f } },
            { "blood red",     { 1.0f, 0.60f, 0.66f }, 0.60f, 0.30f, { 1.0f, 0.28f, 0.36f }, 0.05f, 0.22f, 2.5f, { 1.08f, 0.92f, 0.94f } },
            { "fel green",     { 0.66f, 1.0f, 0.60f }, 0.55f, 0.30f, { 0.40f, 1.0f, 0.32f }, 0.05f, 0.24f, 3.0f, { 0.94f, 1.08f, 0.92f } },
            { "nether purple", { 0.86f, 0.62f, 1.0f }, 0.60f, 0.30f, { 0.78f, 0.34f, 1.0f }, 0.06f, 0.26f, 1.2f, { 1.02f, 0.92f, 1.08f } },
            { "crystalsong",   { 0.94f, 0.95f, 1.0f }, 0.30f, 0.45f, { 0.82f, 0.86f, 1.0f }, 0.04f, 0.18f, 2.0f, { 1.10f, 1.10f, 1.14f } },
            { "mana blue",     { 0.66f, 0.86f, 1.0f }, 0.50f, 0.35f, { 0.35f, 0.72f, 1.0f }, 0.05f, 0.24f, 2.0f, { 0.94f, 1.02f, 1.10f } },
            { "arcane violet", { 0.82f, 0.62f, 1.0f }, 0.60f, 0.30f, { 0.62f, 0.32f, 1.0f }, 0.05f, 0.22f, 2.0f, { 1.02f, 0.94f, 1.08f } },
            { "ruby",          { 1.0f, 0.50f, 0.55f }, 0.65f, 0.30f, { 1.0f, 0.22f, 0.26f }, 0.04f, 0.20f, 1.5f, { 1.08f, 0.92f, 0.92f } },
            { "prism",         { 1.0f, 1.0f, 1.0f }, 0.50f, 0.30f, { 1.0f, 1.0f, 1.0f }, 0.04f, 0.18f, 2.0f, { 1.06f, 1.06f, 1.06f } },
        };

        // Detail parallax and edges fade out from 60 to 180 yd (ModelIce's block, r22.w), as on the ice.
        constexpr float kDetailFadeStart = 60.0f, kDetailFadeEnd = 180.0f;

        constexpr const char* kFile = "DBFilesClient\\CrystalMaterials.cdbc";
        // Column layout (append-only, docs/cdbc-tables.md; tools/gen_crystals.py CRYSTAL_MATERIALS).
        constexpr WXL_SeyrisCdbcField kFields[] = {
            {"ID", 0, WXL_CDBC_FIELD_VALUE},         {"Texture", 1, WXL_CDBC_FIELD_STRING},   {"Family", 2, WXL_CDBC_FIELD_VALUE},
            {"Flags", 3, WXL_CDBC_FIELD_VALUE},      {"Weight", 4, WXL_CDBC_FIELD_VALUE},     {"TintR", 5, WXL_CDBC_FIELD_VALUE},
            {"TintG", 6, WXL_CDBC_FIELD_VALUE},      {"TintB", 7, WXL_CDBC_FIELD_VALUE},      {"GlowR", 8, WXL_CDBC_FIELD_VALUE},
            {"GlowG", 9, WXL_CDBC_FIELD_VALUE},      {"GlowB", 10, WXL_CDBC_FIELD_VALUE},     {"Glow", 11, WXL_CDBC_FIELD_VALUE},
            {"NightGlow", 12, WXL_CDBC_FIELD_VALUE}, {"Deepen", 13, WXL_CDBC_FIELD_VALUE},    {"Rim", 14, WXL_CDBC_FIELD_VALUE},
            {"FacetSize", 15, WXL_CDBC_FIELD_VALUE}, {"MaskR", 16, WXL_CDBC_FIELD_VALUE},     {"MaskG", 17, WXL_CDBC_FIELD_VALUE},
            {"MaskB", 18, WXL_CDBC_FIELD_VALUE},     {"MaskBias", 19, WXL_CDBC_FIELD_VALUE},  {"Comment", 20, WXL_CDBC_FIELD_STRING},
            {"Model", 21, WXL_CDBC_FIELD_STRING},
        };
        constexpr WXL_SeyrisCdbcDefinition kDef = { "CrystalMaterials", kFile, kFields, 22 };

        Settings g_settings;
        std::vector<Material> g_materials;
        std::unordered_map<std::string, uint32_t> g_byTexture;
        uint32_t g_generation = 1;
        std::string g_tableStatus = "CrystalMaterials.cdbc: not loaded yet";
        std::string g_status;
        float g_night = 0.0f;
        unsigned g_m2Batches = 0, g_wmoBatches = 0, g_lastM2 = 0, g_lastWmo = 0;

        IDirect3DTexture9* g_detail = nullptr;
        IDirect3DDevice9* g_detailDevice = nullptr;
        bool g_detailFailed = false;
        constexpr int kDetailSize = 256, kDetailLevels = 9;

        float Clamp01(float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }

        // --- the detail texture ------------------------------------------------------------------------------------
        uint32_t Hash(uint32_t x)
        {
            x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
            return x;
        }
        float Rand(int i, int j, uint32_t seed)
        {
            return static_cast<float>(Hash(static_cast<uint32_t>(i) * 73856093u ^ static_cast<uint32_t>(j) * 19349663u ^ seed * 83492791u) & 0xFFFFFF) / 16777216.0f;
        }
        int Wrap(int i, int p) { return ((i % p) + p) % p; }
        float SmoothStep(float a, float b, float x)
        {
            const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        // The nearest and second-nearest sites of an n x n tiling Voronoi (distances in cells), the nearest's cell (its
        // id, wrapped) and the offset from its site (cells). The second's cell for the edge between them.
        struct Cells { float f1 = 9.0f, f2 = 9.0f, dx = 0.0f, dy = 0.0f; int i1 = 0, j1 = 0, i2 = 0, j2 = 0; };
        Cells Voronoi(float x, float y, int n, uint32_t seed)
        {
            Cells c;
            const float fx = x * n, fy = y * n;
            const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
            for (int dj = -2; dj <= 2; ++dj)
                for (int di = -2; di <= 2; ++di)
                {
                    const int ci = ix + di, cj = iy + dj, wi = Wrap(ci, n), wj = Wrap(cj, n);
                    const float px = ci + 0.1f + 0.8f * Rand(wi, wj, seed), py = cj + 0.1f + 0.8f * Rand(wi, wj, seed + 101);
                    const float d = std::sqrt((px - fx) * (px - fx) + (py - fy) * (py - fy));
                    if (d < c.f1) { c.f2 = c.f1; c.i2 = c.i1; c.j2 = c.j1; c.f1 = d; c.i1 = wi; c.j1 = wj; c.dx = fx - px; c.dy = fy - py; }
                    else if (d < c.f2) { c.f2 = d; c.i2 = wi; c.j2 = wj; }
                }
            return c;
        }

        // A facet's shading: flat per cell, sloping a little across it along its own direction (a plane catching light).
        float Facet(const Cells& c, uint32_t seed)
        {
            const float base = 0.12f + 0.85f * Rand(c.i1, c.j1, seed + 7);
            const float a = 6.2831853f * Rand(c.i1, c.j1, seed + 13);
            return std::clamp(base + 0.5f * (c.dx * std::cos(a) + c.dy * std::sin(a)), 0.0f, 1.0f);
        }

        // An edge between two facets: a thin straight line where the two sites are nearly as far, its strength the pair's;
        // only some pairs have one (an inner plane catching the light, not a web of cracks).
        float Edge(const Cells& c, float width, uint32_t seed)
        {
            const int lo = std::min(c.i1 * 64 + c.j1, c.i2 * 64 + c.j2), hi = std::max(c.i1 * 64 + c.j1, c.i2 * 64 + c.j2);
            if (Rand(lo, hi, seed + 31) < 0.45f) return 0.0f;
            const float strength = 0.2f + 0.5f * Rand(lo, hi, seed + 29);
            return (1.0f - SmoothStep(0.0f, width, c.f2 - c.f1)) * strength;
        }

        bool EnsureDetail(IDirect3DDevice9* dev)
        {
            if (g_detail && g_detailDevice == dev) return true;
            if (g_detailDevice == dev && g_detailFailed) return false;
            if (g_detail) { g_detail->Release(); g_detail = nullptr; }
            g_detailDevice = dev;
            g_detailFailed = true;
            std::vector<uint32_t> level;
            BuildDetail(level, kDetailSize);
            IDirect3DTexture9* tex = nullptr;
            if (FAILED(dev->CreateTexture(kDetailSize, kDetailSize, kDetailLevels, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) || !tex)
                return false;
            int size = kDetailSize;
            for (int l = 0; l < kDetailLevels; ++l)
            {
                D3DLOCKED_RECT lr;
                if (FAILED(tex->LockRect(l, &lr, nullptr, 0))) { tex->Release(); return false; }
                for (int y = 0; y < size; ++y) std::memcpy(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch, level.data() + static_cast<size_t>(y) * size, size * 4);
                tex->UnlockRect(l);
                if (size == 1) break;
                // The next level: 2 x 2 box filter, per channel.
                const int half = size / 2;
                std::vector<uint32_t> next(static_cast<size_t>(half) * half);
                for (int y = 0; y < half; ++y)
                    for (int x = 0; x < half; ++x)
                    {
                        uint32_t out = 0;
                        for (int c = 0; c < 32; c += 8)
                        {
                            const size_t a = static_cast<size_t>(2 * y) * size + 2 * x, b = static_cast<size_t>(2 * y + 1) * size + 2 * x;
                            const uint32_t s = ((level[a] >> c) & 255) + ((level[a + 1] >> c) & 255) + ((level[b] >> c) & 255) + ((level[b + 1] >> c) & 255);
                            out |= ((s + 2) / 4) << c;
                        }
                        next[static_cast<size_t>(y) * half + x] = out;
                    }
                level.swap(next);
                size = half;
            }
            g_detail = tex;
            g_detailFailed = false;
            return true;
        }

        void Rebuild(std::vector<Material> rows, std::string status)
        {
            g_materials.clear();
            g_byTexture.clear();
            for (Material& raw : rows)
            {
                if (raw.texture.empty() || raw.family == kNoFamily) continue;
                Material m = Resolve(raw);
                g_materials.push_back(m);
                g_byTexture[m.texture] = static_cast<uint32_t>(g_materials.size());
            }
            ++g_generation;
            g_tableStatus = std::move(status);
        }
    }

    const FamilyLook& FamilyOf(uint32_t family) { return kFamilies[family < kFamilyCount ? family : kPrism]; }
    Settings& Config() { return g_settings; }
    uint32_t Generation() { return g_generation; }
    const char* TableStatus() { return g_tableStatus.c_str(); }
    size_t MaterialCount() { return g_materials.size(); }
    float Night() { return g_night; }

    std::string Key(const char* path)
    {
        std::string p(path ? path : "");
        const size_t slash = p.find_last_of("\\/");
        if (slash != std::string::npos) p = p.substr(slash + 1);
        for (char& c : p) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        if (p.size() > 4 && p.compare(p.size() - 4, 4, ".blp") == 0) p.resize(p.size() - 4);
        return p;
    }

    Material Resolve(Material m)
    {
        m.texture = Key(m.texture.c_str());
        for (char& c : m.model) c = c == '/' ? '\\' : static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        if (m.family == kNoFamily || m.family >= kFamilyCount) m.family = kPrism;
        const FamilyLook& f = FamilyOf(m.family);
        // -1 (and below) inherits; the mask's fields are signed: -1000 and below.
        auto pick = [](float v, float inherit) { return std::isfinite(v) && v > -0.5f ? v : inherit; };
        m.weight = std::clamp(pick(m.weight, 1.0f), 0.0f, 1.0f);
        for (int k = 0; k < 3; ++k)
        {
            m.tint[k] = std::clamp(pick(m.tint[k], f.tint[k]), 0.0f, 1.5f);
            m.glow[k] = std::clamp(pick(m.glow[k], f.glow[k]), 0.0f, 2.0f);
        }
        m.dayGlow = std::clamp(pick(m.dayGlow, f.dayGlow), 0.0f, 1.0f);
        m.nightGlow = std::clamp(pick(m.nightGlow, f.nightGlow), 0.0f, 1.0f);
        m.deepen = std::clamp(pick(m.deepen, f.deepen), 0.0f, 1.0f);
        m.rim = std::clamp(pick(m.rim, f.rim), 0.0f, 1.0f);
        m.facetYards = std::clamp(pick(m.facetYards, f.facetYards), 0.2f, 40.0f);
        const bool noMask = !(std::isfinite(m.mask[0]) && std::isfinite(m.mask[1]) && std::isfinite(m.mask[2]) && std::isfinite(m.mask[3])) ||
                            m.mask[0] <= -1000.0f || m.mask[1] <= -1000.0f || m.mask[2] <= -1000.0f || m.mask[3] <= -1000.0f;
        if (noMask) { m.mask[0] = m.mask[1] = m.mask[2] = 0.0f; m.mask[3] = 1.0f; }
        for (float& v : m.mask) v = std::clamp(v, -20.0f, 20.0f);
        return m;
    }

    void SetMaterials(const std::vector<Material>& rows)
    {
        char line[96];
        std::snprintf(line, sizeof(line), "CrystalMaterials: %u crystal textures (set directly)", static_cast<unsigned>(rows.size()));
        Rebuild(rows, line);
    }

    void Load(const WXL_SeyrisCdbcApi* cdbc)
    {
        if (!cdbc || !cdbc->HasFeature("cdbc-load")) { Rebuild({}, "wxl-seyris-tools (cdbc) not available (no crystal look)"); return; }
        char err[256] = {};
        void* t = cdbcload::LoadAppendOnly(cdbc, kDef, err, sizeof(err));
        if (!t) { Rebuild({}, std::string("CrystalMaterials.cdbc: ") + (err[0] ? err : "not found") + " (no crystal look)"); return; }
        auto F = [&](const void* row, const char* f, float missing) { return cdbcload::Float(cdbc, t, row, f, missing); };
        std::vector<Material> rows;
        for (uint32_t i = 0, n = cdbc->RowCount(t); i < n; ++i)
        {
            const void* row = cdbc->RowAt(t, i);
            if (!row) continue;
            const char* texture = cdbc->GetString(t, row, "Texture");
            Material m;
            m.id = cdbc->Value(t, row, "ID", 0);
            m.texture = texture ? texture : "";
            if (cdbcload::Has(cdbc, t, "Model"))
                if (const char* model = cdbc->GetString(t, row, "Model")) m.model = model;
            m.family = cdbc->Value(t, row, "Family", 0);
            m.flags = cdbcload::Has(cdbc, t, "Flags") ? cdbc->Value(t, row, "Flags", 0) : 0;
            m.weight = F(row, "Weight", -1.0f);
            const char* tint[3] = { "TintR", "TintG", "TintB" };
            const char* glow[3] = { "GlowR", "GlowG", "GlowB" };
            const char* mask[4] = { "MaskR", "MaskG", "MaskB", "MaskBias" };
            for (int k = 0; k < 3; ++k) { m.tint[k] = F(row, tint[k], -1.0f); m.glow[k] = F(row, glow[k], -1.0f); }
            for (int k = 0; k < 4; ++k) m.mask[k] = F(row, mask[k], -1000.0f);
            m.dayGlow = F(row, "Glow", -1.0f);
            m.nightGlow = F(row, "NightGlow", -1.0f);
            m.deepen = F(row, "Deepen", -1.0f);
            m.rim = F(row, "Rim", -1.0f);
            m.facetYards = F(row, "FacetSize", -1.0f);
            rows.push_back(m);
        }
        cdbc->Release(t);
        size_t kept = 0;
        for (const Material& m : rows) kept += (!m.texture.empty() && m.family != kNoFamily) ? 1 : 0;
        char line[128];
        std::snprintf(line, sizeof(line), "CrystalMaterials.cdbc: %u crystal textures", static_cast<unsigned>(kept));
        Rebuild(rows, line);
    }

    uint32_t MaterialOf(const char* texturePath)
    {
        if (!texturePath || !texturePath[0] || g_byTexture.empty()) return 0;
        const auto it = g_byTexture.find(Key(texturePath));
        return it == g_byTexture.end() ? 0 : it->second;
    }

    const Material* MaterialAt(uint32_t indexPlusOne)
    {
        return indexPlusOne >= 1 && indexPlusOne <= g_materials.size() ? &g_materials[indexPlusOne - 1] : nullptr;
    }

    uint16_t Classify(uint32_t blend, uint32_t materialFlags, const char* texturePath, const char* modelPath)
    {
        if (!BlendTakesLook(blend)) return 0;
        const uint32_t row = MaterialOf(texturePath);
        if (!row || row >= kUnlitBit) return 0;
        if (const std::string& only = g_materials[row - 1].model; !only.empty())
        {
            std::string model(modelPath ? modelPath : "");
            for (char& c : model) c = c == '/' ? '\\' : static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            if (model.find(only) == std::string::npos) return 0;
        }
        return static_cast<uint16_t>(row | ((materialFlags & 0x1) ? kUnlitBit : 0));
    }

    // --- the M2 batch about to draw ---------------------------------------------------------------------------------
    // The draw context (core offsets/engine/Gx.hpp, and the draw's own entry, 0x8203B0 / 0x820AE0): +0x48 the model's
    // file header (its arrays rebased to pointers at load), +0x50 the batch's element, whose +0x28 is its batch record
    // (the skin's M2Batch: +0x0A its material, +0x10 its texture combo). The header: +0x50/+0x54 textures (16 bytes:
    // type, flags, filename count and pointer), +0x70/+0x74 materials (flags u16, blend u16), +0x80/+0x84 the texture
    // combos (i16). The draw reads [header+0x74][material] for the blend the same way.
    namespace
    {
        constexpr size_t kCtxHeader = 0x48, kCtxElement = 0x50, kElementBatch = 0x28;
        constexpr size_t kBatchMaterial = 0x0A, kBatchTextureCombo = 0x10;
        constexpr size_t kHeaderTextures = 0x50, kHeaderMaterials = 0x70, kHeaderTextureCombos = 0x80;
        constexpr uint32_t kMaxCount = 1u << 16;

        struct RawBatch { uint32_t blend = 0, flags = 0; char name[260] = {}; };

        bool ReadBatch(const void* ctx, RawBatch& out)
        {
            __try
            {
                const uint8_t* c = static_cast<const uint8_t*>(ctx);
                const uint8_t* header = *reinterpret_cast<const uint8_t* const*>(c + kCtxHeader);
                const uint8_t* element = *reinterpret_cast<const uint8_t* const*>(c + kCtxElement);
                if (!header || !element) return false;
                const uint8_t* batch = *reinterpret_cast<const uint8_t* const*>(element + kElementBatch);
                if (!batch) return false;
                const uint32_t material = *reinterpret_cast<const uint16_t*>(batch + kBatchMaterial);
                const uint32_t combo = *reinterpret_cast<const uint16_t*>(batch + kBatchTextureCombo);
                const uint32_t materials = *reinterpret_cast<const uint32_t*>(header + kHeaderMaterials);
                const uint8_t* materialArray = *reinterpret_cast<const uint8_t* const*>(header + kHeaderMaterials + 4);
                if (!materialArray || materials > kMaxCount || material >= materials) return false;
                out.flags = *reinterpret_cast<const uint16_t*>(materialArray + 4 * material);
                out.blend = *reinterpret_cast<const uint16_t*>(materialArray + 4 * material + 2);
                const uint32_t combos = *reinterpret_cast<const uint32_t*>(header + kHeaderTextureCombos);
                const int16_t* comboArray = *reinterpret_cast<const int16_t* const*>(header + kHeaderTextureCombos + 4);
                if (!comboArray || combos > kMaxCount || combo >= combos) return false;
                const int texture = comboArray[combo];
                const uint32_t textures = *reinterpret_cast<const uint32_t*>(header + kHeaderTextures);
                const uint8_t* textureArray = *reinterpret_cast<const uint8_t* const*>(header + kHeaderTextures + 4);
                if (!textureArray || textures > kMaxCount || texture < 0 || static_cast<uint32_t>(texture) >= textures) return false;
                const uint8_t* record = textureArray + 16 * texture;
                const uint32_t length = *reinterpret_cast<const uint32_t*>(record + 8);
                const char* name = *reinterpret_cast<const char* const*>(record + 12);
                if (!name || length < 2) return false;   // a replaceable texture: no file name
                const uint32_t limit = std::min<uint32_t>(length, sizeof(out.name) - 1);
                uint32_t n = 0;
                for (; n < limit && name[n]; ++n) out.name[n] = name[n];
                out.name[n] = 0;
                return n > 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
    }

    const void* M2BatchKey(const void* ctx)
    {
        if (!ctx) return nullptr;
        __try
        {
            const uint8_t* element = *reinterpret_cast<const uint8_t* const*>(static_cast<const uint8_t*>(ctx) + kCtxElement);
            return element ? *reinterpret_cast<const void* const*>(element + kElementBatch) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    uint16_t ClassifyM2Batch(const void* ctx, const char* modelPath)
    {
        if (!ctx || g_materials.empty()) return 0;
        RawBatch raw;
        if (!ReadBatch(ctx, raw)) return 0;
        return Classify(raw.blend, raw.flags, raw.name, modelPath);
    }

    // --- the size of a batch's crystals -----------------------------------------------------------------------------
    // M2 (the draw context, as ReadBatch; core offsets/game/M2.hpp): +0x60 the instance, whose +0x2C is the runtime model,
    // whose +0x170 is its live skin profile (parsed in place, its arrays' file offsets rewritten to pointers): +0x04 the
    // vertex lookup (u16 model vertices), +0x0C the triangles' indices (u16, into the lookup), +0x1C the submeshes (0x30
    // each: +0x02 the index start's high word, +0x08 the index start, +0x0A the index count). The batch record's +0x04 is
    // its submesh. The file header's +0x3C: the model's vertices (0x30 each, the position first).
    // WMO (offsets/game/WMO.hpp): the group object's +0xE0 MOVI (u16 indices, their count at +0x154) and +0xE8 MOVT (three
    // floats a vertex, their count at +0x15C); a MOBA record's +0x0C its first index (u32), +0x10 its index count (u16).
    namespace
    {
        constexpr size_t kCtxInstance = 0x60, kInstanceModel = 0x2C, kModelSkin = 0x170;
        constexpr size_t kSkinLookup = 0x04, kSkinIndices = 0x0C, kSkinSubmeshes = 0x1C, kSubmeshStride = 0x30;
        constexpr size_t kSubmeshLevel = 0x02, kSubmeshIndexStart = 0x08, kSubmeshIndexCount = 0x0A, kBatchSubmesh = 0x04;
        constexpr size_t kHeaderVertices = 0x3C, kVertexStride = 0x30;
        constexpr size_t kGroupMovi = 0xE0, kGroupMoviCount = 0x154, kGroupMovt = 0xE8, kGroupMovtCount = 0x15C;
        constexpr size_t kMobaStart = 0x0C, kMobaCount = 0x10;
        constexpr uint32_t kMaxCorners = 3u << 18;   // 262,144 triangles: past that, a batch is left at the row's facets

        // A batch's triangles as the client holds them: corner i's vertex is lookup[indices[start + i]] (or
        // indices[start + i] without a lookup), its position the first three floats at positions + vertex x stride.
        struct Soup
        {
            const uint8_t* positions = nullptr; uint32_t positionCount = 0, positionStride = 0;
            const uint16_t* lookup = nullptr; uint32_t lookupCount = 0;
            const uint16_t* indices = nullptr; uint32_t indexCount = 0;
            uint32_t start = 0, count = 0;
        };

        bool SoupFits(const Soup& s)
        {
            return s.positions && s.indices && s.count >= 3 && s.count <= kMaxCorners && s.start <= s.indexCount &&
                   s.count <= s.indexCount - s.start && s.positionCount > 0 && s.positionCount <= (1u << 20);
        }

        bool ReadM2Soup(const void* ctx, Soup& s)
        {
            __try
            {
                const uint8_t* c = static_cast<const uint8_t*>(ctx);
                const uint8_t* header = *reinterpret_cast<const uint8_t* const*>(c + kCtxHeader);
                const uint8_t* element = *reinterpret_cast<const uint8_t* const*>(c + kCtxElement);
                const uint8_t* instance = *reinterpret_cast<const uint8_t* const*>(c + kCtxInstance);
                if (!header || !element || !instance) return false;
                const uint8_t* batch = *reinterpret_cast<const uint8_t* const*>(element + kElementBatch);
                const uint8_t* model = *reinterpret_cast<const uint8_t* const*>(instance + kInstanceModel);
                if (!batch || !model) return false;
                const uint8_t* skin = *reinterpret_cast<const uint8_t* const*>(model + kModelSkin);
                if (!skin) return false;
                const uint32_t submesh = *reinterpret_cast<const uint16_t*>(batch + kBatchSubmesh);
                const uint32_t submeshes = *reinterpret_cast<const uint32_t*>(skin + kSkinSubmeshes);
                const uint8_t* submeshArray = *reinterpret_cast<const uint8_t* const*>(skin + kSkinSubmeshes + 4);
                if (!submeshArray || submeshes > kMaxCount || submesh >= submeshes) return false;
                const uint8_t* sm = submeshArray + kSubmeshStride * submesh;
                s.start = *reinterpret_cast<const uint16_t*>(sm + kSubmeshIndexStart) +
                          (static_cast<uint32_t>(*reinterpret_cast<const uint16_t*>(sm + kSubmeshLevel)) << 16);
                s.count = *reinterpret_cast<const uint16_t*>(sm + kSubmeshIndexCount);
                s.lookupCount = *reinterpret_cast<const uint32_t*>(skin + kSkinLookup);
                s.lookup = *reinterpret_cast<const uint16_t* const*>(skin + kSkinLookup + 4);
                s.indexCount = *reinterpret_cast<const uint32_t*>(skin + kSkinIndices);
                s.indices = *reinterpret_cast<const uint16_t* const*>(skin + kSkinIndices + 4);
                s.positionCount = *reinterpret_cast<const uint32_t*>(header + kHeaderVertices);
                s.positions = *reinterpret_cast<const uint8_t* const*>(header + kHeaderVertices + 4);
                s.positionStride = kVertexStride;
                return s.lookup && s.lookupCount > 0 && s.lookupCount <= kMaxCount && SoupFits(s);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool ReadWmoSoup(const void* group, const void* record, Soup& s)
        {
            __try
            {
                const uint8_t* g = static_cast<const uint8_t*>(group);
                const uint8_t* r = static_cast<const uint8_t*>(record);
                s.indices = *reinterpret_cast<const uint16_t* const*>(g + kGroupMovi);
                s.indexCount = *reinterpret_cast<const uint32_t*>(g + kGroupMoviCount);
                s.positions = *reinterpret_cast<const uint8_t* const*>(g + kGroupMovt);
                s.positionCount = *reinterpret_cast<const uint32_t*>(g + kGroupMovtCount);
                s.positionStride = 12;
                s.lookup = nullptr;
                s.lookupCount = 0;
                s.start = *reinterpret_cast<const uint32_t*>(r + kMobaStart);
                s.count = *reinterpret_cast<const uint16_t*>(r + kMobaCount);
                return SoupFits(s);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // The corners' positions, three floats each, into out (3 x s.count); false on an index out of range or a fault.
        bool CopyCorners(const Soup& s, float* out)
        {
            __try
            {
                for (uint32_t i = 0; i < s.count; ++i)
                {
                    uint32_t v = s.indices[s.start + i];
                    if (s.lookup)
                    {
                        if (v >= s.lookupCount) return false;
                        v = s.lookup[v];
                    }
                    if (v >= s.positionCount) return false;
                    const float* p = reinterpret_cast<const float*>(s.positions + static_cast<size_t>(v) * s.positionStride);
                    out[3 * i] = p[0]; out[3 * i + 1] = p[1]; out[3 * i + 2] = p[2];
                }
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        float SoupRadius(const Soup& s)
        {
            std::vector<float> corners(3 * static_cast<size_t>(s.count));
            return CopyCorners(s, corners.data()) ? PieceRadius(corners.data(), s.count) : 0.0f;
        }

        struct Cell { int32_t x, y, z; bool operator==(const Cell& o) const { return x == o.x && y == o.y && z == o.z; } };
        struct CellHash
        {
            size_t operator()(const Cell& c) const
            {
                return static_cast<size_t>(Hash(static_cast<uint32_t>(c.x) * 73856093u ^ Hash(static_cast<uint32_t>(c.y) * 19349663u ^ Hash(static_cast<uint32_t>(c.z)))));
            }
        };
    }

    float PieceRadius(const float* corners, size_t cornerCount)
    {
        if (!corners || cornerCount < 3) return 0.0f;
        cornerCount = std::min<size_t>(cornerCount - cornerCount % 3, kMaxCorners);
        // Corners at the same place (to 1/256 yd) are one vertex; a triangle with a corner not finite or out of the world
        // is left out.
        std::unordered_map<Cell, uint32_t, CellHash> ids;
        ids.reserve(cornerCount);
        std::vector<uint32_t> corner(cornerCount, UINT32_MAX), parent;
        std::vector<float> where;
        for (size_t t = 0; t < cornerCount; t += 3)
        {
            bool ok = true;
            for (size_t k = 0; k < 9 && ok; ++k) ok = std::isfinite(corners[3 * t + k]) && std::fabs(corners[3 * t + k]) < 1.0e5f;
            if (!ok) continue;
            for (size_t i = t; i < t + 3; ++i)
            {
                const float* p = corners + 3 * i;
                const Cell key{ static_cast<int32_t>(std::lround(p[0] * 256.0f)), static_cast<int32_t>(std::lround(p[1] * 256.0f)),
                                static_cast<int32_t>(std::lround(p[2] * 256.0f)) };
                const auto [it, added] = ids.emplace(key, static_cast<uint32_t>(parent.size()));
                if (added) { parent.push_back(it->second); where.insert(where.end(), p, p + 3); }
                corner[i] = it->second;
            }
        }
        if (parent.empty()) return 0.0f;
        auto find = [&parent](uint32_t x) {
            while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
            return x;
        };
        for (size_t t = 0; t < cornerCount; t += 3)
        {
            if (corner[t] == UINT32_MAX) continue;
            const uint32_t a = find(corner[t]), b = find(corner[t + 1]), c = find(corner[t + 2]);
            parent[b] = a;
            parent[find(c)] = a;
        }
        // Each piece's box and vertex count.
        struct Box { float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f }; uint32_t vertices = 0; };
        std::vector<Box> boxes(parent.size());
        for (uint32_t v = 0; v < parent.size(); ++v)
        {
            Box& b = boxes[find(v)];
            for (int k = 0; k < 3; ++k) { b.lo[k] = std::min(b.lo[k], where[3 * v + k]); b.hi[k] = std::max(b.hi[k], where[3 * v + k]); }
            ++b.vertices;
        }
        std::vector<std::pair<float, uint32_t>> pieces;
        size_t total = 0;
        for (const Box& b : boxes)
            if (b.vertices)
            {
                const float dx = b.hi[0] - b.lo[0], dy = b.hi[1] - b.lo[1], dz = b.hi[2] - b.lo[2];
                pieces.emplace_back(0.5f * std::sqrt(dx * dx + dy * dy + dz * dz), b.vertices);
                total += b.vertices;
            }
        std::sort(pieces.begin(), pieces.end());
        size_t acc = 0;
        for (const auto& [radius, vertices] : pieces)
        {
            acc += vertices;
            if (2 * acc >= total) return radius;
        }
        return pieces.back().first;
    }

    float M2BatchPieceRadius(const void* ctx)
    {
        Soup s;
        return ctx && ReadM2Soup(ctx, s) ? SoupRadius(s) : 0.0f;
    }

    float WmoBatchPieceRadius(const void* group, const void* record)
    {
        Soup s;
        return group && record && ReadWmoSoup(group, record, s) ? SoupRadius(s) : 0.0f;
    }

    float CellScale(float size)
    {
        if (!std::isfinite(size) || size <= 0.0f) return 1.0f;
        // 1 at 0, about (size / start)^growth well past the start; then levelled off smoothly at the most, the growth
        // past 1 going through t / (1 + t^4)^(1/4) (t at first, 1 at the most).
        const float x = std::min(size, 1.0e5f) / kCellSizeStart;
        const float grown = std::pow(1.0f + x * x, 0.5f * kCellGrowth);
        const float t = (grown - 1.0f) / (kMaxCellScale - 1.0f);
        return 1.0f + (kMaxCellScale - 1.0f) * t / std::pow(1.0f + t * t * t * t, 0.25f);
    }

    float CellFactor(const Settings& s, float size)
    {
        const float setting = std::isfinite(s.cellScale) ? std::clamp(s.cellScale, kMinCellSetting, kMaxCellSetting) : 1.0f;
        return CellScale(size) * setting;
    }

    void Constants(const Material& m, const Settings& s, float night, bool unlit, float* look, float c179[4], float c190[4], float c[4 * kRegisters],
                   float sizeYards)
    {
        const FamilyLook& f = FamilyOf(m.family);
        const float weight = Clamp01(s.strength) * Clamp01(m.weight);
        // The facets grow with the crystal (and the setting), and the layers' depths and the detail's fade with them: a
        // giant crystal's facets read from as far as a shard's do from near.
        const float grow = CellFactor(s, sizeYards);
        // c179: the weight, no vertical stretch, a touch brighter (the deepening darkens the core), a little sky even
        // face on (a polished face).
        c179[0] = weight; c179[1] = 0.0f; c179[2] = 0.08f; c179[3] = 0.04f;
        // c190: the texture's own colour as the body, the facets' shading on it, the edges and sparkles softer (a crystal's
        // inner planes, not cracks), the detail's fade with the distance (from kDetailFadeStart x grow to kDetailFadeEnd x
        // grow: c189.w is the same for any grow).
        c190[0] = 1.0f; c190[1] = 0.55f; c190[2] = -0.3f; c190[3] = -1.0f / ((kDetailFadeEnd - kDetailFadeStart) * grow);
        look[39] = kDetailFadeEnd / (kDetailFadeEnd - kDetailFadeStart);   // c189.w
        look[19] = 160.0f;                                                 // c184.w glint exponent: a sharp glint off a face
        for (int k = 20; k < 23; ++k) look[k] *= 1.6f;                     // c185.xyz glint colour
        look[23] = 0.5f;                                                   // c185.w Fresnel strength
        look[27] = 1.0f;                                                   // c186.w clarity: the crystal body throughout
        const float rowFacet = std::isfinite(m.facetYards) && m.facetYards > 0.0f ? m.facetYards : 2.0f;
        const float facet = std::clamp(rowFacet * grow, 0.05f, 2000.0f);
        look[28] = 0.25f * facet; look[29] = 0.8f * facet;                 // c187.xy the two layers' depths (yd)
        look[30] = 1.0f / facet; look[31] = 1.0f / (1.7f * facet);         // c187.zw the detail's repeats per yd
        for (int k = 0; k < 3; ++k) look[32 + k] = f.highlight[k];         // c188.xyz the edges' and sparkles' colour
        look[35] = 1.0f;                                                   // c188.w sky reflection brightness
        // c78: the mask (kept = 1 - saturate((1 - bias) - dot / (lum + 0.04))); zeros keep everything.
        for (int k = 0; k < 3; ++k) c[k] = m.mask[k];
        c[3] = 1.0f - m.mask[3];
        // c79: the tint the core deepens toward, how far.
        for (int k = 0; k < 3; ++k) c[4 + k] = m.tint[k];
        c[7] = m.deepen;
        // c80: the highlight lit as the surface is (.x 1), the rim (.w).
        c[8] = 1.0f; c[9] = c[10] = 0.0f;
        c[11] = m.rim;
        // c81: the glow (x the weight: it goes on after the weight in the block), stronger at night; the client already
        // draws an unlit crystal at full brightness at night, so half for it. .w: the facets' share.
        const float amount = (m.dayGlow + m.nightGlow * Clamp01(night)) * std::clamp(std::isfinite(s.glow) ? s.glow : 0.0f, 0.0f, 3.0f) *
                             weight * (unlit ? 0.5f : 1.0f);
        for (int k = 0; k < 3; ++k) c[12 + k] = m.glow[k] * amount;
        c[15] = 0.35f;
    }

    void BuildDetail(std::vector<uint32_t>& texels, int size)
    {
        texels.assign(static_cast<size_t>(size) * size, 0);
        std::vector<float> sparkles(static_cast<size_t>(size) * size, 0.0f);
        // Sparkles: small points, a quarter of them with a four-pointed star (a glint off a tiny internal face).
        for (int k = 0; k < 90; ++k)
        {
            const float sx = Rand(k, 1, 211) * size, sy = Rand(k, 2, 211) * size;
            const float bright = 0.5f + 0.5f * Rand(k, 3, 211);
            const float radius = 1.0f + 1.3f * Rand(k, 4, 211);
            const bool star = Rand(k, 5, 211) < 0.3f;
            const int reach = star ? 7 : 3;
            for (int dy = -reach; dy <= reach; ++dy)
                for (int dx = -reach; dx <= reach; ++dx)
                {
                    const int x = Wrap(static_cast<int>(std::floor(sx)) + dx, size), y = Wrap(static_cast<int>(std::floor(sy)) + dy, size);
                    const float px = std::floor(sx) + dx + 0.5f - sx, py = std::floor(sy) + dy + 0.5f - sy;
                    float v = (1.0f - SmoothStep(radius * 0.4f, radius, std::sqrt(px * px + py * py))) * bright;
                    if (star)
                    {
                        const float arm = std::max((1.0f - std::fabs(py) / 0.7f) * (1.0f - std::fabs(px) / reach),
                                                   (1.0f - std::fabs(px) / 0.7f) * (1.0f - std::fabs(py) / reach));
                        v = std::max(v, std::max(arm, 0.0f) * bright * 0.8f);
                    }
                    float& s = sparkles[static_cast<size_t>(y) * size + x];
                    s = std::max(s, v);
                }
        }
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const float u = (x + 0.5f) / size, w = (y + 0.5f) / size;
                // Facets at two scales: big ones (5 across), small ones (11 across) inside them; straight edges.
                const Cells coarse = Voronoi(u, w, 5, 41), fine = Voronoi(u, w, 11, 43);
                const float shade = 0.65f * Facet(coarse, 41) + 0.35f * Facet(fine, 43);
                const float edge = std::min(1.0f, Edge(coarse, 0.03f, 41) + 0.35f * Edge(fine, 0.05f, 43));
                const float sparkle = sparkles[static_cast<size_t>(y) * size + x];
                const auto b8 = [](float f) { return static_cast<uint32_t>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
                texels[static_cast<size_t>(y) * size + x] = (b8(edge) << 16) | (b8(sparkle) << 8) | b8(shade);
            }
    }

    IDirect3DTexture9* DetailTexture(IDirect3DDevice9* dev) { return dev && EnsureDetail(dev) ? g_detail : nullptr; }

    bool Active() { return g_settings.enabled && g_settings.strength > 0.0f && !g_materials.empty(); }

    void Update()
    {
        g_night = Clamp01(windows::Night());
        g_lastM2 = g_m2Batches; g_lastWmo = g_wmoBatches;
        g_m2Batches = g_wmoBatches = 0;
    }

    void NoteBatch(bool wmo) { ++(wmo ? g_wmoBatches : g_m2Batches); }

    const char* StatusLine()
    {
        char line[320];
        std::snprintf(line, sizeof(line), "crystals: %s; %s; last frame %u model and %u building batches with the crystal look; night %.2f; facets x%.2f",
                      g_settings.enabled ? "on" : "off", g_tableStatus.c_str(), g_lastM2, g_lastWmo, g_night, CellFactor(g_settings, 0.0f));
        g_status = line;
        return g_status.c_str();
    }

    void DrawPanelSection(const WXL_Api* api)
    {
        if (!api) return;
        api->UiText(StatusLine());
        int on = g_settings.enabled ? 1 : 0;
        if (api->UiCheckbox("crystals look like deep crystal (draenei, fel, Crystalsong, mana and cave crystals...)", &on)) g_settings.enabled = on;
        api->UiSliderFloat("crystal strength", &g_settings.strength, 0.0f, 1.0f);
        api->UiSliderFloat("crystal inner glow (x, stronger at night)", &g_settings.glow, 0.0f, 3.0f);
        api->UiSliderFloat("crystal facets' size (x; they already grow with the crystal)", &g_settings.cellScale, kMinCellSetting, kMaxCellSetting);
    }
}
