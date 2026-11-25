#pragma once
#include "DspCommon.h"

namespace sauce
{
//==============================================================================
/** Downward noise gate with hysteresis + hold so quiet word endings survive. */
struct NoiseGate
{
    float threshDb = -50, attackMs = 2, holdMs = 60, releaseMs = 150, rangeDb = -40;
    bool  enabled = true;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        det.prepare (sr, 0.3f, 25.0f);
        gain = 1.0f; open = true; holdLeft = 0;
        enabledMix.reset (sr, 20.0f, enabled ? 1.0f : 0.0f);
    }
    void update()
    {
        attCoef = tcCoef (std::max (0.05f, attackMs), sr);
        relCoef = tcCoef (std::max (1.0f, releaseMs), sr);
        holdSamples = (int) (holdMs * 0.001 * sr);
        floorGain = dbToGain (rangeDb);
        enabledMix.setTarget (enabled ? 1.0f : 0.0f);
    }
    inline float process (float x)
    {
        const float env = det.process (std::abs (x));
        const float lvl = gainToDb (env);
        if (lvl > threshDb)                  { open = true; holdLeft = holdSamples; }
        else if (lvl < threshDb - 4.0f)      { if (holdLeft > 0) --holdLeft; else open = false; }
        else if (holdLeft > 0)               { --holdLeft; }
        const float target = open ? 1.0f : floorGain;
        const float c = target > gain ? attCoef : relCoef;
        gain = target + c * (gain - target);
        const float m = enabledMix.next();
        const float g = 1.0f + m * (gain - 1.0f);
        lastGain = g;
        return x * g;
    }
    double sr = 44100; EnvFollower det; float gain = 1, attCoef = 0, relCoef = 0, floorGain = 0.01f, lastGain = 1;
    bool open = true; int holdLeft = 0, holdSamples = 0; Smoothed enabledMix;
};

//==============================================================================
/** Feed-forward soft-knee compressor with log-domain attack/release smoothing. */
struct Compressor
{
    float threshDb = -18, ratio = 3, attackMs = 10, releaseMs = 120, makeupDb = 0, kneeDb = 6;
    bool  enabled = true;

    void prepare (double sampleRate)
    {
        sr = sampleRate; grDb = 0;
        makeup.reset (sr, 30.0f, 1.0f);
        enabledMix.reset (sr, 20.0f, enabled ? 1.0f : 0.0f);
    }
    void update()
    {
        att = tcCoef (std::max (0.1f, attackMs), sr);
        rel = tcCoef (std::max (5.0f, releaseMs), sr);
        makeup.setTarget (dbToGain (makeupDb));
        enabledMix.setTarget (enabled ? 1.0f : 0.0f);
    }
    inline float staticCurve (float lvl) const
    {
        const float over = lvl - threshDb, slope = 1.0f / std::max (1.0f, ratio) - 1.0f;
        if (2.0f * over < -kneeDb) return 0.0f;
        if (std::abs (2.0f * over) <= kneeDb) { const float t = over + kneeDb * 0.5f; return slope * t * t / (2.0f * kneeDb); }
        return slope * over;
    }
    inline float process (float x)
    {
        const float lvl = gainToDb (std::abs (x), -120.0f);
        const float gc  = staticCurve (lvl);
        const float c   = gc < grDb ? att : rel;
        grDb = gc + c * (grDb - gc);
        const float m = enabledMix.next();
        const float g = dbToGain (grDb) * makeup.next();
        const float y = x * g;
        currentGrDb = m * grDb;
        return x + m * (y - x);
    }
    double sr = 44100; float att = 0, rel = 0, grDb = 0, currentGrDb = 0; Smoothed makeup, enabledMix;
};

//==============================================================================
/** Stereo look-ahead peak limiter. Gain is guaranteed never to exceed the ceiling
    (box-filtered minimum-hold envelope + final safety clamp). */
struct Limiter
{
    float ceilingDb = -1.0f, releaseMs = 80.0f;
    bool  enabled = true;
    static constexpr float kLookaheadMs = 1.5f;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        L = std::max (1, (int) std::round (kLookaheadMs * 0.001 * sr));
        dl[0].prepare (L + 2); dl[1].prepare (L + 2);
        req.assign ((size_t) (L + 1), 1.0f); hold.assign ((size_t) (L + 1), 1.0f);
        rp = 0; holdSum = (double) (L + 1); env = 1.0f; grDb = 0;
    }
    int latency() const { return lowLatency ? 0 : L; }
    /** Tracking mode: no look-ahead (0 samples). Peaks are caught instantly; the safety clamp still holds the ceiling. */
    void setLowLatency (bool ll) { if (ll != lowLatency) { lowLatency = ll; env = 1.0f; dl[0].clear(); dl[1].clear(); std::fill (req.begin(), req.end(), 1.0f); std::fill (hold.begin(), hold.end(), 1.0f); holdSum = (double) (L + 1); } }
    bool lowLatency = false;
    void update() { ceil = dbToGain (ceilingDb); relCoef = tcCoef (std::max (1.0f, releaseMs), sr); }

    inline void process (float& l, float& r)
    {
        if (lowLatency)
        {
            const float pk = std::max (std::abs (l), std::abs (r));
            const float need = (enabled && pk > ceil) ? ceil / pk : 1.0f;
            env = need < env ? need : need + relCoef * (env - need);
            l *= env; r *= env;
            if (enabled) { l = clampv (l, -ceil, ceil); r = clampv (r, -ceil, ceil); }
            grDb = gainToDb (env);
            return;
        }
        dl[0].push (l); dl[1].push (r);
        const float pk = std::max (std::abs (l), std::abs (r));
        const float need = (enabled && pk > ceil) ? ceil / pk : 1.0f;
        req[(size_t) rp] = need;
        float m = 1.0f; for (float v : req) m = std::min (m, v);          // min-hold over look-ahead window
        holdSum += (double) m - (double) hold[(size_t) rp];
        hold[(size_t) rp] = m;
        rp = (rp + 1) % (L + 1);
        const float avg = (float) (holdSum / (double) (L + 1));          // box filter -> smooth attack
        env = avg < env ? avg : avg + relCoef * (env - avg);
        l = dl[0].readInt (L) * env; r = dl[1].readInt (L) * env;
        if (enabled) { l = clampv (l, -ceil, ceil); r = clampv (r, -ceil, ceil); }
        grDb = gainToDb (env);
    }
    double sr = 44100; int L = 64, rp = 0; DelayLine dl[2]; std::vector<float> req, hold;
    double holdSum = 0; float env = 1, ceil = 0.89f, relCoef = 0, grDb = 0;
};
} // namespace sauce
