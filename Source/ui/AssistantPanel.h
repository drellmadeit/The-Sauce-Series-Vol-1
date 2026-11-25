#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

/** The Sauce Assistant window: target -> listen -> apply, then A/B and feedback. */
class AssistantPanel : public juce::Component, private juce::Timer
{
public:
    explicit AssistantPanel (SauceProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void open();
    std::function<void()> onClose, onRoutingHelp;

private:
    void timerCallback() override { refreshStatus(); }
    void refreshStatus();
    void fillArtists();
    SauceAssistant::Target target() const;

    SauceProcessor& proc;
    juce::ComboBox genreBox, artistBox, roleBox;
    juce::StringArray artistsShown;
    juce::TextButton listenBtn { "Start listening" }, applyBtn { "Make it sit in the mix" }, closeBtn { "Close" }, routeBtn { "How to route the beat" };
    Segmented ab { "Before", "After" };
    juce::OwnedArray<juce::TextButton> feedback;
    sauce::AssistantInput lastIn;
    juce::String guess;
};
