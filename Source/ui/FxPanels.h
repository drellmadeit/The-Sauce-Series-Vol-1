#pragma once
#include "Widgets.h"
#include "UiState.h"

/** White-glow ducking box: knob + live meter showing how far the return is currently pulled down. */
class DuckBox : public juce::Component
{
public:
    DuckBox (juce::AudioProcessorValueTreeState& s, const juce::String& id) : knob (s, id, "DUCKING", Knob::Big, true) { addAndMakeVisible (knob); }
    void paint (juce::Graphics& g) override;
    void resized() override { knob.setBounds (getLocalBounds().reduced (10, 6).withTrimmedBottom (12)); }
    void setDuckDb (float d) { if (std::abs (d - duckDb) > 0.1f) { duckDb = d; repaint(); } }
    Knob knob;
private:
    float duckDb = 0;
};

class DelayPanel : public juce::Component
{
public:
    DelayPanel (SauceProcessor&, const UiState&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
private:
    SauceProcessor& proc; const UiState& ui;
    PowerButton power;
    juce::ComboBox sync;
    Segmented mode { "Stereo", "Ping-Pong" };
    Knob time, lowCut, highCut, feedback, mix;
    DuckBox duck;
    std::array<float, 24> wave {};
    void syncFromParams();
};

class ReverbPanel : public juce::Component
{
public:
    ReverbPanel (SauceProcessor&, const UiState&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
private:
    SauceProcessor& proc; const UiState& ui;
    PowerButton power;
    juce::ComboBox type; std::unique_ptr<ComboAtt> typeAtt;
    Knob preDelay, damping, decay, mix;
    DuckBox duck;
    float lastDecay = -1, lastPre = -1, lastLevel = 0;
};
