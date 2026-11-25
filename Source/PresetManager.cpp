#include "PresetManager.h"
#include "PluginProcessor.h"

using namespace juce;

PresetManager::PresetManager (SauceProcessor& p) : proc (p)
{
    factory = makeFactoryPresets();
    for (auto& f : factory) f.factory = true;
    refresh();
}

File PresetManager::userFolder()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
             .getChildFile ("Drellmadeit").getChildFile ("The Sauce Suite").getChildFile ("Presets");
}

void PresetManager::refresh()
{
    presets = factory;
    const auto folder = overrideFolder != File() ? overrideFolder : userFolder();
    std::vector<PresetInfo> user;
    for (auto& f : folder.findChildFiles (File::findFiles, false, "*.saucepreset"))
    {
        if (auto xml = XmlDocument::parse (f))
        {
            if (! xml->hasTagName ("SaucePreset")) continue;
            PresetInfo pi; pi.name = xml->getStringAttribute ("name", f.getFileNameWithoutExtension());
            pi.genre = xml->getStringAttribute ("genre"); pi.artist = xml->getStringAttribute ("artist"); pi.file = f;
            user.push_back (pi);
        }
    }
    std::sort (user.begin(), user.end(), [] (auto& a, auto& b) { return a.name.compareNatural (b.name) < 0; });
    presets.insert (presets.end(), user.begin(), user.end());
    sendChangeMessage();
}

std::vector<int> PresetManager::filtered (const String& genre, const String& artist) const
{
    std::vector<int> r;
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        const auto& p = presets[(size_t) i];
        if (genre.isNotEmpty() && p.genre != genre && ! (p.artist.isNotEmpty() && PresetTags::artistInGenre (p.artist, genre))) continue;
        if (artist.isNotEmpty() && p.artist != artist) continue;
        r.push_back (i);
    }
    return r;
}

int PresetManager::indexOf (const String& name) const
{
    for (int i = 0; i < (int) presets.size(); ++i) if (presets[(size_t) i].name == name) return i;
    return -1;
}

bool PresetManager::isFactoryName (const String& name) const
{
    for (auto& f : factory) if (f.name.equalsIgnoreCase (name.trim())) return true;
    return false;
}

bool PresetManager::load (int index)
{
    if (index < 0 || index >= (int) presets.size()) return false;
    const auto p = presets[(size_t) index];
    std::vector<float> matchRaw;

    if (! p.factory)
    {
        auto xml = XmlDocument::parse (p.file);
        if (xml == nullptr) return false;
        proc.markLoading (true);
        proc.resetAllParametersToDefault();
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        {
            const auto id = e->getStringAttribute ("id");
            if (ParamIDs::isWorkflowSetting (id)) continue;
            if (proc.apvts.getParameter (id) != nullptr) proc.setParam (id, (float) e->getDoubleAttribute ("value"));
        }
        proc.setChainOrder (xml->getStringAttribute ("chainOrder", SauceProcessor::defaultChainOrder()), false);
        if (auto* m = xml->getChildByName ("MATCH"))
            for (auto& t : StringArray::fromTokens (m->getStringAttribute ("raw"), " ", ""))
                if (t.isNotEmpty()) matchRaw.push_back (t.getFloatValue());
    }
    else
    {
        proc.markLoading (true);
        proc.resetAllParametersToDefault();
        for (auto& [id, v] : p.values) proc.setParam (id, v);
        proc.setChainOrder (p.chainOrder.isNotEmpty() ? p.chainOrder : SauceProcessor::defaultChainOrder(), false);
    }
    proc.analysis.setAppliedRaw (matchRaw);
    proc.presetName = p.name;
    proc.presetDirty = false;
    proc.markLoading (false);
    proc.undo.clearUndoHistory();
    proc.stateChanged.sendChangeMessage();
    return true;
}

bool PresetManager::saveAs (String name, const String& genre, const String& artist, String& savedName, String& error)
{
    name = name.trim();
    if (name.isEmpty()) { error = "Give your preset a name."; return false; }
    if (isFactoryName (name)) name << " (My Version)";            // factory originals are never overwritten

    const auto folder = overrideFolder != File() ? overrideFolder : userFolder();
    if (! folder.createDirectory()) { error = "Couldn't create the presets folder:\n" + folder.getFullPathName(); return false; }
    const auto file = folder.getChildFile (File::createLegalFileName (name) + ".saucepreset");

    XmlElement root ("SaucePreset");
    root.setAttribute ("name", name); root.setAttribute ("genre", genre); root.setAttribute ("artist", artist);
    root.setAttribute ("version", 1);
    root.setAttribute ("chainOrder", proc.getChainOrder());
    for (auto* param : proc.getParameters())
        if (auto* r = dynamic_cast<RangedAudioParameter*> (param))
        {
            const auto id = r->getParameterID();
            if (ParamIDs::isWorkflowSetting (id)) continue;
            auto* e = root.createNewChildElement ("PARAM");
            e->setAttribute ("id", id);
            e->setAttribute ("value", (double) r->convertFrom0to1 (r->getValue()));
        }
    String raw; for (float v : proc.analysis.getAppliedRaw()) raw << String (v, 3) << ' ';
    root.createNewChildElement ("MATCH")->setAttribute ("raw", raw.trim());

    if (! root.writeTo (file)) { error = "Couldn't write " + file.getFullPathName(); return false; }
    refresh();
    proc.presetName = name; proc.presetDirty = false; savedName = name;
    proc.stateChanged.sendChangeMessage();
    return true;
}

bool PresetManager::deleteUserPreset (int index)
{
    if (index < 0 || index >= (int) presets.size() || presets[(size_t) index].factory) return false;
    const bool ok = presets[(size_t) index].file.deleteFile();
    refresh();
    return ok;
}
