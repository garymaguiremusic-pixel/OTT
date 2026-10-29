#include "CustomLookAndFeel.h"
#include "UI/OttComponents.h"

using namespace juce;

//==============================================================================
CustomLookAndFeel::CustomLookAndFeel()
{
    setColour (ComboBox::textColourId,                  Colours::white);
    setColour (ComboBox::backgroundColourId,            Theme::insetBottom);
    setColour (ComboBox::outlineColourId,               Theme::bevelDark);
    setColour (ComboBox::arrowColourId,                 Colour (0xffd0d4d8));
    setColour (PopupMenu::backgroundColourId,           Colour (0xff1b1e21));
    setColour (PopupMenu::textColourId,                 Colour (0xffe8ebee));
    setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xff2a6fb8));
    setColour (PopupMenu::highlightedTextColourId,      Colours::white);
    setColour (PopupMenu::headerTextColourId,           Theme::labelText);
    setColour (TextButton::textColourOffId,             Colour (0xffdfe3e6));
    setColour (TextButton::textColourOnId,              Colours::white);
    setColour (TooltipWindow::textColourId,             Colours::white);
    setColour (TooltipWindow::backgroundColourId,       Colour (0xf0163f78));
    setColour (TooltipWindow::outlineColourId,          Theme::blue);
}

Font CustomLookAndFeel::uiFont (float height, bool bold)
{
    return Font (FontOptions (height, bold ? Font::bold : Font::plain));
}

//==============================================================================
void CustomLookAndFeel::drawTicks (Graphics& g, Point<float> c, float rInner, float rOuter,
                                   float start, float end, int count, Colour colour, float thickness)
{
    g.setColour (colour);
    for (int i = 0; i < count; ++i)
    {
        const float a = start + (end - start) * (float) i / (float) (count - 1);
        const float s = std::sin (a), co = -std::cos (a);
        const bool major = (i % 5) == 0;
        const float r0 = major ? rInner - 1.5f : rInner;
        g.drawLine (c.x + s * r0, c.y + co * r0, c.x + s * rOuter, c.y + co * rOuter,
                    major ? thickness * 1.4f : thickness);
    }
}

void CustomLookAndFeel::drawKnob (Graphics& g, Rectangle<float> area, float pos, float origin,
                                  float start, float end, bool enabled)
{
    const float size = std::min (area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float R = size * 0.5f - 1.0f;
    const float angle = start + pos * (end - start);
    const float originAngle = start + origin * (end - start);

    drawTicks (g, c, R - 3.5f, R, start, end, 21, Colour (0xff8d949b).withAlpha (0.8f), 0.8f);

    // Blue arc ring: dark recessed track, glow and bright value arc
    const float ringR = R - 7.0f;
    {
        Path track;
        track.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, start, end, true);
        g.setColour (Colour (0xff0a0c0e));
        g.strokePath (track, PathStrokeType (5.5f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colour (0xff1a2633));
        g.strokePath (track, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));

        if (std::abs (angle - originAngle) > 0.01f)
        {
            Path value;
            value.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, std::min (originAngle, angle), std::max (originAngle, angle), true);
            const float alpha = enabled ? 1.0f : 0.35f;
            g.setColour (Theme::blueGlow.withAlpha (0.18f * alpha));
            g.strokePath (value, PathStrokeType (9.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (Theme::blueGlow.withAlpha (0.35f * alpha));
            g.strokePath (value, PathStrokeType (5.5f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (Theme::blue.withAlpha (alpha));
            g.strokePath (value, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (Colour (0xffc9e6ff).withAlpha (0.8f * alpha));
            g.strokePath (value, PathStrokeType (1.0f, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    // Rubberised cap (Virus small-knob style)
    const float bodyR = ringR - 5.5f;
    const auto body = Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);

    Path bodyPath;
    bodyPath.addEllipse (body);
    DropShadow (Colours::black.withAlpha (0.8f), 6, { 0, 3 }).drawForPath (g, bodyPath);

    g.setGradientFill (ColourGradient (Colour (0xff6a6f75), body.getX(), body.getY(),
                                       Colour (0xff141618), body.getX(), body.getBottom(), false));
    g.fillEllipse (body);

    const auto cap = body.reduced (bodyR * 0.14f);
    g.setGradientFill (ColourGradient (Colour (0xff4a4f55), cap.getCentreX(), cap.getY(),
                                       Colour (0xff1f2225), cap.getCentreX(), cap.getBottom(), false));
    g.fillEllipse (cap);

    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.12f), cap.getCentreX(), cap.getY(),
                                       Colours::white.withAlpha (0.0f), cap.getCentreX(), cap.getCentreY(), false));
    g.fillEllipse (cap.reduced (1.0f));

    g.setColour (Colours::black.withAlpha (0.8f));
    g.drawEllipse (body, 0.8f);

    // indicator line
    const float s = std::sin (angle), co = -std::cos (angle);
    const float p0 = bodyR * 0.35f, p1 = bodyR * 0.92f;
    g.setColour (Colour (0xffe6e9ec).withAlpha (enabled ? 1.0f : 0.5f));
    g.drawLine (c.x + s * p0, c.y + co * p0, c.x + s * p1, c.y + co * p1, 1.8f);
}

//==============================================================================
void CustomLookAndFeel::drawInsetBox (Graphics& g, Rectangle<float> b, float corner)
{
    g.setColour (Theme::bevelLight.withAlpha (0.12f));
    g.drawRoundedRectangle (b.translated (0.0f, 1.0f), corner, 1.0f);

    g.setGradientFill (ColourGradient (Theme::insetTop, 0.0f, b.getY(), Theme::insetBottom, 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, corner);

    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawHorizontalLine ((int) b.getY() + 1, b.getX() + corner, b.getRight() - corner);
    g.setColour (Theme::bevelDark);
    g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);
}

void CustomLookAndFeel::drawComboBox (Graphics& g, int width, int height, bool,
                                      int, int, int, int, ComboBox& box)
{
    const auto b = Rectangle<int> (width, height).toFloat();
    drawInsetBox (g, b);

    if (box.hasKeyboardFocus (false) || box.isPopupActive())
    {
        g.setColour (Theme::blue.withAlpha (0.45f));
        g.drawRoundedRectangle (b.reduced (1.0f), 3.0f, 1.0f);
    }

    // small down arrow
    const float ax = (float) width - 12.0f, ay = (float) height * 0.5f;
    Path arrow;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (ComboBox::arrowColourId));
    g.fillPath (arrow);
}

Font CustomLookAndFeel::getComboBoxFont (ComboBox&)        { return uiFont (12.5f, true); }
Font CustomLookAndFeel::getPopupMenuFont()                  { return uiFont (13.0f); }

void CustomLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (4, 1, box.getWidth() - 22, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (Justification::centredLeft);
}

void CustomLookAndFeel::drawPopupMenuBackground (Graphics& g, int width, int height)
{
    g.fillAll (findColour (PopupMenu::backgroundColourId));
    g.setColour (Theme::blue.withAlpha (0.35f));
    g.drawRect (0, 0, width, height, 1);
}

//==============================================================================
void CustomLookAndFeel::drawButtonBackground (Graphics& g, Button& button, const Colour&,
                                              bool isHighlighted, bool isDown)
{
    auto b = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();

    Colour top = on ? Colour (0xff2f86e0) : Colour (0xff6b7076);
    Colour bottom = on ? Colour (0xff15508f) : Colour (0xff3c4045);
    if (isHighlighted) { top = top.brighter (0.12f); bottom = bottom.brighter (0.12f); }
    if (isDown)        std::swap (top, bottom);

    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (b.translated (0.0f, 1.0f), 3.0f);
    g.setGradientFill (ColourGradient (top, 0.0f, b.getY(), bottom, 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine ((int) b.getY() + 1, b.getX() + 3.0f, b.getRight() - 3.0f);
    g.setColour (Theme::bevelDark);
    g.drawRoundedRectangle (b, 3.0f, 1.0f);
}

Font CustomLookAndFeel::getTextButtonFont (TextButton&, int)  { return uiFont (11.5f, true); }

void CustomLookAndFeel::drawButtonText (Graphics& g, TextButton& button, bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.findColour (button.getToggleState() ? TextButton::textColourOnId : TextButton::textColourOffId)
                     .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.drawText (button.getButtonText(), button.getLocalBounds(), Justification::centred);
}

//==============================================================================
void CustomLookAndFeel::drawTooltip (Graphics& g, const String& text, int width, int height)
{
    const auto b = Rectangle<int> (width, height).toFloat();
    g.setColour (findColour (TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (findColour (TooltipWindow::outlineColourId).withAlpha (0.7f));
    g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);
    g.setColour (findColour (TooltipWindow::textColourId));
    g.setFont (uiFont (12.5f));
    g.drawFittedText (text, b.reduced (6.0f, 2.0f).toNearestInt(), Justification::centredLeft, 3);
}

//==============================================================================
void CustomLookAndFeel::drawPanel (Graphics& g, Rectangle<float> b, const String& title)
{
    constexpr float corner = 5.0f, headerH = 18.0f;

    // recess: light bevel below/right, dark above/left
    g.setColour (Theme::bevelLight);
    g.drawRoundedRectangle (b.translated (0.5f, 1.0f), corner, 1.0f);

    g.setGradientFill (ColourGradient (Theme::panelTop, 0.0f, b.getY(), Theme::panelBottom, 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, corner);

    // header bar
    Path header;
    const auto h = b.withHeight (headerH);
    header.addRoundedRectangle (h.getX(), h.getY(), h.getWidth(), h.getHeight(), corner, corner, true, true, false, false);
    g.setGradientFill (ColourGradient (Theme::headerTop, 0.0f, h.getY(), Theme::headerBottom, 0.0f, h.getBottom(), false));
    g.fillPath (header);
    g.setColour (Colours::white.withAlpha (0.25f));
    g.drawHorizontalLine ((int) h.getY() + 1, h.getX() + corner, h.getRight() - corner);
    g.setColour (Theme::bevelDark);
    g.drawHorizontalLine ((int) h.getBottom(), h.getX(), h.getRight());
    g.setColour (Colours::white.withAlpha (0.07f));
    g.drawHorizontalLine ((int) h.getBottom() + 1, h.getX() + 1.0f, h.getRight() - 1.0f);

    g.setColour (Colours::black.withAlpha (0.45f));
    g.setFont (uiFont (10.5f, true));
    g.drawText (title.toUpperCase(), h.translated (0.0f, 1.0f), Justification::centred);
    g.setColour (Theme::headerText);
    g.drawText (title.toUpperCase(), h, Justification::centred);

    g.setColour (Theme::bevelDark);
    g.drawRoundedRectangle (b, corner, 1.0f);
}

void CustomLookAndFeel::drawLcd (Graphics& g, Rectangle<float> b, bool dimmed)
{
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (b.expanded (1.0f), 3.0f);

    const auto top = dimmed ? Theme::lcdDimTop : Theme::lcdTop;
    const auto bottom = dimmed ? Theme::lcdDimBottom : Theme::lcdBottom;
    g.setGradientFill (ColourGradient (top, 0.0f, b.getY(), bottom, 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, 2.5f);

    // glass highlight + faint scanlines
    g.setColour (Colours::white.withAlpha (0.10f));
    g.fillRoundedRectangle (b.withHeight (b.getHeight() * 0.45f).reduced (1.0f, 1.0f), 2.0f);
    g.setColour (Colours::black.withAlpha (0.06f));
    for (float yy = b.getY() + 2.0f; yy < b.getBottom() - 1.0f; yy += 2.0f)
        g.drawHorizontalLine ((int) yy, b.getX() + 1.0f, b.getRight() - 1.0f);

    g.setColour (Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (b, 2.5f, 1.0f);
}

void CustomLookAndFeel::drawSmallLabel (Graphics& g, const String& text, Rectangle<float> area, Justification j)
{
    g.setFont (uiFont (10.0f, true));
    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawText (text.toUpperCase(), area.translated (0.0f, 1.0f), j);
    g.setColour (Theme::labelText);
    g.drawText (text.toUpperCase(), area, j);
}
