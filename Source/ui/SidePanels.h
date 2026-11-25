#pragma once
#include "Header.h"
#include "UiState.h"

class PresetRoutingPanel : public juce::Component
{
public:
    explicit PresetRoutingPanel (SauceProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
    std::function<void()> onSaveAs, onRoutingHelp;
private:
    SauceProcessor& proc;
    PresetSelectors selectors;
    IconButton pencil { "pencil" };
    juce::ComboBox role; std::unique_ptr<ComboAtt> roleAtt;
    juce::TextButton help { "?" }, linkBtn { "LINK" };
    bool receiving = false, connected = false, linked = false; int roleIdx = 0, sendMode = 0; float level = -120;
};

class VocalCoachPanel : public juce::Component
{
public:
    explicit VocalCoachPanel (SauceProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick();
private:
    SauceProcessor& proc;
    sauce::VocalCoach::Message msg;
    int dots = 0, shownRole = -1;
    juce::Rectangle<float> chip;
};

class MetersPanel : public juce::Component
{
public:
    MetersPanel (SauceProcessor&, const UiState&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
private:
    SauceProcessor& proc; const UiState& ui;
    Segmented latency { "Mix", "Tracking" };
    int shownLatency = -1;
    SegMeter inL { true, 24 }, inR { true, 24 }, outL { true, 24 }, outR { true, 24 };
    Knob gain;
};

class BottomBar : public juce::Component
{
public:
    BottomBar (SauceProcessor&, const UiState&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
private:
    SauceProcessor& proc; const UiState& ui;
    juce::Slider inKnob, outKnob, dryWet;
    std::unique_ptr<SliderAtt> inAtt, outAtt, dwAtt;
    SegMeter inMeter { false, 30 }, outMeter { false, 30 };
    float inHold = -100, outHold = -100;
};
