// The client's sky (DayNight): where it draws the sun and the moon, and where the light that shades the world comes
// from -- two different directions. The rules apart from the client (tests/shafts checks them, and the client code
// and tables they come from, against Wow.exe). Decomp: SatyPardus/wowdecomp b1c71b8, WoW/Source/WorldClient/DayNight.cpp.
//
//  - The light (DayNight::SetDirection 0x7EEA90): the day-night block's +0x19C (m_light1.m_dir), what the client
//    shades the world and its models with, pointing away from the light. A fixed bearing (from +x +y: the north-west)
//    at a stylized height: 37 deg at noon and midnight, 20 deg at 06:00 and 18:00 (its tables, built on first use at
//    0xD390E4 and 0xD390C4). env/Climate reads it: climate::SunNow().dir.
//  - The sky's sun and moon (DayNight::SetPlanets 0x7EECC0): their sprites, drawn at the camera + 12 x a direction
//    from the time of day in polar angles (theta from the zenith, phi round from +x): the sun from 10 deg below the
//    horizon at 05:30 up to 5 deg off the zenith at noon and down again by 21:30 (above the horizon 06:10 .. 20:30),
//    the moon the same way at night (above it 22:20 .. 03:25), both on the light's bearing. Every frame
//    (DayNight::UpdateLighting 0x7F3920 calls SetColors, SetDirection, then jumps to SetPlanets) it writes g_planets:
//    the sun's position at 0xD38E28, the moon's at 0xD38E48, from the camera's (the day-night block's +0x18, which
//    CGWorldFrame::UpdateDayNight sets earlier in the frame); the glares (the lens flare) take the same positions
//    (0xD38EB4, 0xD38F64). The tables: the sun's at 0xD391E0 (theta) and 0xD391C8 (phi), the moon's at 0xD391A0 and
//    0xD39188, built on first use; the interpolator is DayNight::InterpTable (0x7ED3B0).
// At 15:09 the light is 28 deg up and the sun 54 deg, on the same bearing: a camera pitched up at the sun has the
// light's point half a screen below it.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace wxl_livingazeroth::sky
{
    /// A key of the client's tables (its C2Vector): the time of day 0..1, the value.
    struct Key { float time, value; };

    // SetDirection's (radians): the light's polar angle from +z (over 90 deg: it points down) and its bearing.
    inline constexpr Key kLightElevation[4] = { { 0.0f, 2.2165682f }, { 0.25f, 1.9198623f }, { 0.5f, 2.2165682f }, { 0.75f, 1.9198623f } };
    inline constexpr Key kLightAzimuth[4] = { { 0.0f, 3.926991f }, { 0.25f, 3.926991f }, { 0.5f, 3.926991f }, { 0.75f, 3.926991f } };
    // SetPlanets' (radians): theta from the zenith, phi round from +x.
    inline constexpr Key kSunTheta[5] = { { 0.22916667f, 1.7453293f }, { 0.49652779f, 0.087266468f }, { 0.5f, 0.087266468f },
                                          { 0.50347221f, 0.087266468f }, { 0.89583331f, 1.7453293f } };
    inline constexpr Key kSunPhi[3] = { { 0.22916667f, 0.78539819f }, { 0.5f, 0.78539819f }, { 0.89583331f, 0.78539819f } };
    inline constexpr Key kMoonTheta[5] = { { 0.0f, 0.61086524f }, { 0.0034722222f, 0.61086524f }, { 0.16666667f, 1.7453293f },
                                           { 0.91666669f, 1.7453293f }, { 0.99652779f, 0.61086524f } };
    inline constexpr Key kMoonPhi[3] = { { 0.0f, 0.78539819f }, { 0.16666667f, 0.78539819f }, { 0.91666669f, 0.78539819f } };

    /// DayNight::InterpTable (0x7ED3B0): t clamped to 0..1, between the keys either side of it round the clock (past
    /// the last key toward the first, across midnight).
    inline float Interp(const Key* table, unsigned count, float t)
    {
        const float value = t >= 0.0f ? (t < 1.0f ? t : 1.0f) : 0.0f;
        unsigned index = 0;
        while (index < count && value > table[index].time) ++index;
        unsigned prev;
        if (index == count) { index = 0; prev = count - 1; }
        else prev = index ? index - 1 : count - 1;
        float range = table[index].time - table[prev].time;
        if (std::fabs(range) < 0.001f) return table[prev].value;
        if (range < 0.0f) range += 1.0f;
        float offset = value - table[prev].time;
        if (offset < 0.0f) offset += 1.0f;
        const float fraction = offset / range, from = table[prev].value, to = table[index].value;
        return to < from ? from - fraction * (from - to) : fraction * (to - from) + from;
    }

    /// Toward the light that shades the world (unit, world, turned to point up as env/Climate turns the client's):
    /// SetDirection's at this time of day (0..1).
    inline void LightToward(float dayFraction, float out[3])
    {
        const float e = Interp(kLightElevation, 4, dayFraction), a = Interp(kLightAzimuth, 4, dayFraction);
        const float d[3] = { std::cos(a) * std::sin(e), std::sin(e) * std::sin(a), std::cos(e) };
        const float flip = d[2] < 0.0f ? -1.0f : 1.0f;
        for (int k = 0; k < 3; ++k) out[k] = flip * d[k];
    }

    /// Toward a planet (unit, world): SetPlanets' direction from its theta and phi tables at this time of day.
    inline void PlanetToward(const Key* theta, unsigned thetaCount, const Key* phi, unsigned phiCount, float dayFraction, float out[3])
    {
        const float t = Interp(theta, thetaCount, dayFraction), p = Interp(phi, phiCount, dayFraction);
        out[0] = std::cos(p) * std::sin(t);
        out[1] = std::sin(t) * std::sin(p);
        out[2] = std::cos(t);
    }
    /// Toward the sky's sun, and its moon (unit, world), as the client draws them at this time of day.
    inline void SunToward(float dayFraction, float out[3]) { PlanetToward(kSunTheta, 5, kSunPhi, 3, dayFraction, out); }
    inline void MoonToward(float dayFraction, float out[3]) { PlanetToward(kMoonTheta, 5, kMoonPhi, 3, dayFraction, out); }

    // --- read from the client each frame ------------------------------------------------------------------------
    constexpr uintptr_t kCameraPosition = 0x00D38B18;  // the day-night block (0xD38B00) +0x18
    constexpr uintptr_t kSunPosition = 0x00D38E28;     // g_planets.m_sun.m_position
    constexpr uintptr_t kMoonPosition = 0x00D38E48;    // g_planets.m_moon.m_position
    constexpr float     kDistance = 12.0f;             // how far from the camera SetPlanets puts them (0xA1047C)

    /// A planet's direction (unit, world) from its position and the camera's as the client wrote them; false unless
    /// it's a finite kDistance (within 1%) from the camera.
    inline bool FromPositions(const float planet[3], const float camera[3], float out[3])
    {
        const float d[3] = { planet[0] - camera[0], planet[1] - camera[1], planet[2] - camera[2] };
        const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (!(len > 0.99f * kDistance && len < 1.01f * kDistance)) return false; // (NaN fails too)
        for (int k = 0; k < 3; ++k) out[k] = d[k] / len;
        return true;
    }

    /// Where this frame's sun and moon directions came from.
    enum class From { Read, Clock, Light };
    /// The sky's sun and moon this frame. With the time override: the client's rule at that hour (dayFraction), as
    /// the light is then. Else from the positions the client wrote (`positions`: the sun's, the moon's, the camera's;
    /// null when they couldn't be read or its code differs) if both check out. Else the light's direction (`light`)
    /// for both.
    inline From Resolve(bool timeOverride, float dayFraction, const float (*positions)[3], const float light[3], float sun[3], float moon[3])
    {
        if (timeOverride)
        {
            SunToward(dayFraction, sun);
            MoonToward(dayFraction, moon);
            return From::Clock;
        }
        float s[3], m[3];
        if (positions && FromPositions(positions[0], positions[2], s) && FromPositions(positions[1], positions[2], m))
        {
            for (int k = 0; k < 3; ++k) { sun[k] = s[k]; moon[k] = m[k]; }
            return From::Read;
        }
        for (int k = 0; k < 3; ++k) sun[k] = moon[k] = light[k];
        return From::Light;
    }

    /// The client code the read relies on (checked byte for byte before the first read).
    struct Guard
    {
        const char* what;
        uintptr_t   address;
        uint32_t    size;
        uint8_t     bytes[16];
    };
    inline constexpr Guard kPlanetGuards[] = {
        {"day-night info: returns the block at 0xD38B00", 0x007ECEF0, 6, {0xB8, 0x00, 0x8B, 0xD3, 0x00, 0xC3}},
        {"day-night lighting: the colours, the light, then the planets, every frame", 0x007F399B, 15,
         {0xE8, 0x90, 0xF8, 0xFF, 0xFF, 0xE8, 0xEB, 0xB0, 0xFF, 0xFF, 0xE9, 0x16, 0xB3, 0xFF, 0xFF}},
        {"planets: the sun's direction scaled to 12, the camera's x", 0x007EF155, 16,
         {0xD8, 0x3D, 0x7C, 0x04, 0xA1, 0x00, 0xDC, 0xCB, 0xDC, 0xCA, 0xDE, 0xC9, 0xD9, 0x05, 0x18, 0x8B}},
        {"planets: the sun's x stored at 0xD38E28", 0x007EF165, 12,
         {0xD3, 0x00, 0xDE, 0xC3, 0xD9, 0xCA, 0xD9, 0x1D, 0x28, 0x8E, 0xD3, 0x00}},
        {"planets: the sun's y and z (the camera's + its own)", 0x007EF171, 16,
         {0xD8, 0x05, 0x1C, 0x8B, 0xD3, 0x00, 0xD9, 0x1D, 0x2C, 0x8E, 0xD3, 0x00, 0xD8, 0x05, 0x20, 0x8B}},
        {"planets: the sun's z stored at 0xD38E30", 0x007EF181, 8, {0xD3, 0x00, 0xD9, 0x1D, 0x30, 0x8E, 0xD3, 0x00}},
        {"planets: the moon's direction scaled to 12", 0x007EF329, 14,
         {0xD8, 0x3D, 0x7C, 0x04, 0xA1, 0x00, 0xDC, 0xCB, 0xDC, 0xCA, 0xDE, 0xC9, 0xD9, 0xCA}},
        {"planets: the moon's x and y (the camera's + its own)", 0x007EF337, 16,
         {0xD8, 0x05, 0x18, 0x8B, 0xD3, 0x00, 0xD9, 0x1D, 0x48, 0x8E, 0xD3, 0x00, 0xD8, 0x05, 0x1C, 0x8B}},
        {"planets: the moon's y and z stored at 0xD38E4C, 0xD38E50", 0x007EF347, 16,
         {0xD3, 0x00, 0xD9, 0x1D, 0x4C, 0x8E, 0xD3, 0x00, 0xD8, 0x05, 0x20, 0x8B, 0xD3, 0x00, 0xD9, 0x1D}},
        {"planets: ... at 0xD38E50", 0x007EF357, 4, {0x50, 0x8E, 0xD3, 0x00}},
        {"planets: their distance, 12", 0x00A1047C, 4, {0x00, 0x00, 0x40, 0x41}},
    };

    /// The first guard whose bytes differ (read through `read(address, size)`: a pointer to that many bytes, or null),
    /// or null when all match.
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
}
