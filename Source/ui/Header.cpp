#include "Header.h"

using namespace juce;

//==============================================================================
PresetSelectors::PresetSelectors (SauceProcessor& p) : proc (p)
{
    genre.setTextWhenNothingSelected ("Select genre");
    artist.setTextWhenNothingSelected ("Select artist");
    preset.setTextWhenNothingSelected ("Select preset");
    genre.setTooltip ("Optional filter: show presets for one genre");
    artist.setTooltip ("Optional filter: show artist-inspired (unofficial) starting points");
    preset.setTooltip ("Load a preset. Factory presets are protected; use Save As to keep your edits.");

    genre.onChange = [this]
    {
        const int id = genre.getSelectedId();
        if (id == 0) return;
        const String g = id == 1000 ? String() : PresetTags::genres[id - 1];
        // keep the artist only if they belong to the new genre
        const bool keepArtist = proc.artistFilter.isNotEmpty() && (g.isEmpty() || PresetTags::artistInGenre (proc.artistFilter, g));
        proc.setFilters (g, keepArtist ? proc.artistFilter : String());
        if (g.isEmpty() || keepArtist) return;
        // picking a genre loads that genre's main sound straight away
        int idx = proc.presets.indexOf (g + " - Lead");
        if (idx < 0) for (int i = 0; i < (int) proc.presets.all().size(); ++i)
            if (proc.presets.all()[(size_t) i].factory && proc.presets.all()[(size_t) i].genre == g && proc.presets.all()[(size_t) i].artist.isEmpty()) { idx = i; break; }
        if (idx >= 0) proc.presets.load (idx);
    };
    artist.onChange = [this]
    {
        const int id = artist.getSelectedId();
        if (id == 0) return;
        if (id == 1000) { proc.setFilters (proc.genreFilter, {}); return; }
        if (id - 1 >= shownArtists.size()) return;
        const String a = shownArtists[id - 1];
        proc.setFilters (proc.genreFilter, a);
        const int idx = proc.presets.indexOf (a + "-Inspired (Unofficial)");     // picking an artist loads their sound
        if (idx >= 0) proc.presets.load (idx);
    };
    preset.onChange = [this]
    {
        const int id = preset.getSelectedId();
        if (id <= 0) return;
        const auto& all = proc.presets.all();
        if (id - 1 < (int) all.size() && ! (all[(size_t) id - 1].name == proc.presetName && ! proc.presetDirty))
            proc.presets.load (id - 1);
        else refresh();
    };
    proc.stateChanged.addChangeListener (this);
    proc.presets.addChangeListener (this);
    refresh();
}

PresetSelectors::~PresetSelectors()
{
    proc.stateChanged.removeChangeListener (this);
    proc.presets.removeChangeListener (this);
}

void PresetSelectors::refresh()
{
    genre.clear (dontSendNotification);
    for (int i = 0; i < PresetTags::genres.size(); ++i) genre.addItem (PresetTags::genres[i], i + 1);
    genre.addSeparator(); genre.addItem ("Show all genres", 1000);
    const int gi = PresetTags::genres.indexOf (proc.genreFilter);
    if (gi >= 0) genre.setSelectedId (gi + 1, dontSendNotification); else genre.setSelectedId (0, dontSendNotification);

    artist.clear (dontSendNotification);
    shownArtists = PresetTags::artistsForGenre (proc.genreFilter);         // only artists that fit the chosen genre
    if (proc.genreFilter.isNotEmpty()) artist.addSectionHeading (proc.genreFilter + " artists (unofficial)");
    for (int i = 0; i < shownArtists.size(); ++i) artist.addItem (shownArtists[i], i + 1);
    artist.addSeparator(); artist.addItem ("Clear artist", 1000);
    const int ai = shownArtists.indexOf (proc.artistFilter);
    if (ai >= 0) artist.setSelectedId (ai + 1, dontSendNotification); else artist.setSelectedId (0, dontSendNotification);

    preset.clear (dontSendNotification);
    const auto& all = proc.presets.all();
    const auto idx = proc.presets.filtered (proc.genreFilter, proc.artistFilter);
    auto section = [&] (const String& title, auto pred)
    {
        bool any = false;
        for (int i : idx)
            if (pred (all[(size_t) i]))
            {
                if (! any) { preset.addSectionHeading (title); any = true; }
                preset.addItem (all[(size_t) i].name, i + 1);
            }
    };
    // With no genre chosen, each genre is a submenu (the full bank is ~1000 presets); with a genre chosen, it's listed flat.
    auto* root = preset.getRootMenu();
    if (proc.genreFilter.isEmpty() && proc.artistFilter.isEmpty())
    {
        preset.addSectionHeading ("Genres");
        for (auto& g : PresetTags::genres)
        {
            PopupMenu sm;
            for (int i : idx) { const auto& p = all[(size_t) i]; if (p.factory && p.artist.isEmpty() && p.genre == g) sm.addItem (i + 1, p.name); }
            if (sm.getNumItems() > 0) root->addSubMenu (g, sm);
        }
    }
    else
        for (auto& g : PresetTags::genres)
            section (g, [g] (const PresetInfo& p) { return p.factory && p.artist.isEmpty() && p.genre == g; });
    if (proc.artistFilter.isNotEmpty())
        section (proc.artistFilter + "-Inspired (Unofficial)", [] (const PresetInfo& p) { return p.factory && p.artist.isNotEmpty(); });
    else
    {
        // one submenu per artist: Lead, Hook, Ad-libs, Backgrounds
        StringArray artists;
        for (int i : idx) { const auto& p = all[(size_t) i]; if (p.factory && p.artist.isNotEmpty()) artists.addIfNotAlreadyThere (p.artist); }
        artists.sortNatural();
        if (! artists.isEmpty()) preset.addSectionHeading ("Artist-Inspired (Unofficial)");
        for (auto& a : artists)
        {
            PopupMenu sm;
            for (int i : idx) { const auto& p = all[(size_t) i]; if (p.factory && p.artist == a) sm.addItem (i + 1, p.name); }
            root->addSubMenu (a, sm);
        }
    }
    section ("My Presets", [] (const PresetInfo& p) { return ! p.factory; });
    if (idx.empty()) { preset.addItem ("No presets match these filters", -1); preset.setItemEnabled (-1, false); }

    const int cur = proc.presets.indexOf (proc.presetName);
    if (! proc.presetDirty && cur >= 0 && preset.indexOfItemId (cur + 1) >= 0) preset.setSelectedId (cur + 1, dontSendNotification);
    else preset.setText (proc.presetName + (proc.presetDirty ? "  (edited)" : ""), dontSendNotification);
}

//==============================================================================
void Wordmark::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    // waveform glyph
    const auto iconArea = Rectangle<float> (b.getX() + 2, b.getCentreY() - 26, 50, 52);
    auto bars = SauceDraw::icon ("brand", iconArea);
    SauceDraw::glowPath (g, bars, SauceColours::neonHi, 4.0f, 12);

    const String text ("The Sauce Suite");
    auto font = SauceFonts::brand (b.getHeight() * 0.7f);
    const float avail = b.getRight() - (iconArea.getRight() + 14.0f) - 8.0f;
    const float tw = GlyphArrangement::getStringWidth (font, text);
    if (tw > avail) font = SauceFonts::brand (font.getHeight() * avail / tw);
    GlyphArrangement ga;
    const float baseline = b.getCentreY() + font.getAscent() * 0.38f;
    ga.addLineOfText (font, text, iconArea.getRight() + 14.0f, baseline);
    Path textPath; ga.createPath (textPath);

    // drips hang from the bottom of selected letters
    Path drips;
    const int dripGlyphs[] = { 0, 1, 4, 6, 9, 11, 13 };
    const float lens[] = { 16, 10, 22, 13, 18, 11, 15 };
    int k = 0;
    for (int gi : dripGlyphs)
    {
        if (gi >= ga.getNumGlyphs()) break;
        const auto gb = ga.getBoundingBox (gi, 1, true);
        const float dx = gb.getX() + gb.getWidth() * (k % 2 == 0 ? 0.38f : 0.58f) - (baseline - gb.getY()) * 0.0f;
        const float w = jmax (4.0f, font.getHeight() * 0.075f), len = lens[k] * font.getHeight() / 62.0f;
        drips.addRoundedRectangle (dx - w * 0.5f, baseline - w, w, len, w * 0.5f);
        drips.addEllipse (dx - w * 0.75f, baseline - w + len - w * 0.6f, w * 1.5f, w * 1.6f);
        ++k;
    }
    Path all (textPath); all.addPath (drips);
    DropShadow (SauceColours::neon.withAlpha (0.95f), 18, {}).drawForPath (g, all);
    DropShadow (SauceColours::neonHi.withAlpha (0.8f), 6, {}).drawForPath (g, all);
    g.setGradientFill (ColourGradient (Colours::white, 0, b.getY(), Colour (0xffd9ecff), 0, b.getBottom(), false));
    g.fillPath (all);
}

//==============================================================================
HeaderBar::HeaderBar (SauceProcessor& p) : selectors (p), proc (p)
{
    addAndMakeVisible (wordmark);
    for (auto* c : { &selectors.genre, &selectors.artist, &selectors.preset }) addAndMakeVisible (c);
    selectors.preset.getProperties().set ("noArrow", true);
    addAndMakeVisible (pencil);
    pencil.setTooltip ("Rename / save the current sound as your own preset");
    pencil.onClick = [this] { if (onSaveAs) onSaveAs(); };
    saveAs.getProperties().set ("primary", true);
    saveAs.getProperties().set ("fontScale", 0.5f);
    saveAs.setTooltip ("Save the current sound as a new preset (factory presets are never overwritten)");
    saveAs.onClick = [this] { if (onSaveAs) onSaveAs(); };
    addAndMakeVisible (saveAs);
    assistantBtn.getProperties().set ("primary", true);
    assistantBtn.getProperties().set ("fontScale", 0.36f);
    assistantBtn.setTooltip ("Sauce Assistant: listens to your beat and sets up a vocal chain that sits in the mix");
    assistantBtn.onClick = [this] { if (onAssistant) onAssistant(); };
    addAndMakeVisible (assistantBtn);
    undoBtn.setTooltip ("Undo"); redoBtn.setTooltip ("Redo");
    undoBtn.onClick = [this] { proc.undo.undo(); };
    redoBtn.onClick = [this] { proc.undo.redo(); };
    gear.setTooltip ("Settings"); info.setTooltip ("About, routing help and credits");
    gear.onClick = [this] { if (onSettings) onSettings(); };
    info.onClick = [this] { if (onInfo) onInfo(); };
    for (auto* c : { (Component*) &undoBtn, (Component*) &redoBtn, (Component*) &gear, (Component*) &info }) addAndMakeVisible (c);
}

void HeaderBar::paint (Graphics& g)
{
    g.setColour (SauceColours::border);
    g.drawVerticalLine (414, 14, 70);
    // (the Sauce Assistant button sits where the tagline used to be)
    g.setFont (SauceFonts::medium (17.0f));
    g.setColour (SauceColours::text);
    g.drawText ("GENRE", 648, 2, 120, 24, Justification::centredLeft);
    g.drawText ("ARTIST", 838, 2, 120, 24, Justification::centredLeft);
    g.drawText ("PRESET", 1036, 2, 120, 24, Justification::centredLeft);
    // undo/redo pill
    auto pill = Rectangle<float> (1473, 27, 104, 38);
    g.setColour (SauceColours::field); g.fillRoundedRectangle (pill, 8);
    g.setColour (Colour (0xff5b6472)); g.drawRoundedRectangle (pill, 8, 1.2f);
    g.drawVerticalLine (1525, 33, 59);
}

void HeaderBar::resized()
{
    wordmark.setBounds (14, 0, 392, 82);
    selectors.genre.setBounds (642, 29, 170, 36);
    selectors.artist.setBounds (831, 29, 166, 36);
    selectors.preset.setBounds (1030, 29, 272, 36);
    pencil.setBounds (1268, 33, 28, 28);
    saveAs.setBounds (1322, 25, 128, 42);
    assistantBtn.setBounds (430, 22, 190, 46);
    undoBtn.setBounds (1478, 30, 44, 32);
    redoBtn.setBounds (1529, 30, 44, 32);
    gear.setBounds (1602, 25, 44, 44);
    info.setBounds (1657, 25, 44, 44);
    undoBtn.setAlpha (1.0f);
}
