#pragma once
#include "PluginProcessor.h"
#include "ui/Header.h"
#include "ui/EqGraph.h"
#include "ui/ModulePages.h"
#include "ui/FxPanels.h"
#include "ui/SidePanels.h"
#include "ui/UiState.h"
#include "ui/AssistantPanel.h"

class TabButton : public juce::Button
{
public:
    TabButton (const juce::String& label, const juce::String& icon, const juce::String& powerParam, SauceProcessor& p, bool caret = false)
        : juce::Button (label), iconName (icon), param (powerParam), proc (p), hasCaret (caret) {}
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    std::function<void()> onCaret;
    std::function<void (const juce::MouseEvent&)> onDragMove;
    std::function<void()> onDragEnd;
    bool movable = false, dragging = false;
private:
    juce::String iconName, param; SauceProcessor& proc; bool hasCaret;
};

/** Banner that confirms what a preset just changed (fades out by itself). */
class PresetToast : public juce::Component
{
public:
    void show (const juce::String& title, const juce::String& detail) { t = title; d = detail; alpha = 1.0f; shownAt = juce::Time::getMillisecondCounterHiRes(); setVisible (true); repaint(); }
    void tick()
    {
        if (! isVisible()) return;
        const double age = juce::Time::getMillisecondCounterHiRes() - shownAt;
        alpha = age < 3800 ? 1.0f : juce::jmax (0.0f, 1.0f - (float) (age - 3800) / 700.0f);
        if (alpha <= 0.0f) setVisible (false); else repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (6);
        g.setOpacity (alpha);
        juce::Path p; p.addRoundedRectangle (r, 12);
        juce::DropShadow (SauceColours::neon.withAlpha (0.8f * alpha), 16, {}).drawForPath (g, p);
        g.setColour (juce::Colour (0xf2080c12).withMultipliedAlpha (alpha)); g.fillPath (p);
        g.setColour (SauceColours::neonHi.withMultipliedAlpha (alpha)); g.strokePath (p, juce::PathStrokeType (1.6f));
        g.setFont (SauceFonts::bold (18.0f)); g.setColour (juce::Colours::white.withMultipliedAlpha (alpha));
        g.drawText (t, r.reduced (18, 6).removeFromTop (26), juce::Justification::centredLeft);
        g.setFont (SauceFonts::regular (14.0f)); g.setColour (SauceColours::text.withMultipliedAlpha (alpha * 0.9f));
        g.drawFittedText (d, r.reduced (18, 6).withTrimmedTop (28).toNearestInt(), juce::Justification::topLeft, 2);
    }
private:
    juce::String t, d; float alpha = 0; double shownAt = 0;
};

class SauceEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SauceEditor (SauceProcessor&);
    ~SauceEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseW = 1720, kBaseH = 920;
    void selectTab (int index);
    PresetSelectors& headerSelectorsForTest() { return header.selectors; }

private:
    void timerCallback() override;
    void showSaveAs();
    void showRoutingHelp();
    void showInfo();
    void showSettings();
    void setFullscreenEq (bool);
    void layoutContent();

    SauceProcessor& proc;
    SauceLookAndFeel lnf;
    UiState ui;
    juce::Component content;              // everything lives here at 1720x920 and is scaled as one unit
    juce::TooltipWindow tooltips { nullptr, 600 };

    HeaderBar header;
    juce::OwnedArray<TabButton> tabs;
    EqPage eqPage;
    GatePage gatePage; TunePage tunePage; MatchPage matchPage; CompPage compPage; FxPage fxPage; LimiterPage limPage;
    DelayPanel delay; ReverbPanel reverb;
    PresetRoutingPanel routing; VocalCoachPanel coach; MetersPanel metersPanel; BottomBar bottom;
    int current = 2; bool eqFull = false;
    // vocal chain order (drag the tabs)
    juce::String shownOrder, workingOrder; int draggingTab = -1; float dragX = 0, dragGrab = 0;
    void layoutTabs();
    static int tabForLetter (juce::juce_wchar c) { return juce::String ("GTEMCS").indexOfChar (c); }
    PresetToast toast; juce::String lastToastName; bool lastToastDirty = true;
    AssistantPanel assistantPanel;
public:
    AssistantPanel& assistantPanelForTest() { return assistantPanel; }
private:
    void showPresetToast();
    juce::Component* pageFor (int i);
    std::unique_ptr<juce::AlertWindow> dialog;
    juce::ComboBox dlgGenre, dlgArtist;
};
