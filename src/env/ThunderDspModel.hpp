// Thunder through distance filters (experimental), the rules: a low-pass, a slight pitch-down and an echo on a thunder
// layer by its strike's distance, in the client's own FMOD Ex. No client access here, so tests/storm runs them offline
// with FMOD mocked; env/ThunderDsp applies them to the client.
//
// The path (3.3.5a build 12340), checked byte for byte (kGuards) before anything is hooked or called:
//  - SI2::PlaySoundKit (0x4C6A40), given SE2SoundKitProperties with m_useDspChain (+0x60) set, hands the sound the
//    chain name at +0x64 and SI2::DSP_AttachCustomDspChain (0x4CE250) as its callback (0x4C70E2, through
//    SESound::SetAttachCustomDSPChainCallback 0x8786E0). The sound engine calls it with the sound's new FMOD channel as
//    soon as the channel exists, still paused, right before it plays (SEFileSound::CompleteNonBlockingLoad, 0x87BA43,
//    then SESoundInternal::Play 0x879760): within PlaySoundKit for a file already loaded, else from the sound engine's
//    heartbeat when its load completes -- both on the main thread. It's how the client gives Death Knight voices their
//    SoundFilter.dbc chain.
//  - The client looks the name up in its own chain table (0xB4ADB4); ours aren't there. A detour on
//    DSP_AttachCustomDspChain takes the names that are ours (kNamePrefix) and passes every other name on untouched.
//    Ours go to a chain we own: an FMOD channel group with a low-pass and an echo, made with the SE2 calls the client's
//    own chain building makes (SI2::DSP_CreateDsp 0x4CDF70: CreateChannelGroup, CreateDSPByType, SetDspParam,
//    BypassDsp, ConnectDspToChannelGroup) and joined as DSP_AttachCustomDspChain joins its own (EnableDSP, then
//    AddChannelToChannelGroup). Nothing is written into the client's tables.
//  - Pitch is the sound's playback rate, not FMOD's PITCHSHIFT DSP (an FFT phase vocoder on every channel of the
//    group's mix, 512-point at 4x overlap as the client sets it: costly and smearing a crack): the channel's frequency
//    times the pitch, with FMOD's Channel::getFrequency and setFrequency as SESound::SetPitch (0x878760) calls them for
//    kits with random pitch. A far rumble so plays a little longer too.
//  - A detour on SI2::DSP_ShutDown (0x4CDD40), which SI2::ShutDown calls (0x4C627C) before the sound engine releases its
//    FMOD system (a sound restart: a device change, the sound options; and quitting), releases ours with the client's.
//
// From Morfium-G's wxl-seyris-dehardcoding (github.com/Morfium-G/wxl-seyris-dehardcoding: src/LiveDsp.cpp,
// src/DspParamTable.hpp), which walks and rebuilds the client's SoundFilter chains live: the SE2 calls' addresses
// (SetDspParam 0x878D20, BypassDsp 0x878D40, EnableDSP 0x878D60, DspDestroy 0x878D80, CreateChannelGroup 0x878E10) and
// the chain table's (0xB4ADB4), the chain-building path through SI2__DSP_CreateDsp (0x4CDF70), that the client's FMOD is
// Ex 4.09.07 with the older 5-parameter ECHO whose dry and wet mixes are FMOD indices 3 and 4 (2, MAXCHANNELS, is never
// set), that a stage made live has to stay enabled to filter, and that no stage may sit on the path unconfigured
// (its ApplyTransparentParams). Each checked again here against the 12340 exe (kGuards, tests/storm).
#pragma once

#include "StormModel.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace wxl_livingazeroth::thunderdsp
{
    // --- the chain by distance ---------------------------------------------------------------------
    enum class Layer : int { Crack = 0, Rumble }; // a crack or a bolt; the rolling rumble (storm::Kit::Rumble)

    struct Params
    {
        bool  lowpass = false;    // FMOD_DSP_TYPE_LOWPASS, a 4-pole resonant low-pass (bypassed when false)
        float cutoff = 22000.0f;  // Hz (FMOD: 10..22000)
        float resonance = 1.0f;   // Q (FMOD: 1..10); 1, its flattest
        bool  echo = false;       // FMOD_DSP_TYPE_ECHO (bypassed when false)
        float delayMs = 500.0f;   // FMOD: 10..5000
        float decay = 0.5f;       // each repeat x this (0..1)
        float dry = 1.0f;         // the sound itself
        float wet = 0.0f;         // its repeats
        float pitch = 1.0f;       // the sound's playback rate x this when it joins (not a DSP; per sound, not per chain)
    };

    // Low-pass and pitch: none up to kNearFrom (near cracks stay crisp), then on a log scale of the distance to kFarAt,
    // where the cutoff reaches kFarCutoff and the pitch 1 - kFarPitchDrop (FilterStrength 1; 2 goes twice as far).
    constexpr float kNearFrom = 150.0f;      // yd
    constexpr float kFarAt = 2000.0f;        // yd
    constexpr float kNearCutoff = 20000.0f;  // Hz
    constexpr float kFarCutoff = 900.0f;     // Hz
    constexpr float kMinCutoff = 250.0f;     // Hz: never lower, whatever the strength
    constexpr float kOpenCutoff = 18000.0f;  // Hz: from here up the low-pass is left out (bypassed)
    constexpr float kFarPitchDrop = 0.12f;   // pitch 0.88 at kFarAt
    constexpr float kMinPitch = 0.75f;
    // Echo, x EchoAmount on its wet mix: a crack or bolt from kCrackEchoFrom out, its delay and decay growing with the
    // distance (the farther, the larger what it rolls off); the rumble at every distance and longer.
    constexpr float kCrackEchoFrom = 200.0f;                  // yd: nearer cracks get none
    constexpr float kCrackDelay[2] = { 150.0f, 400.0f };      // ms, at kCrackEchoFrom .. kFarAt
    constexpr float kCrackDecay[2] = { 0.30f, 0.50f };
    constexpr float kCrackWet = 0.45f;                        // x sqrt of the share: rises fast past kCrackEchoFrom
    constexpr float kRumbleEchoFrom = storm::kNearestStrike;  // yd
    constexpr float kRumbleDelay[2] = { 250.0f, 450.0f };     // ms
    constexpr float kRumbleDecay[2] = { 0.45f, 0.60f };
    constexpr float kRumbleWet[2] = { 0.35f, 0.50f };
    constexpr float kMinWet = 0.01f;                          // a wet mix below this: no echo

    inline float Clamp(float v, float lo, float hi) { return v >= lo ? (v <= hi ? v : hi) : lo; } // NaN -> lo

    /// 0 at `from` and nearer, 1 at `to` and farther, on a log scale between (0 for nonsense).
    inline float LogShare(float distance, float from, float to)
    {
        if (!(distance > from)) return 0.0f;
        if (!(distance < to)) return std::isfinite(distance) ? 1.0f : 0.0f;
        return std::log(distance / from) / std::log(to / from);
    }

    /// 0 near (kNearFrom and nearer) .. 1 far (kFarAt and farther).
    inline float Farness(float distance) { return LogShare(distance, kNearFrom, kFarAt); }

    /// The chain for a layer of a strike this far away, with FilterStrength (0..2) and EchoAmount (0..2).
    inline Params ParamsFor(Layer layer, float distance, float strength, float echoAmount)
    {
        Params p;
        if (!(distance >= 0.0f) || !std::isfinite(distance)) return p;
        const float s = Clamp(strength, 0.0f, 2.0f), e = Clamp(echoAmount, 0.0f, 2.0f);
        const float k = Farness(distance) * s;
        if (k > 0.0f)
        {
            p.cutoff = std::fmax(kNearCutoff * std::pow(kFarCutoff / kNearCutoff, k), kMinCutoff);
            p.lowpass = p.cutoff < kOpenCutoff;
            const float pitch = std::fmax(1.0f - kFarPitchDrop * k, kMinPitch);
            p.pitch = pitch > 0.995f ? 1.0f : pitch;
        }
        if (e > 0.0f)
        {
            if (layer == Layer::Crack)
            {
                const float u = LogShare(distance, kCrackEchoFrom, kFarAt);
                if (u > 0.0f)
                {
                    p.delayMs = kCrackDelay[0] + (kCrackDelay[1] - kCrackDelay[0]) * u;
                    p.decay = kCrackDecay[0] + (kCrackDecay[1] - kCrackDecay[0]) * u;
                    p.wet = std::fmin(1.0f, e * kCrackWet * std::sqrt(u));
                }
            }
            else
            {
                const float u = LogShare(distance, kRumbleEchoFrom, kFarAt);
                p.delayMs = kRumbleDelay[0] + (kRumbleDelay[1] - kRumbleDelay[0]) * u;
                p.decay = kRumbleDecay[0] + (kRumbleDecay[1] - kRumbleDecay[0]) * u;
                p.wet = std::fmin(1.0f, e * (kRumbleWet[0] + (kRumbleWet[1] - kRumbleWet[0]) * u));
            }
            p.echo = p.wet >= kMinWet;
            if (!p.echo) { p.wet = 0.0f; p.delayMs = 500.0f; p.decay = 0.5f; }
        }
        return p;
    }

    /// Nothing to do: the layer plays as it is.
    inline bool Plain(const Params& p) { return !p.lowpass && !p.echo && p.pitch == 1.0f; }

    /// Two layers can share a chain (its DSPs set once): close enough to hear no difference. Pitch is per sound.
    inline bool SameChain(const Params& a, const Params& b)
    {
        if (a.lowpass != b.lowpass || a.echo != b.echo) return false;
        if (a.lowpass && !(std::fabs(std::log(a.cutoff / b.cutoff)) <= 0.12f && a.resonance == b.resonance)) return false;
        if (a.echo && !(std::fabs(a.delayMs - b.delayMs) <= 25.0f && std::fabs(a.decay - b.decay) <= 0.03f &&
                        std::fabs(a.wet - b.wet) <= 0.04f && a.dry == b.dry))
            return false;
        return true;
    }

    /// Seconds a chain keeps ringing after its last sound left: its echo's repeats down to -60 dB (a low-pass alone:
    /// next to nothing). Its DSPs stay on and it isn't set for other params until then.
    constexpr float kQuietAfter = 0.3f;
    constexpr float kMaxTail = 10.0f;
    inline float TailSeconds(const Params& p)
    {
        if (!p.echo || !(p.wet > 0.0f)) return kQuietAfter;
        const float decay = Clamp(p.decay, 0.0f, 0.95f);
        float repeats = decay <= 0.001f ? 1.0f : std::log(0.001f / std::fmin(p.wet, 1.0f)) / std::log(decay);
        repeats = Clamp(repeats, 1.0f, 60.0f);
        return std::fmin(kMaxTail, Clamp(p.delayMs, 10.0f, 5000.0f) * 0.001f * (repeats + 1.0f) + kQuietAfter);
    }

    // --- chain names (the properties' m_dspChainName, 128 bytes) ---------------------------------
    // "LivingAzeroth thunder <slot>.<generation>.<pitch per mille>": which chain, which setting of it (a sound whose load
    // completes after its chain was set for other params stays unfiltered), and the sound's pitch.
    constexpr char   kNamePrefix[] = "LivingAzeroth thunder ";
    constexpr size_t kChainNameSize = 128;
    constexpr unsigned kSlots = 6;           // chains: a strike takes up to two (its crack or bolt, its rumble)

    inline bool IsOurName(const char* name)
    {
        return name && std::strncmp(name, kNamePrefix, sizeof(kNamePrefix) - 1) == 0;
    }

    inline bool FormatName(char* out, size_t size, unsigned slot, uint32_t generation, float pitch)
    {
        if (!out || !size) return false;
        out[0] = '\0';
        if (slot >= kSlots) return false;
        const int permille = static_cast<int>(std::lround(Clamp(pitch, kMinPitch, 1.0f) * 1000.0f));
        const int n = std::snprintf(out, size, "%s%u.%u.%d", kNamePrefix, slot, static_cast<unsigned>(generation), permille);
        if (n <= 0 || static_cast<size_t>(n) >= size) { out[0] = '\0'; return false; }
        return true;
    }

    /// Ours and well formed: which chain, which setting of it, the pitch.
    inline bool ParseName(const char* name, unsigned& slot, uint32_t& generation, float& pitch)
    {
        if (!IsOurName(name)) return false;
        const char* c = name + sizeof(kNamePrefix) - 1;
        uint32_t v[3] = {};
        for (int i = 0; i < 3; ++i)
        {
            if (*c < '0' || *c > '9') return false;
            uint64_t n = 0;
            for (int digits = 0; *c >= '0' && *c <= '9'; ++c)
            {
                if (++digits > 10) return false;
                n = n * 10 + static_cast<uint64_t>(*c - '0');
            }
            if (n > 0xFFFFFFFFull) return false;
            v[i] = static_cast<uint32_t>(n);
            if (i < 2 && *c++ != '.') return false;
        }
        if (*c != '\0' || v[0] >= kSlots || v[2] < static_cast<uint32_t>(kMinPitch * 1000.0f + 0.5f) || v[2] > 1000) return false;
        slot = v[0];
        generation = v[1];
        pitch = static_cast<float>(v[2]) / 1000.0f;
        return true;
    }

    // --- the chains: a few kept and reused ----------------------------------------------------------
    /// FMOD through the client's calls (env/ThunderDsp; mocked in tests/storm). Every method on the main thread.
    struct Engine
    {
        virtual ~Engine() = default;
        virtual bool Ready() = 0;                                                  // the sound engine is up
        virtual bool Create(unsigned slot, void*& group, void*& lowpass, void*& echo) = 0; // all three or none
        virtual bool Configure(void* lowpass, void* echo, const Params& p) = 0;    // their parameters and bypasses
        virtual bool Enable(void* lowpass, void* echo, bool on) = 0;               // FMOD's setActive on both
        virtual int  Channels(void* group) = 0;                                    // sounds in it now; < 0 unknown
        virtual bool Attach(void* channel, void* group, float pitch) = 0;          // pitch, then into the group
        virtual void Destroy(void* group, void* lowpass, void* echo) = 0;
    };

    struct Slot
    {
        void*    group = nullptr;
        void*    lowpass = nullptr;
        void*    echo = nullptr;
        Params   params;               // what its DSPs are set to, once configured
        bool     configured = false;
        bool     active = false;       // its DSPs on (FMOD processes them)
        uint32_t generation = 0;       // which setting: a name made for an earlier one no longer joins
        int      pending = 0;          // plays handed this chain whose channel hasn't joined yet
        float    pendingUntil = 0.0f;  // ... given up on after this (a load that never completes)
        int      channels = 0;         // sounds in its group (the last count, plus joins since)
        float    lastBusy = -1.0e9f;   // when it last had a sound in it or on its way
    };

    struct Ticket
    {
        int      slot = -1;
        uint32_t generation = 0;
        bool Valid() const { return slot >= 0; }
    };

    /// Why a layer plays unfiltered.
    enum class Refusal { None, Plain, NotReady, CreateFailed, Busy, ConfigureFailed };
    enum class Joined { Yes, Stale, EnableFailed, AttachFailed };

    struct PoolStats
    {
        unsigned made = 0;          // times the chains were made (once per sound system)
        unsigned configured = 0;    // a chain set for new params
        unsigned shared = 0;        // a layer given a chain already set that way
        unsigned joined = 0;        // sounds that joined a chain
        unsigned stale = 0;         // ... whose chain had been set otherwise meanwhile (unfiltered)
        unsigned failedJoins = 0;   // enabling or joining failed (unfiltered)
        unsigned busy = 0;          // no chain free (unfiltered)
        unsigned expired = 0;       // reservations given up (a load that never completed)
        unsigned released = 0;      // times released with the sound system
    };

    constexpr float kPendingSeconds = 3.0f;

    class Pool
    {
    public:
        Slot      slot[kSlots];
        bool      created = false;
        bool      createFailed = false;    // tried and failed for this sound system: not again until it restarts
        uint32_t  nextGeneration = 1;      // never reused, across releases too
        PoolStats stats;

        /// A chain for a layer about to play, or an invalid ticket and why not.
        Ticket Acquire(Engine& e, const Params& p, float now, Refusal& why)
        {
            why = Refusal::None;
            if (Plain(p)) { why = Refusal::Plain; return {}; }
            if (!e.Ready()) { why = Refusal::NotReady; return {}; }
            if (!created && !Make(e)) { why = Refusal::CreateFailed; return {}; }
            // A chain already set this way, in use, ringing or idle: shared, its tail left alone.
            for (unsigned i = 0; i < kSlots; ++i)
            {
                Slot& s = slot[i];
                if (!s.configured || !SameChain(s.params, p)) continue;
                if (s.channels > 0 || s.pending > 0) ++stats.shared;
                return Reserve(i, now);
            }
            // Else the chain idle longest: no sound in it or on its way, its echo died away.
            int best = -1;
            for (unsigned i = 0; i < kSlots; ++i)
            {
                const Slot& s = slot[i];
                if (s.channels > 0 || s.pending > 0) continue;
                if (s.configured && now - s.lastBusy < TailSeconds(s.params)) continue;
                if (best < 0 || s.lastBusy < slot[best].lastBusy) best = static_cast<int>(i);
            }
            if (best < 0) { ++stats.busy; why = Refusal::Busy; return {}; }
            Slot& s = slot[best];
            s.generation = nextGeneration++; // whatever happens next, names made for its last setting are done with
            if (!e.Configure(s.lowpass, s.echo, p))
            {
                s.configured = false;        // maybe half set: set again before it's used
                why = Refusal::ConfigureFailed;
                return {};
            }
            s.params = p;
            s.configured = true;
            ++stats.configured;
            return Reserve(static_cast<unsigned>(best), now);
        }

        /// After PlaySoundKit: a play that didn't start gives its reservation back (one that did keeps it until its
        /// channel joins, or kPendingSeconds).
        void Played(const Ticket& t, bool playing)
        {
            if (!t.Valid() || playing || t.slot >= static_cast<int>(kSlots)) return;
            Slot& s = slot[t.slot];
            if (s.generation == t.generation && s.pending > 0) --s.pending;
        }

        /// A sound's channel, from the chain callback: joins its chain if that chain is still set as its name says.
        Joined Attach(Engine& e, unsigned index, uint32_t generation, float pitch, void* channel, float now)
        {
            if (!created || index >= kSlots) { ++stats.stale; return Joined::Stale; }
            Slot& s = slot[index];
            if (!s.configured || s.generation != generation) { ++stats.stale; return Joined::Stale; }
            if (s.pending > 0) --s.pending;
            // As DSP_AttachCustomDspChain: the stages on first, then the channel in (never into a chain that's off: the
            // group's mix would stop there, silence).
            if (!s.active)
            {
                if (!e.Enable(s.lowpass, s.echo, true)) { ++stats.failedJoins; return Joined::EnableFailed; }
                s.active = true;
            }
            if (!e.Attach(channel, s.group, pitch)) { ++stats.failedJoins; return Joined::AttachFailed; }
            ++s.channels;
            s.lastBusy = now;
            ++stats.joined;
            return Joined::Yes;
        }

        /// Once a frame: reservations past their time given up, the sounds in each chain counted (a sound that ended,
        /// early or not, or that FMOD stole, has left its group), and a chain quiet for its echo's tail switched off.
        void Update(Engine& e, float now)
        {
            if (!created || !e.Ready()) return;
            for (Slot& s : slot)
            {
                if (s.pending > 0 && now > s.pendingUntil) { stats.expired += static_cast<unsigned>(s.pending); s.pending = 0; }
                const int n = e.Channels(s.group);
                if (n >= 0) s.channels = n;
                if (s.channels > 0 || s.pending > 0) s.lastBusy = now;
                else if (s.active && now - s.lastBusy >= TailSeconds(s.params) && e.Enable(s.lowpass, s.echo, false))
                    s.active = false;
            }
        }

        /// The sound system is going (DSP_ShutDown): every chain released, once.
        void Release(Engine& e)
        {
            if (created)
            {
                for (Slot& s : slot)
                    if (s.group || s.lowpass || s.echo) e.Destroy(s.group, s.lowpass, s.echo);
                ++stats.released;
            }
            Forget();
        }

        /// Everything dropped without a call (the engine is gone or broken).
        void Forget()
        {
            for (Slot& s : slot) s = Slot{};
            created = false;
            createFailed = false;
        }

        /// Chains with a sound in them or on its way.
        int Busy() const
        {
            int n = 0;
            for (const Slot& s : slot) n += (s.channels > 0 || s.pending > 0) ? 1 : 0;
            return n;
        }

    private:
        bool Make(Engine& e)
        {
            if (createFailed) return false;
            for (unsigned i = 0; i < kSlots; ++i)
            {
                Slot& s = slot[i];
                s = Slot{};
                // Off until a sound joins, as the client leaves its own chains after building them (SI2::DSP_Init).
                const bool made = e.Create(i, s.group, s.lowpass, s.echo);
                if (!made || !e.Enable(s.lowpass, s.echo, false))
                {
                    if (made) e.Destroy(s.group, s.lowpass, s.echo);
                    for (unsigned j = 0; j < i; ++j) e.Destroy(slot[j].group, slot[j].lowpass, slot[j].echo);
                    for (Slot& t : slot) t = Slot{};
                    createFailed = true;
                    return false;
                }
            }
            created = true;
            ++stats.made;
            return true;
        }

        Ticket Reserve(unsigned i, float now)
        {
            Slot& s = slot[i];
            ++s.pending;
            s.pendingUntil = now + kPendingSeconds;
            s.lastBusy = now;
            return Ticket{ static_cast<int>(i), s.generation };
        }
    };

    // --- the client code this relies on (checked byte for byte before either detour or any call) ----
    constexpr uintptr_t kAttachChain = 0x004CE250;    // SI2::DSP_AttachCustomDspChain, __cdecl void(FMOD::Channel*, char*)
    constexpr uintptr_t kChainsShutDown = 0x004CDD40; // SI2::DSP_ShutDown, __cdecl void()
    constexpr size_t    kPropsUseChain = 0x60;        // SE2SoundKitProperties::m_useDspChain (int)
    constexpr size_t    kPropsChainName = 0x64;       // ... m_dspChainName[128]
    // SE2, all __cdecl (FMOD_RESULT: 0 = FMOD_OK).
    constexpr uintptr_t kIsInitialized = 0x00878CB0;   // int()
    constexpr uintptr_t kCreateDspByType = 0x00878D00; // FMOD_RESULT(FMOD_DSP_TYPE, FMOD::DSP**) on its system (0xD43800)
    constexpr uintptr_t kSetDspParam = 0x00878D20;     // FMOD_RESULT(DSP*, int index, float)
    constexpr uintptr_t kBypassDsp = 0x00878D40;       // FMOD_RESULT(DSP*, bool)
    constexpr uintptr_t kEnableDsp = 0x00878D60;       // FMOD_RESULT(DSP*, bool): DSP::setActive
    constexpr uintptr_t kDestroyDsp = 0x00878D80;      // FMOD_RESULT(DSP*): remove, release
    constexpr uintptr_t kConnectDsp = 0x00878DA0;      // FMOD_RESULT(DSP*, ChannelGroup*): between its head and its output
    constexpr uintptr_t kGroupChannels = 0x00878DF0;   // int(ChannelGroup*)
    constexpr uintptr_t kCreateGroup = 0x00878E10;     // ChannelGroup*(const char*): under the master group
    constexpr uintptr_t kDestroyGroup = 0x00878E40;    // FMOD_RESULT(ChannelGroup*)
    constexpr uintptr_t kAddToGroup = 0x00878E50;      // FMOD_RESULT(Channel*, ChannelGroup*): Channel::setChannelGroup
    constexpr uintptr_t kSoundSystem = 0x00D43800;     // SE2's FMOD::System* (s_system)
    constexpr uintptr_t kSoundShutDown = 0x0087B490;   // SE2::ShutDown: releases that system (every call after DSP_ShutDown)
    // The client's own switch for DSP effects, which its chain building obeys (SI2::DSP_CreateDsp makes nothing while it's
    // 0): DSP_CreateDsp's static CVar* for "Sound_EnableDSPEffects", looked up on its first call (flag bit 1), its int
    // value at +0x30. We follow it too.
    constexpr uintptr_t kDspEffectsLookedUp = 0x00B4ADB0;
    constexpr uintptr_t kDspEffectsCVar = 0x00B4ADAC;
    constexpr size_t    kCVarIntValue = 0x30;
    // FMOD Ex, statically linked: __stdcall with `this` first (a Channel* is a handle FMOD validates).
    constexpr uintptr_t kChannelSetFrequency = 0x008D0A00; // FMOD_RESULT(Channel*, float)
    constexpr uintptr_t kChannelGetFrequency = 0x008D0A30; // FMOD_RESULT(Channel*, float*)
    constexpr uintptr_t kChannelSetGroup = 0x008D0C10;     // what kAddToGroup calls
    constexpr int kFmodLowpass = 3, kFmodEcho = 6;         // FMOD_DSP_TYPE, as TranslateDspType (0x4CD9D0) maps the DBC's
    constexpr int kLowpassCutoff = 0, kLowpassResonance = 1;
    constexpr int kEchoDelay = 0, kEchoDecay = 1, kEchoDry = 3, kEchoWet = 4;

    inline constexpr storm::Guard kGuards[] = {
        // The play path: the properties' chain name becomes the sound's callback, which runs before the sound plays.
        {"sound kits: the properties' chain (+0x60 on, its name at +0x64) and DSP_AttachCustomDspChain as the callback", 0x004C70E2, 16,
         {0x83, 0x7B, 0x60, 0x00, 0x74, 0x10, 0x83, 0xC3, 0x64, 0x53, 0x68, 0x50, 0xE2, 0x4C, 0x00, 0x8B}},
        {"sound kits: handed to SESound::SetAttachCustomDSPChainCallback", 0x004C70F1, 7, {0x8B, 0xCF, 0xE8, 0xE8, 0x15, 0x3B, 0x00}},
        {"sound kit properties: their defaults leave the chain off (+0x60 = 0)", 0x004C5A09, 9,
         {0x89, 0x5E, 0x60, 0x89, 0x9E, 0xE4, 0x00, 0x00, 0x00}},
        {"sound engine: a new channel's chain callback (+0xC0, its name +0xC4)", 0x0087BA43, 16,
         {0x8B, 0x86, 0xC0, 0x00, 0x00, 0x00, 0x85, 0xC0, 0x74, 0x0F, 0x8B, 0x96, 0xC4, 0x00, 0x00, 0x00}},
        {"sound engine: called with (channel, name)", 0x0087BA53, 9, {0x8B, 0x0F, 0x52, 0x51, 0xFF, 0xD0, 0x83, 0xC4, 0x08}},
        {"sound engine: and then the sound plays", 0x0087BA6A, 7, {0x8B, 0xCE, 0xE8, 0xEF, 0xDC, 0xFF, 0xFF}},
        // The two detours.
        {"DSP_AttachCustomDspChain: entry and its chain-table lookup", 0x004CE250, 16,
         {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x0C, 0x56, 0x50, 0xB9, 0xB4, 0xAD, 0xB4, 0x00, 0xE8, 0x6E, 0x12}},
        {"DSP_ShutDown: entry", 0x004CDD40, 13, {0xA1, 0xC0, 0xAD, 0xB4, 0x00, 0xA8, 0x01, 0x75, 0x74, 0x85, 0xC0, 0x74, 0x70}},
        {"SI2::ShutDown: the chains go (DSP_ShutDown) ...", 0x004C627C, 5, {0xE8, 0xBF, 0x7A, 0x00, 0x00}},
        {"SI2::ShutDown: ... before the sound engine (SE2::ShutDown)", 0x004C6294, 6, {0x56, 0xE8, 0xF6, 0x51, 0x3B, 0x00}},
        // The client's DSP effects switch, as DSP_CreateDsp reads it.
        {"DSP_CreateDsp: the DSP effects cvar looked up once (flag 0xB4ADB0)", 0x004CDF76, 9, {0xF6, 0x05, 0xB0, 0xAD, 0xB4, 0x00, 0x01, 0x75, 0x19}},
        {"DSP_CreateDsp: kept at 0xB4ADAC, nothing made while its value (+0x30) is 0", 0x004CDF93, 16,
         {0xA3, 0xAC, 0xAD, 0xB4, 0x00, 0xA1, 0xAC, 0xAD, 0xB4, 0x00, 0x57, 0x33, 0xFF, 0x39, 0x78, 0x30}},
        {"the cvar's name: Sound_EnableDSPE...", 0x009F2554, 16,
         {0x53, 0x6F, 0x75, 0x6E, 0x64, 0x5F, 0x45, 0x6E, 0x61, 0x62, 0x6C, 0x65, 0x44, 0x53, 0x50, 0x45}},
        {"the cvar's name: ...ffects", 0x009F2564, 7, {0x66, 0x66, 0x65, 0x63, 0x74, 0x73, 0x00}},
        // The SE2 calls.
        {"SE2::IsInitialized", 0x00878CB0, 6, {0xA1, 0x14, 0x38, 0xD4, 0x00, 0xC3}},
        {"SE2::CreateDSPByType on its FMOD system", 0x00878D00, 16,
         {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x0C, 0x8B, 0x4D, 0x08, 0x8B, 0x15, 0x00, 0x38, 0xD4, 0x00, 0x50}},
        {"SE2::SetDspParam", 0x00878D20, 16,
         {0x55, 0x8B, 0xEC, 0xD9, 0x45, 0x10, 0x8B, 0x45, 0x0C, 0x51, 0x8B, 0x4D, 0x08, 0xD9, 0x1C, 0x24}},
        {"SE2::BypassDsp", 0x00878D40, 16,
         {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x0C, 0x8B, 0x4D, 0x08, 0x50, 0x51, 0xE8, 0x40, 0x7B, 0x05, 0x00}},
        {"SE2::EnableDSP", 0x00878D60, 16,
         {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x0C, 0x8B, 0x4D, 0x08, 0x50, 0x51, 0xE8, 0xE0, 0x7A, 0x05, 0x00}},
        {"SE2::DspDestroy", 0x00878D80, 16,
         {0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x75, 0x08, 0x56, 0xE8, 0x63, 0x7A, 0x05, 0x00, 0x56, 0xE8, 0xCD}},
        {"SE2::ConnectDspToChannelGroup", 0x00878DA0, 16,
         {0x55, 0x8B, 0xEC, 0x8B, 0x4D, 0x0C, 0x83, 0xEC, 0x08, 0x56, 0x8D, 0x45, 0xFC, 0x50, 0x51, 0xE8}},
        {"SE2::GetChannelGroupNumChannels", 0x00878DF0, 16,
         {0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x4D, 0x08, 0x8D, 0x45, 0xFC, 0x50, 0x51, 0xC7, 0x45, 0xFC, 0x00}},
        {"SE2::CreateChannelGroup on its FMOD system", 0x00878E10, 16,
         {0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x4D, 0x08, 0x8B, 0x15, 0x00, 0x38, 0xD4, 0x00, 0x8D, 0x45, 0xFC}},
        {"SE2::DestroyChannelGroup", 0x00878E40, 13, {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x50, 0xE8, 0xA4, 0x8D, 0x05, 0x00, 0x5D}},
        {"SE2::AddChannelToChannelGroup", 0x00878E50, 16,
         {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x0C, 0x8B, 0x4D, 0x08, 0x50, 0x51, 0xE8, 0xB0, 0x7D, 0x05, 0x00}},
        // FMOD's channel frequency, as SESound::SetPitch calls it.
        {"FMOD Channel::setFrequency", 0x008D0A00, 16,
         {0x55, 0x8B, 0xEC, 0x8B, 0x4D, 0x08, 0x8D, 0x45, 0x08, 0x50, 0x51, 0xE8, 0x50, 0xC3, 0x00, 0x00}},
        {"FMOD Channel::getFrequency", 0x008D0A30, 16,
         {0x55, 0x8B, 0xEC, 0x8B, 0x4D, 0x08, 0x8D, 0x45, 0x08, 0x50, 0x51, 0xE8, 0x20, 0xC3, 0x00, 0x00}},
        {"SESound::SetPitch: its getFrequency", 0x0087878B, 5, {0xE8, 0xA0, 0x82, 0x05, 0x00}},
        {"SESound::SetPitch: its setFrequency", 0x008787A0, 5, {0xE8, 0x5B, 0x82, 0x05, 0x00}},
        // The DSP types and their parameter indices, as the client's own chain building uses them.
        {"TranslateDspType: low-pass is FMOD type 3", 0x004CD9F3, 5, {0xB8, 0x03, 0x00, 0x00, 0x00}},
        {"TranslateDspType: echo is FMOD type 6", 0x004CDA01, 5, {0xB8, 0x06, 0x00, 0x00, 0x00}},
        {"TranslateDspType: its cases", 0x004CDA0C, 16,
         {0xE5, 0xD9, 0x4C, 0x00, 0xEC, 0xD9, 0x4C, 0x00, 0xF3, 0xD9, 0x4C, 0x00, 0xFA, 0xD9, 0x4C, 0x00}},
        {"TranslateDspType: its last case", 0x004CDA1C, 4, {0x01, 0xDA, 0x4C, 0x00}},
        {"DSP_CreateDsp: its cases", 0x004CE234, 16,
         {0x2F, 0xE0, 0x4C, 0x00, 0x61, 0xE0, 0x4C, 0x00, 0x90, 0xE0, 0x4C, 0x00, 0xBF, 0xE0, 0x4C, 0x00}},
        {"DSP_CreateDsp: its last cases", 0x004CE244, 8, {0x69, 0xE1, 0x4C, 0x00, 0xBF, 0xE1, 0x4C, 0x00}},
        {"DSP_CreateDsp: the low-pass's cutoff at 0", 0x004CE09A, 8, {0x6A, 0x00, 0x51, 0xE8, 0x7E, 0xAC, 0x3A, 0x00}},
        {"DSP_CreateDsp: the low-pass's resonance at 1", 0x004CE0AF, 8, {0x6A, 0x01, 0x52, 0xE8, 0x69, 0xAC, 0x3A, 0x00}},
        {"DSP_CreateDsp: the echo's delay at 0", 0x004CE173, 8, {0x6A, 0x00, 0x52, 0xE8, 0xA5, 0xAB, 0x3A, 0x00}},
        {"DSP_CreateDsp: the echo's decay at 1", 0x004CE188, 8, {0x6A, 0x01, 0x50, 0xE8, 0x90, 0xAB, 0x3A, 0x00}},
        {"DSP_CreateDsp: the echo's dry mix at 3", 0x004CE19D, 8, {0x6A, 0x03, 0x51, 0xE8, 0x7B, 0xAB, 0x3A, 0x00}},
        {"DSP_CreateDsp: the echo's wet mix at 4", 0x004CE1B2, 8, {0x6A, 0x04, 0x52, 0xE8, 0x66, 0xAB, 0x3A, 0x00}},
    };
}
