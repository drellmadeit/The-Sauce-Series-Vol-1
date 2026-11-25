#pragma once
#include "Widgets.h"
#include "UiState.h"

/** Base for the processor pages shown in the large central panel when a tab other than EQ is selected. */
class ModulePage : public juce::Component
{
public:
    ModulePage (SauceProcessor& p, const UiState& u, const juce::String& title, const juce::String& subtitle, const juce::String& powerParam)
        : proc (p), ui (u), titleText (title), subText (subtitle)
    {
        if (powerParam.isNotEmpty()) { power = std::make_unique<PowerButton> (p.apvts, powerParam); addAndMakeVisible (*power); }
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (SauceColours::neonHi); g.setFont (SauceFonts::bold (26.0f));
        const int tx = power ? 80 : 28;
        g.drawText (titleText, tx, 6, 300, 34, juce::Justification::centredLeft);
        const int tw = (int) juce::GlyphArrangement::getStringWidth (SauceFonts::bold (26.0f), titleText);
        g.setColour (SauceColours::text); g.setFont (SauceFonts::medium (17.0f));
        g.drawText (subText, tx + tw + 14, 6, 600, 34, juce::Justification::centredLeft);
        paintContent (g);
    }
    void resized() override { if (power) power->setBounds (24, 6, 44, 36); layoutContent(); }
    virtual void paintContent (juce::Graphics&) {}
    virtual void layoutContent() {}
    virtual void tick() { repaint (graphArea()); }
protected:
    juce::Rectangle<int> graphArea() const { return { 24, 56, 740, getHeight() - 72 }; }
    juce::Rectangle<int> controlArea() const { return { 790, 56, getWidth() - 810, getHeight() - 72 }; }
    void drawGraphFrame (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        g.setColour (juce::Colours::black); g.fillRoundedRectangle (r, 6);
        g.setColour (SauceColours::border); g.drawRoundedRectangle (r, 6, 1.0f);
    }
    SauceProcessor& proc; const UiState& ui;
    juce::String titleText, subText;
    std::unique_ptr<PowerButton> power;
};

class GatePage : public ModulePage
{
public:
    GatePage (SauceProcessor&, const UiState&);
    void paintContent (juce::Graphics&) override;
    void layoutContent() override;
private:
    Knob thresh, range, attack, hold, release;
};

class TunePage : public ModulePage
{
public:
    TunePage (SauceProcessor&, const UiState&);
    void tick() override { repaint(); }
    void paintContent (juce::Graphics&) override;
    void layoutContent() override;
private:
    juce::ComboBox key, scale; std::unique_ptr<ComboAtt> keyAtt, scaleAtt;
    juce::TextButton natural { "Natural" }, hard { "Hard Tune" }, autoKey { "Auto Key" };
    std::unique_ptr<ButtonAtt> autoKeyAtt;
    Knob speed, amount, humanize;
    float viewCentre = 60.0f;
};

class MatchPage : public ModulePage
{
public:
    MatchPage (SauceProcessor&, const UiState&);
    void paintContent (juce::Graphics&) override;
    void layoutContent() override;
    void tick() override;
private:
    juce::TextButton capMine { "Capture My Vocal" }, capRef { "Capture Reference (Sidechain)" }, loadRef { "Load Reference Vocal File..." },
                     apply { "Apply Match" }, ab { "A/B" }, reset { "Reset" };
    std::unique_ptr<ButtonAtt> abAtt;
    Knob strength, smooth, limit;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;
    AnalysisEngine::MatchView view;
};

class CompPage : public ModulePage
{
public:
    CompPage (SauceProcessor&, const UiState&);
    void paintContent (juce::Graphics&) override;
    void layoutContent() override;
    void tick() override { repaint(); }
private:
    Knob thresh, ratio, attack, release, makeup;
};

class FxPage : public ModulePage
{
public:
    FxPage (SauceProcessor&, const UiState&);
    void paintContent (juce::Graphics&) override;
    void layoutContent() override;
    void tick() override {}
private:
    PowerButton satOn, dblOn, wideOn;
    Knob drive, satMix, dblAmt, wideAmt;
};

class LimiterPage : public ModulePage
{
public:
    LimiterPage (SauceProcessor&, const UiState&);
    void paintContent (juce::Graphics&) override;
    void layoutContent() override;
    void tick() override { repaint(); }
private:
    Knob ceiling, release;
};
