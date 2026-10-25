#include "WetSurfaces.hpp"
#include "../debug/MemoryTag.hpp"
#include "../debug/UiText.hpp"

#include "GroundMaterialTable.hpp"
#include "ModelIce.hpp"
#include "SurfaceCoverTable.hpp"
#include "TerrainIce.hpp"
#include "TerrainWetness.hpp"
#include "../env/Climate.hpp"
#include "../env/Fields.hpp"
#include "../env/Regional.hpp"
#include "../env/Storm.hpp"
#include "../env/Wind.hpp"
#include "../render/MoistureTexture.hpp"

#include "game/Camera.hpp"
#include "game/Gx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace wxl_livingazeroth::wetsurfaces
{
    namespace
    {
        constexpr DWORD kSampler = 15;
        constexpr size_t kMaxZones = 64;

        wetsurf::Settings g_settings;

        struct ZoneRecord { int map = -1; uint32_t zone = 0; wetsurf::ZoneWet wet; };
        std::vector<ZoneRecord> g_zones;
        int      g_map = -1;
        uint32_t g_playerZone = 0;
        float    g_rain = 0.0f, g_dryRate = 1.0f;
        bool     g_raining = false;

        // Shaders and the frame.
        unsigned    g_variants = 0, g_variantsWithout = 0;
        std::string g_withoutReasons;
        bool        g_stageOn = false, g_bound = false;
        unsigned    g_chunks = 0, g_shineChunks = 0, g_lastChunks = 0, g_lastShineChunks = 0, g_stages = 0;
        unsigned    g_buildingBatches = 0, g_lastBuildingBatches = 0;
        float       g_lastConstants[4 * wetsurf::kTerrainRegisters] = {};
        std::string g_status;

        IDirect3DTexture9* g_detail = nullptr;
        IDirect3DDevice9*  g_detailDevice = nullptr;

        ZoneRecord* Find(int map, uint32_t zone)
        {
            for (ZoneRecord& z : g_zones) if (z.map == map && z.zone == zone) return &z;
            return nullptr;
        }

        const wetsurf::ZoneWet& PlayerWet()
        {
            static const wetsurf::ZoneWet kDry;
            const ZoneRecord* z = Find(g_map, g_playerZone);
            return z ? z->wet : kDry;
        }

        bool AnyWet()
        {
            for (const ZoneRecord& z : g_zones) if (z.wet.film > 0.0005f || z.wet.pool > 0.0005f) return true;
            return false;
        }

        // This frame's state; the camera only for the terrain (a client read: never in the buildings' path, which tests run).
        wetsurf::Frame FrameNow(bool camera)
        {
            wetsurf::Frame f;
            const wetsurf::ZoneWet& w = PlayerWet();
            f.film = w.film;
            f.pool = w.pool;
            if (camera) wxl::game::camera::GetPosition(f.camera);
            terrainice::Sun(f.sun, f.sunColour);
            f.daylight = climate::SunNow().daylight;
            const storm::Status& st = storm::GetStatus();
            f.overcast = std::max(st.storminess, g_raining ? 0.6f + 0.4f * g_rain : 0.0f);
            return f;
        }
    }

    // --- the terrain shaders -------------------------------------------------------------------------------------------
    TerrainPieces TerrainShader(const std::string& r)
    {
        // ps_3_0 reads at most one constant register per instruction; lrp's destination can't be its first or third source.
        TerrainPieces p;
        // The surface texel (red: the film x kSurfaceShare, green: the puddle potential); beyond the near grid the film
        // goes to c161.w and the potential to 0 (r31.x: the grid's edge fade); the puddle where the potential is over the
        // threshold (c162.x = -it, c162.y the sharpness); both x the layers' shine (r25.z); a puddle is fully wet (r1.x);
        // the darkening joins the soil's (r30.x, before the ice's scale).
        p.texel =
            "\n    texld r1, r29, s15"
            "\n    add r1.x, r1.x, -c161.w"
            "\n    mad r1.x, r1.x, r31.x, c161.w"
            "\n    mad r1.y, r1.y, r31.x, c162.x"
            "\n    mul_sat r1.y, r1.y, c162.y"
            "\n    mul r1.xy, r1, r25.z"
            "\n    max r1.x, r1.x, r1.y"
            "\n    mad r30.x, r1.x, c162.z, r30.x";
        // V toward the camera (c161), N.V (r2.w), R = 2 (N.V) N - V (r2.xyz, c208.x = 2: the lights' defs); the lobe
        // (r3.xy: the wet one toward the puddle's by the puddle), the gain (r3.z: c167 by the puddle, x the wetness); the
        // sun's glint along R starts the highlights' sum (r4, c166 its colour), dimmed with the surface's own light as the
        // ice's glint is (min(2 x its luminance, 1), c191 the ice's luminance weights): none in a shadow.
        p.beforeLoop =
            "\n    add r2.xyz, c161, -v9"
            "\n    nrm r3.xyz, r2"
            "\n    dp3_sat r2.w, r24, r3"
            "\n    mul r2.xyz, r24, r2.w"
            "\n    mad r2.xyz, r2, c208.x, -r3"
            "\n    lrp r3.xy, r1.y, c163.zw, c163.xy"
            "\n    mad r3.z, r1.y, c167.x, c167.y"
            "\n    mul r3.z, r3.z, r1.x"
            "\n    dp3_sat r5.x, r2, c164"
            "\n    mad_sat r5.x, r5.x, r3.x, r3.y"
            "\n    mul r5.x, r5.x, r5.x"
            "\n    dp3 r5.w, " + r + ", c191"
            "\n    mul_sat r5.w, r5.w, c208.x"
            "\n    mul r5.x, r5.x, r5.w"
            "\n    mul r4.xyz, r5.x, c166";
        // Each light (toward it r20, colour r19, its light here r16.x: falloff, facing, cone, occlusion, indoors).
        p.inLoop =
            "\n      dp3 r5.x, r20, r2"
            "\n      mad_sat r5.x, r5.x, r3.x, r3.y"
            "\n      mul r5.x, r5.x, r5.x"
            "\n      mul r5.x, r5.x, r16.x"
            "\n      mad r4.xyz, r19, r5.x, r4";
        // The sky in the water: Schlick's (1 - N.V)^4 between F0 and 1 (c165, x intensity), x the wetness squared (a
        // puddle all of it, wet ground's thin film kSurfaceShare^2 of it: rougher, so less of a mirror), toward the fog
        // colour (c2); then the highlights x their gain.
        p.after =
            "\n    add r5.y, c202.x, -r2.w"
            "\n    mul r5.z, r5.y, r5.y"
            "\n    mul r5.z, r5.z, r5.z"
            "\n    mad r5.z, r5.z, c165.x, c165.y"
            "\n    mul r5.z, r5.z, r1.x"
            "\n    mul r5.z, r5.z, r1.x"
            "\n    lrp r6.xyz, r5.z, c2, " + r +
            "\n    mad " + r + ".xyz, r4, r3.z, r6";
        p.declarations = "    dcl_2d s15\n";
        return p;
    }

    void NoteVariant(bool withWet, const std::string& whyNot)
    {
        if (withWet) { ++g_variants; return; }
        ++g_variantsWithout;
        if (g_withoutReasons.find(whyNot) == std::string::npos) g_withoutReasons += (g_withoutReasons.empty() ? "" : "; ") + whyNot;
    }

    unsigned PatchedVariants() { return g_variants; }

    namespace { bool g_pieces = true; }
    bool PiecesOn() { return g_pieces; }
    void SetPiecesForTests(bool on) { g_pieces = on; }

    // --- the frame -----------------------------------------------------------------------------------------------------
    wetsurf::Settings& Config() { return g_settings; }

    float ZoneFilm(int map, uint32_t zone)
    {
        if (!g_settings.enabled) return 0.0f;
        const ZoneRecord* z = Find(map, zone);
        return z ? z->wet.film : 0.0f;
    }

    void Update(float dt, const world::Snapshot& snap)
    {
        memuse::Scope memory(memuse::kEnv);
        static bool provided = false;
        if (!provided) { fields::SetZoneFilm(&ZoneFilm); provided = true; }
        if (!(dt >= 0.0f) || dt > 5.0f) dt = 0.0f;
        if (!snap.inWorld) return;

        g_map = snap.mapId;
        g_playerZone = regional::PlayerZone();
        const storm::Status& st = storm::GetStatus();
        g_rain = std::isfinite(st.rain) ? std::clamp(st.rain, 0.0f, 1.0f) : 0.0f;
        g_raining = g_rain > 0.0f;
        // The climate's drying here against a mild day's (15 degC, half humid, a light wind).
        {
            const climate::Row row = climate::Here();
            const float rate = regional::DryingRate(climate::Temperature(row).total, row.humidity, wind::SteadyGround());
            g_dryRate = wetsurf::DryRateFactor(rate, regional::DryingRate(15.0f, 0.5f, 0.3f));
        }

        // The player's zone: a new one starts dry, or part wet if it's raining now (it rained before we came).
        if (!Find(g_map, g_playerZone))
        {
            if (g_zones.size() >= kMaxZones)
            {
                // The driest record goes.
                auto driest = std::min_element(g_zones.begin(), g_zones.end(), [](const ZoneRecord& a, const ZoneRecord& b)
                                               { return a.wet.film + a.wet.pool < b.wet.film + b.wet.pool; });
                g_zones.erase(driest);
            }
            ZoneRecord z;
            z.map = g_map; z.zone = g_playerZone;
            if (g_raining) { z.wet.film = 0.6f; z.wet.pool = 0.3f; }
            g_zones.push_back(z);
        }
        for (ZoneRecord& z : g_zones)
        {
            const bool here = z.map == g_map && z.zone == g_playerZone;
            wetsurf::Step(z.wet, dt, here && g_raining, g_rain, g_dryRate, g_settings.dryOutMinutes);
        }
        // Dry zones the player isn't in are forgotten.
        g_zones.erase(std::remove_if(g_zones.begin(), g_zones.end(), [](const ZoneRecord& z)
                                     { return !(z.map == g_map && z.zone == g_playerZone) && z.wet.film <= 0.0f && z.wet.pool <= 0.0f; }),
                      g_zones.end());
    }

    void BeforeTerrainStage(IDirect3DDevice9* dev, bool moistureBound)
    {
        g_lastChunks = g_chunks; g_lastShineChunks = g_shineChunks;
        g_chunks = g_shineChunks = 0;
        g_lastBuildingBatches = g_buildingBatches;
        g_buildingBatches = 0;
        g_stageOn = false;
        if (!dev) return;
        const bool wanted = g_settings.enabled && moistureBound && g_variants > 0 && terrainwet::PatchActive() && AnyWet();
        g_bound = wanted && moisturetex::BindSurface(dev, kSampler);
        g_stageOn = g_bound;
        float c[4 * wetsurf::kTerrainRegisters];
        wetsurf::Settings s = g_settings;
        if (!g_stageOn) s.enabled = 0;
        wetsurf::TerrainConstants(s, FrameNow(true), c);
        dev->SetPixelShaderConstantF(wetsurf::kTerrainConstant, c, wetsurf::kTerrainRegisters);
        std::memcpy(g_lastConstants, c, sizeof(c));
        // No layer shines until a chunk says so (features/TerrainIce's chunk draw, while the stage is on).
        const float none[4] = {};
        dev->SetPixelShaderConstantF(wetsurf::kMaskConstant, none, 1);
        if (g_stageOn) ++g_stages;
    }

    bool StageOn() { return g_stageOn; }

    void NoteChunk(bool anyShine)
    {
        ++g_chunks;
        g_shineChunks += anyShine ? 1u : 0u;
    }

    float LayerShine(uint32_t material, const terrain::Surface& s, int map)
    {
        const materials::Values v = materials::Get(material);
        const bool clearIce = material && (v.flags & materials::kFlagClearIce) != 0;
        const bool hot = v.hasTemperature && v.temperature >= 60.0f;
        const bool snow = s.terrainType == 3 || (material && covertable::IsSnow(material, s.area, map));
        return wetsurf::LayerShine(v.wetShineSet, v.wetShine, clearIce, snow, hot, s.terrainType);
    }

    // --- buildings -------------------------------------------------------------------------------------------------------
    bool BuildingsActive()
    {
        return g_settings.enabled && g_settings.buildings && modelice::PatchedVariants() > 0 && PlayerWet().film > 0.01f;
    }

    float BuildingShine(const char* texture, uint32_t blend, uint32_t flags)
    {
        if (blend > 1 || (flags & 0x11)) return 0.0f;   // blended, additive, unlit (MOMT 0x1), lit at night (0x10)
        return wetsurf::WmoTextureShine(texture);
    }

    void BuildingConstants(float shine, float* look, float c179[4], float c190[4], float c77[4])
    {
        const wetsurf::Frame f = FrameNow(false);
        const float I = wetsurf::Intensity(g_settings);
        modelice::StyleConstants(modelice::Style::Wet, wetsurf::BuildingWeight(g_settings, f.film, shine), look, c179, c190);
        // The intensity: darker, more of the sky, a stronger glint; the glint only as much as the sun shows.
        c179[2] *= I;
        look[23] = std::min(1.0f, look[23] * I);                     // c185.w Fresnel strength
        const float sun = wetsurf::SunShare(f) * I;
        for (int k = 20; k < 23; ++k) look[k] *= sun;               // c185.xyz glint colour
        wetsurf::FacingConstant(true, c77);
    }

    IDirect3DTexture9* BuildingDetail(IDirect3DDevice9* dev)
    {
        if (!dev) return nullptr;
        if (g_detail && g_detailDevice == dev) return g_detail;
        if (g_detail) { g_detail->Release(); g_detail = nullptr; }
        g_detailDevice = dev;
        if (FAILED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_detail, nullptr)) || !g_detail) { g_detail = nullptr; return nullptr; }
        D3DLOCKED_RECT lr{};
        if (SUCCEEDED(g_detail->LockRect(0, &lr, nullptr, 0))) { *static_cast<uint32_t*>(lr.pBits) = 0; g_detail->UnlockRect(0); }
        return g_detail;
    }

    void NoteBuildingBatch() { ++g_buildingBatches; }

    // --- testing, the panel, the log -----------------------------------------------------------------------------------
    void SoakNow()
    {
        if (ZoneRecord* z = Find(g_map, g_playerZone)) { z->wet.film = 1.0f; z->wet.pool = 1.0f; }
    }

    void DryNow()
    {
        for (ZoneRecord& z : g_zones) { z.wet.film = 0.0f; z.wet.pool = 0.0f; }
    }

    void SetZoneForTests(int map, uint32_t zone, float film, float pool)
    {
        g_map = map;
        g_playerZone = zone;
        ZoneRecord* z = Find(map, zone);
        if (!z) { g_zones.push_back(ZoneRecord{ map, zone, {} }); z = &g_zones.back(); }
        z->wet.film = film;
        z->wet.pool = pool;
    }

    const char* StatusLine()
    {
        const wetsurf::ZoneWet& w = PlayerWet();
        char line[512];
        std::snprintf(line, sizeof(line),
                      "wet surfaces: %s; your zone %u wet %.2f, puddles %.2f (%s %.2f, drying x%.2f); terrain variants with them %u (without %u%s%s); "
                      "last frame %u of %u chunks shining, %u building batches; surface texture uploads %u",
                      !g_settings.enabled ? "off" : (g_stageOn ? "on" : (AnyWet() ? "on, not drawn" : "on, dry")), g_playerZone, w.film, w.pool,
                      g_raining ? "raining" : "dry weather", g_rain, g_dryRate, g_variants, g_variantsWithout,
                      g_withoutReasons.empty() ? "" : ": ", g_withoutReasons.c_str(), g_lastShineChunks, g_lastChunks, g_lastBuildingBatches,
                      moisturetex::SurfaceUploads());
        g_status = line;
        return g_status.c_str();
    }

    void StatusLines(std::vector<std::string>& out)
    {
        char line[512];
        out.push_back(StatusLine());
        const wetsurf::Settings& s = g_settings;
        std::snprintf(line, sizeof(line), "settings: Enabled %d, Intensity %.2f, Puddles %.2f, DryOutMinutes %.1f, Buildings %d", s.enabled, s.intensity,
                      s.puddles, s.dryOutMinutes, s.buildings);
        out.push_back(line);
        for (const ZoneRecord& z : g_zones)
        {
            std::snprintf(line, sizeof(line), "%s map %d zone %u: wet %.3f, puddles %.3f", z.map == g_map && z.zone == g_playerZone ? ">" : " ",
                          z.map, z.zone, z.wet.film, z.wet.pool);
            out.push_back(line);
        }
        const fields::Stats st = fields::GetStats();
        std::snprintf(line, sizeof(line), "moisture grid: %u cells holding water on their surface, %u that can flood, lowness known in %u (%.3f ms a frame)",
                      st.wetCells, st.puddleCells, st.lownessKnown, st.lownessMs);
        out.push_back(line);
        const world::Snapshot& snap = world::Current();
        if (snap.inWorld)
        {
            const fields::MoistureDetail m = fields::Moisture(snap.playerPos);
            if (m.known)
            {
                std::snprintf(line, sizeof(line), "here: surface water %.2f (open sky %.2f), %s %.2f yd lower than round it, puddle potential %.2f -> %s",
                              m.surfaceFilm, m.open, m.lownessKnown ? "lowness" : "lowness not known yet:", m.lowness, m.puddlePotential,
                              wetsurf::PuddleAt(m.puddlePotential * (m.open > 0.0f ? 1.0f : 0.0f), PlayerWet().pool, s.puddles) > 0.0f ? "a puddle" : "no puddle");
                out.push_back(line);
            }
        }
        const float* c = g_lastConstants;
        std::snprintf(line, sizeof(line), "constants: far wet %.2f, puddle offset %.2f, darkening %.2f, sun glint %.2f %.2f %.2f, Fresnel %.2f + %.2f, gains %.2f + %.2f",
                      c[3], c[4], c[6], c[20], c[21], c[22], c[16], c[17], c[25], c[24]);
        out.push_back(line);
    }

    void DrawPanelSection(const WXL_Api* api)
    {
        std::vector<std::string> lines;
        StatusLines(lines);
        for (const std::string& l : lines) ui::Text(api, l.c_str());
        wetsurf::Settings& s = g_settings;
        int on = s.enabled ? 1 : 0;
        if (api->UiCheckbox("wet surfaces in rain (stone and soil shine, puddles; grass doesn't)", &on)) s.enabled = on;
        api->UiSliderFloat("wet surfaces: intensity", &s.intensity, 0.0f, 2.0f);
        api->UiSliderFloat("wet surfaces: puddles", &s.puddles, 0.0f, 2.0f);
        api->UiSliderFloat("wet surfaces: dry-out minutes", &s.dryOutMinutes, 0.5f, 60.0f);
        int buildings = s.buildings ? 1 : 0;
        if (api->UiCheckbox("wet surfaces on buildings' stone, wood and roofs", &buildings)) s.buildings = buildings;
        if (api->UiButton("Soak your zone now (test)")) SoakNow();
        api->UiSameLine();
        if (api->UiButton("Dry every zone now (test)")) DryNow();
    }

    void GpuMemory(memuse::Gpu& out)
    {
        out.Add(g_detail);
    }
}
