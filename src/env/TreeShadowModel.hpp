// Tree shadows that follow the wind (features/TreeShadows): the part that touches no client memory -- the client's shadow
// cache layout and the guards on it, the setup of one band's redraw, when a band is due, which models count as foliage,
// and one frame's refresh over a Client interface the extension binds to the real calls and the tests to fakes.
//
// How the client's world shadows work (12340; wowdecomp ShaderEffect/ShadowCache.cpp and WorldClient/ShadowQuery.cpp,
// checked against the exe; extShadowQuality is CShadowCache::s_shadowMode at 0xD43154):
//  - 1-2: one shadow map round the player (s_extDynShadow, 20 yd), drawn every frame: units only.
//  - 3-4: that map, now also with the animated doodads (bones flagged 0x2F8), plus three cached bands round the
//    player, 40, 160 and 640 yd (s_extShadowBands, 0xD43290; 1024 texels at 3, 2048 at 4), double-buffered, which hold
//    everything static: map doodads with no such bones (nearly every tree: one rigid bone) and the buildings. A band is
//    drawn again only after the viewer has moved 2, 4 or 32 yd from where it was drawn (CShadowCache::GatherBands
//    0x874890 compares the squared distance with 4, 16, 1024), and then a ninth of it a frame (a 25th for the far
//    one) into its back texture, shown when done. Wind on a tree's bones (wxl-treewind) reaches its shadow only then:
//    the shadow holds a pose until the player walks on, and steps.
//  - 5: all three bands drawn again whole every frame, everything in them (the client's "cascaded shadow maps"): the
//    trees' shadows follow the wind, at the cost of redrawing 640 yd of the world four times a frame.
// The refresh here keeps quality 3-4: right after the client's own CShadowCache::RenderBands (0x874FB0, the last step
// of CShadowCache::Update before its PCF state is put back), the near band's shown texture is drawn again, whole, by
// the client's own frustum, query and render callbacks, centred where the client drew it (m_lastPos, what the shaders'
// matrices are built from in PostUpdate), so this frame's scene reads this frame's poses. Only when a tree or bush casts
// into the band (the query's batch lists), and at most Settings::rate times a second.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace wxl_livingazeroth::treeshadow
{
    // --- the client's shadow cache ------------------------------------------------------------------------------------
    constexpr uintptr_t kRenderBands = 0x00874FB0;       // CShadowCache::RenderBands(CShadowCacheQueryData&), __cdecl
    constexpr uintptr_t kFrustumExtents = 0x00983990;    // CFrustum::Extents(CAaBox&), __thiscall, ret 4
    constexpr uintptr_t kFrustumCallbackPtr = 0x00D4315C; // CShadowCache::s_frustumCallback
    constexpr uintptr_t kQueryCallbackPtr = 0x00D43160;   // CShadowCache::s_queryCallback
    constexpr uintptr_t kRenderCallbackPtr = 0x00D43164;  // CShadowCache::s_renderCallback
    constexpr uintptr_t kFrustumCallback = 0x007BAFD0;    // CShadowQuery::FrustumCallback, what CShadowQuery::Initialize sets
    constexpr uintptr_t kQueryCallback = 0x007BD200;      // CShadowQuery::QueryCallback
    constexpr uintptr_t kRenderCallback = 0x007BBC50;     // CShadowQuery::RenderCallback
    constexpr uintptr_t kShadowMode = 0x00D43154;         // CShadowCache::s_shadowMode (extShadowQuality, 0..5)
    constexpr uintptr_t kRecreateTextures = 0x00B1D51C;   // CShadowCache::s_recreateTextures: maps not made yet
    constexpr uintptr_t kBands = 0x00D43290;              // CShadowBand s_extShadowBands[3]
    constexpr uintptr_t kViewer = 0x00D43260;             // s_extDynShadow.m_lastPos: the viewer, set each frame by Update
    constexpr uintptr_t kQueryBuffers = 0x00D25320;       // ShadowQueryBuffers CShadowQuery::s_bandBuffers[3]

    // CShadowBand (0x3C)
    constexpr size_t kBandStride = 0x3C;
    constexpr size_t kBandTextures = 0x00;    // CTexture* [2]
    constexpr size_t kBandRadius = 0x08;      // 40, 160, 640
    constexpr size_t kBandLastPos = 0x10;     // C3Vector: the centre of the texture shown (m_current)
    constexpr size_t kBandDir = 0x28;         // C3Vector: the up vector its views are built with
    constexpr size_t kBandUpdateCount = 0x34; // the tile its own redraw is at (0: none under way)
    constexpr size_t kBandCurrent = 0x38;     // which texture is shown

    // CShadowCacheQueryData (0xA8C), per band [3]
    constexpr size_t kQdFlags = 0x000;     // int32: 1 units, 4 animated doodads, 8 everything static (+ buildings)
    constexpr size_t kQdActive = 0x00C;    // int32
    constexpr size_t kQdUseCull = 0x018;   // int32
    constexpr size_t kQdExtents = 0x024;   // CAaBox (0x18)
    constexpr size_t kQdFrustums = 0x06C;  // CFrustum (0xF4)
    constexpr size_t kQdPos = 0x930;       // C3Vector
    constexpr size_t kQdTexIndex = 0x954;  // uint8: which of the band's textures RenderBands draws into
    constexpr size_t kQdRadius = 0x958;    // float
    constexpr size_t kQdBounds = 0x964;    // float[4]: the ortho box, -r..r for the whole band
    constexpr size_t kQdTexBounds = 0x994; // float[4]: the viewport, 0..1 for the whole band
    constexpr size_t kQdMatrices = 0x9C4;  // C44Matrix (0x40)
    constexpr size_t kQdLastBand = 0xA84;  // int32
    constexpr size_t kQdUseFrustum = 0xA88; // int32
    constexpr size_t kQdSize = 0xA8C;
    constexpr size_t kAaBoxSize = 0x18, kFrustumSize = 0xF4, kMatrixSize = 0x40;
    constexpr int32_t kQueryStatic = 8;    // what the client's own bands ask for at quality 3-4

    // ShadowQueryBuffers (0x24): what a query gathered for a band
    constexpr size_t kBufStride = 0x24;
    constexpr size_t kBufGroups = 0x00;            // uint32: building groups
    constexpr size_t kBufUntexturedBatches = 0x0C; // CModelBatch*, then the count at +4
    constexpr size_t kBufTexturedBatches = 0x18;
    constexpr size_t kBatchStride = 0x0C;          // CModelBatch: model, batch index, count
    constexpr uint32_t kMaxBatches = 0x2000;       // the lists' capacity
    constexpr size_t kModelShared = 0x2C;          // CM2Model -> CM2Shared
    constexpr size_t kSharedPath = 0x3C;           // CM2Shared: the model's path, inline

    // --- the guards: the code above, byte for byte, before any hook or call ------------------------------------------
    struct Guard
    {
        const char* what;
        uintptr_t   address;
        uint32_t    size;
        uint8_t     bytes[16];
    };
    inline constexpr Guard kGuards[] = {
        {"RenderBands: entry, quality read past the maps-not-made flag", 0x00874FB0, 16,
         {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x8B, 0x0D, 0x1C, 0xD5, 0xB1, 0x00, 0xF7, 0xD9, 0x1B, 0xC9}},
        {"RenderBands: the bands up to m_lastBand (+0xA84)", 0x00874FCF, 6, {0x39, 0xB7, 0x84, 0x0A, 0x00, 0x00}},
        {"RenderBands: each band's m_active (+0xC)", 0x00874FE0, 3, {0x8D, 0x57, 0x0C}},
        {"RenderBands: the band array", 0x00874FE7, 5, {0xBB, 0x90, 0x32, 0xD4, 0x00}},
        {"RenderBands: the texture by m_textureIndex (+0x954)", 0x0087501A, 8, {0x0F, 0xB6, 0x8C, 0x3E, 0x54, 0x09, 0x00, 0x00}},
        {"RenderBands: the band's textures", 0x00875024, 7, {0x8B, 0x14, 0x8D, 0x90, 0x32, 0xD4, 0x00}},
        {"RenderBands: the band's direction (+0x28)", 0x0087506D, 3, {0x8D, 0x4B, 0x28}},
        {"RenderBands: the render callback", 0x00875075, 6, {0xFF, 0x15, 0x64, 0x31, 0xD4, 0x00}},
        {"Update: bands only above quality 2", 0x0087629D, 9, {0x83, 0xFF, 0x02, 0x0F, 0x8E, 0x83, 0x00, 0x00, 0x00}},
        {"Update: RenderBands, then nothing more of the query data", 0x0087631A, 16,
         {0x8D, 0x8D, 0x74, 0xF4, 0xFF, 0xFF, 0x51, 0xE8, 0x8A, 0xEC, 0xFF, 0xFF, 0x83, 0xC4, 0x14, 0x83}},
        {"Update: the PCF state put back after it", 0x0087632A, 7, {0x3D, 0x14, 0x30, 0xD4, 0x00, 0x00, 0x5B}},
        {"Update: the viewer kept in s_extDynShadow.m_lastPos", 0x00876018, 5, {0xA3, 0x60, 0x32, 0xD4, 0x00}},
        {"GatherBands: the band's radius into m_radius (+0x958)", 0x008749F4, 13,
         {0xD9, 0x81, 0x98, 0x32, 0xD4, 0x00, 0xD9, 0x9C, 0xBE, 0x58, 0x09, 0x00, 0x00}},
        {"GatherBands: m_textureIndex from m_current (+0x38)", 0x0087498B, 7, {0x83, 0xB9, 0xC8, 0x32, 0xD4, 0x00, 0x00}},
        {"GatherBands: the moved distance from m_lastPos (+0x10)", 0x008749AA, 6, {0xD9, 0x81, 0xA0, 0x32, 0xD4, 0x00}},
        {"GatherBands: m_pos (+0x930)", 0x00874A2A, 7, {0x8D, 0x84, 0x7F, 0x4C, 0x02, 0x00, 0x00}},
        {"GatherBands: m_bounds (+0x964)", 0x00874A90, 6, {0xD9, 0x93, 0x64, 0x09, 0x00, 0x00}},
        {"GatherBands: m_texBounds (+0x994)", 0x00874C4C, 6, {0xD9, 0x9B, 0x9C, 0x09, 0x00, 0x00}},
        {"GatherBands: the frustum callback with m_matrices (+0x9C4)", 0x00874C60, 16,
         {0x8D, 0x80, 0xB8, 0x32, 0xD4, 0x00, 0x50, 0x8D, 0x84, 0x32, 0xC4, 0x09, 0x00, 0x00, 0x50, 0x51}},
        {"GatherBands: the frustum callback", 0x00874C70, 6, {0xFF, 0x15, 0x5C, 0x31, 0xD4, 0x00}},
        {"GatherBands: m_extents (+0x24) from m_frustums (+0x6C)", 0x00874C79, 4, {0x8D, 0x54, 0xCE, 0x24}},
        {"GatherBands: CFrustum::Extents", 0x00874C84, 8, {0x83, 0xC1, 0x6C, 0xE8, 0x04, 0xED, 0x10, 0x00}},
        {"GatherBands: m_queryFlags 8", 0x00874CC2, 7, {0xC7, 0x04, 0xBE, 0x08, 0x00, 0x00, 0x00}},
        {"GatherBands: m_useCullFrustum (+0x18)", 0x00874CA9, 8, {0xC7, 0x44, 0xBE, 0x18, 0x01, 0x00, 0x00, 0x00}},
        {"Update: m_useFrustum (+0xA88) off below quality 5", 0x008762FB, 10, {0xC7, 0x85, 0xFC, 0xFE, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00}},
        {"CFrustum::Extents: entry", 0x00983990, 16,
         {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x6A, 0x08, 0x83, 0xC1, 0x60, 0x51, 0x8D, 0x45, 0xE8, 0x50}},
        {"CShadowQuery::FrustumCallback: entry", 0x007BAFD0, 16,
         {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x50, 0x02, 0x00, 0x00, 0xD9, 0x05, 0x80, 0x31, 0xD4, 0x00, 0x8B}},
        {"CShadowQuery::QueryCallback: entry", 0x007BD200, 16,
         {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0xE8, 0x35, 0xD6, 0xFF, 0xFF, 0x0F, 0xB6, 0x05, 0x00, 0x53}},
        {"CShadowQuery::RenderCallback: entry", 0x007BBC50, 16,
         {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xA4, 0x02, 0x00, 0x00, 0x53, 0x8B, 0x5D, 0x0C, 0x8D, 0x04, 0xDB}},
        {"CShadowQuery::Initialize: its frustum and query callbacks", 0x007BD3AA, 16,
         {0x68, 0xD0, 0xAF, 0x7B, 0x00, 0xE8, 0xFC, 0x6B, 0x0B, 0x00, 0x68, 0x00, 0xD2, 0x7B, 0x00, 0xE8}},
        {"CShadowQuery::Initialize: its render callback", 0x007BD3BA, 14,
         {0x02, 0x6C, 0x0B, 0x00, 0x68, 0x50, 0xBC, 0x7B, 0x00, 0xE8, 0x08, 0x6C, 0x0B, 0x00}},
        {"CShadowCache::SetFrustumCallback: s_frustumCallback", 0x00873FB0, 13,
         {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0xA3, 0x5C, 0x31, 0xD4, 0x00, 0x5D, 0xC3}},
        {"CShadowCache::SetQueryCallback: s_queryCallback", 0x00873FC0, 13,
         {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0xA3, 0x60, 0x31, 0xD4, 0x00, 0x5D, 0xC3}},
        {"CShadowCache::SetRenderCallback: s_renderCallback", 0x00873FD0, 13,
         {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0xA3, 0x64, 0x31, 0xD4, 0x00, 0x5D, 0xC3}},
        {"AllocBuffers: the band buffers' counts", 0x007BA850, 16,
         {0x39, 0x7E, 0xF0, 0x89, 0x7E, 0xEC, 0x89, 0x7E, 0xFC, 0xC7, 0x06, 0x00, 0x20, 0x00, 0x00, 0x89}},
        {"AllocBuffers: the band buffers", 0x007BA842, 5, {0xBE, 0x34, 0x53, 0xD2, 0x00}},
        {"GatherModelBatchesShadowMap: the model's shared data (+0x2C)", 0x00834679, 3, {0x8B, 0x46, 0x2C}},
    };

    /// The first guard whose bytes differ (read through `read(address, size)`, a pointer to that many bytes or null),
    /// or null when all match.
    template <class Read> const Guard* FirstMismatch(Read read)
    {
        for (const Guard& guard : kGuards)
        {
            const uint8_t* at = static_cast<const uint8_t*>(read(guard.address, guard.size));
            if (!at) return &guard;
            for (uint32_t i = 0; i < guard.size; ++i)
                if (at[i] != guard.bytes[i]) return &guard;
        }
        return nullptr;
    }

    // --- one band's redraw ---------------------------------------------------------------------------------------------
    /// Fills band `band`'s slot of a CShadowCacheQueryData for a whole-band redraw into texture `textureIndex`, centred at
    /// `center`: what GatherBands writes for a tile, with the tile the whole band (bounds -r..r, viewport 0..1), the
    /// static casters (flags 8), no cull frustum.
    void SetUpBand(uint8_t* data, int band, const float center[3], float radius, uint32_t textureIndex);
    /// Takes `band` out of a query and a render (flags 0, not active).
    void Deactivate(uint8_t* data, int band);
    /// The query covers bands 0..lastBand, each by its own frustum.
    void Finish(uint8_t* data, int lastBand);

    /// Whether a band is due: at most `rate` redraws a second on average (0 or less: every frame). `next` is the band's.
    bool Due(double& next, double nowMs, float rate);

    /// A tree or bush by its model path, as wxl-treewind picks what sways (its keywords: tree, pine, canopy, oak, ...,
    /// bush, shrub, fern, hedge, ...): a dead or burnt tree counts too (a redraw it didn't need, nothing worse); "street"
    /// doesn't count as "tree".
    bool LooksLikeFoliage(const char* path);

    // --- one frame -----------------------------------------------------------------------------------------------------
    struct BandView
    {
        float    radius = 0.0f;
        float    lastPos[3] = {};
        uint32_t current = 0;
        bool     textures = false; // both of its textures made
    };

    struct Casters
    {
        unsigned untextured = 0, textured = 0, groups = 0; // M2 batches and building groups the query gathered
        unsigned models = 0, foliage = 0;                  // models among the batches (consecutive runs), trees and bushes
        char     firstFoliage[64] = {};                    // the file name of the first tree or bush
    };

    /// LooksLikeFoliage by the path's hash, so a model met every frame is matched once.
    class FoliageCache
    {
    public:
        bool Is(const char* path);
        size_t Size() const { return m_known.size(); }

    private:
        std::unordered_map<uint32_t, bool> m_known;
    };

    /// Counts one batch list into `out` (`count` CModelBatch records at `records`, the model pointer first in each): a
    /// model's batches come one after another, so a run is one model. `pathOf(model)` is its path, or null.
    template <class PathOf>
    void CountBatches(const uint8_t* records, uint32_t count, PathOf pathOf, FoliageCache& cache, Casters& out)
    {
        uintptr_t previous = 0;
        for (uint32_t i = 0; i < count && i < kMaxBatches; ++i)
        {
            uintptr_t model = 0;
            std::memcpy(&model, records + static_cast<size_t>(i) * kBatchStride, sizeof(model));
            if (!model || model == previous) continue;
            previous = model;
            ++out.models;
            const char* path = pathOf(model);
            if (!path || !cache.Is(path)) continue;
            if (!out.foliage++)
            {
                const char* name = path;
                for (const char* p = path; *p; ++p)
                    if (*p == '\\' || *p == '/') name = p + 1;
                size_t n = 0;
                for (; name[n] && n + 1 < sizeof(out.firstFoliage); ++n) out.firstFoliage[n] = name[n];
                out.firstFoliage[n] = 0;
            }
        }
    }

    /// What the refresh needs from the client. Every call returns false where it couldn't be done (a fault, a null).
    struct Client
    {
        int (*quality)();                          // extShadowQuality while its maps are made, else -1
        bool (*treeWind)();                        // wxl-treewind is loaded
        bool (*stockCallbacks)();                  // the cache's frustum, query and render callbacks are the client's own
        bool (*viewer)(float out[3]);              // where the client centres the shadows this frame
        bool (*band)(int index, BandView& out);
        bool (*frustum)(uint8_t* data, int band);  // the band's frustum and matrix (frustum callback), then its extents
        bool (*query)(uint8_t* data);              // the query callback: the casters of every active band
        bool (*casters)(int band, Casters& out);   // what that query gathered for `band`
        bool (*render)(uint8_t* data);             // RenderBands: every active band into its m_textureIndex texture
        double (*nowMs)();
    };

    struct Settings
    {
        int   enabled = 1;     // the near band (40 yd) follows the wind
        float rate = 30.0f;    // its redraws per second at most (0: every frame)
        int   farTrees = 0;    // the 160 yd band too
        float farRate = 10.0f; // its redraws per second at most (0: every frame)
    };

    /// Why a band was or wasn't redrawn this frame.
    enum class Outcome : int
    {
        Drawn,        // drawn again
        Off,          // its setting is off
        Quality,      // no cached bands at this extShadowQuality (0-2)
        ClientDoesIt, // extShadowQuality 5: the client redraws every band every frame itself
        NoTreeWind,   // nothing sways
        Foreign,      // the cache's callbacks aren't the client's own (another module's): left alone
        NoMaps,       // the band's textures aren't made
        Away,         // the viewer is outside what the band shows (it's being redrawn elsewhere, a teleport)
        NotDue,       // drawn recently enough
        Quiet,        // no tree or bush cast into it lately: looked again after kQuietMs
        NoFoliage,    // looked: no tree or bush casts into it
        Fault,        // a client call faulted: off until the next load
        Count
    };
    const char* OutcomeText(Outcome o);
    constexpr double kQuietMs = 500.0;

    struct BandStats
    {
        unsigned drawn = 0, looked = 0;            // redraws; queries that found no tree or bush
        unsigned outcomes[static_cast<int>(Outcome::Count)] = {};
        double   ms = 0.0, maxMs = 0.0;            // CPU time of the queries and redraws (the client's draws included)
        Casters  last;                             // the last query's
    };

    struct State
    {
        double    next[2] = {}, quietUntil[2] = {};
        Outcome   outcome[2] = {Outcome::Off, Outcome::Off}; // this frame's
        int       quality = -1;
        bool      faulted = false;
        BandStats window[2];   // since the last Tick roll
        BandStats second[2];   // the last whole second
        BandStats sinceLog[2]; // since the last log line
        double    windowStart = 0.0, windowMs = 0.0;
        unsigned  frames = 0;       // frames RunFrame ran in the window
        unsigned  secondFrames = 0; // in the last whole second
    };

    /// One frame, right after the client's own RenderBands with its query data: the bands that are due drawn again.
    void RunFrame(const Client& c, uint8_t* data, const Settings& s, State& st);
    /// Once a frame (any time): rolls the one-second window.
    void Tick(State& st, double nowMs);
}
