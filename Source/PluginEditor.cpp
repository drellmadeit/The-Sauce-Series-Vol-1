#include "PluginEditor.h"

using namespace juce;

namespace
{
    const Rectangle<int> kMainPanel { 18, 163, 1348, 404 };
    const Rectangle<int> kMainPanelFull { 18, 163, 1348, 641 };
}

//==============================================================================
void TabButton::paintButton (Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (3);
    const bool sel = getToggleState();
    if (sel)
    {
        g.setColour (Colour (0xff07121f)); g.fillRoundedRectangle (r, 9);
        SauceDraw::neonRoundedRect (g, r, 9, SauceColours::neonHi, 2.6f, 14);
    }
    else
    {
        g.setGradientFill (ColourGradient (Colour (0xff181c22), 0, r.getY(), Colour (0xff0b0d10), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 9);
        g.setColour (over ? Colour (0xff6a7482) : Colour (0xff3a414c)); g.drawRoundedRectangle (r, 9, 1.3f);
    }
    // compact layout (ten tabs): icon on top, label underneath
    auto ic = SauceDraw::icon (iconName, { r.getCentreX() - 13, r.getY() + 7, 26, 22 });
    if (sel) SauceDraw::glowPath (g, ic, SauceColours::neonHi, 2.2f, 6);
    else { g.setColour (SauceColours::text); g.strokePath (ic, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded)); }
    const auto font = SauceFonts::bold (14.5f);
    g.setFont (font); g.setColour (sel ? SauceColours::neonHi : SauceColours::text);
    g.drawText (getButtonText().toUpperCase(), r.withTrimmedTop (r.getHeight() - 24).withTrimmedBottom (4), Justification::centred);
    if (hasCaret)
        g.strokePath (SauceDraw::icon ("chevron", { r.getRight() - 20, r.getY() + 12, 11, 9 }), PathStrokeType (1.8f));
    if (dragging)
    {
        g.setColour (SauceColours::neonHi.withAlpha (0.18f)); g.fillRoundedRectangle (r, 9);
    }
    // module status LED
    if (param.isNotEmpty())
    {
        const bool on = proc.reader.get (param) > 0.5f;
        g.setColour (on ? SauceColours::neonHi : Colour (0xff3a414c));
        g.fillEllipse (r.getX() + 8, r.getY() + 8, 6, 6);
    }
}

void TabButton::mouseDrag (const MouseEvent& e)
{
    if (movable && (dragging || e.getDistanceFromDragStart() > 8))
    {
        dragging = true;
        if (onDragMove) onDragMove (e);
        return;
    }
    Button::mouseDrag (e);
}

void TabButton::mouseUp (const MouseEvent& e)
{
    if (dragging)
    {
        dragging = false; setState (Button::buttonNormal);
        if (onDragEnd) onDragEnd();
        return;
    }
    if (hasCaret && e.x > getWidth() - 30 && e.y < 30 && onCaret && getLocalBounds().contains (e.getPosition())) { onCaret(); return; }
    Button::mouseUp (e);
}

//==============================================================================
SauceEditor::SauceEditor (SauceProcessor& p)
    : AudioProcessorEditor (p), proc (p), header (p),
      eqPage (p), gatePage (p, ui), tunePage (p, ui), matchPage (p, ui), compPage (p, ui), fxPage (p, ui), limPage (p, ui), deessPage (p, ui), popPage (p, ui),
      delay (p, ui), reverb (p, ui), routing (p), coach (p), metersPanel (p, ui), bottom (p, ui), assistantPanel (p)
{
    setLookAndFeel (&lnf);
    tooltips.setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    content.setBounds (0, 0, kBaseW, kBaseH);

    content.addAndMakeVisible (header);
    struct T { const char* name; const char* icon; String param; bool caret; };
    const T defs[] = { { "Gate", "gate", ParamIDs::gateOn, false }, { "Auto Tune", "tune", ParamIDs::tuneOn, false }, { "EQ", "eq", ParamIDs::eqOn, false },
                       { "Match EQ", "match", ParamIDs::matchOn, false }, { "Comp", "comp", ParamIDs::compOn, false },
                       { "Vocal FX", "fx", {}, true }, { "Limiter", "limiter", ParamIDs::limOn, false },
                       { "De-Ess", "deess", ParamIDs::deessOn, false }, { "Delay", "delay", ParamIDs::dlyOn, false }, { "Reverb", "reverb", ParamIDs::revOn, false },
                       { "De-Pop", "pop", ParamIDs::popOn, false } };
    for (int i = 0; i < kNumTabs; ++i)
    {
        auto* t = tabs.add (new TabButton (defs[i].name, defs[i].icon, defs[i].param, p, defs[i].caret));
        t->setRadioGroupId (77);
        t->onClick = [this, i] { selectTab (i); };
        content.addAndMakeVisible (t);
        t->movable = true;
        t->setTooltip ("Click to open. Drag left/right to move this module in the vocal chain: the signal goes through the tabs from left to right.");
        t->onDragMove = [this, i] (const MouseEvent& e)
        {
            auto* tb = tabs[i];
            const auto pos = e.getEventRelativeTo (&content).position;
            if (draggingTab != i) { draggingTab = i; workingOrder = proc.getChainOrder(); dragGrab = e.getMouseDownX(); tb->toFront (false); }
            dragX = pos.x - dragGrab;
            const int target = jlimit (0, kNumTabs - 1, roundToInt ((dragX - (float) kTabX0) / (float) kTabSlot));
            const auto letter = kTabLetters[i];
            String o = workingOrder.removeCharacters (String::charToString (letter));
            workingOrder = o.substring (0, target) + String::charToString (letter) + o.substring (target);
            layoutTabs();
        };
        t->onDragEnd = [this]
        {
            draggingTab = -1;
            proc.setChainOrder (workingOrder);
            shownOrder = workingOrder;
            layoutTabs(); content.repaint();
        };
    }
    tabs[6]->setTooltip ("Limiter: drag it anywhere. At the far right (default) nothing leaves the plugin above the ceiling.");
    tabs[5]->setTooltip ("Saturation, doubler and width. Click the small arrow to switch each effect on or off. Drag to move it in the chain.");
    tabs[8]->setTooltip ("Delay (controls in the DELAY panel below). Drag to move it: e.g. put it after the reverb to echo the reverb.");
    tabs[9]->setTooltip ("Reverb (controls in the REVERB panel below). Drag to move it: e.g. before the delay so the echoes stay dry.");
    tabs[5]->onCaret = [this]
    {
        PopupMenu m;
        auto item = [this, &m] (const String& name, const String& id, int n)
        { m.addItem (n, name, true, proc.reader.get (id) > 0.5f); };
        item ("Saturation", ParamIDs::satOn, 1); item ("Doubler", ParamIDs::dblOn, 2); item ("Width / Chorus", ParamIDs::wideOn, 3);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (tabs[5]), [this] (int r)
        {
            const String ids[] = { ParamIDs::satOn, ParamIDs::dblOn, ParamIDs::wideOn };
            if (r >= 1 && r <= 3) proc.setParam (ids[r - 1], proc.reader.get (ids[r - 1]) > 0.5f ? 0.0f : 1.0f);
            selectTab (5);
        });
    };

    for (auto* c : { (Component*) &eqPage, (Component*) &gatePage, (Component*) &tunePage, (Component*) &matchPage, (Component*) &compPage,
                     (Component*) &fxPage, (Component*) &limPage, (Component*) &deessPage, (Component*) &popPage })
        content.addChildComponent (c);
    eqPage.onFullscreen = [this] (bool f) { setFullscreenEq (f); };
    for (auto* c : { (Component*) &delay, (Component*) &reverb, (Component*) &routing, (Component*) &coach, (Component*) &metersPanel, (Component*) &bottom })
        content.addAndMakeVisible (c);
    content.addChildComponent (toast);
    content.addChildComponent (assistantPanel);
    assistantPanel.onRoutingHelp = [this] { showRoutingHelp(); };
    header.onAssistant = [this] { assistantPanel.open(); };
    toast.setInterceptsMouseClicks (false, false);
    shownOrder = proc.getChainOrder(); lastToastName = proc.presetName; lastToastDirty = proc.presetDirty;

    header.onSaveAs = [this] { showSaveAs(); };
    header.onInfo = [this] { showInfo(); };
    header.onSettings = [this] { showSettings(); };
    routing.onSaveAs = [this] { showSaveAs(); };
    routing.onRoutingHelp = [this] { showRoutingHelp(); };

    layoutContent();
    selectTab (2);

    setResizable (true, true);
    setResizeLimits (kBaseW / 2, kBaseH / 2, kBaseW * 3 / 2, kBaseH * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setSize (kBaseW * 4 / 5, kBaseH * 4 / 5);
    startTimerHz (30);
}

SauceEditor::~SauceEditor()
{
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void SauceEditor::layoutContent()
{
    header.setBounds (0, 0, kBaseW, 84);
    const int x0 = 20, tw = 184, gap = 10;
    ignoreUnused (x0, tw, gap);
    layoutTabs();
    toast.setBounds (300, 172, 790, 78);
    assistantPanel.setBounds (250, 120, 1220, 700);
    const auto mp = eqFull ? kMainPanelFull : kMainPanel;
    for (int i = 0; i < kNumTabs; ++i) if (auto* pg = pageFor (i)) pg->setBounds (mp);
    delay.setBounds (18, 579, 660, 226);
    reverb.setBounds (694, 579, 672, 226);
    routing.setBounds (1384, 92, 322, 222);
    coach.setBounds (1384, 322, 322, 218);
    metersPanel.setBounds (1384, 548, 322, 258);
    bottom.setBounds (8, 818, 1704, 72);
}

void SauceEditor::layoutTabs()
{
    const String order = draggingTab >= 0 ? workingOrder : proc.getChainOrder();
    const int tw = kTabSlot - 8;
    for (int slot = 0; slot < kNumTabs; ++slot)
    {
        const int t = tabForLetter (order[slot]);
        if (t < 0) continue;
        if (t == draggingTab) tabs[t]->setBounds (jlimit (kTabX0, kTabX0 + (kNumTabs - 1) * kTabSlot, roundToInt (dragX)), 86, tw, 62);
        else tabs[t]->setBounds (kTabX0 + slot * kTabSlot, 90, tw, 62);
    }
}

void SauceEditor::showPresetToast()
{
    static const char* names[] = { "Gate", "Tune", "EQ", "Match", "Comp", "FX", "Limiter", "De-Ess", "Delay", "Reverb", "De-Pop" };
    String chain;
    for (auto c : proc.getChainOrder()) { const int t = tabForLetter (c); if (t >= 0) chain << (chain.isEmpty() ? "" : " > ") << names[t]; }
    auto on = [this] (const String& id) { return proc.reader.get (id) > 0.5f; };
    String tune = ! on (ParamIDs::tuneOn) ? String ("off")
                : proc.reader.get (ParamIDs::tuneSpeed) < 3 ? String ("hard")
                : proc.reader.get (ParamIDs::tuneSpeed) < 30 ? String ("polished") : String ("natural");
    String rev = on (ParamIDs::revOn) ? ParamChoices::revTypes[(int) proc.reader.get (ParamIDs::revType)] + " " + String (proc.reader.get (ParamIDs::revDecay), 1) + " s" : String ("off");
    String dly = on (ParamIDs::dlyOn) ? ParamChoices::divisions[(int) proc.reader.get (ParamIDs::dlyDiv)]
                                         + (proc.reader.get (ParamIDs::dlyFeel) == 1 ? " dotted" : proc.reader.get (ParamIDs::dlyFeel) == 2 ? " triplet" : "") : String ("off");
    toast.show ("Loaded: " + proc.presetName,
                "Chain: " + chain + "\n"
                "Tune: " + tune + "   |   Comp " + String (proc.reader.get (ParamIDs::compRatio), 1) + ":1   |   Reverb: " + rev + "   |   Delay: " + dly
                + "   |   Grit: " + (on (ParamIDs::satOn) ? String (roundToInt (proc.reader.get (ParamIDs::satMix))) + "%" : String ("off")));
}

Component* SauceEditor::pageFor (int i)
{
    switch (i)
    {
        case 0: return &gatePage; case 1: return &tunePage; case 2: return &eqPage; case 3: return &matchPage;
        case 4: return &compPage; case 5: return &fxPage; case 6: return &limPage; case 7: return &deessPage; case 10: return &popPage; default: return nullptr;
    }
}

void SauceEditor::selectTab (int index)
{
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState (i == index, dontSendNotification);
    if (pageFor (index) == nullptr)             // Delay / Reverb: their controls live in the panels below, so point at them
    {
        flashPanel = index; flashStart = Time::getMillisecondCounterHiRes();
        content.repaint();
        return;
    }
    current = index;
    for (int i = 0; i < kNumTabs; ++i) if (auto* pg = pageFor (i)) pg->setVisible (i == index);
    if (index != 2 && eqFull) setFullscreenEq (false);
    content.repaint();
}

void SauceEditor::setFullscreenEq (bool f)
{
    eqFull = f;
    delay.setVisible (! f); reverb.setVisible (! f);
    layoutContent();
    content.repaint();
}

void SauceEditor::paint (Graphics& g)
{
    g.addTransform (content.getTransform());
    if (! wall.isValid()) wall = SauceDraw::sprayWall (kBaseW, kBaseH);
    g.drawImageAt (wall, 0, 0);
    // main panel frame behind the current page
    const auto mp = (eqFull ? kMainPanelFull : kMainPanel).toFloat();
    SauceDraw::panel (g, mp, 12);
    // signal-flow chevrons between the chain tabs
    g.setColour (SauceColours::spray);
    for (int slot = 0; slot < kNumTabs - 1; ++slot)
    {
        const float x = (float) (kTabX0 + (slot + 1) * kTabSlot) - 4.0f, y = 121.0f;
        Path c; c.startNewSubPath (x - 2.5f, y - 5); c.lineTo (x + 2.5f, y); c.lineTo (x - 2.5f, y + 5);
        g.strokePath (c, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

void SauceEditor::paintOverChildren (Graphics& g)
{
    if (flashPanel < 0) return;
    const double age = Time::getMillisecondCounterHiRes() - flashStart;
    if (age > 1400) { flashPanel = -1; return; }
    g.addTransform (content.getTransform());
    const auto r = (flashPanel == 8 ? delay.getBounds() : reverb.getBounds()).toFloat().reduced (2);
    const float a = (float) (1.0 - age / 1400.0);
    SauceDraw::neonRoundedRect (g, r, 12, SauceColours::neonHi.withAlpha (a), 3.0f, 18);
}

void SauceEditor::resized()
{
    const float s = (float) getWidth() / (float) kBaseW;
    content.setTransform (AffineTransform::scale (s));
}

void SauceEditor::timerCallback()
{
    ui.poll (proc.meters);
    if (auto* pg = dynamic_cast<ModulePage*> (pageFor (current)); pg != nullptr && pg->isVisible()) pg->tick();
    delay.tick(); reverb.tick(); routing.tick(); coach.tick(); metersPanel.tick(); bottom.tick();
    toast.tick();
    if (flashPanel >= 0) repaint();
    if (draggingTab < 0 && proc.getChainOrder() != shownOrder) { shownOrder = proc.getChainOrder(); layoutTabs(); content.repaint(); }
    if (! proc.presetDirty && (proc.presetName != lastToastName || lastToastDirty)) { showPresetToast(); toast.toFront (false); }
    lastToastName = proc.presetName; lastToastDirty = proc.presetDirty;
    static int n = 0;
    if (++n % 10 == 0) { for (auto* t : tabs) t->repaint(); if (! Desktop::getInstance().getMainMouseSource().isDragging()) proc.undo.beginNewTransaction(); }
}

//==============================================================================
void SauceEditor::showSaveAs()
{
    dialog = std::make_unique<AlertWindow> ("Save As", "Save the current sound as your own preset. Factory presets are never overwritten.", MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel (&lnf);
    String suggestion = proc.presetName;
    if (proc.presets.isFactoryName (suggestion)) suggestion << " (My Version)";
    dialog->addTextEditor ("name", suggestion, "Preset name");
    dlgGenre.clear(); dlgArtist.clear();
    dlgGenre.addItem ("No genre", 1000); dlgGenre.addItemList (PresetTags::genres, 1);
    const auto allArtists = PresetTags::artistsForGenre ({});
    dlgArtist.addItem ("No artist", 1000); dlgArtist.addItemList (allArtists, 1);
    const auto& all = proc.presets.all(); const int ci = proc.presets.indexOf (proc.presetName);
    const String g = ci >= 0 ? all[(size_t) ci].genre : proc.genreFilter, a = ci >= 0 ? all[(size_t) ci].artist : proc.artistFilter;
    dlgGenre.setSelectedId (PresetTags::genres.indexOf (g) >= 0 ? PresetTags::genres.indexOf (g) + 1 : 1000, dontSendNotification);
    dlgArtist.setSelectedId (allArtists.indexOf (a) >= 0 ? allArtists.indexOf (a) + 1 : 1000, dontSendNotification);
    dlgGenre.setSize (300, 30); dlgArtist.setSize (300, 30);
    dialog->addCustomComponent (&dlgGenre); dialog->addCustomComponent (&dlgArtist);
    dialog->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    dialog->enterModalState (true, ModalCallbackFunction::create ([this] (int r)
    {
        if (r == 1 && dialog != nullptr)
        {
            const String name = dialog->getTextEditorContents ("name");
            const int gi = dlgGenre.getSelectedId(), ai = dlgArtist.getSelectedId();
            String saved, err;
            if (! proc.presets.saveAs (name, gi > 0 && gi < 1000 ? PresetTags::genres[gi - 1] : String(),
                                       ai > 0 && ai < 1000 ? PresetTags::artistsForGenre ({})[ai - 1] : String(), saved, err))
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Couldn't save", err);
        }
        if (dialog != nullptr) { dialog->removeCustomComponent (0); dialog->removeCustomComponent (0); }
        dialog.reset();
    }), false);
}

void SauceEditor::showRoutingHelp()
{
    AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Route your beat into The Sauce Suite",
        "The sidechain is for analysis only. It is never heard in the plugin's output.\n\n"
        "1. Insert The Sauce Suite on your vocal track.\n"
        "2. In your DAW, send the beat's track to the plugin's sidechain input:\n"
        "   - Logic Pro: plugin header > Side Chain > choose the beat track.\n"
        "   - Studio One / Cubase: enable the plugin's sidechain, then pick the beat track as its source.\n"
        "   - Reaper: route the beat track to channels 3/4 of the vocal track.\n"
        "   - FL Studio: right-click the beat mixer track's route arrow into the vocal track > 'Sidechain to this track', "
        "then map that sidechain to the plugin's second input in the wrapper's Processing settings.\n"
        "   - Ableton Live: if the device shows a Sidechain section, choose the beat track there.\n"
        "3. Set 'Sidechain Input' to Instrumental. The status line turns blue when the beat arrives.\n\n"
        "No sidechain? Everything still works: the Vocal Coach gives vocal-only feedback, and Match EQ can load a reference vocal file.");
}

void SauceEditor::showInfo()
{
    AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "The Sauce Suite " + String (kSauceVersion),
        "Vocal production suite by Drellmadeit.\n\n"
        "Chain: Input > your modules in tab order, left to right (drag the tabs to rearrange) > Dry/Wet > Output. "
        "The limiter sits last by default so nothing goes over the ceiling.\n\n"
        "Artist-inspired presets are unofficial starting points. They aren't affiliated with or endorsed by any artist, and they shape your vocal's processing, not your voice.\n\n"
        "Latency: " + String (proc.getLatencySamples()) + " samples (reported to your DAW for compensation).\n\n"
        "Built with JUCE. Fonts: Poppins (SIL Open Font License 1.1).");
}

void SauceEditor::showSettings()
{
    PopupMenu m, size;
    const int pct[] = { 60, 70, 80, 90, 100, 125, 150 };
    for (int i = 0; i < 7; ++i) size.addItem (100 + i, String (pct[i]) + "%", true, std::abs (getWidth() - kBaseW * pct[i] / 100) < 4);
    m.addSubMenu ("Interface size", size);
    m.addSeparator();
    const int ci = proc.presets.indexOf (proc.presetName);
    const bool userPreset = ci >= 0 && ! proc.presets.all()[(size_t) ci].factory;
    m.addItem (1, "Delete \"" + proc.presetName + "\"", userPreset);
    m.addItem (2, "Show presets folder");
    m.addItem (3, "Rescan presets folder");
    m.addSeparator();
    m.addItem (4, "Clear Genre / Artist filters", proc.genreFilter.isNotEmpty() || proc.artistFilter.isNotEmpty());
    m.addItem (5, "Routing help...");
    m.addItem (6, "Reset vocal chain order", proc.getChainOrder() != SauceProcessor::defaultChainOrder());
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&header), [this, ci, pct] (int r)
    {
        if (r >= 100) { setSize (kBaseW * pct[r - 100] / 100, kBaseH * pct[r - 100] / 100); return; }
        if (r == 1) proc.presets.deleteUserPreset (ci);
        if (r == 2) { auto f = PresetManager::userFolder(); f.createDirectory(); f.revealToUser(); }
        if (r == 3) proc.presets.refresh();
        if (r == 4) proc.setFilters ({}, {});
        if (r == 5) showRoutingHelp();
        if (r == 6) proc.setChainOrder (SauceProcessor::defaultChainOrder());
    });
}
