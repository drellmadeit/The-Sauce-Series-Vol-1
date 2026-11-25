#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace SauceColours
{
    const juce::Colour bg        { 0xff040609 };
    const juce::Colour panelTop  { 0xff10141a };
    const juce::Colour panelBot  { 0xff07090c };
    const juce::Colour border    { 0xff252b34 };
    const juce::Colour borderHi  { 0xff3a424e };
    const juce::Colour neon      { 0xff1f8fff };
    const juce::Colour neonHi    { 0xff52b4ff };
    const juce::Colour neonDeep  { 0xff0b4fd8 };
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
