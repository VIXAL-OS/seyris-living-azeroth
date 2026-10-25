#include "Storm.hpp"
#include "../debug/MemoryTag.hpp"

#include "Climate.hpp"
#include "Fog.hpp"
#include "Lights.hpp"
#include "StormModel.hpp"
#include "TerrainHeight.hpp"
#include "ThunderDsp.hpp"
#include "Wind.hpp"
#include "../features/TerrainWetness.hpp"

#include "game/Script.hpp"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace wxl_livingazeroth::storm
{
    namespace
    {
        constexpr const char* kTag = "wxl-seyris-living-azeroth";
        constexpr uint32_t    kClientTimestamp = 0x4C2452FE; // 3.3.5a build 12340
        constexpr float       kTwoPi = 6.28318531f;

        // A strike's point light: this far around it (yd), hung this high over the ground, this bright at the
        // flash's peak (x the setting; a torch is about 1).
        constexpr float kStrikeLightRadius = 60.0f;
        constexpr float kStrikeLightHeight = 8.0f;
        constexpr float kStrikeLightGain = 3.0f;
        // A scorch mark: strikes nearer than this leave one (yd), this wide (yd), this dark at its centre at first.
        constexpr float kScorchRange = 200.0f;
        constexpr float kScorchRadius = 2.5f;
        constexpr float kScorchDark = 0.7f;
        // The bolt model is kept loaded from medium rain on, and let go this long after the storm passed (s).
        constexpr float kWarmLinger = 60.0f;

        const WXL_Api* g_api = nullptr;
        Settings  g_settings;
        Status    g_status;
        Rng       g_rng;
        Scheduler g_scheduler;
        float     g_clock = 0.0f;
        float     g_storm = 0.0f;     // storminess, smoothed
        float     g_outdoors = 1.0f;  // smoothed: indoors, a flash doesn't light the scene around you
        std::vector<Flash> g_flashes;

        // A layer of thunder on its way: when it arrives, which kit, its gain (x the kit's volume), where the strike
        // is from the player (a unit vector in world x, y), how far (yd: its distance filters, env/ThunderDsp) and
        // whether it's filtered (-1 as the setting says, 0 no, 1 yes).
        struct PendingSound { float due; Kit kit; float gain; float dir[2]; float distance; int filters; };
        std::vector<PendingSound> g_sounds;

        // --- thunder through the client's sound kits ---
        using ResetSoundPropsFn = void(__fastcall*)(void* props, void* edx);
        using PlaySoundKitFn = int(__cdecl*)(int32_t kitId, const float* position, void* object, void* props, uint32_t advancedId,
                                             uint32_t* soundId, int32_t checkDistance, int32_t keepPlaying);
        using ListenerPositionFn = bool(__cdecl*)(float* position);
        bool  g_soundNative = false, g_soundBroken = false;
        char  g_soundStatus[200] = "not checked";
        bool  g_loggedNative = false, g_loggedLua = false;
        int   g_loggedProblem = 0x7FFFFFFF; // the last kit result logged as a problem (each new one once)

        // --- the day-night bands ---
        using BandsFn = void(__cdecl*)();
        BandsFn    g_origBands = nullptr;
        bool       g_bandHooked = false, g_bandBroken = false;
        char       g_bandStatus[200] = "not installed";
        Shade      g_shade;       // written by Update, read by the hook (both on the main thread)
        BandMemory g_bandMemory;
        // Snow: its client reads and the fog distance (kSnowGuards). g_shade.snow is 1 while it snows (Update); the hook
        // weighs it by the client's own share of its weather set this frame.
        bool       g_snowHooked = false;
        char       g_snowStatus[200] = "not installed";
        float      g_snowDistance = 0.0f; // yd, 0 = the client's own (Update: SnowFogDistance while it snows)
        FogDistanceMemory g_fogMemory;

        // --- bolts ---
        using CreateModelFn = void*(__fastcall*)(void* scene, void* edx, const char* path, uint32_t flags);
        using SetAnimatingFn = void(__fastcall*)(void* model, void* edx, int on);
        using ReleaseFn = void(__fastcall*)(void* model, void* edx);
        using SetBoneSequenceFn = void(__fastcall*)(void* model, void* edx, uint32_t slot, uint32_t seqId, uint32_t prevSeqId,
                                                    uint32_t prevSubSeqId, float blendTime, uint32_t loop, uint32_t primary);
        bool  g_boltsAvailable = false, g_boltsBroken = false;
        char  g_boltStatus[200] = "not checked";
        struct Bolt { void* model; float born; };
        std::vector<Bolt> g_bolts;
        void* g_warm = nullptr;   // a bolt model never shown, holding the file loaded while a storm may need it
        float g_warmIdle = 0.0f;

        // --- the scorch mark ---
        struct Scorch { bool active = false; float x = 0.0f, y = 0.0f, born = 0.0f; };
        Scorch g_scorch;

        float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

        // --- client checks ---
        bool ImageIs12340Unsafe()
        {
            const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
            if (reinterpret_cast<uintptr_t>(base) != 0x00400000u) return false;
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 0x1000) return false;
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.TimeDateStamp == kClientTimestamp;
        }

        // The first guard that differs. A function entry starting with a jump instead of its prologue is another
        // party's detour (the core's or an extension's): its own bytes can't be checked there, the rest still are.
        template <size_t N> const Guard* MismatchUnsafe(const Guard (&guards)[N], unsigned& detoured)
        {
            for (const Guard& g : guards)
            {
                const auto* at = reinterpret_cast<const uint8_t*>(g.address);
                const bool entry = g.bytes[0] == 0x55 || g.bytes[0] == 0x56;
                if (entry && at[0] == 0xE9) { ++detoured; continue; }
                if (std::memcmp(at, g.bytes, g.size) != 0) return &g;
            }
            return nullptr;
        }

        // 0 fine, 1 not the 12340 image, 2 a guard differs (`bad`), 3 a fault while reading.
        template <size_t N> int Verify(const Guard (&guards)[N], const Guard*& bad, unsigned& detoured)
        {
            __try
            {
                if (!ImageIs12340Unsafe()) return 1;
                bad = MismatchUnsafe(guards, detoured);
                return bad ? 2 : 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 3; }
        }

        // --- the band hook ---
        bool ShadeLive()
        {
            __try
            {
                Shade shade = g_shade;
                // Snow: as much as the client shows its weather set this frame, and not with the camera in a liquid.
                float snow = 0.0f;
                if (g_snowHooked && shade.snow > 0.0f && *reinterpret_cast<const uint32_t*>(kCameraInLiquid) == 0)
                {
                    const float blend = *reinterpret_cast<const float*>(kStormBlend);
                    snow = blend > 0.0f ? (blend < 1.0f ? blend : 1.0f) * shade.snow : 0.0f;
                }
                shade.snow = snow;
                ShadeBands(reinterpret_cast<uint32_t*>(kBands), shade, g_bandMemory);
                g_status.snow = snow;
                if (g_snowHooked)
                {
                    float& end = *reinterpret_cast<float*>(kFogEnd);
                    float& start = *reinterpret_cast<float*>(kFogStartMul);
                    g_status.fogEndIn = end;
                    g_status.fogStartIn = start;
                    LiftFogDistance(end, start, snow, g_snowDistance, g_fogMemory);
                    if (g_fogMemory.valid) { g_status.fogEndIn = g_fogMemory.in.end; g_status.fogStartIn = g_fogMemory.in.startMul; }
                    g_status.fogEndOut = end;
                    g_status.fogStartOut = start;
                }
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // Pre-hook of the band copy: the client has just resolved this frame's day-night colours; the storm's
        // shade goes on them before the light block, the sky and the fog take them.
        void __cdecl hkBands()
        {
            memuse::Scope memory(memuse::kEnv);
            if (!g_bandBroken)
            {
                const bool active = !Identity(g_shade);
                if (!ShadeLive())
                {
                    g_bandBroken = true;
                    std::snprintf(g_bandStatus, sizeof(g_bandStatus), "stopped: a fault while shading the day-night colours");
                    if (g_api) g_api->Log(WXL_LOG_WARN, kTag, "storm: a fault while shading the client's day-night colours; no darker sky or flashes from now on.");
                }
                else if (active) ++g_status.bandFrames;
                g_status.fogIn = g_bandMemory.valid ? g_bandMemory.in[kFogBand] : 0;
                g_status.fogOut = g_bandMemory.valid ? g_bandMemory.out[kFogBand] : 0;
            }
            g_origBands();
        }

        // --- bolts ---
        void* CreateUnsafe(const float pos[3], float yaw, float scale)
        {
            void* scene = *reinterpret_cast<void* const*>(kWorldScene);
            if (!scene) return nullptr;
            void* model = reinterpret_cast<CreateModelFn>(kCreateModel)(scene, nullptr, kBoltModel, 0);
            if (!model) return nullptr;
            float m[16];
            BoltMatrix(pos, yaw, scale, m);
            std::memcpy(static_cast<uint8_t*>(model) + kModelMatrix, m, sizeof(m));
            // Its one sequence from the start, as the doodad spawn does right after creating one.
            reinterpret_cast<SetBoneSequenceFn>(kSetBoneSequence)(model, nullptr, 0xFFFFFFFFu, 0, 0xFFFFFFFFu, 0, 1.0f, 1, 1);
            return model;
        }
        bool Create(const float pos[3], float yaw, float scale, void*& model)
        {
            model = nullptr;
            __try { model = CreateUnsafe(pos, yaw, scale); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { model = nullptr; return false; }
        }
        bool SetAnimating(void* model, bool on)
        {
            __try { reinterpret_cast<SetAnimatingFn>(kSetAnimating)(model, nullptr, on ? 1 : 0); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool Linked(void* model)
        {
            __try { return *reinterpret_cast<void* const*>(static_cast<uint8_t*>(model) + kModelAnimLink) != nullptr; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool Release(void* model)
        {
            __try { reinterpret_cast<ReleaseFn>(kReleaseModel)(model, nullptr); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        void BoltsBroke(const char* while_)
        {
            g_boltsBroken = true;
            std::snprintf(g_boltStatus, sizeof(g_boltStatus), "stopped: a fault while %s a bolt model", while_);
            if (g_api) g_api->Log(WXL_LOG_WARN, kTag, "storm: a fault while %s a bolt model; no bolts from now on.", while_);
        }

        bool BoltsUsable() { return g_boltsAvailable && !g_boltsBroken; }

        bool SpawnBolt(const float pos[3])
        {
            if (!BoltsUsable()) return false;
            void* model = nullptr;
            const float scale = g_settings.boltSize > 0.1f ? g_settings.boltSize : 0.1f;
            if (!Create(pos, g_rng.Range(0.0f, kTwoPi), scale, model)) { BoltsBroke("creating"); return false; }
            if (!model) return false;
            g_bolts.push_back({ model, g_clock });
            ++g_status.bolts;
            return true;
        }

        void ReleaseAll()
        {
            for (const Bolt& b : g_bolts)
                if (!g_boltsBroken && !Release(b.model)) BoltsBroke("releasing");
            g_bolts.clear();
            if (g_warm && !g_boltsBroken && !Release(g_warm)) BoltsBroke("releasing");
            g_warm = nullptr;
        }

        // Each frame: live bolts back on the animate list (the scene empties it every frame); spent ones let go
        // once the scene has taken them off it (the destructor unlinks them anyway, if a frame never drew).
        void UpdateBolts(Phase phase, const world::Snapshot& snap, float dt)
        {
            for (size_t i = 0; i < g_bolts.size();)
            {
                Bolt& b = g_bolts[i];
                const float age = g_clock - b.born;
                if (!g_boltsBroken && age < kBoltLife)
                {
                    if (!SetAnimating(b.model, true)) BoltsBroke("showing");
                    ++i;
                    continue;
                }
                if (!g_boltsBroken && Linked(b.model) && age < kBoltLife + 2.0f) { ++i; continue; }
                if (!g_boltsBroken && !Release(b.model)) BoltsBroke("releasing");
                g_bolts[i] = g_bolts.back();
                g_bolts.pop_back();
            }
            // The model file stays loaded from medium rain on, so the first strike's bolt shows at once.
            const bool want = BoltsUsable() && g_settings.enabled && g_settings.bolts && g_settings.thunder && snap.inWorld &&
                              static_cast<int>(phase) >= static_cast<int>(Phase::Medium);
            g_warmIdle = want ? 0.0f : g_warmIdle + dt;
            if (want && !g_warm)
            {
                // Never put on the animate list: never drawn.
                const float below[3] = { snap.playerPos[0], snap.playerPos[1], snap.playerPos[2] - 5000.0f };
                void* model = nullptr;
                if (!Create(below, 0.0f, 0.01f, model)) BoltsBroke("loading");
                else g_warm = model;
            }
            else if (g_warm && (!BoltsUsable() || !g_settings.bolts || g_warmIdle > kWarmLinger))
            {
                if (!g_boltsBroken && !Release(g_warm)) BoltsBroke("releasing");
                g_warm = nullptr;
            }
        }

        // --- thunder ---
        bool PlayLua(const char* name)
        {
            if (!name || !wxl::game::script::Context()) return false;
            char source[96];
            std::snprintf(source, sizeof(source), "PlaySound(\"%s\")", name);
            wxl::game::script::Execute(source, "LivingAzerothStorm");
            return true;
        }

        // The kit through the client's own call, as its object sounds do: properties at their defaults with the
        // volume scale set, positional at `pos` (or not, null), and through a chain of env/ThunderDsp's when `chain`
        // names one (the properties' DSP chain, as the client gives Death Knight voices theirs). Returns PlaySoundKit's
        // result.
        int PlayKitUnsafe(int32_t kitId, const float* pos, float scale, const char* chain)
        {
            alignas(16) uint8_t props[0x100] = {};
            static_assert(sizeof(props) >= kSoundPropsSize, "room for SE2SoundKitProperties");
            static_assert(thunderdsp::kPropsChainName + thunderdsp::kChainNameSize <= kSoundPropsSize, "the chain name inside them");
            reinterpret_cast<ResetSoundPropsFn>(kResetSoundProps)(props, nullptr);
            std::memcpy(props + kSoundPropsVolumeScale, &scale, sizeof(scale));
            if (chain && chain[0])
            {
                const int32_t on = 1;
                std::memcpy(props + thunderdsp::kPropsUseChain, &on, sizeof(on));
                std::strncpy(reinterpret_cast<char*>(props + thunderdsp::kPropsChainName), chain, thunderdsp::kChainNameSize - 1);
            }
            return reinterpret_cast<PlaySoundKitFn>(kPlaySoundKit)(kitId, pos, nullptr, props, 0, nullptr, 1, 0);
        }
        bool PlayKit(int32_t kitId, const float* pos, float scale, const char* chain, int& result)
        {
            __try { result = PlayKitUnsafe(kitId, pos, scale, chain); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        bool ListenerUnsafe(float out[3]) { return reinterpret_cast<ListenerPositionFn>(kListenerPosition)(out); }
        bool Listener(float out[3])
        {
            __try { return ListenerUnsafe(out); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // One layer of thunder now: through the client's sound kits at its volume and from the strike's direction (a
        // point PlacementDistance from the listener, inside the kit's MinDistance, so the client's rolloff leaves the
        // volume alone) and through its distance filters (env/ThunderDsp; `filters` as PendingSound's), or through Lua
        // PlaySound with the row nearest that volume.
        void PlayLayer(Kit kit, float gain, const float dir[2], float distance, int filters)
        {
            const KitInfo& k = Info(kit);
            // The kit's volume x this scale is what it plays at: LayerVolume, at most 1.
            const float scale = LayerVolume(kit, gain, g_settings.soundVolume) / k.volume;
            if (!(scale > 0.0f)) return;
            const char* why = !g_settings.nativeSound ? "switched off in the panel or WarcraftXL.ini" : g_soundStatus;
            if (g_settings.nativeSound && g_soundNative && !g_soundBroken)
            {
                float listener[3] = {};
                const float place = PlacementDistance(kit);
                float at[3] = {};
                const bool positional = place > 0.0f && Listener(listener);
                if (positional)
                {
                    at[0] = listener[0] + dir[0] * place;
                    at[1] = listener[1] + dir[1] * place;
                    at[2] = listener[2] + 0.25f * place; // a little from above
                }
                int result = -1;
                char chain[thunderdsp::kChainNameSize] = {};
                const bool wantFilters = filters < 0 ? thunderdsp::Config().filters != 0 : filters != 0;
                const thunderdsp::Use use = thunderdsp::Prepare(kit == Kit::Rumble ? thunderdsp::Layer::Rumble : thunderdsp::Layer::Crack,
                                                                distance, wantFilters, chain, sizeof(chain));
                const bool called = PlayKit(k.soundKitId, positional ? at : nullptr, scale, use.filtered ? chain : nullptr, result);
                thunderdsp::Played(use, called && result == 0);
                if (!called)
                {
                    g_soundBroken = true;
                    std::snprintf(g_soundStatus, sizeof(g_soundStatus), "Lua PlaySound: a fault in the client's sound kit call; stopped using it");
                    if (g_api) g_api->Log(WXL_LOG_WARN, kTag, "storm: a fault while playing thunder through the client's sound kits; Lua PlaySound from now on.");
                }
                else
                {
                    g_status.lastKitResult = result;
                    const SoundOutcome outcome = Outcome(result);
                    if (outcome == SoundOutcome::Played)
                    {
                        ++g_status.nativeSounds;
                        ++g_status.sounds;
                        if (!g_loggedNative && g_api)
                        {
                            g_loggedNative = true;
                            g_api->Log(WXL_LOG_INFO, kTag, "storm: thunder plays through the client's sound kits (%s, %s at %.2f).",
                                       positional ? "positional" : "not positional", k.name, scale * k.volume);
                        }
                        return;
                    }
                    if (outcome == SoundOutcome::Declined) { ++g_status.declinedSounds; return; } // Lua would be declined too
                    if (g_api && result != g_loggedProblem)
                    {
                        g_loggedProblem = result;
                        g_api->Log(WXL_LOG_WARN, kTag, "storm: the client's sound kit %d (%s) didn't play (result %d); its Lua row instead.",
                                   k.soundKitId, k.name, result);
                    }
                    why = "the kit didn't play (see the log)";
                }
            }
            const char* row = LuaRowFor(kit, scale * k.volume);
            if (!row) { ++g_status.droppedSounds; return; }
            if (!PlayLua(row)) return;
            ++g_status.luaSounds;
            ++g_status.sounds;
            if (!g_loggedLua && g_api)
            {
                g_loggedLua = true;
                g_api->Log(WXL_LOG_INFO, kTag, "storm: thunder plays through Lua PlaySound (%s).", why);
            }
        }

        // --- strikes ---
        void Strike(const world::Snapshot& snap, float distance)
        {
            const Settings& s = g_settings;
            const float d = distance > 0.0f ? distance : StrikeDistance(g_rng);
            const float bearing = g_rng.Range(0.0f, kTwoPi);
            float pos[3] = { snap.playerPos[0] + std::cos(bearing) * d, snap.playerPos[1] + std::sin(bearing) * d, snap.playerPos[2] };
            float ground = 0.0f;
            const bool known = terrain::HeightAt(pos[0], pos[1], ground);
            if (known) pos[2] = ground;
            // A bolt shows its stroke kBoltFlashDelay into its sequence: the flash, its light and its sound wait for it.
            const float delay = s.bolts && known && d <= s.boltRange && SpawnBolt(pos) ? kBoltFlashDelay : 0.0f;
            Flash f = MakeStrike(g_rng, d, g_clock + delay);
            std::memcpy(f.pos, pos, sizeof(f.pos));
            g_flashes.push_back(f);
            if (s.sounds)
            {
                const ThunderPlan plan = PlanThunder(d, s.soundDistance, g_rng.Uniform(), s.rumble != 0);
                for (int i = 0; i < plan.count; ++i)
                    g_sounds.push_back({ g_clock + delay + ThunderDelay(d) + plan.layer[i].after, plan.layer[i].kit, plan.layer[i].gain,
                                         { std::cos(bearing), std::sin(bearing) }, d, -1 });
            }
            if (s.scorch && known && d <= kScorchRange)
            {
                float water = 0.0f;
                const bool wet = terrain::LiquidHeightAt(pos[0], pos[1], water) && water > ground - 0.1f;
                if (!wet) g_scorch = Scorch{ true, pos[0], pos[1], g_clock + delay };
            }
            ++g_status.strikes;
            g_status.lastStrikeDistance = d;
        }

        void UpdateScorch()
        {
            float strength = 0.0f;
            if (g_scorch.active)
            {
                const float life = g_settings.scorchSeconds > 5.0f ? g_settings.scorchSeconds : 5.0f;
                const float age = g_clock - g_scorch.born;
                if (!g_settings.scorch || !g_settings.enabled || age >= life) g_scorch.active = false;
                else if (age >= 0.0f) strength = kScorchDark * std::pow(1.0f - age / life, 1.5f);
                g_status.scorchLeft = g_scorch.active ? life - age : 0.0f;
            }
            else g_status.scorchLeft = 0.0f;
            terrainwet::SetScorch(g_scorch.x, g_scorch.y, kScorchRadius, strength);
        }
    }

    Settings& Config() { return g_settings; }
    const Status& GetStatus() { return g_status; }
    const char* BandStatus() { return g_bandStatus; }
    const char* SnowStatus() { return g_snowStatus; }
    const char* BoltStatus() { return g_boltStatus; }
    const char* SoundStatus()
    {
        if (!g_settings.nativeSound) return "Lua PlaySound (the client's sound kits are switched off here)";
        return g_soundStatus;
    }

    void Install(const WXL_Api* api)
    {
        g_api = api;
        g_rng.state ^= static_cast<uint32_t>(GetTickCount());

        const Guard* bad = nullptr;
        unsigned detoured = 0;
        switch (Verify(kBandGuards, bad, detoured))
        {
            case 0:
                if (api->HookAttach("LivingAzeroth.StormBands", kBandsToLight, reinterpret_cast<void*>(&hkBands),
                                    reinterpret_cast<void**>(&g_origBands), WXL_HOOK_DEFAULT_PRIORITY))
                {
                    g_bandHooked = true;
                    std::snprintf(g_bandStatus, sizeof(g_bandStatus), "on");
                }
                else std::snprintf(g_bandStatus, sizeof(g_bandStatus), "off: the hook on the band copy failed");
                break;
            case 1: std::snprintf(g_bandStatus, sizeof(g_bandStatus), "off: not the 3.3.5a 12340 client"); break;
            case 2: std::snprintf(g_bandStatus, sizeof(g_bandStatus), "off: the client's %s at 0x%08X differs", bad->what, static_cast<unsigned>(bad->address)); break;
            default: std::snprintf(g_bandStatus, sizeof(g_bandStatus), "off: the client's code could not be read"); break;
        }
        if (!g_bandHooked) api->Log(WXL_LOG_WARN, kTag, "storm: no darker sky or flashes (%s).", g_bandStatus);

        // Snow rides on the band hook: the weather set's share and the camera's liquid it reads, the fog distance it writes.
        bad = nullptr;
        detoured = 0;
        switch (g_bandHooked ? Verify(kSnowGuards, bad, detoured) : -1)
        {
            case 0: g_snowHooked = true; std::snprintf(g_snowStatus, sizeof(g_snowStatus), "on"); break;
            case 1: std::snprintf(g_snowStatus, sizeof(g_snowStatus), "off: not the 3.3.5a 12340 client"); break;
            case 2: std::snprintf(g_snowStatus, sizeof(g_snowStatus), "off: the client's %s at 0x%08X differs", bad->what, static_cast<unsigned>(bad->address)); break;
            case 3: std::snprintf(g_snowStatus, sizeof(g_snowStatus), "off: the client's code could not be read"); break;
            default: std::snprintf(g_snowStatus, sizeof(g_snowStatus), "off: no band hook"); break;
        }
        if (!g_snowHooked) api->Log(WXL_LOG_WARN, kTag, "storm: snow keeps the client's grey weather fog (%s).", g_snowStatus);

        bad = nullptr;
        detoured = 0;
        switch (Verify(kModelGuards, bad, detoured))
        {
            case 0:
                g_boltsAvailable = true;
                std::snprintf(g_boltStatus, sizeof(g_boltStatus), detoured ? "available (%u of its entries detoured by another party)" : "available", detoured);
                break;
            case 1: std::snprintf(g_boltStatus, sizeof(g_boltStatus), "off: not the 3.3.5a 12340 client"); break;
            case 2: std::snprintf(g_boltStatus, sizeof(g_boltStatus), "off: the client's %s at 0x%08X differs", bad->what, static_cast<unsigned>(bad->address)); break;
            default: std::snprintf(g_boltStatus, sizeof(g_boltStatus), "off: the client's code could not be read"); break;
        }
        bad = nullptr;
        detoured = 0;
        switch (Verify(kSoundGuards, bad, detoured))
        {
            case 0:
                g_soundNative = true;
                std::snprintf(g_soundStatus, sizeof(g_soundStatus), "the client's sound kits: a volume per distance, from the strike's direction%s",
                              detoured ? " (play call detoured by another party)" : "");
                break;
            case 1: std::snprintf(g_soundStatus, sizeof(g_soundStatus), "Lua PlaySound: not the 3.3.5a 12340 client"); break;
            case 2: std::snprintf(g_soundStatus, sizeof(g_soundStatus), "Lua PlaySound: the client's %s at 0x%08X differs", bad->what, static_cast<unsigned>(bad->address)); break;
            default: std::snprintf(g_soundStatus, sizeof(g_soundStatus), "Lua PlaySound: the client's sound code could not be read"); break;
        }
        api->Log(WXL_LOG_INFO, kTag, "storm: day-night colours %s; snow %s; bolt models %s; thunder through %s.", g_bandStatus, g_snowStatus,
                 g_boltStatus, g_soundStatus);
    }

    void Update(float dt, const world::Snapshot& snap)
    {
        if (!(dt >= 0.0f) || dt > 1.0f) dt = 1.0f / 60.0f;
        g_clock += dt;
        const Settings& s = g_settings;

        // The weather: the server's grade as the client has walked it, or the climate panel's override (whose
        // intensity is a grade too: "Show this weather in the client" sends it as one).
        const climate::Weather& w = climate::CurrentWeather();
        const bool overridden = climate::WeatherOverrideType() >= 0;
        const int type = w.type;
        const float grade = overridden ? w.intensity : (snap.weatherType == 1 ? snap.weatherRaw : 0.0f);
        const bool active = s.enabled && snap.inWorld;
        const Phase phase = active ? PhaseFor(type, grade) : Phase::None;
        const float target = active ? Storminess(type, grade) : 0.0f;
        g_storm += (target - g_storm) * (1.0f - std::exp(-dt / 8.0f));
        g_outdoors += ((snap.outdoors ? 1.0f : 0.0f) - g_outdoors) * (1.0f - std::exp(-dt / 0.75f));

        // Flashes and strikes.
        const bool want = phase == Phase::Heavy ? (s.thunder || s.flashes) : s.flashes != 0;
        if (g_scheduler.Step(dt, want ? phase : Phase::None, grade, s.frequency, g_rng))
        {
            if (phase == Phase::Heavy && s.thunder && g_rng.Uniform() < kStrikeShare) Strike(snap, -1.0f);
            else if (s.flashes) { g_flashes.push_back(MakeDistantFlash(g_rng, phase, g_clock)); ++g_status.flashes; }
        }

        // This frame's light: the brightest flash going, and the strikes' point lights.
        float sky = 0.0f, scene = 0.0f;
        const float lightReach = lights::Config().range + kStrikeLightRadius;
        for (size_t i = 0; i < g_flashes.size();)
        {
            const Flash& f = g_flashes[i];
            if (g_clock > EndTime(f) || !snap.inWorld) { g_flashes[i] = g_flashes.back(); g_flashes.pop_back(); continue; }
            const float e = Envelope(f, g_clock);
            sky = std::fmax(sky, e * f.sky);
            scene = std::fmax(scene, e * f.scene);
            if (f.strike && e > 0.01f && s.strikeLight > 0.0f && f.distance < lightReach)
            {
                const float k = e * kStrikeLightGain * s.strikeLight * s.flashStrength;
                const float color[3] = { g_shade.tint[0] * k, g_shade.tint[1] * k, g_shade.tint[2] * k };
                const float at[3] = { f.pos[0], f.pos[1], f.pos[2] + kStrikeLightHeight };
                lights::AddTransient(at, color, kStrikeLightRadius);
                ++g_status.lights;
            }
            ++i;
        }
        g_shade.dark = active ? Clamp01(s.darkness) * g_storm : 0.0f;
        g_shade.desat = 0.6f * g_shade.dark;
        // The fog's own storm: lighter and cooler than the sky, brighter by day (the sky's day, as the ground fog's).
        const climate::Clock& clock = climate::Now();
        const float day = clock.valid ? 1.0f - fog::SkyNight(clock.dayFraction) : climate::SunNow().daylight;
        g_shade.fog = active ? Clamp01(g_storm) : 0.0f;
        g_shade.fogBright = s.fogBrightness;
        g_shade.fogCool = s.fogCool;
        g_shade.day = std::isfinite(day) ? Clamp01(day) : 1.0f;
        // Falling snow, as the client shows it (its own weather, not the climate panel's): the weather set's greys lifted
        // and its fog kept back (the hook weighs this by the set's share this frame).
        const bool snowing = active && snap.weatherType == 2;
        g_shade.snow = snowing ? 1.0f : 0.0f;
        g_shade.snowBright = s.snowBrightness;
        g_shade.snowWhite = s.snowWhite;
        g_snowDistance = snowing && std::isfinite(s.snowDistance) && s.snowDistance > 0.0f ? s.snowDistance : 0.0f;
        g_shade.sky = sky * s.flashStrength;
        g_shade.scene = scene * s.flashStrength * g_outdoors;
        // Darker nights, storms on or off: the night's share (the sky's day) x the place's (a city, frost, indoors)
        const bool city = (snap.areaFlags & (kAreaCapital | kAreaSlaveCapital)) != 0;
        const float temperature = snap.inWorld ? climate::Temperature(climate::Here()).total : 20.0f;
        g_status.night = NightShade(snap.inWorld ? s.nightDarkness : 0.0f, g_shade.day, city, temperature, g_outdoors);
        g_shade.night = g_status.night.total;

        // The cracks and rumbles, after the sound's travel time (one a frame; dropped if long overdue: a loading screen).
        for (size_t i = 0; i < g_sounds.size(); ++i)
        {
            if (g_clock < g_sounds[i].due) continue;
            const PendingSound p = g_sounds[i];
            g_sounds.erase(g_sounds.begin() + static_cast<std::ptrdiff_t>(i));
            if (g_clock - p.due < 1.5f && snap.inWorld && s.sounds) PlayLayer(p.kit, p.gain, p.dir, p.distance, p.filters);
            break;
        }

        // The wind and the haze.
        const float storm = active ? g_storm : 0.0f;
        wind::SetStorm(storm * 1.0f * s.wind, storm * 0.3f * s.wind);
        fog::SetStormHaze(storm * s.haze);

        UpdateBolts(phase, snap, dt);
        UpdateScorch();

        float next = -1.0f;
        if (g_scheduler.wait >= 0.0f) next = g_scheduler.wait;
        g_status.phase = static_cast<int>(phase);
        g_status.grade = grade;
        g_status.rain = snap.inWorld ? RainLevel(type, grade) : 0.0f;
        g_status.overridden = overridden;
        g_status.storminess = g_storm;
        g_status.darkness = g_shade.dark;
        g_status.skyFlash = g_shade.sky;
        g_status.sceneFlash = g_shade.scene;
        g_status.day = g_shade.day;
        g_status.nextFlash = next;
        g_status.pendingSounds = static_cast<int>(g_sounds.size());
        g_status.liveBolts = static_cast<int>(g_bolts.size());
    }

    void OnWorldLeave()
    {
        ReleaseAll();
        g_flashes.clear();
        g_sounds.clear();
        g_scorch = Scorch{};
        terrainwet::SetScorch(0.0f, 0.0f, 0.0f, 0.0f);
        g_shade.sky = g_shade.scene = 0.0f;
        g_bandMemory.valid = false;
    }

    void TestFlash()
    {
        const Phase phase = static_cast<Phase>(g_status.phase);
        g_flashes.push_back(MakeDistantFlash(g_rng, phase == Phase::None ? Phase::Medium : phase, g_clock));
        ++g_status.flashes;
    }

    void TestStrike(float distance)
    {
        const world::Snapshot& snap = world::Current();
        if (!snap.inWorld) return;
        Strike(snap, distance);
    }

    void TestSound(float distance)
    {
        const float bearing = g_rng.Range(0.0f, kTwoPi);
        const ThunderPlan plan = PlanThunder(distance, 1.0e9f, g_rng.Uniform(), g_settings.rumble != 0);
        const float dir[2] = { std::cos(bearing), std::sin(bearing) };
        for (int i = 0; i < plan.count; ++i)
        {
            if (plan.layer[i].after <= 0.0f) PlayLayer(plan.layer[i].kit, plan.layer[i].gain, dir, distance, -1); // the crack: now
            else g_sounds.push_back({ g_clock + plan.layer[i].after, plan.layer[i].kit, plan.layer[i].gain, { dir[0], dir[1] }, distance, -1 });
        }
    }

    void TestKit(int kit, float distance, int filters)
    {
        if (kit < 0 || kit >= static_cast<int>(Kit::Count)) return;
        const float bearing = g_rng.Range(0.0f, kTwoPi);
        const float dir[2] = { std::cos(bearing), std::sin(bearing) };
        PlayLayer(static_cast<Kit>(kit), ThunderGain(distance, 1.0e9f), dir, distance, filters);
    }
}
