// A flame's flicker, shared so every fire feels alike: env/Lights' smooth flicker (torches, braziers, the window lights'
// own) and the hearths behind frosted glass (env/GlassModel's HearthDip: the glass and its window light together).
// Three waves at unrelated rates, so no repeat shows; the weights sum to 1.
#pragma once

namespace wxl_livingazeroth::flame
{
    inline constexpr double kRates[3] = { 7.13, 13.71, 23.17 };   // rad/s at pace 1 (~1.1, 2.2 and 3.7 dips a second)
    inline constexpr double kWeights[3] = { 0.5, 0.3, 0.2 };
    inline constexpr double kOffsets[3] = { 0.0, 1.3, 2.1 };      // rad
    inline constexpr double kTwoPi = 6.283185307179586;
}
