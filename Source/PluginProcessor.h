#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "PresetManager.h"
#include "AnalysisEngine.h"
#include "SauceAssistant.h"
#include "dsp/VocalChain.h"
#include "SauceLink.h"

/** Product version: bump for every release (also CMakeLists.txt project VERSION, installer/Installer.rc, CHANGELOG.md). */
constexpr const char* kSauceVersion = "1.5.0";

#ifndef SAUCE_TEST_BUILD
 #define SAUCE_TEST_BUILD 0
#endif

/** Meter values published by the audio thread for the UI (lock-free). */
struct SauceMeters
{
    std::atomic<float> inL { 0 }, inR { 0 }, outL { 0 }, outR { 0 };
    std::atomic<float> gateGainDb { 0 }, compGrDb { 0 }, limGrDb { 0 }, deessGrDb { 0 }, popGrDb { 0 };
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
    void setBandSolo (int band) { chain.setBandSolo (band); }
    int  getBandSolo()          { return chain.eq().soloBand.load(); }
    /** Live resonance cuts and dynamic-band offsets for the EQ display (read-only floats written by the audio thread). */
    const std::array<float, sauce::ResonanceSuppressor::kBands>& resonanceCuts() { return chain.eq().res.cutDb; }
    const std::array<float, sauce::ParametricEQ::kBands>& dynamicOffsets() { return chain.eq().dynNow; }

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
    static juce::String defaultChainOrder() { return "GPTEMCDSYRL"; }  // Gate, Plosive, Tune, EQ, Match, Comp, De-Ess, Vocal FX, Delay, Reverb, Limiter
    static juce::String upgradeChainOrder (const juce::String& o);
    static bool isValidChainOrder (const juce::String& o);
    juce::String getChainOrder() const { return chainOrder; }
    bool setChainOrder (const juce::String& order, bool markEdited = true);   // message thread

    /** Auto Key: applies a stable key estimate from the analysis thread (message thread; adds no latency). */
    void applyAutoKey();
    juce::String autoKeyStatus;   // shown on the Auto Tune page

    juce::ValueTree captureFullState() const;
    void restoreFullState (const juce::ValueTree&);

    // ---- What part this track is (drives the role-aware Vocal Coach) ----
    static const juce::StringArray& roleNames() { static const juce::StringArray n { "Lead", "Hook", "Ad-libs", "Backgrounds", "Harmonies", "Doubles" }; return n; }
    void setRoleOverride (int role) { roleOverride = juce::jlimit (-1, 5, role); updateRole(); }   // -1 = Auto
    int  getRoleOverride() const { return roleOverride; }
    int  getRole() const { return currentRole.load(); }
    juce::String getRoleReason() const { return roleReason; }
    void updateRole();                                 // message thread: detect from override, track name, preset, settings

    // ---- Track Link (no-sidechain routing between Sauce Suite instances in the same DAW) ----
    enum LinkMode { LinkOff = 0, LinkSendBeat = SauceLink::Beat, LinkSendReference = SauceLink::Reference };
    void setLinkSend (int mode);                       // message thread. Sending = this track passes through untouched (0 latency)
    int  getLinkSend() const { return linkSendMode; }
    void setLinkSource (const juce::String& senderName);   // "" = none
    juce::String getLinkSource() const { return linkSourceName; }
    bool isLinkReceiving() const { return linkReceiving.load(); }
    juce::String getTrackName() const { const juce::SpinLock::ScopedLockType l (trackNameLock); return trackName; }
    juce::String linkDisplayName() const;
    void refreshLink();                                // message thread: re-finds the chosen sender by name, renames our slot
    void updateTrackProperties (const TrackProperties& props) override;

private:
    void parameterChanged (const juce::String&, float) override;

    sauce::VocalChain chain;
    std::vector<float> tapIn, tapEq, tapLead, tapOut, scMono;
    int preparedBlock = 0; double currentSr = 44100;

    std::array<float, sauce::MatchFilterbank::kBands> matchBuf[2] {};
    std::atomic<int> matchIdx { 0 };
    juce::String chainOrder { defaultChainOrder() };
    int roleOverride = -1; std::atomic<int> currentRole { 0 }; juce::String roleReason { "lead vocal (default)" };
    int linkSendMode = LinkOff, linkSendSlot = -1; juce::String linkSourceName;
    std::atomic<int> sendSlot { -1 }, receiveSlot { -1 }; std::atomic<bool> linkReceiving { false };
    SauceLink::Reader linkReader;
    juce::String trackName; mutable juce::SpinLock trackNameLock;
    std::atomic<uint64_t> chainOrderPacked { 0x987615432A0ull };   // nibble i = module at slot i (default order)
    std::atomic<bool> loadingPreset { false }, autoKeyApplying { false };
    void timerCallback() override { applyAutoKey(); assistant.updateLevel(); refreshLink(); updateRole(); }
    float scEnv = 0;
    void processChunk (juce::AudioBuffer<float>& buffer, int start, int n);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SauceProcessor)
};
