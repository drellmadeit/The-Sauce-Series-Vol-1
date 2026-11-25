#pragma once
#include "DspCommon.h"
#include "Biquad.h"
#include <array>

namespace sauce
{
/** Real-time key/scale-aware pitch correction for a monophonic lead vocal.

    Detection : YIN on a decimated (~11 kHz) copy of the input, run every ~11 ms.
    Shifting  : pitch-synchronous dual-head delay-line shifter (grain = whole periods,
                so the two crossfaded heads stay phase-aligned on voiced sound).
    Unvoiced  : breaths, S/T/K consonants and silence crossfade to the untouched
                (latency-aligned) dry signal, so they are never pitch-processed.
    Latency   : fixed (getLatency()), independent of settings, so hosts can compensate. */
class PitchCorrector
{
public:
    enum Scale { Major = 0, Minor, HarmonicMinor, PentaMajor, PentaMinor, Blues, Chromatic, NumScales };

    // parameters (set from the processor, then call update())
    bool  enabled   = true;
    int   key       = 0;        // 0 = C ... 11 = B
    int   scale     = Major;
    float retuneMs  = 20.0f;    // 0 = instant (hard tune)
    float amount    = 1.0f;     // 0..1 correction depth
    float humanize  = 0.0f;     // 0..1 keeps small natural deviations (vibrato / scoops)
    float smooth    = 0.35f;    // 0..1 tunes the note's centre and lets vibrato/slides ride on top (scaled down at hard-tune speeds)

    /** Tracking (low-latency) mode: the dry/unvoiced path runs at kTrackDelay samples and tuned notes use the
        smallest possible grain (two periods), so a tuned note arrives about one cycle of the voice later.
        Mix mode keeps a fixed, fully compensated delay for the best quality. */
    static constexpr int kTrackDelay = 24;
    void setLowLatency (bool ll) { if (ll != lowLatency) { lowLatency = ll; reset(); } }
    bool isLowLatency() const { return lowLatency; }

    void prepare (double sampleRate)
    {
        sr  = sampleRate;
        dec = std::max (1, (int) std::round (sr / 11025.0));
        ds  = sr / dec;
        const double lpHz = std::min (2000.0, 0.4 * ds);
        aa[0].c = BiquadCoefs::make (FilterType::HighCut, sr, lpHz, 0.54, 0); aa[1].c = BiquadCoefs::make (FilterType::HighCut, sr, lpHz, 1.31, 0);
        hp.c    = BiquadCoefs::make (FilterType::LowCut, sr, 60.0, 0.707, 0);
        tauMin  = std::max (2, (int) (ds / 1000.0));
        tauMax  = (int) (ds / 70.0);
        W       = (int) std::round (ds * 0.040);
        hop     = std::max (16, (int) std::round (ds * 0.0105));
        dbuf.assign ((size_t) (W + tauMax + 2), 0.0f);
        diff.assign ((size_t) (tauMax + 2), 0.0f);

        maxPeriod = (float) (sr / 80.0);         // shifter grain sized for voices down to ~80 Hz (detection still goes to 70 Hz)
        grainMax  = 2.0f * maxPeriod;
        Dlat      = (int) std::ceil (grainMax * 0.5f) + 4;
        line.prepare ((int) (Dlat + grainMax) + 16);
        reset();
    }

    void reset()
    {
        line.clear(); for (auto& f : aa) f.reset(); hp.reset();
        std::fill (dbuf.begin(), dbuf.end(), 0.0f);
        decCount = 0; hopCount = 0; dbFill = 0;
        phase = 0; grain = (float) (0.020 * sr); ratioCents = 0; targetCents = 0; stage1 = 0; rampCents = 0; rampStep = 0; centreMidi = -1;
        voiced = false; voicedMix = 0; curNote = -1; hist[0] = hist[1] = hist[2] = 0;
        lastHz = 0; lastTargetMidi = -1; lastCorrection = 0;
    }

    int getLatency() const { return lowLatency ? kTrackDelay : Dlat; }
    int getMaxLatency() const { return Dlat; }

    void update()
    {
        // two cascaded stages at half the time each: same overall speed, but the glide starts and lands softly (S-curve)
        retuneCoef = retuneMs <= 0.5f ? 0.0f : tcCoef (retuneMs * 0.5f, sr);
        smoothEff = clampv (smooth, 0.0f, 1.0f) * clampv (retuneMs / 15.0f, 0.0f, 1.0f);
        frameLen = (float) (hop * dec);
    }

    inline float process (float x)
    {
        // ---- detection path (decimated) ----
        const float xh = hp.process (x);
        const float xa = aa[1].process (aa[0].process (xh));
        if (++decCount >= dec)
        {
            decCount = 0;
            std::memmove (dbuf.data(), dbuf.data() + 1, (dbuf.size() - 1) * sizeof (float));
            dbuf.back() = xa;
            if (dbFill < (int) dbuf.size()) ++dbFill;
            if (++hopCount >= hop) { hopCount = 0; analyse(); }
        }

        // ---- correction amount, smoothed per sample by retune speed ----
        targetSmoothed();
        line.push (x);
        const float ratio = std::pow (2.0f, ratioCents / 1200.0f);

        // ---- pitch-synchronous two-head shifter ----
        const float desiredGrain = voiced && lastHz > 0 ? grainFor ((float) (sr / lastHz)) : (lowLatency ? 2.0f * (float) (sr / 200.0) : (float) (0.020 * sr));
        grain += 0.002f * (desiredGrain - grain);
        phase += (ratio - 1.0f) / grain;
        phase -= std::floor (phase);
        float shifted = 0.0f;
        for (int h = 0; h < 2; ++h)
        {
            float q = phase + 0.5f * (float) h; q -= std::floor (q);
            const float centre = lowLatency ? (float) kTrackDelay + 0.5f * grain : (float) Dlat;
            const float d = centre + grain * (0.5f - q);
            const float w = std::sin (kPi * q);
            shifted += w * w * line.read (d);
        }
        const float dry = line.readInt (getLatency());

        // ---- voiced / unvoiced crossfade (breaths + consonants stay dry) ----
        const float tgt = (enabled && voiced && amount > 0.0f) ? 1.0f : 0.0f;
        voicedMix += (tgt > voicedMix ? 0.006f : 0.0025f) * (tgt - voicedMix) * (float) (48000.0 / sr) * 1.0f;
        return dry + voicedMix * (shifted - dry);
    }

    // read-outs for the UI (written on the audio thread, read via processor atomics)
    float lastHz = 0, lastTargetMidi = -1, lastCorrection = 0;
    bool  isVoiced() const { return voiced; }

    static bool noteInScale (int pitchClass, int key, int scale)
    {
        static const std::array<std::array<int, 12>, NumScales> masks {{
            {{1,0,1,0,1,1,0,1,0,1,0,1}}, // major
            {{1,0,1,1,0,1,0,1,1,0,1,0}}, // natural minor
            {{1,0,1,1,0,1,0,1,1,0,0,1}}, // harmonic minor
            {{1,0,1,0,1,0,0,1,0,1,0,0}}, // pentatonic major
            {{1,0,0,1,0,1,0,1,0,0,1,0}}, // pentatonic minor
            {{1,0,0,1,0,1,1,1,0,0,1,0}}, // blues
            {{1,1,1,1,1,1,1,1,1,1,1,1}}  // chromatic
        }};
        const int s = clampv (scale, 0, (int) NumScales - 1);
        return masks[(size_t) s][(size_t) (((pitchClass - key) % 12 + 12) % 12)] != 0;
    }

    /** Nearest allowed note (MIDI) to a fractional MIDI pitch. */
    static int nearestScaleNote (float midi, int key, int scale)
    {
        const int base = (int) std::round (midi);
        int best = base; float bestD = 1e9f;
        for (int o = -6; o <= 6; ++o)
        {
            const int n = base + o;
            if (! noteInScale (((n % 12) + 12) % 12, key, scale)) continue;
            const float d = std::abs ((float) n - midi);
            if (d < bestD) { bestD = d; best = n; }
        }
        return best;
    }

    /** Stand-alone YIN estimate (also used by tests). Returns Hz or 0 if unvoiced. */
    static float yin (const float* x, int W, int tauMin, int tauMax, double fs, float* scratch, float* aperiodicity = nullptr)
    {
        for (int t = 1; t <= tauMax; ++t)
        {
            double s = 0;
            for (int j = 0; j < W; ++j) { const float d = x[j] - x[j + t]; s += d * d; }
            scratch[t] = (float) s;
        }
        scratch[0] = 1.0f; double run = 0;
        for (int t = 1; t <= tauMax; ++t) { run += scratch[t]; scratch[t] = run > 0 ? (float) (scratch[t] * t / run) : 1.0f; }
        int tau = -1;
        for (int t = tauMin; t < tauMax; ++t)
            if (scratch[t] < 0.15f) { while (t + 1 < tauMax && scratch[t + 1] < scratch[t]) ++t; tau = t; break; }
        if (tau < 0)
        {
            float mn = 1e9f; int at = -1;
            for (int t = tauMin; t < tauMax; ++t) if (scratch[t] < mn) { mn = scratch[t]; at = t; }
            if (mn > 0.30f || at < 0) { if (aperiodicity) *aperiodicity = mn; return 0.0f; }
            tau = at;
        }
        if (aperiodicity) *aperiodicity = scratch[tau];
        float bt = (float) tau;
        if (tau > 1 && tau < tauMax - 1)
        {
            const float a = scratch[tau - 1], b = scratch[tau], c = scratch[tau + 1];
            const float den = a - 2 * b + c;
            if (std::abs (den) > 1e-9f) bt += 0.5f * (a - c) / den;
        }
        return (float) (fs / bt);
    }

private:
    float grainFor (float period) const
    {
        if (lowLatency) return std::min (grainMax, 2.0f * period);
        const float target = (float) (0.020 * sr);
        int k = 2 * (int) std::floor (target / (2.0f * period));
        k = std::max (2, k);
        return std::min (grainMax, period * (float) k);
    }

    void analyse()
    {
        if (dbFill < (int) dbuf.size()) { voiced = false; return; }
        double e = 0; for (int i = 0; i < W; ++i) e += dbuf[(size_t) i] * dbuf[(size_t) i];
        const float rmsDb = gainToDb ((float) std::sqrt (e / W));
        float ap = 1.0f;
        float hz = rmsDb > -52.0f ? yin (dbuf.data(), W, tauMin, tauMax, ds, diff.data(), &ap) : 0.0f;
        if (hz < 70.0f || hz > 1000.0f) hz = 0.0f;

        if (hz <= 0.0f) { voiced = false; lastHz = 0; targetCents = 0; lastCorrection = 0; centreMidi = -1; return; }

        // median of last three estimates rejects single-frame octave slips
        hist[0] = hist[1]; hist[1] = hist[2]; hist[2] = hz;
        float a = hist[0], b = hist[1], c = hist[2];
        float med = std::max (std::min (a, b), std::min (std::max (a, b), c));
        if (med <= 0.0f) med = hz;
        voiced = true; lastHz = med;

        const float midi = 69.0f + 12.0f * std::log2 (med / 440.0f);
        // slow-moving centre of the sung note (~120 ms); vibrato and small slides are measured against it
        if (centreMidi < 0.0f || std::abs (midi - centreMidi) > 0.8f) centreMidi = midi;
        else centreMidi += 0.09f * (midi - centreMidi);
        const float sung = midi + smoothEff * (centreMidi - midi);  // Smoothness: correct the centre, keep the wobble
        // the target note follows the (smoothed) sung pitch, so a wide vibrato can't flip between two notes
        int note = nearestScaleNote (sung, key, scale);
        // hysteresis: hold the current note until we are clearly closer to another
        if (curNote >= 0 && note != curNote && std::abs (sung - (float) curNote) < 0.62f
            && noteInScale (((curNote % 12) + 12) % 12, key, scale))
            note = curNote;
        curNote = note;

        const float dev = ((float) note - sung) * 100.0f;          // cents to move
        float shift = dev * amount;
        const float window = humanize * 35.0f;                     // humanize keeps small deviations
        if (window > 0.0f && std::abs (dev) < window) shift *= std::abs (dev) / window;
        targetCents = clampv (shift, -600.0f, 600.0f);
        rampStep = retuneCoef > 0.0f ? (targetCents - rampCents) / std::max (1.0f, frameLen) : 0.0f;   // ramp to the new target over one frame (no zipper steps)
        if (retuneCoef <= 0.0f) rampCents = targetCents;
        lastTargetMidi = (float) note; lastCorrection = targetCents;
    }

    inline void targetSmoothed()
    {
        if (rampStep != 0.0f)
        {
            rampCents += rampStep;
            if ((rampStep > 0.0f && rampCents >= targetCents) || (rampStep < 0.0f && rampCents <= targetCents)) { rampCents = targetCents; rampStep = 0.0f; }
        }
        else rampCents = targetCents;
        const float tgt = (enabled && voiced) ? rampCents : 0.0f;
        stage1 = tgt + retuneCoef * (stage1 - tgt);
        ratioCents = stage1 + retuneCoef * (ratioCents - stage1);
    }

    double sr = 44100, ds = 11025; int dec = 4, tauMin = 10, tauMax = 160, W = 441, hop = 116;
    Biquad aa[2], hp; std::vector<float> dbuf, diff; int decCount = 0, hopCount = 0, dbFill = 0;
    DelayLine line; int Dlat = 512; float maxPeriod = 600, grainMax = 1200, grain = 800, phase = 0;
    float ratioCents = 0, targetCents = 0, retuneCoef = 0, voicedMix = 0; bool voiced = false;
    float stage1 = 0, rampCents = 0, rampStep = 0, frameLen = 512, centreMidi = -1, smoothEff = 0;
    int curNote = -1; float hist[3] {}; bool lowLatency = false;
};
} // namespace sauce
