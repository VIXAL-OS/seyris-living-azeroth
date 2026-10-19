// The cloud deck's tint of what the client draws after it (features/GroundFog), kept apart so tests/storm can check it
// offline: which draws take which fog colour, and the client code that tells them apart.
//
// Blizzard's own storm doodads stand in the Storm Peaks' cloud heights: the Temple of Storms' lightning
// (stormpeaks\lightning\stormpeakslightning01 and 02: all eight placed are within 260 yd of the temple, at z 1450..2220;
// the deck there is 1040..1950) and Ulduar's storm clouds (stormpeaks\fog\stormpeaks_ulduar_clouds*). Their batches are
// alpha blended, unlit and fogged, so the client fogs them toward its own fog colour (#002D41 on a Northrend night).
// (The temple's other bolts, blastedlandslightningbolt01's particles, are additive: they fog to black and only fade.)
// They're drawn after the deck, and came out as dark bolts on the white cloud ("are some lightning strikes at the
// Temple of Storms still loading in black?", the user, 2026-10-19, flying below the deck at night under a clear sky).
//
// The swap of 2026-10-13 (the pixel constant cache's c2, the batch's fog colour, swapped right before the draw) gave
// them the stock colour moved toward the deck's by how overcast the sky is. Under a clear or broken sky that is the
// stock night colour, or nearly: the swap changed nothing there. Since 2026-10-13 23:30 the swap runs whenever the deck
// is drawn, from any camera height, and the colour it compares with is the one the client uploads: kFogColor is the
// day-night band 8 itself (0xD38BD4 + 8 x 4), the storm and snow shading write that band in place before the fog update
// reads it (0x7F171A), and the batch's fog setup uploads it (below). So neither the window nor the match was at fault.
//
// Now the storm doodads are known by name (the M2 batch draw's model) and by the fog the client gives the batch, not
// by its colour, and take the deck's colour as it shows from where the camera is (its underside from below, its top
// from above), at any sky. Everything else drawn after the deck keeps the colour match and the overcast-weighted
// colour it had.
#pragma once

#include "../env/StormModel.hpp" // storm::Guard

#include <cstddef>
#include <cstdint>

namespace wxl_livingazeroth::cloudtint
{
    // --- the client's fog for an M2 batch (Wow.exe 3.3.5a 12340, static disassembly, 2026-10-19) ----------------------
    // Both M2 batch draws (M2.DrawBatch 0x8203B0, M2.DrawBatchDoodad 0x820AE0) call the batch's material setup 0x81FB10
    // with their draw context before they draw. It looks the material's blend mode up in 0xA45390 (opaque, alpha key and
    // alpha: 1; the two additive modes: 2; mod: 3; mod2x: 4; the eighth: 0), 0 for an unfogged material, and stores the
    // result at the context's +0x80. Mode 1 takes the scene's fog colour (the model lighting's, its +0xB8 floats, as bytes),
    // 2 black, 3 white, 4 grey (0x80); then 0x873210 uploads it, x 1/255, as pixel constant c2 (core: kPsConstCache).
    constexpr int kFogUnknown = -1; // not in an M2 batch, or the guards below differ
    constexpr int kFogNone = 0, kFogScene = 1, kFogBlack = 2, kFogWhite = 3, kFogGrey = 4;
    constexpr size_t kCtxFogMode = 0x80; // M2 batch draw context -> the batch's fog mode (int)
    // The M2 batch draw context -> its instance (+0x60), the shared model (+0x2C), its path stem inline (+0x3C; as
    // features/ModelIce reads it).
    constexpr size_t kCtxInstance = 0x60, kInstShared = 0x2C, kSharedStem = 0x3C, kStemCapacity = 0x150 - 0x3C;

    inline constexpr storm::Guard kGuards[] = {
        {"M2 batch material setup: it stores the fog mode at the context's +0x80", 0x0081FB88, 6, {0x89, 0x86, 0x80, 0x00, 0x00, 0x00}},
        {"M2 batch material setup: fog modes 1..4 are the scene's colour, black, white, grey", 0x0081FE7C, 16,
         {0xAA, 0xFB, 0x81, 0x00, 0xAE, 0xFC, 0x81, 0x00, 0xC2, 0xFC, 0x81, 0x00, 0xD8, 0xFC, 0x81, 0x00}},
        {"M2 batch draw: it calls the material setup with its context", 0x0082040B, 7, {0x8B, 0xCE, 0xE8, 0xFE, 0xF6, 0xFF, 0xFF}},
        {"M2 batched doodad draw: it calls the material setup with its context", 0x00820B3C, 7, {0x8B, 0xCE, 0xE8, 0xCD, 0xEF, 0xFF, 0xFF}},
        {"fog setter: the fog colour goes up as pixel constant c2", 0x008732D7, 11, {0x6A, 0x01, 0x68, 0x58, 0x30, 0xD4, 0x00, 0x6A, 0x02, 0x6A, 0x04}},
    };
    constexpr uintptr_t kMaterialSetup = 0x0081FB10;
    constexpr uintptr_t kBlendFogModes = 0x00A45390; // uint32 per blend mode 0..7
    inline constexpr uint32_t kBlendFogModeTable[8] = { 1, 1, 1, 2, 2, 3, 4, 0 };

    // --- which models ----------------------------------------------------------------------------------------------
    // Path parts, lower case with backslashes: the Storm Peaks' lightning doodads and the storm cloud doodads (the
    // folder's additive fog_01 is left anyway: its batches fog to black).
    inline constexpr const char* kStormModels[] = {
        "doodads\\stormpeaks\\lightning\\",
        "doodads\\stormpeaks\\fog\\",
    };

    /// Whether a model path (or stem; any case, either slash) is one of the storm doodads.
    inline bool IsStormModel(const char* path, size_t length)
    {
        if (!path) return false;
        char p[kStemCapacity + 1];
        size_t n = 0;
        for (; n < length && n < kStemCapacity && path[n]; ++n)
        {
            const char c = path[n];
            p[n] = c == '/' ? '\\' : (c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
        }
        p[n] = 0;
        for (const char* m : kStormModels)
        {
            size_t mn = 0;
            while (m[mn]) ++mn;
            for (size_t at = 0; at + mn <= n; ++at)
            {
                size_t k = 0;
                while (k < mn && p[at + k] == m[k]) ++k;
                if (k == mn) return true;
            }
        }
        return false;
    }

    // --- the colours -------------------------------------------------------------------------------------------------
    constexpr float kMatch = 0.03f; // a draw's c2 within this of the stock fog colour (each channel) is fogged toward it

    inline bool Same(const float a[3], const float b[3]) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; }

    inline bool MatchesStock(const float c2[3], const float stock[3])
    {
        for (int i = 0; i < 3; ++i)
        {
            const float d = c2[i] - stock[i];
            if (!(d <= kMatch && d >= -kMatch)) return false;
        }
        return true;
    }

    /// Exactly a blend mode's own fog colour as the client uploads it: black, white or its grey (0x80 x 1/255).
    inline bool BlendModeColour(const float c[3])
    {
        const float grey = 128.0f * (1.0f / 255.0f);
        const bool flat = c[0] == c[1] && c[1] == c[2];
        return flat && (c[0] == 0.0f || c[0] == 1.0f || c[0] == grey);
    }

    inline void FromArgb(uint32_t argb, float out[3])
    {
        out[0] = ((argb >> 16) & 0xFF) / 255.0f;
        out[1] = ((argb >> 8) & 0xFF) / 255.0f;
        out[2] = (argb & 0xFF) / 255.0f;
    }

    /// The deck's brightness where the camera sees it, as the deck pass lights it (0.85 at its base .. 1.1 at its top,
    /// GroundFog kDeckHlsl): its underside from below, its top from above, between inside it.
    inline float DeckShade(float camZ, float base, float top)
    {
        const float thick = top - base > 1.0f ? top - base : 1.0f;
        float t = (camZ - base) / thick;
        t = t > 0.0f ? (t < 1.0f ? t : 1.0f) : 0.0f; // (NaN: 0, the underside)
        return 0.85f + 0.25f * t;
    }

    /// What everything else drawn after the deck fogs toward (as since 2026-10-13): the stock colour moved toward the
    /// deck's as much as the sky is overcast (a clear sky's thin wisps don't light up the night).
    inline void GeneralColour(const float stock[3], const float deck[3], float cover, float out[3])
    {
        const float k = cover > 0.0f ? (cover < 1.0f ? cover : 1.0f) : 0.0f;
        for (int i = 0; i < 3; ++i) out[i] = stock[i] + (deck[i] - stock[i]) * k;
    }

    /// What the storm doodads fog toward: the deck's colour (it never goes below its night floor) at its shade, any sky.
    inline void StormColour(const float deck[3], float shade, float out[3])
    {
        for (int i = 0; i < 3; ++i)
        {
            const float v = deck[i] * shade;
            out[i] = v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f;
        }
    }

    // --- the window --------------------------------------------------------------------------------------------------
    /// Whether the window opens this frame (at the deck's draw, to the end of the world frame): whenever the deck was
    /// drawn, wherever the camera is (below it, inside or above: the storm doodads stand in it, and are seen from all
    /// three), with the tint on and the world frame hooked (it closes the window).
    inline bool WindowOpens(bool deckDrawn, bool tintOn, bool frameHooked) { return deckDrawn && tintOn && frameHooked; }

    // --- one draw ----------------------------------------------------------------------------------------------------
    enum class Action { Leave, General, Storm };
    struct DrawIs
    {
        bool stormBatch = false;   // an M2 batch of a storm doodad
        int  fogMode = kFogUnknown; // the batch's fog mode, kFogUnknown outside an M2 batch or without the guards
    };

    /// Inside the window (from the deck's draw to the end of the world frame), for a draw whose c2 the client set to
    /// `client`: a storm doodad's batch fogged with the scene's colour takes the storm colour, whatever that colour is
    /// (without the guards: unless it's a blend mode's own black, white or grey); another batch with a known fog mode
    /// other than the scene's is left; anything else fogged toward the stock colour takes the general colour.
    inline Action Decide(const DrawIs& d, const float client[3], const float stock[3])
    {
        if (d.stormBatch)
        {
            if (d.fogMode == kFogScene) return Action::Storm;
            if (d.fogMode == kFogUnknown && !BlendModeColour(client)) return Action::Storm;
            return Action::Leave;
        }
        if (d.fogMode != kFogUnknown && d.fogMode != kFogScene) return Action::Leave;
        return MatchesStock(client, stock) ? Action::General : Action::Leave;
    }

    /// The cache's c2 between draws: whether it holds a colour we wrote, and the client's own value it replaced.
    struct State
    {
        bool  holds = false;
        float client[3] = {};
        float written[3] = {};
    };

    /// Right before a draw in the window: c2 (the cache's, `c2`) set for it. The client's value is c2 unless c2 still
    /// holds what we wrote (the client didn't set it again since: the setter skips equal values), then the one we
    /// replaced. `dirty`: c2 changed, its flush must upload it.
    inline Action Step(State& s, float c2[3], const DrawIs& d, const float stock[3], const float general[3], const float storm[3], bool& dirty)
    {
        dirty = false;
        float client[3];
        const bool ours = s.holds && Same(c2, s.written);
        for (int i = 0; i < 3; ++i) client[i] = ours ? s.client[i] : c2[i];
        const Action a = Decide(d, client, stock);
        const float* want = a == Action::Storm ? storm : (a == Action::General ? general : client);
        if (!Same(c2, want))
        {
            for (int i = 0; i < 3; ++i) c2[i] = want[i];
            dirty = true;
        }
        s.holds = a != Action::Leave;
        for (int i = 0; i < 3; ++i) { s.client[i] = client[i]; s.written[i] = want[i]; }
        return a;
    }

    /// The window's end: the client's value back where ours still is. True when c2 changed.
    inline bool End(State& s, float c2[3])
    {
        bool dirty = false;
        if (s.holds && Same(c2, s.written) && !Same(c2, s.client))
        {
            for (int i = 0; i < 3; ++i) c2[i] = s.client[i];
            dirty = true;
        }
        s.holds = false;
        return dirty;
    }
}
