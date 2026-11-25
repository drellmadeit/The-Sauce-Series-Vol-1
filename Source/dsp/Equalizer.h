#pragma once
#include "Biquad.h"
#include <array>
#include <atomic>
#include "DspCommon.h"

namespace sauce
{
struct EqBandParams
{
    bool  on = true;
    int   type = (int) FilterType::Bell;
    float freq = 1000.0f, gainDb = 0.0f, q = 1.0f;
    float dynDb = 0.0f, dynThreshDb = -30.0f;   // dynamic band: gain moves by up to dynDb while the band is above the threshold
};

/** Soothe-style dynamic resonance suppressor. 28 third-octave detectors compare each band with the smoothed
    spectrum around it; a band that rings out above its neighbours gets a narrow, fast, temporary cut,
    only as long as it sticks out. Zero latency (IIR detectors + peaking filters). */
class ResonanceSuppressor
{
public:
    static constexpr int kBands = 28;
    static float centre (int i) { return 125.0f * std::pow (2.0f, (float) i / 4.0f); }   // 125 Hz .. ~16 kHz, quarter-octave
    bool enabled = false, delta = false;
    float depthDb = 5.0f, sharpness = 0.5f, selectivity = 0.5f, speed = 0.5f, loHz = 400.0f, hiHz = 12000.0f;
    std::array<float, kBands> cutDb {};          // live cut per band (for the display)

    void prepare (double sampleRate)
    {
        sr = sampleRate; counter = 0; builtQ = -1;
        for (int i = 0; i < kBands; ++i) { det[(size_t) i].reset(); f[(size_t) i].reset(); env[(size_t) i] = 0; cur[(size_t) i] = 0; cutDb[(size_t) i] = 0; }
        mix.reset (sr, 25.0f, enabled ? 1.0f : 0.0f); update();
    }
    void update()
    {
        const float q = 2.0f + 8.0f * clampv (sharpness, 0.0f, 1.0f);
        if (std::abs (q - builtQ) > 0.01f)
        {
            builtQ = q;
            for (int i = 0; i < kBands; ++i)
            {
                det[(size_t) i].c = BiquadCoefs::make (FilterType::BandPass, sr, std::min (centre (i), (float) sr * 0.45f), 4.3, 0);
                f[(size_t) i].c = BiquadCoefs::make (FilterType::Bell, sr, std::min (centre (i), (float) sr * 0.45f), q, cur[(size_t) i]);
            }
        }
        const float sp = clampv (speed, 0.0f, 1.0f);
        att = tcCoef (1.0f + 9.0f * (1.0f - sp), sr); rel = tcCoef (25.0f + 175.0f * (1.0f - sp), sr);
        triggerDb = 7.5f - 5.0f * clampv (selectivity, 0.0f, 1.0f);       // how far above the neighbours before cutting
        mix.setTarget (enabled ? 1.0f : 0.0f);
    }
    inline float process (float x)
    {
        for (int i = 0; i < kBands; ++i)
        {
            const float a = std::abs (det[(size_t) i].process (x));
            float& e = env[(size_t) i];
            e = a + (a > e ? att : rel) * (e - a);
        }
        if (--counter <= 0) { counter = 32; tick(); }
        float y = x;
        for (int i = 0; i < kBands; ++i) if (active[(size_t) i]) y = f[(size_t) i].process (y);
        const float m = mix.next();
        if (delta && enabled) return (x - y) * m;            // listen to what is being removed
        return x + m * (y - x);
    }
private:
    void tick()
    {
        std::array<float, kBands> lv {};
        for (int i = 0; i < kBands; ++i) lv[(size_t) i] = gainToDb (env[(size_t) i], -120.0f);
        for (int i = 0; i < kBands; ++i)
        {
            const float c = centre (i);
            float want = 0;
            if (enabled && c >= loHz && c <= hiHz && c < sr * 0.45 && lv[(size_t) i] > -70.0f)
            {
                double s = 0; int n = 0;                            // smoothed spectrum: +-1 octave, excluding the band itself
                for (int k = std::max (0, i - 4); k <= std::min (kBands - 1, i + 4); ++k) if (std::abs (k - i) > 1) { s += lv[(size_t) k]; ++n; }
                const float around = n ? (float) (s / n) : lv[(size_t) i];
                const float excess = lv[(size_t) i] - around - triggerDb;
                if (excess > 0) want = -std::min (depthDb, excess * 1.2f);
            }
            float& g = cur[(size_t) i];
            const float k = want < g ? 0.5f : 0.12f;                 // dig in quickly, let go smoothly
            const float ng = g + k * (want - g);
            if (std::abs (ng - g) > 0.02f || (ng == 0.0f) != (g == 0.0f))
            {
                g = std::abs (ng) < 0.03f ? 0.0f : ng;
                f[(size_t) i].c = BiquadCoefs::make (FilterType::Bell, sr, std::min (c, (float) sr * 0.45f), builtQ, g);
            }
            active[(size_t) i] = g < -0.01f || std::abs (f[(size_t) i].z1) > 1e-9;
            cutDb[(size_t) i] = g;
        }
    }
    double sr = 44100; std::array<Biquad, kBands> det, f; std::array<float, kBands> env {}, cur {}; std::array<bool, kBands> active {};
    float att = 0, rel = 0, triggerDb = 3.5f, builtQ = -1; int counter = 0; Smoothed mix;
};

/** Twelve-band parametric EQ with dynamic bands, click-free parameter smoothing, per-band bypass and band solo ("listen"),
    followed by the resonance suppressor. */
class ParametricEQ
{
public:
    static constexpr int kBands = 12;
    std::array<EqBandParams, kBands> bands;
    bool enabled = true;
    std::atomic<int> soloBand { -1 };   // -1 = none
    ResonanceSuppressor res;
    std::array<float, kBands> dynNow {};   // current dynamic gain offset per band (display)

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (int b = 0; b < kBands; ++b)
        {
            cur[(size_t) b] = bands[(size_t) b];
            filt[(size_t) b].reset(); det[(size_t) b].reset(); denv[(size_t) b] = 0; dynNow[(size_t) b] = 0;
            filt[(size_t) b].c = design (cur[(size_t) b], 0.0f);
            bandMix[(size_t) b].reset (sr, 25.0f, bands[(size_t) b].on ? 1.0f : 0.0f);
        }
        modMix.reset (sr, 25.0f, enabled ? 1.0f : 0.0f);
        soloMix.reset (sr, 25.0f, 0.0f); solo.reset(); counter = 0;
        datt = tcCoef (5.0f, sr); drel = tcCoef (90.0f, sr);
        res.prepare (sr);
    }

    static BiquadCoefs designFor (const EqBandParams& p, double sr, float extraDb = 0.0f)
    {
        // gain is ignored by cut / notch types inside the RBJ design
        return BiquadCoefs::make ((FilterType) p.type, sr, p.freq, p.q, p.gainDb + extraDb);
    }
    static bool hasGain (int type) { return type == (int) FilterType::Bell || type == (int) FilterType::LowShelf || type == (int) FilterType::HighShelf; }

    inline float process (float x)
    {
        for (int b = 0; b < kBands; ++b)
            if (dynActive[(size_t) b])
            {
                const float a = std::abs (det[(size_t) b].process (x));
                float& e = denv[(size_t) b]; e = a + (a > e ? datt : drel) * (e - a);
            }
        if (--counter <= 0) { counter = 16; tick(); }
        float y = x;
        for (int b = 0; b < kBands; ++b)
        {
            const float m = bandMix[(size_t) b].next();
            if (m <= 0.0f && ! bands[(size_t) b].on) continue;
            const float f = filt[(size_t) b].process (y);
            y += m * (f - y);
        }
        y = res.process (y);
        const float s = solo.process (x);
        const float sm = soloMix.next();
        const float mm = modMix.next();
        const float eqOut = x + mm * (y - x);
        return eqOut + sm * (s - eqOut);
    }

private:
    BiquadCoefs design (const EqBandParams& p, float extra) const { return designFor (p, sr, extra); }

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
            // dynamic band: detector follows the band's frequency; gain offset follows how far the band is over its threshold
            const bool dyn = t.on && hasGain (t.type) && std::abs (t.dynDb) > 0.05f;
            if (dyn && (! dynActive[(size_t) b] || changed)) det[(size_t) b].c = BiquadCoefs::make (FilterType::BandPass, sr, c.freq, std::max (0.5f, c.q), 0);
            dynActive[(size_t) b] = dyn;
            float want = 0;
            if (dyn)
            {
                const float over = gainToDb (denv[(size_t) b] * 1.41f, -120.0f) - t.dynThreshDb;
                want = t.dynDb * clampv (over / 12.0f, 0.0f, 1.0f);
            }
            float& dn = dynNow[(size_t) b];
            if (std::abs (want - dn) > 0.01f) { dn += (std::abs (want) > std::abs (dn) ? 0.6f : 0.15f) * (want - dn); changed = true; }
            else if (dn != want) { dn = want; changed = true; }
            if (changed) filt[(size_t) b].c = design (c, dn);
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
    std::array<Biquad, kBands> filt, det;
    std::array<float, kBands> denv {};
    std::array<bool, kBands> dynActive {};
    std::array<Smoothed, kBands> bandMix;
    Smoothed modMix, soloMix; Biquad solo; int counter = 0; float datt = 0, drel = 0;
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
