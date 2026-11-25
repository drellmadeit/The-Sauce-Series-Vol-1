#include "AssistantPanel.h"
#include "LinkMenu.h"

using namespace juce;

static const char* kRoles[] = { "Lead vocal", "Hook / chorus", "Ad-libs", "Backgrounds", "Harmonies", "Doubles" };

AssistantPanel::AssistantPanel (SauceProcessor& p) : proc (p)
{
    genreBox.addItem ("Match the beat (auto)", 1);
    for (int i = 0; i < PresetTags::genres.size(); ++i) genreBox.addItem (PresetTags::genres[i], i + 2);
    genreBox.setSelectedId (1, dontSendNotification);
    genreBox.onChange = [this] { fillArtists(); };
    for (int i = 0; i < 6; ++i) roleBox.addItem (kRoles[i], i + 1);
    roleBox.setSelectedId (1, dontSendNotification);
    fillArtists();
    for (auto* c : { &genreBox, &artistBox, &roleBox }) addAndMakeVisible (c);

    listenBtn.setClickingTogglesState (true);
    listenBtn.getProperties().set ("fontScale", 0.42f);
    listenBtn.onClick = [this]
    {
        if (listenBtn.getToggleState()) proc.analysis.assistantStart(); else proc.analysis.assistantStop();
        refreshStatus();
    };
    applyBtn.getProperties().set ("primary", true);
    applyBtn.getProperties().set ("fontScale", 0.42f);
    applyBtn.onClick = [this]
    {
        proc.analysis.assistantStop(); listenBtn.setToggleState (false, dontSendNotification);
        if (proc.assistant.apply (target())) ab.setIndex (1);
        refreshStatus(); repaint();
    };
    closeBtn.onClick = [this] { proc.analysis.assistantStop(); listenBtn.setToggleState (false, dontSendNotification); setVisible (false); if (onClose) onClose(); };
    routeBtn.getProperties().set ("fontScale", 0.42f);
    routeBtn.onClick = [this] { if (onRoutingHelp) onRoutingHelp(); };
    linkBtn.getProperties().set ("fontScale", 0.42f);
    linkBtn.setTooltip ("Hear the beat track without sidechain routing: put The Sauce Suite on the beat track, choose Send there, then pick it here");
    linkBtn.onClick = [this] { LinkMenu::show (proc, &linkBtn, [this] { refreshStatus(); }); };
    fileBtn.getProperties().set ("fontScale", 0.42f);
    fileBtn.setTooltip ("Analyse your instrumental from a file (WAV, MP3, FLAC, AIFF, OGG). You can also drag the file onto this window.");
    fileBtn.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Choose your instrumental", File(), "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc) { if (fc.getResult().existsAsFile()) loadBeatFile (fc.getResult()); });
    };
    addAndMakeVisible (linkBtn); addAndMakeVisible (fileBtn);
    ab.onChange = [this] (int i) { proc.assistant.showBefore (i == 0); repaint(); };
    ab.setTooltip ("Compare your sound before the Assistant with what it made");
    for (auto* c : { (Component*) &listenBtn, (Component*) &applyBtn, (Component*) &closeBtn, (Component*) &routeBtn, (Component*) &ab }) addAndMakeVisible (c);

    using N = sauce::Assistant::Nudge;
    const std::pair<const char*, N> fb[] = { { "More polished", N::MorePolished }, { "More raw", N::MoreRaw }, { "Wetter", N::Wetter }, { "Drier", N::Drier },
                                             { "Brighter", N::Brighter }, { "Darker", N::Darker }, { "Vocal louder", N::Louder }, { "Vocal quieter", N::Quieter },
                                             { "Harder tune", N::HarderTune }, { "Softer tune", N::SofterTune } };
    for (auto& [label, n] : fb)
    {
        auto* b = feedback.add (new TextButton (label));
        b->getProperties().set ("fontScale", 0.42f);
        b->onClick = [this, n = n] { proc.assistant.nudge (n); ab.setIndex (1); repaint(); };
        addAndMakeVisible (b);
    }
}

bool AssistantPanel::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files) if (File (f).hasFileExtension ("wav;mp3;flac;aif;aiff;ogg")) return true;
    return false;
}

void AssistantPanel::filesDropped (const StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files) if (File (f).hasFileExtension ("wav;mp3;flac;aif;aiff;ogg")) { loadBeatFile (File (f)); break; }
    repaint();
}

void AssistantPanel::loadBeatFile (const File& f)
{
    fileStatus = "Analysing " + f.getFileName() + "...";
    repaint();
    Component::SafePointer<AssistantPanel> safe (this);
    auto* engine = &proc.analysis;
    Thread::launch ([safe, engine, f]
    {
        String err;
        const bool ok = engine->loadBeatFile (f, err);
        MessageManager::callAsync ([safe, ok, err, name = f.getFileName()]
        {
            if (safe == nullptr) return;
            safe->fileStatus = ok ? "Beat file: " + name : err;
            safe->refreshStatus();
        });
    });
}

void AssistantPanel::fillArtists()
{
    const int g = genreBox.getSelectedId();
    const String genre = g >= 2 ? PresetTags::genres[g - 2] : String();
    artistsShown = PresetTags::artistsForGenre (genre);
    artistBox.clear (dontSendNotification);
    artistBox.addItem ("Any artist", 1);
    for (int i = 0; i < artistsShown.size(); ++i) artistBox.addItem (artistsShown[i], i + 2);
    artistBox.setSelectedId (1, dontSendNotification);
}

SauceAssistant::Target AssistantPanel::target() const
{
    SauceAssistant::Target t;
    const int g = genreBox.getSelectedId(), a = artistBox.getSelectedId();
    t.genre = g >= 2 ? PresetTags::genres[g - 2] : String();
    t.artist = a >= 2 ? artistsShown[a - 2] : String();
    t.role = (sauce::Role) jlimit (0, 5, roleBox.getSelectedId() - 1);
    return t;
}

void AssistantPanel::open()
{
    setVisible (true); toFront (true);
    startTimerHz (4); refreshStatus();
}

void AssistantPanel::refreshStatus()
{
    if (! isVisible()) { stopTimer(); return; }
    proc.assistant.updateLevel();
    lastIn = proc.assistant.gather();
    guess = proc.assistant.isMatchingLevel() ? String ("Measuring your vocal against the beat to set the level...") : proc.assistant.guessText (lastIn);
    if (proc.assistant.hasResult()) ab.setIndex (proc.assistant.isShowingBefore() ? 0 : 1);
    const bool enough = lastIn.hasBeat || lastIn.hasVocal || lastIn.bpm > 0 || proc.analysis.beatFileName().isNotEmpty();
    applyBtn.setEnabled (enough);
    listenBtn.setButtonText (proc.assistant.isMatchingLevel() ? "Matching level..." : listenBtn.getToggleState() ? "Stop listening" : "Start listening");
    ab.setVisible (proc.assistant.hasResult());
    for (auto* b : feedback) b->setEnabled (proc.assistant.hasResult());
    repaint();
}

void AssistantPanel::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    { Path p; p.addRoundedRectangle (r.reduced (8), 16); DropShadow (Colours::black.withAlpha (0.8f), 30, {}).drawForPath (g, p); }
    SauceDraw::panel (g, r.reduced (8), 16);
    SauceDraw::neonRoundedRect (g, r.reduced (8), 16, SauceColours::neon, 1.6f, 10);
    g.setColour (SauceColours::neonHi); g.strokePath (SauceDraw::icon ("fx", { 34, 26, 30, 30 }), PathStrokeType (2.0f));
    SauceDraw::tagText (g, "SAUCE ASSISTANT", 76, 52, 32.0f);
    SauceDraw::splatter (g, { r.getRight() - 60, r.getBottom() - 40 }, 50, SauceColours::spray, 7);
    g.setFont (SauceFonts::regular (14.0f)); g.setColour (SauceColours::textDim);
    g.drawText ("Tell it the sound you want, play your beat, and it sets up a chain that sits your vocal in this mix.", 560, 34, 760, 22, Justification::centredLeft);

    auto stepTitle = [&] (int n, const String& t, float x, float y)
    {
        g.setColour (SauceColours::neon); g.fillEllipse (x, y + 3, 22, 22);
        g.setColour (Colours::white); g.setFont (SauceFonts::bold (14.0f)); g.drawText (String (n), Rectangle<float> (x, y + 3, 22, 22), Justification::centred);
        g.setColour (SauceColours::text); g.setFont (SauceFonts::bold (17.0f)); g.drawText (t, Rectangle<float> (x + 32, y, 400, 28), Justification::centredLeft);
    };
    stepTitle (1, "What are you going for?", 34, 96);
    g.setFont (SauceFonts::medium (14.0f)); g.setColour (SauceColours::textDim);
    g.drawText ("Genre", 34, 132, 200, 20, Justification::centredLeft);
    g.drawText ("Artist sound (unofficial)", 34, 192, 300, 20, Justification::centredLeft);
    g.drawText ("Which part are you recording?", 34, 252, 300, 20, Justification::centredLeft);

    stepTitle (2, "Play, link or drop your beat (and sing if you like)", 34, 324);
    g.setFont (SauceFonts::regular (14.0f));
    auto line = [&] (float y, const String& label, const String& value, bool ok)
    {
        g.setColour (ok ? SauceColours::neonHi : SauceColours::textDim); g.fillEllipse (40, y + 7, 8, 8);
        g.setColour (SauceColours::textDim); g.drawText (label, 56, (int) y, 110, 22, Justification::centredLeft);
        g.setColour (SauceColours::text); g.drawText (value, 166, (int) y, 280, 22, Justification::centredLeft);
    };
    static const char* keys[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const bool rx = proc.analysis.isReceivingSidechain() && (int) proc.reader.get (ParamIDs::scRole) == 0;
    const String linkNote = proc.isLinkReceiving() ? " (linked)" : "";
    const String beatLine = lastIn.beatFromFile ? fileStatus
                          : lastIn.beatSeconds > 0 ? String (lastIn.beatSeconds, 1) + " s heard" + linkNote
                          : fileStatus.isNotEmpty() ? fileStatus
                          : (rx ? (proc.analysis.assistantIsListening() ? "receiving - press play" : "receiving - press Start") + linkNote
                                : String ("drop the file, link it, or route it"));
    line (410, "Beat", beatLine, lastIn.hasBeat);
    line (434, "Vocal", lastIn.vocalSeconds > 0 ? String (lastIn.vocalSeconds, 1) + " s heard" : "optional", lastIn.hasVocal);
    line (458, "Tempo", lastIn.bpm > 0 ? String (roundToInt (lastIn.bpm)) + " BPM (from your DAW)" : "unknown", lastIn.bpm > 0);
    line (482, "Key", lastIn.keyValid ? String (keys[lastIn.key]) + (lastIn.minor ? " minor" : " major") : "listening...", lastIn.keyValid);
    g.setColour (SauceColours::neonHi); g.setFont (SauceFonts::medium (13.5f));
    g.drawFittedText (guess, Rectangle<int> (40, 508, 420, 36), Justification::topLeft, 2);

    // results
    auto res = Rectangle<float> (490, 96, r.getWidth() - 520, 420);
    stepTitle (3, "What it did", res.getX(), 96);
    auto box = res.withTrimmedTop (40).withHeight (330);
    g.setColour (Colour (0xff0a0d11)); g.fillRoundedRectangle (box, 10);
    g.setColour (SauceColours::border); g.drawRoundedRectangle (box, 10, 1.0f);
    auto tb = box.reduced (16, 12);
    if (! proc.assistant.hasResult())
    {
        g.setColour (SauceColours::textDim); g.setFont (SauceFonts::regular (14.5f));
        g.drawFittedText ("1. Pick a genre, an artist sound and the part you're recording (or leave it on \"Match the beat\").\n"
                          "2. Get the beat in: drag your beat file onto this window, OR link the beat track (no routing), OR use the sidechain. "
                          "Press Start listening and play 10-20 seconds (sing along for level matching).\n"
                          "3. Press \"Make it sit in the mix\". You can A/B it and fine-tune it with the feedback buttons.",
                          tb.toNearestInt(), Justification::topLeft, 8);
    }
    else
    {
        g.setColour (SauceColours::neonHi); g.setFont (SauceFonts::medium (14.5f));
        g.drawFittedText (proc.assistant.headline, tb.removeFromTop (44).toNearestInt(), Justification::topLeft, 2);
        g.setFont (SauceFonts::regular (13.5f));
        for (int i = 0; i < proc.assistant.notes.size() && tb.getHeight() > 20; ++i)
        {
            auto row = tb.removeFromTop (38);
            g.setColour (SauceColours::neon); g.fillEllipse (row.getX(), row.getY() + 6, 7, 7);
            g.setColour (SauceColours::text); g.drawFittedText (proc.assistant.notes[i], row.withTrimmedLeft (16).toNearestInt(), Justification::topLeft, 2);
        }
    }
    g.setColour (SauceColours::textDim); g.setFont (SauceFonts::medium (14.0f));
    g.drawText ("Fine-tune it:", (int) res.getX(), 482, 120, 22, Justification::centredLeft);
    if (dragOver)
    {
        auto d = r.reduced (24);
        g.setColour (Colour (0xe0081018)); g.fillRoundedRectangle (d, 16);
        Path p; p.addRoundedRectangle (d.reduced (10), 14);
        Path dashed; const float dl[] = { 14, 9 }; PathStrokeType (3.0f).createDashedStroke (dashed, p, dl, 2);
        g.setColour (SauceColours::neonHi); g.fillPath (dashed);
        g.setFont (SauceFonts::bold (34.0f));
        g.drawText ("DROP YOUR BEAT HERE", d, Justification::centred);
    }
}

void AssistantPanel::resized()
{
    genreBox.setBounds (34, 152, 420, 34);
    artistBox.setBounds (34, 212, 420, 34);
    roleBox.setBounds (34, 272, 420, 34);
    listenBtn.setBounds (34, 360, 140, 40);
    linkBtn.setBounds (180, 360, 132, 40);
    fileBtn.setBounds (318, 360, 136, 40);
    routeBtn.setVisible (false);
    const int rx = 490, rw = getWidth() - 520;
    ab.setBounds (rx + rw - 200, 98, 200, 30);
    const int bw = (rw - 4 * 8) / 5;
    for (int i = 0; i < feedback.size(); ++i) feedback[i]->setBounds (rx + (i % 5) * (bw + 8), 510 + (i / 5) * 42, bw, 36);
    applyBtn.setBounds (34, getHeight() - 76, 420, 50);
    closeBtn.setBounds (getWidth() - 140, 24, 106, 34);
}
