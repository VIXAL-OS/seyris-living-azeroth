#include "SurfaceCover.hpp"
#include "SurfaceCoverInternal.hpp"

#include "../debug/Pages.hpp"
#include "../env/Fields.hpp"
#include "GrassPerf.hpp"

#include <cmath>
#include <cstring>

namespace wxl_livingazeroth::cover::detail
{
    const WXL_Api* g_api = nullptr;
    Settings       g_settings;


    Level g_levels[kMaxLevels];

    unsigned             g_frame = 0;
    int                  g_snowCursor = 0;                    // the level the next snow re-fold starts looking at

    // What the table sees where the player is (for the panel, so rows can be authored on the spot).
    terrain::Surface   g_hereSurface;
    covertable::Values g_hereValues;
    std::string        g_hereTexture;
    bool               g_hereValid = false;
    float              g_hereMccv[3] = {}, g_hereLiquid = 0.0f, g_hereLiquidDepth = 0.0f;
    bool               g_hereMccvValid = false, g_hereLiquidValid = false;

    const void* g_cdbcApi = nullptr;
    uint32_t    g_tableGeneration = 0;

    int   g_lastCoverage = -1, g_lastMaterialSource = -1, g_lastAlphaSwap = -1;
    int   g_lastMap = -1;
    float g_lastDepthMul = -1.0f, g_lastDriftMul = -1.0f, g_lastBreakupMul = -1.0f;

    // Under the player, for the panel: the layer weights both ways round and the client's own
    // dominant layer (the check that the alpha maps are read the right way round).
    terrain::LayerWeights g_hereLayers, g_hereLayersSwapped;
    std::string g_hereLayerTexture[4]; // own copies for the panel (the cache can drop the originals)
    bool g_hereLayersValid = false;
    bool g_wasInWorld = false;
    int g_mapNow = -1;

    double g_simMs = 0;
    bool g_resting = false;
    bool g_lookSnowActive = false; // the snow state the looks were built for
    // Moisture-gated covers (mud): refolded at most every kWetSeconds while the moisture data changes.
    constexpr float kWetSeconds = 3.0f;
    float    g_wetTimer = 0.0f;
    uint32_t g_wetVersion = 0;
    unsigned g_wetPending = 0; // levels still to refold (bit per level)
}

namespace wxl_livingazeroth::cover
{
    using namespace detail;

    void Configure(bool enabled, int levels, bool trenches, float depthMultiplier, float fillBudgetMs)
    {
        g_settings.enabled = enabled ? 1 : 0;
        g_settings.levels = levels < 1 ? 1 : (levels > kMaxLevels ? kMaxLevels : levels);
        g_settings.stamp = trenches ? 1 : 0;
        g_settings.depthMul = depthMultiplier < 0.0f ? 0.0f : depthMultiplier;
        g_settings.fillBudgetMs = fillBudgetMs < 0.1f ? 0.1f : fillBudgetMs;
    }

    bool Enabled() { return g_settings.enabled != 0; }
    int  Levels() { return g_settings.levels; }
    bool Trenches() { return g_settings.stamp != 0; }
    float DepthMultiplier() { return g_settings.depthMul; }
    float FillBudgetMs() { return g_settings.fillBudgetMs; }
    void SetLiftBaseEffects(bool on) { g_settings.liftBase = on ? 1 : 0; }
    bool LiftBaseEffects() { return g_settings.liftBase != 0; }

    void LoadTable(const void* cdbcApi)
    {
        g_cdbcApi = cdbcApi;
        covertable::Load(cdbcApi);
    }

    void Install(const WXL_Api* api)
    {
        g_api = api;
        for (int k = 0; k < kMaxLevels; ++k) g_levels[k].cell = kCell0 * static_cast<float>(1 << k);
        detail::InstallDraw(api);
        detail::InstallCamera(api);
        detail::InstallLift(api);
        pages::AddPanel(kPanelTitle, &Panel);
    }

    void OnDeviceLost() { detail::ReleaseDrawDeviceState(); }

    bool TopAt(float x, float y, float& z) { float depth; return detail::SampleTop(x, y, ActiveLevels(), z, depth); }
}

namespace wxl_livingazeroth::cover::detail
{
    bool SampleTop(float x, float y, int levels, float& z, float& depth, bool trenches)
    {
        if (!g_settings.enabled || g_inactive) return false;
        for (int k = 0; k < levels; ++k)
        {
            const Level& L = g_levels[k];
            if (!L.haveGrid || !L.anyCover) continue;
            // The mesh's vertex (i, j) sits at (i, j) x cell; the grid spans kGridCells cells from gridI/gridJ.
            const float fi = x / L.cell, fj = y / L.cell;
            const int i0 = static_cast<int>(std::floor(fi)), j0 = static_cast<int>(std::floor(fj));
            if (i0 < L.gridI || j0 < L.gridJ || i0 + 1 > L.gridI + kGridCells || j0 + 1 > L.gridJ + kGridCells) continue;
            const float fx = fi - i0, fy = fj - j0;
            float base[4], dep[4];
            for (int c = 0; c < 4; ++c)
            {
                const int row = Slot(j0 + (c >> 1)), col = Slot(i0 + (c & 1));
                base[c] = L.base[row * kTex + col];
                if (base[c] <= kHole) return false;
                dep[c] = DrawnDepth(L, k == 0, row, col, trenches);
            }
            auto bilerp = [&](const float* v) { return (v[0] * (1 - fx) + v[1] * fx) * (1 - fy) + (v[2] * (1 - fx) + v[3] * fx) * fy; };
            // The outermost level's fade band (0.8..0.97 of its half) is left out: nothing asks that far out.
            depth = bilerp(dep);
            z = bilerp(base) + depth;
            return depth > 0.0f;
        }
        return false;
    }
}

namespace wxl_livingazeroth::cover
{
    using namespace detail;

    void PatchLightStats(unsigned& most, unsigned& full) { most = g_patchLightsMost; full = g_patchLightsFull; }
    bool TerrainLight(float ambient[3], float diffuse[3])
    {
        if (!g_seenScene) return false;
        for (int i = 0; i < 3; ++i) { ambient[i] = g_seenLight[4 + i]; diffuse[i] = g_seenLight[8 + i]; }
        return ambient[0] + ambient[1] + ambient[2] + diffuse[0] + diffuse[1] + diffuse[2] > 0.0f;
    }

    std::string RimShaderSelfTest(void* device)
    {
        auto* dev = static_cast<IDirect3DDevice9*>(device);
        if (!dev) return "no device";
        // Three prints about a camera off the origin: a boot turned 30 degrees, a paw turned the other way and mirrored
        // (the other foot), and an oval for a print whose placement couldn't be read; the boot's and the oval's rims
        // overlap. Strengths 1, 0.6 and 0.8.
        const float eye[2] = { 1000.25f, 2000.5f };
        const auto mapping = [](float turn, float acrossYd, float alongYd, bool mirrored, float map[6])
        {
            const float c = std::cos(turn), sn = std::sin(turn), m = mirrored ? -1.0f : 1.0f;
            map[0] = m * -sn / acrossYd; map[1] = m * c / acrossYd; map[2] = 0.5f;   // u across the print
            map[3] = -c / alongYd;       map[4] = -sn / alongYd;    map[5] = 0.5f;   // v from the toes back
        };
        ripples::Shape shapes[3];
        float map[6];
        mapping(0.52f, 0.15f, 0.33f, false, map);
        bool ok = ripples::ShapeFromPrint(1, map, eye[0] - 0.25f, eye[1] - 0.2f, shapes[0]);
        mapping(-1.1f, 0.3f, 0.3f, true, map);
        ok = ripples::ShapeFromPrint(7, map, eye[0] + 0.3f, eye[1] - 0.15f, shapes[1]) && ok;
        const float axis[2] = { 0.8f, 0.6f };
        ok = ripples::ShapeFromOval(eye[0] - 0.05f, eye[1] + 0.2f, axis, 0.165f, 0.075f, shapes[2]) && ok;
        if (!ok) return "shapes failed";
        const float strength[3] = { 1.0f, 0.6f, 0.8f };
        float regs[ripples::kMax * 4 * ripples::kRegistersPerRim] = {};
        for (int k = 0; k < 3; ++k) ripples::PackRim(shapes[k], eye[0], eye[1], strength[k], regs + k * 4 * ripples::kRegistersPerRim);

        int wholeSlots = -1;
        constexpr int kSide = 120;
        constexpr float kFrom = -0.65f, kTo = 0.65f, kPx = 1e-4f;   // rel covered; a pixel fine enough not to fade
        IDirect3DSurface9* target = nullptr;
        IDirect3DSurface9* readback = nullptr;
        IDirect3DPixelShader9* ps = nullptr;
        IDirect3DSurface9* oldTarget = nullptr;
        IDirect3DStateBlock9* savedState = nullptr;
        const auto cleanup = [&]()
        {
            if (target) target->Release();
            if (readback) readback->Release();
            if (ps) ps->Release();
            if (oldTarget) { dev->SetRenderTarget(0, oldTarget); oldTarget->Release(); }
            if (savedState) { savedState->Apply(); savedState->Release(); }
        };
        if (FAILED(dev->CreateRenderTarget(kSide, kSide, D3DFMT_A32B32G32R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr)) ||
            FAILED(dev->CreateOffscreenPlainSurface(kSide, kSide, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &readback, nullptr)))
        { cleanup(); return "render target creation failed"; }
        {
            HMODULE m = GetModuleHandleA("d3dcompiler_47.dll");
            if (!m) m = LoadLibraryA("d3dcompiler_47.dll");
            const auto compile = m ? reinterpret_cast<pD3DCompile>(GetProcAddress(m, "D3DCompile")) : nullptr;
            const std::string src = std::string("float4 ripples[144] : register(c42);\nfloat4 testMap : register(c0);\n") +
                                    kRippleHlsl + "float4 main(float2 rel : TEXCOORD0) : COLOR { float2 g; float h = RippleRings(rel, testMap.w, g); return float4(h, g, 1); }";
            ID3DBlob* code = nullptr;
            ID3DBlob* err = nullptr;
            const HRESULT hr = compile ? compile(src.c_str(), src.size(), nullptr, nullptr, nullptr, "main", "ps_3_0", 0, 0, &code, &err) : E_FAIL;
            std::string why = err ? static_cast<const char*>(err->GetBufferPointer()) : std::string();
            if (err) err->Release();
            if (FAILED(hr) || !code || FAILED(dev->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()), &ps)))
            { if (code) code->Release(); cleanup(); return "rim shader failed: " + why; }
            code->Release();
            // The cover's whole pixel shader, as the game compiles it (only there otherwise): it must still compile.
            const std::string whole = std::string(kPsHlsl) + kRippleHlsl + kPsMainHlsl;
            code = nullptr;
            err = nullptr;
            const HRESULT wholeHr = compile(whole.c_str(), whole.size(), nullptr, nullptr, nullptr, "main", "ps_3_0", 0, 0, &code, &err);
            why = err ? static_cast<const char*>(err->GetBufferPointer()) : std::string();
            if (err) err->Release();
            IDirect3DPixelShader9* wholePs = nullptr;
            if (FAILED(wholeHr) || !code || FAILED(dev->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()), &wholePs)))
            { if (code) code->Release(); cleanup(); return "the cover's pixel shader failed: " + why; }
            wholePs->Release();
            using Disassemble = HRESULT(WINAPI*)(const void*, SIZE_T, UINT, const char*, ID3DBlob**);
            const auto disassemble = reinterpret_cast<Disassemble>(GetProcAddress(m, "D3DDisassemble"));
            ID3DBlob* text = nullptr;
            if (disassemble && SUCCEEDED(disassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr, &text)) && text)
            {
                const std::string t(static_cast<const char*>(text->GetBufferPointer()), text->GetBufferSize());
                const size_t at = t.find("instruction slots used");
                const size_t from = at == std::string::npos ? std::string::npos : t.rfind("approximately ", at);
                if (from != std::string::npos) wholeSlots = std::atoi(t.c_str() + from + 14);
                text->Release();
            }
            code->Release();
        }
        dev->GetRenderTarget(0, &oldTarget);
        dev->CreateStateBlock(D3DSBT_ALL, &savedState);
        struct V { float x, y, z, w, u, v; };
        const float e = static_cast<float>(kSide) - 0.5f;
        const V quad[4] = { { -0.5f, -0.5f, 0, 1, kFrom, kFrom }, { e, -0.5f, 0, 1, kTo, kFrom }, { -0.5f, e, 0, 1, kFrom, kTo }, { e, e, 0, 1, kTo, kTo } };
        if (FAILED(dev->SetRenderTarget(0, target))) { cleanup(); return "set render target failed"; }
        D3DVIEWPORT9 vp{ 0, 0, kSide, kSide, 0.0f, 1.0f };
        dev->SetViewport(&vp);
        dev->SetRenderState(D3DRS_ZENABLE, FALSE); dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE); dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        dev->SetVertexShader(nullptr); dev->SetPixelShader(ps);
        dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
        const float c0[4] = { 0.0f, 0.0f, 0.0f, kPx };
        dev->SetPixelShaderConstantF(0, c0, 1);
        dev->SetPixelShaderConstantF(42, regs, ripples::kRegistersPerRim * ripples::kMax);
        if (FAILED(dev->BeginScene())) { cleanup(); return "begin scene failed"; }
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(V));
        dev->EndScene();
        D3DLOCKED_RECT lr{};
        if (FAILED(dev->GetRenderTargetData(target, readback)) || FAILED(readback->LockRect(&lr, nullptr, D3DLOCK_READONLY)))
        { cleanup(); return "read back failed"; }
        std::vector<float> gpu(static_cast<size_t>(kSide) * kSide * 4);
        for (int row = 0; row < kSide; ++row)
            std::memcpy(&gpu[static_cast<size_t>(row) * kSide * 4], static_cast<const uint8_t*>(lr.pBits) + row * lr.Pitch, kSide * 16);
        readback->UnlockRect();
        cleanup();

        // The same from CoverRipples' RimShade, with the shapes as the shader got them.
        ripples::Shape seen[3];
        float seenStrength[3];
        for (int k = 0; k < 3; ++k)
        {
            const float* o = regs + k * 4 * ripples::kRegistersPerRim;
            seen[k] = ripples::AsPacked(shapes[k]);
            seen[k].cx = o[0]; seen[k].cy = o[1]; seen[k].r0 = 1.0f / o[2];
            seenStrength[k] = (o[3] - std::floor(o[3])) / 0.99f;
        }
        float worstH = 0.0f, worstG = 0.0f, mostG = 0.0f, peak = 0.0f;
        int lit = 0, outside = 0;
        for (int j = 0; j < kSide; ++j)
            for (int i = 0; i < kSide; ++i)
            {
                const float x = kFrom + (i + 0.5f) / kSide * (kTo - kFrom), y = kFrom + (j + 0.5f) / kSide * (kTo - kFrom);
                float h = 0.0f, g[2] = { 0.0f, 0.0f };
                for (int k = 0; k < 3; ++k)
                {
                    const float band = ripples::RimShade(seen[k], seenStrength[k], x, y, kPx, h, g);
                    // Past the reach the shader never looks: the rim must be over by then.
                    if (std::max(std::fabs(x - seen[k].cx), std::fabs(y - seen[k].cy)) >= shapes[k].reach && band < ripples::kRimOffset + 0.5f * ripples::kRimBand)
                        ++outside;
                }
                const float* p = &gpu[(static_cast<size_t>(j) * kSide + i) * 4];
                worstH = std::max(worstH, std::fabs(p[0] - h));
                worstG = std::max(worstG, std::hypot(p[1] - g[0], p[2] - g[1]));
                mostG = std::max(mostG, std::hypot(g[0], g[1]));
                peak = std::max(peak, p[0]);
                lit += p[0] > 0.05f ? 1 : 0;
            }
        char line[320];
        std::snprintf(line, sizeof(line), "rims: GPU against CoverRipples over %d x %d pixels: height off by up to %.4f (peak %.3f, %d pixels lit), slope by %.4f of %.3f; %d inside a rim past its reach; the cover's pixel shader compiles at %d instruction slots",
                      kSide, kSide, worstH, peak, lit, worstG, mostG, outside, wholeSlots);
        if (worstH > 0.02f || worstG > 0.02f * mostG || peak < 0.9f || lit < 300 || outside || wholeSlots <= 0 || wholeSlots > 3500)
            return std::string("FAIL ") + line;
        return line;
    }

    void RingShading(float& slope, float& brightness)
    {
        slope = g_settings.rippleSlope;
        brightness = g_settings.rippleBright;
    }

    unsigned WmoRings(float* out, unsigned max, bool snow)
    {
        const float sandLife = ripples::LifeFor(10.0f * g_settings.relaxMul);
        if (!g_settings.enabled || !g_settings.ripples) return 0;
        // The newest first: the list keeps its order of arrival except where a newer one took an oldest's place.
        std::vector<const ripples::Ripple*> live;
        for (const ripples::Ripple& r : g_wmoRipples.List()) live.push_back(&r);
        std::sort(live.begin(), live.end(), [](const ripples::Ripple* a, const ripples::Ripple* b) { return a->born > b->born; });
        unsigned n = 0;
        for (const ripples::Ripple* r : live)
        {
            if (n >= max) break;
            const float strength = ripples::Strength(g_rippleClock - r->born, snow ? r->life : std::min(r->life, sandLife));
            if (strength <= 0.0f || !(r->shape.r0 > 0.0f)) continue;
            ripples::PackRim(r->shape, 0.0f, 0.0f, strength, out + n * 4 * kWmoRingRegisters);
            ++n;
        }
        return n;
    }

    bool PrintsOnCover()
    {
        return g_settings.enabled && g_settings.printsOnCover && g_settings.drawPoint == 0 && g_printLift.valid;
    }

    float DrawnCoverAbove(float x, float y, float terrainZ)
    {
        if (!PrintsOnCover()) return 0.0f;
        const Level& L = g_levels[0];
        const PrintLift& d = g_printLift;
        if (!L.haveGrid) return 0.0f;
        if (d.outermost && (x - d.cx) * (x - d.cx) + (y - d.cy) * (y - d.cy) > d.radius * d.radius) return 0.0f;
        const float u = x / L.cell, v = y / L.cell;
        const float fu = std::floor(u), fv = std::floor(v);
        const int i = static_cast<int>(fu), j = static_cast<int>(fv);
        if (i < L.gridI || j < L.gridJ || i >= L.gridI + kGridCells || j >= L.gridJ + kGridCells) return 0.0f;
        const float fx = u - fu, fy = v - fv;
        const int corner[4][2] = { { i, j }, { i + 1, j }, { i, j + 1 }, { i + 1, j + 1 } };
        const float w[4] = { (1.0f - fx) * (1.0f - fy), fx * (1.0f - fy), (1.0f - fx) * fy, fx * fy };
        float top = 0.0f, depth = 0.0f;
        for (int k = 0; k < 4; ++k)
        {
            const int s = SlotOf(corner[k][0], corner[k][1]);
            const float base = L.base[s];
            if (base <= kHole * 0.5f) return 0.0f;
            // What the vertex shader draws: base + the uploaded depth (x its edge fade when outermost).
            float c = blend::DeformedDepth(g_static0[s], g_rimCell[s] * g_settings.rimMul, g_rim[s], g_press[s], L.stiff[s] / 255.0f, DeformK(L, s));
            if (d.outermost)
            {
                const int ci = std::abs(corner[k][0] - (L.gridI + kHalfCells)), cj = std::abs(corner[k][1] - (L.gridJ + kHalfCells));
                const float start = kHalfCells * kOuterFadeStart, end = kHalfCells * kOuterFadeEnd;
                const float f = (end - static_cast<float>(ci > cj ? ci : cj)) / (end - start);
                c *= f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
            }
            top += w[k] * (base + c);
            depth += w[k] * c;
        }
        if (!(depth >= kMinDrawnDepth)) return 0.0f;
        const float above = top - terrainZ;
        return above > 0.0f ? above : 0.0f;
    }

    bool SeenSceneFog(float vsFog[4], float psFogColor[4])
    {
        std::memcpy(vsFog, g_seenFog, sizeof(g_seenFog));
        std::memcpy(psFogColor, g_seenFogColor, sizeof(g_seenFogColor));
        return g_seenScene;
    }

    void Update(float dt, const world::Snapshot& snap)
    {
        // Leaving the world (logout, loading screens without a player) unloads every tile: drop
        // everything that refers to them, and start over on the way back.
        if (!snap.inWorld)
        {
            if (g_wasInWorld)
            {
                terrain::ClearLayerCache();
                ClearLayerValues();
                ClearCellCache();
                g_hereLayersValid = g_hereValid = false;
                for (Level& L : g_levels) { L.haveGrid = false; L.jobActive = false; }
                g_lastMap = -1;
            }
            g_wasInWorld = false;
            return;
        }
        g_wasInWorld = true;
        if (!g_settings.enabled) return;
        if (dt < 0.0f || dt > 0.5f) dt = 0.0f;
        const double t0 = grassperf::Now();

        g_mapNow = snap.mapId;
        const bool newMap = g_lastMap != snap.mapId;
        const bool refill = newMap || g_lastCoverage != g_settings.coverage || g_tableGeneration != covertable::Generation() ||
                            g_lastMaterialSource != g_settings.materialSource || g_lastAlphaSwap != g_settings.alphaSwap;
        if (refill) { ClearCellCache(); ClearLayerValues(); }
        if (newMap) terrain::ClearLayerCache();
        g_lastMaterialSource = g_settings.materialSource;
        g_lastAlphaSwap = g_settings.alphaSwap;
        g_lastCoverage = g_settings.coverage;
        g_lastMap = snap.mapId;
        g_tableGeneration = covertable::Generation();
        if (g_lastDepthMul != g_settings.depthMul || g_lastDriftMul != g_settings.driftMul)
        {
            g_lastDepthMul = g_settings.depthMul; g_lastDriftMul = g_settings.driftMul;
            for (Level& L : g_levels) L.coverDirty = true;
        }
        if (g_lastBreakupMul != g_settings.breakupMul)
        {
            g_lastBreakupMul = g_settings.breakupMul;
            for (Level& L : g_levels) { L.coverDirty = true; L.boostRows = kFilled; } // the warp is sampled
        }

        ++g_frame;
        const int levels = ActiveLevels();
        // Nothing within the outermost level's reach (no cover, no ground fallen snow could settle
        // on while it snows): the finer levels rest -- no recentres, no re-sampling -- and only the
        // outermost keeps watching. Cover coming into reach shows up there first, seconds before the
        // finer levels' ground does, and they catch up (or refill) from where they stopped.
        const Level& outer = g_levels[levels - 1];
        g_resting = levels > 1 && !refill && outer.haveGrid && !outer.jobActive && outer.coverCells <= 0 &&
                    (outer.snowCells <= 0 || !fields::SnowActive());
        for (int k = 0; k < levels; ++k)
        {
            Level& L = g_levels[k];
            if (g_resting && k < levels - 1) continue; // (a job already running still works off below)
            // A refill or a new map replaces whatever job was running; a recentre waits for it.
            if (refill) PlanFill(L, snap.playerPos, newMap || !L.haveGrid ? Plan::NewPlace : Plan::RefillInPlace);
            else if (!L.jobActive) PlanFill(L, snap.playerPos, Plan::Recentre);
            if (L.jobActive) continue; // re-sampling and hole counting wait for the fill
            // Chunks stream in after the grid saw them: re-sample a few rows per frame, keeping trenches.
            // Once nothing is missing one row is enough, except for a pass at full speed after a
            // setting that's applied while sampling changed.
            const int fast = k == 0 ? 8 : 4;
            const bool idle = !L.holes && L.boostRows <= 0;
            const int rows = !idle ? fast : ((g_frame % 8) == static_cast<unsigned>(k) ? 1 : 0);
            if (L.boostRows > 0) L.boostRows -= rows;
            for (int r = 0; r < rows; ++r)
            {
                const int row = L.gridJ - kApron + L.refreshRow;
                SampleRect(L, k == 0, L.gridI - kApron, L.gridI - kApron + kFilled - 1, row, row, false);
                L.refreshRow = (L.refreshRow + 1) % kFilled;
            }
            if (rows && L.refreshRow < rows) // once per pass: count what's still missing
            {
                unsigned holes = 0;
                for (int j = 0; j < kGridVerts; ++j)
                    for (int i = 0; i < kGridVerts; ++i) holes += L.base[SlotOf(L.gridI + i, L.gridJ + j)] == kHole;
                L.holes = holes;
            }
        }
        // A level switched off is refilled from scratch when it comes back.
        for (int k = levels; k < kMaxLevels; ++k) { g_levels[k].haveGrid = false; g_levels[k].jobActive = false; }

        // Fills within the budget. Levels with nothing on screen go first, coarsest first (a coarse
        // level covers the most ground for the same work, so there's always cover under the player);
        // the rest of the budget is shared evenly by the visible levels' jobs, so fast travel can't
        // starve any of them.
        const double end = grassperf::Now() + (g_settings.fillBudgetMs > 0.1f ? g_settings.fillBudgetMs : 0.1f);
        int order[kMaxLevels], jobs = 0;
        for (int k = levels - 1; k >= 0; --k) if (g_levels[k].jobActive && g_levels[k].jobHidden) order[jobs++] = k;
        for (int k = 0; k < levels; ++k)      if (g_levels[k].jobActive && !g_levels[k].jobHidden) order[jobs++] = k;
        for (int n = 0; n < jobs; ++n)
        {
            const double now = grassperf::Now();
            if (now >= end) break;
            const int k = order[n];
            const bool hidden = g_levels[k].jobHidden;
            const double deadline = hidden ? end : now + (end - now) / (jobs - n);
            WorkFill(g_levels[k], k == 0, deadline);
        }

        // New snow values (fallen snow, painted snow melting): one level re-folds its depth per frame.
        // While there's no snow anywhere and the last fold had none either, there's nothing to redo;
        // nor in a level without a single cell snow can change (its fold would come out the same:
        // it re-folded every frame in zones without cover, ~2.8 ms CPU + GPU for nothing).
        {
            const uint32_t version = fields::SnowVersion();
            const bool active = fields::SnowActive();
            // Ground that could hold fallen snow shows the snow's look only while snow is around (else a
            // painted neighbour's, see LookSource): snow starting or stopping redoes the looks.
            if (active != g_lookSnowActive)
            {
                g_lookSnowActive = active;
                for (Level& L : g_levels) L.lookDirty = true;
            }
            for (int n = 0; n < levels; ++n)
            {
                const int k = (g_snowCursor + n) % levels;
                Level& L = g_levels[k];
                if (!L.haveGrid || L.snowVersion == version) continue;
                if (L.snowCells <= 0 && L.coverCells <= 0) { L.snowVersion = version; continue; } // nothing snow can change here
                L.snowVersion = version;
                if (active || L.snowFolded) L.coverDirty = true;
                L.snowFolded = active;
                g_snowCursor = k + 1;
                break;
            }
        }

        // Moisture-gated covers: every few seconds, if the ground's moisture changed, the levels with
        // such cells refold, one per frame. (So mud stays a moment after the ground dried.)
        g_wetTimer += dt;
        if (g_wetTimer >= kWetSeconds)
        {
            g_wetTimer = 0.0f;
            const uint32_t version = fields::Wet().version;
            if (version != g_wetVersion) { g_wetVersion = version; g_wetPending = (1u << levels) - 1u; }
        }
        for (int k = 0; k < levels && g_wetPending; ++k)
        {
            if (!(g_wetPending & (1u << k))) continue;
            g_wetPending &= ~(1u << k);
            Level& L = g_levels[k];
            if (L.haveGrid && L.wetCells > 0) { L.coverDirty = true; break; }
        }

        // The table's values where the player is, for the panel's "here" lines.
        covertable::Values here;
        g_hereValid = terrain::SurfaceAt(snap.playerPos[0], snap.playerPos[1], g_hereSurface);
        if (g_hereValid)
        {
            g_hereTexture = g_hereSurface.texture ? g_hereSurface.texture : "";
            if (static_cast<Coverage>(g_settings.coverage) == Coverage::Table)
                here = covertable::Resolve(g_hereSurface.area, snap.mapId, g_hereSurface.texture, g_hereSurface.groundEffect, g_hereSurface.terrainType);
        }
        g_hereValues = here;
        float terrainZ;
        g_hereMccvValid = terrain::VertexColorAt(snap.playerPos[0], snap.playerPos[1], g_hereMccv);
        g_hereLiquidValid = terrain::LiquidHeightAt(snap.playerPos[0], snap.playerPos[1], g_hereLiquid) &&
                            terrain::HeightAt(snap.playerPos[0], snap.playerPos[1], terrainZ);
        if (g_hereLiquidValid) g_hereLiquidDepth = g_hereLiquid - terrainZ;
        g_hereLayersValid = terrain::LayerWeightsAt(snap.playerPos[0], snap.playerPos[1], g_hereLayers, false) &&
                            terrain::LayerWeightsAt(snap.playerPos[0], snap.playerPos[1], g_hereLayersSwapped, true);
        for (int l = 0; l < 4; ++l)
        {
            g_hereLayerTexture[l] = g_hereLayersValid && l < g_hereLayers.layers && g_hereLayers.surface[l].texture ? g_hereLayers.surface[l].texture : "";
            g_hereLayers.surface[l].texture = g_hereLayersSwapped.surface[l].texture = nullptr;
        }

        Simulate(dt);
        g_simMs = grassperf::Now() - t0;
    }
}
