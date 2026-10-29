#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
namespace Theme
{
    // Same palette as VIRUS DELAY
    const juce::Colour background   { 0xff2a2d31 };
    const juce::Colour panelTop     { 0xff34373b };
    const juce::Colour panelBottom  { 0xff25282b };
    const juce::Colour headerTop    { 0xff74797f };
    const juce::Colour headerBottom { 0xff50555a };
    const juce::Colour headerText   { 0xffe3e6e8 };
    const juce::Colour labelText    { 0xffb9bec3 };
    const juce::Colour bevelDark    { 0xff0e0f11 };
    const juce::Colour bevelLight   { 0x33ffffff };
    const juce::Colour insetTop     { 0xff0b0c0e };
    const juce::Colour insetBottom  { 0xff1c1f22 };
    const juce::Colour lcdTop       { 0xff2a74c9 };
    const juce::Colour lcdBottom    { 0xff144a92 };
    const juce::Colour lcdDimTop    { 0xff40464d };
    const juce::Colour lcdDimBottom { 0xff2b3035 };
    const juce::Colour blue         { 0xff3fa9ff };
    const juce::Colour blueGlow     { 0xff1f7fe0 };
    const juce::Colour red          { 0xffff3b30 };
    const juce::Colour orange       { 0xffffa53a };   // threshold handles / gain markers
    const juce::Colour upRegion     { 0xff1d3a57 };   // below threshold (upward)
    const juce::Colour downRegion   { 0xff4a2b28 };   // above threshold (downward)
}

//==============================================================================
/**
    Draws every control procedurally (gradients, paths, drop shadows), so the
    plugin needs no image assets.
*/
class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel();

    // Fonts ------------------------------------------------------------------------
    static juce::Font uiFont (float height, bool bold = false);

    // Knob (drawn by OttKnob) -------------------------------------------------------
    static void drawKnob (juce::Graphics&, juce::Rectangle<float> area, float pos, float origin,
                          float start, float end, bool enabled);

    // ComboBox / popup ---------------------------------------------------------------
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    // Buttons ------------------------------------------------------------------------
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isHighlighted, bool isDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    // Tooltip ------------------------------------------------------------------------
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    // Panel helpers (used by the editor and custom components) ------------------------
    static void drawPanel (juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& title);
    static void drawInsetBox (juce::Graphics&, juce::Rectangle<float> bounds, float corner = 3.0f);
    static void drawLcd (juce::Graphics&, juce::Rectangle<float> bounds, bool dimmed);
    static void drawSmallLabel (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area,
                                juce::Justification = juce::Justification::centred);

private:
    static void drawTicks (juce::Graphics&, juce::Point<float> centre, float rInner, float rOuter,
                           float start, float end, int count, juce::Colour, float thickness);
};
