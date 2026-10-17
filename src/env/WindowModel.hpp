// Window lights and glow: the rules, apart from the client (tests/windows checks them offline). The runtime is
// features/Windows (the table, the night fade, the client's glow) with env/Lights (the lights) and
// features/WindowGlow (the glow pass).
//
// The night fade. The client fades a WMO window material's emissive colour in at night: its per-frame material
// update (CMapObj, 0x7A8520) scales every SIDN material's (MOMT flag 0x10) emissive colour by a factor it reads
// from the day-night block (DayNightGetInfo 0x7ECEF0 returns 0xD38B00; +0x1DC). The day-night update (at 0x7F38CC)
// computes that factor from the time of day (+0x04, 0..1) with its key interpolator (0x7ED3B0, cyclic, linear)
// over four keys at 0xAF4C80: 1 at 06:00, 0 from 07:00 to 20:30, 1 again from 21:30. Window lights and glow follow
// the same factor: the client's own value where the guards hold, else the same curve from the clock.
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace wxl_livingazeroth::windows
{
    // --- the night fade ------------------------------------------------------------------------------------------
    struct Key { float time, value; };
    /// The client's keys (0xAF4C80: 4 pairs of time of day 0..1 and factor).
    inline constexpr Key kNightKeys[4] = { { 0.25f, 1.0f }, { 0.2916666567f, 0.0f }, { 0.8541666269f, 0.0f }, { 0.8958333135f, 1.0f } };

    /// The window factor at a time of day (0..1, wraps), as the client's interpolator works it out.
    float NightFade(float dayFraction);

    constexpr uintptr_t kDayNight = 0x00D38B00;    // DayNightGetInfo's block
    constexpr size_t    kDayFraction = 0x04;       // the time of day, 0..1
    constexpr size_t    kWindowNight = 0x1DC;      // the window factor, 0..1
    constexpr uintptr_t kNightKeyTable = 0x00AF4C80;

    // The client's Full Screen Glow (ffxGlow): the world frame's setup stores its effect object at 0xB74364 (0x4FAEBB);
    // the object's +0x04 is the ffxGlow CVar, whose int value (+0x30) its "enabled" method (0x8BFD34) tests.
    constexpr uintptr_t kGlowEffect = 0x00B74364;
    constexpr size_t    kGlowCVar = 0x04, kCVarInt = 0x30;

    struct Guard
    {
        const char* what;
        uintptr_t   address;
        uint32_t    size;
        uint8_t     bytes[16];
    };
    inline constexpr Guard kNightGuards[] = {
        {"day-night info: returns the block at 0xD38B00", 0x007ECEF0, 6, {0xB8, 0x00, 0x8B, 0xD3, 0x00, 0xC3}},
        {"day-night update: the window keys, the interpolator, the store at +0x1DC", 0x007F38DB, 16,
         {0xBF, 0x80, 0x4C, 0xAF, 0x00, 0xE8, 0xCB, 0x9A, 0xFF, 0xFF, 0xD9, 0x1D, 0xDC, 0x8C, 0xD3, 0x00}},
        {"WMO material update: scales SIDN emissive by +0x1DC", 0x007A855C, 9, {0x8B, 0x45, 0xF4, 0xD9, 0x80, 0xDC, 0x01, 0x00, 0x00}},
    };
    inline constexpr Guard kGlowGuards[] = {
        {"world frame setup: stores the full-screen glow object", 0x004FAEBB, 5, {0xA3, 0x64, 0x43, 0xB7, 0x00}},
        {"full-screen glow: tests ffxGlow's value", 0x008BFD34, 7, {0x8B, 0x46, 0x04, 0x83, 0x78, 0x30, 0x00}},
    };

    /// The first guard of `guards` whose bytes differ (read through `read(address, size)`, a pointer to that many
    /// bytes or null), or null when all match.
    template <class Read, size_t N> const Guard* FirstMismatch(const Guard (&guards)[N], Read read)
    {
        for (const Guard& g : guards)
        {
            const uint8_t* at = static_cast<const uint8_t*>(read(g.address, g.size));
            if (!at) return &g;
            for (uint32_t i = 0; i < g.size; ++i)
                if (at[i] != g.bytes[i]) return &g;
        }
        return nullptr;
    }

    // --- the table (WindowLights.cdbc, tools/gen_window_lights.py) -----------------------------------------------
    enum RowFlag : uint32_t { kGlowOnly = 0x1 };   // the window glows but casts no light (its cell was full)

    struct Window
    {
        uint32_t id = 0, map = 0, area = 0;
        float    pos[3] = {};          // the light: OUT yd in front of the glass's middle
        float    facing[3] = {};       // out of the building (horizontal for upright windows)
        float    back = 0.75f;         // yd from the light back to the glass
        float    halfWidth = 1.0f;     // the glass's extent on the facade (the glow's mask)
        float    halfHeight = 1.0f;
        float    radius = 0.0f;        // yd; 0 = the light's
        uint32_t lightId = 0;          // DoodadLightProperties
        float    strength = 1.0f;      // x the light's intensity
        uint32_t flags = 0;            // RowFlag
    };

    /// The windows of every map, bucketed in kTile-yd squares for "near the camera".
    class Index
    {
    public:
        static constexpr float kTile = 64.0f;
        void Build(std::vector<Window> windows);
        /// `out` gets the windows of `map` within `range` yd of `center` (3D), nearest first, at most `max`; glow-only
        /// rows only with `glowOnly`.
        void Near(uint32_t map, const float center[3], float range, bool glowOnly, size_t max, std::vector<const Window*>& out) const;
        size_t Size() const { return m_windows.size(); }
        const std::vector<Window>& All() const { return m_windows; }

    private:
        static uint64_t Key(uint32_t map, int tx, int ty);
        std::vector<Window> m_windows;
        std::unordered_map<uint64_t, std::vector<uint32_t>> m_tiles;
    };

    // --- the light and the glass ---------------------------------------------------------------------------------
    /// The spot's axis: out of the window, tilted down (kTilt: ~24 degrees), so the light falls on the ground and
    /// the walls in front of it.
    constexpr float kTilt = 0.45f;
    void SpotDirection(const float facing[3], float out[3]);

    /// The glass's rectangle on the facade, widened by `margin` yd: corners (left-low, right-low, left-high,
    /// right-high), world space. Its middle is `back` yd behind the light.
    void GlassCorners(const Window& w, float margin, float out[4][3]);

    // --- the light budget in one cell of the lights' grid ----------------------------------------------------------
    /// A window's light ranks this much below another light of the same brightness when a cell is full: torches
    /// and lanterns keep their places, windows fill in where there's room.
    constexpr float kWindowRank = 0.35f;
    struct CellEntry { float score; uint16_t light; bool window; };
    /// One cell's lights, sorted strongest first: takes up to `perCell`, of those at most `windowCap` windows.
    /// Returns how many it took (into `out`); `windowsLeftOut` counts the windows the cap kept out.
    unsigned PickCell(const CellEntry* entries, size_t count, int perCell, int windowCap, uint16_t* out, unsigned& windowsLeftOut);
}
