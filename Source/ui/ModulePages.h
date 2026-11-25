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
        const int tx = power ? 80 : 28;
        const int tw = (int) SauceDraw::tagText (g, titleText, (float) tx, 33.0f, 28.0f, false);
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
    Knob speed, amount, humanize, smooth;
    float viewCentre = 60.0f;
};

class MatchPage : public ModulePage, public juce::FileDragAndDropTarget
{
public:
    MatchPage (SauceProcessor&, const UiState&);
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;
    void paintOverChildren (juce::Graphics&) override;
    void paintContent (juce::Graphics&) override;
    void layoutContent() override;
    void tick() override;
private:
    juce::TextButton capMine { "Capture My Vocal" }, capRef { "Capture Reference (Sidechain / Link)" }, loadRef { "Load / Drop Reference File..." },
                     apply { "Apply Match" }, ab { "A/B" }, reset { "Reset" }, linkBtn { "Link a reference track..." };
    bool dragOver = false;
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
    Knob drive, ceiling, release;
    juce::ComboBox character; std::unique_ptr<ComboAtt> characterAtt;
};

class DeessPage : public ModulePage
{
public:
    DeessPage (SauceProcessor&, const UiState&);
    void paintContent (juce::Graphics&) override;
    void layoutContent() override;
    void tick() override { repaint(); }
private:
    Knob freq, thresh, range;
    juce::TextButton listen { "Listen" }; std::unique_ptr<ButtonAtt> listenAtt;
};

class PlosivePage : public ModulePage
{
public:
    PlosivePage (SauceProcessor&, const UiState&);
    void paintContent (juce::Graphics&) override;
    void layoutContent() override;
    void tick() override { repaint(); }
private:
    Knob sens, freq, depth;
};
