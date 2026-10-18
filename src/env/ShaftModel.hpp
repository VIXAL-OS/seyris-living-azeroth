// Sun shafts: the rules, apart from the client (tests/shafts checks them offline). The runtime is features/SunShafts
// (the pass) with its shaders in features/SunShaftShaders.hpp.
//
// How strong the shafts are comes from what's in the air and when:
//  - The air: the place's fogginess (AreaClimate) and the ground fog's area fog, mist, haze and water fog where the
//    player is (env/Fog). Shafts need something to light up: strong in Duskwood's fog or a misty forest morning,
//    faint in clear air.
//  - The time of day: by day (the sky's day, the fog's own FogNight curve: day until 18:00, night by 22:00, dawn from
//    03:00 to 06:00), strongest in the low sun of morning and evening, three quarters of that at noon (kMidday);
//    nothing once the light is below the horizon.
//  - The weather: less in rain or snow, none in heavy rain or a storm (env/Storm); none indoors.
//  - At night, optionally (the Moon setting), faint shafts from the moon.
//
// Where they radiate from: the disc the player sees in the sky -- the sky's sun, or the moon when the moon's part
// leads -- through the scene's view and projection as a point at infinity (the client's projection is OpenGL-style
// with w = the view depth: render/SceneDepth, debug/DepthView). Not the light's direction: the client draws the sun
// along one path and lights the world from another, lower one (env/SkyPath; at 15:09 the sun is 54 deg up, the light
// 28 deg). The screen-space shafts fade out as the disc leaves the screen or sinks below the horizon, and are off once
// it's behind the camera. What lights the world -- the in-scatter under the cloud deck, the glass, the floor light --
// keeps the light's direction (climate::SunNow().dir), so its shadows agree with the scene's shading.
#pragma once

namespace wxl_livingazeroth::shafts
{
    /// The midday sun's part, as a share of the low sun's (morning and evening).
    constexpr float kMidday = 0.75f;

    struct Inputs
    {
        float sunHeight = 0.5f;      // world z of the unit direction toward the light (by day and night; climate::SunNow().dir)
        float night = 0.0f;          // the sky's night, 0 day .. 1 night (env/Fog RulesInputs::night)
        float hour = 12.0f;          // the time of day, 0 .. 24
        float fogginess = 0.0f;      // the place's (AreaClimate), 0 .. 1
        float mist = 0.0f, areaFog = 0.0f, haze = 0.0f, waterFog = 0.0f; // the fog field's amounts where the player is, 0 .. 1
        float precipitation = 0.0f;  // rain or snow, 0 .. 1 (a sandstorm: 0.6 of its intensity)
        float storminess = 0.0f;     // env/Storm, smoothed 0 .. 1
        float interior = 0.0f;       // 0 outdoors .. 1 inside (the client's interior blend)
        float moon = 0.0f;           // the Moon setting: x the night light's faint shafts (0 = none)
    };

    struct Strength
    {
        float air = 0.0f;        // how much is in the air, 0 .. 1
        float airFactor = 0.0f;  // what the air makes of the shafts (0.3 in clear air .. 1)
        float golden = 0.0f;     // the low sun of morning and evening, 0 .. 1
        float sun = 0.0f;        // the sun's part: the day x (kMidday at noon .. all in the low sun) x above the horizon
        float moon = 0.0f;       // the night light's part
        float weather = 0.0f;    // 1 dry and calm .. 0 in heavy rain or a storm
        float indoor = 0.0f;     // 1 outdoors .. 0 inside
        float total = 0.0f;      // (sun + moon) x airFactor x weather x indoor
        float airPerYard = 0.0f; // how fast the air in front of a surface lights up (the shafts' source between the trees)
        const char* why = nullptr; // why it's (nearly) none; nullptr when it isn't
    };

    /// How much is in the air, 0 .. 1: the place's fogginess and the fog kinds' amounts, each counting for a share,
    /// combined as independent veils (1 - the product of what each lets through).
    float AirAmount(float fogginess, float mist, float areaFog, float haze, float waterFog);
    /// The low sun, 0 .. 1: a bump round 07:00 and one round 19:00 (by the hour, 0 .. 24).
    float GoldenHour(float hour);
    Strength Evaluate(const Inputs& in);

    /// The sun on the screen.
    struct SunScreen
    {
        bool  inFront = false;   // in front of the camera (its projection means something)
        float u = 0.5f, v = 0.5f; // viewport uv (0,0 top left .. 1,1 bottom right); may lie off the screen
        float view[3] = { 0.0f, 0.0f, 1.0f }; // its direction in view space (unit)
        float fade = 0.0f;       // 1 on the screen .. 0 once it's `margin` (uv) off it, or behind
    };
    /// view, proj: the scene's matrices as the client keeps them (row vectors: clip = p x view x proj; float[16] row by
    /// row). dir: toward the disc (or the light), world space (unit). margin: how far off the screen (uv) the fade
    /// reaches 0.
    SunScreen ProjectSun(const float view[16], const float proj[16], const float dir[3], float margin);
    /// 1 on the screen, falling smoothly to 0 at `margin` (uv) outside it.
    float ScreenFade(float u, float v, float margin);
    /// The disc's own fade at the horizon (discZ: the world z of the unit direction toward it): 0 once it's a degree
    /// below, 1 from two degrees above (the client fades its sprite out there too).
    float HorizonFade(float discZ);
    /// The view-space direction (unit) through a viewport uv, as the shaders work it out (the inverse of ProjectSun).
    void ViewRay(const float proj[16], float u, float v, float out[3]);
    /// A view-space direction to world space (the view's rotation inverted; unit).
    void ViewToWorld(const float view[16], const float in[3], float out[3]);

    /// The in-scatter under the cloud deck: 1 with the camera below its base, fading to 0 as the camera rises through
    /// its soft bottom (a quarter of its thickness, at most 100 yd), 0 above.
    float UnderDeck(float camZ, float base, float top);
    /// The deck's mean extinction per yd as its glow takes it (its density x the cloud's mean coverage, ~0.55, x how
    /// overcast the sky over it is): for its slab when its own result isn't at hand.
    float DeckMeanExtinction(float density, float cover);
    /// The slab's length (yd) along a view ray (dirZ: the ray's world z, unit), out to `range` yd.
    float DeckSlabLength(float camZ, float dirZ, float base, float top, float range);
}
