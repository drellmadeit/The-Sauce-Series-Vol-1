#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SauceLookAndFeel.h"

using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

/** Rotary knob with a title above and an editable value box below, attached to a parameter. */
class Knob : public juce::Component
{
public:
    enum Size { Big, Medium, Mini };
    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& title, Size sz = Big, bool whiteArc = false)
        : size (sz)
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.getProperties().set ("whiteArc", whiteArc);
        slider.getProperties().set ("mini", sz == Mini);
        const int tbw = sz == Big ? 82 : (sz == Medium ? 70 : 60), tbh = sz == Big ? 30 : (sz == Medium ? 24 : 18);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, tbw, tbh);
        slider.setPopupDisplayEnabled (false, false, nullptr);
        slider.setDoubleClickReturnValue (true, s.getParameter (paramId)->convertFrom0to1 (s.getParameter (paramId)->getDefaultValue()));
        slider.setTooltip (s.getParameter (paramId)->getName (64) + " (double-click to reset, type a value in the box)");
        addAndMakeVisible (slider);
        label.setText (title, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (sz == Big ? SauceFonts::medium (19.0f) : (sz == Medium ? SauceFonts::medium (15.0f) : SauceFonts::medium (12.0f)));
        label.setColour (juce::Label::textColourId, sz == Mini ? SauceColours::textDim : SauceColours::text);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);
        att = std::make_unique<SliderAtt> (s, paramId, slider);
    }
    void setTooltip (const juce::String& t) { slider.setTooltip (t + " (double-click to reset)"); }
    void resized() override
    {
        auto b = getLocalBounds();
        const int lh = size == Big ? 26 : (size == Medium ? 20 : 16);
        label.setBounds (b.removeFromTop (lh));
        slider.setBounds (b);
    }
    juce::Slider slider; juce::Label label;
private:
    Size size; std::unique_ptr<SliderAtt> att;
};

/** Icon button (power, headphones, trash, ...). Optional toggle; neon when on. */
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& iconName, bool circular = false, bool toggles = false)
        : juce::Button (iconName), icon (iconName), round (circular)
    {
        setClickingTogglesState (toggles);
    }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.5f);
        const bool on = getToggleState() && (getClickingTogglesState() || forceToggleLook);
        const float rad = round ? r.getHeight() * 0.5f : 8.0f;
        g.setColour (down ? juce::Colour (0xff141a22) : (on ? juce::Colour (0xff0b1a2e) : SauceColours::field));
        g.fillRoundedRectangle (r, rad);
        if (on) SauceDraw::neonRoundedRect (g, r, rad, SauceColours::neon, 1.6f, 7);
        else { g.setColour (over ? juce::Colour (0xff8a94a3) : juce::Colour (0xff4f5763)); g.drawRoundedRectangle (r, rad, 1.3f); }
        const float inset = r.getHeight() * 0.27f;
        auto p = SauceDraw::icon (icon, r.reduced (inset));
        g.setColour (on ? SauceColours::neonHi : (isEnabled() ? SauceColours::text : SauceColours::textDim));
        g.strokePath (p, juce::PathStrokeType (juce::jmax (1.5f, r.getHeight() * 0.06f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    bool forceToggleLook = false;
private:
    juce::String icon; bool round;
};

/** Neon power switch attached to a module's On parameter. */
class PowerButton : public IconButton
{
public:
    PowerButton (juce::AudioProcessorValueTreeState& s, const juce::String& paramId) : IconButton ("power", false, true)
    {
        att = std::make_unique<ButtonAtt> (s, paramId, *this);
        setTooltip ("Switch " + s.getParameter (paramId)->getName (40).upToLastOccurrenceOf (" On", false, false) + " on/off");
    }
private:
    std::unique_ptr<ButtonAtt> att;
};

/** Two-way segmented toggle (e.g. Spectrum | EQ Only, Stereo | Ping-Pong). */
class Segmented : public juce::Component, public juce::SettableTooltipClient
{
public:
    Segmented (const juce::String& a, const juce::String& b) { names[0] = a; names[1] = b; }
    std::function<void (int)> onChange;
    void setIndex (int i, bool notify = false) { if (i != index) { index = i; repaint(); if (notify && onChange) onChange (i); } }
    int getIndex() const { return index; }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1);
        SauceDraw::fieldBox (g, r, 6);
        for (int i = 0; i < 2; ++i)
        {
            auto seg = r.withWidth (r.getWidth() * 0.5f).withX (r.getX() + r.getWidth() * 0.5f * (float) i).reduced (2);
            if (i == index)
            {
                g.setGradientFill (juce::ColourGradient (SauceColours::neonHi, seg.getX(), seg.getY(), SauceColours::neonDeep, seg.getX(), seg.getBottom(), false));
                g.fillRoundedRectangle (seg, 4);
            }
            g.setColour (i == index ? juce::Colours::black : SauceColours::text);
            g.setFont (SauceFonts::medium (juce::jlimit (11.0f, 16.0f, (float) getHeight() * 0.5f)));
            g.drawText (names[i], seg, juce::Justification::centred);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { setIndex (e.x < getWidth() / 2 ? 0 : 1, true); }
private:
    juce::String names[2]; int index = 0;
};

/** Segmented LED-style meter. Vertical or horizontal. Values in dBFS. */
class SegMeter : public juce::Component
{
public:
    /** Scale used by the meters: the marks 0,-3,-6,-9,-12,-18,-24,-36,-48,-60 are evenly spaced (like the mockup). */
    static float fracForDb (float db)
    {
        static const float m[] = { -60, -48, -36, -24, -18, -12, -9, -6, -3, 0 };
        db = juce::jlimit (-60.0f, 0.0f, db);
        for (int i = 0; i < 9; ++i) if (db <= m[i + 1]) return ((float) i + (db - m[i]) / (m[i + 1] - m[i])) / 9.0f;
        return 1.0f;
    }
    explicit SegMeter (bool vertical = true, int segments = 22) : vert (vertical), segs (segments) {}
    void setLevelDb (float db) { db = juce::jlimit (-60.0f, 6.0f, db); level = db > level ? db : juce::jmax (db, level - 1.2f); repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float gap = 2.0f;
        for (int i = 0; i < segs; ++i)
        {
            const float frac = (float) i / (float) (segs - 1);
            juce::Rectangle<float> s;
            if (vert) { const float sh = (r.getHeight() - gap * (segs - 1)) / (float) segs; s = { r.getX(), r.getBottom() - (float) (i + 1) * sh - (float) i * gap, r.getWidth(), sh }; }
            else      { const float sw = (r.getWidth() - gap * (segs - 1)) / (float) segs; s = { r.getX() + (float) i * (sw + gap), r.getY(), sw, r.getHeight() }; }
            const bool lit = level > -59.5f && fracForDb (level) >= frac - 0.001f;
            juce::Colour c = frac > 0.93f ? juce::Colour (0xffff4d5e) : (frac > 0.8f ? juce::Colours::white : SauceColours::neon.interpolatedWith (SauceColours::neonHi, frac));
            g.setColour (lit ? c : juce::Colour (0xff1a1f27));
            g.fillRoundedRectangle (s, 1.0f);
        }
    }
private:
    bool vert; int segs; float level = -60.0f;
};

/** Small helper to paint a section title with icon. */
inline void drawTitle (juce::Graphics& g, const juce::String& icon, const juce::String& title, juce::Rectangle<float> iconArea, juce::Point<float> textPos, float size = 26.0f)
{
    if (icon.isNotEmpty())
    {
        g.setColour (SauceColours::text);
        g.strokePath (SauceDraw::icon (icon, iconArea), juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    SauceDraw::tagText (g, title, textPos.x, textPos.y + size * 0.98f, size * 1.05f);
}
