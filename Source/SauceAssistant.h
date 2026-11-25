#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Assistant.h"

class SauceProcessor;

/** Runs the Sauce Assistant on the message thread: picks a preset for the target sound,
    tailors it to the beat that was measured, explains what it did, and supports A/B + feedback. */
class SauceAssistant
{
public:
    struct Target { juce::String genre, artist; sauce::Role role = sauce::Role::Lead; };   // empty genre = "match the beat"

    explicit SauceAssistant (SauceProcessor& p) : proc (p) {}

    sauce::AssistantInput gather() const;                  // measurements + DAW tempo + detected key
    juce::String guessText (const sauce::AssistantInput&) const;
    bool apply (const Target&);                            // returns false if nothing could be chosen
    void showBefore (bool before);
    bool isShowingBefore() const { return showingBefore; }
    bool hasResult() const { return after.isValid(); }
    void nudge (sauce::Assistant::Nudge);
    /** Second step after apply(): listens to the new sound for a few seconds and sets the vocal level against the beat.
        Call regularly (the editor and the processor timer do). Returns true when it made the change. */
    bool updateLevel();
    bool isMatchingLevel() const { return pendingLevel; }

    juce::String headline;
    juce::StringArray notes;

private:
    SauceProcessor& proc;
    juce::ValueTree before, after;
    bool showingBefore = false, pendingLevel = false;
    float levelTargetDb = 0, levelTotal = 0; int levelPasses = 0; juce::String levelLabel;
    void applyPlan (const sauce::Plan&);
};
