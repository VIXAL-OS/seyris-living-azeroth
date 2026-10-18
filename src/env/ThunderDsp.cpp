#include "ThunderDsp.hpp"

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace wxl_livingazeroth::thunderdsp
{
    namespace
    {
        constexpr const char* kTag = "wxl-seyris-living-azeroth";
        constexpr uint32_t    kClientTimestamp = 0x4C2452FE; // 3.3.5a build 12340
        constexpr int         kFault = -0x7FFF;              // an SEH-caught fault, not an FMOD_RESULT

        // FMOD keeps a copy of a channel group's name.
        constexpr const char* kGroupNames[kSlots] = {
            "LivingAzeroth thunder chain 0", "LivingAzeroth thunder chain 1", "LivingAzeroth thunder chain 2",
            "LivingAzeroth thunder chain 3", "LivingAzeroth thunder chain 4", "LivingAzeroth thunder chain 5",
        };
        static_assert(kSlots == 6, "a group name per chain");

        const WXL_Api* g_api = nullptr;
        Settings  g_settings;
        Status    g_status;
        Pool      g_pool;
        float     g_clock = 0.0f;
        bool      g_available = false;  // the guards held and both detours are in
        bool      g_broken = false;     // a fault: nothing of ours is called again
        DWORD     g_mainThread = 0;     // OnUpdate's thread
        uintptr_t g_system = 0;         // the FMOD system our chains were made in
        char      g_statusText[220] = "not checked";
        bool      g_loggedFirst = false, g_loggedBusy = false, g_loggedStale = false, g_loggedJoin = false, g_loggedMade = false;
        bool      g_loggedCreate = false, g_loggedConfigure = false, g_loggedEffectsOff = false;

        using AttachFn = void(__cdecl*)(void* channel, char* name);
        using ShutDownFn = void(__cdecl*)();
        AttachFn   g_origAttach = nullptr;
        ShutDownFn g_origShutDown = nullptr;

        using IsInitializedFn = int(__cdecl*)();
        using CreateDspFn = int(__cdecl*)(int type, void** dsp);
        using SetParamFn = int(__cdecl*)(void* dsp, int index, float value);
        using SwitchFn = int(__cdecl*)(void* dsp, int on);          // BypassDsp, EnableDSP (a bool, passed as a dword)
        using DestroyDspFn = int(__cdecl*)(void* dsp);
        using ConnectFn = int(__cdecl*)(void* dsp, void* group);
        using GroupChannelsFn = int(__cdecl*)(void* group);
        using CreateGroupFn = void*(__cdecl*)(const char* name);
        using DestroyGroupFn = int(__cdecl*)(void* group);
        using AddToGroupFn = int(__cdecl*)(void* channel, void* group);
        using SetFrequencyFn = int(__stdcall*)(void* channel, float frequency);
        using GetFrequencyFn = int(__stdcall*)(void* channel, float* frequency);

        // --- client checks (as env/Storm's) ---
        bool ImageIs12340Unsafe()
        {
            const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
            if (reinterpret_cast<uintptr_t>(base) != 0x00400000u) return false;
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 0x1000) return false;
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.TimeDateStamp == kClientTimestamp;
        }

        // The first guard that differs. A function entry starting with a jump instead of its prologue is another party's
        // detour: its own bytes can't be checked there (we chain behind it), the rest still are.
        template <size_t N> const storm::Guard* MismatchUnsafe(const storm::Guard (&guards)[N], unsigned& detoured)
        {
            for (const storm::Guard& g : guards)
            {
                const auto* at = reinterpret_cast<const uint8_t*>(g.address);
                const bool entry = g.bytes[0] == 0x55 || g.bytes[0] == 0x56;
                if (entry && at[0] == 0xE9) { ++detoured; continue; }
                if (std::memcmp(at, g.bytes, g.size) != 0) return &g;
            }
            return nullptr;
        }

        // 0 fine, 1 not the 12340 image, 2 a guard differs (`bad`), 3 a fault while reading.
        template <size_t N> int Verify(const storm::Guard (&guards)[N], const storm::Guard*& bad, unsigned& detoured)
        {
            __try
            {
                if (!ImageIs12340Unsafe()) return 1;
                bad = MismatchUnsafe(guards, detoured);
                return bad ? 2 : 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 3; }
        }

        // --- the client's calls, each alone in its SEH frame ---
        int SafeIsInitialized()
        {
            __try { return reinterpret_cast<IsInitializedFn>(kIsInitialized)(); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return kFault; }
        }
        // The client's Sound_EnableDSPEffects as its chain building reads it: 1 on, 0 off, -1 not looked up yet.
        int SafeDspEffects()
        {
            __try
            {
                if (!(*reinterpret_cast<const uint32_t*>(kDspEffectsLookedUp) & 1u)) return -1;
                const auto* cvar = *reinterpret_cast<const uint8_t* const*>(kDspEffectsCVar);
                if (!cvar) return -1;
                return *reinterpret_cast<const int32_t*>(cvar + kCVarIntValue) != 0 ? 1 : 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return kFault; }
        }
        bool SafeSystem(uintptr_t& system)
        {
            __try { system = *reinterpret_cast<const uintptr_t*>(kSoundSystem); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { system = 0; return false; }
        }
        void* SafeCreateGroup(const char* name, bool& fault)
        {
            fault = false;
            __try { return reinterpret_cast<CreateGroupFn>(kCreateGroup)(name); }
            __except (EXCEPTION_EXECUTE_HANDLER) { fault = true; return nullptr; }
        }
        int SafeCreateDsp(int type, void** dsp)
        {
            __try { return reinterpret_cast<CreateDspFn>(kCreateDspByType)(type, dsp); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return kFault; }
        }
        int SafeConnect(void* dsp, void* group)
        {
            __try { return reinterpret_cast<ConnectFn>(kConnectDsp)(dsp, group); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return kFault; }
        }
        int SafeSetParam(void* dsp, int index, float value)
        {
            __try { return reinterpret_cast<SetParamFn>(kSetDspParam)(dsp, index, value); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return kFault; }
        }
        int SafeBypass(void* dsp, bool bypass)
        {
            __try { return reinterpret_cast<SwitchFn>(kBypassDsp)(dsp, bypass ? 1 : 0); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return kFault; }
        }
        int SafeEnable(void* dsp, bool on)
        {
            __try { return reinterpret_cast<SwitchFn>(kEnableDsp)(dsp, on ? 1 : 0); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return kFault; }
        }
        int SafeDestroyDsp(void* dsp)
        {
            __try { return reinterpret_cast<DestroyDspFn>(kDestroyDsp)(dsp); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return kFault; }
        }
        int SafeDestroyGroup(void* group)
        {
            __try { return reinterpret_cast<DestroyGroupFn>(kDestroyGroup)(group); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return kFault; }
        }
        int SafeChannels(void* group)
        {
            __try { return reinterpret_cast<GroupChannelsFn>(kGroupChannels)(group); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return kFault; }
        }
        // The pitch as SESound::SetPitch sets it (the channel's frequency x the pitch), then the channel into the group as
        // DSP_AttachCustomDspChain puts it there. AddChannelToChannelGroup's result, or kFault.
        int SafeJoin(void* channel, void* group, float pitch)
        {
            __try
            {
                if (pitch != 1.0f)
                {
                    float frequency = 0.0f;
                    if (reinterpret_cast<GetFrequencyFn>(kChannelGetFrequency)(channel, &frequency) == 0 && frequency > 0.0f)
                        reinterpret_cast<SetFrequencyFn>(kChannelSetFrequency)(channel, frequency * pitch);
                }
                return reinterpret_cast<AddToGroupFn>(kAddToGroup)(channel, group);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return kFault; }
        }

        void Broke(const char* while_)
        {
            if (g_broken) return;
            g_broken = true;
            std::snprintf(g_statusText, sizeof(g_statusText), "stopped: a fault while %s; thunder plays unfiltered", while_);
            if (g_api)
                g_api->Log(WXL_LOG_WARN, kTag, "storm: thunder filters: a fault while %s; thunder plays unfiltered from now on "
                                               "(our chains are left as they are).", while_);
        }

        // FMOD through the client's SE2 calls. Every call checked for a fault; a fault stops everything (Ready() false).
        struct ClientEngine final : Engine
        {
            bool Ready() override
            {
                if (g_broken || !g_available) return false;
                const int up = SafeIsInitialized();
                if (up == kFault) { Broke("asking whether the sound engine is up"); return false; }
                return up != 0;
            }

            bool Create(unsigned slot, void*& group, void*& lowpass, void*& echo) override
            {
                group = lowpass = echo = nullptr;
                if (slot >= kSlots) return false;
                bool fault = false;
                void* g = SafeCreateGroup(kGroupNames[slot], fault);
                if (fault) { Broke("making a chain's channel group"); return false; }
                if (!g) return false;
                void* lp = nullptr;
                void* ec = nullptr;
                const int a = SafeCreateDsp(kFmodLowpass, &lp);
                const int b = a == 0 ? SafeCreateDsp(kFmodEcho, &ec) : -1;
                if (a == kFault || b == kFault) { Broke("making a chain's low-pass or echo"); return false; }
                if (a != 0 || b != 0 || !lp || !ec)
                {
                    Destroy(g, a == 0 ? lp : nullptr, b == 0 ? ec : nullptr);
                    return false;
                }
                // Each between the group's head and its output, as SI2::DSP_CreateDsp connects a stage: the last connected
                // runs first, so the echo runs before the low-pass and its repeats are muffled as much.
                if (SafeConnect(lp, g) == kFault || SafeConnect(ec, g) == kFault) { Broke("connecting a chain"); return false; }
                // Transparent until set for a layer: never an unconfigured stage on the path.
                if (!Configure(lp, ec, Params{}))
                {
                    if (!g_broken) Destroy(g, lp, ec);
                    return false;
                }
                group = g;
                lowpass = lp;
                echo = ec;
                return true;
            }

            bool Configure(void* lowpass, void* echo, const Params& p) override
            {
                struct Set { void* dsp; int index; float value; }; // index < 0: the bypass (value 1 = bypassed)
                const Set sets[] = {
                    { lowpass, kLowpassCutoff, Clamp(p.cutoff, 10.0f, 22000.0f) },
                    { lowpass, kLowpassResonance, Clamp(p.resonance, 1.0f, 10.0f) },
                    { lowpass, -1, p.lowpass ? 0.0f : 1.0f },
                    { echo, kEchoDelay, Clamp(p.delayMs, 10.0f, 5000.0f) },
                    { echo, kEchoDecay, Clamp(p.decay, 0.0f, 1.0f) },
                    { echo, kEchoDry, Clamp(p.dry, 0.0f, 1.0f) },
                    { echo, kEchoWet, p.echo ? Clamp(p.wet, 0.0f, 1.0f) : 0.0f },
                    { echo, -1, p.echo ? 0.0f : 1.0f },
                };
                for (const Set& s : sets)
                {
                    const int r = s.index < 0 ? SafeBypass(s.dsp, s.value != 0.0f) : SafeSetParam(s.dsp, s.index, s.value);
                    if (r == kFault) { Broke("setting a chain's filters"); return false; }
                    if (r != 0) return false;
                }
                return true;
            }

            bool Enable(void* lowpass, void* echo, bool on) override
            {
                const int a = SafeEnable(lowpass, on);
                const int b = a == kFault ? kFault : SafeEnable(echo, on);
                if (a == kFault || b == kFault) { Broke(on ? "switching a chain on" : "switching a chain off"); return false; }
                return a == 0 && b == 0;
            }

            int Channels(void* group) override
            {
                const int n = SafeChannels(group);
                if (n == kFault) { Broke("counting a chain's sounds"); return -1; }
                return n;
            }

            bool Attach(void* channel, void* group, float pitch) override
            {
                const int r = SafeJoin(channel, group, pitch);
                if (r == kFault) { Broke("joining a sound to its chain"); return false; }
                return r == 0;
            }

            void Destroy(void* group, void* lowpass, void* echo) override
            {
                if (g_broken) return;
                // As SI2::DSP_ShutDown: the stages out of the path and released, then the group (its sounds, if any,
                // back to the master group).
                if ((echo && SafeDestroyDsp(echo) == kFault) || (lowpass && SafeDestroyDsp(lowpass) == kFault) ||
                    (group && SafeDestroyGroup(group) == kFault))
                    Broke("releasing a chain");
            }
        };
        ClientEngine g_engine;

        bool OnMainThread() { return g_mainThread != 0 && GetCurrentThreadId() == g_mainThread; }

        // The chains belong to the FMOD system they were made in. DSP_ShutDown releases them before the sound engine lets
        // its system go; if a new system ever turned up without that, they're forgotten (never called again).
        void CheckSystem()
        {
            if (!g_pool.created || g_broken) return;
            uintptr_t system = 0;
            if (!SafeSystem(system)) { Broke("reading the sound engine's FMOD system"); return; }
            if (system == g_system) return;
            g_pool.Forget();
            g_system = 0;
            if (g_api) g_api->Log(WXL_LOG_WARN, kTag, "storm: thunder filters: the sound engine's FMOD system changed without the chains' shutdown; forgot them (made again when needed).");
        }

        // 0 not ours, 1 ours, 2 ours but malformed, -1 a fault reading the name.
        int ReadName(const char* name, unsigned& slot, uint32_t& generation, float& pitch)
        {
            __try
            {
                if (!IsOurName(name)) return 0;
                return ParseName(name, slot, generation, pitch) ? 1 : 2;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
        }

        // The client's chain callback for every sound played with a chain name: Death Knight voices pass on untouched;
        // ours (the main thread's, while we're working) join our chain.
        void __cdecl hkAttach(void* channel, char* name)
        {
            unsigned slot = 0;
            uint32_t generation = 0;
            float pitch = 1.0f;
            const int ours = ReadName(name, slot, generation, pitch);
            if (ours == 0) { g_origAttach(channel, name); return; }
            if (ours != 1 || !channel || !g_available || g_broken || !OnMainThread()) return; // plays unfiltered
            CheckSystem();
            const Joined r = g_pool.Attach(g_engine, slot, generation, pitch, channel, g_clock);
            if (r == Joined::Stale && !g_loggedStale && g_api)
            {
                g_loggedStale = true;
                g_api->Log(WXL_LOG_INFO, kTag, "storm: thunder filters: a sound whose load completed after its chain was set for another plays unfiltered (logged once).");
            }
            if ((r == Joined::EnableFailed || r == Joined::AttachFailed) && !g_loggedJoin && g_api)
            {
                g_loggedJoin = true;
                g_api->Log(WXL_LOG_WARN, kTag, "storm: thunder filters: FMOD refused %s; that layer plays unfiltered (logged once).",
                           r == Joined::EnableFailed ? "to switch a chain on" : "a sound into its chain");
            }
        }

        // The client tears its chains down before the sound engine releases its FMOD system: ours go with them.
        void __cdecl hkShutDown()
        {
            const bool had = g_pool.created;
            if (!g_broken) g_pool.Release(g_engine);
            else g_pool.Forget();
            g_system = 0;
            if (had && g_api)
                g_api->Log(WXL_LOG_INFO, kTag, "storm: thunder filters: the sound engine is shutting down; released our chains with the client's (made again when needed).");
            g_origShutDown();
        }

        const char* RefusalName(Refusal r)
        {
            switch (r)
            {
                case Refusal::NotReady: return "the sound engine isn't up";
                case Refusal::CreateFailed: return "FMOD refused to make the chains";
                case Refusal::Busy: return "every chain is busy";
                case Refusal::ConfigureFailed: return "FMOD refused a chain's settings";
                default: return "no chain";
            }
        }
    }

    Settings& Config() { return g_settings; }
    const Status& GetStatus() { return g_status; }
    const char* StatusText() { return g_statusText; }

    Params Preview(Layer layer, float distanceYards)
    {
        return ParamsFor(layer, distanceYards, g_settings.strength, g_settings.echoAmount);
    }

    void Install(const WXL_Api* api)
    {
        g_api = api;
        const storm::Guard* bad = nullptr;
        unsigned detoured = 0;
        const int verified = Verify(kGuards, bad, detoured);
        switch (verified)
        {
            case 0: break;
            case 1: std::snprintf(g_statusText, sizeof(g_statusText), "off: not the 3.3.5a 12340 client"); break;
            case 2: std::snprintf(g_statusText, sizeof(g_statusText), "off: the client's %s at 0x%08X differs", bad->what, static_cast<unsigned>(bad->address)); break;
            default: std::snprintf(g_statusText, sizeof(g_statusText), "off: the client's code could not be read"); break;
        }
        if (verified != 0)
        {
            api->Log(WXL_LOG_WARN, kTag, "storm: thunder filters %s; thunder plays unfiltered.", g_statusText);
            return;
        }
        if (!api->HookAttach("LivingAzeroth.ThunderChains", kAttachChain, reinterpret_cast<void*>(&hkAttach),
                             reinterpret_cast<void**>(&g_origAttach), WXL_HOOK_DEFAULT_PRIORITY))
        {
            std::snprintf(g_statusText, sizeof(g_statusText), "off: the detour on DSP_AttachCustomDspChain failed");
            api->Log(WXL_LOG_WARN, kTag, "storm: thunder filters %s; thunder plays unfiltered.", g_statusText);
            return;
        }
        // Without this one ours could outlive the FMOD system: none are made (the first detour, already in, passes every
        // name but ours on, and none of ours are made).
        if (!api->HookAttach("LivingAzeroth.ThunderChainsShutDown", kChainsShutDown, reinterpret_cast<void*>(&hkShutDown),
                             reinterpret_cast<void**>(&g_origShutDown), WXL_HOOK_DEFAULT_PRIORITY))
        {
            std::snprintf(g_statusText, sizeof(g_statusText), "off: the detour on DSP_ShutDown failed");
            api->Log(WXL_LOG_WARN, kTag, "storm: thunder filters %s; thunder plays unfiltered.", g_statusText);
            return;
        }
        g_available = true;
        std::snprintf(g_statusText, sizeof(g_statusText), "available: the client's chain callback (DSP_AttachCustomDspChain, detoured for our names) into %u chains of our own%s",
                      kSlots, detoured ? " (a call detoured by another party too)" : "");
        api->Log(WXL_LOG_INFO, kTag, "storm: thunder filters %s; each a low-pass and an echo, the pitch by the sound's rate; %s.", g_statusText,
                 g_settings.filters ? "on" : "switched off in WarcraftXL.ini");
    }

    void Update(float dt)
    {
        if (!g_mainThread) g_mainThread = GetCurrentThreadId();
        if (dt >= 0.0f && dt <= 1.0f) g_clock += dt;
        else g_clock += 1.0f / 60.0f;
        if (g_available && !g_broken && OnMainThread())
        {
            CheckSystem();
            g_pool.Update(g_engine, g_clock);
        }
        g_status.chains = g_pool.created ? static_cast<int>(kSlots) : 0;
        g_status.busyChains = g_pool.Busy();
        g_status.pool = g_pool.stats;
    }

    Use Prepare(Layer layer, float distanceYards, bool want, char* name, size_t size)
    {
        Use use;
        if (name && size) name[0] = '\0';
        if (!want) return use;
        use.params = ParamsFor(layer, distanceYards, g_settings.strength, g_settings.echoAmount);
        if (Plain(use.params)) { ++g_status.plain; return use; }
        if (!g_available || g_broken || !OnMainThread() || !name || size < kChainNameSize) { ++g_status.unavailable; return use; }
        // The client's own switch for DSP effects: off (or not read by the client yet), none of ours either.
        const int effects = SafeDspEffects();
        if (effects == kFault) Broke("reading the client's DSP effects setting");
        if (effects != 1)
        {
            ++g_status.unavailable;
            if (effects == 0 && !g_loggedEffectsOff && g_api)
            {
                g_loggedEffectsOff = true;
                g_api->Log(WXL_LOG_INFO, kTag, "storm: thunder filters: the client's DSP effects are off (Sound_EnableDSPEffects 0); thunder plays unfiltered (logged once).");
            }
            return use;
        }
        CheckSystem();
        Refusal why = Refusal::None;
        const bool had = g_pool.created;
        const Ticket t = g_pool.Acquire(g_engine, use.params, g_clock, why);
        if (g_pool.created && !had)
        {
            if (!SafeSystem(g_system)) Broke("reading the sound engine's FMOD system");
            if (!g_loggedMade && g_api)
            {
                g_loggedMade = true;
                g_api->Log(WXL_LOG_INFO, kTag, "storm: thunder filters: made %u chains (a channel group, a low-pass and an echo each).", kSlots);
            }
        }
        if (!t.Valid())
        {
            if (why == Refusal::NotReady) ++g_status.unavailable;
            bool* once = why == Refusal::Busy ? &g_loggedBusy : why == Refusal::CreateFailed ? &g_loggedCreate
                       : why == Refusal::ConfigureFailed ? &g_loggedConfigure : nullptr;
            if (once && !*once && g_api)
            {
                *once = true;
                g_api->Log(why == Refusal::Busy ? WXL_LOG_INFO : WXL_LOG_WARN, kTag,
                           "storm: thunder filters: %s; that layer plays unfiltered (logged once).", RefusalName(why));
            }
            return use;
        }
        if (!g_broken && FormatName(name, size, static_cast<unsigned>(t.slot), t.generation, use.params.pitch))
        {
            use.filtered = true;
            use.ticket = t;
        }
        else g_pool.Played(t, false);
        return use;
    }

    void Played(const Use& use, bool playing)
    {
        if (!use.filtered) return;
        g_pool.Played(use.ticket, playing);
        if (!playing) return;
        ++g_status.filtered;
        if (!g_loggedFirst && g_api)
        {
            g_loggedFirst = true;
            const Params& p = use.params;
            char lowpass[48] = "no low-pass", echo[64] = "no echo";
            if (p.lowpass) std::snprintf(lowpass, sizeof(lowpass), "low-pass %.0f Hz", p.cutoff);
            if (p.echo) std::snprintf(echo, sizeof(echo), "echo %.0f ms x %.2f, wet %.2f", p.delayMs, p.decay, p.wet);
            g_api->Log(WXL_LOG_INFO, kTag, "storm: thunder filters: the first filtered layer through chain %d: %s, pitch %.3f, %s.",
                       use.ticket.slot, lowpass, p.pitch, echo);
        }
    }
}
