#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
namespace Theme
{
    // Black, with orange as the only accent, light grey for text and labels and white for
    // highlights (the LeadVerb theme).
    const juce::Colour background   { 0xff0e0e0f };
    const juce::Colour panelTop     { 0xff1b1b1d };
    const juce::Colour panelBottom  { 0xff111112 };
    const juce::Colour headerTop    { 0xff2e2e31 };
    const juce::Colour headerBottom { 0xff1e1e20 };
    const juce::Colour headerText   { 0xffd9d9db };
    const juce::Colour labelText    { 0xffb4b4b8 };
    const juce::Colour lightGrey    { 0xffc8c8cc };
    const juce::Colour bevelDark    { 0xff000000 };
    const juce::Colour bevelLight   { 0x22ffffff };
    const juce::Colour insetTop     { 0xff050505 };
    const juce::Colour insetBottom  { 0xff141415 };
    const juce::Colour lcdTop       { 0xff1c1714 };
    const juce::Colour lcdBottom    { 0xff0b0908 };
    const juce::Colour lcdDimTop    { 0xff171718 };
    const juce::Colour lcdDimBottom { 0xff0c0c0d };
    const juce::Colour accent       { 0xffff7a1a };   // orange: the one accent colour
    const juce::Colour accentGlow   { 0xffe8600c };   // glow around orange elements
    const juce::Colour accentLight  { 0xffffd2a8 };   // hot core of orange arcs, lit elements
    const juce::Colour red          { 0xffff3b30 };   // clip / warning only

    // Fixed shades
    const juce::Colour lightText       { 0xffdfe3e6 };   // button text, help text
    const juce::Colour dimText         { 0xff6e6e72 };   // text on inactive LCDs
    const juce::Colour orangeTop       { 0xffff8a2a };   // orange gradient: toggled buttons, meters
    const juce::Colour orangeBottom    { 0xffc4520a };
    const juce::Colour darkButtonTop   { 0xff343437 };
    const juce::Colour darkButtonBottom{ 0xff1a1a1c };
    const juce::Colour popupBackground { 0xff121213 };
    const juce::Colour windowTop       { 0xff1a1a1c };   // window background gradient
    const juce::Colour windowBottom    { 0xff070708 };
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
