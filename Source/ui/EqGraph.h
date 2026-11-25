#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

/** Rotary knob with label on the left and value box on the right; can be re-attached to another parameter. */
class InlineKnob : public juce::Component
{
public:
    InlineKnob (const juce::String& title)
    {
        label.setText (title, juce::dontSendNotification);
        label.setFont (SauceFonts::medium (17.0f));
        label.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (label);
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 30);
        addAndMakeVisible (slider);
    }
    void attach (juce::AudioProcessorValueTreeState& s, const juce::String& id)
    {
        att.reset();
        att = std::make_unique<SliderAtt> (s, id, slider);
        if (auto* p = s.getParameter (id)) slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }
    void resized() override
    {
        auto b = getLocalBounds();
        label.setBounds (b.removeFromLeft (52));
        slider.setBounds (b);
    }
    juce::Label label; juce::Slider slider;
private:
    std::unique_ptr<SliderAtt> att;
};

class EqPlot : public juce::Component
{
public:
    explicit EqPlot (SauceProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void updateSpectra();

    int selected = 2;
    bool showSpectrum = true;
    std::function<void (int)> onSelect;

    juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().withTrimmedLeft (66).withTrimmedBottom (40).withTrimmedTop (22).withTrimmedRight (14); }
    float xForFreq (float f) const;
    float freqForX (float x) const;
    float yForDb (float db) const;
    float dbForY (float y) const;

private:
    SauceProcessor& proc;
    std::vector<float> voc, beat; bool beatActive = false;
    int dragging = -1;
    float param (int band, const char* what) const;
    void setParam (int band, const char* what, float realValue);
    juce::Point<float> nodePos (int band) const;
    bool isShown (int b) const { return b < 6 || param (b, "on") > 0.5f; }
    juce::Path spectrumPath (const std::vector<float>&, juce::Rectangle<float>) const;
};

class EqPage : public juce::Component, private juce::Timer
{
public:
    explicit EqPage (SauceProcessor& p);
    ~EqPage() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void (bool)> onFullscreen;
    void selectBand (int b);

private:
    void timerCallback() override;
    SauceProcessor& proc;
    EqPlot plot;
    Segmented viewToggle { "Spectrum", "EQ Only" };
    IconButton soloBtn { "headphones", false, true }, fullBtn { "expand", false, true };
    std::unique_ptr<PowerButton> eqPower;
    juce::TextButton abBtn { "A/B" };
    juce::ComboBox bandBox, typeBox;
    InlineKnob freqK { "Freq" }, gainK { "Gain" }, qK { "Q" }, dynK { "Dyn" }, thrK { "Thr" };
    juce::TextButton resBtn { "RESONANCE" }, resDelta { "Delta" };
    std::unique_ptr<PowerButton> resPower; std::unique_ptr<ButtonAtt> resDeltaAtt;
    InlineKnob resDepthK { "Depth" }, resSharpK { "Sharp" }, resSelK { "Select" }, resSpeedK { "Speed" }, resLoK { "Low" }, resHiK { "High" };
    bool showRes = false;
    IconButton bandSolo { "headphones", false, true }, bandTrash { "trash" };
    std::unique_ptr<PowerButton> bandPower;
    std::unique_ptr<ComboAtt> typeAtt;
    juce::ValueTree abSlot[2]; int abIndex = 0;
    void applyAB();
};
