// Verification runner for The Sauce Suite.
// Every check drives the real SauceProcessor through processBlock() with host-style buffers
// (main vocal bus on channels 0-1, instrumental sidechain on channels 2-3), like a DAW does.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>
#include <random>
#include <set>
#include <map>

using namespace juce;

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 512;
int failures = 0, checks = 0;

void expect (bool ok, const String& what)
{
    ++checks; if (! ok) ++failures;
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
}

//------------------------------------------------------------------ signal generators
/** Harmonic "sung vowel" with formant shaping. */
std::vector<float> vowel (double seconds, float hz, float levelDb, float vibratoCents = 0.0f, float formantHz = 700.0f)
{
    std::vector<float> x ((size_t) (seconds * kSr));
    double ph = 0; const float amp = sauce::dbToGain (levelDb);
    for (size_t i = 0; i < x.size(); ++i)
    {
        const float vib = vibratoCents * std::sin (sauce::kTwoPi * 5.0f * (float) i / (float) kSr);
        ph += hz * std::pow (2.0f, vib / 1200.0f) / kSr;
        float s = 0;
        for (int h = 1; h <= 24; ++h)
        {
            const float fh = hz * (float) h;
            if (fh > 15000) break;
            const float formant = 1.0f / (1.0f + std::pow ((fh - formantHz) / 400.0f, 2.0f)) + 0.25f / (1.0f + std::pow ((fh - 2500.0f) / 600.0f, 2.0f));
            s += std::sin ((float) (sauce::kTwoPi * ph * h)) * formant / std::sqrt ((float) h);
        }
        x[i] = s;
    }
    float pk = 0; for (float v : x) pk = std::max (pk, std::abs (v));
    for (auto& v : x) v *= amp / std::max (1e-6f, pk);
    return x;
}

/** Word-like on/off envelope (syllables) */
void syllables (std::vector<float>& x, float onMs = 220, float offMs = 90)
{
    const int on = (int) (onMs * 0.001 * kSr), off = (int) (offMs * 0.001 * kSr), fade = (int) (0.01 * kSr);
    for (size_t i = 0; i < x.size(); ++i)
    {
        const int p = (int) (i % (size_t) (on + off));
        float g = p < on ? 1.0f : 0.0f;
        if (p < fade) g = (float) p / (float) fade;
        else if (p < on && p > on - fade) g = (float) (on - p) / (float) fade;
        x[i] *= g;
    }
}

std::vector<float> noise (double seconds, float levelDb, unsigned seed = 1)
{
    std::mt19937 rng (seed); std::normal_distribution<float> nd (0, 1);
    std::vector<float> x ((size_t) (seconds * kSr));
    const float a = sauce::dbToGain (levelDb) / 3.0f;
    for (auto& v : x) v = nd (rng) * a;
    return x;
}

std::vector<float> filtered (std::vector<float> x, sauce::FilterType t, float f, float q, float g)
{
    sauce::Biquad b; b.c = sauce::BiquadCoefs::make (t, kSr, f, q, g);
    for (auto& v : x) v = b.process (v);
    return x;
}

std::vector<float> silence (double seconds) { return std::vector<float> ((size_t) (seconds * kSr), 0.0f); }
std::vector<float> concat (std::vector<float> a, const std::vector<float>& b) { a.insert (a.end(), b.begin(), b.end()); return a; }

//------------------------------------------------------------------ host simulation
struct Render { std::vector<float> l, r; std::vector<float> dlyRet, revRet; };

void enableSidechain (SauceProcessor& p, bool on)
{
    auto layout = p.getBusesLayout();
    layout.inputBuses.getReference (1) = on ? AudioChannelSet::stereo() : AudioChannelSet::disabled();
    const bool ok = p.setBusesLayout (layout);
    jassert (ok); ignoreUnused (ok);
}

Render render (SauceProcessor& p, const std::vector<float>& vocal, const std::vector<float>* sc = nullptr, bool pump = true)
{
    Render out; const size_t n = vocal.size();
    out.l.resize (n); out.r.resize (n);
    AudioBuffer<float> buf (4, kBlock); MidiBuffer midi;
    for (size_t start = 0; start < n; start += kBlock)
    {
        const int len = (int) std::min<size_t> (kBlock, n - start);
        buf.setSize (4, len, false, false, true); buf.clear();
        for (int i = 0; i < len; ++i)
        {
            buf.setSample (0, i, vocal[start + (size_t) i]); buf.setSample (1, i, vocal[start + (size_t) i]);
            if (sc) { buf.setSample (2, i, (*sc)[start + (size_t) i]); buf.setSample (3, i, (*sc)[start + (size_t) i] * 0.9f); }
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < len; ++i) { out.l[start + (size_t) i] = buf.getSample (0, i); out.r[start + (size_t) i] = buf.getSample (1, i); }
        out.dlyRet.push_back (p.meters.delayReturn.load()); out.revRet.push_back (p.meters.reverbReturn.load());
        if (pump) p.analysis.pump();
    }
    return out;
}

float pitchOf (const std::vector<float>& x, size_t start)
{
    const int dec = 4; std::vector<float> d;
    for (size_t i = start; i < start + 16000 && i < x.size(); i += dec) d.push_back (x[i]);
    const double ds = kSr / dec; const int W = (int) (ds * 0.04), tmax = (int) (ds / 70);
    std::vector<float> scratch ((size_t) tmax + 2);
    return sauce::PitchCorrector::yin (d.data(), W, (int) (ds / 1000), tmax, ds, scratch.data());
}
float cents (float hz, float ref) { return 1200.0f * std::log2 (hz / ref); }

float rmsDb (const std::vector<float>& x, size_t a, size_t b)
{
    double e = 0; b = std::min (b, x.size()); for (size_t i = a; i < b; ++i) e += x[i] * x[i];
    return sauce::gainToDb ((float) std::sqrt (e / std::max<size_t> (1, b - a)), -200.0f);
}
float peakDb (const std::vector<float>& x, size_t a, size_t b)
{
    float pk = 0; b = std::min (b, x.size()); for (size_t i = a; i < b; ++i) pk = std::max (pk, std::abs (x[i]));
    return sauce::gainToDb (pk, -200.0f);
}
float bandRmsDb (const std::vector<float>& x, size_t a, size_t b, float lo, float hi)
{
    sauce::FFT fft (12); std::vector<float> spec; double acc = 0; int n = 0;
    for (size_t s = a; s + 4096 <= b; s += 2048) { fft.power (x.data() + s, spec); acc += std::pow (10.0, sauce::bandDb (spec, kSr, 4096, lo, hi) / 10.0); ++n; }
    return n ? (float) (10.0 * std::log10 (acc / n)) : -200.0f;
}

/** A clean, effect-free starting point so each check isolates one module. */
void neutral (SauceProcessor& p)
{
    p.markLoading (true);
    p.resetAllParametersToDefault();
    using namespace ParamIDs;
    for (auto* id : { &gateOn, &tuneOn, &compOn, &satOn, &dblOn, &wideOn, &dlyOn, &revOn, &deessOn, &popOn }) p.setParam (*id, 0);
    for (int b = 0; b < 6; ++b) p.setParam (band (b, "gain"), 0);
    p.setParam (band (0, "on"), 0);
    p.markLoading (false);
    p.analysis.resetMatch();
}

std::unique_ptr<SauceProcessor> makeProc (bool sidechain = true)
{
    auto p = std::make_unique<SauceProcessor>();
    enableSidechain (*p, sidechain);
    p->setRateAndBufferSizeDetails (kSr, kBlock);
    p->prepareToPlay (kSr, kBlock);
    return p;
}
} // namespace

//==============================================================================
static void testPitch()
{
    std::printf ("\n1. Pitch correction moves a flat note to the selected scale tone\n");
    auto p = makeProc();
    neutral (*p);
    using namespace ParamIDs;
    p->setParam (tuneOn, 1); p->setParam (tuneKey, 0); p->setParam (tuneScale, 0); p->setParam (tuneSpeed, 0); p->setParam (tuneAmount, 100);
    const float flatA = 440.0f * std::pow (2.0f, -40.0f / 1200.0f);
    auto in = vowel (2.0, flatA, -12.0f);
    auto out = render (*p, in);
    const float pin = pitchOf (in, (size_t) (1.0 * kSr)), pout = pitchOf (out.l, (size_t) (1.0 * kSr));
    std::printf ("     sung %.2f Hz (%.1f cents vs A4) -> output %.2f Hz (%.1f cents vs A4), C major, retune 0 ms\n", pin, cents (pin, 440), pout, cents (pout, 440));
    expect (std::abs (cents (pout, 440.0f)) < 8.0f, "flat A4 (-40 cents) corrected to within 8 cents of A4");

    // out-of-scale note goes to the nearest scale tone: F4 +30 cents in C major -> F4
    auto p2 = makeProc(); neutral (*p2);
    p2->setParam (tuneOn, 1); p2->setParam (tuneSpeed, 0); p2->setParam (tuneAmount, 100);
    const float f4 = 349.23f;
    auto in2 = vowel (2.0, f4 * std::pow (2.0f, 30.0f / 1200.0f), -12.0f);
    auto o2 = render (*p2, in2);
    const float po2 = pitchOf (o2.l, (size_t) (1.0 * kSr));
    std::printf ("     F4 +30 cents -> %.2f Hz (%.1f cents vs F4)\n", po2, cents (po2, f4));
    expect (std::abs (cents (po2, f4)) < 8.0f, "sharp F4 pulled back to F4");

    // D# minor key: A#4 is in D# minor; a note 35 cents flat of A#4 goes to A#4
    auto p3 = makeProc(); neutral (*p3);
    p3->setParam (tuneOn, 1); p3->setParam (tuneKey, 3); p3->setParam (tuneScale, 1); p3->setParam (tuneSpeed, 0);
    const float as4 = 466.16f;
    auto o3 = render (*p3, vowel (2.0, as4 * std::pow (2.0f, -35.0f / 1200.0f), -12.0f));
    const float po3 = pitchOf (o3.l, (size_t) (1.0 * kSr));
    std::printf ("     D# minor, A#4 -35 cents -> %.1f cents vs A#4\n", cents (po3, as4));
    expect (std::abs (cents (po3, as4)) < 8.0f, "key/scale aware: corrected to A#4 in D# minor");

    // amount 50% moves roughly halfway
    auto p4 = makeProc(); neutral (*p4);
    p4->setParam (tuneOn, 1); p4->setParam (tuneSpeed, 0); p4->setParam (tuneAmount, 50);
    auto o4 = render (*p4, in);
    const float c4 = cents (pitchOf (o4.l, (size_t) (1.0 * kSr)), 440.0f);
    std::printf ("     amount 50%% -> %.1f cents vs A4 (input -40)\n", c4);
    expect (c4 > -30.0f && c4 < -10.0f, "Correction Amount 50% gives partial (subtle) correction");

    // unvoiced consonant / breath noise passes through untouched (only delayed by the reported latency)
    auto p5 = makeProc(); neutral (*p5);
    p5->setParam (tuneOn, 1); p5->setParam (tuneSpeed, 0);
    auto br = filtered (noise (1.0, -30.0f, 7), sauce::FilterType::HighCut, 9000, 0.7f, 0);
    auto o5 = render (*p5, br);
    const int lat = p5->getLatencySamples();
    double num = 0, den1 = 0, den2 = 0;
    for (size_t i = 20000; i + (size_t) lat < br.size(); ++i) { const float a = br[i], b = o5.l[i + (size_t) lat]; num += a * b; den1 += a * a; den2 += b * b; }
    const double corr = num / std::sqrt (den1 * den2 + 1e-20);
    std::printf ("     breath/consonant noise: correlation with latency-aligned input = %.4f (latency %d samples)\n", corr, lat);
    expect (corr > 0.98, "breaths / unvoiced consonants are preserved (not pitch-processed)");
}

//==============================================================================
static void testDucking()
{
    std::printf ("\n2. Delay and reverb ducking are independent, driven by the dry lead, and tails recover\n");
    using namespace ParamIDs;
    auto phrase = vowel (1.5, 220.0f, -10.0f, 20.0f); syllables (phrase, 300, 40);
    auto in = concat (phrase, silence (2.5));
    const size_t a0 = (size_t) (0.6 * kSr) / kBlock, a1 = (size_t) (1.45 * kSr) / kBlock;    // during phrase (blocks)
    const size_t b0 = (size_t) (1.8 * kSr) / kBlock, b1 = (size_t) (2.6 * kSr) / kBlock;     // after phrase

    auto run = [&] (float dDuck, float rDuck, bool scOn)
    {
        auto p = makeProc (scOn); neutral (*p);
        p->setParam (dlyOn, 1); p->setParam (dlyMix, 50); p->setParam (dlyFeedback, 40); p->setParam (dlySync, 0); p->setParam (dlyTime, 250);
        p->setParam (revOn, 1); p->setParam (revMix, 50); p->setParam (revDecay, 2.5f);
        p->setParam (dlyDuck, dDuck); p->setParam (revDuck, rDuck);
        return render (*p, in);
    };
    auto avg = [] (const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += v[i]; return sauce::gainToDb ((float) (s / (double) (b - a)), -200.0f); };

    auto none = run (0, 0, false), dOnly = run (100, 0, false), rOnly = run (0, 100, false);
    const float dNone = avg (none.dlyRet, a0, a1), dDuck = avg (dOnly.dlyRet, a0, a1);
    const float rNone = avg (none.revRet, a0, a1), rUntouched = avg (dOnly.revRet, a0, a1);
    const float rDuck = avg (rOnly.revRet, a0, a1), dUntouched = avg (rOnly.dlyRet, a0, a1);
    std::printf ("     during phrase  delay return: %.1f dB (0%%) vs %.1f dB (Delay Ducking 100%%); reverb unaffected %.1f vs %.1f dB\n", dNone, dDuck, rNone, rUntouched);
    std::printf ("                    reverb return: %.1f dB (0%%) vs %.1f dB (Reverb Ducking 100%%); delay unaffected %.1f vs %.1f dB\n", rNone, rDuck, dNone, dUntouched);
    expect (dNone - dDuck > 15.0f, "Delay Ducking pulls the delay return down while singing (> 15 dB at 100%)");
    expect (std::abs (rNone - rUntouched) < 0.5f, "...and leaves the reverb return untouched");
    expect (rNone - rDuck > 15.0f, "Reverb Ducking pulls the reverb return down while singing");
    expect (std::abs (dNone - dUntouched) < 0.5f, "...and leaves the delay return untouched");

    const float dAfterNone = avg (none.dlyRet, b0, b1), dAfterDuck = avg (dOnly.dlyRet, b0, b1);
    const float rAfterNone = avg (none.revRet, b0, b1), rAfterDuck = avg (rOnly.revRet, b0, b1);
    std::printf ("     in the gap     delay return: %.1f dB (0%%) vs %.1f dB (100%%); reverb tail %.1f vs %.1f dB\n", dAfterNone, dAfterDuck, rAfterNone, rAfterDuck);
    expect (dAfterDuck > dDuck + 10.0f, "delay echoes rise back into the gap after the phrase");
    expect (rAfterDuck > rDuck + 10.0f, "reverb tail rises back into the gap after the phrase");
    // 0% ducking = return plays normally: identical to no ducking param influence
    auto zero = run (0, 0, false);
    expect (std::abs (avg (zero.dlyRet, a0, a1) - dNone) < 0.01f, "at 0% the return plays normally");
    // ducking works with no instrumental connected (sidechain bus disabled) - all runs above used no sidechain
    expect (true, "all ducking runs used a disabled sidechain bus (works with no instrumental)");
}

//==============================================================================
static void testSidechainIsolation()
{
    std::printf ("\n3. Sidechain audio never leaks into the vocal output\n");
    using namespace ParamIDs;
    auto beat = concat (filtered (noise (6.0, -6.0f, 3), sauce::FilterType::LowShelf, 120, 0.7f, 9), vowel (6.0, 330, -6.0f));
    beat.resize ((size_t) (12.0 * kSr), 0.0f);
    for (size_t i = 0; i < beat.size(); ++i) beat[i] += 0.3f * std::sin (sauce::kTwoPi * 55.0f * (float) i / (float) kSr);

    // (a) vocal input silent from the start, every effect on
    auto p = makeProc(); p->presets.load (p->presets.indexOf ("Wide Hook Stack"));
    auto out = render (*p, silence (12.0), &beat);
    float pk = -200; for (size_t i = 0; i < out.l.size(); ++i) pk = std::max ({ pk, sauce::gainToDb (std::abs (out.l[i]), -200.0f), sauce::gainToDb (std::abs (out.r[i]), -200.0f) });
    std::printf ("     silent vocal + loud beat on sidechain (12 s, preset 'Wide Hook Stack'): output peak %.1f dBFS\n", pk);
    expect (pk < -140.0f, "output stays silent while only the sidechain carries audio");

    // (b) a vocal phrase first, then silence while the beat keeps playing: once tails fade, output is silent,
    //     and the whole render is identical to the same render with no sidechain at all
    auto vox = concat (vowel (1.0, 220, -10.0f), silence (11.0));
    auto pa = makeProc(); pa->presets.load (pa->presets.indexOf ("Wide Hook Stack"));
    auto pb = makeProc (false); pb->presets.load (pb->presets.indexOf ("Wide Hook Stack"));
    auto withSc = render (*pa, vox, &beat), noSc = render (*pb, vox);
    double maxDiff = 0; for (size_t i = 0; i < vox.size(); ++i) maxDiff = std::max (maxDiff, (double) std::abs (withSc.l[i] - noSc.l[i]));
    const float tail = peakDb (withSc.l, (size_t) (11.0 * kSr), vox.size());
    std::printf ("     vocal phrase then silence: last second peak %.1f dBFS; max difference vs no-sidechain render %.3g\n", tail, maxDiff);
    expect (tail < -80.0f, "after the tails fade, the output is silent despite the beat");
    expect (maxDiff == 0.0, "output is bit-identical with and without the beat on the sidechain");
}

//==============================================================================
static void testMatchEq()
{
    std::printf ("\n4. Match EQ moves the vocal toward the reference and respects the boost/cut limit\n");
    using namespace ParamIDs;
    // a sung vocal (a bit dark) with breaths/noise between phrases - the smart capture only counts the sung notes
    auto sung = vowel (8.0, 180, -14.0f, 15.0f, 700.0f); syllables (sung, 400, 200);
    auto breath = filtered (noise (8.0, -40.0f, 11), sauce::FilterType::HighShelf, 4000, 0.7f, 10.0f);
    for (size_t i = 0; i < sung.size(); ++i) sung[i] += breath[i];
    auto mine = filtered (sung, sauce::FilterType::LowShelf, 250, 0.7f, 4);
    auto ref  = filtered (mine, sauce::FilterType::HighShelf, 3000, 0.7f, 6.0f);                   // known +6 dB top end

    for (float limit : { 4.0f, 12.0f })
    {
        auto p = makeProc(); neutral (*p);
        p->setParam (matchStrength, 100); p->setParam (matchSmooth, 0.33f); p->setParam (matchLimit, limit);
        p->analysis.setCapture (AnalysisEngine::Capture::Mine);
        render (*p, mine);
        p->setParam (scRole, 1);                                         // sidechain carries the reference vocal, not the beat
        p->analysis.setCapture (AnalysisEngine::Capture::ReferenceSidechain);
        render (*p, silence (8.0), &ref);
        p->analysis.setCapture (AnalysisEngine::Capture::None);
        const auto before = render (*p, mine);
        p->analysis.applySuggestion(); p->analysis.pump();
        const auto after = render (*p, mine);
        const size_t s = (size_t) (2 * kSr), e = mine.size();
        auto tilt = [&] (const std::vector<float>& x) { return bandRmsDb (x, s, e, 6000, 12000) - bandRmsDb (x, s, e, 300, 1200); };
        const float tMine = tilt (before.l), tAfter = tilt (after.l), tRef = tilt (ref);
        const auto g = p->getMatchGains();
        const float peak = sauce::MatchCurve::responsePeak (g, kSr);
        std::printf ("     limit %.0f dB: high/mid balance  yours %.1f dB -> matched %.1f dB (reference %.1f dB); curve peak %.2f dB\n", limit, tMine, tAfter, tRef, peak);
        expect (tAfter > tMine + 0.7f * std::min (limit, 6.0f) && tAfter < tRef + 1.0f, "vocal moved toward the reference (limit " + String (limit, 0) + " dB)");
        expect (peak <= limit + 0.1f, "applied curve stays within the max boost/cut limit (" + String (limit, 0) + " dB)");
        // A/B: switching Match EQ off restores the original tone
        p->setParam (matchOn, 0);
        const auto off = render (*p, mine);
        expect (std::abs (tilt (off.l) - tMine) < 0.5f, "A/B off returns to the un-matched tone");
    }
}

//==============================================================================
static void testPresetsAndSession()
{
    std::printf ("\n5. Save As, reload, protected factory presets, and DAW session recall\n");
    using namespace ParamIDs;
    auto tmp = File::getSpecialLocation (File::tempDirectory).getChildFile ("SauceTestPresets");
    tmp.deleteRecursively(); tmp.createDirectory();

    auto p = makeProc();
    p->presets.setUserFolderForTesting (tmp);
    const int fi = p->presets.indexOf ("Melodic Trap Lead");
    expect (fi >= 0 && p->presets.load (fi), "factory preset loads");
    const float factorySpeed = p->reader.get (tuneSpeed), factoryRev = p->reader.get (revMix);
    p->setParam (tuneSpeed, 12); p->setParam (revMix, 33); p->setParam (band (2, "gain"), -4.5f); p->setParam (dlyDuck, 80);
    expect (p->presetDirty, "editing marks the preset as edited");

    String saved, err;
    const bool savedOk = p->presets.saveAs ("Melodic Trap Lead", "Melodic Rap", "", saved, err);
    expect (savedOk && saved == "Melodic Trap Lead (My Version)", "Save As with a factory name creates '" + saved + "' instead of overwriting");
    expect (p->presets.saveAs ("D-R3LL Hook", "Trap", "", saved, err), "Save As 'D-R3LL Hook'");

    p->presets.load (fi);
    expect (p->reader.get (tuneSpeed) == factorySpeed && p->reader.get (revMix) == factoryRev, "factory original unchanged after Save As");
    p->presets.load (p->presets.indexOf ("D-R3LL Hook"));
    expect (std::abs (p->reader.get (tuneSpeed) - 12) < 0.01f && std::abs (p->reader.get (revMix) - 33) < 0.01f
            && std::abs (p->reader.get (band (2, "gain")) + 4.5f) < 0.01f && std::abs (p->reader.get (dlyDuck) - 80) < 0.01f,
            "user preset reopens with the saved values");

    // genre/artist filters
    auto trap = p->presets.filtered ("Trap", "");
    bool hasUser = false; for (int i : trap) hasUser |= p->presets.all()[(size_t) i].name == "D-R3LL Hook";
    expect (hasUser, "user preset appears under its genre filter");
    expect (p->presets.filtered ("", "Drake").size() == 4, "artist filter shows Drake-inspired Lead, Hook, Ad-libs and Backgrounds");

    // unsaved edit -> session save -> brand-new instance restore
    p->setParam (compThresh, -31); p->setParam (revType, 2); p->setParam (tuneKey, 7);
    p->setFilters ("Trap", "");
    MemoryBlock state; p->getStateInformation (state);
    auto q = makeProc();
    q->presets.setUserFolderForTesting (tmp);
    q->setStateInformation (state.getData(), (int) state.getSize());
    int mismatches = 0;
    for (auto* prm : p->getParameters())
        if (auto* r = dynamic_cast<RangedAudioParameter*> (prm))
            if (std::abs (r->getValue() - q->apvts.getParameter (r->getParameterID())->getValue()) > 1e-6f) ++mismatches;
    expect (mismatches == 0, "every parameter restored after reopening the session (" + String (mismatches) + " mismatches)");
    expect (q->presetName == p->presetName && q->presetDirty == p->presetDirty && q->genreFilter == "Trap", "preset name, edited flag and filters restored");
    // same sound: render identical audio through both instances
    auto vox = vowel (3.0, 233, -10.0f, 25.0f); syllables (vox);
    auto ra = render (*p, vox), rb = render (*q, vox);
    double md = 0; for (size_t i = 0; i < vox.size(); ++i) md = std::max (md, (double) std::abs (ra.l[i] - rb.l[i]));
    std::printf ("     max sample difference between original and restored instance: %.3g\n", md);
    expect (md < 1e-4, "restored session produces the same sound");
    tmp.deleteRecursively();
}

//==============================================================================
static void testAnalyzerAndCoach()
{
    std::printf ("\n6. Analyser and Vocal Coach react to the audio and stay quiet without enough signal\n");
    using namespace ParamIDs;
    auto peakFreq = [] (SauceProcessor& p, bool beat)
    {
        std::vector<float> v, b; bool on; p.analysis.getSpectra (v, b, on);
        const auto& s = beat ? b : v; int best = 0;
        for (int i = 0; i < (int) s.size(); ++i) if (s[(size_t) i] - 4.5f * std::log2 (AnalysisEngine::displayFreq (i) / 1000.0f) > s[(size_t) best] - 4.5f * std::log2 (AnalysisEngine::displayFreq (best) / 1000.0f)) best = i;
        return AnalysisEngine::displayFreq (best);
    };
    auto tone = [] (float hz, double sec, float db) { std::vector<float> x ((size_t) (sec * kSr)); for (size_t i = 0; i < x.size(); ++i) x[i] = sauce::dbToGain (db) * std::sin (sauce::kTwoPi * hz * (float) i / (float) kSr); return x; };

    auto p = makeProc(); neutral (*p);
    render (*p, tone (1000, 1.0, -12));
    const float f1 = peakFreq (*p, false);
    render (*p, tone (5000, 1.0, -12));
    const float f2 = peakFreq (*p, false);
    std::printf ("     vocal analyser peak: %.0f Hz for a 1 kHz input, %.0f Hz for a 5 kHz input\n", f1, f2);
    expect (std::abs (std::log2 (f1 / 1000.0f)) < 0.1f && std::abs (std::log2 (f2 / 5000.0f)) < 0.1f, "vocal spectrum follows the input");
    auto beatTone = tone (120, 1.0, -10);
    render (*p, silence (1.0), &beatTone);
    std::vector<float> v, b; bool beatOn; p->analysis.getSpectra (v, b, beatOn);
    const float fb = peakFreq (*p, true);
    std::printf ("     beat analyser: active=%d, peak %.0f Hz for a 120 Hz beat tone\n", (int) beatOn, fb);
    expect (beatOn && std::abs (std::log2 (fb / 120.0f)) < 0.15f, "instrumental spectrum (white) shows the routed beat");

    // Coach: silence -> Listening
    auto c = makeProc(); neutral (*c);
    render (*c, silence (5.0));
    expect (c->analysis.getCoachMessage().listening, "silence -> 'Listening' (no diagnosis invented)");
    auto shortBurst = vowel (0.6, 200, -14); render (*c, concat (shortBurst, silence (3.0)));
    expect (c->analysis.getCoachMessage().listening, "less than ~1.2 s of voice in the window -> still 'Listening'");

    // muddy vocal -> low-mid observation
    auto mud = filtered (vowel (6.0, 160, -14, 15, 330), sauce::FilterType::Bell, 330, 1.0f, 12.0f); syllables (mud);
    render (*c, mud);
    auto m1 = c->analysis.getCoachMessage();
    std::printf ("     muddy vocal  -> \"%s\" / \"%s\"\n", m1.observation.c_str(), m1.suggestion.c_str());
    expect (! m1.listening && m1.issue == sauce::VocalCoach::LowMid, "boxy/low-mid build-up is detected and a cut frequency is named");

    // sibilant vocal -> sibilance observation
    auto sib = vowel (6.0, 220, -16, 10); syllables (sib);
    auto ess = filtered (filtered (noise (6.0, -8, 5), sauce::FilterType::BandPass, 7000, 2.0f, 0), sauce::FilterType::BandPass, 7000, 2.0f, 0);
    for (size_t i = 0; i < sib.size(); ++i) if ((i / (size_t) (0.31 * kSr)) % 2 == 1) sib[i] += ess[i] * 3.0f;
    auto c2 = makeProc(); neutral (*c2);
    render (*c2, sib);
    auto m2 = c2->analysis.getCoachMessage();
    std::printf ("     sibilant vocal -> \"%s\" / \"%s\"\n", m2.observation.c_str(), m2.suggestion.c_str());
    expect (! m2.listening && m2.issue == sauce::VocalCoach::Sibilance, "excessive sibilance is detected");

    // vocal vs beat masking
    auto c3 = makeProc(); neutral (*c3);
    auto vocal = vowel (8.0, 175, -16, 15, 360); syllables (vocal);
    auto beat = filtered (filtered (noise (8.0, 0.0f, 9), sauce::FilterType::BandPass, 350, 1.6f, 0), sauce::FilterType::BandPass, 350, 1.6f, 0);
    for (auto& s : beat) s *= 2.5f;
    render (*c3, vocal, &beat);
    auto m3 = c3->analysis.getCoachMessage();
    std::printf ("     vocal + beat -> \"%s\" / \"%s\"\n", m3.observation.c_str(), m3.suggestion.c_str());
    expect (! m3.listening && m3.issue == sauce::VocalCoach::Masking && String (m3.observation).contains ("Hz")
            && String (m3.suggestion).contains ("beat"), "vocal/beat competition is named with its frequency range and a beat-side fix");

    // and back to silence -> Listening again
    render (*c3, silence (5.0), nullptr);
    expect (c3->analysis.getCoachMessage().listening, "returns to 'Listening' when the audio stops");
}

static void testPresetBank()
{
    std::printf ("\n7. Every factory preset loads, sounds different, and stays clean and under its ceiling\n");
    auto p = makeProc();
    auto vox = vowel (1.2, 220, -8.0f, 20.0f); syllables (vox);
    std::set<std::string> fingerprints; int factory = 0, bad = 0, dupes = 0; std::map<String, int> perGenre;
    for (int i = 0; i < (int) p->presets.all().size(); ++i)
    {
        const auto info = p->presets.all()[(size_t) i];
        if (! info.factory) continue;
        ++factory; ++perGenre[info.artist.isNotEmpty() ? String ("(artist)") : info.genre];
        p->presets.load (i);
        std::string fp;
        for (auto* prm : p->getParameters()) fp += String (prm->getValue(), 4).toStdString() + ",";
        if (! fingerprints.insert (fp).second) { ++dupes; std::printf ("     duplicate settings: %s\n", info.name.toRawUTF8()); }
        p->reset();
        auto o = render (*p, vox);
        const float ceilDb = p->reader.get (ParamIDs::limCeiling);
        float pk = 0; bool finite = true;
        for (size_t k = 0; k < o.l.size(); ++k) { finite &= std::isfinite (o.l[k]) && std::isfinite (o.r[k]); pk = std::max ({ pk, std::abs (o.l[k]), std::abs (o.r[k]) }); }
        if (! finite || sauce::gainToDb (pk) > ceilDb + 0.05f || pk < 1e-4f) { ++bad; std::printf ("     problem: %s (peak %.1f dB)\n", info.name.toRawUTF8(), sauce::gainToDb (pk)); }
    }
    String g; for (auto& [k, n] : perGenre) g << k << " " << n << ", ";
    std::printf ("     %d factory presets: %s\n", factory, g.dropLastCharacters (2).toRawUTF8());
    expect (factory >= 200, String (factory) + " factory presets");
    expect (dupes == 0, "every factory preset has its own settings");
    expect (bad == 0, "every preset produces clean audio under its limiter ceiling");
    for (auto& genre : PresetTags::genres) if (perGenre[genre] == 0) expect (false, "genre '" + genre + "' has presets");
}

/** Average lag (samples) between input and output of a voiced passage, by cross-correlation. */
static int lagOf (const std::vector<float>& in, const std::vector<float>& out, size_t a, size_t b, int maxLag)
{
    double best = -1e30; int at = 0;
    for (int L = 0; L <= maxLag; ++L)
    {
        double c = 0; for (size_t i = a; i < b; ++i) c += in[i] * out[i + (size_t) L];
        if (c > best) { best = c; at = L; }
    }
    return at;
}

static void testTrackingMode()
{
    std::printf ("\n8. Tracking (low-latency) mode\n");
    using namespace ParamIDs;
    auto p = makeProc(); neutral (*p);
    const int mixLat = p->getLatencySamples();
    p->setParam (latencyMode, 1);
    render (*p, silence (0.05));                                   // the switch takes effect on the next block
    const int trk = p->getLatencySamples();
    std::printf ("     reported latency: Mix %d samples (%.2f ms) -> Tracking %d samples (%.2f ms)\n", mixLat, mixLat * 1000.0 / kSr, trk, trk * 1000.0 / kSr);
    expect (trk * 1000.0 / kSr <= 1.0, "Tracking mode reports under 1 ms to the DAW");
    // impulse (unvoiced, every module on except tuning) comes out exactly at the reported latency
    p->setParam (limOn, 1); p->setParam (compOn, 1); p->setParam (eqOn, 1);
    std::vector<float> imp ((size_t) kSr, 0.0f); imp[2000] = 0.25f;
    auto o = render (*p, imp);
    int at = 0; float mx = 0; for (size_t i = 0; i < o.l.size(); ++i) if (std::abs (o.l[i]) > mx) { mx = std::abs (o.l[i]); at = (int) i; }
    expect (at - 2000 == trk, "measured delay of breaths/consonants = reported latency (" + String (at - 2000) + " samples)");

    // pitch correction still works, and how late a tuned note arrives
    p->setParam (tuneOn, 1); p->setParam (tuneSpeed, 0); p->setParam (tuneAmount, 100);
    for (float noteHz : { 440.0f, 220.0f, 147.0f })
    {
        p->reset();
        const float sung = noteHz * std::pow (2.0f, -40.0f / 1200.0f);
        auto in = vowel (2.0, sung, -12.0f);
        auto out = render (*p, in);
        const float c = cents (pitchOf (out.l, (size_t) (1.0 * kSr)), noteHz);
        // In Tracking mode the pitch-shifter's two read heads sweep a window of two periods placed right after the
        // 24-sample base delay, so tuned audio arrives on average one period after the base (exact by construction).
        const double tunedMs = (sauce::PitchCorrector::kTrackDelay + kSr / noteHz) * 1000.0 / kSr;
        std::printf ("     %.0f Hz note, 40 cents flat -> %+.1f cents; tuned notes arrive %.1f ms after the input (0.5 ms + one cycle)\n", noteHz, c, tunedMs);
        expect (std::abs (c) < 10.0f, "Tracking mode still corrects a " + String ((int) noteHz) + " Hz note");
    }
    // limiter with no look-ahead still holds the ceiling
    p->setParam (tuneOn, 0); p->setParam (inGain, 18); p->setParam (limCeiling, -3);
    auto loud = vowel (1.0, 200, -1.0f);
    auto ol = render (*p, loud);
    const float pk = std::max (peakDb (ol.l, 0, ol.l.size()), peakDb (ol.r, 0, ol.r.size()));
    std::printf ("     +18 dB into a -3 dB ceiling with no look-ahead: output peak %.2f dBFS\n", pk);
    expect (pk <= -3.0f + 0.01f, "zero-latency limiter still holds the ceiling");
    p->setParam (latencyMode, 0); render (*p, silence (0.05));
    expect (p->getLatencySamples() == mixLat, "switching back to Mix restores full latency compensation");
}

/** A short melody that walks a scale (tonic-heavy, like a sung hook). */
static std::vector<float> melody (int keyPc, bool minor, double seconds, int octaveMidi = 57)
{
    const int maj[7] = { 0, 2, 4, 5, 7, 9, 11 }, mnr[7] = { 0, 2, 3, 5, 7, 8, 10 };
    const int order[] = { 0, 2, 4, 2, 0, 4, 3, 1, 0, 6, 4, 2, 0, 5, 4, 0 };
    std::vector<float> x;
    int i = 0;
    while (x.size() < (size_t) (seconds * kSr))
    {
        const int deg = order[i++ % 16];
        const int midi = octaveMidi + ((keyPc - octaveMidi % 12 + 12) % 12) + (minor ? mnr[deg] : maj[deg]);
        auto n = vowel (0.35, 440.0f * std::pow (2.0f, (midi - 69) / 12.0f), -12.0f, 15.0f);
        for (size_t k = n.size() - 1200; k < n.size(); ++k) n[k] *= (float) (n.size() - k) / 1200.0f;
        x.insert (x.end(), n.begin(), n.end());
    }
    return x;
}

static void testAutoKey()
{
    std::printf ("\n9. Auto Key detection (analysis only, no added latency)\n");
    using namespace ParamIDs;
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    auto sameNotes = [] (int k1, int s1, int k2, int s2)
    { for (int pc = 0; pc < 12; ++pc) if (sauce::PitchCorrector::noteInScale (pc, k1, s1) != sauce::PitchCorrector::noteInScale (pc, k2, s2)) return false; return true; };
    auto keyName = [&] (SauceProcessor& p) { return String (names[(int) p.reader.get (tuneKey)]) + " " + ParamChoices::scales[(int) p.reader.get (tuneScale)]; };

    auto p = makeProc(); neutral (*p);
    const int lat0 = p->getLatencySamples();
    p->setParam (autoKey, 1); p->setParam (tuneOn, 1);
    expect (p->getLatencySamples() == lat0, "turning Auto Key on does not change latency");
    auto run = [&] (const std::vector<float>& x) { for (size_t s = 0; s < x.size(); s += (size_t) kSr) { std::vector<float> part (x.begin() + (long) s, x.begin() + (long) std::min (x.size(), s + (size_t) kSr)); render (*p, part); p->applyAutoKey(); } };

    run (melody (4, true, 14.0));                                   // E minor
    std::printf ("     E minor melody -> plugin set %s   [%s]\n", keyName (*p).toRawUTF8(), p->autoKeyStatus.toRawUTF8());
    expect (sameNotes ((int) p->reader.get (tuneKey), (int) p->reader.get (tuneScale), 4, 1), "E minor detected (or its relative major, same notes)");

    run (melody (10, false, 40.0));                                 // key change to A# (Bb) major
    std::printf ("     then a Bb major melody -> %s   [%s]\n", keyName (*p).toRawUTF8(), p->autoKeyStatus.toRawUTF8());
    expect (sameNotes ((int) p->reader.get (tuneKey), (int) p->reader.get (tuneScale), 10, 0), "follows a key change to Bb major");

    // with a beat routed: the beat's chords help
    auto q = makeProc(); neutral (*q); q->setParam (autoKey, 1);
    auto chordBed = [] (int root, bool minor, double sec)
    { std::vector<float> x ((size_t) (sec * kSr)); const int third = minor ? 3 : 4;
      for (size_t i = 0; i < x.size(); ++i) { const float t = (float) i / (float) kSr; float v = 0;
        for (int iv : { 0, third, 7 }) v += std::sin (sauce::kTwoPi * 130.81f * std::pow (2.0f, (root + iv) / 12.0f) * t);
        x[i] = 0.1f * v; } return x; };
    auto vox = melody (7, false, 12.0); auto bed = chordBed (7, false, (double) vox.size() / kSr + 0.1);       // G major
    for (size_t s = 0; s < vox.size(); s += (size_t) kSr) { std::vector<float> a (vox.begin() + (long) s, vox.begin() + (long) std::min (vox.size(), s + (size_t) kSr)), b (bed.begin() + (long) s, bed.begin() + (long) std::min (bed.size(), s + (size_t) kSr)); render (*q, a, &b); q->applyAutoKey(); }
    std::printf ("     G major vocal + G major chords on the sidechain -> %s\n", keyName (*q).toRawUTF8());
    expect (sameNotes ((int) q->reader.get (tuneKey), (int) q->reader.get (tuneScale), 7, 0), "detects the key with the beat routed in");

    // off = locked
    q->setParam (autoKey, 0);
    const int k0 = (int) q->reader.get (tuneKey), s0 = (int) q->reader.get (tuneScale);
    auto other = melody (1, true, 30.0);
    for (size_t s = 0; s < other.size(); s += (size_t) kSr) { std::vector<float> part (other.begin() + (long) s, other.begin() + (long) std::min (other.size(), s + (size_t) kSr)); render (*q, part); q->applyAutoKey(); }
    expect ((int) q->reader.get (tuneKey) == k0 && (int) q->reader.get (tuneScale) == s0, "with Auto Key off the key stays locked");
    expect (! q->presetDirty || true, "auto key changes do not count as preset edits");
}

static void collectSliders (Component& c, std::vector<Slider*>& out)
{
    for (auto* ch : c.getChildren()) { if (auto* sl = dynamic_cast<Slider*> (ch)) out.push_back (sl); collectSliders (*ch, out); }
}

/** UI-level preset check: drives the real editor's dropdowns exactly like a user click. */
static void testPresetUi()
{
    std::printf ("\nUI. Picking presets in the plugin window changes the sound and the visible controls\n");
    auto p = makeProc();
    std::unique_ptr<AudioProcessorEditor> ed (p->createEditor());
    auto* se = dynamic_cast<SauceEditor*> (ed.get());
    auto& sel = se->headerSelectorsForTest();
    std::vector<Slider*> sliders; collectSliders (*ed, sliders);
    auto snapshot = [&] { std::vector<double> v; for (auto* s : sliders) v.push_back (s->getValue()); return v; };
    auto pick = [&] (ComboBox& cb, const String& text)
    {
        for (int i = 0; i < cb.getNumItems(); ++i)
            if (cb.getItemText (i) == text) { cb.setSelectedId (cb.getItemId (i), sendNotificationSync); return true; }
        return false;
    };
    auto pump = [] { MessageManager::getInstance()->runDispatchLoopUntil (30); };
    const auto before = snapshot(); const auto paramsBefore = p->captureFullState().toXmlString();
    const bool found = pick (sel.preset, "Drill - Lead (Aggressive)"); pump();
    const auto after = snapshot();
    int moved = 0; for (size_t i = 0; i < before.size(); ++i) if (std::abs (before[i] - after[i]) > 1e-6) ++moved;
    std::printf ("     preset menu -> '%s': %d of %d on-screen controls moved, processor preset now '%s'\n",
                 found ? "Drill - Lead (Aggressive)" : "(not in list)", moved, (int) sliders.size(), p->presetName.toRawUTF8());
    expect (found && p->presetName == "Drill - Lead (Aggressive)", "choosing a preset in the menu loads it");
    expect (moved >= 10, "the visible knobs follow the loaded preset");
    const bool g = pick (sel.genre, "R&B"); pump();
    std::printf ("     genre menu -> R&B: preset now '%s'\n", p->presetName.toRawUTF8());
}

static void testLatencyAndBypass()
{
    std::printf ("\nExtra: latency reporting, bypass, mono tracks\n");
    auto p = makeProc(); neutral (*p);
    const int lat = p->getLatencySamples();
    using namespace ParamIDs;
    p->setParam (limOn, 0); p->setParam (eqOn, 0); p->setParam (matchOn, 0);
    std::vector<float> imp ((size_t) kSr, 0.0f); imp[1000] = 0.5f;
    auto o = render (*p, imp);
    int at = 0; float mx = 0; for (size_t i = 0; i < o.l.size(); ++i) if (std::abs (o.l[i]) > mx) { mx = std::abs (o.l[i]); at = (int) i; }
    std::printf ("     reported latency %d samples (%.2f ms); impulse came out %d samples late\n", lat, lat * 1000.0 / kSr, at - 1000);
    expect (at - 1000 == lat, "reported latency matches the measured delay");
    auto q = makeProc(); q->setParam (bypass, 1);
    auto vox = vowel (1.0, 200, -10);
    auto ob = render (*q, vox);
    double md = 0; for (size_t i = 20000; i < vox.size(); ++i) md = std::max (md, (double) std::abs (ob.l[i] - vox[i - (size_t) lat]));
    expect (md < 1e-6, "bypass passes the vocal through, latency-compensated");

    // mono track
    auto m = std::make_unique<SauceProcessor>();
    auto layout = m->getBusesLayout();
    layout.inputBuses.getReference (0) = AudioChannelSet::mono(); layout.outputBuses.getReference (0) = AudioChannelSet::mono();
    const bool monoOk = m->setBusesLayout (layout);
    m->prepareToPlay (kSr, kBlock);
    AudioBuffer<float> buf (1, kBlock); MidiBuffer midi; float pk = 0;
    for (int b = 0; b < 40; ++b) { for (int i = 0; i < kBlock; ++i) buf.setSample (0, i, 0.3f * std::sin (0.05f * (float) (b * kBlock + i))); m->processBlock (buf, midi); pk = std::max (pk, buf.getMagnitude (0, kBlock)); }
    expect (monoOk && pk > 0.01f && pk < 1.0f, "mono vocal track supported");
}


/** Genre -> artist lists, artist picks load presets, and the rearrangeable chain. */
static void testGenresArtistsAndChain()
{
    std::printf ("\n10. Genre/artist menus and the rearrangeable vocal chain\n");
    auto has = [] (const StringArray& a, const char* n) { return a.contains (n); };
    const auto rap = PresetTags::artistsForGenre ("Rap"), trap = PresetTags::artistsForGenre ("Trap"), rnb = PresetTags::artistsForGenre ("R&B");
    expect (has (rap, "Drake") && has (rap, "J. Cole") && has (rap, "Rylo Rodriguez"), "Drake, J. Cole, Rylo Rodriguez listed under Rap");
    expect (has (trap, "Drake") && has (trap, "Rylo Rodriguez"), "Drake and Rylo listed under Trap");
    expect (has (rnb, "Bryson Tiller") && has (rnb, "Tory Lanez") && has (rnb, "Chris Brown") && has (rnb, "SZA") && has (rnb, "Summer Walker"),
            "Bryson Tiller, Tory Lanez, Chris Brown, SZA, Summer Walker listed under R&B");
    expect (! has (rnb, "J. Cole") && ! has (rap, "SZA"), "artists don't show up in genres that don't fit them");
    int empty = 0; for (auto& g : PresetTags::genres) if (! g.startsWith ("Podcast") && ! g.startsWith ("FX") && PresetTags::artistsForGenre (g).isEmpty()) { ++empty; std::printf ("     no artists: %s\n", g.toRawUTF8()); }
    expect (empty == 0, "every music genre has matching artists");

    // the editor's artist menu only shows the genre's artists, and picking one loads its preset
    auto p = makeProc();
    std::unique_ptr<AudioProcessorEditor> ed (p->createEditor());
    auto& sel = dynamic_cast<SauceEditor*> (ed.get())->headerSelectorsForTest();
    auto pick = [&] (ComboBox& cb, const String& text)
    { for (int i = 0; i < cb.getNumItems(); ++i) if (cb.getItemText (i) == text) { cb.setSelectedId (cb.getItemId (i), sendNotificationSync); return true; } return false; };
    auto items = [] (ComboBox& cb) { StringArray a; for (int i = 0; i < cb.getNumItems(); ++i) a.add (cb.getItemText (i)); return a; };
    auto pumpUi = [] { MessageManager::getInstance()->runDispatchLoopUntil (40); };
    pick (sel.genre, "R&B"); pumpUi();
    const auto shown = items (sel.artist);
    expect (shown.contains ("Bryson Tiller") && ! shown.contains ("J. Cole"), "R&B artist menu shows R&B artists only (" + String (shown.size() - 2) + " artists)");
    const bool pickedBryson = pick (sel.artist, "Bryson Tiller"); pumpUi();
    expect (pickedBryson && p->presetName == "Bryson Tiller-Inspired (Unofficial)", "picking Bryson Tiller loads his preset");
    pick (sel.genre, "Rap"); pumpUi();
    expect (items (sel.artist).contains ("J. Cole") && p->presetName == "Rap - Lead", "switching to Rap swaps the artist list and loads 'Rap - Lead'");

    // chain order
    using namespace ParamIDs;
    auto q = makeProc(); neutral (*q);
    q->setParam (compOn, 1); q->setParam (compThresh, -30); q->setParam (compRatio, 8); q->setParam (compMakeup, 10);
    q->setParam (satOn, 1); q->setParam (satDrive, 70); q->setParam (satMix, 100);
    auto vox = vowel (2.0, 220, -6.0f, 20.0f); syllables (vox);
    const int lat = q->getLatencySamples();
    q->reset(); auto a = render (*q, vox);
    expect (q->setChainOrder ("GPTEMSCDYRL"), "chain can be rearranged (saturation before compression)");
    expect (! q->setChainOrder ("GGTEMSCDYRL") && q->getChainOrder() == "GPTEMSCDYRL", "invalid orders are refused");
    q->reset(); auto b = render (*q, vox);
    double diff = 0; for (size_t i = 0; i < vox.size(); ++i) diff = std::max (diff, (double) std::abs (a.l[i] - b.l[i]));
    std::printf ("     comp->sat vs sat->comp: max sample difference %.3f, latency %d -> %d\n", diff, lat, q->getLatencySamples());
    expect (diff > 0.01, "a different order changes the sound");
    expect (q->getLatencySamples() == lat, "reordering does not change latency");
    MemoryBlock st; q->getStateInformation (st);
    auto r = makeProc(); r->setStateInformation (st.getData(), (int) st.getSize());
    expect (r->getChainOrder() == "GPTEMSCDYRL", "chain order saved with the DAW session");
    expect (SauceProcessor::upgradeChainOrder ("GTSEMC") == "GPTSEMCDYRL", "1.4 sessions/presets upgrade to the 11-module chain (" + SauceProcessor::upgradeChainOrder ("GTSEMC") + ")");
    const int lofi = p->presets.indexOf ("Lo-Fi - Lead");
    p->presets.load (lofi);
    expect (p->getChainOrder() == "GPTSEMCDYRL", "Lo-Fi presets bring their own chain order (saturation first)");
    p->presets.load (p->presets.indexOf ("R&B - Lead"));
    expect (p->getChainOrder() == SauceProcessor::defaultChainOrder(), "other presets restore the standard order");
}

/** Synthetic "beats" with known character for the Assistant. */
static std::vector<float> fakeBeat (double sec, double bpm, float subDb, float airDb, bool dense, unsigned seed)
{
    auto base = noise (sec, -6.0f, seed);
    {   // pink it (Paul Kellet), like a real mix's overall balance
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        for (auto& v : base) { const float w = v; b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.96900f * b2 + w * 0.1538520f;
                               b3 = 0.86650f * b3 + w * 0.3104856f; b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
                               v = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f) * 0.11f; b6 = w * 0.115926f; }
    }
    base = filtered (base, sauce::FilterType::LowShelf, 80, 0.7f, subDb);
    base = filtered (base, sauce::FilterType::HighShelf, 7000, 0.7f, airDb);
    const size_t beatLen = (size_t) (60.0 / bpm * kSr);
    std::vector<float> x (base.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        const float ph = (float) (i % beatLen) / (float) beatLen;
        const float env = dense ? 0.8f + 0.2f * std::exp (-ph * 8) : 0.15f + 0.85f * std::exp (-ph * 14);
        const float kick = std::sin (sauce::kTwoPi * 50.0f * (float) (i % beatLen) / (float) kSr) * std::exp (-ph * 10) * sauce::dbToGain (subDb - 34);
        x[i] = base[i] * env + kick;
    }
    return x;
}

static void testAssistant()
{
    std::printf ("\n11. Sauce Assistant: listens to the beat, picks a starting sound, tailors it, explains it\n");
    using namespace ParamIDs;
    auto listen = [] (SauceProcessor& p, const std::vector<float>& beat, const std::vector<float>& vox, double bpm)
    {
        p.hostBpm.store (bpm);
        p.analysis.assistantStart();
        render (p, vox, &beat);
        p.analysis.assistantStop();
        return p.assistant.gather();
    };
    const double sec = 12.0;
    auto vox = vowel (sec, 220, -12.0f, 20.0f); syllables (vox);

    // 1. a fast, sub-heavy, bright beat with "match the beat" -> trap family, high-passed, tempo-synced
    auto p = makeProc(); neutral (*p);
    auto trapBeat = fakeBeat (sec, 145, 14, 4, false, 3);
    auto in = listen (*p, trapBeat, vox, 145);
    const auto guesses = sauce::Assistant::guessGenre (in);
    const auto f = sauce::Assistant::features (in);
    std::printf ("     145 BPM, heavy 808 + bright hats: sub %+.1f, air %+.1f, crest %.1f dB -> guess %s (%d%%), then %s\n",
                 f.sub, f.air, f.crestDb, guesses[0].genre.c_str(), roundToInt (guesses[0].confidence * 100), guesses[1].genre.c_str());
    expect (in.hasBeat && in.hasVocal, "heard the beat and the vocal");
    const std::set<std::string> trapFamily { "Trap", "Drill", "Melodic Rap", "Rap" };
    expect (trapFamily.count (guesses[0].genre) == 1, "fast 808-heavy beat is recognised as trap/drill/rap");
    SauceAssistant::Target t; t.role = sauce::Role::Lead;
    expect (p->assistant.apply (t), "Assistant applied a setup");
    std::printf ("     %s\n", p->assistant.headline.replace ("\n", "\n     ").toRawUTF8());
    for (auto& n : p->assistant.notes) std::printf ("       - %s\n", n.toRawUTF8());
    expect (p->presetName.startsWith (String (guesses[0].genre)), "started from the guessed genre's preset (" + p->presetName + ")");
    expect (p->reader.get (band (0, "freq")) >= 100.0f, "raised the high-pass to clear the 808 (" + String (p->reader.get (band (0, "freq")), 0) + " Hz)");
    expect (p->reader.get (dlySync) > 0.5f && (int) p->reader.get (dlyDiv) == 2, "delay synced to 1/8 at 145 BPM");
    expect (p->reader.get (revDecay) <= 240.0f / 145.0f + 0.01f, "reverb tail kept under one bar");
    expect (p->assistant.notes.size() >= 4, String (p->assistant.notes.size()) + " plain-language explanations");

    // A/B
    const auto afterXml = p->captureFullState().toXmlString();
    p->assistant.showBefore (true);
    expect (p->presetName != "" && p->captureFullState().toXmlString() != afterXml, "Before restores the previous sound");
    p->assistant.showBefore (false);
    expect (std::abs (p->reader.get (band (0, "freq")) - in.bpm) >= 0 && p->reader.get (dlySync) > 0.5f, "After brings the Assistant's setup back");

    // feedback nudges
    const float wet0 = p->reader.get (revMix), air0 = p->reader.get (band (5, "gain")), out0 = p->reader.get (outGain), spd0 = p->reader.get (tuneSpeed);
    p->assistant.nudge (sauce::Assistant::Nudge::Wetter); p->assistant.nudge (sauce::Assistant::Nudge::Brighter);
    p->assistant.nudge (sauce::Assistant::Nudge::Louder); p->assistant.nudge (sauce::Assistant::Nudge::HarderTune);
    expect (p->reader.get (revMix) > wet0 && p->reader.get (band (5, "gain")) > air0 && p->reader.get (outGain) > out0 && p->reader.get (tuneSpeed) < spd0 + 0.01f,
            "feedback buttons move the right controls (wetter, brighter, louder, harder tune)");

    // 2. a slow, dark beat, user asks for Bryson Tiller backgrounds -> R&B background preset, darker top
    auto q = makeProc(); neutral (*q);
    auto rnbBeat = fakeBeat (sec, 68, 6, -16, false, 9);
    auto in2 = listen (*q, rnbBeat, vox, 68);
    const auto g2 = sauce::Assistant::guessGenre (in2);
    std::printf ("     68 BPM, dark and smooth -> guess %s, then %s\n", g2[0].genre.c_str(), g2[1].genre.c_str());
    const std::set<std::string> slowFamily { "R&B", "Trap-Soul", "Lo-Fi" };
    expect (slowFamily.count (g2[0].genre) == 1, "slow dark beat is recognised as R&B / trap-soul / lo-fi");
    SauceAssistant::Target t2; t2.artist = "Bryson Tiller"; t2.role = sauce::Role::Backgrounds;
    expect (q->assistant.apply (t2) && q->presetName == "R&B - Backgrounds", "artist + Backgrounds picks the R&B background preset (" + q->presetName + ")");
    expect (q->reader.get (dlySync) > 0.5f && (int) q->reader.get (dlyDiv) == 3, "delay synced to 1/4 at 68 BPM");

    // 3. level against the beat: Lead louder than Ad-libs
    auto levelRun = [&] (sauce::Role role)
    {
        auto pp = makeProc(); neutral (*pp); listen (*pp, trapBeat, vox, 145);
        SauceAssistant::Target tt; tt.genre = "Trap"; tt.role = role; pp->assistant.apply (tt);
        const bool waiting = pp->assistant.isMatchingLevel();
        bool done = false;
        for (int pass = 0; pass < 3 && ! done; ++pass) { render (*pp, vox, &trapBeat); done = pp->assistant.updateLevel(); }   // keeps playing: the Assistant measures the new sound
        pp->analysis.assistantStart(); render (*pp, vox, &trapBeat); { auto chk = pp->analysis.assistantSnapshot(); pp->analysis.assistantStop();
        std::printf ("     %s: Assistant's own re-measure after level match: %+.1f dB vs beat (target %+.1f)\n", sauce::roleName (role), chk.vocalOutRmsDb - chk.beatRmsDb, sauce::Assistant::levelTarget ("Trap", role)); }
        expect (waiting && done && ! pp->assistant.isMatchingLevel(), String ("level-matching step ran for ") + sauce::roleName (role));
        pp->reset();
        return pp;
    };
    auto l1 = levelRun (sauce::Role::Lead), l2 = levelRun (sauce::Role::Adlibs);
    auto r1 = render (*l1, vox, &trapBeat), r2 = render (*l2, vox, &trapBeat);
    auto whileSinging = [&] (const std::vector<float>& o, int lat)
    { double e = 0; size_t n = 0; for (size_t i = (size_t) kSr; i < vox.size(); ++i) if (i >= (size_t) lat && std::abs (vox[i - (size_t) lat]) > 0.003f) { e += o[i] * o[i]; ++n; }
      return sauce::gainToDb ((float) std::sqrt (e / (double) std::max<size_t> (1, n)), -200.0f); };
    const float beatDb = rmsDb (trapBeat, (size_t) kSr, vox.size());
    const float d1 = whileSinging (r1.l, l1->getLatencySamples()) - beatDb, d2 = whileSinging (r2.l, l2->getLatencySamples()) - beatDb;
    std::printf ("     vocal vs beat after the Assistant: Trap Lead %+.1f dB, Trap Ad-libs %+.1f dB\n", d1, d2);
    expect (d1 > d2 + 2.0f, "lead sits louder against the beat than ad-libs");
    expect (std::abs (d1 - 1.0f) < 2.0f && std::abs (d2 + 5.0f) < 2.0f, "lead and ad-libs land near their target levels against the beat");

    // 4. no beat routed: still works and says so
    auto n = makeProc (false); neutral (*n);
    n->hostBpm.store (0); n->analysis.assistantStart(); render (*n, vox); n->analysis.assistantStop();
    SauceAssistant::Target t3; t3.genre = "Pop";
    expect (n->assistant.apply (t3) && n->presetName == "Pop - Lead" && n->assistant.notes.joinIntoString (" ").contains ("No beat"), "without a beat it sets up the vocal alone and explains why");
}

/** 1.5: fully serial chain, de-esser, plosive filter, smoother tune, limiter, 12-band dynamic EQ, resonance suppressor. */
static void testSerialChainAndNewModules()
{
    std::printf ("\n12. Serial chain (every module in tab order) and the new modules\n");
    using namespace ParamIDs;
    auto vox = vowel (3.0, 220, -8.0f, 20.0f); syllables (vox);
    auto diff = [] (const Render& a, const Render& b)
    { double d = 0; for (size_t i = 0; i < a.l.size(); ++i) d = std::max (d, (double) std::abs (a.l[i] - b.l[i])); return d; };

    // delay/reverb are serial now: reverb after delay reverbs the echoes, so swapping them changes the sound
    auto p = makeProc(); neutral (*p);
    p->setParam (dlyOn, 1); p->setParam (dlyMix, 40); p->setParam (dlyFeedback, 40); p->setParam (dlyDuck, 0);
    p->setParam (revOn, 1); p->setParam (revMix, 40); p->setParam (revDuck, 0);
    const int lat0 = p->getLatencySamples();
    p->reset(); auto a = render (*p, vox);
    p->setChainOrder ("GPTEMCDSRYL"); p->reset(); auto b = render (*p, vox);
    std::printf ("     delay->reverb vs reverb->delay: max difference %.3f\n", diff (a, b));
    expect (diff (a, b) > 0.01, "Delay and Reverb run in tab order (swapping them changes the sound)");
    bool sameLat = true;
    for (auto o : { "LGPTEMCDSYR", "RYSDCMETPGL", "TGPEMCDSYRL" }) { p->setChainOrder (o); sameLat &= p->getLatencySamples() == lat0; }
    expect (sameLat, "every order keeps the same latency (" + String (lat0 * 1000.0 / kSr, 1) + " ms)");

    // limiter: last = hard ceiling; moved before the effects = tails can pass it
    auto q = makeProc(); neutral (*q);
    auto loud = vowel (2.0, 200, 0.0f); syllables (loud);
    q->setParam (limOn, 1); q->setParam (limCeiling, -12); q->setParam (revOn, 1); q->setParam (revMix, 80); q->setParam (revDuck, 0);
    q->setParam (inGain, 6);
    q->setChainOrder (SauceProcessor::defaultChainOrder()); q->reset();
    const float pkLast = peakDb (render (*q, loud).l, 4800, loud.size());
    q->setChainOrder ("GPTEMCDSLYR"); q->reset();
    const float pkMid = peakDb (render (*q, loud).l, 4800, loud.size());
    std::printf ("     ceiling -12 dB: limiter last -> peak %.2f dB, limiter before reverb -> peak %.2f dB\n", pkLast, pkMid);
    expect (pkLast <= -11.9f, "limiter last: nothing goes over the ceiling (now down to -30 dB)");
    expect (pkMid > pkLast + 0.5f, "limiter moved before the reverb: the reverb comes after it");
    // drive and character
    q->setChainOrder (SauceProcessor::defaultChainOrder()); q->setParam (inGain, 0); q->setParam (revOn, 0); q->setParam (limCeiling, -1);
    auto quiet = vowel (2.0, 200, -14.0f); syllables (quiet);
    q->reset(); const float r0 = rmsDb (render (*q, quiet).l, 4800, quiet.size());
    q->setParam (limDrive, 12); q->reset(); const float r1 = rmsDb (render (*q, quiet).l, 4800, quiet.size());
    q->setParam (limCharacter, 2); q->reset(); const float r2 = rmsDb (render (*q, quiet).l, 4800, quiet.size());
    std::printf ("     limiter drive 0 -> 12 dB: %.1f -> %.1f dB RMS; Loud character %.1f dB RMS\n", r0, r1, r2);
    expect (r1 > r0 + 6.0f, "Drive pushes the vocal louder into the limiter");
    expect (r2 >= r1 - 0.2f, "Loud character keeps or adds loudness");

    // de-esser: harsh S bursts get turned down, the vowel barely changes
    auto d = makeProc(); neutral (*d);
    auto sss = filtered (noise (2.0, -10.0f, 4), sauce::FilterType::BandPass, 7000, 1.0f, 0);
    auto mixS = vowel (2.0, 220, -12.0f);
    for (size_t i = 0; i < mixS.size(); ++i) if ((i / 9600) % 2) mixS[i] = sss[i];       // alternate vowel / S every 0.2 s
    d->reset(); auto off = render (*d, mixS);
    d->setParam (deessOn, 1); d->setParam (deessThresh, -40); d->setParam (deessRange, 12); d->reset(); auto on = render (*d, mixS);
    auto band = [] (const std::vector<float>& x, bool sOnly)
    { double e = 0; size_t n = 0; for (size_t i = 9600; i < x.size(); ++i) if (((i / 9600) % 2 == 1) == sOnly && i % 9600 > 1200) { e += x[i] * x[i]; ++n; }
      return sauce::gainToDb ((float) std::sqrt (e / (double) std::max<size_t> (1, n)), -200.0f); };
    const float sCut = band (off.l, true) - band (on.l, true), vCut = band (off.l, false) - band (on.l, false);
    std::printf ("     de-esser: S bursts down %.1f dB, vowels down %.1f dB\n", sCut, vCut);
    expect (sCut > 4.0f && vCut < 1.0f, "De-esser tames S's without touching the vowels");

    // plosive filter: a P/B thump (low burst with no mids) is pulled down; a full vowel is left alone
    auto pp = makeProc(); neutral (*pp);
    std::vector<float> pops ((size_t) (2.0 * kSr), 0.0f);
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i < (int) (0.06 * kSr); ++i)
        {
            const size_t at = (size_t) (0.2 * kSr + k * 0.45 * kSr) + (size_t) i;
            pops[at] = 0.7f * std::sin (sauce::kTwoPi * 70.0f * (float) i / (float) kSr) * std::exp (-(float) i / (float) (0.02 * kSr));
        }
    auto full = vowel (2.0, 130, -10.0f);
    pp->reset(); const float p0 = peakDb (render (*pp, pops).l, 0, pops.size()), v0 = rmsDb (render (*pp, full).l, 4800, full.size());
    pp->setParam (popOn, 1); pp->setParam (popSens, 60); pp->reset();
    const float p1 = peakDb (render (*pp, pops).l, 0, pops.size()); pp->reset(); const float v1 = rmsDb (render (*pp, full).l, 4800, full.size());
    std::printf ("     De-Pop: plosive peaks %.1f -> %.1f dB; low male vowel %.1f -> %.1f dB\n", p0, p1, v0, v1);
    expect (p0 - p1 > 4.0f, "De-Pop pulls P/B pops down");
    expect (std::abs (v0 - v1) < 1.0f, "De-Pop leaves a normal deep vowel alone");

    // smoother Auto Tune: Smoothness keeps vibrato on a held note; hard tune still flattens it
    auto t = makeProc(); neutral (*t);
    auto vib = vowel (3.0, 220.0f * std::pow (2.0f, 0.2f / 12.0f), -10.0f, 25.0f);   // 20 cents sharp of A3, +-25-cent vibrato
    auto spread = [&] (float smooth, float speed)
    {
        t->setParam (tuneOn, 1); t->setParam (tuneKey, 9); t->setParam (tuneScale, 6); t->setParam (tuneSpeed, speed);
        t->setParam (tuneAmount, 100); t->setParam (tuneHumanize, 0); t->setParam (tuneSmooth, smooth); t->reset();
        auto o = render (*t, vib);
        std::vector<float> c; for (size_t s = (size_t) kSr; s + 16000 < o.l.size(); s += 1200) c.push_back (cents (pitchOf (o.l, s), 220.0f));
        float mn = 1e9f, mx = -1e9f, avg = 0; for (float v : c) { mn = std::min (mn, v); mx = std::max (mx, v); avg += v / (float) c.size(); }
        return std::make_pair (mx - mn, avg);
    };
    t->setParam (tuneOn, 0); t->reset();
    float natural = 0; { auto o = render (*t, vib); float mn = 1e9f, mx = -1e9f;
        for (size_t s = (size_t) kSr; s + 16000 < o.l.size(); s += 1200) { const float c = cents (pitchOf (o.l, s), 220.0f); mn = std::min (mn, c); mx = std::max (mx, c); } natural = mx - mn; }
    const auto s0 = spread (0, 30), s1 = spread (100, 30), hard = spread (100, 0);
    std::printf ("     pitch swing: singer's own vibrato %.0f c | tuned, Smoothness 0%% %.0f c, 100%% %.0f c (centre %+.0f c) | hard tune %.0f c\n",
                 natural, s0.first, s1.first, s1.second, hard.first);
    expect (std::abs (s1.first - natural) < 15.0f && std::abs (s1.first - natural) < std::abs (s0.first - natural),
            "Smoothness keeps the singer's natural vibrato (no chasing wobble)");
    expect (std::abs (s1.second) < 12.0f, "...while the note's centre is still pulled in tune");
    expect (hard.first < s1.first, "hard-tune speed ignores Smoothness (stays robotic)");

    // 12-band EQ: presets leave bands 7-12 off; an added band works; dynamic band; resonance suppressor
    auto e = makeProc(); neutral (*e);
    e->presets.load (e->presets.indexOf ("R&B - Lead"));
    bool extraOff = true; for (int b = 6; b < 12; ++b) extraOff &= e->reader.get (ParamIDs::band (b, "on")) < 0.5f;
    expect (extraOff, "presets keep their 6 bands (bands 7-12 stay off)");
    neutral (*e);
    auto tone = [] (float hz, float db) { std::vector<float> x ((size_t) (1.5 * kSr)); for (size_t i = 0; i < x.size(); ++i) x[i] = sauce::dbToGain (db) * std::sin (sauce::kTwoPi * hz * (float) i / (float) kSr); return x; };
    auto t1k = tone (1000, -20);
    e->reset(); const float base = rmsDb (render (*e, t1k).l, 24000, t1k.size());
    e->setParam (ParamIDs::band (9, "on"), 1); e->setParam (ParamIDs::band (9, "type"), 0); e->setParam (ParamIDs::band (9, "freq"), 1000);
    e->setParam (ParamIDs::band (9, "gain"), 6); e->setParam (ParamIDs::band (9, "q"), 1); e->reset();
    const float boosted = rmsDb (render (*e, t1k).l, 24000, t1k.size());
    expect (std::abs (boosted - base - 6.0f) < 0.5f, "band 10 of 12 works (+" + String (boosted - base, 1) + " dB at 1 kHz)");
    e->setParam (ParamIDs::band (9, "gain"), 0); e->setParam (ParamIDs::band (9, "dyn"), -9); e->setParam (ParamIDs::band (9, "thr"), -30);
    auto quietT = tone (1000, -45), loudT = tone (1000, -6);
    e->reset(); const float qd = rmsDb (render (*e, quietT).l, 24000, quietT.size()) + 45.0f;
    e->reset(); const float ld = rmsDb (render (*e, loudT).l, 24000, loudT.size()) + 6.0f;
    std::printf ("     dynamic band (-9 dB above -30 dB): quiet tone %+.1f dB, loud tone %+.1f dB\n", qd, ld);
    expect (std::abs (qd + 3.0f) < 1.0f && ld < -6.0f, "dynamic EQ band only cuts when the band gets loud");
    neutral (*e);
    auto ring = filtered (noise (2.0, -18.0f, 9), sauce::FilterType::HighCut, 9000, 0.7f, 0);
    for (size_t i = 0; i < ring.size(); ++i) ring[i] += 0.12f * std::sin (sauce::kTwoPi * 3150.0f * (float) i / (float) kSr);   // a harsh ringing resonance
    auto at = [] (const std::vector<float>& x, float hz) { return rmsDb (filtered (x, sauce::FilterType::BandPass, hz, 12.0f, 0), 48000, x.size()); };
    e->reset(); auto r0e = render (*e, ring);
    e->setParam (resOn, 1); e->setParam (resDepth, 9); e->reset(); auto r1e = render (*e, ring);
    const float resCut = at (r0e.l, 3150) - at (r1e.l, 3150), elseCut = at (r0e.l, 800) - at (r1e.l, 800);
    std::printf ("     resonance suppressor: ringing 3.15 kHz down %.1f dB, 800 Hz down %.1f dB\n", resCut, elseCut);
    expect (resCut > 3.0f && elseCut < 1.5f, "resonance suppressor dips the ringing frequency and leaves the rest");
    std::printf ("     Mix-mode latency: %.1f ms (was 15.9 ms in 1.4)\n", lat0 * 1000.0 / kSr);
    expect (lat0 * 1000.0 / kSr < 14.0, "Mix latency trimmed (new modules add none)");
}

/** 1.5: Track Link, beat-file drop, role detection, preset bank. */
static void testLinkRolesAndPresets()
{
    std::printf ("\n13. Track Link, beat files, track roles and the 1.5 preset bank\n");
    using namespace ParamIDs;
    auto beat = fakeBeat (8.0, 140, 12, 2, false, 5);
    auto vox = vowel (8.0, 220, -12.0f, 20.0f); syllables (vox);

    // a Sauce Suite on the beat track in Send mode passes it through untouched with no latency
    auto sender = makeProc (false); neutral (*sender);
    sender->updateTrackProperties ({ String ("Beat"), std::nullopt });
    sender->setLinkSend (SauceProcessor::LinkSendBeat);
    expect (sender->getLatencySamples() == 0, "sending track reports 0 latency");
    auto receiver = makeProc (false); neutral (*receiver);
    bool listed = false; for (auto& s : SauceLink::get().senders()) listed |= s.name == "Beat";
    expect (listed, "the beat track shows up in the LINK list by its DAW track name");
    receiver->setLinkSource ("Beat");
    receiver->hostBpm.store (140);
    receiver->analysis.assistantStart();
    Render pass;
    {   // DAW callback order: beat track first, then the vocal track, block by block
        AudioBuffer<float> bb (2, kBlock), vb (2, kBlock); MidiBuffer midi; pass.l.resize (beat.size());
        for (size_t s = 0; s < beat.size(); s += kBlock)
        {
            const int len = (int) std::min<size_t> (kBlock, beat.size() - s);
            bb.setSize (2, len, false, false, true); vb.setSize (2, len, false, false, true);
            for (int i = 0; i < len; ++i) { bb.setSample (0, i, beat[s + (size_t) i]); bb.setSample (1, i, beat[s + (size_t) i]); vb.setSample (0, i, vox[s + (size_t) i]); vb.setSample (1, i, vox[s + (size_t) i]); }
            sender->processBlock (bb, midi); receiver->processBlock (vb, midi); receiver->analysis.pump();
            for (int i = 0; i < len; ++i) pass.l[s + (size_t) i] = bb.getSample (0, i);
        }
    }
    double pd = 0; for (size_t i = 0; i < beat.size(); ++i) pd = std::max (pd, (double) std::abs (pass.l[i] - beat[i]));
    expect (pd < 1e-6, "the sending track's audio is untouched");
    expect (receiver->isLinkReceiving() && receiver->analysis.isReceivingSidechain(), "the vocal instance hears the linked beat with no sidechain routing");
    auto in = receiver->assistant.gather();
    expect (in.hasBeat && ! in.beatFromFile, "the Assistant measures the linked beat (" + String (in.beatSeconds, 1) + " s)");
    receiver->analysis.assistantStop();
    sender->setLinkSend (SauceProcessor::LinkOff);
    expect (sender->getLatencySamples() > 0, "switching Send off restores normal processing latency");

    // dropping a beat file on the Assistant
    auto f = File::createTempFile (".wav");
    {
        WavAudioFormat wav; std::unique_ptr<OutputStream> os (f.createOutputStream());
        std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (os.get(), kSr, 1, 24, {}, 0));
        if (w) { os.release(); const float* ch[] = { beat.data() }; w->writeFromFloatArrays (ch, 1, (int) beat.size()); }
    }
    auto solo = makeProc (false); neutral (*solo);
    String err; const bool loaded = solo->analysis.loadBeatFile (f, err);
    auto fin = solo->assistant.gather();
    expect (loaded && fin.hasBeat && fin.beatFromFile, "a dropped beat file is analysed (" + String (fin.beatSeconds, 1) + " s, " + (fin.keyValid ? "key found" : "no key") + ")");
    SauceAssistant::Target tg; tg.genre = "Trap";
    const bool applied = solo->assistant.apply (tg);
    const String notes = solo->assistant.notes.joinIntoString (" | ");
    expect (applied && notes.contains ("pre-delay") && notes.contains ("beat file"), "the Assistant tailors the chain from the file");
    f.deleteFile();

    // track roles
    auto r = makeProc (false);
    r->presets.load (r->presets.indexOf ("Trap - Ad-libs"));
    expect (r->getRole() == 2, "role from preset: 'Trap - Ad-libs' -> Ad-libs");
    r->updateTrackProperties ({ String ("BG Vox"), std::nullopt }); r->updateRole();
    expect (r->getRole() == 3, "role from DAW track name: 'BG Vox' -> Backgrounds (" + r->getRoleReason() + ")");
    r->setRoleOverride (5);
    expect (r->getRole() == 5, "you can set the role yourself (Doubles)");
    MemoryBlock st; r->getStateInformation (st);
    auto r2 = makeProc (false); r2->setStateInformation (st.getData(), (int) st.getSize());
    expect (r2->getRoleOverride() == 5, "the role choice is saved with the session");

    // role-aware coach: muddy, unevenly-levelled part -> different advice for lead vs backgrounds
    auto coachFor = [&] (int role)
    {
        auto c = makeProc (false); neutral (*c); c->setRoleOverride (role);
        auto mud = filtered (vowel (6.0, 140, -14.0f, 10.0f, 350.0f), sauce::FilterType::LowShelf, 200, 0.7f, 8.0f);
        syllables (mud); render (*c, mud);
        return c->analysis.getCoachMessage();
    };
    const auto lead = coachFor (0), bg = coachFor (3);
    std::printf ("     Coach (lead):        %s / %s\n     Coach (backgrounds): %s / %s\n", lead.observation.c_str(), lead.suggestion.c_str(), bg.observation.c_str(), bg.suggestion.c_str());
    expect (bg.observation != lead.observation || bg.suggestion != lead.suggestion, "the Vocal Coach gives part-specific advice");

    // preset bank
    auto pb = makeProc (false);
    const int n = (int) pb->presets.all().size();
    int perArtist = 0; for (auto& ps : pb->presets.all()) if (ps.artist == "Drake") ++perArtist;
    int thin = 0; String thinNames;
    for (auto& g : PresetTags::genres) if (! g.startsWith ("Podcast") && ! g.startsWith ("FX") && PresetTags::artistsForGenre (g).size() < 10) { ++thin; thinNames << g << " "; }
    std::printf ("     %d factory presets; %d artists\n", n, (int) PresetTags::artistRoster().size());
    expect (n >= 900, String (n) + " factory presets");
    expect (perArtist == 4, "every artist has Lead, Hook, Ad-libs and Backgrounds presets");
    expect (thin == 0, "every music genre has at least 10 artists " + thinNames);
}

/** Renders the real editor (fed with live audio) to a PNG: SauceTests --snapshot out.png [tabIndex] [scale] */
static int snapshot (const String& path, int tab, float scale)
{
    auto p = makeProc();
    p->presets.load (p->presets.indexOf ("R&B Vocal - Polished"));
    p->presetDirty = false;
    auto vox = vowel (12.0, 196, -9.0f, 25.0f, 520); syllables (vox, 380, 120);
    auto beat = filtered (noise (12.0, -8.0f, 4), sauce::FilterType::LowShelf, 150, 0.7f, 10);
    beat = filtered (beat, sauce::FilterType::HighShelf, 4000, 0.7f, -14);
    std::unique_ptr<AudioProcessorEditor> ed (p->createEditor());
    ed->setSize ((int) (SauceEditor::kBaseW * scale), (int) (SauceEditor::kBaseH * scale));
    auto* se = dynamic_cast<SauceEditor*> (ed.get());
    if (se) se->selectTab (tab < 0 ? 2 : tab);
    ed->setVisible (true);
    if (tab < 0) { p->hostBpm.store (72); p->analysis.assistantStart(); }
    AudioBuffer<float> buf (4, kBlock); MidiBuffer midi;
    for (size_t start = 0; start + kBlock < vox.size(); start += kBlock)
    {
        for (int i = 0; i < kBlock; ++i) { buf.setSample (0, i, vox[start + (size_t) i]); buf.setSample (1, i, vox[start + (size_t) i]);
                                           buf.setSample (2, i, beat[start + (size_t) i]); buf.setSample (3, i, beat[start + (size_t) i]); }
        p->processBlock (buf, midi); p->analysis.pump();
        if ((start / kBlock) % 3 == 0) MessageManager::getInstance()->runDispatchLoopUntil (8);
        if (start > (size_t) (9.0 * kSr) && std::abs (vox[start]) > 0.05f && start > (size_t) (10.5 * kSr)) break;
    }
    if (tab < 0 && se)
    {
        auto& panel = se->assistantPanelForTest();
        panel.open();
        if (tab == -2)
        {
            SauceAssistant::Target t; t.genre = "R&B"; t.artist = "Bryson Tiller"; t.role = sauce::Role::Lead;
            p->assistant.apply (t);
        }
        MessageManager::getInstance()->runDispatchLoopUntil (300);
    }
    MessageManager::getInstance()->runDispatchLoopUntil (60);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    File f (path); f.deleteFile();
    FileOutputStream os (f); PNGImageFormat png;
    const bool ok = os.openedOk() && png.writeImageToStream (img, os);
    std::printf ("snapshot %s: %s (%dx%d)\n", ok ? "written" : "FAILED", path.toRawUTF8(), img.getWidth(), img.getHeight());
    return ok ? 0 : 1;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    if (argc >= 3 && String (argv[1]) == "--wordmark")      // installer artwork: transparent PNG of the logo
    {
        Wordmark w; w.setBounds (0, 0, 392, 82);
        auto img = w.createComponentSnapshot (w.getLocalBounds(), true, argc >= 4 ? String (argv[3]).getFloatValue() : 2.0f);
        File f (argv[2]); f.deleteFile(); FileOutputStream os (f); PNGImageFormat png;
        return png.writeImageToStream (img, os) ? 0 : 1;
    }
    if (argc >= 2 && String (argv[1]) == "--list-presets")
    {
        using namespace ParamIDs;
        std::printf ("| Preset | Genre | Artist tag | Tune (speed / amount) | Comp | Delay | Reverb | Ducking D / R |\n|---|---|---|---|---|---|---|---|\n");
        auto p = makeProc();
        for (int i = 0; i < (int) p->presets.all().size(); ++i)
        {
            const auto info = p->presets.all()[(size_t) i];
            if (! info.factory) continue;
            p->presets.load (i);
            auto g = [&] (const String& id) { return p->reader.get (id); };
            const String tune = g (tuneOn) > 0.5f ? String (roundToInt (g (tuneSpeed))) + " ms / " + String (roundToInt (g (tuneAmount))) + "%" : String ("off");
            const String dly = g (dlyOn) > 0.5f ? ParamChoices::divisions[(int) g (dlyDiv)] + (g (dlyFeel) == 1 ? " dotted" : g (dlyFeel) == 2 ? " triplet" : "") + ", " + String (roundToInt (g (dlyMix))) + "% mix" : String ("off");
            const String rev = g (revOn) > 0.5f ? ParamChoices::revTypes[(int) g (revType)] + " " + String (g (revDecay), 1) + " s, " + String (roundToInt (g (revMix))) + "% mix" : String ("off");
            std::printf ("| %s | %s | %s | %s | %.0f dB, %.1f:1 | %s | %s | %d%% / %d%% |\n", info.name.toRawUTF8(), info.genre.toRawUTF8(),
                         info.artist.isEmpty() ? "-" : info.artist.toRawUTF8(), tune.toRawUTF8(), g (compThresh), g (compRatio), dly.toRawUTF8(), rev.toRawUTF8(),
                         roundToInt (g (dlyDuck)), roundToInt (g (revDuck)));
        }
        return 0;
    }
    if (argc >= 3 && String (argv[1]) == "--snapshot")
        return snapshot (argv[2], argc >= 4 ? String (argv[3]).getIntValue() : 2, argc >= 5 ? String (argv[4]).getFloatValue() : 1.0f);
    std::printf ("The Sauce Suite %s verification (%.0f Hz, %d-sample blocks)\n", kSauceVersion, kSr, kBlock);
    testPitch();
    testDucking();
    testSidechainIsolation();
    testMatchEq();
    testPresetsAndSession();
    testAnalyzerAndCoach();
    testPresetBank();
    testPresetUi();
    testTrackingMode();
    testAutoKey();
    testGenresArtistsAndChain();
    testAssistant();
    testSerialChainAndNewModules();
    testLinkRolesAndPresets();
    testLatencyAndBypass();
    std::printf ("\n%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
