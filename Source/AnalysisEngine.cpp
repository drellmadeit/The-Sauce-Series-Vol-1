#include "AnalysisEngine.h"
#include "PluginProcessor.h"

using namespace juce;

AnalysisEngine::AnalysisEngine (SauceProcessor& p) : Thread ("Sauce Analysis"), proc (p)
{
    for (auto& r : ring) r.assign ((size_t) kFifoSize, 0.0f);
    scFlag.assign ((size_t) kFifoSize, 0);
    vocHist.assign (4096, 0.0f); beatHist.assign (4096, 0.0f);
    vocDisp.assign (kDisplayPoints, -120.0f); beatDisp.assign (kDisplayPoints, -120.0f);
    formats.registerBasicFormats();
}

AnalysisEngine::~AnalysisEngine() { stop(); }

void AnalysisEngine::prepare (double sampleRate, int)
{
    const ScopedLock sl (lock);
    sr = sampleRate;
    fifo.reset();
    coach.prepare (sampleRate);
    keyDetector.prepare (sampleRate);
    // Captured averages live on a log-frequency grid, so they survive sample-rate changes and session reloads.
    accMine.reset(); accRef.reset();
    capture.store (Capture::None);
    builtVersion = -1;
}

void AnalysisEngine::push (const float* in, const float* eq, const float* lead, const float* out, const float* sc, bool scPresent, int n) noexcept
{
    int s1, n1, s2, n2;
    fifo.prepareToWrite (n, s1, n1, s2, n2);
    const float* src[kStreams] = { in, eq, lead, out, sc };
    for (int k = 0; k < kStreams; ++k)
    {
        if (n1 > 0) std::memcpy (ring[k].data() + s1, src[k], (size_t) n1 * sizeof (float));
        if (n2 > 0) std::memcpy (ring[k].data() + s2, src[k] + n1, (size_t) n2 * sizeof (float));
    }
    if (n1 > 0) std::memset (scFlag.data() + s1, scPresent ? 1 : 0, (size_t) n1);
    if (n2 > 0) std::memset (scFlag.data() + s2, scPresent ? 1 : 0, (size_t) n2);
    fifo.finishedWrite (n1 + n2);          // if the FIFO is full the newest audio is dropped (analysis only)
}

void AnalysisEngine::run()
{
    while (! threadShouldExit())
    {
        pump();
        wait (12);
    }
}

void AnalysisEngine::pump()
{
    const int avail = fifo.getNumReady();
    const bool instrumentalRole = (int) proc.reader.get (ParamIDs::scRole) == 0;
    const float ceilLin = sauce::dbToGain (proc.reader.get (ParamIDs::limCeiling));
    const bool limOn = proc.reader.get (ParamIDs::limOn) > 0.5f;

    if (avail > 0)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (avail, s1, n1, s2, n2);
        for (int k = 0; k < kStreams; ++k)
        {
            chunk[k].resize ((size_t) avail);
            if (n1 > 0) std::memcpy (chunk[k].data(), ring[k].data() + s1, (size_t) n1 * sizeof (float));
            if (n2 > 0) std::memcpy (chunk[k].data() + n1, ring[k].data() + s2, (size_t) n2 * sizeof (float));
        }
        chunkFlag.resize ((size_t) avail);
        if (n1 > 0) std::memcpy (chunkFlag.data(), scFlag.data() + s1, (size_t) n1);
        if (n2 > 0) std::memcpy (chunkFlag.data() + n1, scFlag.data() + s2, (size_t) n2);
        fifo.finishedRead (n1 + n2);

        const float* in = chunk[0].data(); const float* eq = chunk[1].data(); const float* lead = chunk[2].data();
        const float* out = chunk[3].data(); const float* sc = chunk[4].data();

        // sidechain presence (level of the routed signal, ~1 s memory)
        bool anyFlag = false;
        for (int i = 0; i < avail; ++i)
        {
            anyFlag |= chunkFlag[(size_t) i] != 0;
            const float a = std::abs (sc[i]);
            scEnv = a > scEnv ? a : scEnv * 0.99995f;
        }
        scLevelDb.store (anyFlag ? sauce::gainToDb (scEnv, -120.0f) : -120.0f);

        const ScopedLock sl (lock);
        // ---- rolling buffers for the display analyser ----
        auto roll = [avail] (std::vector<float>& hist, const float* x)
        {
            const int keep = (int) hist.size();
            if (avail >= keep) std::memcpy (hist.data(), x + avail - keep, (size_t) keep * sizeof (float));
            else { std::memmove (hist.data(), hist.data() + avail, (size_t) (keep - avail) * sizeof (float));
                   std::memcpy (hist.data() + keep - avail, x, (size_t) avail * sizeof (float)); }
        };
        roll (vocHist, eq);
        roll (beatHist, sc);

        // ---- Match EQ captures ----
        const auto cap = capture.load();
        if (cap == Capture::Mine) { accMine.feed (eq, avail, sr); if (accMine.frames > 0) mineAvg = accMine.averageDb(); }
        if (cap == Capture::ReferenceSidechain) { accRef.feed (sc, avail, sr); if (accRef.frames > 0) { refAvg = accRef.averageDb(); refFromFile = false; refName = "Sidechain capture"; } }

        // ---- Vocal Coach: beat only counts when routed as Instrumental and actually playing ----
        const bool beatForCoach = instrumentalRole && scLevelDb.load() > -60.0f;
        coach.feed (in, lead, out, beatForCoach ? sc : nullptr, avail, ceilLin, limOn);
        keyDetector.feed (in, beatForCoach ? sc : nullptr, avail);     // raw vocal pitch (before tuning) + beat

        if (assistListening.load())
        {
            if (beatForCoach)
            {
                assistBeat.feed (sc, avail, sr);
                for (int i = 0; i < avail; ++i) { assistBeatPow += (double) sc[i] * sc[i]; assistBeatPeak = jmax (assistBeatPeak, std::abs (sc[i])); }
                assistBeatN += avail;
            }
            assistVocal.feed (in, avail, sr);
            for (int i = 0; i < avail; ++i)
                if (std::abs (lead[i]) > 0.003f) { assistVocalPow += (double) out[i] * out[i]; ++assistVocalN; }   // only while singing
        }

        sinceDisplay += avail;
    }

    // ---- display spectra (~30 fps; keeps decaying to silence when audio stops) ----
    {
        const ScopedLock sl (lock);
        if (sinceDisplay >= (int) (sr / 30.0) || avail == 0)
        {
            sinceDisplay = 0;
            const bool beatOn = instrumentalRole && scLevelDb.load() > -60.0f;
            auto toDisplay = [this] (const std::vector<float>& hist, std::vector<float>& disp)
            {
                fft.power (hist.data(), specTmp);
                const double binHz = sr / 4096.0;
                for (int i = 0; i < kDisplayPoints; ++i)
                {
                    const float f = displayFreq (i);
                    const float lo = f * 0.986f, hi = f * 1.0145f;
                    const int a = jmax (1, (int) std::ceil (lo / binHz)), b = jmin ((int) specTmp.size() - 1, (int) std::floor (hi / binHz));
                    float pk = 0;
                    if (a <= b) for (int k = a; k <= b; ++k) pk = jmax (pk, specTmp[(size_t) k]);
                    else
                    {   // fewer bins than display points (low end): interpolate between the two nearest bins
                        const double pos = f / binHz; const int k0 = jlimit (1, (int) specTmp.size() - 2, (int) pos);
                        const float t = (float) (pos - k0);
                        pk = specTmp[(size_t) k0] * (1.0f - t) + specTmp[(size_t) k0 + 1] * t;
                    }
                    // +4.5 dB/oct tilt so a voice reads "flat" like pro analysers
                    const float db = 10.0f * std::log10 (pk + 1e-14f) + 4.5f * std::log2 (f / 1000.0f);
                    float& d = disp[(size_t) i];
                    d = db > d ? db : jmax (db, d - 1.6f);
                }
            };
            toDisplay (vocHist, vocDisp);
            if (beatOn) toDisplay (beatHist, beatDisp);
            else for (auto& d : beatDisp) d = jmax (-120.0f, d - 3.0f);
            beatDispActive = beatOn;
        }
    }
    recomputeMatchGains (false);
}

void AnalysisEngine::getSpectra (std::vector<float>& v, std::vector<float>& b, bool& beatActive) const
{
    const ScopedLock sl (lock);
    v = vocDisp; b = beatDisp; beatActive = beatDispActive;
}

//==============================================================================
void AnalysisEngine::setCapture (Capture c)
{
    const ScopedLock sl (lock);
    if (c == Capture::Mine && capture.load() != Capture::Mine) accMine.reset();
    if (c == Capture::ReferenceSidechain && capture.load() != Capture::ReferenceSidechain) accRef.reset();
    capture.store (c);
}

bool AnalysisEngine::loadReferenceFile (const File& f, String& error)
{
    std::unique_ptr<AudioFormatReader> r (formats.createReaderFor (f));
    if (r == nullptr) { error = "Couldn't read that file. Use WAV, AIFF, FLAC, OGG or MP3."; return false; }
    const auto maxLen = (int64) jmin<double> ((double) r->lengthInSamples, r->sampleRate * 600.0);
    AudioBuffer<float> buf ((int) r->numChannels, (int) maxLen);
    r->read (&buf, 0, (int) maxLen, 0, true, true);
    std::vector<float> mono ((size_t) maxLen, 0.0f);
    for (int c = 0; c < buf.getNumChannels(); ++c)
        for (int i = 0; i < (int) maxLen; ++i) mono[(size_t) i] += buf.getSample (c, i) / (float) buf.getNumChannels();
    sauce::SpectrumAccumulator acc;
    acc.feed (mono.data(), (int) mono.size(), r->sampleRate);
    if (! acc.hasData()) { error = "That file is too quiet or too short to analyse."; return false; }
    const ScopedLock sl (lock);
    refAvg = acc.averageDb(); refFromFile = true; refName = f.getFileName();
    if (capture.load() == Capture::ReferenceSidechain) capture.store (Capture::None);
    return true;
}

void AnalysisEngine::resetMatch()
{
    const ScopedLock sl (lock);
    capture.store (Capture::None);
    accMine.reset(); accRef.reset(); mineAvg.clear(); refAvg.clear(); appliedRaw.clear(); refFromFile = false; refName.clear();
    ++appliedVersion;
}

void AnalysisEngine::applySuggestion()
{
    const ScopedLock sl (lock);
    if (mineAvg.empty() || refAvg.empty()) return;
    appliedRaw = sauce::MatchCurve::rawDifference (mineAvg, refAvg);
    ++appliedVersion;
}

std::vector<float> AnalysisEngine::getAppliedRaw() const { const ScopedLock sl (lock); return appliedRaw; }

void AnalysisEngine::setAppliedRaw (const std::vector<float>& raw)
{
    {
        const ScopedLock sl (lock);
        appliedRaw = (int) raw.size() == sauce::LogGrid::kPoints ? raw : std::vector<float>{};
        ++appliedVersion;
    }
    recomputeMatchGains (true);
}

String AnalysisEngine::captureStateToString() const
{
    const ScopedLock sl (lock);
    auto enc = [] (const std::vector<float>& v) { String s; for (float x : v) s << String (x, 2) << ','; return s; };
    return enc (mineAvg) + "|" + enc (refAvg) + "|" + refName;
}

void AnalysisEngine::captureStateFromString (const String& s)
{
    const ScopedLock sl (lock);
    auto parts = StringArray::fromTokens (s, "|", "");
    auto dec = [] (const String& t) { std::vector<float> v; for (auto& x : StringArray::fromTokens (t, ",", "")) if (x.isNotEmpty()) v.push_back (x.getFloatValue()); return v; };
    mineAvg = parts.size() > 0 ? dec (parts[0]) : std::vector<float>{};
    refAvg  = parts.size() > 1 ? dec (parts[1]) : std::vector<float>{};
    if ((int) mineAvg.size() != sauce::LogGrid::kPoints) mineAvg.clear();
    if ((int) refAvg.size()  != sauce::LogGrid::kPoints) refAvg.clear();
    refName = parts.size() > 2 ? parts[2] : String();
    refFromFile = ! refAvg.empty();
}

AnalysisEngine::MatchView AnalysisEngine::getMatchView() const
{
    const float strength = proc.reader.get (ParamIDs::matchStrength) * 0.01f;
    const float smooth   = proc.reader.get (ParamIDs::matchSmooth);
    const float limit    = proc.reader.get (ParamIDs::matchLimit);
    const ScopedLock sl (lock);
    MatchView v;
    v.mine = mineAvg; v.ref = refAvg; v.hasMine = ! mineAvg.empty(); v.hasRef = ! refAvg.empty();
    v.mineSec = accMine.seconds (sr); v.refSec = refFromFile ? 0.0 : accRef.seconds (sr); v.refName = refName;
    if (v.hasMine && v.hasRef)
        v.suggested = sauce::MatchCurve::shape (sauce::MatchCurve::rawDifference (mineAvg, refAvg), smooth, strength, limit);
    if (! appliedRaw.empty())
    {
        v.hasApplied = true;
        const auto g = proc.getMatchGains();
        v.applied.resize (sauce::LogGrid::kPoints);
        for (int i = 0; i < sauce::LogGrid::kPoints; ++i) v.applied[(size_t) i] = sauce::MatchCurve::responseDb (g, sauce::LogGrid::freq (i), sr);
    }
    return v;
}

void AnalysisEngine::recomputeMatchGains (bool force)
{
    const float strength = proc.reader.get (ParamIDs::matchStrength) * 0.01f;
    const float smooth   = proc.reader.get (ParamIDs::matchSmooth);
    const float limit    = proc.reader.get (ParamIDs::matchLimit);
    std::vector<float> raw; int ver;
    {
        const ScopedLock sl (lock);
        if (! force && strength == lastStrength && smooth == lastSmooth && limit == lastLimit && builtVersion == appliedVersion) return;
        lastStrength = strength; lastSmooth = smooth; lastLimit = limit; ver = appliedVersion; raw = appliedRaw;
    }
    std::array<float, sauce::MatchFilterbank::kBands> g {};
    if (! raw.empty())
        g = sauce::MatchCurve::gainsFor (sauce::MatchCurve::shape (raw, smooth, strength, limit), limit, sr);
    proc.setMatchGains (g);
    const ScopedLock sl (lock);
    builtVersion = ver;
}

void AnalysisEngine::assistantStart()
{
    const ScopedLock sl (lock);
    assistBeat.reset(); assistVocal.reset();
    assistBeatPow = assistVocalPow = 0; assistBeatPeak = 0; assistBeatN = assistVocalN = 0;
    assistListening.store (true);
}

sauce::AssistantInput AnalysisEngine::assistantSnapshot() const
{
    const ScopedLock sl (lock);
    sauce::AssistantInput in;
    in.beatSeconds = assistBeat.seconds (sr); in.vocalSeconds = assistVocal.seconds (sr);
    in.hasBeat = in.beatSeconds >= 4.0 && assistBeatN > 0;
    in.hasVocal = in.vocalSeconds >= 3.0;
    if (in.hasBeat)
    {
        in.beatSpec = assistBeat.averageDb();
        in.beatRmsDb = sauce::gainToDb ((float) std::sqrt (assistBeatPow / (double) assistBeatN), -100.0f);
        in.beatPeakDb = sauce::gainToDb (assistBeatPeak, -100.0f);
    }
    if (in.hasVocal)
    {
        in.vocalSpec = assistVocal.averageDb();
        if (assistVocalN > 0) in.vocalOutRmsDb = sauce::gainToDb ((float) std::sqrt (assistVocalPow / (double) assistVocalN), -100.0f);
    }
    return in;
}

sauce::KeyDetector::Estimate AnalysisEngine::getKeyEstimate() const { const ScopedLock sl (lock); return keyDetector.estimate(); }

sauce::VocalCoach::Message AnalysisEngine::getCoachMessage() const { const ScopedLock sl (lock); return coach.message(); }
