#include "FxPanels.h"

using namespace juce;
using namespace ParamIDs;

void DuckBox::paint (Graphics& g)
{
    SauceDraw::panel (g, getLocalBounds().toFloat().reduced (4), 10, true);
    // live ducking meter along the bottom edge
    auto bar = getLocalBounds().toFloat().reduced (18, 0).withHeight (4).withY ((float) getHeight() - 14);
    g.setColour (Colour (0xff1b2029)); g.fillRoundedRectangle (bar, 2);
    const float frac = jlimit (0.0f, 1.0f, -duckDb / 24.0f);
    if (frac > 0.01f) { g.setColour (Colours::white); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * frac), 2); }
}

//==============================================================================
DelayPanel::DelayPanel (SauceProcessor& p, const UiState& u)
    : proc (p), ui (u), power (p.apvts, dlyOn),
      time (p.apvts, dlyTime, "Time", Knob::Mini), lowCut (p.apvts, dlyLowCut, "Low Cut", Knob::Mini), highCut (p.apvts, dlyHighCut, "High Cut", Knob::Mini),
      feedback (p.apvts, dlyFeedback, "Feedback"), mix (p.apvts, dlyMix, "Mix"), duck (p.apvts, dlyDuck)
{
    sync.addItem ("Free (ms)", 1);
    for (int d = 0; d < ParamChoices::divisions.size(); ++d)
        for (int f = 0; f < 3; ++f)
            sync.addItem (ParamChoices::divisions[d] + (f == 1 ? " Dotted" : (f == 2 ? " Triplet" : "")), 2 + d * 3 + f);
    sync.setTooltip ("Sync to the DAW tempo, or Free to set the time in milliseconds");
    sync.onChange = [this]
    {
        const int id = sync.getSelectedId();
        if (id <= 0) return;
        if (id == 1) { proc.setParam (dlySync, 0); return; }
        proc.setParam (dlySync, 1); proc.setParam (dlyDiv, (float) ((id - 2) / 3)); proc.setParam (dlyFeel, (float) ((id - 2) % 3));
    };
    mode.setTooltip ("Stereo echoes, or Ping-Pong bouncing left/right");
    mode.onChange = [this] (int i) { proc.setParam (dlyMode, (float) i); };
    duck.knob.slider.setTooltip ("Delay Ducking: how far the echoes drop while you're delivering a line. They rise back into the gaps. Independent of reverb ducking.");
    for (auto* c : { (Component*) &power, (Component*) &sync, (Component*) &mode, (Component*) &time, (Component*) &lowCut, (Component*) &highCut,
                     (Component*) &feedback, (Component*) &mix, (Component*) &duck }) addAndMakeVisible (c);
    syncFromParams();
}

void DelayPanel::syncFromParams()
{
    const bool s = proc.reader.get (dlySync) > 0.5f;
    const int id = s ? 2 + (int) proc.reader.get (dlyDiv) * 3 + (int) proc.reader.get (dlyFeel) : 1;
    if (! sync.isPopupActive() && sync.getSelectedId() != id) sync.setSelectedId (id, dontSendNotification);
    time.setEnabled (! s);
    mode.setIndex ((int) proc.reader.get (dlyMode));
}

void DelayPanel::tick()
{
    syncFromParams();
    std::rotate (wave.begin(), wave.begin() + 1, wave.end());
    wave.back() = jlimit (0.0f, 1.0f, sauce::gainToDb (ui.dlyRet, -60.0f) / 50.0f + 1.15f);
    duck.setDuckDb (ui.dlyDuckDb);
    repaint (520, 10, 130, 46);
}

void DelayPanel::paint (Graphics& g)
{
    SauceDraw::panel (g, getLocalBounds().toFloat().reduced (1), 10);
    drawTitle (g, "delay", "DELAY", { 76, 16, 34, 34 }, { 124, 16 }, 28.0f);
    // animated return level
    auto wr = Rectangle<float> (530, 18, 110, 32);
    g.setColour (SauceColours::neonHi);
    for (int i = 0; i < (int) wave.size(); ++i)
    {
        const float hgt = jmax (2.0f, wave[(size_t) i] * wr.getHeight() * (0.4f + 0.6f * std::exp (-0.08f * (float) ((int) wave.size() - 1 - i))));
        g.fillRect (wr.getX() + (float) i * 4.6f, wr.getCentreY() - hgt * 0.5f, 2.2f, hgt);
    }
    g.setColour (SauceColours::text); g.setFont (SauceFonts::medium (17.0f));
    g.drawText ("Sync", 26, 56, 120, 22, Justification::centredLeft);
}

void DelayPanel::resized()
{
    power.setBounds (18, 14, 44, 40);
    sync.setBounds (24, 78, 172, 30);
    mode.setBounds (24, 112, 172, 26);
    time.setBounds (12, 140, 66, 82); lowCut.setBounds (78, 140, 66, 82); highCut.setBounds (144, 140, 66, 82);
    feedback.setBounds (202, 60, 132, 158);
    mix.setBounds (336, 60, 132, 158);
    duck.setBounds (474, 56, 172, 164);
}

//==============================================================================
ReverbPanel::ReverbPanel (SauceProcessor& p, const UiState& u)
    : proc (p), ui (u), power (p.apvts, revOn),
      preDelay (p.apvts, revPreDelay, "Pre-Delay", Knob::Medium), damping (p.apvts, revDamp, "Damping", Knob::Medium),
      decay (p.apvts, revDecay, "Decay"), mix (p.apvts, revMix, "Mix"), duck (p.apvts, revDuck)
{
    type.addItemList (ParamChoices::revTypes, 1);
    typeAtt = std::make_unique<ComboAtt> (p.apvts, revType, type);
    type.setTooltip ("Room, Plate or Hall");
    duck.knob.slider.setTooltip ("Reverb Ducking: how far the reverb drops while you're delivering a line. The tail blooms into the gaps. Independent of delay ducking.");
    for (auto* c : { (Component*) &power, (Component*) &type, (Component*) &preDelay, (Component*) &damping, (Component*) &decay, (Component*) &mix, (Component*) &duck })
        addAndMakeVisible (c);
}

void ReverbPanel::tick()
{
    duck.setDuckDb (ui.revDuckDb);
    const float d = proc.reader.get (revDecay), pr = proc.reader.get (revPreDelay);
    const float lvl = jlimit (0.0f, 1.0f, sauce::gainToDb (ui.revRet, -60.0f) / 50.0f + 1.15f);
    if (d != lastDecay || pr != lastPre || std::abs (lvl - lastLevel) > 0.03f) { lastDecay = d; lastPre = pr; lastLevel = lvl; repaint (548, 10, 110, 46); }
}

void ReverbPanel::paint (Graphics& g)
{
    SauceDraw::panel (g, getLocalBounds().toFloat().reduced (1), 10);
    drawTitle (g, "reverb", "REVERB", { 76, 16, 34, 34 }, { 124, 16 }, 28.0f);
    // decay envelope preview (pre-delay gap + exponential tail), brightness follows the live return
    auto r = Rectangle<float> (556, 14, 96, 38);
    const float pre = jlimit (0.0f, 250.0f, proc.reader.get (revPreDelay)), dec = jlimit (0.2f, 10.0f, proc.reader.get (revDecay));
    const float span = 2.5f + dec * 0.5f, x0 = r.getX() + r.getWidth() * (pre / 1000.0f) / span;
    Path p; p.startNewSubPath (r.getX(), r.getBottom()); p.lineTo (x0, r.getBottom()); p.lineTo (x0, r.getY());
    for (float t = 0; t <= span; t += span / 40.0f)
    {
        const float x = x0 + r.getWidth() * t / span;
        if (x > r.getRight()) break;
        p.lineTo (x, r.getBottom() - r.getHeight() * std::pow (10.0f, -3.0f * t / dec));
    }
    p.lineTo (r.getRight(), r.getBottom()); p.closeSubPath();
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.55f + 0.4f * lastLevel), r.getX(), r.getY(), Colours::white.withAlpha (0.05f), r.getRight(), r.getBottom(), false));
    g.fillPath (p);
}

void ReverbPanel::resized()
{
    power.setBounds (18, 14, 44, 40);
    type.setBounds (360, 16, 160, 34);
    preDelay.setBounds (14, 74, 92, 140);
    damping.setBounds (104, 74, 92, 140);
    decay.setBounds (204, 60, 132, 158);
    mix.setBounds (338, 60, 132, 158);
    duck.setBounds (482, 56, 174, 164);
}
