#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "dsp/Analysis.h"
#include "dsp/Assistant.h"

class SauceProcessor;

/** Everything that measures audio but must never run on the audio thread:
    the live spectrum, Match EQ captures/curve fitting, and the Vocal Coach.
    The audio thread only writes into a lock-free FIFO (push()). */
class AnalysisEngine : private juce::Thread
{
public:
    explicit AnalysisEngine (SauceProcessor& p);
    ~AnalysisEngine() override;

    void prepare (double sampleRate, int maxBlock);
    void start()  { if (! isThreadRunning()) startThread (juce::Thread::Priority::low); }
    void stop()   { stopThread (2000); }

    // ---- audio thread (lock-free, never blocks) ----
    void push (const float* in, const float* eq, const float* lead, const float* out, const float* sc, bool scPresent, int n) noexcept;

    // ---- analysis work; the thread calls this, tests may call it directly ----
    void pump();

    // ---- display spectrum (GUI thread) ----
    static constexpr int kDisplayPoints = 240;
    static float displayFreq (int i) { return 20.0f * std::pow (1000.0f, (float) i / (float) (kDisplayPoints - 1)); }
    void getSpectra (std::vector<float>& vocalDb, std::vector<float>& beatDb, bool& beatActive) const;
    bool isReceivingSidechain() const { return scLevelDb.load() > -60.0f; }
    float sidechainLevelDb() const    { return scLevelDb.load(); }

    // ---- Match EQ ----
    enum class Capture { None, Mine, ReferenceSidechain };
    void setCapture (Capture c);
    Capture getCapture() const { return capture.load(); }
    bool loadReferenceFile (const juce::File& f, juce::String& error);   // decodes + analyses on the calling (message) thread's job
    void resetMatch();
    void applySuggestion();
    struct MatchView { std::vector<float> mine, ref, suggested, applied; double mineSec = 0, refSec = 0; bool hasMine = false, hasRef = false, hasApplied = false; juce::String refName; };
    MatchView getMatchView() const;
    std::vector<float> getAppliedRaw() const;
    void setAppliedRaw (const std::vector<float>& raw);                 // from preset / session state
    juce::String captureStateToString() const;
    void captureStateFromString (const juce::String&);

    // ---- Sauce Assistant listening session ----
    void assistantStart();
    void assistantStop() { assistListening.store (false); }
    bool assistantIsListening() const { return assistListening.load(); }
    sauce::AssistantInput assistantSnapshot() const;          // spectra + levels gathered so far (bpm/key filled by the caller)
    /** Analyses an instrumental file (drag & drop) so the Assistant can work without any routing. Any thread. */
    bool loadBeatFile (const juce::File& f, juce::String& error);
    juce::String beatFileName() const { const juce::ScopedLock sl (lock); return fileBeatName; }
    void clearBeatFile() { const juce::ScopedLock sl (lock); fileBeatName.clear(); fileBeatSpec.clear(); }

    // ---- Auto Key (analysis only; never in the audio path) ----
    sauce::KeyDetector::Estimate getKeyEstimate() const;

    // ---- Vocal Coach ----
    sauce::VocalCoach::Message getCoachMessage() const;

private:
    void run() override;
    void recomputeMatchGains (bool force);

    SauceProcessor& proc;
    double sr = 44100;
    static constexpr int kFifoSize = 1 << 16, kStreams = 5;
    juce::AbstractFifo fifo { kFifoSize };
    std::vector<float> ring[kStreams];
    std::vector<uint8_t> scFlag;

    std::vector<float> chunk[kStreams];
    std::vector<uint8_t> chunkFlag;

    // display
    sauce::FFT fft { 12 };
    std::vector<float> vocHist, beatHist, specTmp;
    std::vector<float> vocDisp, beatDisp;
    bool beatDispActive = false;
    int sinceDisplay = 0;
    std::atomic<float> scLevelDb { -120.0f };
    float scEnv = 0.0f;

    // match
    std::atomic<Capture> capture { Capture::None };
    sauce::SpectrumAccumulator accMine, accRef;
    std::vector<float> mineAvg, refAvg, appliedRaw;
    bool refFromFile = false; juce::String refName;
    float lastStrength = -1, lastSmooth = -1, lastLimit = -1; int appliedVersion = 0, builtVersion = -1;

    // coach
    sauce::VocalCoach coach;
    sauce::KeyDetector keyDetector;
    std::atomic<bool> assistListening { false };
    sauce::SpectrumAccumulator assistBeat, assistVocal;
    std::vector<float> fileBeatSpec; float fileBeatRms = -100, fileBeatPeak = -100; double fileBeatSec = 0; juce::String fileBeatName;
    sauce::KeyDetector::Estimate fileKey;
    double assistBeatPow = 0, assistVocalPow = 0; float assistBeatPeak = 0; int64_t assistBeatN = 0, assistVocalN = 0;

    mutable juce::CriticalSection lock;     // guards display + match + coach data between this thread and the GUI
    juce::AudioFormatManager formats;
};
