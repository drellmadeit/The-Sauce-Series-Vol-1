#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace SauceColours
{
    const juce::Colour bg        { 0xff040609 };
    const juce::Colour panelTop  { 0xec11161d };    // slightly see-through so the spray wall shows at the edges
    const juce::Colour panelBot  { 0xf206080c };
    const juce::Colour border    { 0xff26303c };
    const juce::Colour borderHi  { 0xff3c4a5a };
    const juce::Colour neon      { 0xff12a6ff };    // 1.5: brighter electric blue
    const juce::Colour neonHi    { 0xff5fd4ff };
    const juce::Colour neonDeep  { 0xff1060ff };
    const juce::Colour spray     { 0xff00e5ff };    // cyan highlight for tags and splatter
    const juce::Colour text      { 0xffeef3f8 };
    const juce::Colour textDim   { 0xff8c96a5 };
    const juce::Colour field     { 0xff0a0d11 };
    const juce::Colour warn      { 0xffffb020 };
}

namespace SauceFonts
{
    juce::Font regular (float h);
    juce::Font medium (float h);
    juce::Font bold (float h);
    juce::Font brand (float h);
}

/** Draw helpers shared by every panel so the whole UI reads as one system. */
namespace SauceDraw
{
    void panel (juce::Graphics&, juce::Rectangle<float>, float radius = 10.0f, bool glowWhite = false);
    void glowPath (juce::Graphics&, const juce::Path&, juce::Colour, float strokeWidth, int glowRadius);
    void neonRoundedRect (juce::Graphics&, juce::Rectangle<float>, float radius, juce::Colour, float width, int glowRadius);
    void fieldBox (juce::Graphics&, juce::Rectangle<float>, float radius = 6.0f);
    juce::Path icon (const juce::String& name, juce::Rectangle<float> area);   // vector icons used by buttons/tabs
    /** Graffiti tag lettering: jittered chunky italic letters, dark outline, blue 3D drop, white-to-electric fill, drips.
        `x, baseline` place the text; returns the width used. */
    float tagText (juce::Graphics&, const juce::String& text, float x, float baseline, float height, bool drips = true, bool centred = false);
    /** The spray-painted brick wall behind the whole UI (drawn once into an image). */
    juce::Image sprayWall (int w, int h);
    /** A little spray splatter burst (accent marks on panel corners). */
    void splatter (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour, int seed);
}

class SauceLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SauceLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getLabelFont (juce::Label&) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Font getAlertWindowTitleFont() override   { return SauceFonts::bold (18.0f); }
    juce::Font getAlertWindowMessageFont() override { return SauceFonts::regular (14.0f); }
};
