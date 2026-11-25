#include "SidePanels.h"

using namespace juce;

//==============================================================================
PresetRoutingPanel::PresetRoutingPanel (SauceProcessor& p) : proc (p), selectors (p)
{
    for (auto* c : { &selectors.preset, &selectors.genre, &selectors.artist }) addAndMakeVisible (c);
    selectors.preset.getProperties().set ("noArrow", true);
    addAndMakeVisible (pencil);
    pencil.setTooltip ("Rename / save the current sound as your own preset");
    pencil.onClick = [this] { if (onSaveAs) onSaveAs(); };
    role.addItemList (ParamChoices::scRoles, 1);
    roleAtt = std::make_unique<ComboAtt> (p.apvts, ParamIDs::scRole, role);
    role.setTooltip ("What the sidechain carries: the Instrumental (beat) for the analyser and Vocal Coach, or a Reference Vocal for Match EQ. It is never heard in the output.");
    addAndMakeVisible (role);
    help.getProperties().set ("fontScale", 0.48f);
    help.onClick = [this] { if (onRoutingHelp) onRoutingHelp(); };
    addAndMakeVisible (help);
}

void PresetRoutingPanel::tick()
{
    const bool r = proc.analysis.isReceivingSidechain(), c = proc.sidechainConnected.load();
    const int ri = (int) proc.reader.get (ParamIDs::scRole);
    const float lv = proc.analysis.sidechainLevelDb();
    if (r != receiving || c != connected || ri != roleIdx || std::abs (lv - level) > 3.0f)
    { receiving = r; connected = c; roleIdx = ri; level = lv; repaint (0, 180, getWidth(), 36); }
}

void PresetRoutingPanel::paint (Graphics& g)
{
    SauceDraw::panel (g, getLocalBounds().toFloat().reduced (1), 10);
    g.setColour (SauceColours::text); g.setFont (SauceFonts::bold (21.0f));
    g.drawText ("PRESET & ROUTING", 18, 8, 280, 32, Justification::centredLeft);
    g.setFont (SauceFonts::medium (16.0f));
    g.drawText ("Preset", 18, 44, 80, 30, Justification::centredLeft);
    g.drawText ("Genre", 18, 78, 80, 22, Justification::centredLeft);
    g.drawText ("Artist", 172, 78, 80, 22, Justification::centredLeft);
    g.setColour (SauceColours::border); g.drawHorizontalLine (142, 16, (float) getWidth() - 16);
    g.setColour (SauceColours::text);
    g.drawText ("Sidechain Input", 18, 148, 140, 30, Justification::centredLeft);
    g.strokePath (SauceDraw::icon ("link", { 140, 154, 18, 18 }), PathStrokeType (1.6f));
    // status
    const auto dotC = receiving ? SauceColours::neonHi : (connected ? SauceColours::warn : SauceColours::textDim);
    g.setColour (dotC); g.fillEllipse (20, 193, 10, 10);
    g.setColour (SauceColours::text); g.setFont (SauceFonts::regular (13.5f));
    String s;
    if (receiving) s = (roleIdx == 0 ? "Receiving beat  " : "Receiving reference  ") + String (roundToInt (level)) + " dB";
    else if (connected) s = "Sidechain on, no signal";
    else s = "No sidechain routed";
    g.drawText (s, 36, 186, 170, 24, Justification::centredLeft);
}

void PresetRoutingPanel::resized()
{
    selectors.preset.setBounds (90, 42, 214, 34);
    pencil.setBounds (272, 46, 26, 26);
    selectors.genre.setBounds (16, 100, 144, 32);
    selectors.artist.setBounds (170, 100, 136, 32);
    role.setBounds (172, 148, 134, 32);
    help.setBounds (206, 184, 100, 28);
}

//==============================================================================
void VocalCoachPanel::tick()
{
    auto m = proc.analysis.getCoachMessage();
    ++dots;
    if (m.listening != msg.listening || m.observation != msg.observation || m.suggestion != msg.suggestion) { msg = m; repaint(); }
    else if (msg.listening && dots % 15 == 0) repaint();
}

void VocalCoachPanel::paint (Graphics& g)
{
    SauceDraw::panel (g, getLocalBounds().toFloat().reduced (1), 10);
    g.setColour (SauceColours::text);
    g.strokePath (SauceDraw::icon ("coach", { 20, 12, 34, 32 }), PathStrokeType (2.0f));
    g.setFont (SauceFonts::bold (23.0f));
    g.drawText ("VOCAL COACH", 70, 10, 240, 36, Justification::centredLeft);

    auto inner = getLocalBounds().toFloat().reduced (12).withTrimmedTop (44);
    g.setColour (Colour (0xff0a0d11)); g.fillRoundedRectangle (inner, 8);
    g.setColour (SauceColours::border); g.drawRoundedRectangle (inner, 8, 1.0f);

    g.setColour (SauceColours::neonHi);
    g.strokePath (SauceDraw::icon ("wave", { inner.getX() + 10, inner.getY() + 10, 26, 28 }), PathStrokeType (2.0f));
    g.strokePath (SauceDraw::icon ("bulb", { inner.getX() + 10, inner.getY() + 84, 26, 28 }), PathStrokeType (1.8f));
    g.setFont (SauceFonts::bold (16.5f));
    g.drawText ("Observation", Rectangle<float> (inner.getX() + 46, inner.getY() + 4, 220, 22), Justification::centredLeft);
    g.drawText ("Suggestion", Rectangle<float> (inner.getX() + 46, inner.getY() + 78, 220, 22), Justification::centredLeft);

    String obs = msg.observation, sug = msg.suggestion;
    if (msg.listening)
    {
        obs = "Listening" + String::repeatedString (".", 1 + (dots / 15) % 3);
        sug = "Play your vocal for a few seconds. Route your beat to the sidechain to check how they fit together.";
    }
    g.setColour (SauceColours::text); g.setFont (SauceFonts::regular (15.0f));
    g.drawFittedText (obs, Rectangle<float> (inner.getX() + 46, inner.getY() + 26, inner.getWidth() - 54, 50).toNearestInt(), Justification::topLeft, 3, 1.0f);
    auto box = Rectangle<float> (inner.getX() + 42, inner.getY() + 102, inner.getWidth() - 50, inner.getHeight() - 108);
    g.setColour (SauceColours::field); g.fillRoundedRectangle (box, 6);
    g.setColour (Colour (0xff5b6472)); g.drawRoundedRectangle (box, 6, 1.0f);
    g.setColour (SauceColours::text); g.setFont (SauceFonts::regular (14.5f));
    g.drawFittedText (sug, box.reduced (8, 4).toNearestInt(), Justification::centredLeft, 3, 1.0f);
}

//==============================================================================
MetersPanel::MetersPanel (SauceProcessor& p, const UiState& u) : proc (p), ui (u), gain (p.apvts, ParamIDs::outGain, "GAIN", Knob::Medium)
{
    gain.label.setFont (SauceFonts::medium (17.0f));
    gain.slider.setTooltip ("Output gain (same control as OUTPUT below)");
    latency.setTooltip ("Mix: best quality, fully delay-compensated (about 16 ms).\n"
                        "Tracking: low latency for recording (about 0.5 ms; tuned notes arrive about one cycle of your voice later). "
                        "Switch back to Mix for mixing.");
    latency.onChange = [this] (int i) { proc.setParam (ParamIDs::latencyMode, (float) i); };
    addAndMakeVisible (latency);
    for (auto* c : { (Component*) &inL, (Component*) &inR, (Component*) &outL, (Component*) &outR, (Component*) &gain }) addAndMakeVisible (c);
}

void MetersPanel::tick()
{
    latency.setIndex ((int) proc.reader.get (ParamIDs::latencyMode));
    if (proc.getLatencySamples() != shownLatency) { shownLatency = proc.getLatencySamples(); repaint(); }
    inL.setLevelDb (ui.inDb[0]); inR.setLevelDb (ui.inDb[1]); outL.setLevelDb (ui.outDb[0]); outR.setLevelDb (ui.outDb[1]);
}

void MetersPanel::paint (Graphics& g)
{
    SauceDraw::panel (g, getLocalBounds().toFloat().reduced (1), 10);
    g.setColour (SauceColours::text); g.setFont (SauceFonts::bold (21.0f));
    g.drawText ("METERS", 18, 8, 200, 32, Justification::centredLeft);
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    g.setFont (SauceFonts::medium (13.0f)); g.setColour (shownLatency > 200 ? SauceColours::textDim : SauceColours::neonHi);
    g.drawText ("Latency " + String (jmax (0, shownLatency) * 1000.0 / sr, 1) + " ms", 104, 226, 112, 18, Justification::centred);
    g.setFont (SauceFonts::medium (16.0f));
    g.drawText ("IN", 34, 40, 40, 22, Justification::centred);
    g.drawText ("OUT", 244, 40, 50, 22, Justification::centred);
    g.setFont (SauceFonts::regular (12.5f));
    const int marks[] = { 0, -3, -6, -9, -12, -18, -24, -36, -48, -60 };
    for (int m : marks)
    {
        const float y = jmap (SegMeter::fracForDb ((float) m), 0.0f, 1.0f, 236.0f, 76.0f);
        g.drawText (String (m), 72, (int) y - 7, 30, 14, Justification::centredLeft);
        g.drawText (String (m), 226, (int) y - 7, 30, 14, Justification::centredRight);
    }
}

void MetersPanel::resized()
{
    inL.setBounds (34, 70, 14, 172); inR.setBounds (52, 70, 14, 172);
    outL.setBounds (262, 70, 14, 172); outR.setBounds (280, 70, 14, 172);
    gain.setBounds (104, 60, 112, 140);
    latency.setBounds (128, 10, 178, 28);
}

//==============================================================================
BottomBar::BottomBar (SauceProcessor& p, const UiState& u) : proc (p), ui (u)
{
    for (auto* k : { &inKnob, &outKnob })
    {
        k->setSliderStyle (Slider::RotaryVerticalDrag);
        k->setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        k->setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        k->getProperties().set ("mini", true);
        addAndMakeVisible (k);
    }
    inAtt = std::make_unique<SliderAtt> (p.apvts, ParamIDs::inGain, inKnob);
    outAtt = std::make_unique<SliderAtt> (p.apvts, ParamIDs::outGain, outKnob);
    inKnob.setTooltip ("Input gain (drives the gate and compressor)"); outKnob.setTooltip ("Output gain (into the final limiter)");
    inKnob.setDoubleClickReturnValue (true, 0.0); outKnob.setDoubleClickReturnValue (true, 0.0);
    dryWet.setSliderStyle (Slider::LinearHorizontal);
    dryWet.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    dwAtt = std::make_unique<SliderAtt> (p.apvts, ParamIDs::dryWet, dryWet);
    dryWet.setTooltip ("Blend the untouched vocal with the processed chain");
    dryWet.setDoubleClickReturnValue (true, 100.0);
    addAndMakeVisible (dryWet);
    addAndMakeVisible (inMeter); addAndMakeVisible (outMeter);
}

void BottomBar::tick()
{
    const float i = jmax (ui.inDb[0], ui.inDb[1]), o = jmax (ui.outDb[0], ui.outDb[1]);
    inMeter.setLevelDb (i); outMeter.setLevelDb (o);
    inHold = i > inHold ? i : inHold - 0.4f; outHold = o > outHold ? o : outHold - 0.4f;
    repaint (380, 0, 120, getHeight()); repaint (1580, 0, 120, getHeight()); repaint (1000, 0, 140, getHeight());
}

void BottomBar::paint (Graphics& g)
{
    SauceDraw::panel (g, getLocalBounds().toFloat().reduced (1), 10);
    g.setColour (SauceColours::text); g.setFont (SauceFonts::medium (19.0f));
    g.drawText ("INPUT", 92, 0, 80, getHeight(), Justification::centredLeft);
    g.drawText ("DRY / WET", 610, 0, 120, getHeight(), Justification::centredLeft);
    g.drawText ("OUTPUT", 1262, 0, 100, getHeight(), Justification::centredLeft);
    auto fmt = [] (float d) { return d <= -99.0f ? String ("-inf dB") : String (d, 1) + " dB"; };
    g.drawText (fmt (inHold), 410, 0, 90, getHeight(), Justification::centredLeft);
    g.drawText (fmt (outHold), 1606, 0, 90, getHeight(), Justification::centredLeft);
    g.drawText (String (roundToInt (proc.reader.get (ParamIDs::dryWet))) + "%", 1028, 0, 80, getHeight(), Justification::centredLeft);
    g.setColour (SauceColours::border);
    g.drawVerticalLine (536, 12, (float) getHeight() - 12); g.drawVerticalLine (1136, 12, (float) getHeight() - 12);
}

void BottomBar::resized()
{
    inKnob.setBounds (26, 10, 50, 50);
    inMeter.setBounds (170, 26, 228, 18);
    dryWet.setBounds (720, 14, 290, 42);
    outKnob.setBounds (1196, 10, 50, 50);
    outMeter.setBounds (1356, 26, 238, 18);
}
