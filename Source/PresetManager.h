#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class SauceProcessor;

struct PresetInfo
{
    juce::String name, genre, artist;
    bool factory = false;
    juce::File file;                                   // user presets only
    std::map<juce::String, float> values;              // factory presets: real-unit overrides on top of defaults
    juce::String chainOrder;                           // e.g. "GTECMS" (empty = default order)
};

namespace PresetTags
{
    inline const juce::StringArray genres  { "Rap", "Melodic Rap", "Trap", "Drill", "Trap-Soul", "R&B", "Boom Bap", "Pop", "Afrobeats",
                                             "Dancehall", "Latin / Reggaeton", "Gospel", "Country", "Rock", "Indie / Alt", "EDM", "Lo-Fi",
                                             "Podcast / Voice", "FX & Creative" };
    /** Artists and the genres they fit (first = main genre). Defined with the presets in FactoryPresets.cpp. */
    struct ArtistDef { juce::String name; juce::StringArray genres; };
    const std::vector<ArtistDef>& artistRoster();
    juce::StringArray artistsForGenre (const juce::String& genre);   // empty genre = every artist (A-Z)
    bool artistInGenre (const juce::String& artist, const juce::String& genre);
}

std::vector<PresetInfo> makeFactoryPresets();

/** Factory presets live in code and can never be overwritten. User presets are XML files in the
    user's application-data folder. Loading, Save As, delete, and genre/artist filtering live here. */
class PresetManager : public juce::ChangeBroadcaster
{
public:
    explicit PresetManager (SauceProcessor& p);

    void refresh();                                            // rescan the user folder (message thread)
    const std::vector<PresetInfo>& all() const { return presets; }
    std::vector<int> filtered (const juce::String& genre, const juce::String& artist) const;
    int indexOf (const juce::String& name) const;

    bool load (int index);
    bool saveAs (juce::String name, const juce::String& genre, const juce::String& artist, juce::String& savedName, juce::String& error);
    bool deleteUserPreset (int index);
    bool isFactoryName (const juce::String& name) const;

    static juce::File userFolder();
    void setUserFolderForTesting (const juce::File& f) { overrideFolder = f; refresh(); }

private:
    SauceProcessor& proc;
    std::vector<PresetInfo> factory, presets;
    juce::File overrideFolder;
};
