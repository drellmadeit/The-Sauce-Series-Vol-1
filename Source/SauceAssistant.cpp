#include "SauceAssistant.h"
#include "PluginProcessor.h"

using namespace juce;

sauce::AssistantInput SauceAssistant::gather() const
{
    auto in = proc.analysis.assistantSnapshot();
    in.bpm = proc.hostBpm.load();
    const auto k = proc.analysis.getKeyEstimate();
    in.keyValid = k.valid; in.key = k.key; in.minor = k.minor;
    return in;
}

String SauceAssistant::guessText (const sauce::AssistantInput& in) const
{
    if (! in.hasBeat) return "Waiting for the beat...";
    const auto g = sauce::Assistant::guessGenre (in);
    return "Sounds like " + String (g[0].genre) + " (" + String (roundToInt (g[0].confidence * 100)) + "% sure): " + String (g[0].why);
}

void SauceAssistant::applyPlan (const sauce::Plan& plan)
{
    for (auto& [id, v] : plan.sets)
        if (proc.apvts.getParameter (id) != nullptr) proc.setParam (id, v);
}

bool SauceAssistant::apply (const Target& target)
{
    const auto in = gather();
    String genre = target.genre;
    // an artist decides the genre if none was chosen (or the artist doesn't fit the chosen one)
    if (target.artist.isNotEmpty())
        for (auto& a : PresetTags::artistRoster())
            if (a.name == target.artist && (genre.isEmpty() || ! a.genres.contains (genre))) genre = a.genres[0];
    String why;
    if (genre.isEmpty())
    {
        if (in.hasBeat) { const auto g = sauce::Assistant::guessGenre (in); genre = g[0].genre; why = "Sounds like " + genre + " (" + String (roundToInt (g[0].confidence * 100)) + "% sure)"; }
        else { genre = "R&B"; why = "No beat heard yet, so I started from R&B"; }
    }
    else why = target.artist.isNotEmpty() ? "Going for a " + target.artist + "-inspired sound" : "Going for " + genre;

    // choose the starting preset
    int idx = -1;
    if (target.artist.isNotEmpty() && (target.role == sauce::Role::Lead || target.role == sauce::Role::Hook))
        idx = proc.presets.indexOf (target.artist + "-Inspired (Unofficial)");
    if (idx < 0) idx = proc.presets.indexOf (sauce::rolePresetName (genre.toStdString(), target.role));
    if (idx < 0) idx = proc.presets.indexOf (genre + " - Lead");
    if (idx < 0) return false;

    before = proc.captureFullState();
    proc.setFilters (genre, target.artist);
    proc.presets.load (idx);
    const String presetName = proc.presetName;

    auto get = [this] (const std::string& id) { return proc.reader.get (String (id)); };
    const auto plan = sauce::Assistant::tailor (in, genre.toStdString(), target.role, get);
    applyPlan (plan);
    proc.presetDirty = true;
    proc.stateChanged.sendChangeMessage();

    static const char* keys[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    headline = why;
    if (in.bpm > 0) headline << "  |  " << String (roundToInt (in.bpm)) << " BPM";
    if (in.keyValid) headline << "  |  " << keys[in.key] << (in.minor ? " minor" : " major");
    headline << "\nStarted from \"" << presetName << "\" (" << sauce::roleName (target.role) << "), then tailored it to this beat:";
    notes.clear();
    for (auto& n : plan.notes) notes.add (n);
    // level is measured on the NEW sound, so listen a little longer before setting it
    levelTargetDb = sauce::Assistant::levelTarget (genre.toStdString(), target.role);
    levelLabel = genre + " " + sauce::roleName (target.role);
    pendingLevel = proc.analysis.isReceivingSidechain() || (in.hasBeat && ! in.beatFromFile);
    levelPasses = 0; levelTotal = 0;
    if (pendingLevel)
    {
        proc.analysis.assistantStart();
        notes.add ("Keep the beat playing and sing a few lines: the vocal level against the beat gets set in a few seconds.");
    }
    else if (in.beatFromFile)
        notes.add ("Used your beat file (" + proc.analysis.beatFileName() + "). For automatic level matching, link the beat track or route it to the sidechain and play it.");
    after = proc.captureFullState();
    showingBefore = false;
    return true;
}

void SauceAssistant::showBefore (bool b)
{
    if (! after.isValid() || ! before.isValid() || b == showingBefore) return;
    if (! b) { /* keep any tweaks made while listening to "after" */ }
    else after = proc.captureFullState();
    proc.restoreFullState (b ? before : after);
    showingBefore = b;
}

bool SauceAssistant::updateLevel()
{
    if (! pendingLevel || showingBefore) return false;
    const auto in = proc.analysis.assistantSnapshot();
    if (! (in.hasBeat && in.vocalSeconds >= 3.0 && in.vocalOutRmsDb > -80 && in.beatRmsDb > -80)) return false;
    const float now = in.vocalOutRmsDb - in.beatRmsDb;
    const float delta = jlimit (-12.0f, 12.0f, levelTargetDb - now);
    if (std::abs (delta) > 0.5f)
    {
        proc.setParam (ParamIDs::outGain, jlimit (-24.0f, 24.0f, proc.reader.get (ParamIDs::outGain) + delta));
        proc.presetDirty = true;
        proc.stateChanged.sendChangeMessage();
        levelTotal += delta;
    }
    // the limiter can soak up part of a big move: check once more if this one was large
    if (++levelPasses < 3 && std::abs (delta) > 1.0f) { proc.analysis.assistantStart(); return false; }
    proc.analysis.assistantStop();
    pendingLevel = false;
    for (auto& n : notes) if (n.startsWith ("Keep the beat playing")) { notes.removeString (n); break; }
    notes.add (String::formatted ("Level-matched to the beat: vocal %s %.1f dB, now sitting about %+.1f dB against it (the %s target).",
                                  levelTotal >= 0 ? "up" : "down", std::abs (levelTotal), levelTargetDb, levelLabel.toRawUTF8()));
    after = proc.captureFullState();
    return true;
}

void SauceAssistant::nudge (sauce::Assistant::Nudge n)
{
    if (showingBefore) showBefore (false);
    auto get = [this] (const std::string& id) { return proc.reader.get (String (id)); };
    const auto plan = sauce::Assistant::nudge (n, get);
    applyPlan (plan);
    proc.presetDirty = true;
    proc.stateChanged.sendChangeMessage();
    for (auto& s : plan.notes) notes.add (s);
    if (after.isValid()) after = proc.captureFullState();
}
