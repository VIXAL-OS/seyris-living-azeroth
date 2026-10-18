// Thunder through distance filters (experimental): a low-pass, a slight pitch-down and an echo on each thunder layer by
// its strike's distance, through the client's own FMOD Ex (the rules and the path: env/ThunderDspModel). env/Storm asks
// for a chain before it plays a layer through the client's sound kits and puts its name in the kit's properties; the
// client hands the sound's new channel to SI2::DSP_AttachCustomDspChain, where our detour joins it to that chain. Any
// check that fails, any fault, no free chain: the layer plays unfiltered, as before, and the log says why.
#pragma once

#include "ThunderDspModel.hpp"

#include "wxl/PluginApi.h"

namespace wxl_livingazeroth::thunderdsp
{
    struct Settings
    {
        int   filters = 1;         // ThunderFilters: thunder through the distance filters
        float strength = 1.0f;     // FilterStrength: x how far the low-pass and the pitch-down go (0 = none, 0..2)
        float echoAmount = 1.0f;   // EchoAmount: x the echo's repeats (0 = none, 0..2)
    };
    Settings& Config();

    /// The guards, then the detours on DSP_AttachCustomDspChain and DSP_ShutDown. From WXL_Load.
    void Install(const WXL_Api* api);

    /// Once per frame, main thread: the chains' upkeep (sounds counted, quiet chains switched off).
    void Update(float dt);

    /// One thunder layer about to play through PlaySoundKit: `name` gets its chain's name for the kit's properties
    /// (filtered), or stays empty (plain: near, filters off or unavailable, no chain free). `want`: the filters for this
    /// one (the setting, or the panel's audition).
    struct Use
    {
        bool   filtered = false;
        Ticket ticket;
        Params params;
    };
    Use Prepare(Layer layer, float distanceYards, bool want, char* name, size_t size);
    /// After PlaySoundKit: playing = it returned 0 (else its chain is given back).
    void Played(const Use& use, bool playing);

    /// The chain a layer at this distance gets with the current settings (the panel).
    Params Preview(Layer layer, float distanceYards);

    struct Status
    {
        unsigned filtered = 0;     // layers played with a chain
        unsigned plain = 0;        // layers near enough (or settings low enough) to need none
        unsigned unavailable = 0;  // layers that wanted one while the filters were off (checks, a fault) or the sound was down
        int      chains = 0;       // chains made (0 until the first filtered layer)
        int      busyChains = 0;   // ... with a sound in them or on its way
        PoolStats pool;            // joins, shares, stale, busy, ...
    };
    const Status& GetStatus();
    const char* StatusText();      // available, or off and why
}
