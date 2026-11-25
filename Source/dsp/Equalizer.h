#pragma once
#include "Biquad.h"
#include <array>
#include <atomic>

namespace sauce
{
struct EqBandParams
{
    bool  on = true;
    int   type = (int) FilterType::Bell;
    float freq = 1000.0f, gainDb = 0.0f, q = 1.0f;
};

/** Six-band parametric EQ with click-free parameter smoothing, per-band bypass and band solo ("listen"). */
class ParametricEQ
{
public:
    static constexpr int kBands = 6;
    std::array<EqBandParams, kBands> bands;
    bool enabled = true;
    std::atomic<int> soloBand { -1 };   // -1 = none

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (int b = 0; b < kBands; ++b)
        {
            cur[(size_t) b] = bands[(size_t) b];
            filt[(size_t) b].reset();
            filt[(size_t) b].c = design (cur[(size_t) b]);
            bandMix[(size_t) b].reset (sr, 25.0f, bands[(size_t) b].on ? 1.0f : 0.0f);
        }
        modMix.reset (sr, 25.0f, enabled ? 1.0f : 0.0f);
        soloMix.reset (sr, 25.0f, 0.0f); solo.reset(); counter = 0;
    }

    static BiquadCoefs designFor (const EqBandParams& p, double sr)
    {
        // gain is ignored by cut / notch types inside the RBJ design
        return BiquadCoefs::make ((FilterType) p.type, sr, p.freq, p.q, p.gainDb);
    }

    inline float process (float x)
    {
        if (--counter <= 0) { counter = 16; tick(); }
        float y = x;
        for (int b = 0; b < kBands; ++b)
        {
            const float m = bandMix[(size_t) b].next();
            const float f = filt[(size_t) b].process (y);
            y += m * (f - y);
        }
        const float s = solo.process (x);
        const float sm = soloMix.next();
        const float mm = modMix.next();
        const float eqOut = x + mm * (y - x);
        return eqOut + sm * (s - eqOut);
    }

private:
    BiquadCoefs design (const EqBandParams& p) const { return designFor (p, sr); }

    void tick()
    {
        modMix.setTarget (enabled ? 1.0f : 0.0f);
        for (int b = 0; b < kBands; ++b)
        {
            auto& c = cur[(size_t) b]; const auto& t = bands[(size_t) b];
            bandMix[(size_t) b].setTarget (t.on ? 1.0f : 0.0f);
            bool changed = c.type != t.type;
            c.type = t.type;
            const float k = 0.12f;
            auto approach = [&] (float& v, float target, bool logScale)
            {
                if (std::abs (v - target) < (logScale ? 0.001f * target : 0.002f)) { if (v != target) { v = target; changed = true; } return; }
                v = logScale ? v * std::pow (target / v, k) : v + k * (target - v); changed = true;
            };
            approach (c.freq, t.freq, true); approach (c.q, t.q, true); approach (c.gainDb, t.gainDb, false);
            if (changed) filt[(size_t) b].c = design (c);
        }
        const int sb = soloBand.load (std::memory_order_relaxed);
        soloMix.setTarget (sb >= 0 ? 1.0f : 0.0f);
        if (sb >= 0 && sb < kBands)
        {
            const auto& c = cur[(size_t) sb];
            const bool cut = c.type == (int) FilterType::LowCut || c.type == (int) FilterType::HighCut;
            solo.c = BiquadCoefs::make (FilterType::BandPass, sr, c.freq, cut ? 0.7 : std::max (0.4f, c.q), 0);
        }
    }

    double sr = 44100;
    std::array<EqBandParams, kBands> cur;
    std::array<Biquad, kBands> filt;
    std::array<Smoothed, kBands> bandMix;
    Smoothed modMix, soloMix; Biquad solo; int counter = 0;
};

//==============================================================================
/** Fixed half-octave bank of peaking filters that realises a Match EQ curve. */
class MatchFilterbank
{
public:
    static constexpr int kBands = 19;                        // 40 Hz ... 16 kHz, half-octave spacing
    static float centre (int i) { return 40.0f * std::pow (2.0f, 0.5f * (float) i); }
    static constexpr float kQ = 2.87f;                       // ~half-octave bandwidth

    std::array<float, kBands> gainsDb {};                    // written by processor (audio thread) from a lock-free snapshot
    bool enabled = true;

    void prepare (double sampleRate)
    {
        sr = sampleRate; curDb.fill (0.0f);
        for (int i = 0; i < kBands; ++i) { f[(size_t) i].reset(); f[(size_t) i].c = BiquadCoefs::make (FilterType::Bell, sr, centre (i), kQ, 0); }
        mix.reset (sr, 30.0f, enabled ? 1.0f : 0.0f); counter = 0;
    }
    inline float process (float x)
    {
        if (--counter <= 0) { counter = 32; tick(); }
        float y = x;
        for (int i = 0; i < kBands; ++i) if (centre (i) < sr * 0.45) y = f[(size_t) i].process (y);
        const float m = mix.next();
        return x + m * (y - x);
    }

    /** Fit filter gains so the summed response follows a target curve sampled at the band centres. */
    static std::array<float, kBands> fit (const std::array<float, kBands>& target, double sr)
    {
        std::array<float, kBands> g = target;
        for (int iter = 0; iter < 12; ++iter)
        {
            std::array<float, kBands> resp {};
            for (int i = 0; i < kBands; ++i)
                for (int j = 0; j < kBands; ++j)
                    resp[(size_t) i] += (float) BiquadCoefs::make (FilterType::Bell, sr, centre (j), kQ, g[(size_t) j]).magnitudeDb (centre (i), sr);
            for (int i = 0; i < kBands; ++i) g[(size_t) i] = clampv (g[(size_t) i] + 0.6f * (target[(size_t) i] - resp[(size_t) i]), -24.0f, 24.0f);
        }
        return g;
    }

private:
    void tick()
    {
        mix.setTarget (enabled ? 1.0f : 0.0f);
        for (int i = 0; i < kBands; ++i)
        {
            float& c = curDb[(size_t) i]; const float t = gainsDb[(size_t) i];
            if (std::abs (c - t) > 0.005f) { c += 0.15f * (t - c); f[(size_t) i].c = BiquadCoefs::make (FilterType::Bell, sr, centre (i), kQ, c); }
        }
    }
    double sr = 44100; std::array<Biquad, kBands> f; std::array<float, kBands> curDb {}; Smoothed mix; int counter = 0;
};
} // namespace sauce
