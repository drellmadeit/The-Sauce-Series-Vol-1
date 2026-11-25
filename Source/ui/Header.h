#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

/** Genre / Artist / Preset selectors. Several sets can exist (header + sidebar); they all read and write
    the processor's single browsing state, so they always stay in sync. */
class PresetSelectors : private juce::ChangeListener
{
public:
    explicit PresetSelectors (SauceProcessor& p);
    ~PresetSelectors() override;
    juce::ComboBox genre, artist, preset;
    void refresh();
    juce::StringArray shownArtists;
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    SauceProcessor& proc;
};

/** The wordmark is drawn as vector text with neon glow and procedural drips (no bitmap). */
class Wordmark : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
};

class HeaderBar : public juce::Component
{
public:
    explicit HeaderBar (SauceProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onSaveAs, onSettings, onInfo, onAssistant;
    PresetSelectors selectors;

private:
    SauceProcessor& proc;
    Wordmark wordmark;
    IconButton pencil { "pencil" }, undoBtn { "undo" }, redoBtn { "redo" }, gear { "gear", true }, info { "info", true };
    juce::TextButton saveAs { "Save As..." }, assistantBtn { "SAUCE ASSISTANT" };
};
