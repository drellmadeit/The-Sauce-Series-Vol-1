#pragma once
// Analysis code that runs OFF the audio thread (spectrum, Match EQ, Vocal Coach). No JUCE dependency.
#include "DspCommon.h"
#include "Equalizer.h"
#include "PitchCorrector.h"
#include <complex>
#include <string>
#include <deque>
#include <algorithm>
#include <cstdio>

namespace sauce
{
//==============================================================================
struct FFT
{
    explicit FFT (int order) : n (1 << order), win ((size_t) n), buf ((size_t) n)
    {
        for (int i = 0; i < n; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) (n - 1));
    }
    /** Windowed power spectrum of n samples -> n/2+1 bins (power, linear). */
    void power (const float* x, std::vector<float>& out)
    {
        for (int i = 0; i < n; ++i) buf[(size_t) i] = std::complex<float> (x[i] * win[(size_t) i], 0.0f);
        transform();
        out.resize ((size_t) (n / 2 + 1));
        const float norm = 4.0f / ((float) n * (float) n);
        for (int k = 0; k <= n / 2; ++k) out[(size_t) k] = std::norm (buf[(size_t) k]) * norm;
    }
    int size() const { return n; }
private:
    void transform()
    {
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (buf[(size_t) i], buf[(size_t) j]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const float ang = -kTwoPi / (float) len;
            const std::complex<float> wl (std::cos (ang), std::sin (ang));
            for (int i = 0; i < n; i += len)
            {
                std::complex<float> w (1.0f, 0.0f);
                for (int j = 0; j < len / 2; ++j)
                {
                    const auto u = buf[(size_t) (i + j)], v = buf[(size_t) (i + j + len / 2)] * w;
                    buf[(size_t) (i + j)] = u + v; buf[(size_t) (i + j + len / 2)] = u - v; w *= wl;
                }
            }
        }
    }
    int n; std::vector<float> win; std::vector<std::complex<float>> buf;
};

/** Energy (dB) in a frequency band of a power spectrum. */
inline float bandDb (const std::vector<float>& p, double sr, int fftSize, float lo, float hi)
{
    const double binHz = sr / fftSize;
    int a = std::max (1, (int) std::floor (lo / binHz)), b = std::min ((int) p.size() - 1, (int) std::ceil (hi / binHz));
    double s = 0; for (int k = a; k <= b; ++k) s += p[(size_t) k];
    return 10.0f * std::log10 ((float) std::max (1e-14, s));
}

//==============================================================================
/** Log-frequency grid shared by Match EQ and the analyser (1/12 octave, 30 Hz .. 16 kHz). */
struct LogGrid
{
    static constexpr int kPoints = 110;
    static float freq (int i) { return 30.0f * std::pow (2.0f, (float) i / 12.0f); }
};

/** Averages the tonal balance of a signal over "representative" (non-silent) frames. */
struct SpectrumAccumulator
{
    SpectrumAccumulator() : fft (12) { frame.assign (4096, 0.0f); reset(); }
    void reset() { sum.assign (LogGrid::kPoints, 0.0); frames = 0; skipped = 0; fill = 0; loudest = -100.0f; std::fill (frame.begin(), frame.end(), 0.0f); }
    bool voicedOnly = false;      // Match EQ captures: ignore breaths, sibilance, noise and quiet tails
    int skipped = 0; float loudest = -100.0f;
    void feed (const float* x, int n, double sr)
    {
        for (int i = 0; i < n; ++i)
        {
            frame[(size_t) fill++] = x[i];
            if (fill == (int) frame.size()) { analyse (sr); std::memmove (frame.data(), frame.data() + 2048, 2048 * sizeof (float)); fill = 2048; }
        }
    }
    bool hasData() const { return frames >= 8; }
    double seconds (double sr) const { return frames * 2048.0 / sr; }
    std::vector<float> averageDb() const
    {
        std::vector<float> r ((size_t) LogGrid::kPoints, -120.0f);
        if (frames == 0) return r;
        for (int i = 0; i < LogGrid::kPoints; ++i) r[(size_t) i] = 10.0f * std::log10 ((float) std::max (1e-14, sum[(size_t) i] / frames));
        return r;
    }
    int frames = 0;
private:
    void analyse (double sr)
    {
        double e = 0; for (float v : frame) e += v * v;
        const float lvl = gainToDb ((float) std::sqrt (e / frame.size()));
        if (lvl < -50.0f) return;     // skip silence / room tone
        if (voicedOnly)
        {
            // smart capture: only sung, tonal frames count - not breaths, S's, noise or quiet tails
            loudest = std::max (loudest, lvl);
            if (lvl < loudest - 18.0f) { ++skipped; return; }
        }
        fft.power (frame.data(), spec);
        if (voicedOnly)
        {
            const int a = (int) (300.0 * fft.size() / sr), b = std::min ((int) spec.size() - 1, (int) (4000.0 * fft.size() / sr));
            double lg = 0, ar = 0; int n = 0;
            for (int k = a; k <= b; ++k) { const double v = std::max (1e-20, (double) spec[(size_t) k]); lg += std::log (v); ar += v; ++n; }
            const double flat = n > 0 ? std::exp (lg / n) / (ar / n) : 1.0;          // spectral flatness: ~0 tonal, ~1 noise
            if (flat > 0.45) { ++skipped; return; }
        }
        for (int i = 0; i < LogGrid::kPoints; ++i)
        {
            const float f = LogGrid::freq (i);
            const float lo = f * std::pow (2.0f, -1.0f / 24.0f), hi = f * std::pow (2.0f, 1.0f / 24.0f);
            const float db = bandDb (spec, sr, fft.size(), lo, hi) - 10.0f * std::log10 (std::max (1.0f, hi - lo));  // per-Hz density
            sum[(size_t) i] += std::pow (10.0, db / 10.0);
        }
        ++frames;
    }
    FFT fft; std::vector<float> frame, spec; std::vector<double> sum; int fill = 0;
};

//==============================================================================
/** Turns two captured tonal balances into a Match EQ curve and filter-bank gains. */
struct MatchCurve
{
    /** Raw difference (reference - mine), level-normalised so only tonal balance is matched. */
    static std::vector<float> rawDifference (const std::vector<float>& mine, const std::vector<float>& ref)
    {
        std::vector<float> d ((size_t) LogGrid::kPoints, 0.0f);
        double acc = 0; int cnt = 0;
        for (int i = 0; i < LogGrid::kPoints; ++i)
        {
            d[(size_t) i] = ref[(size_t) i] - mine[(size_t) i];
            const float f = LogGrid::freq (i);
            if (f >= 150.0f && f <= 6000.0f) { acc += d[(size_t) i]; ++cnt; }
        }
        const float mean = cnt ? (float) (acc / cnt) : 0.0f;
        for (auto& v : d) v -= mean;
        return d;
    }
    /** Smoothing (octaves), strength (0..1) and maximum boost/cut (dB) applied to the raw difference. */
    static std::vector<float> shape (const std::vector<float>& raw, float smoothOct, float strength, float limitDb)
    {
        std::vector<float> out (raw.size(), 0.0f);
        if (raw.empty()) return out;
        const int half = std::max (0, (int) std::round (smoothOct * 12.0f * 0.5f));
        for (int i = 0; i < (int) raw.size(); ++i)
        {
            double s = 0; int c = 0;
            for (int k = i - half; k <= i + half; ++k) if (k >= 0 && k < (int) raw.size()) { s += raw[(size_t) k]; ++c; }
            out[(size_t) i] = clampv ((float) (s / c) * strength, -limitDb, limitDb);
        }
        return out;
    }
    static float sampleAt (const std::vector<float>& curve, float f)
    {
        if (curve.empty()) return 0.0f;
        const float pos = 12.0f * std::log2 (f / 30.0f);
        const int i = clampv ((int) std::floor (pos), 0, (int) curve.size() - 2);
        const float t = clampv (pos - (float) i, 0.0f, 1.0f);
        return curve[(size_t) i] + t * (curve[(size_t) i + 1] - curve[(size_t) i]);
    }
    /** Filter-bank gains for a shaped curve; final response is held inside +/- limitDb. */
    static std::array<float, MatchFilterbank::kBands> gainsFor (const std::vector<float>& shaped, float limitDb, double sr)
    {
        std::array<float, MatchFilterbank::kBands> target {};
        for (int i = 0; i < MatchFilterbank::kBands; ++i) target[(size_t) i] = sampleAt (shaped, MatchFilterbank::centre (i));
        auto g = MatchFilterbank::fit (target, sr);
        for (int pass = 0; pass < 4; ++pass)
        {
            const float peak = responsePeak (g, sr);
            if (peak <= limitDb + 0.05f) break;
            const float k = limitDb / peak * 0.98f;
            for (auto& v : g) v *= k;
        }
        return g;
    }
    static float responseDb (const std::array<float, MatchFilterbank::kBands>& g, float f, double sr)
    {
        double s = 0;
        for (int j = 0; j < MatchFilterbank::kBands; ++j)
            if (MatchFilterbank::centre (j) < sr * 0.45)
                s += BiquadCoefs::make (FilterType::Bell, sr, MatchFilterbank::centre (j), MatchFilterbank::kQ, g[(size_t) j]).magnitudeDb (f, sr);
        return (float) s;
    }
    static float responsePeak (const std::array<float, MatchFilterbank::kBands>& g, double sr)
    {
        float pk = 0; for (int i = 0; i < 160; ++i) pk = std::max (pk, std::abs (responseDb (g, 25.0f * std::pow (2.0f, i / 16.0f), sr)));
        return pk;
    }
};

//==============================================================================
/** Plain-language mix feedback measured over a rolling ~4 s window. */
class VocalCoach
{
public:
    struct Message { bool listening = true; int issue = -1; std::string observation, suggestion; };
    enum Issue { None = -1, Clipping = 0, OutputClip, Masking, Sibilance, LowMid, Uneven, TooQuiet, RoleLowEnd, RoleTune, RoleWidth };
    /** What the track is (0 Lead, 1 Hook, 2 Ad-libs, 3 Backgrounds, 4 Harmonies, 5 Doubles) plus a few current settings,
        so advice fits the part: backgrounds can be darker and steadier, ad-libs thinner and more dynamic, doubles tight, etc. */
    struct Context { int role = 0; bool tuneOn = false, stereoFx = false, deessOn = true; float hpfHz = 90.0f; };
    void setContext (const Context& c) { ctx = c; }
    static const char* roleWord (int r) { static const char* w[] = { "lead vocal", "hook", "ad-libs", "backgrounds", "harmonies", "doubles" }; return w[std::clamp (r, 0, 5)]; }

    VocalCoach() : fft (11) { reset(); }
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset()
    {
        frames.clear(); fill = 0; sinceEval = 0; current = Message{}; pendingIssue = -2; pendingCount = 0; heldSeconds = 0;
        lead.assign (2048, 0.0f); beat.assign (2048, 0.0f); in.assign (2048, 0.0f); out.assign (2048, 0.0f);
    }

    /** Feed equally-sized chunks. beatOrNull = sidechain mono when an instrumental is routed in. */
    void feed (const float* inSig, const float* leadSig, const float* outSig, const float* beatOrNull, int n, float ceilingLin, bool limiterOn)
    {
        for (int i = 0; i < n; ++i)
        {
            in[(size_t) fill] = inSig[i]; lead[(size_t) fill] = leadSig[i]; out[(size_t) fill] = outSig[i];
            beat[(size_t) fill] = beatOrNull ? beatOrNull[i] : 0.0f;
            if (++fill == 2048)
            {
                analyseFrame (beatOrNull != nullptr, ceilingLin, limiterOn);
                for (auto* v : { &in, &lead, &out, &beat }) std::memmove (v->data(), v->data() + 1024, 1024 * sizeof (float));
                fill = 1024;
                sinceEval += 1024;
                if (sinceEval >= (int) (0.5 * sr)) { sinceEval = 0; evaluate(); }
            }
        }
    }
    const Message& message() const { return current; }

    static constexpr int kBands = 22;                          // 1/3-octave, 80 Hz .. 10 kHz
    static float bandCentre (int b) { return 80.0f * std::pow (2.0f, (float) b / 3.0f); }

private:
    struct Frame { float inPeak, outPeak, leadDb; bool beatOn; float sibDb; float v[kBands]; float b[kBands]; };

    void analyseFrame (bool beatOn, float ceilingLin, bool limiterOn)
    {
        Frame f {};
        for (int i = 1024; i < 2048; ++i) { f.inPeak = std::max (f.inPeak, std::abs (in[(size_t) i])); f.outPeak = std::max (f.outPeak, std::abs (out[(size_t) i])); }
        double e = 0; for (float v : lead) e += v * v;
        f.leadDb = gainToDb ((float) std::sqrt (e / 2048.0));
        fft.power (lead.data(), spec);
        for (int k = 0; k < kBands; ++k) f.v[k] = bandDb (spec, sr, 2048, bandCentre (k) * 0.891f, bandCentre (k) * 1.122f);
        f.sibDb = bandDb (spec, sr, 2048, 5000, 10000) - bandDb (spec, sr, 2048, 300, 3000);
        f.beatOn = beatOn;
        if (beatOn)
        {
            double eb = 0; for (float v : beat) eb += v * v;
            f.beatOn = gainToDb ((float) std::sqrt (eb / 2048.0)) > -60.0f;
            fft.power (beat.data(), spec);
            for (int k = 0; k < kBands; ++k) f.b[k] = bandDb (spec, sr, 2048, bandCentre (k) * 0.891f, bandCentre (k) * 1.122f);
        }
        frames.push_back (f);
        ceil_ = ceilingLin; limOn_ = limiterOn;
        const size_t maxFrames = (size_t) (4.0 * sr / 1024.0);
        while (frames.size() > maxFrames) frames.pop_front();
    }

    struct Candidate { int issue; float score; std::string obs, sug; };

    static std::string hz (float f) { char b[32]; if (f >= 1000) std::snprintf (b, sizeof b, "%.1f kHz", f / 1000.0f); else std::snprintf (b, sizeof b, "%d Hz", (int) (std::round (f / 10.0f) * 10)); return b; }
    static std::string range (float lo, float hi)
    {
        char b[48];
        if (hi < 1000) std::snprintf (b, sizeof b, "%d-%d Hz", (int) (std::round (lo / 10) * 10), (int) (std::round (hi / 10) * 10));
        else std::snprintf (b, sizeof b, "%.1f-%.1f kHz", lo / 1000.0f, hi / 1000.0f);
        return b;
    }
    static std::string fmt (const char* f, float a) { char b[200]; std::snprintf (b, sizeof b, f, a); return b; }

    void evaluate()
    {
        std::vector<const Frame*> voiced;
        for (auto& f : frames) if (f.leadDb > -45.0f) voiced.push_back (&f);
        const double voicedSec = voiced.size() * 1024.0 / sr;
        if (voicedSec < 1.2) { setMessage (Message{}); return; }

        const int role = ctx.role;
        const bool stack = role == 3 || role == 4 || role == 5;            // backgrounds, harmonies, doubles
        const std::string forRole = std::string ("For ") + roleWord (role) + ": ";
        std::vector<Candidate> c;
        // 1. input clipping (baked into the recording or the clip gain)
        int clipFrames = 0; float maxIn = 0;
        for (auto& f : frames) { if (f.inPeak >= 0.989f) ++clipFrames; maxIn = std::max (maxIn, f.inPeak); }
        if (clipFrames >= 2)
            c.push_back ({ Clipping, 100.0f + clipFrames, "Your vocal is clipping before it reaches the plugin (peaks hit 0 dBFS).",
                           "Lower the recording level or clip gain by about 6 dB and re-check the take." });
        // 2. output clipping with the limiter switched off
        float maxOut = 0; for (auto& f : frames) maxOut = std::max (maxOut, f.outPeak);
        if (! limOn_ && maxOut > 1.0f)
            c.push_back ({ OutputClip, 95.0f, fmt ("The output is clipping, peaking about %.1f dB over 0 dBFS.", gainToDb (maxOut)),
                           fmt ("Switch the Limiter on, or lower OUTPUT by %.0f dB.", std::ceil (gainToDb (maxOut)) + 1.0f) });
        // 3. masking against the beat
        int beatFrames = 0; for (auto* f : voiced) if (f->beatOn) ++beatFrames;
        if (beatFrames > (int) voiced.size() / 2)
        {
            float V[kBands] {}, B[kBands] {};
            for (auto* f : voiced) if (f->beatOn) for (int k = 0; k < kBands; ++k) { V[k] += std::pow (10.0f, f->v[k] / 10.0f); B[k] += std::pow (10.0f, f->b[k] / 10.0f); }
            float vMax = -200; for (int k = 0; k < kBands; ++k) { V[k] = 10.0f * std::log10 (V[k] / beatFrames + 1e-14f); B[k] = 10.0f * std::log10 (B[k] / beatFrames + 1e-14f); }
            for (int k = 2; k < 17; ++k) vMax = std::max (vMax, V[k]);
            float best = 0; int bestK = -1;
            for (int k = 3; k < 17; ++k)                       // 160 Hz .. 4 kHz: where vocals live
            {
                if (V[k] < vMax - 15.0f) continue;
                const float over = (B[k] - V[k]) + (B[k + 1] - V[k + 1]);
                const float score = over + 6.0f;
                if (score > best) { best = score; bestK = k; }
            }
            if (bestK >= 0 && best > 8.0f)
            {
                const float lo = bandCentre (bestK), hi = bandCentre (bestK + 1), mid = std::sqrt (lo * hi);
                c.push_back ({ Masking, 40.0f + best * 3.0f,
                    "Your vocal and the beat are competing around " + range (lo, hi) + ".",
                    role == 0 || role == 1 ? "Try a broad 2 dB cut near " + hz (mid) + " on the beat's track and compare."
                                           : forRole + "cut 2-3 dB near " + hz (mid) + " on this track itself - it's a supporting part, so let the beat and the lead keep that range." });
            }
        }
        // 4. sibilance (stacked parts pile up their S's, so they're held to a stricter standard)
        const float sibThr = stack ? -10.0f : (role == 2 ? -8.0f : -6.0f);
        int sib = 0; float sibPk = -100;
        for (auto* f : voiced) { if (f->sibDb > sibThr) ++sib; sibPk = std::max (sibPk, f->sibDb); }
        if (sib > (int) voiced.size() / 8)
        {
            float bestF = 7000, bestV = -200; int cnt = 0; float acc[kBands] {};
            for (auto* f : voiced) if (f->sibDb > sibThr) { ++cnt; for (int k = 17; k < kBands; ++k) acc[k] += f->v[k]; }
            for (int k = 17; k < kBands; ++k) if (cnt && acc[k] / cnt > bestV) { bestV = acc[k] / cnt; bestF = bandCentre (k); }
            std::string sug = stack ? forRole + "S's stack up across layers. Set the De-Esser near " + hz (bestF) + " and lower its threshold 6 dB - these parts can be darker than the lead."
                            : role == 2 ? forRole + "set the De-Esser near " + hz (bestF) + " and lower its threshold 4 dB, so the S's don't poke out over the lead."
                            : ctx.deessOn ? "Set the De-Esser frequency near " + hz (bestF) + " and lower its threshold 3-4 dB, then listen to the S's."
                                          : "Switch the De-Esser on and set it near " + hz (bestF) + ".";
            c.push_back ({ Sibilance, 30.0f + (float) sib * 100.0f / (float) voiced.size(),
                           "S and T sounds are harsh, peaking around " + hz (bestF) + ".", sug });
        }
        // 5. low-mid build-up
        {
            float lm = 0, pres = 0;
            float bandAvg[kBands] {};
            for (auto* f : voiced) for (int k = 0; k < kBands; ++k) bandAvg[k] += f->v[k] / (float) voiced.size();
            for (int k = 5; k <= 8; ++k) lm += bandAvg[k] / 4.0f;            // 250 - 500 Hz
            for (int k = 11; k <= 15; ++k) pres += bandAvg[k] / 5.0f;        // 1 - 2.5 kHz
            const float lmThr = stack || role == 2 ? 8.0f : 12.0f;
            if (lm - pres > lmThr)
            {
                int pk = 5; for (int k = 5; k <= 8; ++k) if (bandAvg[k] > bandAvg[pk]) pk = k;
                c.push_back ({ LowMid, 20.0f + (lm - pres - lmThr) * 3.0f,
                               "The vocal sounds boxy: there's a build-up around " + hz (bandCentre (pk)) + ".",
                               stack || role == 2 ? forRole + "cut 4 dB near " + hz (bandCentre (pk)) + " - supporting parts should leave the body to the lead."
                                                  : "Try a broad 3 dB cut near " + hz (bandCentre (pk)) + " (Q 1.0) on the vocal." });
            }
            // role check: low end on supporting parts thickens the lead
            float low = 0; for (int k = 0; k <= 2; ++k) low += bandAvg[k] / 3.0f;     // 80-130 Hz
            if ((stack || role == 2) && low - pres > -4.0f && ctx.hpfHz < 180.0f)
                c.push_back ({ RoleLowEnd, 35.0f + (low - pres + 4.0f) * 2.0f,
                               std::string ("These ") + roleWord (role) + " carry a lot of low end, which muddies the lead when they're stacked.",
                               "Raise EQ band 1 (Low Cut) to " + std::string (role == 2 ? "250-350 Hz" : "200-300 Hz") + "." });
        }
        // 6. uneven level
        {
            double m = 0; for (auto* f : voiced) m += f->leadDb; m /= voiced.size();
            double v = 0; for (auto* f : voiced) v += (f->leadDb - m) * (f->leadDb - m); v = std::sqrt (v / voiced.size());
            const double unevenThr = stack ? 3.0 : (role == 2 ? 7.0 : 4.5);      // ad-libs are meant to jump out; stacks should be steady
            if (v > unevenThr)
                c.push_back ({ Uneven, 15.0f + (float) v * 2.0f, fmt ("Your vocal level jumps around by about %.0f dB between words.", (float) v * 2.0f),
                               stack ? forRole + "keep them steady so they glue: lower the Compressor threshold 6 dB and use a 6:1 ratio."
                                     : "Lower the Compressor threshold by 4 dB (or raise the ratio to 4:1) and compare." });
            if (m < -32.0)
                c.push_back ({ TooQuiet, 10.0f, fmt ("The vocal is quiet, averaging about %.0f dBFS.", (float) m),
                               fmt ("Raise INPUT by %.0f dB so the gate and compressor work as intended.", (float) std::round (-22.0 - m)) });
        }

        // role checks that depend on settings
        if ((role == 4 || role == 5) && ! ctx.tuneOn)
            c.push_back ({ RoleTune, 32.0f, std::string ("Auto Tune is off on these ") + roleWord (role) + ".",
                           "Turn Auto Tune on (Retune 10-20 ms) so they lock to the lead's pitch instead of beating against it." });
        if ((role == 3 || role == 4) && ! ctx.stereoFx)
            c.push_back ({ RoleWidth, 18.0f, std::string ("These ") + roleWord (role) + " are mono, sitting right on top of the lead.",
                           "Turn on Width or the Doubler (Vocal FX) so they surround the lead instead of covering it." });

        if (c.empty())
        {
            Message ok; ok.listening = false; ok.issue = None;
            ok.observation = std::string ("Nothing stands out for ") + roleWord (role) + ". Level, tone and S's are in a healthy range.";
            ok.suggestion  = role == 0 ? "Try the Delay and Reverb ducking to add space without crowding the words."
                           : role == 1 ? "Hooks can take a little more air and a wider doubler than the verses."
                           : role == 2 ? "Ad-libs: try the Phone Filter or Echo Throw presets, panned off-centre in your mixer."
                           : "Keep these 6-10 dB under the lead and a little darker so the lead stays in front.";
            setCandidate ({ None, 0.0f, ok.observation, ok.suggestion });
            return;
        }
        std::sort (c.begin(), c.end(), [] (const Candidate& a, const Candidate& b) { return a.score > b.score; });
        setCandidate (c.front());
    }

    void setCandidate (const Candidate& top)
    {
        heldSeconds += 0.5;
        if (current.listening || current.issue == top.issue)
        {
            if (current.listening || heldSeconds >= 4.0) { Message m; m.listening = false; m.issue = top.issue; m.observation = top.obs; m.suggestion = top.sug; current = m; if (heldSeconds >= 4.0) heldSeconds = 0; }
            pendingIssue = -2; pendingCount = 0; return;
        }
        // a different issue is now on top: require it twice in a row and a minimum display time
        if (pendingIssue == top.issue) ++pendingCount; else { pendingIssue = top.issue; pendingCount = 1; }
        if (pendingCount >= 2 && heldSeconds >= 3.0)
        {
            Message m; m.listening = false; m.issue = top.issue; m.observation = top.obs; m.suggestion = top.sug;
            current = m; heldSeconds = 0; pendingIssue = -2; pendingCount = 0;
        }
    }
    void setMessage (const Message& m) { current = m; heldSeconds = 0; pendingIssue = -2; pendingCount = 0; }

    double sr = 44100; FFT fft; std::vector<float> spec, lead, beat, in, out; int fill = 0, sinceEval = 0;
    std::deque<Frame> frames; Message current; int pendingIssue = -2, pendingCount = 0; double heldSeconds = 0;
    float ceil_ = 1.0f; bool limOn_ = true; Context ctx;
};

//==============================================================================
/** Song-key estimate from (a) the sung pitch (YIN on the vocal) and (b) the beat's chroma when routed.
    Krumhansl-Schmuckler key profiles, slowly forgetting the past so a key change is followed.
    Runs on the analysis thread only: it never touches the audio path, so it adds no latency. */
class KeyDetector
{
public:
    struct Estimate { bool valid = false, stable = false, minor = false; int key = 0; float confidence = 0; };

    void prepare (double sampleRate)
    {
        sr = sampleRate; dec = std::max (1, (int) std::round (sr / 11025.0)); ds = sr / dec;
        W = (int) (ds * 0.04); tauMax = (int) (ds / 70.0);
        vbuf.assign ((size_t) (W + tauMax + 2), 0.0f); scratch.assign ((size_t) tauMax + 2, 0.0f);
        bbuf.assign (4096, 0.0f); reset();
    }
    void reset()
    {
        std::fill (std::begin (vocal), std::end (vocal), 0.0); std::fill (std::begin (beat), std::end (beat), 0.0);
        vFill = 0; bFill = 0; decAcc = 0; decN = 0; sinceEval = 0; voicedSec = 0; beatSec = 0;
        est = {}; lastWinner = -1; winnerRuns = 0;
    }
    void feed (const float* vocalSig, const float* beatOrNull, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            decAcc += vocalSig[i];
            if (++decN == dec)
            {
                vbuf[(size_t) vFill++] = decAcc / (float) dec; decAcc = 0; decN = 0;
                if (vFill == (int) vbuf.size())
                {
                    analyseVocal();
                    const int keep = (int) vbuf.size() / 2;
                    std::memmove (vbuf.data(), vbuf.data() + vbuf.size() - (size_t) keep, (size_t) keep * sizeof (float)); vFill = keep;
                }
            }
            if (beatOrNull)
            {
                bbuf[(size_t) bFill++] = beatOrNull[i];
                if (bFill == 4096) { analyseBeat(); std::memmove (bbuf.data(), bbuf.data() + 2048, 2048 * sizeof (float)); bFill = 2048; }
            }
            if (++sinceEval >= (int) sr) { sinceEval = 0; evaluate(); }
        }
    }
    Estimate estimate() const { return est; }

    static constexpr double kMajor[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static constexpr double kMinor[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

private:
    void analyseVocal()
    {
        double e = 0; for (int i = 0; i < W; ++i) e += vbuf[(size_t) i] * vbuf[(size_t) i];
        const double hopSec = (vbuf.size() / 2) / ds;
        if (gainToDb ((float) std::sqrt (e / W)) < -45.0f) return;
        const float hz = PitchCorrector::yin (vbuf.data(), W, std::max (2, (int) (ds / 1000.0)), tauMax, ds, scratch.data());
        if (hz < 70.0f || hz > 1000.0f) return;
        const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        const int pc = (((int) std::round (midi)) % 12 + 12) % 12;
        vocal[pc] += hopSec; voicedSec += hopSec;
    }
    void analyseBeat()
    {
        double e = 0; for (float v : bbuf) e += v * v;
        if (gainToDb ((float) std::sqrt (e / bbuf.size())) < -55.0f) return;
        fft.power (bbuf.data(), spec);
        const double binHz = sr / 4096.0;
        double c[12] {}; double tot = 0;
        for (int k = (int) (60 / binHz); k < (int) (2000 / binHz) && k < (int) spec.size(); ++k)
        {
            const double f = k * binHz; const double m = std::sqrt ((double) spec[(size_t) k]);
            const int pc = (((int) std::round (69.0 + 12.0 * std::log2 (f / 440.0))) % 12 + 12) % 12;
            c[pc] += m; tot += m;
        }
        if (tot <= 0) return;
        const double hopSec = 2048.0 / sr;
        for (int p = 0; p < 12; ++p) beat[p] += hopSec * c[p] / tot;
        beatSec += hopSec;
    }
    void decay (double sec)
    {
        const double k = std::exp (-sec / 20.0);          // ~20 s memory: follows key changes
        for (auto& v : vocal) v *= k; for (auto& v : beat) v *= k;
        voicedSec *= k; beatSec *= k;
    }
    static double corr (const double* a, const double* b, int rot)
    {
        double ma = 0, mb = 0; for (int i = 0; i < 12; ++i) { ma += a[i]; mb += b[i]; } ma /= 12; mb /= 12;
        double num = 0, da = 0, db = 0;
        for (int i = 0; i < 12; ++i) { const double x = a[(i + rot) % 12] - ma, y = b[i] - mb; num += x * y; da += x * x; db += y * y; }
        return da > 0 && db > 0 ? num / std::sqrt (da * db) : 0.0;
    }
    void evaluate()
    {
        decay (1.0);                                       // once per second: older material fades (~20 s memory)
        double chroma[12] {}; double vs = 0, bs = 0;
        for (int i = 0; i < 12; ++i) { vs += vocal[i]; bs += beat[i]; }
        for (int i = 0; i < 12; ++i) chroma[i] = (vs > 0 ? vocal[i] / vs : 0.0) + (bs > 0 ? 1.5 * beat[i] / bs : 0.0);
        if (voicedSec < 3.0 && beatSec < 3.0) { est.valid = false; est.stable = false; return; }
        double best = -2, second = -2; int bestK = 0; bool bestMinor = false;
        for (int k = 0; k < 12; ++k)
            for (int m = 0; m < 2; ++m)
            {
                const double r = corr (chroma, m ? kMinor : kMajor, k);
                if (r > best) { second = best; best = r; bestK = k; bestMinor = m; } else if (r > second) second = r;
            }
        // relative major/minor share every note, so they never fight over the tuning: treat them as one winner
        const int winner = bestMinor ? (bestK + 3) % 12 : bestK;
        winnerRuns = winner == lastWinner ? winnerRuns + 1 : 1; lastWinner = winner;
        est.valid = best > 0.45; est.key = bestK; est.minor = bestMinor;
        est.confidence = (float) clampv (best, 0.0, 1.0);
        est.stable = est.valid && winnerRuns >= 3;
    }

    double sr = 44100, ds = 11025; int dec = 4, W = 441, tauMax = 157;
    std::vector<float> vbuf, scratch, bbuf, spec; int vFill = 0, bFill = 0, decN = 0, sinceEval = 0; float decAcc = 0;
    double vocal[12] {}, beat[12] {}, voicedSec = 0, beatSec = 0;
    FFT fft { 12 }; Estimate est; int lastWinner = -1, winnerRuns = 0;
};
} // namespace sauce
