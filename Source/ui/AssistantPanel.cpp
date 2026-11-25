#include "AssistantPanel.h"

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
    const bool enough = lastIn.hasBeat || lastIn.hasVocal || lastIn.bpm > 0;
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
    g.setFont (SauceFonts::bold (26.0f)); g.setColour (SauceColours::text);
    g.drawText ("SAUCE ASSISTANT", 76, 22, 400, 36, Justification::centredLeft);
    g.setFont (SauceFonts::regular (14.0f)); g.setColour (SauceColours::textDim);
    g.drawText ("Tell it the sound you want, play your beat, and it sets up a chain that sits your vocal in this mix.", 34, 58, 900, 22, Justification::centredLeft);

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

    stepTitle (2, "Play your beat (and sing if you like)", 34, 324);
    g.setFont (SauceFonts::regular (14.0f));
    auto line = [&] (float y, const String& label, const String& value, bool ok)
    {
        g.setColour (ok ? SauceColours::neonHi : SauceColours::textDim); g.fillEllipse (40, y + 7, 8, 8);
        g.setColour (SauceColours::textDim); g.drawText (label, 56, (int) y, 110, 22, Justification::centredLeft);
        g.setColour (SauceColours::text); g.drawText (value, 166, (int) y, 280, 22, Justification::centredLeft);
    };
    static const char* keys[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const bool rx = proc.analysis.isReceivingSidechain() && (int) proc.reader.get (ParamIDs::scRole) == 0;
    line (410, "Beat", lastIn.beatSeconds > 0 ? String (lastIn.beatSeconds, 1) + " s heard" : (rx ? (proc.analysis.assistantIsListening() ? "receiving - press play" : "receiving - press Start") : "not routed to the sidechain"), lastIn.hasBeat);
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
                          "2. Route your beat to the sidechain, press Start listening and play 10-20 seconds of the song.\n"
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
}

void AssistantPanel::resized()
{
    genreBox.setBounds (34, 152, 420, 34);
    artistBox.setBounds (34, 212, 420, 34);
    roleBox.setBounds (34, 272, 420, 34);
    listenBtn.setBounds (34, 360, 200, 40);
    routeBtn.setBounds (244, 360, 210, 40);
    const int rx = 490, rw = getWidth() - 520;
    ab.setBounds (rx + rw - 200, 98, 200, 30);
    const int bw = (rw - 4 * 8) / 5;
    for (int i = 0; i < feedback.size(); ++i) feedback[i]->setBounds (rx + (i % 5) * (bw + 8), 510 + (i / 5) * 42, bw, 36);
    applyBtn.setBounds (34, getHeight() - 76, 420, 50);
    closeBtn.setBounds (getWidth() - 140, 24, 106, 34);
}
