#include "StormModel.hpp"

#include <cmath>

namespace wxl_livingazeroth::storm
{
    namespace
    {
        float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
        float Lerp(float a, float b, float t) { return a + (b - a) * t; }
        float Smooth(float a, float b, float v) { const float t = Clamp01((v - a) / (b - a)); return t * t * (3.0f - 2.0f * t); }

        // Per band: how much the storm darkens it, how much a flash in the sky lights it, how much a flash
        // lighting the ground does.
        struct Weights { float dark, sky, scene; };
        constexpr Weights kWeights[kBandCount] = {
            { 0.55f, 0.00f, 0.55f }, //  0 direct light: overcast, the sun's light weaker
            { 0.35f, 0.20f, 0.80f }, //  1 ambient: a near flash lights everything
            { 1.00f, 0.75f, 0.30f }, //  2 sky, top
            { 1.00f, 0.90f, 0.30f }, //  3
            { 1.00f, 1.00f, 0.30f }, //  4
            { 1.00f, 1.00f, 0.30f }, //  5
            { 0.90f, 1.00f, 0.30f }, //  6
            { 0.80f, 0.80f, 0.30f }, //  7 horizon
            { 0.70f, 0.70f, 0.40f }, //  8 fog
            { 0.90f, 0.00f, 0.00f }, //  9 sun
            { 0.90f, 0.00f, 0.00f }, // 10 sun halo
            { 0.80f, 1.00f, 0.30f }, // 11 clouds
            { 0.80f, 1.00f, 0.30f }, // 12
            { 0.80f, 1.00f, 0.30f }, // 13
            { 0.40f, 0.30f, 0.10f }, // 14 water (reflecting a darker sky)
            { 0.40f, 0.30f, 0.10f }, // 15
            { 0.40f, 0.30f, 0.10f }, // 16
            { 0.40f, 0.30f, 0.10f }, // 17
        };
    }

    Phase PhaseFor(int weatherType, float grade)
    {
        if (weatherType != 1 || !(grade >= kRainFrom)) return Phase::None;
        if (grade < kMediumFrom) return Phase::Light;
        if (grade < kHeavyFrom) return Phase::Medium;
        return Phase::Heavy;
    }

    const char* PhaseName(Phase phase)
    {
        switch (phase)
        {
            case Phase::Light:  return "light rain (the build-up)";
            case Phase::Medium: return "medium rain";
            case Phase::Heavy:  return "heavy rain (thunder)";
            default:            return "no storm";
        }
    }

    float Storminess(int weatherType, float grade)
    {
        if (weatherType != 1 || !std::isfinite(grade)) return 0.0f;
        return Clamp01((grade - kRainFrom) / (kFullStorm - kRainFrom));
    }

    uint32_t Rng::Next()
    {
        // xorshift32
        uint32_t x = state ? state : 0x9E3779B9u;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        state = x;
        return x;
    }
    float Rng::Uniform() { return static_cast<float>(Next() >> 8) * (1.0f / 16777216.0f); }
    float Rng::Range(float a, float b) { return a + (b - a) * Uniform(); }

    float MeanInterval(Phase phase, float grade)
    {
        switch (phase)
        {
            case Phase::Light:  return Lerp(80.0f, 55.0f, Clamp01((grade - kRainFrom) / (kMediumFrom - kRainFrom)));
            case Phase::Medium: return Lerp(45.0f, 25.0f, Clamp01((grade - kMediumFrom) / (kHeavyFrom - kMediumFrom)));
            case Phase::Heavy:  return Lerp(20.0f, 9.0f, Clamp01((grade - kHeavyFrom) / (1.0f - kHeavyFrom)));
            default:            return 0.0f;
        }
    }

    float NextWait(Rng& rng, float mean)
    {
        const float rest = mean > kMinGap ? mean - kMinGap : 0.0f;
        // Exponential with mean `rest`, capped at 4x so a long silence never runs away.
        const float u = rng.Uniform();
        const float e = -std::log(1.0f - (u < 0.9999f ? u : 0.9999f));
        return kMinGap + rest * (e < 4.0f ? e : 4.0f);
    }

    bool Scheduler::Step(float dt, Phase now, float grade, float frequency, Rng& rng)
    {
        if (now == Phase::None || !(frequency > 0.0f))
        {
            wait = -1.0f;
            phase = now;
            return false;
        }
        const float mean = MeanInterval(now, grade) / frequency;
        if (wait < 0.0f || phase == Phase::None) wait = rng.Range(0.3f, 1.0f) * mean;
        else if (static_cast<int>(now) > static_cast<int>(phase) && wait > 1.5f * mean) wait = rng.Range(0.3f, 1.0f) * mean;
        phase = now;
        wait -= dt > 0.0f ? dt : 0.0f;
        if (wait > 0.0f) return false;
        wait = NextWait(rng, mean);
        return true;
    }

    Flash MakeDistantFlash(Rng& rng, Phase phase, float now)
    {
        Flash f;
        f.start = now;
        f.pulses = 1 + static_cast<int>(rng.Uniform() * 3.0f);
        if (f.pulses > 3) f.pulses = 3;
        float t = 0.0f, a = rng.Range(0.6f, 1.0f);
        for (int i = 0; i < f.pulses; ++i)
        {
            f.at[i] = t;
            f.amp[i] = a;
            t += rng.Range(0.05f, 0.18f);
            a *= rng.Range(0.4f, 0.9f);
        }
        f.decay = rng.Range(0.10f, 0.16f); // a glow in the cloud, softer than a stroke
        switch (phase)
        {
            case Phase::Light:  f.sky = rng.Range(0.15f, 0.28f); break;
            case Phase::Medium: f.sky = rng.Range(0.22f, 0.42f); break;
            default:            f.sky = rng.Range(0.30f, 0.55f); break;
        }
        f.scene = 0.15f * f.sky;
        return f;
    }

    Flash MakeStrike(Rng& rng, float distance, float now)
    {
        Flash f;
        f.start = now;
        f.strike = true;
        f.distance = distance;
        f.pulses = 2 + static_cast<int>(rng.Uniform() * 3.0f);
        if (f.pulses > kMaxPulses) f.pulses = kMaxPulses;
        float t = 0.0f, a = 1.0f;
        for (int i = 0; i < f.pulses; ++i)
        {
            f.at[i] = t;
            f.amp[i] = a;
            t += rng.Range(0.04f, 0.14f);
            a = rng.Range(0.45f, 0.95f);
        }
        f.decay = rng.Range(0.06f, 0.09f);
        const float near = 1.0f - Smooth(120.0f, 1500.0f, distance);
        f.sky = 0.55f + 0.45f * near;
        f.scene = 0.15f + 0.85f * near * near;
        return f;
    }

    float Envelope(const Flash& f, float now)
    {
        const float t = now - f.start;
        float v = 0.0f;
        for (int i = 0; i < f.pulses && i < kMaxPulses; ++i)
        {
            const float dt = t - f.at[i];
            if (dt < 0.0f) continue;
            const float p = dt < kPulseRise ? dt / kPulseRise : std::exp(-(dt - kPulseRise) / (f.decay > 0.001f ? f.decay : 0.001f));
            const float a = f.amp[i] * p;
            if (a > v) v = a;
        }
        return v;
    }

    float EndTime(const Flash& f)
    {
        float last = 0.0f;
        for (int i = 0; i < f.pulses && i < kMaxPulses; ++i)
            if (f.at[i] > last) last = f.at[i];
        return f.start + last + kPulseRise + 7.0f * f.decay; // e^-7 < 0.1%
    }

    float StrikeDistance(Rng& rng)
    {
        const float u = rng.Uniform();
        if (u < 0.07f) return rng.Range(kNearestStrike, 250.0f);
        if (u < 0.30f) return rng.Range(250.0f, 700.0f);
        return rng.Range(700.0f, kFarthestStrike);
    }

    float ThunderDelay(float distanceYards)
    {
        return (distanceYards > 0.0f ? distanceYards : 0.0f) * kYard / kSoundSpeed;
    }

    const char* ThunderSound(float d, float maxDistance)
    {
        if (!(d <= maxDistance)) return nullptr;
        if (d < 250.0f) return "StormPeaksLightning";          // 13021, volume 0.8: Silvermyst's bolt
        if (d < 600.0f) return "NetherstormLightningStrikes";  // 9843, 0.5: Netherstorm's crack
        if (d < 1000.0f) return "TK_Lightning_Slow";           // 10007, 0.4: the same recordings
        return "UL_lightning";                                 // 12779, 0.1: the same, far off
    }

    bool Identity(const Shade& s)
    {
        return s.dark <= 0.0f && s.desat <= 0.0f && s.sky <= 0.0f && s.scene <= 0.0f;
    }

    uint32_t ShadeBand(uint32_t argb, int band, const Shade& s)
    {
        if (band < 0 || band >= kBandCount) return argb;
        const Weights& w = kWeights[band];
        float c[3] = { static_cast<float>((argb >> 16) & 0xFF), static_cast<float>((argb >> 8) & 0xFF), static_cast<float>(argb & 0xFF) };
        const float keep = 1.0f - Clamp01(s.dark * w.dark);
        const float grey = Clamp01(s.desat * w.dark);
        const float luma = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
        const float add = 255.0f * kFlashGain * ((s.sky > 0.0f ? s.sky : 0.0f) * w.sky + (s.scene > 0.0f ? s.scene : 0.0f) * w.scene);
        uint32_t out = argb & 0xFF000000u;
        for (int k = 0; k < 3; ++k)
        {
            float v = (c[k] + (luma - c[k]) * grey) * keep + add * s.tint[k];
            v = v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v);
            out |= static_cast<uint32_t>(v + 0.5f) << (16 - 8 * k);
        }
        return out;
    }

    void ShadeBands(uint32_t bands[kBandCount], const Shade& s, BandMemory& last)
    {
        if (Identity(s)) { last.valid = false; return; }
        for (int i = 0; i < kBandCount; ++i)
        {
            uint32_t in = bands[i];
            if (last.valid && in == last.out[i] && in != last.in[i]) in = last.in[i];
            const uint32_t out = ShadeBand(in, i, s);
            last.in[i] = in;
            last.out[i] = out;
            bands[i] = out;
        }
        last.valid = true;
    }

    void BoltMatrix(const float pos[3], float yaw, float scale, float m[16])
    {
        const float c = std::cos(yaw) * scale, s = std::sin(yaw) * scale;
        m[0] = c;    m[1] = s;    m[2] = 0.0f;   m[3] = 0.0f;
        m[4] = -s;   m[5] = c;    m[6] = 0.0f;   m[7] = 0.0f;
        m[8] = 0.0f; m[9] = 0.0f; m[10] = scale; m[11] = 0.0f;
        m[12] = pos[0]; m[13] = pos[1]; m[14] = pos[2]; m[15] = 1.0f;
    }
}
