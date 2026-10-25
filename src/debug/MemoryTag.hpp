// Which Living Azeroth feature an allocation is for (2026-10-25, before HD texture packs: "do we have sufficient logging to
// catch this if it happens and figure out what costs the most memory?"). Every C++ allocation the module makes is charged
// to the tag open on its thread (debug/MemoryUse.cpp replaces operator new in this DLL only), and given back to it when
// freed. A Scope opens one for its lifetime; the frame profiler's scopes open their feature's too. Header-only, so code that
// is tested without debug/MemoryUse.cpp still builds (its tags are then never read).
#pragma once

#include <cstdint>

namespace wxl_livingazeroth::memuse
{
    enum Tag : uint8_t
    {
        kOther,        // allocated outside every scope (startup, the client's hooks without one)
        kTables,       // the cdbc tables read at the first frame (wind, grass, cover, climate, fog, lights, windows, glass, crystals)
        kEnv,          // the world snapshot, climate, wind, fields, storms, actors, regional
        kFog,          // the fog field, the ground fog, the fog wake, the far fog
        kLights,       // our point lights: the scan, the grid, the models' and the terrain's light textures
        kSpellLights,  // spell lights
        kGlass,        // window glass and its coloured shafts
        kSeeThrough,   // see-through windows
        kWindows,      // lit windows' lights and glow
        kCover,        // surface cover
        kGrass,        // grass: instanced, motion, doodads, density
        kIce,          // clear ice and crystals
        kTreeShadows,  // tree shadows
        kShafts,       // sun shafts
        kRender,       // anti-aliasing, the scene depth, the player's depth, shader patches
        kPanels,       // the panels, snapshots, the profiler and this readout
        kTagCount
    };

    inline const char* TagName(int tag)
    {
        static const char* const kNames[kTagCount] = { "other (untagged)", "cdbc tables", "climate, wind, fields, storms", "fog",
                                                       "lights", "spell lights", "glass", "see-through", "window lights and glow",
                                                       "surface cover", "grass", "ice and crystals", "tree shadows", "sun shafts",
                                                       "anti-aliasing, depth, shader patches", "panels and readouts" };
        return tag >= 0 && tag < kTagCount ? kNames[tag] : "?";
    }

    /// The tag open on this thread (kOther outside every scope).
    inline thread_local uint8_t t_tag = kOther;

    /// Charges this thread's allocations to `tag` while it lives (nested scopes restore the outer one); a negative tag
    /// leaves the open one as it is.
    struct Scope
    {
        uint8_t prev;
        explicit Scope(int tag) : prev(t_tag)
        {
            if (tag >= 0 && tag < kTagCount) t_tag = static_cast<uint8_t>(tag);
        }
        ~Scope() { t_tag = prev; }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };
}
