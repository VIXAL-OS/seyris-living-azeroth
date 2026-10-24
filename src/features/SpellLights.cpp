#include "SpellLights.hpp"

#include "../debug/Profiler.hpp"
#include "../debug/UiText.hpp"
#include "../render/BlpColour.hpp"
#include "../render/BlpTexture.hpp"
#include "GrassPerf.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace wxl_livingazeroth::spelllights
{
    namespace sl = spelllight;

    namespace
    {
        // Guarded instead of a VirtualQuery per read, like env/Lights' scan: every pointer comes from the client's own
        // lists, and one that has gone stale fails cleanly.
        bool ReadGuarded(const void* p, void* out, size_t n)
        {
            if (reinterpret_cast<uintptr_t>(p) < 0x10000) return false;
            __try
            {
                std::memcpy(out, p, n);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        template <class T> bool Get(const void* base, size_t off, T& out)
        {
            return base && ReadGuarded(static_cast<const uint8_t*>(base) + off, &out, sizeof(T));
        }

        template <class T> T GetOr(const void* base, size_t off, T fallback)
        {
            T v;
            return Get(base, off, v) ? v : fallback;
        }

        // A NUL-terminated string of at most `cap` - 1 characters.
        bool ReadString(const void* p, char* out, size_t cap)
        {
            if (!p || cap < 2) return false;
            for (size_t done = 0; done < cap - 1;)
            {
                const size_t n = std::min<size_t>(32, cap - 1 - done);
                if (!ReadGuarded(static_cast<const uint8_t*>(p) + done, out + done, n))
                {
                    // The end of a readable region inside this chunk: byte by byte.
                    for (size_t k = 0; k < n; ++k)
                    {
                        if (!ReadGuarded(static_cast<const uint8_t*>(p) + done + k, out + done + k, 1)) return false;
                        if (!out[done + k]) return true;
                    }
                }
                else if (std::memchr(out + done, 0, n))
                    return true;
                done += n;
            }
            out[cap - 1] = 0;
            return false;
        }

        const Guard kGuards[] = {
            { 0x006FF214, 6, { 0x8B, 0x35, 0x58, 0x0B, 0xCA, 0x00 }, "CMissile::AdjustAllMissileTrajectoryDurations reads s_inFlightMissileList (0xCA0B58)" },
            { 0x006FF233, 6, { 0x8B, 0xB6, 0x64, 0x01, 0x00, 0x00 }, "... and each missile's next at +0x164" },
            { 0x0070392E, 6, { 0x8B, 0x1D, 0x5C, 0x0B, 0xCA, 0x00 }, "CMissile::UpdateAttachments reads s_missileDestroyList (0xCA0B5C)" },
            { 0x00703971, 3, { 0x8B, 0x43, 0x18 }, "... and a missile's model at +0x18" },
            { 0x00834550, 18, { 0x8D, 0x42, 0x08, 0x89, 0x46, 0x08, 0x89, 0x56, 0x28, 0x8B, 0x38, 0x8D, 0x4E, 0x0C, 0x89, 0x39, 0x89, 0x30 },
              "CM2Model::AttachToScene puts the model at the head of the scene's list (+0x08; its own next +0x0C, its scene +0x28)" },
            { 0x0083482E, 3, { 0x89, 0x4E, 0x2C }, "CM2Model::Initialize: its shared model at +0x2C" },
            { 0x00834843, 21, { 0x8B, 0x45, 0x14, 0x8B, 0x56, 0x28, 0x8D, 0x4E, 0x34, 0x89, 0x46, 0x04, 0x89, 0x4E, 0x38, 0x8B, 0x42, 0x0C, 0x89, 0x46, 0x74 },
              "... its creation flags at +0x04 and its loop origin +0x74 = the scene's time (+0x0C)" },
            { 0x00828466, 22, { 0x8B, 0x86, 0xC0, 0x02, 0x00, 0x00, 0x80, 0xBC, 0x03, 0x85, 0x00, 0x00, 0x00, 0x00, 0x74, 0x26, 0xD8, 0x96, 0x98, 0x01, 0x00, 0x00 },
              "CM2Model::GetNumPrimitives: the emitter states +0x2C0 (active +0x85), the effective alpha +0x198" },
            { 0x00828483, 6, { 0x8B, 0x8E, 0xC4, 0x02, 0x00, 0x00 }, "... the emitters +0x2C4" },
            { 0x008284A8, 6, { 0x81, 0xC3, 0x88, 0x00, 0x00, 0x00 }, "... 0x88 bytes per emitter state" },
            { 0x0082813B, 13, { 0x8B, 0x9E, 0xA0, 0x00, 0x00, 0x00, 0xC1, 0xE2, 0x05, 0xD8, 0x4C, 0x1A, 0x1C },
              "... a batch's colour: the colours +0xA0, 0x20 apart, the alpha at +0x1C" },
            { 0x0082815D, 13, { 0x8B, 0x96, 0xA8, 0x00, 0x00, 0x00, 0x8D, 0x04, 0x40, 0xD8, 0x4C, 0x82, 0x08 },
              "... its texture weight: the weights +0xA8, 12 bytes apart, the value at +8" },
            { 0x00828414, 11, { 0x8B, 0x86, 0xB8, 0x02, 0x00, 0x00, 0x80, 0x7C, 0x18, 0x4C, 0x00 },
              "... the ribbon states +0x2B8 (visible +0x4C)" },
            { 0x00828446, 9, { 0x83, 0xC3, 0x50, 0x3B, 0xB8, 0x20, 0x01, 0x00, 0x00 }, "... 0x50 bytes per ribbon state, the file's ribbons +0x120" },
            { 0x0097B9E0, 4, { 0x83, 0x79, 0x50, 0x00 }, "CParticleEmitter2::HasLiveParticles: the live particles' count +0x50" },
            { 0x00833EEE, 24, { 0x8D, 0x8E, 0x04, 0x01, 0x00, 0x00, 0x89, 0x8F, 0xD8, 0x00, 0x00, 0x00, 0x8D, 0x96, 0x14, 0x01, 0x00, 0x00,
                                0x8D, 0x86, 0x24, 0x01, 0x00, 0x00 },
              "CM2Model's emitter set-up: the colour track M2Particle +0x104, the alpha track +0x114, the scale track +0x124" },
            { 0x00830B57, 19, { 0x8B, 0x86, 0x9C, 0x00, 0x00, 0x00, 0x83, 0xF8, 0x01, 0x77, 0x12, 0x75, 0x19, 0x8B, 0x96, 0xA0, 0x00, 0x00, 0x00 },
              "the particles' animation: the lifespan track M2Particle +0x98 (its keys +0x9C, +0xA0)" },
        };

        sl::Settings g_settings;
        Stats        g_stats;
        FileReader   g_read = &blp::ReadBytes;
        bool (*g_indoor)(const float pos[3]) = &lights::IsIndoor;
        int          g_guards = 0;       // 0 not checked, 1 hold, -1 don't
        std::string  g_guardWhy;

        sl::VisualSets g_sets;
        bool g_setsRead = false;

        // Glow colours of textures, by path, for the session (render/BlpColour GlowColour), read a couple a frame.
        struct Texture { std::string path; float rgb[3] = { 1.0f, 1.0f, 1.0f }; uint8_t state = 0; }; // 0 waiting, 1 known, 2 none
        std::vector<Texture> g_textures;
        std::unordered_map<std::string, int> g_textureIndex;
        std::vector<int> g_textureQueue;
        size_t g_textureNext = 0;
        constexpr unsigned kTexturesPerFrame = 2;
        constexpr double   kTextureBudgetMs = 1.0;

        // One model file (CM2Shared), by its address; its name is compared each time to notice the address reused.
        struct Emitter { float track[3] = { 1.0f, 1.0f, 1.0f }; float weight = 0.0f; int texture = -1; float life = 0.5f; };
        struct File
        {
            std::string raw;                 // the name as the client has it
            uint64_t head8 = 0;              // its first 8 bytes (the light scan's quick check that the address is still it)
            std::string path;                // normalized (spelllight::NormalizePath)
            sl::Kind pathKind = sl::Kind::None; // what the file alone says (spelllight::PathKind), once
            bool  neverLit = false;
            bool  objectModel = false;       // a game object's own model (GameObjectDisplayInfo)
            bool  loaded = false;            // its data read
            float bounds = 1.0f;
            uint32_t vertices = 0, colours = 0, weights = 0;
            std::vector<uint8_t> lights;     // its point lights' indices
            std::vector<Emitter> emitters;
            std::vector<Emitter> ribbons;    // its ribbons: colour and alpha (their tracks' first keys), texture
            std::vector<int> textures;       // its textures (file ones: -1 for the rest)
            std::vector<int> rest;           // of those, the ones its emitters and ribbons don't use (its geometry's; a spell's file only)
            bool  hasKeyword = false;
            float keyword[3] = {};
        };
        std::unordered_map<const void*, File> g_files;
        constexpr size_t kMaxFiles = 8192;
        uint32_t g_filesGeneration = 1;   // changes whenever g_files' entries may have gone (a clear, an erase)

        struct Tracked
        {
            const uint8_t* model = nullptr;
            const void* shared = nullptr;
            uint32_t origin = 0;
            sl::Kind kind = sl::Kind::Effect;
            bool  alive = true;
            sl::Fade fade;
            float pos[3] = {};
            float radius = 0.0f;
            bool  placed = false;
            // Each emitter's live particles over wall time (spelllight::StepCount) and the most it held.
            float count[sl::kMaxEmitters] = {}, countPeak[sl::kMaxEmitters] = {};
            uint64_t lastShown = 0, firstFrame = 0;
            float age = 0.0f;                // s since it was first followed
            uint32_t stamp = 0;              // its frame stamp (+0x3C) as last seen, and s since it last changed
            float sinceStamp = 1e3f;
            float held[3] = {};              // the colour it last gave when looked at closely, and s since
            float sinceLook = 0.0f;
            // Its place in the frame's selection (spelllight::Select): presence, kept, its cluster.
            float presence = 0.0f;
            bool  kept = false;
            const void* label = nullptr;
            // Its file, kept while the cache holds it (g_filesGeneration): a model alive holds its shared model, so its name
            // needn't be read again every frame.
            File* file = nullptr;
            uint32_t fileGeneration = 0;
            // This frame (Place): drawn now and within reach, how far (squared), its size; looked at closely (Shine).
            bool  shining = false, lookedAt = false;
            float distance2 = 1e30f, size = 1.0f;
        };
        std::unordered_map<const void*, Tracked> g_tracked;
        constexpr size_t kMaxTracked = 2048;
        constexpr unsigned kHeadWalkCap = 512, kMissileCap = 256;
        constexpr uint64_t kForgetFrames = 600;   // alive but not drawn for this long: forgotten (offered again when it shows)

        struct Head { const void* model; uint32_t origin; };
        Head     g_heads[8];
        unsigned g_headCount = 0;
        unsigned g_offered = 0;
        uint64_t g_frame = 0;

        std::vector<Drawn> g_drawn;
        std::vector<sl::Light> g_models, g_frameLights;   // this frame: one per model lit (with its state), the lights drawn
        std::vector<Tracked*> g_owners;                   // g_models' models
        double g_lastMs = 0.0;                            // grassperf::Now() at the last Update (its wall time between frames)

        struct IndoorCell { bool indoor; DWORD expires; };
        std::unordered_map<uint64_t, IndoorCell> g_indoorCells;
        constexpr unsigned kIndoorTestsPerFrame = 4;

        double   g_windowSum = 0.0, g_windowMost = 0.0;
        unsigned g_windowFrames = 0;
        DWORD    g_windowStart = 0;
        std::string g_status;

        // --- the tables ---------------------------------------------------------------------------------------------------
        void EnsureSets()
        {
            if (g_setsRead || !g_read) return;
            g_setsRead = true;
            std::vector<uint8_t> bytes;
            if (g_read("DBFilesClient\\SpellVisualEffectName.dbc", bytes))
            {
                g_sets.spellRows = sl::LoadVisualTable(bytes.data(), bytes.size(), 2, g_sets.spells);
                sl::LoadVisualTable(bytes.data(), bytes.size(), 2, g_sets.hardcoded, 1, "HARDCODED");
            }
            bytes.clear();
            if (g_read("DBFilesClient\\ItemVisualEffects.dbc", bytes))
                g_sets.itemRows = sl::LoadVisualTable(bytes.data(), bytes.size(), 1, g_sets.items);
            bytes.clear();
            if (g_read("DBFilesClient\\GameObjectDisplayInfo.dbc", bytes))
                g_sets.objectRows = sl::LoadVisualTable(bytes.data(), bytes.size(), 1, g_sets.objects);
            g_stats.spellRows = g_sets.spellRows;
            g_stats.itemRows = g_sets.itemRows;
            g_stats.objectRows = g_sets.objectRows;
            g_stats.clientEffects = static_cast<unsigned>(g_sets.hardcoded.size());
            g_stats.objectModels = static_cast<unsigned>(g_sets.objects.size());
        }

        // --- textures -----------------------------------------------------------------------------------------------------
        int TextureSlot(const char* name)
        {
            std::string key = sl::NormalizePath(name);
            if (key.empty()) return -1;
            auto it = g_textureIndex.find(key);
            if (it != g_textureIndex.end()) return it->second;
            const int index = static_cast<int>(g_textures.size());
            Texture t;
            t.path = key;
            g_textures.push_back(t);
            g_textureIndex.emplace(std::move(key), index);
            g_textureQueue.push_back(index);
            return index;
        }

        void ReadTextures()
        {
            g_stats.texturesRead = 0;
            if (!g_read) return;
            const double t0 = grassperf::Now();
            std::vector<uint8_t> bytes;
            while (g_textureNext < g_textureQueue.size() && g_stats.texturesRead < kTexturesPerFrame && grassperf::Now() - t0 < kTextureBudgetMs)
            {
                Texture& t = g_textures[g_textureQueue[g_textureNext++]];
                bytes.clear();
                float rgb[3];
                if (g_read(t.path.c_str(), bytes) && blp::GlowColour(bytes.data(), bytes.size(), rgb))
                {
                    std::memcpy(t.rgb, rgb, sizeof(rgb));
                    t.state = 1;
                }
                else t.state = 2;
                ++g_stats.texturesRead;
            }
            if (g_textureNext >= g_textureQueue.size()) { g_textureQueue.clear(); g_textureNext = 0; }
        }

        const float* TextureColour(int index)
        {
            static const float kWhite[3] = { 1.0f, 1.0f, 1.0f };
            return index >= 0 && index < static_cast<int>(g_textures.size()) && g_textures[index].state == 1 ? g_textures[index].rgb : kWhite;
        }

        // --- model files --------------------------------------------------------------------------------------------------
        File* FileFor(const void* shared)
        {
            char name[264];
            if (!shared || !ReadString(static_cast<const uint8_t*>(shared) + kSharedName, name, 261) || !name[0]) return nullptr;
            auto it = g_files.find(shared);
            if (it != g_files.end() && it->second.raw == name) return &it->second;
            if (it != g_files.end()) { g_files.erase(it); ++g_filesGeneration; }
            if (g_files.size() >= kMaxFiles) { g_files.clear(); ++g_filesGeneration; }
            File f;
            f.raw = name;
            std::memcpy(&f.head8, name, std::min<size_t>(8, std::strlen(name) + 1));
            f.path = sl::NormalizePath(name);
            f.pathKind = sl::PathKind(f.path, g_sets);
            f.neverLit = sl::NeverLit(f.path, g_sets);
            f.objectModel = sl::ObjectModel(f.path, g_sets);
            f.hasKeyword = sl::KeywordColour(f.path, f.keyword);
            return &g_files.emplace(shared, std::move(f)).first->second;
        }

        // The light scan's quick look-up: the file at this address as known, if its name still starts the same.
        File* FileQuick(const void* shared)
        {
            auto it = g_files.find(shared);
            if (it == g_files.end()) return FileFor(shared);
            uint64_t head = 0;
            if (!ReadGuarded(static_cast<const uint8_t*>(shared) + kSharedName, &head, sizeof(head))) return nullptr;
            uint64_t mask = ~0ull;
            if (it->second.raw.size() < 8) mask = (1ull << (8 * (it->second.raw.size() + 1))) - 1;
            return (head & mask) == (it->second.head8 & mask) ? &it->second : FileFor(shared);
        }

        // A particle track's keys (M2PartTrack: times then values, each count + pointer once loaded).
        template <class T>
        uint32_t TrackKeys(const uint8_t* track, size_t valueSize, int16_t* times, uint32_t& timeCount, T* values)
        {
            uint32_t tc = 0, vc = 0;
            const uint8_t* tp = nullptr;
            const uint8_t* vp = nullptr;
            timeCount = 0;
            if (!Get(track, 0x0, tc) || !Get(track, 0x4, tp) || !Get(track, 0x8, vc) || !Get(track, 0xC, vp) || !vc || !vp) return 0;
            vc = std::min(vc, 16u);
            if (!ReadGuarded(vp, values, valueSize * vc)) return 0;
            if (tc == vc && tp && ReadGuarded(tp, times, 2 * tc)) timeCount = tc;
            return vc;
        }

        // An M2Track's first key (its first sequence's first value): values (count, pointer to per-sequence (count,
        // pointer)) at +0x0C [believed, as env/Lights' FirstTrackValue: once loaded, the nested arrays hold pointers too].
        bool FirstKey(const uint8_t* track, void* out, size_t size)
        {
            uint32_t sequences = 0, count = 0;
            const uint8_t* perSequence = nullptr;
            const uint8_t* values = nullptr;
            if (!Get(track, 0x0C, sequences) || !Get(track, 0x10, perSequence) || !sequences || !perSequence) return false;
            if (!Get(perSequence, 0, count) || !Get(perSequence, 4, values) || !count || !values) return false;
            return ReadGuarded(values, out, size);
        }

        void LoadFile(File& f, const void* shared)
        {
            if (f.loaded) return;
            const uint8_t* data = nullptr;
            if (!Get(shared, kSharedData, data) || !data) return;   // still loading
            uint32_t magic = 0;
            if (!Get(data, 0, magic) || magic != 0x3032444D) return; // "MD20"
            f.bounds = GetOr<float>(data, kDataBoundsRadius, 1.0f);
            f.vertices = GetOr<uint32_t>(data, kDataVertices, 0);
            f.colours = std::min(GetOr<uint32_t>(data, kDataColours, 0), 8u);
            f.weights = std::min(GetOr<uint32_t>(data, kDataTextureWeights, 0), 8u);
            // Textures: the file ones by name (type 0), the replaceable ones have none.
            uint32_t nt = 0;
            const uint8_t* textures = nullptr;
            if (Get(data, kDataTextures, nt) && Get(data, kDataTextures + 4, textures) && textures)
                for (uint32_t t = 0; t < std::min(nt, 32u); ++t)
                {
                    const uint8_t* rec = textures + t * kTextureStride;
                    uint32_t type = 1, len = 0;
                    const char* name = nullptr;
                    char buf[264];
                    Get(rec, 0, type);
                    if (type == 0 && Get(rec, kTextureName, len) && Get(rec, kTextureName + 4, name) && len > 1 && ReadString(name, buf, 261))
                        f.textures.push_back(TextureSlot(buf));
                    else f.textures.push_back(-1);
                }
            uint32_t nl = 0;
            const uint8_t* lightsArr = nullptr;
            if (Get(data, kDataLights, nl) && Get(data, kDataLights + 4, lightsArr) && lightsArr)
                for (uint32_t l = 0; l < std::min(nl, 16u); ++l)
                {
                    uint16_t type = 0;
                    if (Get(lightsArr + l * kFileLightStride, 0, type) && type == 1) f.lights.push_back(static_cast<uint8_t>(l));
                }
            uint32_t ne = 0;
            const uint8_t* emitters = nullptr;
            if (Get(data, kDataEmitters, ne) && Get(data, kDataEmitters + 4, emitters) && emitters)
                for (uint32_t e = 0; e < std::min<uint32_t>(ne, sl::kMaxEmitters); ++e)
                {
                    const uint8_t* p = emitters + e * kParticleStride;
                    Emitter em;
                    const uint8_t blend = GetOr<uint8_t>(p, kParticleBlend, 2);
                    const uint16_t tex = GetOr<uint16_t>(p, kParticleTexture, 0xFFFF);
                    int16_t ct[16], at[16], st[16];
                    float cv[16 * 3], sv[16 * 2];
                    int16_t av[16];
                    uint32_t ctn = 0, atn = 0, stn = 0;
                    const uint32_t cn = TrackKeys(p + kParticleColourTrack, 12, ct, ctn, cv);
                    const uint32_t an = TrackKeys(p + kParticleAlphaTrack, 2, at, atn, av);
                    const uint32_t sn = TrackKeys(p + kParticleScaleTrack, 8, st, stn, sv);
                    float alphaMean = 1.0f, scale = 0.0f;
                    sl::EmitterTrackColour(ct, ctn, cn ? cv : nullptr, cn, at, atn, an ? av : nullptr, an, em.track, alphaMean);
                    for (uint32_t k = 0; k < sn; ++k) scale += 0.5f * (std::fabs(sv[2 * k]) + std::fabs(sv[2 * k + 1])) / sn;
                    em.weight = sl::BlendWeight(blend) * alphaMean * sl::ScaleWeight(sn ? scale : 0.5f);
                    em.texture = tex < f.textures.size() ? f.textures[tex] : -1;
                    // Its particles' life (the lifespan track's first key; 0.5 s when it can't be read): how long its
                    // live count is smoothed over.
                    float life = 0.0f;
                    em.life = FirstKey(p + kParticleLifespan, &life, sizeof(life)) && std::isfinite(life) && life > 0.01f && life < 60.0f ? life : 0.5f;
                    f.emitters.push_back(em);
                }
            // Its ribbons: a spell's glowing trail (Arcane Missiles, Frostbolt); a thrown weapon's or an arrow's is the streak
            // of its flight, no light (any file outside spells\ lights by its particles and own lights alone).
            uint32_t nr = 0, nm = 0;
            const uint8_t* ribbons = nullptr;
            const uint8_t* materials = nullptr;
            Get(data, kDataMaterials, nm);
            Get(data, kDataMaterials + 4, materials);
            if (f.pathKind != sl::Kind::None && Get(data, kDataRibbons, nr) && Get(data, kDataRibbons + 4, ribbons) && ribbons)
                for (uint32_t r = 0; r < std::min(nr, 8u); ++r)
                {
                    const uint8_t* rb = ribbons + r * kRibbonStride;
                    Emitter em;
                    uint32_t n = 0;
                    const uint8_t* p = nullptr;
                    uint16_t tex = 0xFFFF, mat = 0xFFFF, blend = 4;
                    if (Get(rb, kRibbonTextures, n) && n && Get(rb, kRibbonTextures + 4, p) && p) Get(p, 0, tex);
                    if (Get(rb, kRibbonMaterials, n) && n && Get(rb, kRibbonMaterials + 4, p) && p) Get(p, 0, mat);
                    if (materials && mat < nm) Get(materials + mat * kMaterialStride, kMaterialBlend, blend);
                    float colour[3] = { 1.0f, 1.0f, 1.0f };
                    int16_t alpha = 32767;
                    if (FirstKey(rb + kRibbonColourTrack, colour, sizeof(colour)))
                        for (float& c : colour) c = std::isfinite(c) ? std::clamp(c, 0.0f, 1.0f) : 1.0f;
                    FirstKey(rb + kRibbonAlphaTrack, &alpha, sizeof(alpha));
                    std::memcpy(em.track, colour, sizeof(colour));
                    em.weight = sl::BlendWeight(static_cast<uint8_t>(std::min<uint16_t>(blend, 255))) * std::clamp(alpha / 32767.0f, 0.0f, 1.0f);
                    em.texture = tex < f.textures.size() ? f.textures[tex] : -1;
                    f.ribbons.push_back(em);
                }
            // The rest's glow (its geometry's textures) for a spell's own file only: an arrow or a thrown axe flying as a
            // missile gives light by its particles and lights, not by its wood and steel.
            if (f.pathKind != sl::Kind::None)
                for (int t : f.textures)
                {
                    bool used = false;
                    for (const Emitter& em : f.emitters) used = used || em.texture == t;
                    for (const Emitter& em : f.ribbons) used = used || em.texture == t;
                    if (t >= 0 && !used) f.rest.push_back(t);
                }
            f.loaded = true;
        }

        // --- discovery ----------------------------------------------------------------------------------------------------
        bool Register(const uint8_t* model, bool missile, bool quick = false)
        {
            const void* shared = nullptr;
            uint32_t origin = 0, flags = 0;
            if (!Get(model, kModelShared, shared) || !shared || !Get(model, kModelLoopOrigin, origin) || !Get(model, kModelFlags, flags)) return false;
            // Followed already (the missile lists offer theirs every frame): nothing to look up.
            auto known = g_tracked.find(model);
            if (known != g_tracked.end() && known->second.shared == shared && known->second.origin == origin)
            {
                if (missile) known->second.kind = sl::Kind::Missile;
                return true;
            }
            File* f = quick ? FileQuick(shared) : FileFor(shared);
            if (!f) return false;
            // Hung on another model (a unit's, a weapon's) or placed alone (a game object's own model is placed alone).
            const void* parent = nullptr;
            const bool attached = f->objectModel && Get(model, kModelParent, parent) && parent != nullptr;
            const sl::Kind kind = sl::KindOf(f->pathKind, f->neverLit, f->objectModel, flags, missile, attached);
            if (kind == sl::Kind::None || (kind == sl::Kind::Enchant && !g_settings.enchants)) return false;
            auto it = g_tracked.find(model);
            if (it != g_tracked.end())
            {
                if (it->second.shared == shared && it->second.origin == origin)
                {
                    if (kind == sl::Kind::Missile) it->second.kind = kind;
                    return true;
                }
                g_tracked.erase(it);   // a new model in a slot the allocator handed out again
            }
            if (g_tracked.size() >= kMaxTracked) return true;
            Tracked t;
            t.model = model;
            t.shared = shared;
            t.origin = origin;
            t.kind = kind;
            t.firstFrame = g_frame;
            t.stamp = GetOr<uint32_t>(model, kModelFrame, 0);   // only a change from here counts as animated recently
            g_tracked.emplace(model, t);
            return true;
        }

        void WalkHead(const void* scene)
        {
            const uint8_t* head = nullptr;
            g_stats.headWalked = g_stats.headNew = 0;
            if (!Get(scene, kSceneModels, head)) return;
            const uint8_t* m = head;
            for (; m && g_stats.headWalked < kHeadWalkCap; ++g_stats.headWalked)
            {
                const uint32_t origin = GetOr<uint32_t>(m, kModelLoopOrigin, 0);
                bool known = false;
                for (unsigned k = 0; k < g_headCount && !known; ++k) known = g_heads[k].model == m && g_heads[k].origin == origin;
                if (known) break;
                if (Register(m, false)) ++g_stats.headNew;
                const uint8_t* next = nullptr;
                if (!Get(m, kModelNext, next)) break;
                m = next;
            }
            g_headCount = 0;
            for (m = head; m && g_headCount < 8;)
            {
                g_heads[g_headCount++] = { m, GetOr<uint32_t>(m, kModelLoopOrigin, 0) };
                const uint8_t* next = nullptr;
                if (!Get(m, kModelNext, next)) break;
                m = next;
            }
        }

        unsigned WalkMissiles(const void* first)
        {
            unsigned n = 0;
            const uint8_t* m = static_cast<const uint8_t*>(first);
            for (; m && n < kMissileCap; ++n)
            {
                const uint8_t* model = nullptr;
                if (Get(m, kMissileModel, model) && model) Register(model, true);
                const uint8_t* next = nullptr;
                if (!Get(m, kMissileNext, next)) break;
                m = next;
            }
            return n;
        }

        // --- what a model gives off ---------------------------------------------------------------------------------------
        // `dt`: the time since it was last looked at.
        void BuildLook(Tracked& t, const File& f, sl::Look& look, float dt)
        {
            const uint8_t* model = t.model;
            look.alpha = std::clamp(GetOr<float>(model, kModelEffectiveAlpha, 1.0f), 0.0f, 1.0f);
            if (!std::isfinite(look.alpha)) look.alpha = 1.0f;
            look.hasKeyword = f.hasKeyword;
            std::memcpy(look.keyword, f.keyword, sizeof(look.keyword));

            // Its emitters: as bright as their particles alive (smoothed over a quarter of their life, spelllight::StepCount)
            // of the most they held.
            const uint8_t* emitters = nullptr;
            Get(model, kModelEmitters, emitters);
            const bool young = t.age < sl::kYoungSeconds;
            if (emitters)
                for (size_t e = 0; e < f.emitters.size() && e < sl::kMaxEmitters; ++e)
                {
                    const uint8_t* em = nullptr;
                    uint32_t live = 0;
                    if (Get(emitters, e * 4, em) && em) Get(em, kEmitterLive, live);
                    live = std::min(live, 65535u);
                    const Emitter& fe = f.emitters[e];
                    if (!live && !(t.count[e] > 0.0f)) continue;   // none alive, none lately
                    sl::StepCount(t.count[e], static_cast<float>(live), sl::CountSeconds(fe.life, young), dt);
                    t.countPeak[e] = std::max(t.countPeak[e], t.count[e]);
                    if (!(t.count[e] > 0.0f)) continue;
                    const float share = std::sqrt(std::min(1.0f, t.count[e] / std::max(4.0f, t.countPeak[e])));
                    const float* tc = TextureColour(fe.texture);
                    sl::EmitterLook& out = look.emitters[look.emitterCount++];
                    for (int k = 0; k < 3; ++k) out.color[k] = fe.track[k] * tc[k];
                    out.weight = fe.weight * share;
                }

            // Its ribbons (a missile's trail), while the client keeps them visible.
            const uint8_t* ribbonStates = nullptr;
            if (!f.ribbons.empty() && Get(model, kModelRibbons, ribbonStates) && ribbonStates)
                for (size_t r = 0; r < f.ribbons.size() && look.emitterCount < sl::kMaxEmitters; ++r)
                {
                    uint8_t visible = 0;
                    if (!Get(ribbonStates + r * kRibbonStateStride, kRibbonStateVisible, visible) || !visible) continue;
                    const Emitter& fr = f.ribbons[r];
                    const float* tc = TextureColour(fr.texture);
                    sl::EmitterLook& out = look.emitters[look.emitterCount++];
                    for (int k = 0; k < 3; ++k) out.color[k] = fr.track[k] * tc[k];
                    out.weight = fr.weight;
                }

            const uint8_t* records = nullptr;
            if (!f.lights.empty() && Get(model, kModelLights, records) && records)
                for (uint8_t l : f.lights)
                {
                    const uint8_t* rec = records + l * kLightStride;
                    uint32_t enabled = 0;
                    uint8_t visible = 0;
                    float c[3];
                    if (!Get(rec, kLightEnabled, enabled) || !enabled || !Get(rec, kLightVisibility, visible) || !visible ||
                        !ReadGuarded(rec + kLightCm2 + kCm2Colour, c, sizeof(c)))
                        continue;
                    for (int k = 0; k < 3; ++k)
                        if (std::isfinite(c[k])) look.light[k] += std::clamp(c[k], 0.0f, 64.0f);
                    look.hasLight = true;
                }

            // The rest of it (its geometry and ribbons: a missile's bolt, a shield, an ice block): the glow of the textures
            // its emitters don't use, tinted by its colours, as visible as its most visible colour (alpha) and texture
            // weight say; 0.8 of a full additive emitter's weight in the mix.
            if (!f.rest.empty())
            {
                double g[3] = {};
                unsigned known = 0;
                for (int ti : f.rest)
                    if (ti >= 0 && g_textures[ti].state == 1)
                    {
                        for (int k = 0; k < 3; ++k) g[k] += g_textures[ti].rgb[k];
                        ++known;
                    }
                if (known)
                {
                    float tint[3] = { 1.0f, 1.0f, 1.0f }, visible = 1.0f;
                    const uint8_t* colours = nullptr;
                    if (f.colours && Get(model, kModelColours, colours) && colours)
                    {
                        double sum[3] = {}, alphaSum = 0.0;
                        float most = 0.0f;
                        for (uint32_t c = 0; c < f.colours; ++c)
                        {
                            float v[3] = {}, a = 0.0f;
                            if (!ReadGuarded(colours + c * kColourStride + kColourValue, v, sizeof(v)) || !Get(colours + c * kColourStride, kColourAlpha, a)) continue;
                            a = std::isfinite(a) ? std::clamp(a, 0.0f, 1.0f) : 0.0f;
                            for (int k = 0; k < 3; ++k) sum[k] += (std::isfinite(v[k]) ? std::clamp(v[k], 0.0f, 1.0f) : 0.0f) * a;
                            alphaSum += a;
                            most = std::max(most, a);
                        }
                        if (alphaSum > 1e-4)
                            for (int k = 0; k < 3; ++k) tint[k] = static_cast<float>(sum[k] / alphaSum);
                        visible = most;
                    }
                    const uint8_t* weights = nullptr;
                    if (f.weights && Get(model, kModelTextureWeights, weights) && weights)
                    {
                        float most = 0.0f;
                        for (uint32_t w = 0; w < f.weights; ++w)
                        {
                            float v = 0.0f;
                            if (Get(weights + w * kWeightStride, kWeightValue, v) && std::isfinite(v)) most = std::max(most, std::clamp(v, 0.0f, 1.0f));
                        }
                        visible *= most;
                    }
                    for (int k = 0; k < 3; ++k) look.geometry[k] = static_cast<float>(g[k] / known) * tint[k];
                    look.geometryWeight = 0.8f * visible;
                }
            }
        }

        bool Finite(const float* v, int n)
        {
            for (int k = 0; k < n; ++k) if (!std::isfinite(v[k])) return false;
            return true;
        }

        // Each frame for every model followed: whether it's still the model it was and in the scene (a model taken out of
        // the scene and put back is followed on; its memory handed to another model has another file or loop origin),
        // drawn by the client lately, where it is and how far its light reaches (a few of its own cache lines).
        void Place(Tracked& t, const ClientView& view, uint32_t sceneFrame, const float eye[3], float dt)
        {
            const void* shared = nullptr;
            const void* prev = nullptr;
            const void* scene = nullptr;
            uint32_t origin = 0;
            t.alive = Get(t.model, kModelShared, shared) && shared == t.shared && Get(t.model, kModelLoopOrigin, origin) &&
                      origin == t.origin && Get(t.model, kModelScenePrev, prev) && prev && Get(t.model, kModelScene, scene) && scene == view.scene;
            t.shining = false;
            t.size = 1.0f;
            t.distance2 = 1e30f;
            if (!t.alive) return;
            const uint32_t init = GetOr<uint32_t>(t.model, kModelInit, 0), stamp = GetOr<uint32_t>(t.model, kModelFrame, 0);
            // Drawn: animated this frame or lately (the client animates a far or busy model every few frames).
            if (stamp != t.stamp) { t.stamp = stamp; t.sinceStamp = 0.0f; }
            else t.sinceStamp += dt;
            const bool shown = (init & 1) && (sceneFrame - stamp <= 3u || t.sinceStamp < kHoldSeconds);
            float w[16];
            const void* parent = nullptr;
            bool placed = false, animated = false;
            if (Get(t.model, kModelParent, parent) && !parent) placed = ReadGuarded(t.model + kModelWorld, w, sizeof(w));
            else if (parent) placed = lights::AttachedWorld(t.model, w, animated);
            placed = placed && Finite(w, 16);
            if (!t.file || t.fileGeneration != g_filesGeneration)
            {
                t.file = FileFor(shared);
                t.fileGeneration = g_filesGeneration;
            }
            File* f = t.file;
            if (!placed || !f) return;
            LoadFile(*f, shared);
            const float scale = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
            t.size = sl::SizeOf(f->bounds, scale);
            t.radius = sl::LightRadius(g_settings, t.kind, t.size);
            t.pos[0] = w[12]; t.pos[1] = w[13]; t.pos[2] = w[14] + sl::Lift(t.size);
            t.placed = true;
            if (!f->loaded) { ++g_stats.filesWaiting; return; }
            if (!shown) return;
            t.lastShown = g_frame;
            ++g_stats.shown;
            const float dx = t.pos[0] - eye[0], dy = t.pos[1] - eye[1], dz = t.pos[2] - eye[2];
            const float reach = g_settings.range + t.radius;
            t.distance2 = dx * dx + dy * dy + dz * dz;
            t.shining = t.distance2 <= reach * reach;
        }

        // For the nearest models drawn now: what it gives off (its lights, particles, ribbons, geometry); one drawn but not
        // looked at closely this frame keeps what it last gave (for kHoldSeconds); then the fade.
        void Shine(Tracked& t, float dt)
        {
            float target[3] = {};
            t.sinceLook += dt;
            if (t.shining && t.lookedAt && t.file)
            {
                sl::Look look;
                BuildLook(t, *t.file, look, std::min(t.sinceLook, 0.5f));
                float c[3];
                if (sl::Shade(look, c))
                {
                    const float k = sl::SizeGain(t.size) * (t.kind == sl::Kind::Enchant ? g_settings.enchantStrength : 1.0f);
                    for (int i = 0; i < 3; ++i) target[i] = c[i] * k;
                }
                std::memcpy(t.held, target, sizeof(target));
                t.sinceLook = 0.0f;
            }
            else if (t.shining && t.sinceLook < kHoldSeconds)
                std::memcpy(target, t.held, sizeof(target));
            sl::StepFade(t.fade, target, dt);
            t.age += dt;
        }

        bool IndoorAt(const float pos[3], unsigned& tests)
        {
            if (!g_indoor || !lights::Config().indoorSkipsTerrain) return false;
            const auto q = [](float v) { return static_cast<uint64_t>(static_cast<int64_t>(std::floor(v * 0.25f)) & 0x1FFFFF); };
            const uint64_t key = q(pos[0]) | (q(pos[1]) << 21) | (q(pos[2]) << 42);
            const DWORD now = GetTickCount();
            auto it = g_indoorCells.find(key);
            if (it != g_indoorCells.end() && static_cast<int32_t>(it->second.expires - now) > 0) return it->second.indoor;
            if (tests >= kIndoorTestsPerFrame) return it != g_indoorCells.end() && it->second.indoor;
            ++tests;
            if (g_indoorCells.size() > 4096) g_indoorCells.clear();
            const bool indoor = g_indoor(pos);
            g_indoorCells[key] = IndoorCell{ indoor, now + 3000 + static_cast<DWORD>(key % 1000) };
            return indoor;
        }

        void Frame(const ClientView& view, float dt, const float eye[3], std::vector<lights::FrameLight>& out)
        {
            const double t0 = grassperf::Now();
            ++g_frame;
            const unsigned offered = g_offered;
            g_offered = 0;
            Stats& st = g_stats;
            st.fromScan = offered;
            st.shown = st.looked = st.lit = st.fading = st.filesWaiting = 0;
            EnsureSets();
            const uint32_t sceneFrame = GetOr<uint32_t>(view.scene, kSceneFrame, 0);
            if (view.scene) WalkHead(view.scene);
            st.inFlight = WalkMissiles(view.missiles[0]);
            st.impact = WalkMissiles(view.missiles[1]);
            const double tFound = grassperf::Now();
            st.msFind = tFound - t0;
            ReadTextures();
            const double tTextures = grassperf::Now();
            st.msTextures = tTextures - tFound;

            g_models.clear();
            g_owners.clear();
            unsigned byKind[4] = {};
            // Where each one is; only the nearest kLooksPerFrame drawn now (3 x the cap, at least 48) are looked at
            // closely (their emitters, ribbons, colours: a dozen scattered reads each), the rest keep what they last gave.
            static std::vector<Tracked*> shining;
            shining.clear();
            for (auto& entry : g_tracked)
            {
                Place(entry.second, view, sceneFrame, eye, dt);
                entry.second.lookedAt = false;
                if (entry.second.shining) shining.push_back(&entry.second);
            }
            const size_t looks = std::max<size_t>(48, 3 * static_cast<size_t>(std::clamp(g_settings.maxLights, 0, sl::kMaxLightsCap)));
            if (shining.size() > looks)
            {
                std::nth_element(shining.begin(), shining.begin() + looks, shining.end(), [](const Tracked* x, const Tracked* y)
                {
                    return x->distance2 != y->distance2 ? x->distance2 < y->distance2 : std::less<const void*>()(x->model, y->model);
                });
                shining.resize(looks);
            }
            st.looked = static_cast<unsigned>(shining.size());
            for (Tracked* t : shining) t->lookedAt = true;
            for (auto it = g_tracked.begin(); it != g_tracked.end();)
            {
                Tracked& t = it->second;
                Shine(t, dt);
                const bool faded = t.fade.level < 0.004f;
                if ((!t.alive && faded) || (t.alive && faded && g_frame - std::max(t.lastShown, t.firstFrame) > kForgetFrames))
                {
                    it = g_tracked.erase(it);
                    continue;
                }
                ++byKind[static_cast<int>(t.kind)];
                if (!t.alive) ++st.fading;
                if (!faded && t.placed)
                {
                    ++st.lit;
                    sl::Light l;
                    std::memcpy(l.pos, t.pos, sizeof(l.pos));
                    sl::FadedColour(t.fade, l.color);
                    for (float& c : l.color) c *= g_settings.brightness;
                    l.radius = t.radius;
                    l.model = t.model;
                    l.kind = t.kind;
                    l.age = t.age;
                    l.presence = t.presence;
                    l.kept = t.kept;
                    l.label = t.label;
                    g_models.push_back(l);
                    g_owners.push_back(&t);
                }
                else { t.presence = 0.0f; t.kept = false; t.label = nullptr; }
                ++it;
            }
            st.tracked = static_cast<unsigned>(g_tracked.size());
            st.missiles = byKind[static_cast<int>(sl::Kind::Missile)];
            st.effects = byKind[static_cast<int>(sl::Kind::Effect)];
            st.enchants = byKind[static_cast<int>(sl::Kind::Enchant)];

            const double tModels = grassperf::Now();
            st.msModels = tModels - tTextures;
            sl::MergeStats ms;
            sl::CellRoom room;
            room.cellSize = lights::GridCellSize();
            room.perCell = lights::SpellSlotsPerCell();
            sl::Select(g_models, g_frameLights, eye, g_settings, room, dt, ms);
            for (size_t k = 0; k < g_models.size(); ++k)
            {
                g_owners[k]->presence = g_models[k].presence;
                g_owners[k]->kept = g_models[k].kept;
                g_owners[k]->label = g_models[k].label;
            }
            st.msMerge = grassperf::Now() - tModels;
            st.inRange = ms.inRange; st.merged = ms.merged; st.clusters = ms.clusters; st.dropped = ms.dropped; st.drawn = ms.out;
            st.leaving = ms.leaving; st.waiting = ms.waiting; st.overflow = ms.overflow; st.cellRoom = static_cast<unsigned>(room.perCell);

            g_drawn.clear();
            unsigned tests = 0;
            for (const sl::Light& l : g_frameLights)
            {
                lights::FrameLight fl{};
                std::memcpy(fl.pos, l.pos, sizeof(fl.pos));
                std::memcpy(fl.color, l.color, sizeof(fl.color));
                fl.radius = l.radius;
                fl.model = l.model;
                fl.indoor = IndoorAt(l.pos, tests) ? 1 : 0;
                out.push_back(fl);
                Drawn d;
                auto it = g_tracked.find(l.model);
                const File* f = it != g_tracked.end() && it->second.fileGeneration == g_filesGeneration ? it->second.file : nullptr;
                d.path = f ? f->path : "?";
                d.kind = l.kind;
                std::memcpy(d.pos, l.pos, sizeof(d.pos));
                std::memcpy(d.color, l.color, sizeof(d.color));
                d.radius = l.radius;
                d.sources = l.sources;
                d.presence = l.presence;
                d.kept = l.kept;
                const float dx = l.pos[0] - eye[0], dy = l.pos[1] - eye[1], dz = l.pos[2] - eye[2];
                d.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                g_drawn.push_back(std::move(d));
            }

            unsigned known = 0, waiting = 0;
            for (const Texture& t : g_textures) (t.state == 0 ? waiting : known)++;
            st.textures = known;
            st.texturesWaiting = waiting;
            st.files = static_cast<unsigned>(g_files.size());

            st.ms = grassperf::Now() - t0;
            const DWORD now = GetTickCount();
            if (now - g_windowStart >= 1000)
            {
                st.msAverage = g_windowFrames ? g_windowSum / g_windowFrames : st.ms;
                st.msMost = g_windowMost;
                g_windowSum = g_windowMost = 0.0;
                g_windowFrames = 0;
                g_windowStart = now;
            }
            g_windowSum += st.ms;
            g_windowMost = std::max(g_windowMost, st.ms);
            ++g_windowFrames;
        }

        const uint8_t* CodeInProcess(uintptr_t va, size_t size, void* user)
        {
            auto* buf = static_cast<uint8_t*>(user);
            return size <= 64 && ReadGuarded(reinterpret_cast<const void*>(va), buf, size) ? buf : nullptr;
        }
    }

    sl::Settings& Config() { return g_settings; }
    const Stats& GetStats() { return g_stats; }
    const std::vector<Drawn>& DrawnLights() { return g_drawn; }

    const Guard* Guards(size_t& count)
    {
        count = sizeof(kGuards) / sizeof(kGuards[0]);
        return kGuards;
    }

    bool GuardsHold(const uint8_t* (*codeAt)(uintptr_t va, size_t size, void* user), void* user, std::string& why)
    {
        for (const Guard& g : kGuards)
        {
            const uint8_t* code = codeAt ? codeAt(g.va, g.size, user) : nullptr;
            if (!code || std::memcmp(code, g.bytes, g.size) != 0)
            {
                char line[256];
                std::snprintf(line, sizeof(line), "the client's code at 0x%08X isn't as expected (%s)", static_cast<unsigned>(g.va), g.what);
                why = line;
                return false;
            }
        }
        why.clear();
        return true;
    }

    void Reset()
    {
        g_tracked.clear();
        g_files.clear();
        ++g_filesGeneration;
        g_headCount = 0;
        g_drawn.clear();
        g_models.clear();
        g_owners.clear();
        g_frameLights.clear();
        g_indoorCells.clear();
        g_lastMs = 0.0;
        g_offered = 0;
        Stats keep = g_stats;
        g_stats = Stats{};
        g_stats.spellRows = keep.spellRows;
        g_stats.itemRows = keep.itemRows;
        g_stats.objectRows = keep.objectRows;
        g_stats.clientEffects = keep.clientEffects;
        g_stats.objectModels = keep.objectModels;
        g_stats.why = keep.why;
    }

    bool OfferFromScan(const void* model)
    {
        if (!g_settings.enabled || g_guards < 0 || !model) return false;
        EnsureSets();
        ++g_offered;
        return Register(static_cast<const uint8_t*>(model), false, true);
    }

    void Update(float dt, const world::Snapshot& snap, const float eye[3], std::vector<lights::FrameLight>& out)
    {
        if (!g_settings.enabled || !snap.inWorld)
        {
            if (!g_tracked.empty() || !g_drawn.empty()) Reset();
            g_stats.active = false;
            g_stats.why = g_settings.enabled ? "not in the world" : "off";
            g_stats.ms = 0.0;
            return;
        }
        if (g_guards == 0)
        {
            uint8_t buf[64];
            g_guards = GuardsHold(&CodeInProcess, buf, g_guardWhy) ? 1 : -1;
        }
        if (g_guards < 0)
        {
            if (!g_tracked.empty()) Reset();
            g_stats.active = false;
            g_stats.why = g_guardWhy.c_str();
            return;
        }
        prof::Scope scope(prof::kOursSpellLights);
        ClientView view;
        ReadGuarded(reinterpret_cast<const void*>(kScene), &view.scene, sizeof(view.scene));
        ReadGuarded(reinterpret_cast<const void*>(kMissilesInFlight), &view.missiles[0], sizeof(view.missiles[0]));
        ReadGuarded(reinterpret_cast<const void*>(kMissilesImpact), &view.missiles[1], sizeof(view.missiles[1]));
        if (!view.scene)
        {
            g_stats.active = false;
            g_stats.why = "no M2 scene";
            return;
        }
        g_stats.active = true;
        g_stats.why = "on";
        // Wall time since the last frame (the fades and the smoothing go by it), the frame's dt if there's none yet.
        const double nowMs = grassperf::Now();
        const double wall = g_lastMs > 0.0 ? (nowMs - g_lastMs) / 1000.0 : dt;
        g_lastMs = nowMs;
        Frame(view, std::isfinite(wall) && wall > 0.0 && wall < 1.0 ? static_cast<float>(wall) : dt, eye, out);
    }

    void SetFileReader(FileReader read) { g_read = read; g_setsRead = false; g_sets = sl::VisualSets{}; }
    void SetIndoorTest(bool (*isIndoor)(const float pos[3])) { g_indoor = isIndoor; g_indoorCells.clear(); }

    void FrameForTests(const ClientView& view, float dt, const float eye[3], std::vector<lights::FrameLight>& out)
    {
        g_stats.active = true;
        g_stats.why = "tests";
        Frame(view, dt, eye, out);
    }

    const char* StatusLine()
    {
        const Stats& s = g_stats;
        char line[1200];
        if (!s.active)
            std::snprintf(line, sizeof(line), "spell lights: %s%s", g_settings.enabled ? "stopped: " : "", s.why);
        else
            std::snprintf(line, sizeof(line),
                          "spell lights: on; %u followed (%u missiles, %u effects, %u enchants), %u drawn by the client (the nearest %u looked at), %u lit (%u gone, fading); "
                          "this frame %u in range, %u merged into %u, %u over the cap, %u drawn (%u of them fading out of the kept set), %u waiting for room in the grid (%u a cell), %u dropped for want of it; "
                          "missiles %u in flight, %u after their hit; "
                          "the scene's head: %u walked, %u new; %u offered by the light scan; files %u (%u loading), glow colours %u (%u waiting, %u read now); "
                          "SpellVisualEffectName %d rows (%u files of the client's own effects left out), ItemVisualEffects %d, GameObjectDisplayInfo %d (%u game objects' models left out unless they hang on another); "
                          "cost %.3f ms (%.3f on average, %.3f at most over the last second; "
                          "this frame: finding %.3f, glow colours %.3f, the models %.3f, merge and cap %.3f)",
                          s.tracked, s.missiles, s.effects, s.enchants, s.shown, s.looked, s.lit, s.fading, s.inRange, s.merged, s.clusters, s.dropped, s.drawn,
                          s.leaving, s.waiting, s.cellRoom, s.overflow,
                          s.inFlight, s.impact, s.headWalked, s.headNew, s.fromScan, s.files, s.filesWaiting, s.textures, s.texturesWaiting,
                          s.texturesRead, s.spellRows, s.clientEffects, s.itemRows, s.objectRows, s.objectModels, s.ms, s.msAverage, s.msMost, s.msFind, s.msTextures,
                          s.msModels, s.msMerge);
        g_status = line;
        return g_status.c_str();
    }

    void SnapshotLines(std::vector<std::string>& out)
    {
        const sl::Settings& c = g_settings;
        char line[600];
        std::snprintf(line, sizeof(line), "spell lights settings: %s; brightness %.2f, reach %.1f yd for a 1-yd model, at most %d a frame, range %.0f yd, merge closer than %.1f yd, enchants %d (x%.2f)",
                      c.enabled ? "on" : "off", c.brightness, c.radius, c.maxLights, c.range, c.mergeDistance, c.enchants, c.enchantStrength);
        out.emplace_back(line);
        out.emplace_back(StatusLine());
        int n = 0;
        for (const Drawn& d : g_drawn)
        {
            if (n++ >= 8) break;
            std::snprintf(line, sizeof(line), "  %s %s, %.1f yd away at %.1f %.1f %.1f: colour %.2f %.2f %.2f, radius %.1f yd%s%s", sl::KindName(d.kind), d.path.c_str(),
                          d.distance, d.pos[0], d.pos[1], d.pos[2], d.color[0], d.color[1], d.color[2], d.radius,
                          d.sources > 1 ? (" (" + std::to_string(d.sources) + " merged)").c_str() : "", d.kept ? "" : ", fading out of the kept set");
            out.emplace_back(line);
        }
    }

    void DrawPanelSection(const WXL_Api* api)
    {
        if (!api->UiCollapsingHeader("Spell lights: spells, projectiles and enchants light what's round them (experimental)##spelllights")) return;
        sl::Settings& s = g_settings;
        api->UiCheckbox("spell lights (experimental): missiles, spell effects and enchants give a point light##sl", &s.enabled);
        api->UiSliderFloat("spell lights: brightness##slb", &s.brightness, 0.0f, 3.0f);
        api->UiSliderFloat("spell lights: reach for a model of 1 yd (yd; bigger effects reach further)##slr", &s.radius, 2.0f, 16.0f);
        api->UiSliderInt("spell lights: at most this many a frame (the nearest and brightest)##slm", &s.maxLights, 0, sl::kMaxLightsCap);
        api->UiSliderFloat("spell lights: range from the camera (yd)##slg", &s.range, 20.0f, 200.0f);
        api->UiSliderFloat("spell lights: merge closer than (yd; sqrt of their summed light)##sld", &s.mergeDistance, 0.0f, 8.0f);
        api->UiCheckbox("spell lights: weapon enchants glow too##sle", &s.enchants);
        api->UiSliderFloat("spell lights: enchants' strength##sles", &s.enchantStrength, 0.0f, 1.5f);
        ui::Text(api, StatusLine());
        if (!lights::Config().enabled) ui::Text(api, "(our point lights are switched off above: spell lights need them)");
        int n = 0;
        char line[512];
        for (const Drawn& d : g_drawn)
        {
            if (n++ >= 10) break;
            std::snprintf(line, sizeof(line), "  %s %s, %.1f yd: colour %.2f %.2f %.2f, radius %.1f yd%s%s", sl::KindName(d.kind), d.path.c_str(), d.distance,
                          d.color[0], d.color[1], d.color[2], d.radius, d.sources > 1 ? (" (" + std::to_string(d.sources) + " merged)").c_str() : "",
                          d.kept ? "" : " (leaving)");
            ui::Text(api, line);
        }
    }
}
