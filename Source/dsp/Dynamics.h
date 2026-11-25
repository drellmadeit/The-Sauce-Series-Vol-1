#pragma once
#include "DspCommon.h"
#include "Biquad.h"

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
    inline float process (float x) { return x * gainFor (std::abs (x)); }
    /** Gain for a detection level (linked stereo: pass max(|L|,|R|)). */
    inline float gainFor (float level)
    {
        const float env = det.process (level);
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
        return g;
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
    inline float process (float x) { return x * gainFor (std::abs (x)); }
    /** Gain for a detection level (linked stereo: pass max(|L|,|R|)). */
    inline float gainFor (float level)
    {
        const float lvl = gainToDb (level, -120.0f);
        const float gc  = staticCurve (lvl);
        const float c   = gc < grDb ? att : rel;
        grDb = gc + c * (grDb - gc);
        const float m = enabledMix.next();
        const float g = dbToGain (grDb) * makeup.next();
        currentGrDb = m * grDb;
        return 1.0f + m * (g - 1.0f);
    }
    double sr = 44100; float att = 0, rel = 0, grDb = 0, currentGrDb = 0; Smoothed makeup, enabledMix;
};

//==============================================================================
/** Split-band de-esser, zero latency. Only the band above `freqHz` is turned down when S/T/SH sounds
    cross the threshold, so the body of the voice is untouched. Linked stereo. `listen` outputs just the band it works on. */
struct DeEsser
{
    bool enabled = true, listen = false;
    float freqHz = 6500.0f, threshDb = -30.0f, rangeDb = 10.0f;

    void prepare (double sampleRate)
    {
        sr = sampleRate; env = 0; gr = 0; lastGrDb = 0;
        for (auto& f : hp) f.reset();
        for (auto& f : det) f.reset();
        enabledMix.reset (sr, 20.0f, enabled ? 1.0f : 0.0f);
        builtFreq = -1; update();
    }
    void update()
    {
        if (std::abs (freqHz - builtFreq) > 0.5f)
        {
            builtFreq = freqHz;
            // complementary split: band = x - lowpass(x) stays in phase with the voice, so turning it down really removes it
            for (auto& f : hp)  f.c = BiquadCoefs::make (FilterType::HighCut, sr, clampv (freqHz, 2000.0f, 16000.0f), 0.707, 0);
            for (auto& f : det) f.c = BiquadCoefs::make (FilterType::BandPass, sr, clampv (freqHz * 1.15f, 2000.0f, 18000.0f), 1.1, 0);
        }
        att = tcCoef (0.4f, sr); rel = tcCoef (60.0f, sr); grS = tcCoef (1.5f, sr);
        enabledMix.setTarget (enabled ? 1.0f : 0.0f);
    }
    inline void process (float& l, float& r, bool stereo)
    {
        const float hl = l - hp[0].process (l), hr = stereo ? r - hp[1].process (r) : hl;
        const float dl = det[0].process (l), dr = stereo ? det[1].process (r) : dl;
        const float a = std::max (std::abs (dl), std::abs (dr));
        env = a + (a > env ? att : rel) * (env - a);
        const float over = gainToDb (env * 1.41f, -120.0f) - threshDb;
        const float want = over > 0.0f ? std::min (rangeDb, over * 0.75f) : 0.0f;     // 4:1 above threshold, capped by range
        gr = want + grS * (gr - want);
        const float m = enabledMix.next();
        const float g = dbToGain (-gr * m);
        lastGrDb = -gr * m;
        if (listen && enabled) { l = hl; r = hr; return; }
        l -= (1.0f - g) * hl;
        r = stereo ? r - (1.0f - g) * hr : l;
    }
    double sr = 44100; Biquad hp[2], det[2]; float env = 0, gr = 0, att = 0, rel = 0, grS = 0, builtFreq = -1, lastGrDb = 0;
    Smoothed enabledMix;
};

//==============================================================================
/** Plosive filter (P / B pops, mic air blasts), zero latency.
    The voice is split at `freqHz` (the low band is recovered exactly: low + rest = input). When the low band jumps
    far above the voice's own mids - the signature of a plosive - only the low band is pulled down, fast,
    then released. Normal words with a full low end don't trigger it because they lift the mids too. Linked stereo. */
struct PlosiveFilter
{
    bool enabled = true; float sensitivity = 0.5f, freqHz = 150.0f, depthDb = 12.0f;
    void prepare (double sampleRate)
    {
        sr = sampleRate; lowEnv = midEnv = 0; gr = 0; lastGrDb = 0;
        for (auto& f : lp) f.reset();
        for (auto& f : mid) f.reset();
        enabledMix.reset (sr, 20.0f, enabled ? 1.0f : 0.0f);
        builtFreq = -1; update();
    }
    void update()
    {
        if (std::abs (freqHz - builtFreq) > 0.5f)
        {
            builtFreq = freqHz;
            // complementary split: low = x - highpass(x), in phase with the voice
            for (auto& f : lp) f.c = BiquadCoefs::make (FilterType::LowCut, sr, clampv (freqHz, 50.0f, 400.0f), 0.707, 0);
            for (auto& f : mid) f.c = BiquadCoefs::make (FilterType::BandPass, sr, 1200.0, 0.6, 0);
        }
        att = tcCoef (0.6f, sr); rel = tcCoef (70.0f, sr); grAtt = tcCoef (0.4f, sr); grRel = tcCoef (90.0f, sr);
        // how far the lows must jump above the mids to count as a pop: 14 dB (low sensitivity) .. 2 dB (high)
        triggerDb = 14.0f - 12.0f * clampv (sensitivity, 0.0f, 1.0f);
        enabledMix.setTarget (enabled ? 1.0f : 0.0f);
    }
    inline void process (float& l, float& r, bool stereo)
    {
        const float lowL = l - lp[0].process (l), lowR = stereo ? r - lp[1].process (r) : lowL;
        const float mL = mid[0].process (l), mR = stereo ? mid[1].process (r) : mL;
        const float a = std::max (std::abs (lowL), std::abs (lowR)), b = std::max (std::abs (mL), std::abs (mR));
        lowEnv = a + (a > lowEnv ? att : rel) * (lowEnv - a);
        midEnv = b + (b > midEnv ? att : rel) * (midEnv - b);
        const float lowDb = gainToDb (lowEnv, -120.0f), midDb = gainToDb (midEnv, -120.0f);
        const float excess = (lowDb - midDb) - triggerDb;
        const float want = (lowDb > -45.0f && excess > 0.0f) ? depthDb * clampv (excess / 6.0f, 0.0f, 1.0f) : 0.0f;
        gr = want + (want > gr ? grAtt : grRel) * (gr - want);
        const float m = enabledMix.next();
        const float g = dbToGain (-gr * m);
        lastGrDb = -gr * m;
        l -= (1.0f - g) * lowL;
        r = stereo ? r - (1.0f - g) * lowR : l;
    }
    double sr = 44100; Biquad lp[2], mid[2]; float lowEnv = 0, midEnv = 0, gr = 0, att = 0, rel = 0, grAtt = 0, grRel = 0;
    float triggerDb = 8.0f, builtFreq = -1, lastGrDb = 0; Smoothed enabledMix;
};

//==============================================================================
/** Stereo look-ahead peak limiter. Gain is guaranteed never to exceed the ceiling
    (box-filtered minimum-hold envelope + final safety clamp). */
struct Limiter
{
    float ceilingDb = -1.0f, releaseMs = 80.0f, driveDb = 0.0f;
    int   character = 0;                 // 0 Clean, 1 Punchy (faster release, more impact), 2 Loud (soft-clips peaks first, more volume)
    bool  enabled = true;
    static constexpr float kLookaheadMs = 1.0f;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        L = std::max (1, (int) std::round (kLookaheadMs * 0.001 * sr));
        dl[0].prepare (L + 2); dl[1].prepare (L + 2);
        req.assign ((size_t) (L + 1), 1.0f); hold.assign ((size_t) (L + 1), 1.0f);
        rp = 0; holdSum = (double) (L + 1); env = 1.0f; grDb = 0;
        drive.reset (sr, 30.0f, 1.0f);
    }
    int latency() const { return lowLatency ? 0 : L; }
    /** Tracking mode: no look-ahead (0 samples). Peaks are caught instantly; the safety clamp still holds the ceiling. */
    void setLowLatency (bool ll) { if (ll != lowLatency) { lowLatency = ll; env = 1.0f; dl[0].clear(); dl[1].clear(); std::fill (req.begin(), req.end(), 1.0f); std::fill (hold.begin(), hold.end(), 1.0f); holdSum = (double) (L + 1); } }
    bool lowLatency = false;
    void update()
    {
        ceil = dbToGain (ceilingDb);
        relCoef = tcCoef (std::max (1.0f, releaseMs * (character == 1 ? 0.45f : 1.0f)), sr);
        drive.setTarget (enabled ? dbToGain (driveDb) : 1.0f);
    }
    /** Loud character: smooth tanh knee that starts 3 dB under the ceiling, so peaks are rounded off instead of ducked. */
    inline float softClip (float x) const
    {
        const float k = ceil * 0.708f, a = std::abs (x);
        if (a <= k) return x;
        const float room = ceil * 1.15f - k;
        return std::copysign (k + room * std::tanh ((a - k) / room), x);
    }

    inline void process (float& l, float& r)
    {
        const float dg = drive.next();
        l *= dg; r *= dg;
        if (enabled && character == 2) { l = softClip (l); r = softClip (r); }
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
    double holdSum = 0; float env = 1, ceil = 0.89f, relCoef = 0, grDb = 0; Smoothed drive;
};
} // namespace sauce
