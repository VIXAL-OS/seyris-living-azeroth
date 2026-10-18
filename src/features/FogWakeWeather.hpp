// What the weather does to the fog wake (features/FogWake), kept apart so tests/storm can check it offline. Rain and
// wind stir the ground fog back into a wake almost at once: in heavy rain a unit leaves next to no trail (a straight dark
// channel through the rain's fog behind every runner was "extremely goofy", the user, 2026-10-17, Elwynn in a storm).
// Calm, dry fog keeps the wake exactly as it was.
#pragma once

namespace wxl_livingazeroth::fogwake
{
    // The wind starts stirring the fog from a fresh breeze up (wind::Sample strength, ~0..1 at the ground; Elwynn's and
    // Duskwood's calm days stay below it) and fully by a strong wind.
    constexpr float kWindStirFrom = 0.35f, kWindStirFull = 1.0f;
    // At full mixing the push is down to this share (heavy rain: "almost no wake") and the trail fills back in in this
    // share of its time.
    constexpr float kPushInFullMix = 0.05f;
    constexpr float kFillInFullMix = 0.2f;

    /// 0..1, how much the weather mixes the fog back: rain (StormModel RainLevel: 0 below light rain, 1 heavy) or the
    /// storm's storminess, whichever is more, times rainMixing; and the wind at the ground over kWindStirFrom, times
    /// windMixing; the two combined like independent chances. 0 in calm, dry weather.
    inline float WeatherMixing(float rain, float storminess, float wind, float rainMixing, float windMixing)
    {
        auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
        if (!(rain == rain)) rain = 0.0f;
        if (!(storminess == storminess)) storminess = 0.0f;
        if (!(wind == wind)) wind = 0.0f;
        const float wet = clamp01(clamp01(rain > storminess ? rain : storminess) * clamp01(rainMixing));
        float t = clamp01((wind - kWindStirFrom) / (kWindStirFull - kWindStirFrom));
        t = t * t * (3.0f - 2.0f * t);
        const float stir = clamp01(t * clamp01(windMixing));
        return clamp01(1.0f - (1.0f - wet) * (1.0f - stir));
    }

    /// The wake under that mixing: its push and its fill-in time as multipliers (1, 1 at no mixing).
    struct WakeScale { float push = 1.0f; float fill = 1.0f; };
    inline WakeScale ScaleFor(float mixing)
    {
        const float m = mixing < 0.0f ? 0.0f : (mixing > 1.0f ? 1.0f : (mixing == mixing ? mixing : 0.0f));
        return { 1.0f - (1.0f - kPushInFullMix) * m, 1.0f - (1.0f - kFillInFullMix) * m };
    }
}
