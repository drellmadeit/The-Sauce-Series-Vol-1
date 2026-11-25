#include "ModulePages.h"
#include "LinkMenu.h"

using namespace juce;
using namespace ParamIDs;

namespace
{
    void placeKnobs (std::initializer_list<Component*> ks, Rectangle<int> row)
    {
        const int w = row.getWidth() / (int) ks.size();
        int i = 0;
        for (auto* k : ks) k->setBounds (row.withX (row.getX() + w * i++).withWidth (w).reduced (4, 0));
    }
    void historyPath (Graphics& g, const UiState& ui, const std::array<float, UiState::kHist>& h, Rectangle<float> r,
                      float lo, float hi, Colour c, bool fill)
    {
        Path p; bool started = false;
        for (int i = 0; i < UiState::kHist; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) (UiState::kHist - 1);
            const float y = jmap (jlimit (lo, hi, ui.at (h, i)), lo, hi, r.getBottom(), r.getY());
            if (! started) { p.startNewSubPath (x, fill ? r.getBottom() : y); if (fill) p.lineTo (x, y); started = true; } else p.lineTo (x, y);
        }
        if (fill)
        {
            p.lineTo (r.getRight(), r.getBottom()); p.closeSubPath();
            g.setGradientFill (ColourGradient (c.withAlpha (0.8f), 0, r.getY(), c.withAlpha (0.15f), 0, r.getBottom(), false));
            g.fillPath (p);
        }
        else SauceDraw::glowPath (g, p, c, 2.0f, 6);
    }
    void dbGrid (Graphics& g, Rectangle<float> r, float lo, float hi, float step)
    {
        g.setFont (SauceFonts::regular (12.0f));
        for (float d = lo; d <= hi + 0.01f; d += step)
        {
            const float y = jmap (d, lo, hi, r.getBottom(), r.getY());
            g.setColour (Colour (0xff161b22)); g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            g.setColour (SauceColours::textDim); g.drawText (String ((int) d), Rectangle<float> (r.getX() + 4, y - 14, 40, 13), Justification::left);
        }
    }
    void grMeter (Graphics& g, Rectangle<float> r, float grDb, const String& label)
    {
        SauceDraw::fieldBox (g, r, 5);
        const float frac = jlimit (0.0f, 1.0f, -grDb / 24.0f);
        auto bar = r.reduced (5).withHeight ((r.getHeight() - 10) * frac);
        Path p; p.addRoundedRectangle (bar, 3);
        if (frac > 0.002f) { DropShadow (SauceColours::neon.withAlpha (0.7f), 8, {}).drawForPath (g, p); g.setColour (SauceColours::neonHi); g.fillPath (p); }
        g.setColour (SauceColours::text); g.setFont (SauceFonts::medium (13.0f));
        g.drawText (label, r.withY (r.getBottom() + 4).withHeight (18).expanded (30, 0), Justification::centred);
        g.drawText (String (grDb, 1) + " dB", r.withY (r.getBottom() + 22).withHeight (18).expanded (30, 0), Justification::centred);
    }
}

//==============================================================================
GatePage::GatePage (SauceProcessor& p, const UiState& u)
    : ModulePage (p, u, "GATE", "Noise Gate  -  cleans room noise between lines, keeps quiet word endings", gateOn),
      thresh (p.apvts, gateThresh, "Threshold"), range (p.apvts, gateRange, "Range"),
      attack (p.apvts, gateAttack, "Attack", Knob::Medium), hold (p.apvts, gateHold, "Hold", Knob::Medium), release (p.apvts, gateRelease, "Release", Knob::Medium)
{
    for (auto* k : { &thresh, &range, &attack, &hold, &release }) addAndMakeVisible (k);
}

void GatePage::layoutContent()
{
    auto c = controlArea();
    placeKnobs ({ &thresh, &range }, c.removeFromTop (168).reduced (60, 0));
    c.removeFromTop (12);
    placeKnobs ({ &attack, &hold, &release }, c.removeFromTop (138));
}

void GatePage::paintContent (Graphics& g)
{
    auto r = graphArea().toFloat();
    drawGraphFrame (g, r);
    auto pr = r.reduced (10, 12);
    dbGrid (g, pr, -60, 0, 12);
    historyPath (g, ui, ui.hIn, pr, -60, 0, SauceColours::neon, true);
    // gate state strip
    auto strip = pr.removeFromBottom (10);
    for (int i = 0; i < UiState::kHist; ++i)
    {
        const bool closed = ui.at (ui.hGate, i) < -3.0f;
        g.setColour (closed ? Colour (0xff3a1f26) : SauceColours::neonHi.withAlpha (0.8f));
        g.fillRect (strip.getX() + strip.getWidth() * (float) i / UiState::kHist, strip.getY(), strip.getWidth() / UiState::kHist + 0.5f, strip.getHeight());
    }
    const float th = proc.reader.get (gateThresh);
    const float y = jmap (jlimit (-60.0f, 0.0f, th), -60.0f, 0.0f, r.reduced (10, 12).getBottom(), r.reduced (10, 12).getY());
    Path d; d.startNewSubPath (pr.getX(), y); d.lineTo (pr.getRight(), y);
    Path dashed; const float dl[] = { 6, 4 }; PathStrokeType (1.5f).createDashedStroke (dashed, d, dl, 2);
    g.setColour (Colours::white); g.fillPath (dashed);
    g.setFont (SauceFonts::medium (13.0f));
    g.drawText ("Threshold " + String (th, 1) + " dB", Rectangle<float> (pr.getRight() - 190, y - 20, 180, 18), Justification::right);
    const bool open = ui.gateGainDb > -3.0f;
    g.setColour (open ? SauceColours::neonHi : SauceColours::textDim);
    g.drawText (open ? "GATE OPEN" : "GATE CLOSED", Rectangle<float> (pr.getX() + 50, pr.getY(), 160, 18), Justification::left);
}

//==============================================================================
TunePage::TunePage (SauceProcessor& p, const UiState& u)
    : ModulePage (p, u, "AUTO TUNE", "Key-aware pitch correction for a single lead vocal", tuneOn),
      speed (p.apvts, tuneSpeed, "Retune Speed"), amount (p.apvts, tuneAmount, "Amount"), humanize (p.apvts, tuneHumanize, "Humanize"),
      smooth (p.apvts, tuneSmooth, "Smoothness")
{
    key.addItemList (ParamChoices::keys, 1); scale.addItemList (ParamChoices::scales, 1);
    keyAtt = std::make_unique<ComboAtt> (p.apvts, tuneKey, key);
    scaleAtt = std::make_unique<ComboAtt> (p.apvts, tuneScale, scale);
    key.setTooltip ("Key of the song"); scale.setTooltip ("Scale. Chromatic corrects to the nearest semitone.");
    natural.setTooltip ("Subtle, natural correction"); hard.setTooltip ("Instant, obvious hard-tuned melodic-rap sound");
    natural.onClick = [this] { proc.setParam (tuneOn, 1); proc.setParam (tuneSpeed, 45); proc.setParam (tuneAmount, 70); proc.setParam (tuneHumanize, 45); proc.setParam (tuneSmooth, 60); };
    hard.onClick    = [this] { proc.setParam (tuneOn, 1); proc.setParam (tuneSpeed, 0); proc.setParam (tuneAmount, 100); proc.setParam (tuneHumanize, 0); proc.setParam (tuneSmooth, 0); };
    smooth.setTooltip ("Smoothness: tunes the centre of each note and lets your vibrato and slides ride on top, with soft S-curve glides between notes. "
                       "Has no effect at hard-tune speeds (under 15 ms), so the hard-tune sound stays hard.");
    autoKey.setClickingTogglesState (true);
    autoKeyAtt = std::make_unique<ButtonAtt> (p.apvts, ParamIDs::autoKey, autoKey);
    autoKey.setTooltip ("Auto Key: listens to your vocal (and the beat, if routed) and sets Key and Scale for you. "
                        "It runs in the background analysis, so it adds no latency. Turn it off to lock the key.");
    for (auto* c : { (Component*) &key, (Component*) &scale, (Component*) &natural, (Component*) &hard, (Component*) &autoKey, (Component*) &speed, (Component*) &amount, (Component*) &humanize, (Component*) &smooth })
        addAndMakeVisible (c);
}

void TunePage::layoutContent()
{
    auto c = controlArea();
    auto row = c.removeFromTop (40);
    key.setBounds (row.removeFromLeft (110)); row.removeFromLeft (10);
    scale.setBounds (row.removeFromLeft (200)); row.removeFromLeft (16);
    natural.setBounds (row.removeFromLeft (100)); row.removeFromLeft (8);
    hard.setBounds (row.removeFromLeft (110));
    c.removeFromTop (10);
    autoKey.setBounds (c.removeFromTop (34).removeFromLeft (130));
    c.removeFromTop (10);
    placeKnobs ({ &speed, &amount, &humanize, &smooth }, c.removeFromTop (170));
}

void TunePage::paintContent (Graphics& g)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    auto r = graphArea().toFloat();
    drawGraphFrame (g, r);
    auto pr = r.reduced (12, 14).withTrimmedLeft (40);
    const int k = (int) proc.reader.get (tuneKey), sc = (int) proc.reader.get (tuneScale);
    // follow the singer: centre the view on recent pitch
    float sum = 0; int cnt = 0;
    for (int i = UiState::kHist - 60; i < UiState::kHist; ++i) { const float v = ui.at (ui.hPitch, i); if (v > 0) { sum += v; ++cnt; } }
    if (cnt > 0) viewCentre += 0.15f * (sum / (float) cnt - viewCentre);
    const float lo = std::round (viewCentre) - 7.5f, hi = std::round (viewCentre) + 7.5f;
    g.setFont (SauceFonts::medium (12.0f));
    for (int n = (int) std::ceil (lo); n <= (int) hi; ++n)
    {
        const float y = jmap ((float) n, lo, hi, pr.getBottom(), pr.getY());
        const bool in = sauce::PitchCorrector::noteInScale (((n % 12) + 12) % 12, k, sc);
        g.setColour (in ? Colour (0xff182436) : Colour (0xff0e1115)); g.drawHorizontalLine ((int) y, pr.getX(), pr.getRight());
        g.setColour (in ? SauceColours::text : SauceColours::textDim.withAlpha (0.5f));
        g.drawText (String (names[((n % 12) + 12) % 12]) + String (n / 12 - 1), Rectangle<float> (r.getX() + 6, y - 8, 40, 16), Justification::left);
    }
    auto pitchLine = [&] (const std::array<float, UiState::kHist>& h, Colour c, float w)
    {
        Path p; bool pen = false;
        for (int i = 0; i < UiState::kHist; ++i)
        {
            const float v = ui.at (h, i);
            const float x = pr.getX() + pr.getWidth() * (float) i / (float) (UiState::kHist - 1);
            if (v <= 0 || v < lo - 1 || v > hi + 1) { pen = false; continue; }
            const float y = jmap (v, lo, hi, pr.getBottom(), pr.getY());
            if (! pen) { p.startNewSubPath (x, y); pen = true; } else p.lineTo (x, y);
        }
        SauceDraw::glowPath (g, p, c, w, 6);
    };
    pitchLine (ui.hPitch, Colours::white.withAlpha (0.85f), 1.6f);
    pitchLine (ui.hTarget, SauceColours::neonHi, 3.2f);
    // readout
    auto box = Rectangle<float> (r.getRight() - 230, r.getY() + 12, 214, 64);
    SauceDraw::fieldBox (g, box, 8);
    g.setFont (SauceFonts::bold (24.0f)); g.setColour (SauceColours::text);
    if (ui.voiced && ui.tuneHz > 0)
    {
        const float midi = 69.0f + 12.0f * std::log2 (ui.tuneHz / 440.0f);
        const int t = (int) ui.tuneTarget;
        const int cents = roundToInt ((midi - (float) t) * 100.0f);
        g.drawText (String (names[((t % 12) + 12) % 12]) + String (t / 12 - 1), box.removeFromLeft (90), Justification::centred);
        g.setFont (SauceFonts::medium (14.0f));
        g.drawText ((cents >= 0 ? "+" : "") + String (cents) + " cents sung", box.removeFromTop (32), Justification::centredLeft);
        g.setColour (SauceColours::neonHi);
        g.drawText ("moving " + String (roundToInt (ui.tuneCorr)) + " cents", box, Justification::centredLeft);
    }
    else
    {
        g.setFont (SauceFonts::medium (16.0f)); g.setColour (SauceColours::textDim);
        g.drawText (proc.reader.get (tuneOn) > 0.5f ? "Listening for a sung note" : "Auto Tune is off", box, Justification::centred);
    }
    {   // Auto Key status next to its switch
        auto c = controlArea();
        g.setFont (SauceFonts::medium (15.0f));
        g.setColour (proc.reader.get (ParamIDs::autoKey) > 0.5f ? SauceColours::neonHi : SauceColours::textDim);
        g.drawText (proc.autoKeyStatus.isNotEmpty() ? proc.autoKeyStatus : String ("Auto Key off - set Key and Scale yourself"),
                    Rectangle<int> (c.getX() + 142, c.getY() + 50, c.getWidth() - 142, 34), Justification::centredLeft);
    }
    g.setFont (SauceFonts::regular (13.0f)); g.setColour (SauceColours::textDim);
    g.drawText ("White: what you sang   Blue: corrected note   Breaths and consonants pass through untouched",
                Rectangle<float> (r.getX() + 50, r.getBottom() - 22, r.getWidth() - 60, 18), Justification::left);
}

//==============================================================================
MatchPage::MatchPage (SauceProcessor& p, const UiState& u)
    : ModulePage (p, u, "MATCH EQ", "Match your vocal's tonal balance to a reference vocal", matchOn),
      strength (p.apvts, matchStrength, "Strength", Knob::Medium), smooth (p.apvts, matchSmooth, "Smoothing", Knob::Medium),
      limit (p.apvts, matchLimit, "Max Boost/Cut", Knob::Medium)
{
    if (power) power->setTooltip ("Match EQ on/off");
    capMine.setClickingTogglesState (true); capRef.setClickingTogglesState (true);
    capMine.setTooltip ("Play 10-30 s of your vocal (verse + hook). The average tone is captured from your vocal after the EQ.");
    capRef.setTooltip ("Play a reference vocal into the sidechain input. The sidechain is switched to 'Reference Vocal' so it isn't treated as the beat.");
    loadRef.setTooltip ("Load an acapella / isolated reference vocal (WAV, AIFF, FLAC, MP3, OGG).");
    apply.setTooltip ("Apply the suggested curve. Strength, Smoothing and Max Boost/Cut stay adjustable afterwards.");
    ab.setClickingTogglesState (true);
    abAtt = std::make_unique<ButtonAtt> (p.apvts, matchOn, ab);
    ab.setTooltip ("A/B: switch the applied Match EQ in and out");
    reset.setTooltip ("Clear both captures and the applied curve");
    for (auto* b : { &capMine, &capRef, &loadRef, &apply, &ab, &reset }) { b->getProperties().set ("fontScale", 0.4f); addAndMakeVisible (b); }
    for (auto* k : { &strength, &smooth, &limit }) addAndMakeVisible (k);

    capMine.onClick = [this]
    {
        proc.analysis.setCapture (capMine.getToggleState() ? AnalysisEngine::Capture::Mine : AnalysisEngine::Capture::None);
        status = capMine.getToggleState() ? "Capturing your vocal: sing or play back a representative passage." : "Vocal capture stopped.";
    };
    capRef.onClick = [this]
    {
        if (capRef.getToggleState() && (int) proc.reader.get (scRole) != 1)
        { proc.setParam (scRole, 1); status = "Sidechain Input switched to 'Reference Vocal' for this capture. "; }
        else status.clear();
        proc.analysis.setCapture (capRef.getToggleState() ? AnalysisEngine::Capture::ReferenceSidechain : AnalysisEngine::Capture::None);
        status << (capRef.getToggleState() ? "Capturing the reference vocal from the sidechain." : "Reference capture stopped.");
    };
    loadRef.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Choose a reference vocal (acapella)", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            String err;
            status = proc.analysis.loadReferenceFile (f, err) ? "Reference loaded: " + f.getFileName() : err;
            repaint();
        });
    };
    apply.onClick = [this]
    {
        auto v = proc.analysis.getMatchView();
        if (! v.hasMine || ! v.hasRef) { status = "Capture your vocal and a reference first."; repaint(); return; }
        proc.analysis.setCapture (AnalysisEngine::Capture::None);
        proc.analysis.applySuggestion();
        proc.setParam (matchOn, 1);
        status = "Match applied. Use A/B to compare, or adjust Strength / Smoothing / Max Boost-Cut.";
    };
    reset.onClick = [this] { proc.analysis.resetMatch(); status = "Match EQ reset."; };
    linkBtn.getProperties().set ("fontScale", 0.4f);
    linkBtn.setTooltip ("Read a reference vocal from another track in your DAW without sidechaining: put The Sauce Suite on that track, "
                        "choose 'Send as reference vocal' there, pick it here, then press Capture Reference while it plays.");
    linkBtn.onClick = [this] { LinkMenu::show (proc, &linkBtn, [this] { status = proc.getLinkSource().isNotEmpty() ? "Linked to " + proc.getLinkSource() + ". Press Capture Reference while it plays." : String(); repaint(); }); };
    addAndMakeVisible (linkBtn);
}

bool MatchPage::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files) if (File (f).hasFileExtension ("wav;mp3;flac;aif;aiff;ogg")) return true;
    return false;
}

void MatchPage::filesDropped (const StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (File (f).hasFileExtension ("wav;mp3;flac;aif;aiff;ogg"))
        {
            String err;
            status = proc.analysis.loadReferenceFile (File (f), err) ? "Reference loaded: " + File (f).getFileName() + " (sung notes only)" : err;
            break;
        }
    repaint();
}

void MatchPage::paintOverChildren (Graphics& g)
{
    if (! dragOver) return;
    auto d = getLocalBounds().toFloat().reduced (16);
    g.setColour (Colour (0xe0081018)); g.fillRoundedRectangle (d, 14);
    Path p; p.addRoundedRectangle (d.reduced (8), 12);
    Path dashed; const float dl[] = { 14, 9 }; PathStrokeType (3.0f).createDashedStroke (dashed, p, dl, 2);
    g.setColour (SauceColours::neonHi); g.fillPath (dashed);
    g.setFont (SauceFonts::bold (30.0f));
    g.drawText ("DROP A REFERENCE VOCAL HERE", d, Justification::centred);
}

void MatchPage::layoutContent()
{
    auto c = controlArea();
    auto r1 = c.removeFromTop (40); const int w = r1.getWidth() / 3;
    capMine.setBounds (r1.removeFromLeft (w).reduced (3, 0)); capRef.setBounds (r1.removeFromLeft (w).reduced (3, 0)); loadRef.setBounds (r1.reduced (3, 0));
    c.removeFromTop (8);
    auto r2 = c.removeFromTop (40);
    apply.setBounds (r2.removeFromLeft (w).reduced (3, 0)); ab.setBounds (r2.removeFromLeft (w).reduced (3, 0)); reset.setBounds (r2.reduced (3, 0));
    c.removeFromTop (8);
    linkBtn.setBounds (c.removeFromTop (36).removeFromLeft (w * 2).reduced (3, 0));
    c.removeFromTop (26);
    placeKnobs ({ &strength, &smooth, &limit }, c.removeFromTop (130));
}

void MatchPage::tick()
{
    view = proc.analysis.getMatchView();
    const auto cap = proc.analysis.getCapture();
    capMine.setToggleState (cap == AnalysisEngine::Capture::Mine, dontSendNotification);
    capRef.setToggleState (cap == AnalysisEngine::Capture::ReferenceSidechain, dontSendNotification);
    ab.setButtonText (proc.reader.get (matchOn) > 0.5f ? "A/B: Match On" : "A/B: Match Off");
    apply.setEnabled (view.hasMine && view.hasRef);
    repaint();
}

void MatchPage::paintContent (Graphics& g)
{
    auto r = graphArea().toFloat();
    drawGraphFrame (g, r);
    auto pr = r.reduced (14, 14).withTrimmedLeft (30).withTrimmedBottom (18);
    auto xf = [&] (float f) { return pr.getX() + pr.getWidth() * std::log (f / 30.0f) / std::log (16000.0f / 30.0f); };
    auto yd = [&] (float d) { return jmap (jlimit (-13.0f, 13.0f, d), 13.0f, -13.0f, pr.getY(), pr.getBottom()); };
    g.setFont (SauceFonts::regular (12.0f));
    for (int d = -12; d <= 12; d += 6) { g.setColour (Colour (0xff161b22)); g.drawHorizontalLine ((int) yd ((float) d), pr.getX(), pr.getRight());
                                          g.setColour (SauceColours::textDim); g.drawText (String (d), Rectangle<float> (r.getX() + 6, yd ((float) d) - 7, 30, 14), Justification::left); }
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    { g.setColour (Colour (0xff161b22)); g.drawVerticalLine ((int) xf (f), pr.getY(), pr.getBottom());
      g.setColour (SauceColours::textDim); g.drawText (f >= 1000 ? String ((int) (f / 1000)) + "k" : String ((int) f), Rectangle<float> (xf (f) - 20, pr.getBottom() + 2, 40, 14), Justification::centred); }

    auto curvePath = [&] (const std::vector<float>& v, float offset, float scale)
    {
        Path p;
        for (int i = 0; i < (int) v.size(); ++i)
        {
            const float x = xf (sauce::LogGrid::freq (i)), y = yd ((v[(size_t) i] - offset) * scale);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };
    auto centreOf = [] (const std::vector<float>& v)
    {
        double s = 0; int c = 0;
        for (int i = 0; i < (int) v.size(); ++i) { const float f = sauce::LogGrid::freq (i); if (f >= 150 && f <= 6000) { s += v[(size_t) i]; ++c; } }
        return c ? (float) (s / c) : 0.0f;
    };
    // captured tonal balances (level-normalised, scaled to fit)
    if (view.hasMine) { g.setColour (SauceColours::neon.withAlpha (0.7f)); g.strokePath (curvePath (view.mine, centreOf (view.mine), 0.35f), PathStrokeType (1.4f)); }
    if (view.hasRef)  { g.setColour (Colours::white.withAlpha (0.6f));     g.strokePath (curvePath (view.ref, centreOf (view.ref), 0.35f), PathStrokeType (1.4f)); }
    if (! view.suggested.empty())
    {
        Path s = curvePath (view.suggested, 0, 1), dashed; const float dl[] = { 7, 5 };
        PathStrokeType (2.2f).createDashedStroke (dashed, s, dl, 2);
        g.setColour (SauceColours::neonHi); g.fillPath (dashed);
    }
    if (view.hasApplied && ! view.applied.empty())
        SauceDraw::glowPath (g, curvePath (view.applied, 0, 1), proc.reader.get (matchOn) > 0.5f ? Colours::white : SauceColours::textDim, 2.6f, 8);

    // legend + capture status
    g.setFont (SauceFonts::medium (13.0f));
    auto lg = Rectangle<float> (pr.getX() + 8, pr.getY() + 4, 700, 18);
    auto item = [&] (Colour c, const String& t, bool dashed) { g.setColour (c); if (dashed) { g.fillRect (lg.getX(), lg.getCentreY() - 1, 8.0f, 2.5f); g.fillRect (lg.getX() + 12, lg.getCentreY() - 1, 8.0f, 2.5f); } else g.fillRect (lg.getX(), lg.getCentreY() - 1, 20.0f, 2.5f);
                                                                 g.setColour (SauceColours::text); g.drawText (t, lg.withTrimmedLeft (26), Justification::centredLeft); lg.translate (26 + GlyphArrangement::getStringWidth (SauceFonts::medium (13.0f), t) + 18, 0); };
    item (SauceColours::neon, "Your vocal" + (view.mineSec > 0 ? " (" + String (view.mineSec, 1) + " s)" : String()), false);
    item (Colours::white.withAlpha (0.6f), "Reference" + (view.refName.isNotEmpty() ? " (" + view.refName + ")" : String()), false);
    item (SauceColours::neonHi, "Suggested", true);
    item (Colours::white, "Applied", false);

    auto info = controlArea().toFloat().withTrimmedTop (98).withHeight (60);
    g.setFont (SauceFonts::regular (14.0f)); g.setColour (SauceColours::textDim);
    const String steps = "1) Capture your vocal   2) Capture or load a reference vocal   3) Apply";
    g.drawFittedText (status.isNotEmpty() ? status : steps, info.toNearestInt(), Justification::centredLeft, 3);
}

//==============================================================================
CompPage::CompPage (SauceProcessor& p, const UiState& u)
    : ModulePage (p, u, "COMPRESSOR", "Evens out your level so every word sits forward", compOn),
      thresh (p.apvts, compThresh, "Threshold"), ratio (p.apvts, compRatio, "Ratio"),
      attack (p.apvts, compAttack, "Attack", Knob::Medium), release (p.apvts, compRelease, "Release", Knob::Medium), makeup (p.apvts, compMakeup, "Makeup", Knob::Medium)
{
    for (auto* k : { &thresh, &ratio, &attack, &release, &makeup }) addAndMakeVisible (k);
}

void CompPage::layoutContent()
{
    auto c = controlArea().withTrimmedRight (90);
    placeKnobs ({ &thresh, &ratio }, c.removeFromTop (168).reduced (30, 0));
    c.removeFromTop (12);
    placeKnobs ({ &attack, &release, &makeup }, c.removeFromTop (138));
}

void CompPage::paintContent (Graphics& g)
{
    auto r = graphArea().toFloat();
    drawGraphFrame (g, r);
    // transfer curve (left) + gain-reduction history (right)
    auto tc = r.reduced (14).removeFromLeft (r.getHeight() - 28);
    const float th = proc.reader.get (compThresh), ra = proc.reader.get (compRatio), mk = proc.reader.get (compMakeup);
    auto map = [&] (float in, float out) { return Point<float> (jmap (in, -60.0f, 0.0f, tc.getX(), tc.getRight()), jmap (jlimit (-60.0f, 6.0f, out), -60.0f, 6.0f, tc.getBottom(), tc.getY())); };
    g.setColour (Colour (0xff161b22));
    for (int d = -60; d <= 0; d += 12) { auto p = map ((float) d, (float) d); g.drawVerticalLine ((int) p.x, tc.getY(), tc.getBottom()); g.drawHorizontalLine ((int) p.y, tc.getX(), tc.getRight()); }
    g.setColour (SauceColours::textDim); g.drawLine (Line<float> (map (-60, -60), map (0, 0)), 1.0f);
    sauce::Compressor cmp; cmp.threshDb = th; cmp.ratio = ra;
    Path p;
    for (float in = -60; in <= 0.01f; in += 0.5f) { auto pt = map (in, in + cmp.staticCurve (in) + mk); if (in == -60) p.startNewSubPath (pt); else p.lineTo (pt); }
    SauceDraw::glowPath (g, p, SauceColours::neonHi, 2.6f, 8);
    const float lvl = jlimit (-60.0f, 0.0f, ui.inDb[0]);
    auto dot = map (lvl, lvl + cmp.staticCurve (lvl) + mk);
    g.setColour (Colours::white); g.fillEllipse (dot.x - 5, dot.y - 5, 10, 10);
    g.setFont (SauceFonts::regular (12.0f)); g.setColour (SauceColours::textDim);
    g.drawText ("input level >", tc.withTop (tc.getBottom() - 14), Justification::right);

    auto hr = r.reduced (14).withTrimmedLeft (tc.getWidth() + 20);
    g.setColour (SauceColours::text); g.setFont (SauceFonts::medium (13.0f));
    g.drawText ("Gain reduction", hr.removeFromTop (18), Justification::left);
    Path gp;
    for (int i = 0; i < UiState::kHist; ++i)
    {
        const float x = hr.getX() + hr.getWidth() * (float) i / (UiState::kHist - 1);
        const float y = jmap (jlimit (-24.0f, 0.0f, ui.at (ui.hGr, i)), 0.0f, -24.0f, hr.getY(), hr.getBottom());
        if (i == 0) gp.startNewSubPath (x, y); else gp.lineTo (x, y);
    }
    SauceDraw::glowPath (g, gp, SauceColours::neonHi, 2.0f, 6);
    grMeter (g, controlArea().toFloat().withLeft ((float) controlArea().getRight() - 60).withWidth (34).withHeight (230).translated (0, 20), ui.compGrDb, "GR");
}

//==============================================================================
FxPage::FxPage (SauceProcessor& p, const UiState& u)
    : ModulePage (p, u, "VOCAL FX", "Colour and width around a centred lead", {}),
      satOn (p.apvts, ParamIDs::satOn), dblOn (p.apvts, ParamIDs::dblOn), wideOn (p.apvts, ParamIDs::wideOn),
      drive (p.apvts, satDrive, "Drive"), satMix (p.apvts, ParamIDs::satMix, "Mix"),
      dblAmt (p.apvts, dblAmount, "Amount"), wideAmt (p.apvts, wideAmount, "Amount")
{
    for (auto* c : { (Component*) &satOn, (Component*) &dblOn, (Component*) &wideOn, (Component*) &drive, (Component*) &satMix,
                     (Component*) &dblAmt, (Component*) &wideAmt }) addAndMakeVisible (c);
}

void FxPage::layoutContent()
{
    const int w = (getWidth() - 48) / 3;
    for (int i = 0; i < 3; ++i)
    {
        auto col = Rectangle<int> (24 + i * w, 56, w - 12, getHeight() - 72);
        PowerButton* pb = i == 0 ? &satOn : (i == 1 ? &dblOn : &wideOn);
        pb->setBounds (col.getX() + 16, col.getY() + 14, 40, 32);
        auto kr = col.withTrimmedTop (110).withHeight (170);
        if (i == 0) placeKnobs ({ &drive, &satMix }, kr.reduced (20, 0));
        else placeKnobs ({ i == 1 ? &dblAmt : &wideAmt }, kr.reduced (110, 0));
    }
}

void FxPage::paintContent (Graphics& g)
{
    const int w = (getWidth() - 48) / 3;
    const char* titles[] = { "SATURATION", "DOUBLER", "WIDTH / CHORUS" };
    const char* desc[] = { "Warm harmonic grit; blend it in with Mix.", "Two drifting copies panned left/right; the lead stays centred.", "Modulated copies on the sides only; mono-compatible." };
    for (int i = 0; i < 3; ++i)
    {
        auto col = Rectangle<float> ((float) (24 + i * w), 56, (float) w - 12, (float) getHeight() - 72);
        SauceDraw::panel (g, col, 8);
        g.setColour (SauceColours::text); g.setFont (SauceFonts::bold (20.0f));
        g.drawText (titles[i], col.withTrimmedLeft (68).withHeight (60), Justification::centredLeft);
        g.setColour (SauceColours::textDim); g.setFont (SauceFonts::regular (14.0f));
        g.drawFittedText (desc[i], col.reduced (18, 0).withTrimmedTop (60).withHeight (40).toNearestInt(), Justification::centredLeft, 2);
    }
}

//==============================================================================
LimiterPage::LimiterPage (SauceProcessor& p, const UiState& u)
    : ModulePage (p, u, "LIMITER", "Ceiling, drive and character - drag the tab to move it in the chain", limOn),
      drive (p.apvts, limDrive, "Drive"), ceiling (p.apvts, limCeiling, "Ceiling"), release (p.apvts, limRelease, "Release")
{
    character.addItemList (ParamChoices::limCharacters, 1);
    characterAtt = std::make_unique<ComboAtt> (p.apvts, limCharacter, character);
    character.setTooltip ("Clean: transparent. Punchy: faster release for more impact. Loud: rounds peaks off with a soft clipper first, for more volume.");
    drive.setTooltip ("Pushes the vocal into the limiter: more drive = louder and more squashed.");
    ceiling.setTooltip ("Highest level the limiter lets out (down to -30 dB). At the end of the chain nothing goes over it.");
    for (auto* k : { (Component*) &drive, (Component*) &ceiling, (Component*) &release, (Component*) &character }) addAndMakeVisible (k);
}

void LimiterPage::layoutContent()
{
    auto c = controlArea().withTrimmedRight (90);
    auto top = c.removeFromTop (40);
    character.setBounds (top.removeFromLeft (220));
    c.removeFromTop (10);
    placeKnobs ({ &drive, &ceiling, &release }, c.removeFromTop (180));
}

void LimiterPage::paintContent (Graphics& g)
{
    auto r = graphArea().toFloat();
    drawGraphFrame (g, r);
    auto pr = r.reduced (10, 12);
    dbGrid (g, pr, -36, 0, 6);
    historyPath (g, ui, ui.hOut, pr, -36, 0, SauceColours::neon, true);
    Path gp;
    for (int i = 0; i < UiState::kHist; ++i)
    {
        const float x = pr.getX() + pr.getWidth() * (float) i / (UiState::kHist - 1);
        const float y = jmap (jlimit (-24.0f, 0.0f, ui.at (ui.hLim, i)), 0.0f, -24.0f, pr.getY(), pr.getY() + pr.getHeight() * 0.6f);
        if (i == 0) gp.startNewSubPath (x, y); else gp.lineTo (x, y);
    }
    SauceDraw::glowPath (g, gp, Colours::white, 2.0f, 6);
    const float ce = proc.reader.get (limCeiling);
    const float y = jmap (jlimit (-36.0f, 0.0f, ce), -36.0f, 0.0f, pr.getBottom(), pr.getY());
    g.setColour (Colour (0xffff4d5e)); g.drawHorizontalLine ((int) y, pr.getX(), pr.getRight());
    g.setFont (SauceFonts::medium (13.0f)); g.setColour (SauceColours::text);
    g.drawText ("Ceiling " + String (ce, 1) + " dB   White: gain reduction   Blue: output level", Rectangle<float> (pr.getX() + 50, pr.getY(), 520, 18), Justification::left);
    const bool last = proc.getChainOrder().endsWithChar ('L');
    g.setColour (last ? SauceColours::textDim : Colour (0xffffb020));
    g.drawText (last ? "Last in the chain: nothing leaves the plugin above the ceiling."
                     : "Moved inside the chain: later modules (delay, reverb...) can go above the ceiling.",
                Rectangle<float> (pr.getX() + 50, pr.getY() + 20, 620, 18), Justification::left);
    auto c = controlArea().toFloat();
    grMeter (g, c.withLeft (c.getRight() - 60).withWidth (34).withHeight (230).translated (0, 20), ui.limGrDb, "GR");
}

//==============================================================================
DeessPage::DeessPage (SauceProcessor& p, const UiState& u)
    : ModulePage (p, u, "DE-ESSER", "Tames harsh S, T and SH sounds without dulling the voice", deessOn),
      freq (p.apvts, deessFreq, "Frequency"), thresh (p.apvts, deessThresh, "Threshold"), range (p.apvts, deessRange, "Max Reduction")
{
    listen.setClickingTogglesState (true);
    listenAtt = std::make_unique<ButtonAtt> (p.apvts, deessListen, listen);
    listen.setTooltip ("Hear only the band the de-esser works on, to find the harsh S's. Turn it off when you're done.");
    freq.setTooltip ("Where the S's are: around 5-8 kHz for most voices (lower for deep voices, higher for bright ones). Only sound above this is reduced.");
    thresh.setTooltip ("Lower = de-esses more often.");
    range.setTooltip ("The most it will ever turn the S's down.");
    for (auto* c : { (Component*) &freq, (Component*) &thresh, (Component*) &range, (Component*) &listen }) addAndMakeVisible (c);
}

void DeessPage::layoutContent()
{
    auto c = controlArea().withTrimmedRight (90);
    listen.setBounds (c.removeFromTop (40).removeFromLeft (130));
    c.removeFromTop (10);
    placeKnobs ({ &freq, &thresh, &range }, c.removeFromTop (180));
}

void DeessPage::paintContent (Graphics& g)
{
    auto r = graphArea().toFloat();
    drawGraphFrame (g, r);
    auto pr = r.reduced (10, 12);
    dbGrid (g, pr, -24, 0, 6);
    Path gp;
    for (int i = 0; i < UiState::kHist; ++i)
    {
        const float x = pr.getX() + pr.getWidth() * (float) i / (UiState::kHist - 1);
        const float y = jmap (jlimit (-24.0f, 0.0f, ui.at (ui.hDeess, i)), -24.0f, 0.0f, pr.getBottom(), pr.getY());
        if (i == 0) gp.startNewSubPath (x, y); else gp.lineTo (x, y);
    }
    Path fill (gp); fill.lineTo (pr.getRight(), pr.getY()); fill.lineTo (pr.getX(), pr.getY()); fill.closeSubPath();
    g.setColour (SauceColours::neon.withAlpha (0.25f)); g.fillPath (fill);
    SauceDraw::glowPath (g, gp, SauceColours::neonHi, 2.2f, 6);
    g.setFont (SauceFonts::medium (13.0f)); g.setColour (SauceColours::text);
    const bool listening = proc.reader.get (deessListen) > 0.5f;
    g.drawText (listening ? "LISTEN is on: you're hearing only the S band" : "S reduction over time (deeper = more de-essing)",
                Rectangle<float> (pr.getX() + 50, pr.getBottom() - 22, 520, 18), Justification::left);
    auto c = controlArea().toFloat();
    grMeter (g, c.withLeft (c.getRight() - 60).withWidth (34).withHeight (230).translated (0, 20), ui.deessGrDb, "S cut");
}

//==============================================================================
PlosivePage::PlosivePage (SauceProcessor& p, const UiState& u)
    : ModulePage (p, u, "DE-POP", "Plosive filter - catches P and B pops and mic air blasts", popOn),
      sens (p.apvts, popSens, "Sensitivity"), freq (p.apvts, popFreq, "Frequency"), depth (p.apvts, popDepth, "Max Reduction")
{
    sens.setTooltip ("Higher = catches softer pops. Too high can thin out deep words.");
    freq.setTooltip ("Only sound below this is reduced when a pop hits (the thump of a P or B lives under ~150-250 Hz).");
    depth.setTooltip ("The most a pop is turned down.");
    for (auto* k : { &sens, &freq, &depth }) addAndMakeVisible (k);
}

void PlosivePage::layoutContent()
{
    auto c = controlArea().withTrimmedRight (90);
    c.removeFromTop (50);
    placeKnobs ({ &sens, &freq, &depth }, c.removeFromTop (180));
}

void PlosivePage::paintContent (Graphics& g)
{
    auto r = graphArea().toFloat();
    drawGraphFrame (g, r);
    auto pr = r.reduced (10, 12);
    dbGrid (g, pr, -30, 0, 6);
    historyPath (g, ui, ui.hIn, pr.withTrimmedBottom (pr.getHeight() * 0.5f), -60, 0, SauceColours::neon.withAlpha (0.6f), true);
    Path gp;
    for (int i = 0; i < UiState::kHist; ++i)
    {
        const float x = pr.getX() + pr.getWidth() * (float) i / (UiState::kHist - 1);
        const float y = jmap (jlimit (-30.0f, 0.0f, ui.at (ui.hPop, i)), -30.0f, 0.0f, pr.getBottom(), pr.getY());
        if (i == 0) gp.startNewSubPath (x, y); else gp.lineTo (x, y);
    }
    SauceDraw::glowPath (g, gp, Colours::white, 2.2f, 6);
    g.setFont (SauceFonts::medium (13.0f)); g.setColour (SauceColours::text);
    g.drawText ("White: pops caught (dips = low end pulled down for a moment)", Rectangle<float> (pr.getX() + 50, pr.getBottom() - 22, 560, 18), Justification::left);
    auto c = controlArea().toFloat();
    grMeter (g, c.withLeft (c.getRight() - 60).withWidth (34).withHeight (230).translated (0, 20), ui.popGrDb, "Pop cut");
}
