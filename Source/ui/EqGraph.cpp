#include "EqGraph.h"

using namespace juce;

static double displaySr (SauceProcessor& p) { return p.getSampleRate() > 0 ? p.getSampleRate() : 48000.0; }

EqPlot::EqPlot (SauceProcessor& p) : proc (p) { setRepaintsOnMouseActivity (false); }

float EqPlot::xForFreq (float f) const { auto r = plotArea(); return r.getX() + r.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); }
float EqPlot::freqForX (float x) const { auto r = plotArea(); return 20.0f * std::pow (1000.0f, jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth())); }
float EqPlot::yForDb (float db) const { auto r = plotArea(); return jmap (db, 13.0f, -13.0f, r.getY(), r.getBottom()); }
float EqPlot::dbForY (float y) const { auto r = plotArea(); return jmap (y, r.getY(), r.getBottom(), 13.0f, -13.0f); }

float EqPlot::param (int b, const char* what) const { return proc.reader.get (ParamIDs::band (b, what)); }
void EqPlot::setParam (int b, const char* what, float v)
{
    if (auto* p = proc.apvts.getParameter (ParamIDs::band (b, what))) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

Point<float> EqPlot::nodePos (int b) const
{
    const int type = (int) param (b, "type");
    const bool hasGain = type == 0 || type == 1 || type == 2;
    return { xForFreq (param (b, "freq")), yForDb (hasGain ? jlimit (-12.5f, 12.5f, param (b, "gain")) : 0.0f) };
}

void EqPlot::updateSpectra() { proc.analysis.getSpectra (voc, beat, beatActive); repaint(); }

Path EqPlot::spectrumPath (const std::vector<float>& s, Rectangle<float> r) const
{
    Path p; p.startNewSubPath (r.getX(), r.getBottom());
    for (int i = 0; i < (int) s.size(); ++i)
    {
        const float x = xForFreq (AnalysisEngine::displayFreq (i));
        const float y = jmap (jlimit (-100.0f, -12.0f, s[(size_t) i]), -100.0f, -18.0f, r.getBottom(), r.getY() + r.getHeight() * 0.12f);
        p.lineTo (x, jlimit (r.getY(), r.getBottom(), y));
    }
    p.lineTo (r.getRight(), r.getBottom()); p.closeSubPath();
    return p;
}

void EqPlot::paint (Graphics& g)
{
    const auto r = plotArea();
    g.setColour (Colours::black); g.fillRect (r);
    // grid
    g.setFont (SauceFonts::medium (15.0f));
    for (int db = -12; db <= 12; db += 3)
    {
        const float y = yForDb ((float) db);
        g.setColour (Colour (0xff161b22)); g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (SauceColours::text.withAlpha (0.85f));
        g.drawText ((db > 0 ? "+" : "") + String (db), Rectangle<float> (r.getX() - 52, y - 9, 40, 18), Justification::centredRight);
    }
    g.drawText ("dB", Rectangle<float> (r.getX() - 52, r.getY() - 24, 40, 18), Justification::centredRight);
    const float fl[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (float f : fl)
    {
        const float x = xForFreq (f);
        g.setColour (Colour (0xff161b22)); g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (SauceColours::text.withAlpha (0.85f));
        const String t = f >= 1000 ? String ((int) (f / 1000)) + " k" : String ((int) f) + (f == 20 ? " Hz" : "");
        g.drawText (t, Rectangle<float> (x - 34, r.getBottom() + 8, 68, 22), Justification::centred);
    }

    // spectra: instrumental (white) behind, vocal (neon blue) in front
    if (showSpectrum)
    {
        Graphics::ScopedSaveState ss (g); g.reduceClipRegion (r.toNearestInt());
        if (beatActive || std::any_of (beat.begin(), beat.end(), [] (float v) { return v > -99.0f; }))
        {
            auto bp = spectrumPath (beat, r);
            g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.85f), 0, r.getY() + r.getHeight() * 0.3f, Colour (0xff6c7480).withAlpha (0.35f), 0, r.getBottom(), false));
            g.fillPath (bp);
        }
        if (! voc.empty())
        {
            auto vp = spectrumPath (voc, r);
            g.setGradientFill (ColourGradient (SauceColours::neonHi.withAlpha (0.95f), 0, r.getY() + r.getHeight() * 0.3f, SauceColours::neonDeep.withAlpha (0.35f), 0, r.getBottom(), false));
            g.fillPath (vp);
            g.setColour (SauceColours::neonHi.withAlpha (0.6f)); g.strokePath (vp, PathStrokeType (1.0f));
        }
        // legend
        auto lg = Rectangle<float> (r.getRight() - 270, r.getY() + 6, 260, 22);
        g.setFont (SauceFonts::medium (15.0f));
        g.setColour (SauceColours::neon); g.fillRoundedRectangle (lg.getX() + 10, lg.getCentreY() - 6, 24, 12, 3);
        g.setColour (SauceColours::text); g.drawText ("Vocal", lg.withX (lg.getX() + 40).withWidth (60), Justification::centredLeft);
        g.setColour (beatActive ? Colours::white : Colour (0xff3a404a)); g.fillRoundedRectangle (lg.getX() + 102, lg.getCentreY() - 6, 24, 12, 3);
        g.setColour (beatActive ? SauceColours::text : SauceColours::textDim);
        g.drawText (beatActive ? "Instrumental" : "Instrumental (not routed)", lg.withX (lg.getX() + 132).withWidth (160), Justification::centredLeft);
    }

    // EQ curve
    const double sr = displaySr (proc);
    const bool eqOn = proc.reader.get (ParamIDs::eqOn) > 0.5f;
    sauce::BiquadCoefs cs[6]; bool on[6];
    for (int b = 0; b < 6; ++b)
    {
        on[b] = param (b, "on") > 0.5f;
        sauce::EqBandParams bp; bp.type = (int) param (b, "type"); bp.freq = param (b, "freq"); bp.gainDb = param (b, "gain"); bp.q = param (b, "q");
        cs[b] = sauce::ParametricEQ::designFor (bp, sr);
    }
    Path curve;
    for (float x = r.getX(); x <= r.getRight(); x += 2.0f)
    {
        const float f = freqForX (x);
        double db = 0; for (int b = 0; b < 6; ++b) if (on[b]) db += cs[b].magnitudeDb (f, sr);
        const float y = jlimit (r.getY(), r.getBottom(), yForDb ((float) db));
        if (x == r.getX()) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    {
        Graphics::ScopedSaveState ss (g); g.reduceClipRegion (r.toNearestInt().expanded (0, 2));
        SauceDraw::glowPath (g, curve, eqOn ? SauceColours::neonHi : SauceColours::textDim, 3.0f, eqOn ? 10 : 0);
    }

    // selected band marker
    const auto sp = nodePos (selected);
    {
        Path dash; dash.startNewSubPath (sp.x, r.getY()); dash.lineTo (sp.x, r.getBottom());
        Path dashed; const float d[] = { 5.0f, 4.0f };
        PathStrokeType (1.6f).createDashedStroke (dashed, dash, d, 2);
        g.setColour (Colours::white.withAlpha (0.9f)); g.fillPath (dashed);
        const float f = param (selected, "freq");
        auto box = Rectangle<float> (sp.x - 34, r.getBottom() + 6, 68, 26);
        g.setColour (Colours::black); g.fillRoundedRectangle (box, 4);
        g.setColour (Colours::white); g.drawRoundedRectangle (box, 4, 1.2f);
        g.setFont (SauceFonts::medium (14.0f));
        g.drawText (f >= 1000 ? String (f / 1000.0f, 2) + " kHz" : String (roundToInt (f)) + " Hz", box, Justification::centred);
    }

    // nodes
    for (int b = 0; b < 6; ++b)
    {
        const auto p = nodePos (b);
        const bool sel = b == selected;
        const float rad = sel ? 13.0f : 12.0f;
        Path c; c.addEllipse (p.x - rad, p.y - rad, rad * 2, rad * 2);
        if (on[b]) DropShadow ((sel ? Colours::white : SauceColours::neon).withAlpha (0.8f), 10, {}).drawForPath (g, c);
        g.setColour (sel ? Colour (0xff10141a) : (on[b] ? Colour (0xff0d2c52) : Colour (0xff1a1f27)));
        g.fillPath (c);
        g.setColour (sel ? Colours::white : (on[b] ? SauceColours::neonHi : SauceColours::textDim));
        g.strokePath (c, PathStrokeType (2.2f));
        g.setColour (on[b] ? Colours::white : SauceColours::textDim);
        g.setFont (SauceFonts::bold (15.0f));
        g.drawText (String (b + 1), Rectangle<float> (p.x - rad, p.y - rad + 1, rad * 2, rad * 2), Justification::centred);
    }
}

void EqPlot::mouseDown (const MouseEvent& e)
{
    dragging = -1; float best = 22.0f;
    for (int b = 0; b < 6; ++b) { const float d = nodePos (b).getDistanceFrom (e.position); if (d < best) { best = d; dragging = b; } }
    if (dragging >= 0)
    {
        selected = dragging; if (onSelect) onSelect (selected);
        for (auto* w : { "freq", "gain" }) if (auto* p = proc.apvts.getParameter (ParamIDs::band (dragging, w))) p->beginChangeGesture();
    }
    repaint();
}

void EqPlot::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0) return;
    auto r = plotArea();
    setParam (dragging, "freq", jlimit (20.0f, 20000.0f, freqForX (jlimit (r.getX(), r.getRight(), e.position.x))));
    const int type = (int) param (dragging, "type");
    if (type == 0 || type == 1 || type == 2) setParam (dragging, "gain", jlimit (-18.0f, 18.0f, dbForY (e.position.y)));
    repaint();
}

void EqPlot::mouseUp (const MouseEvent&)
{
    if (dragging >= 0) for (auto* w : { "freq", "gain" }) if (auto* p = proc.apvts.getParameter (ParamIDs::band (dragging, w))) p->endChangeGesture();
    dragging = -1;
}

void EqPlot::mouseDoubleClick (const MouseEvent& e)
{
    for (int b = 0; b < 6; ++b)
        if (nodePos (b).getDistanceFrom (e.position) < 18.0f) { setParam (b, "on", param (b, "on") > 0.5f ? 0.0f : 1.0f); repaint(); return; }
}

void EqPlot::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    const float q = param (selected, "q");
    setParam (selected, "q", jlimit (0.1f, 18.0f, q * std::pow (1.25f, w.deltaY * 4.0f)));
    repaint();
}

//==============================================================================
EqPage::EqPage (SauceProcessor& p) : proc (p), plot (p)
{
    addAndMakeVisible (plot);
    plot.onSelect = [this] (int b) { selectBand (b); };
    viewToggle.onChange = [this] (int i) { plot.showSpectrum = i == 0; plot.repaint(); };
    addAndMakeVisible (viewToggle);
    viewToggle.setTooltip ("Show the live spectrum behind the EQ curve, or the curve only");

    soloBtn.setTooltip ("Listen to the selected band on its own");
    soloBtn.onClick = [this] { bandSolo.setToggleState (soloBtn.getToggleState(), dontSendNotification); proc.setBandSolo (soloBtn.getToggleState() ? plot.selected : -1); };
    bandSolo.setTooltip ("Listen to this band on its own");
    bandSolo.onClick = [this] { soloBtn.setToggleState (bandSolo.getToggleState(), dontSendNotification); proc.setBandSolo (bandSolo.getToggleState() ? plot.selected : -1); };
    eqPower = std::make_unique<PowerButton> (proc.apvts, ParamIDs::eqOn);
    eqPower->setTooltip ("EQ on/off (compare with and without the EQ)");
    fullBtn.setTooltip ("Enlarge the EQ graph");
    fullBtn.onClick = [this] { if (onFullscreen) onFullscreen (fullBtn.getToggleState()); };
    abBtn.setClickingTogglesState (true);
    abBtn.getProperties().set ("fontScale", 0.5f);
    abBtn.setTooltip ("A/B: store two EQ settings and flip between them");
    abBtn.onClick = [this] { applyAB(); };
    for (auto* c : { (Component*) &soloBtn, (Component*) eqPower.get(), (Component*) &abBtn, (Component*) &fullBtn }) addAndMakeVisible (c);

    for (int b = 0; b < 6; ++b) bandBox.addItem ("Band " + String (b + 1), b + 1);
    bandBox.onChange = [this] { selectBand (bandBox.getSelectedId() - 1); };
    typeBox.addItemList (ParamChoices::bandTypes, 1);
    bandTrash.setTooltip ("Reset this band to flat");
    bandTrash.onClick = [this]
    {
        for (auto* w : { "type", "freq", "gain", "q", "on" })
            if (auto* prm = proc.apvts.getParameter (ParamIDs::band (plot.selected, w))) prm->setValueNotifyingHost (prm->getDefaultValue());
    };
    for (auto* c : { (Component*) &bandBox, (Component*) &typeBox, (Component*) &freqK, (Component*) &gainK, (Component*) &qK,
                     (Component*) &bandSolo, (Component*) &bandTrash }) addAndMakeVisible (c);
    selectBand (2);
    startTimerHz (30);
}

EqPage::~EqPage() { proc.setBandSolo (-1); }

void EqPage::selectBand (int b)
{
    b = jlimit (0, 5, b);
    plot.selected = b;
    bandBox.setSelectedId (b + 1, dontSendNotification);
    typeAtt.reset(); typeAtt = std::make_unique<ComboAtt> (proc.apvts, ParamIDs::band (b, "type"), typeBox);
    freqK.attach (proc.apvts, ParamIDs::band (b, "freq"));
    gainK.attach (proc.apvts, ParamIDs::band (b, "gain"));
    qK.attach (proc.apvts, ParamIDs::band (b, "q"));
    bandPower.reset (new PowerButton (proc.apvts, ParamIDs::band (b, "on")));
    bandPower->setTooltip ("Band " + String (b + 1) + " on/off");
    addAndMakeVisible (*bandPower);
    if (proc.getBandSolo() >= 0) proc.setBandSolo (b);
    resized(); plot.repaint();
}

void EqPage::applyAB()
{
    auto snapshot = [this]
    {
        ValueTree t ("EQAB");
        for (int b = 0; b < 6; ++b) for (auto* w : { "on", "type", "freq", "gain", "q" })
            t.setProperty (ParamIDs::band (b, w), proc.reader.get (ParamIDs::band (b, w)), nullptr);
        return t;
    };
    abSlot[abIndex] = snapshot();
    abIndex ^= 1;
    if (! abSlot[abIndex].isValid()) abSlot[abIndex] = abSlot[abIndex ^ 1].createCopy();
    for (int i = 0; i < abSlot[abIndex].getNumProperties(); ++i)
    {
        const auto id = abSlot[abIndex].getPropertyName (i).toString();
        if (auto* prm = proc.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) abSlot[abIndex][Identifier (id)]));
    }
    abBtn.setButtonText (abIndex == 0 ? "A/B" : "B/A");
    abBtn.setToggleState (abIndex == 1, dontSendNotification);
}

void EqPage::timerCallback()
{
    plot.updateSpectra();
    const bool solo = proc.getBandSolo() >= 0;
    soloBtn.setToggleState (solo, dontSendNotification); bandSolo.setToggleState (solo, dontSendNotification);
}

void EqPage::paint (Graphics& g)
{
    g.setColour (SauceColours::neonHi); g.setFont (SauceFonts::bold (26.0f));
    g.drawText ("EQ", 28, 6, 60, 34, Justification::centredLeft);
    g.setColour (SauceColours::text); g.setFont (SauceFonts::medium (17.0f));
    g.drawText ("Surgical Equalizer", 92, 6, 170, 34, Justification::centredLeft);
    auto row = Rectangle<float> (14, (float) getHeight() - 74, (float) getWidth() - 28, 64);
    SauceDraw::panel (g, row, 8);
    g.setColour (SauceColours::border); g.drawVerticalLine (1112, row.getY() + 12, row.getBottom() - 12);
    g.setColour (SauceColours::text); g.setFont (SauceFonts::medium (17.0f));
    g.drawText ("Type", 232, (int) row.getY() + 2, 60, 20, Justification::centredLeft);
}

void EqPage::resized()
{
    const int w = getWidth(), h = getHeight();
    viewToggle.setBounds (246, 8, 210, 32);
    soloBtn.setBounds (w - 214, 7, 44, 34);
    if (eqPower) eqPower->setBounds (w - 162, 7, 44, 34);
    abBtn.setBounds (w - 110, 7, 50, 34);
    fullBtn.setBounds (w - 54, 7, 40, 34);
    plot.setBounds (0, 46, w, h - 46 - 84);
    const int ry = h - 74;
    bandBox.setBounds (30, ry + 14, 164, 36);
    typeBox.setBounds (228, ry + 22, 194, 34);
    freqK.setBounds (460, ry + 4, 200, 56);
    gainK.setBounds (680, ry + 4, 200, 56);
    qK.setBounds (890, ry + 4, 200, 56);
    bandSolo.setBounds (w - 210, ry + 13, 46, 38);
    if (bandPower) bandPower->setBounds (w - 154, ry + 13, 46, 38);
    bandTrash.setBounds (w - 98, ry + 13, 46, 38);
}
