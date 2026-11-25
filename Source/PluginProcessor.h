#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "PresetManager.h"
#include "AnalysisEngine.h"
#include "SauceAssistant.h"
#include "dsp/VocalChain.h"

/** Product version: bump for every release (also CMakeLists.txt project VERSION, installer/Installer.rc, CHANGELOG.md). */
constexpr const char* kSauceVersion = "1.4.0";

#ifndef SAUCE_TEST_BUILD
 #define SAUCE_TEST_BUILD 0
#endif

/** Meter values published by the audio thread for the UI (lock-free). */
struct SauceMeters
{
    std::atomic<float> inL { 0 }, inR { 0 }, outL { 0 }, outR { 0 };
    std::atomic<float> gateGainDb { 0 }, compGrDb { 0 }, limGrDb { 0 };
    std::atomic<float> delayReturn { 0 }, reverbReturn { 0 }, delayDuckDb { 0 }, reverbDuckDb { 0 };
    std::atomic<float> tuneHz { 0 }, tuneTarget { -1 }, tuneCorrection { 0 }; std::atomic<bool> tuneVoiced { false };
    std::atomic<float> leadLevelDb { -100 };
};

class SauceProcessor : public juce::AudioProcessor,
                       private juce::AudioProcessorValueTreeState::Listener,
                       private juce::Timer
{
public:
    SauceProcessor();
    ~SauceProcessor() override;

    static constexpr const char* kName = "The Sauce Suite";

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return kName; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    // Sauce
    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;
    ParamReader reader;
    AnalysisEngine analysis;
    PresetManager presets;
    SauceAssistant assistant { *this };
    SauceMeters meters;
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> sidechainConnected { false };

    void setMatchGains (const std::array<float, sauce::MatchFilterbank::kBands>& g);
    std::array<float, sauce::MatchFilterbank::kBands> getMatchGains() const;
    void setBandSolo (int band) { chain.eq.soloBand.store (band); }
    int  getBandSolo() const    { return chain.eq.soloBand.load(); }

    // preset / browsing state (message thread)
    juce::String presetName { "R&B Vocal - Polished" };
    bool presetDirty = false;
    juce::String genreFilter, artistFilter;            // empty = "Select genre" / "Select artist"
    juce::ChangeBroadcaster stateChanged;              // header + sidebar listen to stay in sync
    void setFilters (const juce::String& genre, const juce::String& artist);
    void markLoading (bool l) { loadingPreset = l; }

    /** Restores every parameter to its default value (used before applying a preset). */
    void resetAllParametersToDefault();
    void setParam (const juce::String& id, float realValue);

    /** Vocal chain order as letters: G gate, T tune, E eq, M match eq, C compressor, S saturation. */
    static juce::String defaultChainOrder() { return "GTEMCS"; }
    static bool isValidChainOrder (const juce::String& o);
    juce::String getChainOrder() const { return chainOrder; }
    bool setChainOrder (const juce::String& order, bool markEdited = true);   // message thread

    /** Auto Key: applies a stable key estimate from the analysis thread (message thread; adds no latency). */
    void applyAutoKey();
    juce::String autoKeyStatus;   // shown on the Auto Tune page

    juce::ValueTree captureFullState() const;
    void restoreFullState (const juce::ValueTree&);

private:
    void parameterChanged (const juce::String&, float) override;

    sauce::VocalChain chain;
    std::vector<float> tapIn, tapEq, tapLead, tapOut, scMono;
    int preparedBlock = 0; double currentSr = 44100;

    std::array<float, sauce::MatchFilterbank::kBands> matchBuf[2] {};
    std::atomic<int> matchIdx { 0 };
    juce::String chainOrder { "GTEMCS" };
    std::atomic<uint32_t> chainOrderPacked { 0x543210 };
    std::atomic<bool> loadingPreset { false }, autoKeyApplying { false };
    void timerCallback() override { applyAutoKey(); assistant.updateLevel(); }
    float scEnv = 0;
    void processChunk (juce::AudioBuffer<float>& buffer, int start, int n);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SauceProcessor)
};
