#pragma once
#include "DspCommon.h"
#include "Biquad.h"
#include <array>

namespace sauce
{
//==============================================================================
/** tanh saturation with first-order anti-derivative anti-aliasing and level compensation. */
struct Saturation
{
    bool enabled = false; float driveDb = 6.0f, mix = 0.5f;
    void prepare (double sr) { mixS.reset (sr, 30.0f, enabled ? mix : 0.0f); driveS.reset (sr, 30.0f, dbToGain (driveDb)); xPrev = 0; }
    void update() { mixS.setTarget (enabled ? mix : 0.0f); driveS.setTarget (dbToGain (driveDb)); }
    static inline double F1 (double x) { const double ax = std::abs (x); return ax > 20 ? ax - 0.6931471805599453 : std::log (std::cosh (x)); }
    inline float process (float x)
    {
        const float d = driveS.next(), m = mixS.next();
        const double u = (double) x * d, up = (double) xPrev * d; xPrev = x;
        double y;
        if (std::abs (u - up) < 1e-5) y = std::tanh (0.5 * (u + up));
        else y = (F1 (u) - F1 (up)) / (u - up);
        const float comp = 1.0f / std::max (1.0f, std::sqrt (d));     // keep perceived level roughly constant
        return x + m * ((float) y * comp * 1.1f - x);
    }
    Smoothed mixS, driveS; float xPrev = 0;
};

//==============================================================================
/** Two short, slowly-drifting delayed copies panned L/R: an artificial double track. Lead stays centred. */
struct Doubler
{
    bool enabled = false; float amount = 0.4f;
    void prepare (double s) { sr = s; dl.prepare ((int) (0.06 * sr)); amt.reset (sr, 30.0f, enabled ? amount : 0.0f); ph[0] = 0.0f; ph[1] = 0.37f; hp.c = BiquadCoefs::make (FilterType::LowCut, sr, 180, 0.7, 0); hp.reset(); }
    void update() { amt.setTarget (enabled ? amount : 0.0f); }
    inline void process (float lead, float& l, float& r)
    {
        dl.push (hp.process (lead));
        const float a = amt.next();
        if (a <= 0.0f) return;
        static constexpr float baseMs[2] = { 13.0f, 21.0f }, depthMs[2] = { 1.6f, 2.1f }, rate[2] = { 0.31f, 0.43f };
        float v[2];
        for (int i = 0; i < 2; ++i)
        {
            ph[i] += rate[i] / (float) sr; if (ph[i] >= 1.0f) ph[i] -= 1.0f;
            const float d = (baseMs[i] + depthMs[i] * std::sin (kTwoPi * ph[i])) * 0.001f * (float) sr;
            v[i] = dl.read (d);
        }
        l += a * 0.8f * v[0]; r += a * 0.8f * v[1];
    }
    double sr = 44100; DelayLine dl; Smoothed amt; float ph[2]; Biquad hp;
};

//==============================================================================
/** Chorus-style widener: modulated copies added in anti-phase to the sides (mid stays untouched). */
struct Widener
{
    bool enabled = false; float amount = 0.3f;
    void prepare (double s) { sr = s; dl.prepare ((int) (0.04 * sr)); amt.reset (sr, 30.0f, enabled ? amount : 0.0f); for (int i = 0; i < 3; ++i) ph[i] = 0.33f * (float) i; hp.c = BiquadCoefs::make (FilterType::LowCut, sr, 250, 0.7, 0); hp.reset(); }
    void update() { amt.setTarget (enabled ? amount : 0.0f); }
    inline void process (float lead, float& l, float& r)
    {
        dl.push (hp.process (lead));
        const float a = amt.next();
        if (a <= 0.0f) return;
        float side = 0;
        for (int i = 0; i < 3; ++i)
        {
            ph[i] += 0.8f / (float) sr * (1.0f + 0.3f * (float) i); if (ph[i] >= 1.0f) ph[i] -= 1.0f;
            const float d = (7.0f + 3.0f * (float) i + 2.5f * std::sin (kTwoPi * ph[i])) * 0.001f * (float) sr;
            side += (i == 1 ? -1.0f : 1.0f) * dl.read (d);
        }
        side *= 0.45f * a;
        l += side; r -= side;
    }
    double sr = 44100; DelayLine dl; Smoothed amt; float ph[3]; Biquad hp;
};

//==============================================================================
/** Follows the DRY lead vocal and produces a ducking gain for one wet return.
    Each return owns its own Ducker so depths are fully independent. */
struct Ducker
{
    float depth = 0.0f;                   // 0..1  (1 = return drops by kMaxDuckDb while singing)
    static constexpr float kMaxDuckDb = 36.0f;
    void prepare (double s) { sr = s; g = 1.0f; atk = tcCoef (12.0f, sr); rel = tcCoef (160.0f, sr); }
    /** activity: 0 (silence) .. 1 (clearly delivering a line) from the shared detector. */
    inline float process (float activity)
    {
        const float target = dbToGain (-kMaxDuckDb * depth * activity);
        const float c = target < g ? atk : rel;
        g = target + c * (g - target);
        return g;
    }
    double sr = 44100; float g = 1, atk = 0, rel = 0;
};

/** Shared "is the singer delivering a line" detector on the dry lead. */
struct VoiceActivity
{
    // Fast release (in dB terms ~0.2 s to fall 40 dB) so wet returns can bloom into short gaps between lines.
    void prepare (double s) { env.prepare (s, 3.0f, 30.0f); }
    inline float process (float dryLead)
    {
        const float e = env.process (std::abs (dryLead));
        const float db = gainToDb (e);
        return clampv ((db + 48.0f) / 16.0f, 0.0f, 1.0f);            // -48 dBFS -> 0, -32 dBFS and up -> 1
    }
    EnvFollower env;
};

//==============================================================================
/** Tempo-syncable stereo / ping-pong delay with filtered feedback. */
struct StereoDelay
{
    bool  enabled = true, pingPong = false;
    float timeMs = 375.0f, feedback = 0.35f, lowCutHz = 250.0f, highCutHz = 6000.0f, mix = 0.25f;

    void prepare (double s)
    {
        sr = s; for (auto& d : dl) d.prepare ((int) (4.1 * sr));
        cur = target = timeMs * 0.001f * (float) sr;
        mixS.reset (sr, 30.0f, enabled ? mix : 0.0f); fbS.reset (sr, 30.0f, feedback);
        for (auto& f : lc) f.reset();
        for (auto& f : hc) f.reset();
        update();
    }
    void update()
    {
        target = clampv (timeMs, 1.0f, 4000.0f) * 0.001f * (float) sr;
        mixS.setTarget (enabled ? mix : 0.0f); fbS.setTarget (clampv (feedback, 0.0f, 0.95f));
        for (int c = 0; c < 2; ++c) { lc[c].c = BiquadCoefs::make (FilterType::LowCut, sr, lowCutHz, 0.707, 0); hc[c].c = BiquadCoefs::make (FilterType::HighCut, sr, highCutHz, 0.707, 0); }
    }
    /** in: processed vocal (stereo). Writes the wet return (pre-ducking) into wl/wr. */
    inline void process (float inL, float inR, float& wl, float& wr)
    {
        cur += 0.0008f * (target - cur);                       // smooth time changes (tape-style glide, no clicks)
        const float fb = fbS.next(), m = mixS.next();
        const float yl = dl[0].read (cur), yr = dl[1].read (pingPong ? cur : cur * 1.012f);
        const float fl = hc[0].process (lc[0].process (yl)), fr = hc[1].process (lc[1].process (yr));
        if (pingPong)
        {
            const float mono = 0.5f * (inL + inR);
            dl[0].push (mono + fb * fr);  dl[1].push (fb * fl);
        }
        else { dl[0].push (inL + fb * fl); dl[1].push (inR + fb * fr); }
        float ol = fl, orr = fr; flushDenormal (ol); flushDenormal (orr);
        wl = ol * m; wr = orr * m;
    }
    double sr = 44100; DelayLine dl[2]; float cur = 0, target = 0; Smoothed mixS, fbS; Biquad lc[2], hc[2];
};

//==============================================================================
/** 8-line feedback-delay-network reverb with Room / Plate / Hall voicings. */
struct FdnReverb
{
    enum Type { Room = 0, Plate, Hall };
    bool  enabled = true; int type = Plate;
    float preDelayMs = 20.0f, decayS = 2.0f, damping = 0.4f, mix = 0.25f;

    void prepare (double s)
    {
        sr = s; pre.prepare ((int) (0.31 * sr));
        for (auto& d : lines) d.prepare ((int) (0.2 * sr));
        for (auto& a : diff) a.prepare ((int) (0.03 * sr));
        mixS.reset (sr, 30.0f, enabled ? mix : 0.0f);
        lfo = 0; std::fill (std::begin (lp), std::end (lp), 0.0f); builtType = -1; update();
    }
    void update()
    {
        if (builtType != type) buildType();
        const float rt = clampv (decayS, 0.1f, 12.0f);
        for (int i = 0; i < 8; ++i) g[i] = std::pow (10.0f, -3.0f * len[i] / (rt * (float) sr));
        const float baseCut = type == Plate ? 0.25f : 0.0f;          // plates stay brighter
        dampCoef = clampv (damping * 0.85f - baseCut * damping, 0.0f, 0.95f);
        preS = clampv (preDelayMs, 0.0f, 300.0f) * 0.001f * (float) sr;
        mixS.setTarget (enabled ? mix : 0.0f);
    }
    inline void process (float inL, float inR, float& wl, float& wr)
    {
        pre.push (0.5f * (inL + inR));
        float x = pre.read (std::max (1.0f, preS));
        for (int i = 0; i < 4; ++i)                                 // input diffusion (allpasses)
        {
            const float d = diff[i].readInt (diffLen[i]);
            const float v = x + 0.62f * d; diff[i].push (v); x = d - 0.62f * v;
        }
        lfo += 0.6f / (float) sr; if (lfo >= 1.0f) lfo -= 1.0f;
        const float mod = 6.0f * std::sin (kTwoPi * lfo);
        float o[8];
        for (int i = 0; i < 8; ++i)
        {
            const float dl_ = len[i] + (i < 2 ? (i == 0 ? mod : -mod) : 0.0f);
            float y = lines[i].read (dl_);
            lp[i] = y + dampCoef * (lp[i] - y); flushDenormal (lp[i]);
            o[i] = lp[i] * g[i];
        }
        // Householder feedback matrix: y = x - (2/N) * sum
        float sum = 0; for (float v : o) sum += v; sum *= 0.25f;
        for (int i = 0; i < 8; ++i) lines[i].push (o[i] - sum + (i & 1 ? -x : x) * 0.35f);
        const float m = mixS.next();
        wl = (o[0] + o[2] + o[4] + o[6]) * 0.5f * m;
        wr = (o[1] + o[3] + o[5] + o[7]) * 0.5f * m;
    }
private:
    void buildType()
    {
        builtType = type;
        static const float roomMs[8]  = { 11.3f, 13.7f, 16.1f, 18.9f, 21.7f, 24.1f, 27.3f, 30.1f };
        static const float plateMs[8] = { 17.1f, 19.3f, 23.7f, 26.9f, 31.1f, 35.3f, 38.9f, 43.7f };
        static const float hallMs[8]  = { 37.1f, 43.9f, 51.3f, 59.7f, 67.1f, 73.9f, 83.3f, 97.1f };
        static const float diffRoom[4] = { 3.1f, 4.7f, 6.3f, 8.9f }, diffPlate[4] = { 1.7f, 2.9f, 4.1f, 5.3f }, diffHall[4] = { 4.3f, 6.7f, 9.1f, 12.7f };
        const float* m = type == Room ? roomMs : type == Plate ? plateMs : hallMs;
        const float* dm = type == Room ? diffRoom : type == Plate ? diffPlate : diffHall;
        for (int i = 0; i < 8; ++i) len[i] = m[i] * 0.001f * (float) sr;
        for (int i = 0; i < 4; ++i) diffLen[i] = std::max (1, (int) (dm[i] * 0.001f * (float) sr));
        for (auto& d : lines) d.clear();
        for (auto& a : diff) a.clear();
    }
    double sr = 44100; DelayLine pre, lines[8], diff[4]; float len[8] {}, g[8] {}, lp[8] {}; int diffLen[4] {};
    float dampCoef = 0.3f, preS = 0, lfo = 0; int builtType = -1; Smoothed mixS;
};
} // namespace sauce
