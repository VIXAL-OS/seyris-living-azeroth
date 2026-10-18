// Offline tests for env/ThunderDspModel (thunder's distance filters): the chain by distance, the chain names, the pool
// of chains and their lifetime with FMOD mocked, and -- given the client executable -- every guard and what the calls
// it names really call. Included by StormTests.cpp after its helpers (Test, CHECK, Near, Image, LoadImage, CheckGuards)
// and run from its main(): storm_tests [<Wow.exe>] [<SoundEntries.dbc>].
#pragma once

#include "env/ThunderDspModel.hpp"

namespace
{
    namespace td = wxl_livingazeroth::thunderdsp;

    // FMOD as the pool sees it: three tags per chain stand for its group, low-pass and echo. Checks every call it gets
    // against what the client would allow (a released chain is never touched again; a sound never joins a chain that's
    // off, where the group's mix would stop: silence).
    struct MockEngine final : td::Engine
    {
        bool ready = true;
        int  failCreateAt = -1;       // Create of this slot fails
        bool failConfigure = false, failEnable = false, failAttach = false;
        int  calls = 0, creates = 0, configures = 0, enables = 0, disables = 0, attaches = 0, destroys = 0;
        int  channels[td::kSlots] = {};
        bool alive[td::kSlots] = {};
        bool active[td::kSlots] = {};
        td::Params params[td::kSlots];
        float lastPitch = 0.0f;
        void* lastChannel = nullptr;
        uint8_t tags[td::kSlots][3] = {};

        int SlotOf(const void* p) const
        {
            for (unsigned i = 0; i < td::kSlots; ++i)
                for (int k = 0; k < 3; ++k)
                    if (p == &tags[i][k]) return static_cast<int>(i);
            return -1;
        }
        int Live(const void* group, const void* lowpass, const void* echo) const
        {
            const int i = SlotOf(group);
            CHECK(i >= 0 && alive[i] && (!lowpass || SlotOf(lowpass) == i) && (!echo || SlotOf(echo) == i));
            CHECK(!lowpass || lowpass == &tags[i][1]);
            return i;
        }

        bool Ready() override { ++calls; return ready; }
        bool Create(unsigned slot, void*& group, void*& lowpass, void*& echo) override
        {
            ++calls;
            CHECK(slot < td::kSlots && !alive[slot]);
            if (static_cast<int>(slot) == failCreateAt) { group = lowpass = echo = nullptr; return false; }
            ++creates;
            group = &tags[slot][0];
            lowpass = &tags[slot][1];
            echo = &tags[slot][2];
            alive[slot] = true;
            active[slot] = true; // as FMOD may leave a new DSP: the pool switches it off
            params[slot] = td::Params{};
            return true;
        }
        bool Configure(void* lowpass, void* echo, const td::Params& p) override
        {
            ++calls;
            const int i = SlotOf(lowpass);
            CHECK(i >= 0 && alive[i] && SlotOf(echo) == i && echo == &tags[i][2]);
            CHECK(channels[i] == 0); // never a chain with a sound in it
            if (failConfigure) return false;
            ++configures;
            params[i] = p;
            return true;
        }
        bool Enable(void* lowpass, void* echo, bool on) override
        {
            ++calls;
            const int i = SlotOf(lowpass);
            CHECK(i >= 0 && alive[i] && SlotOf(echo) == i);
            if (on && failEnable) return false;
            if (!on) CHECK(channels[i] == 0); // never off under a sound
            active[i] = on;
            ++(on ? enables : disables);
            return true;
        }
        int Channels(void* group) override
        {
            ++calls;
            return channels[Live(group, nullptr, nullptr)];
        }
        bool Attach(void* channel, void* group, float pitch) override
        {
            ++calls;
            const int i = Live(group, nullptr, nullptr);
            CHECK(active[i]); // never into a chain that's off
            if (failAttach) return false;
            ++attaches;
            ++channels[i];
            lastPitch = pitch;
            lastChannel = channel;
            return true;
        }
        void Destroy(void* group, void* lowpass, void* echo) override
        {
            ++calls;
            const int i = Live(group, lowpass, echo);
            alive[i] = false;
            active[i] = false;
            channels[i] = 0;
            ++destroys;
        }
    };

    td::Params Crack(float d) { return td::ParamsFor(td::Layer::Crack, d, 1.0f, 1.0f); }
    td::Params Rumble(float d) { return td::ParamsFor(td::Layer::Rumble, d, 1.0f, 1.0f); }

    // Chains for six different far layers, each with a sound in it.
    void FillAll(td::Pool& pool, MockEngine& e, float now, int base[td::kSlots])
    {
        for (unsigned i = 0; i < td::kSlots; ++i)
        {
            td::Refusal why;
            const td::Params p = td::ParamsFor(i % 2 ? td::Layer::Rumble : td::Layer::Crack, 300.0f + 300.0f * static_cast<float>(i), 1.0f, 1.0f);
            const td::Ticket t = pool.Acquire(e, p, now, why);
            CHECK(t.Valid() && why == td::Refusal::None);
            pool.Played(t, true);
            CHECK(pool.Attach(e, static_cast<unsigned>(t.slot), t.generation, p.pitch, &e, now) == td::Joined::Yes);
            base[i] = t.slot;
        }
    }

    // Every rel32 call or jump in the image's code section that lands on `target`.
    std::vector<uintptr_t> CallersOf(const Image& im, uintptr_t target)
    {
        std::vector<uintptr_t> out;
        for (const Image::Section& s : im.sections)
        {
            if (!(td::kAttachChain - im.base >= s.rva && td::kAttachChain - im.base < s.rva + s.rawSize)) continue; // .text
            const uint8_t* b = im.bytes.data() + s.raw;
            for (uint32_t k = 0; k + 5 <= s.rawSize && s.raw + k + 5 <= im.bytes.size(); ++k)
            {
                if (b[k] != 0xE8 && b[k] != 0xE9) continue;
                int32_t rel = 0;
                std::memcpy(&rel, b + k + 1, 4);
                if (im.base + s.rva + k + 5 + static_cast<uintptr_t>(rel) == target) out.push_back(im.base + s.rva + k);
            }
        }
        return out;
    }

    uintptr_t CallTarget(const Image& im, uintptr_t at)
    {
        const auto* c = static_cast<const uint8_t*>(im.At(at, 5));
        CHECK(c && (c[0] == 0xE8 || c[0] == 0xE9));
        int32_t rel = 0;
        std::memcpy(&rel, c + 1, 4);
        return at + 5 + static_cast<uintptr_t>(rel);
    }
    uint32_t Dword(const Image& im, uintptr_t at)
    {
        const auto* c = static_cast<const uint8_t*>(im.At(at, 4));
        CHECK(c);
        uint32_t v = 0;
        std::memcpy(&v, c, 4);
        return v;
    }

    void ThunderDspTests(int argc, char** argv)
    {
        Test("thunder filters by distance: near cracks crisp; the low-pass 20 kHz to about 900 Hz far, pitch 1 to 0.88; echo 150-400 ms x 0.3-0.5 mid and far; the rumble rolls longer", []
        {
            for (float d : { 0.0f, 60.0f, 100.0f, 149.0f, 150.0f })
                CHECK(td::Plain(Crack(d))); // a near crack: no chain at all
            CHECK(!Crack(200.0f).echo && Crack(200.0f).lowpass && Crack(200.0f).cutoff > 12000.0f); // muffled a little, no echo yet
            CHECK(Crack(250.0f).echo && Crack(250.0f).wet > 0.05f);                                  // mid: rolling
            float lastCutoff = 1e9f, lastPitch = 2.0f, lastDelay = 0.0f, lastDecay = 0.0f, lastWet = 0.0f;
            for (float d = 150.0f; d <= 2600.0f; d += 25.0f)
            {
                const td::Params c = Crack(d), r = Rumble(d);
                CHECK(c.cutoff <= lastCutoff && c.pitch <= lastPitch && c.cutoff >= td::kFarCutoff - 1.0f && c.pitch >= 0.88f - 1e-5f);
                lastCutoff = c.cutoff;
                lastPitch = c.pitch;
                if (d >= 250.0f)
                {
                    CHECK(c.echo && c.delayMs >= 150.0f && c.delayMs <= 400.0f && c.decay >= 0.3f - 1e-6f && c.decay <= 0.5f + 1e-6f);
                    CHECK(c.delayMs >= lastDelay && c.decay >= lastDecay && c.wet >= lastWet && c.dry == 1.0f);
                    lastDelay = c.delayMs;
                    lastDecay = c.decay;
                    lastWet = c.wet;
                }
                // The rumble: the same muffling and pitch, a longer roll at every distance.
                CHECK(r.echo && r.cutoff == c.cutoff && r.pitch == c.pitch && td::TailSeconds(r) > td::TailSeconds(c));
                if (c.echo) CHECK(r.delayMs > c.delayMs && r.decay >= c.decay);
            }
            const td::Params farthest = Crack(2000.0f);
            CHECK(Near(farthest.cutoff, 900.0f, 1.0f) && farthest.cutoff >= 600.0f && farthest.cutoff <= 1200.0f);
            CHECK(Near(farthest.pitch, 0.88f, 1e-4f) && farthest.pitch >= 0.85f && farthest.pitch <= 0.9f);
            CHECK(Near(farthest.delayMs, 400.0f, 0.01f) && Near(farthest.decay, 0.5f, 1e-5f) && Near(farthest.wet, 0.45f, 1e-5f));
            CHECK(Crack(2500.0f).cutoff == farthest.cutoff && Crack(2500.0f).pitch == farthest.pitch);
            const td::Params rumbleNear = Rumble(60.0f);
            CHECK(rumbleNear.echo && !rumbleNear.lowpass && rumbleNear.pitch == 1.0f && Near(rumbleNear.delayMs, 250.0f, 0.01f));
            // The settings: strength 0 no low-pass and no pitch, the echo as it was; EchoAmount 0 no echo (a near rumble
            // then plain); strength 2 twice as far, never below 250 Hz or a pitch of 0.75; nonsense plain.
            for (float d : { 300.0f, 900.0f, 2000.0f })
            {
                const td::Params none = td::ParamsFor(td::Layer::Crack, d, 0.0f, 1.0f);
                CHECK(!none.lowpass && none.pitch == 1.0f && none.echo && none.delayMs == Crack(d).delayMs);
                CHECK(!td::ParamsFor(td::Layer::Rumble, d, 1.0f, 0.0f).echo);
                const td::Params twice = td::ParamsFor(td::Layer::Crack, d, 2.0f, 2.0f);
                CHECK(twice.cutoff < Crack(d).cutoff && twice.cutoff >= td::kMinCutoff && twice.pitch >= td::kMinPitch && twice.wet <= 1.0f);
                CHECK(twice.wet > Crack(d).wet);
            }
            CHECK(td::Plain(td::ParamsFor(td::Layer::Rumble, 100.0f, 1.0f, 0.0f)));
            CHECK(td::ParamsFor(td::Layer::Crack, 2000.0f, 2.0f, 1.0f).cutoff == td::kMinCutoff);
            CHECK(td::Plain(Crack(std::nanf(""))) && td::Plain(Rumble(-5.0f)) && td::Plain(Rumble(INFINITY)));
            CHECK(td::Plain(td::ParamsFor(td::Layer::Crack, 2000.0f, std::nanf(""), std::nanf(""))));
            std::printf("  yd    crack: low-pass  pitch  echo                rumble: echo               rings\n");
            for (float d : { 100.0f, 250.0f, 400.0f, 700.0f, 1000.0f, 1600.0f, 2000.0f })
            {
                const td::Params c = Crack(d), r = Rumble(d);
                std::printf("  %4.0f  %8.0f Hz  %.3f  %3.0f ms x %.2f w %.2f   %3.0f ms x %.2f w %.2f   %.1f / %.1f s\n", d, c.lowpass ? c.cutoff : 0.0f,
                            c.pitch, c.echo ? c.delayMs : 0.0f, c.echo ? c.decay : 0.0f, c.wet, r.delayMs, r.decay, r.wet, td::TailSeconds(c), td::TailSeconds(r));
            }
        });

        Test("thunder filters: chains share within a hair; an echo rings to -60 dB, never past 10 s", []
        {
            CHECK(td::SameChain(Crack(1000.0f), Crack(1010.0f)) && !td::SameChain(Crack(1000.0f), Crack(1600.0f)));
            CHECK(!td::SameChain(Crack(1000.0f), Rumble(1000.0f)) && td::SameChain(Rumble(800.0f), Rumble(805.0f)));
            CHECK(!td::SameChain(Crack(200.0f), Crack(300.0f)));                // one with an echo, one without
            CHECK(td::TailSeconds(td::Params{}) == td::kQuietAfter && td::TailSeconds(Crack(170.0f)) == td::kQuietAfter);
            td::Params p;
            p.echo = true;
            p.delayMs = 450.0f;
            p.decay = 0.6f;
            p.wet = 0.5f;
            const float repeats = std::log(0.001f / 0.5f) / std::log(0.6f);
            CHECK(Near(td::TailSeconds(p), 0.45f * (repeats + 1.0f) + td::kQuietAfter, 1e-3f));
            p.delayMs = 5000.0f;
            p.decay = 1.0f;
            p.wet = 1.0f;
            CHECK(td::TailSeconds(p) == td::kMaxTail);
            p.decay = std::nanf("");
            CHECK(td::TailSeconds(p) <= td::kMaxTail && td::TailSeconds(p) > 0.0f);
        });

        Test("thunder filters: chain names say which chain, which setting of it and the pitch; others' names aren't ours", []
        {
            char name[td::kChainNameSize];
            CHECK(td::FormatName(name, sizeof(name), 3, 41, 0.913f) && std::strcmp(name, "LivingAzeroth thunder 3.41.913") == 0);
            unsigned slot = 9;
            uint32_t generation = 0;
            float pitch = 0.0f;
            CHECK(td::ParseName(name, slot, generation, pitch) && slot == 3 && generation == 41 && Near(pitch, 0.913f, 1e-6f));
            CHECK(td::FormatName(name, sizeof(name), 5, 0xFFFFFFFFu, 1.0f) && std::strlen(name) < td::kChainNameSize);
            CHECK(td::ParseName(name, slot, generation, pitch) && slot == 5 && generation == 0xFFFFFFFFu && pitch == 1.0f);
            CHECK(td::FormatName(name, sizeof(name), 0, 7, 0.5f) && td::ParseName(name, slot, generation, pitch) && pitch == td::kMinPitch);
            CHECK(!td::FormatName(name, 20, 1, 1, 1.0f) && name[0] == '\0');   // doesn't fit: nothing
            CHECK(!td::FormatName(name, sizeof(name), td::kSlots, 1, 1.0f));
            for (const char* other : { "Death Knight Human Male", "Death Knight Scourge Female", "", "LivingAzeroth", "LivingAzeroth thunderX 1.2.900" })
                CHECK(!td::IsOurName(other) && !td::ParseName(other, slot, generation, pitch));
            CHECK(!td::IsOurName(nullptr) && !td::ParseName(nullptr, slot, generation, pitch));
            for (const char* bad : { "LivingAzeroth thunder ", "LivingAzeroth thunder 3.41", "LivingAzeroth thunder 6.1.900", "LivingAzeroth thunder 3.41.913x",
                                     "LivingAzeroth thunder 3.41.1001", "LivingAzeroth thunder 3.41.749", "LivingAzeroth thunder 3..913",
                                     "LivingAzeroth thunder 3.99999999999.913", "LivingAzeroth thunder 3.4294967296.913", "LivingAzeroth thunder -3.1.900" })
                CHECK(td::IsOurName(bad) && !td::ParseName(bad, slot, generation, pitch)); // ours, malformed: never the client's lookup
        });

        Test("thunder filters, FMOD mocked: a near crack makes nothing; the chains made once and off; shared when set alike; an idle one set anew", []
        {
            MockEngine e;
            td::Pool pool;
            td::Refusal why;
            CHECK(!pool.Acquire(e, Crack(100.0f), 0.0f, why).Valid() && why == td::Refusal::Plain && e.calls == 0);
            e.ready = false;
            CHECK(!pool.Acquire(e, Crack(800.0f), 0.0f, why).Valid() && why == td::Refusal::NotReady && e.creates == 0);
            e.ready = true;
            const td::Params mid = Crack(800.0f);
            const td::Ticket a = pool.Acquire(e, mid, 1.0f, why);
            CHECK(a.Valid() && why == td::Refusal::None && pool.created && e.creates == static_cast<int>(td::kSlots));
            CHECK(e.disables == static_cast<int>(td::kSlots) && e.configures == 1);
            for (bool on : e.active) CHECK(!on); // all off until a sound joins
            pool.Played(a, true);
            int channel = 0;
            CHECK(pool.Attach(e, static_cast<unsigned>(a.slot), a.generation, mid.pitch, &channel, 1.0f) == td::Joined::Yes);
            CHECK(e.active[a.slot] && e.attaches == 1 && e.lastChannel == &channel && e.lastPitch == mid.pitch && e.channels[a.slot] == 1);
            CHECK(pool.slot[a.slot].pending == 0 && pool.Busy() == 1);
            // The same layer again: the same chain, not set again.
            const td::Ticket b = pool.Acquire(e, Crack(805.0f), 1.5f, why);
            CHECK(b.slot == a.slot && b.generation == a.generation && e.configures == 1 && pool.stats.shared == 1);
            CHECK(pool.Attach(e, static_cast<unsigned>(b.slot), b.generation, 0.95f, &channel, 1.5f) == td::Joined::Yes);
            CHECK(e.enables == 1 && e.channels[a.slot] == 2 && e.lastPitch == 0.95f); // already on; the pitch per sound
            // Its rumble: another chain.
            const td::Ticket c = pool.Acquire(e, Rumble(800.0f), 1.5f, why);
            CHECK(c.Valid() && c.slot != a.slot && e.configures == 2 && e.params[c.slot].delayMs == Rumble(800.0f).delayMs);
            pool.Played(c, false); // declined (too many sounds): its reservation back at once
            CHECK(pool.slot[c.slot].pending == 0);
        });

        Test("thunder filters, FMOD mocked: never a busy or ringing chain set anew; plain when none is free; off only after its echo's tail", []
        {
            MockEngine e;
            td::Pool pool;
            td::Refusal why;
            int slotOf[td::kSlots];
            FillAll(pool, e, 10.0f, slotOf);
            const int configured = e.configures;
            CHECK(!pool.Acquire(e, Crack(150.5f + 2400.0f), 10.0f, why).Valid() && why == td::Refusal::Busy && pool.stats.busy == 1);
            // A layer like one playing still shares that chain.
            CHECK(pool.Acquire(e, td::ParamsFor(td::Layer::Crack, 300.0f, 1.0f, 1.0f), 10.0f, why).Valid());
            pool.Played({ slotOf[0], pool.slot[slotOf[0]].generation }, false);
            // Every sound ends (or FMOD stole it): out of their groups.
            for (int& n : e.channels) n = 0;
            pool.Update(e, 10.1f);
            for (const td::Slot& s : pool.slot) CHECK(s.channels == 0 && s.active); // ringing
            CHECK(!pool.Acquire(e, Crack(2450.0f), 10.2f, why).Valid() && why == td::Refusal::Busy && e.configures == configured);
            // Past the shortest tail one chain is off and free; the longest rings on.
            float shortest = 1e9f, longest = 0.0f;
            for (const td::Slot& s : pool.slot) { shortest = std::fmin(shortest, td::TailSeconds(s.params)); longest = std::fmax(longest, td::TailSeconds(s.params)); }
            CHECK(longest > shortest + 0.5f);
            pool.Update(e, 10.0f + shortest + 0.01f);
            int off = 0;
            for (unsigned i = 0; i < td::kSlots; ++i) off += e.active[i] ? 0 : 1;
            CHECK(off >= 1 && off < static_cast<int>(td::kSlots) && e.disables == static_cast<int>(td::kSlots) + off);
            const td::Ticket t = pool.Acquire(e, Crack(2450.0f), 10.0f + shortest + 0.02f, why);
            CHECK(t.Valid() && !e.active[t.slot] && e.configures == configured + 1 && pool.slot[t.slot].params.cutoff == Crack(2450.0f).cutoff);
            pool.Update(e, 10.0f + longest + 0.01f);
            for (unsigned i = 0; i < td::kSlots; ++i) CHECK(!e.active[i]);
        });

        Test("thunder filters, FMOD mocked: lifetime: a load that never completes, a sound too late for its chain, a join inside the play", []
        {
            MockEngine e;
            td::Pool pool;
            td::Refusal why;
            const td::Params p = Crack(900.0f);
            // A load that never completes: its reservation given up after kPendingSeconds.
            const td::Ticket t = pool.Acquire(e, p, 0.0f, why);
            pool.Played(t, true);
            pool.Update(e, 1.0f);
            CHECK(pool.slot[t.slot].pending == 1);
            pool.Update(e, td::kPendingSeconds + 0.1f);
            CHECK(pool.slot[t.slot].pending == 0 && pool.stats.expired == 1);
            // Its chain set for other params meanwhile (every other chain busy): the late sound stays unfiltered.
            for (unsigned i = 0; i < td::kSlots; ++i)
                if (static_cast<int>(i) != t.slot) pool.slot[i].channels = e.channels[i] = 1;
            const td::Params other = Rumble(1500.0f);
            const float later = td::kPendingSeconds + 0.2f + td::TailSeconds(p);
            const td::Ticket u = pool.Acquire(e, other, later, why);
            CHECK(u.slot == t.slot && u.generation > t.generation);
            const int attaches = e.attaches, calls = e.calls;
            CHECK(pool.Attach(e, static_cast<unsigned>(t.slot), t.generation, p.pitch, &e, later) == td::Joined::Stale);
            CHECK(e.attaches == attaches && e.calls == calls && pool.stats.stale == 1);
            CHECK(pool.slot[u.slot].pending == 1);      // the new play's reservation untouched
            // A join inside the play (a file already loaded): before Played, which then leaves it be.
            CHECK(pool.Attach(e, static_cast<unsigned>(u.slot), u.generation, other.pitch, &e, later) == td::Joined::Yes);
            pool.Played(u, true);
            CHECK(pool.slot[u.slot].pending == 0 && pool.slot[u.slot].channels == 1);
            // Names that were never handed out: nothing.
            CHECK(pool.Attach(e, td::kSlots, u.generation, 1.0f, &e, later) == td::Joined::Stale);
            CHECK(pool.Attach(e, static_cast<unsigned>(u.slot), u.generation + 1, 1.0f, &e, later) == td::Joined::Stale);
        });

        Test("thunder filters, FMOD mocked: released once with the sound system, never touched after; made anew with new settings' numbers", []
        {
            MockEngine e;
            td::Pool pool;
            td::Refusal why;
            const td::Ticket t = pool.Acquire(e, Crack(1200.0f), 0.0f, why);
            pool.Played(t, true);
            CHECK(pool.Attach(e, static_cast<unsigned>(t.slot), t.generation, 0.9f, &e, 0.0f) == td::Joined::Yes);
            pool.Release(e);
            CHECK(e.destroys == static_cast<int>(td::kSlots) && !pool.created && pool.stats.released == 1);
            for (bool alive : e.alive) CHECK(!alive);
            const int calls = e.calls;
            pool.Update(e, 1.0f);                                       // nothing to look after
            CHECK(pool.Attach(e, static_cast<unsigned>(t.slot), t.generation, 0.9f, &e, 1.0f) == td::Joined::Stale);
            pool.Release(e);                                            // twice: nothing more
            pool.Forget();
            CHECK(e.calls == calls && e.destroys == static_cast<int>(td::kSlots));
            const td::Ticket again = pool.Acquire(e, Crack(1200.0f), 2.0f, why); // a new sound system
            CHECK(again.Valid() && e.creates == 2 * static_cast<int>(td::kSlots) && again.generation > t.generation);
            CHECK(pool.Attach(e, static_cast<unsigned>(t.slot), t.generation, 0.9f, &e, 2.0f) == td::Joined::Stale);
        });

        Test("thunder filters, FMOD mocked: failures leave thunder unfiltered, never silent, nothing half made", []
        {
            {
                MockEngine e;
                e.failCreateAt = 3;
                td::Pool pool;
                td::Refusal why;
                CHECK(!pool.Acquire(e, Crack(900.0f), 0.0f, why).Valid() && why == td::Refusal::CreateFailed);
                CHECK(e.creates == 3 && e.destroys == 3 && !pool.created && pool.createFailed);
                for (bool alive : e.alive) CHECK(!alive);
                e.failCreateAt = -1;
                CHECK(!pool.Acquire(e, Crack(900.0f), 1.0f, why).Valid() && why == td::Refusal::CreateFailed && e.creates == 3); // not again ...
                pool.Release(e);
                CHECK(pool.Acquire(e, Crack(900.0f), 2.0f, why).Valid());                                                      // ... until a restart
            }
            {
                MockEngine e;
                td::Pool pool;
                td::Refusal why;
                const td::Ticket t = pool.Acquire(e, Crack(900.0f), 0.0f, why);
                e.failEnable = true;
                CHECK(pool.Attach(e, static_cast<unsigned>(t.slot), t.generation, 1.0f, &e, 0.0f) == td::Joined::EnableFailed);
                CHECK(e.attaches == 0 && pool.stats.failedJoins == 1); // not into a chain that's off
                e.failEnable = false;
                e.failAttach = true;
                const td::Ticket u = pool.Acquire(e, Crack(900.0f), 0.1f, why);
                CHECK(pool.Attach(e, static_cast<unsigned>(u.slot), u.generation, 1.0f, &e, 0.1f) == td::Joined::AttachFailed);
                e.failAttach = false;
                e.failConfigure = true;
                const td::Ticket v = pool.Acquire(e, Rumble(1900.0f), 0.2f, why);
                CHECK(!v.Valid() && why == td::Refusal::ConfigureFailed);
                for (const td::Slot& s : pool.slot) CHECK(!s.configured || td::SameChain(s.params, Crack(900.0f)));
                e.failConfigure = false;
                CHECK(pool.Acquire(e, Rumble(1900.0f), 0.3f, why).Valid());
                e.ready = false;                                       // the sound engine down: nothing called but Ready
                const int calls = e.calls;
                pool.Update(e, 1.0f);
                CHECK(e.calls == calls + 1);
            }
        });

        if (argc > 1)
        {
            const std::string name = std::string("thunder filters: guards against ") + argv[1] + ", and what the calls they name call";
            Test(name.c_str(), [&]
            {
                const Image im = LoadImage(argv[1]);
                CHECK(im.base == 0x00400000u && im.timestamp == 0x4C2452FEu);
                CheckGuards(im, td::kGuards);
                // The play path: the callback PlaySoundKit hands over is DSP_AttachCustomDspChain, through
                // SetAttachCustomDSPChainCallback (0x8786E0), before CompleteNonBlockingLoad (0x87BF00); nothing calls it
                // directly, so a sound's chain callback is the only way in.
                CHECK(Dword(im, 0x004C70ED) == td::kAttachChain && CallTarget(im, 0x004C70F3) == 0x008786E0 && CallTarget(im, 0x004C70FA) == 0x0087BF00);
                CHECK(CallersOf(im, td::kAttachChain).empty());
                CHECK(CallTarget(im, 0x0087BA6C) == 0x00879760);                     // ... and only then the play
                // Every SE2::ShutDown (which releases the FMOD system) comes right after a DSP_ShutDown.
                const std::vector<uintptr_t> down = CallersOf(im, td::kSoundShutDown), chains = CallersOf(im, td::kChainsShutDown);
                CHECK(!down.empty() && down.size() == chains.size());
                for (uintptr_t at : down)
                {
                    bool before = false;
                    for (uintptr_t c : chains) before = before || (c < at && at - c <= 0x40);
                    CHECK(before);
                }
                CHECK(CallTarget(im, 0x004C627C) == td::kChainsShutDown && CallTarget(im, 0x004C6295) == td::kSoundShutDown);
                // The pitch: SESound::SetPitch calls exactly these; the group join is FMOD's Channel::setChannelGroup.
                CHECK(CallTarget(im, 0x0087878B) == td::kChannelGetFrequency && CallTarget(im, 0x008787A0) == td::kChannelSetFrequency);
                CHECK(CallTarget(im, 0x00878E5B) == td::kChannelSetGroup);
                // The SE2 calls work on the FMOD system at 0xD43800, and IsInitialized's flag is the one the listener checks.
                CHECK(Dword(im, td::kCreateDspByType + 11) == td::kSoundSystem && Dword(im, td::kCreateGroup + 9) == td::kSoundSystem);
                CHECK(Dword(im, td::kIsInitialized + 1) == 0x00D43814u && Dword(im, 0x00879355) == 0x00D43814u);
                // The client's DSP effects switch: the cvar DSP_CreateDsp looks up by that name, kept where we read it.
                CHECK(Dword(im, 0x004CDF87) == 0x009F2554u && CallTarget(im, 0x004CDF8B) == 0x00767440u);
                CHECK(Dword(im, 0x004CDF78) == td::kDspEffectsLookedUp && Dword(im, 0x004CDF94) == td::kDspEffectsCVar &&
                      Dword(im, 0x004CDF99) == td::kDspEffectsCVar);
                CHECK(static_cast<const uint8_t*>(im.At(0x004CDFA2, 1))[0] == td::kCVarIntValue);
                CHECK(std::memcmp(im.At(0x009F2554, 23), "Sound_EnableDSPEffects", 23) == 0);
                // The DSP types: the DBC's low-pass (3) and echo (5) are FMOD's 3 and 6.
                CHECK(Dword(im, 0x004CDA0C + 4 * 2) == 0x004CD9F3u && Dword(im, 0x004CDA0C + 4 * 4) == 0x004CDA01u);
                CHECK(Dword(im, 0x004CD9F3 + 1) == static_cast<uint32_t>(td::kFmodLowpass) && Dword(im, 0x004CDA01 + 1) == static_cast<uint32_t>(td::kFmodEcho));
                // Their parameters, as the client's own chain building sets them through SE2::SetDspParam.
                CHECK(Dword(im, 0x004CE234 + 4 * 2) == 0x004CE090u && Dword(im, 0x004CE234 + 4 * 4) == 0x004CE169u);
                const struct { uintptr_t at; int index; } sets[] = {
                    { 0x004CE09A, td::kLowpassCutoff }, { 0x004CE0AF, td::kLowpassResonance }, { 0x004CE173, td::kEchoDelay },
                    { 0x004CE188, td::kEchoDecay }, { 0x004CE19D, td::kEchoDry }, { 0x004CE1B2, td::kEchoWet },
                };
                for (const auto& s : sets)
                {
                    const auto* b = static_cast<const uint8_t*>(im.At(s.at, 2));
                    CHECK(b && b[0] == 0x6A && b[1] == s.index && CallTarget(im, s.at + 3) == td::kSetDspParam);
                }
                std::printf("  %zu guards\n", sizeof(td::kGuards) / sizeof(td::kGuards[0]));
            });
        }
        else std::printf("SKIP thunder filters' guards (no Wow.exe given)\n");

        if (argc > 2)
        {
            Test("thunder filters: no thunder kit has a SoundEntriesAdvanced row (PlaySoundKit's advanced path would drop the chain)", [&]
            {
                FILE* f = std::fopen(argv[2], "rb");
                CHECK(f);
                std::vector<uint8_t> b;
                for (int c; (c = std::fgetc(f)) != EOF;) b.push_back(static_cast<uint8_t>(c));
                std::fclose(f);
                CHECK(b.size() > 20 && std::memcmp(b.data(), "WDBC", 4) == 0);
                uint32_t h[5];
                std::memcpy(h, b.data(), 20);
                CHECK(h[2] == 30 && h[3] == 120 && 20ull + uint64_t(h[1]) * h[3] <= b.size());
                for (const wxl_livingazeroth::storm::KitInfo& k : wxl_livingazeroth::storm::kKits)
                {
                    bool found = false;
                    for (uint32_t i = 0; i < h[1]; ++i)
                    {
                        uint32_t r[30];
                        std::memcpy(r, b.data() + 20 + size_t(i) * h[3], sizeof(r));
                        if (static_cast<int32_t>(r[0]) != k.soundKitId) continue;
                        found = true;
                        CHECK(r[29] == 0);
                    }
                    CHECK(found);
                }
            });
        }
        else std::printf("SKIP thunder filters' kits (no SoundEntries.dbc given)\n");
    }
}
