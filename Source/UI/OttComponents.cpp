#include "OttComponents.h"
#include "../CustomLookAndFeel.h"
#include "../Parameters.h"
#include "../PluginProcessor.h"

using namespace juce;

namespace
{
    constexpr float kRotaryStart = MathConstants<float>::pi * 1.25f;
    constexpr float kRotaryEnd   = MathConstants<float>::pi * 2.75f;

    float dragPixelsForFullRange (const ModifierKeys& mods)
    {
        return mods.isShiftDown() ? 1500.0f : 200.0f;
    }
}

//==============================================================================
// ParamControl
//==============================================================================
ParamControl::ParamControl (RangedAudioParameter& p)
    : param (p),
      attachment (p, [this] (float v) { value = v; repaint(); })
{
    attachment.sendInitialUpdate();
    setRepaintsOnMouseActivity (true);
}

ParamControl::~ParamControl() = default;

String ParamControl::getValueText() const
{
    if (formatter)
        return formatter (value);
    return param.getText (param.convertTo0to1 (value), 32);
}

String ParamControl::getTooltip()
{
    return param.getName (64) + ":  " + getValueText()
         + "\nWheel = 1.0 steps, Shift + wheel = 0.1. Double-click = reset, right-click = type a value.";
}

void ParamControl::sendStatus()
{
    if (onStatus)
        onStatus (param.getName (64).toUpperCase() + "   " + getValueText());
}

void ParamControl::setValueFromUser (float newValue)
{
    const auto range = param.getNormalisableRange();
    const float oldValue = value;
    newValue = jlimit (range.start, range.end, newValue);
    attachment.setValueAsCompleteGesture (newValue);
    if (onUserChange)
        onUserChange (oldValue, newValue);
    sendStatus();
}

int ParamControl::wheelSteps (const MouseWheelDetails& wheel, float& accumulator)
{
    // Use whichever axis moved (some systems turn shift + wheel into horizontal scrolling).
    float delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
    if (wheel.isReversed)
        delta = -delta;

    if (std::abs (delta) < 1.0e-6f)
        return 0;

    // A real mouse wheel: exactly one step per notch.
    if (! wheel.isSmooth)
        return delta > 0.0f ? 1 : -1;

    // Trackpads send many small deltas; turn them into steps.
    constexpr float stepThreshold = 0.12f;
    accumulator += delta;
    int steps = 0;
    while (accumulator >= stepThreshold)  { ++steps; accumulator -= stepThreshold; }
    while (accumulator <= -stepThreshold) { --steps; accumulator += stepThreshold; }
    return steps;
}

void ParamControl::mouseDown (const MouseEvent& e)
{
    if (! isEnabled())
        return;

    if (e.mods.isPopupMenu() || e.mods.isCommandDown())
    {
        showTextEditor();
        return;
    }

    dragging = true;
    dragProportion = getProportion();
    lastDragY = e.position.y;
    attachment.beginGesture();
    sendStatus();
    repaint();
}

void ParamControl::mouseDrag (const MouseEvent& e)
{
    if (! dragging)
        return;

    // Incremental, so pressing or releasing shift mid-drag never jumps.
    dragProportion = jlimit (0.0f, 1.0f, dragProportion + (lastDragY - e.position.y) / dragPixelsForFullRange (e.mods));
    lastDragY = e.position.y;
    const float oldValue = value;
    const float newValue = param.convertFrom0to1 (dragProportion);
    attachment.setValueAsPartOfGesture (newValue);
    if (onUserChange)
        onUserChange (oldValue, newValue);
    sendStatus();
}

void ParamControl::mouseUp (const MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;
    attachment.endGesture();
    repaint();
}

void ParamControl::mouseDoubleClick (const MouseEvent& e)
{
    if (isEnabled() && ! e.mods.isPopupMenu())
        setValueFromUser (resetValue.has_value() ? *resetValue : param.convertFrom0to1 (param.getDefaultValue()));
}

void ParamControl::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    if (! isEnabled())
        return;

    if (const int steps = wheelSteps (wheel, wheelAccumulator); steps != 0)
        setValueFromUser (value + (float) steps * wheelStepSize (e.mods));
}

void ParamControl::mouseEnter (const MouseEvent&) { hovering = true;  sendStatus(); repaint(); }
void ParamControl::mouseExit (const MouseEvent&)
{
    hovering = false;
    if (onStatus) onStatus ({});
    repaint();
}

void ParamControl::showTextEditor()
{
    editor = std::make_unique<TextEditor>();
    editor->setFont (CustomLookAndFeel::uiFont (12.0f, true));
    editor->setJustification (Justification::centred);
    editor->setColour (TextEditor::backgroundColourId, Colour (0xff0b0c0e));
    editor->setColour (TextEditor::textColourId, Colours::white);
    editor->setColour (TextEditor::outlineColourId, Theme::blue);
    editor->setColour (TextEditor::focusedOutlineColourId, Theme::blue);
    editor->setColour (TextEditor::highlightColourId, Theme::blueGlow.withAlpha (0.6f));
    editor->setBounds (getTextEditorArea());
    editor->setText (getValueText().upToFirstOccurrenceOf (" ", false, false), false);
    addAndMakeVisible (*editor);
    editor->selectAll();
    editor->grabKeyboardFocus();

    auto close = [safe = SafePointer<ParamControl> (this)]
    {
        // Deleting the editor inside its own callback isn't safe, so do it afterwards.
        MessageManager::callAsync ([safe] { if (safe != nullptr) safe->editor.reset(); });
    };

    editor->onReturnKey = [this, close]
    {
        const auto text = editor->getText().trim();
        if (text.isNotEmpty())
            setValueFromUser (param.convertFrom0to1 (param.getValueForText (text)));
        close();
    };
    editor->onEscapeKey = close;
    editor->onFocusLost = close;
}

//==============================================================================
// OttKnob
//==============================================================================
OttKnob::OttKnob (RangedAudioParameter& p, const String& t, bool isBipolar)
    : ParamControl (p), title (t), bipolar (isBipolar)
{
}

Rectangle<int> OttKnob::getTextEditorArea() const
{
    return getLocalBounds().removeFromBottom (18).reduced (2, 0);
}

void OttKnob::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    if (title.isNotEmpty())
        CustomLookAndFeel::drawSmallLabel (g, title, b.removeFromTop (14.0f));

    auto valueArea = showValue ? b.removeFromBottom (16.0f) : Rectangle<float>();
    const float side = std::min (b.getWidth(), b.getHeight());
    const auto knobArea = b.withSizeKeepingCentre (side, side);

    const float origin = bipolar ? param.convertTo0to1 (jlimit (param.getNormalisableRange().start,
                                                                param.getNormalisableRange().end, 0.0f))
                                 : 0.0f;
    CustomLookAndFeel::drawKnob (g, knobArea, getProportion(), origin, kRotaryStart, kRotaryEnd, isEnabled());

    if (! showValue)
        return;

    g.setFont (CustomLookAndFeel::uiFont (11.5f, true));
    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawText (getValueText(), valueArea.translated (0.0f, 1.0f), Justification::centred);
    g.setColour ((hovering || dragging) ? Theme::blue : Colour (0xffdfe3e6));
    g.drawText (getValueText(), valueArea, Justification::centred);
}

//==============================================================================
// ValueField
//==============================================================================
ValueField::ValueField (RangedAudioParameter& p, Style s)
    : ParamControl (p), style (s)
{
    setMouseCursor (MouseCursor::UpDownResizeCursor);
}

void ValueField::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    if (style == Style::Lcd)
    {
        CustomLookAndFeel::drawLcd (g, b.reduced (1.0f), ! isEnabled());
        g.setFont (CustomLookAndFeel::uiFont (12.0f, true));
        g.setColour (Colours::black.withAlpha (0.35f));
        g.drawText (getValueText(), b.translated (0.0f, 1.0f), Justification::centred);
        g.setColour (isEnabled() ? Colours::white : Colour (0xff8a9097));
        g.drawText (getValueText(), b, Justification::centred);
    }
    else
    {
        if (hovering || dragging)
        {
            g.setColour (Theme::blue.withAlpha (0.12f));
            g.fillRoundedRectangle (b, 3.0f);
        }
        g.setFont (CustomLookAndFeel::uiFont (13.0f, true));
        g.setColour (Theme::blue.withAlpha (isEnabled() ? 1.0f : 0.4f));
        g.drawText (getValueText(), b, Justification::centredRight);
    }
}

//==============================================================================
// PowerButton
//==============================================================================
void PowerButton::paintButton (Graphics& g, bool isHighlighted, bool isDown)
{
    getLookAndFeel().drawButtonBackground (g, *this, Colours::transparentBlack, isHighlighted, isDown);

    const auto b = getLocalBounds().toFloat();
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.26f;
    const auto c = b.getCentre();

    Path icon;
    icon.addCentredArc (c.x, c.y, r, r, 0.0f, 0.75f, MathConstants<float>::twoPi - 0.75f, true);
    icon.startNewSubPath (c.x, c.y - r - 1.5f);
    icon.lineTo (c.x, c.y - 0.5f);

    g.setColour ((getToggleState() ? Colours::white : Colour (0xff9aa1a8)).withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f));
    g.strokePath (icon, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

//==============================================================================
// DynamicsDisplay
//==============================================================================
DynamicsDisplay::DynamicsDisplay (OttAudioProcessor& p) : processor (p)
{
}

void DynamicsDisplay::setLaneBounds (const std::array<Rectangle<float>, 3>& l)
{
    lanes = l;
    repaint();
}

float DynamicsDisplay::get (const String& id) const
{
    return processor.apvts.getRawParameterValue (id)->load();
}

bool DynamicsDisplay::bandEnabled (int band) const
{
    if (band == ott::BandHigh) return get (ott::ParamIDs::highOn) >= 0.5f;
    if (band == ott::BandLow)  return get (ott::ParamIDs::lowOn) >= 0.5f;
    return true;
}

float DynamicsDisplay::downThresholdDb (int band) const
{
    return get (ott::bandParam (band, ott::BandIDs::threshold)) - ott::Engine::kDownThresholdDb[(size_t) band];
}

float DynamicsDisplay::upThresholdDb (int band) const
{
    return get (ott::bandParam (band, ott::BandIDs::threshold)) - ott::Engine::kUpThresholdDb[(size_t) band];
}

bool DynamicsDisplay::downwardActive (int band) const
{
    return get (ott::ParamIDs::depth) > 0.05f && get (ott::ParamIDs::downward) > 0.05f
        && get (ott::bandParam (band, ott::BandIDs::bypDown)) < 0.5f;
}

bool DynamicsDisplay::upwardActive (int band) const
{
    return get (ott::ParamIDs::depth) > 0.05f && get (ott::ParamIDs::upward) > 0.05f
        && get (ott::bandParam (band, ott::BandIDs::bypUp)) < 0.5f;
}

float DynamicsDisplay::xForDb (float db, const Rectangle<float>& meter) const
{
    return meter.getX() + meter.getWidth() * (jlimit (ott::kMinDb, 0.0f, db) - ott::kMinDb) / -ott::kMinDb;
}

int DynamicsDisplay::bandAt (Point<float> pos) const
{
    for (int b = 0; b < 3; ++b)
        if (lanes[(size_t) b].expanded (3.0f).contains (pos))
            return b;
    return -1;
}

String DynamicsDisplay::describe (int band) const
{
    if (band < 0)
        return {};
    const auto& m = meterState[(size_t) band];
    return String (ott::bandName (band)).toUpperCase()
         + "   LEVEL " + (m.envelope <= ott::kMinDb + 0.1f ? String ("-inf") : String (m.envelope, 1))
         + "   DOWN THR " + String (downThresholdDb (band), 1)
         + "   UP THR " + String (upThresholdDb (band), 1) + " dB";
}

String DynamicsDisplay::getTooltip()
{
    return hoverBand < 0 ? String()
                         : describe (hoverBand) + "\nGrey = level, orange tick = down threshold, blue tick = up threshold.";
}

void DynamicsDisplay::mouseMove (const MouseEvent& e)
{
    hoverBand = bandAt (e.position);
    if (onStatus) onStatus (describe (hoverBand));
}

void DynamicsDisplay::mouseExit (const MouseEvent&)
{
    hoverBand = -1;
    if (onStatus) onStatus ({});
}

void DynamicsDisplay::updateMeters (float dt)
{
    // Calm ballistics so the bars read as a level, not a flicker: quick rise, slow fall.
    const float rise = 1.0f - std::exp (-dt / 0.03f), fall = 1.0f - std::exp (-dt / 0.35f);
    const float gainGlide = 1.0f - std::exp (-dt / 0.12f);
    for (int b = 0; b < 3; ++b)
    {
        auto& m = processor.meters[(size_t) b];
        auto& s = meterState[(size_t) b];
        const float env = m.envelopeDb.load();
        s.envelope += (env - s.envelope) * (env > s.envelope ? rise : fall);
        s.gain   += (m.gainDb.load() - s.gain) * gainGlide;
        s.makeup += (m.makeupDb.load() - s.makeup) * gainGlide;
    }
    repaint();
}

void DynamicsDisplay::paint (Graphics& g)
{
    for (int b = 0; b < 3; ++b)
        paintLane (g, b, lanes[(size_t) b]);
}

void DynamicsDisplay::paintLane (Graphics& g, int band, Rectangle<float> lane)
{
    CustomLookAndFeel::drawInsetBox (g, lane.expanded (3.0f, 3.0f), 3.0f);

    if (! bandEnabled (band))
    {
        g.setFont (CustomLookAndFeel::uiFont (10.5f, true));
        g.setColour (Theme::labelText.withAlpha (0.6f));
        g.drawText ("BAND OFF", lane, Justification::centred);
        return;
    }

    const auto& m = meterState[(size_t) band];
    auto textArea = lane.removeFromRight (64.0f);
    const auto meter = lane.reduced (2.0f, 0.0f);
    const auto strip = meter.withSizeKeepingCentre (meter.getWidth(), 8.0f);

    // faint dB ticks every 10 dB
    g.setColour (Colours::white.withAlpha (0.06f));
    for (int db = -70; db < 0; db += 10)
        g.drawVerticalLine ((int) xForDb ((float) db, meter), meter.getY() + 2.0f, meter.getBottom() - 2.0f);

    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRect (strip);

    // the level against the thresholds
    if (m.envelope > ott::kMinDb + 0.5f)
    {
        const float inX = xForDb (m.envelope, meter);
        const float endX = xForDb (m.envelope + m.gain, meter);
        g.setColour (Colours::white.withAlpha (0.28f));
        g.fillRect (strip.withRight (std::min (inX, endX)));
        if (std::abs (endX - inX) > 0.5f)
        {
            g.setColour (endX < inX ? Theme::orange.withAlpha (0.85f) : Colour (0xff8fc7ff).withAlpha (0.85f));
            g.fillRect (Rectangle<float>::leftTopRightBottom (std::min (inX, endX), strip.getY(), std::max (inX, endX), strip.getBottom()));
        }
        g.setColour (Colours::white);
        g.fillRect (endX - 1.0f, strip.getY() - 3.0f, 2.0f, strip.getHeight() + 6.0f);
    }

    // threshold ticks: up (blue) and down (orange), above and below the strip
    auto drawTick = [&] (float db, Colour colour)
    {
        const float x = xForDb (db, meter);
        g.setColour (colour);
        g.fillRect (x - 0.75f, meter.getY() + 2.0f, 1.5f, strip.getY() - meter.getY() - 3.0f);
        g.fillRect (x - 0.75f, strip.getBottom() + 1.0f, 1.5f, meter.getBottom() - strip.getBottom() - 3.0f);
    };
    if (upwardActive (band))   drawTick (upThresholdDb (band),   Colour (0xff8fc7ff));
    if (downwardActive (band)) drawTick (downThresholdDb (band), Theme::orange);

    // gain change readout
    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawVerticalLine ((int) textArea.getX(), textArea.getY() + 3.0f, textArea.getBottom() - 3.0f);
    g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
    g.setColour (Colours::white.withAlpha (0.8f));
    const String text = (m.gain >= 0.05f ? "+" : "") + String (m.gain, 1) + " dB";
    if (get (ott::bandParam (band, ott::BandIDs::autoGain)) >= 0.5f)
    {
        g.drawText (text, textArea.withTrimmedBottom (textArea.getHeight() * 0.45f), Justification::centredBottom);
        g.setFont (CustomLookAndFeel::uiFont (10.0f, true));
        g.setColour (Theme::blue);
        g.drawText ("A " + String (m.makeup >= 0.0f ? "+" : "") + String (m.makeup, 1),
                    textArea.withTrimmedTop (textArea.getHeight() * 0.55f), Justification::centredTop);
    }
    else
    {
        g.drawText (text, textArea, Justification::centred);
    }
}

//==============================================================================
// HelpButton / HelpOverlay
//==============================================================================
void HelpButton::paintButton (Graphics& g, bool isHighlighted, bool isDown)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillEllipse (b.translated (0.0f, 1.0f));
    g.setGradientFill (ColourGradient (isHighlighted ? Colour (0xff8a9096) : Colour (0xff6f757b), 0.0f, b.getY(),
                                       Colour (0xff35393d), 0.0f, b.getBottom(), false));
    g.fillEllipse (isDown ? b.reduced (0.5f) : b);
    g.setColour (Theme::bevelDark);
    g.drawEllipse (b, 1.0f);
    g.setColour (Colours::white);
    g.setFont (CustomLookAndFeel::uiFont (12.0f, true));
    g.drawText ("?", b, Justification::centred);
}

void HelpOverlay::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.72f));

    auto box = getLocalBounds().toFloat().reduced (12.0f, 60.0f);
    CustomLookAndFeel::drawPanel (g, box, "OTT - Quick Help");

    const char* lines[] =
    {
        "Three-band upward + downward compressor.",
        "DEPTH = how much of the effect is used (0 % = untouched).  TIME scales every attack / release.",
        "IN GAIN drives the bands, OUT GAIN sets the level.  LINK: In up = Out down by the same amount.",
        "AUTO GAIN: keeps the output as loud (K-weighted, like LUFS) as what comes into the plugin.",
        "BANDS: AMOUNT = how much of that band's gain change is used (0 = none, 150 % = extra).",
        "Meter: grey = level, orange tick = down threshold, blue tick = up threshold, right knob = band gain.",
        "S = solo a band.  A = band auto gain: keeps that band's level when you change its compression.",
        "UPWARD / DOWNWARD = strength of each direction.  LINEAR PHASE: no phase shift between the bands.",
        "ADVANCED: split points, slope, Clean XOV, and per band Threshold, Attack / Release, Byp Up / Down.",
        "RANDOM: new random settings (Bypass, Solo, Linear Phase, LINK and Auto Gain stay as they are).",
        "Knobs: drag (shift = fine), wheel = 1.0 steps, shift + wheel = 0.1, double-click = reset,",
        "right-click = type a value.  Drag the corner or use SIZE to resize.",
        "",
        "Click anywhere to close."
    };

    auto text = box.reduced (14.0f, 0.0f).withTrimmedTop (30.0f);
    g.setFont (CustomLookAndFeel::uiFont (12.0f));
    for (auto* line : lines)
    {
        g.setColour (String (line).startsWith ("Click") ? Theme::blue : Colour (0xffdfe3e6));
        g.drawFittedText (line, text.removeFromTop (24.0f).toNearestInt(), Justification::centredLeft, 1, 0.8f);
    }
}
