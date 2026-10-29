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
         + "\nWheel = 1.0 steps, Shift + wheel = 0.1. Double-click = default, right-click = type a value.";
}

void ParamControl::sendStatus()
{
    if (onStatus)
        onStatus (param.getName (64).toUpperCase() + "   " + getValueText());
}

void ParamControl::setValueFromUser (float newValue)
{
    const auto range = param.getNormalisableRange();
    attachment.setValueAsCompleteGesture (jlimit (range.start, range.end, newValue));
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
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (dragProportion));
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
        setValueFromUser (param.convertFrom0to1 (param.getDefaultValue()));
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

    auto valueArea = b.removeFromBottom (16.0f);
    const float side = std::min (b.getWidth(), b.getHeight());
    const auto knobArea = b.withSizeKeepingCentre (side, side);

    const float origin = bipolar ? param.convertTo0to1 (jlimit (param.getNormalisableRange().start,
                                                                param.getNormalisableRange().end, 0.0f))
                                 : 0.0f;
    CustomLookAndFeel::drawKnob (g, knobArea, getProportion(), origin, kRotaryStart, kRotaryEnd, isEnabled());

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

void DynamicsDisplay::setLaneBounds (const std::array<Rectangle<float>, 3>& l, Rectangle<float> scale)
{
    lanes = l;
    scaleArea = scale;
    repaint();
}

float DynamicsDisplay::get (int band, const char* id) const
{
    return processor.apvts.getRawParameterValue (ott::bandParam (band, id))->load();
}

bool DynamicsDisplay::bandEnabled (int band) const
{
    auto on = [this] (const char* id) { return processor.apvts.getRawParameterValue (id)->load() >= 0.5f; };
    if (band == ott::BandHigh) return on (ott::ParamIDs::highOn);
    if (band == ott::BandLow)  return on (ott::ParamIDs::lowOn);
    return true;
}

bool DynamicsDisplay::showAbove() const
{
    return (int) processor.apvts.getRawParameterValue (ott::ParamIDs::displayMode)->load() != ott::DisplayBottom;
}

bool DynamicsDisplay::showBelow() const
{
    return (int) processor.apvts.getRawParameterValue (ott::ParamIDs::displayMode)->load() != ott::DisplayTop;
}

float DynamicsDisplay::xForDb (float db, const Rectangle<float>& lane) const
{
    return lane.getX() + lane.getWidth() * (jlimit (ott::kMinDb, 0.0f, db) - ott::kMinDb) / -ott::kMinDb;
}

float DynamicsDisplay::dbForX (float x, const Rectangle<float>& lane) const
{
    return ott::kMinDb + (x - lane.getX()) / lane.getWidth() * -ott::kMinDb;
}

DynamicsDisplay::Hit DynamicsDisplay::findHit (Point<float> p) const
{
    for (int band = 0; band < 3; ++band)
    {
        const auto& lane = lanes[(size_t) band];
        if (! lane.expanded (4.0f, 0.0f).contains (p) || ! bandEnabled (band))
            continue;

        const float ax = xForDb (get (band, ott::BandIDs::aboveThresh), lane);
        const float bx = xForDb (get (band, ott::BandIDs::belowThresh), lane);
        const float da = showAbove() ? std::abs (p.x - ax) : 1.0e9f;
        const float db = showBelow() ? std::abs (p.x - bx) : 1.0e9f;

        constexpr float grab = 6.0f;
        if (std::min (da, db) <= grab)
        {
            // When both handles overlap, pick by side: right of the line = above, left = below.
            if (da <= grab && db <= grab)
                return { band, p.x >= ax ? Target::AboveHandle : Target::BelowHandle };
            return { band, da < db ? Target::AboveHandle : Target::BelowHandle };
        }
        if (showAbove() && p.x > ax)  return { band, Target::AboveRegion };
        if (showBelow() && p.x < bx)  return { band, Target::BelowRegion };
        return { band, Target::None };
    }
    return {};
}

RangedAudioParameter* DynamicsDisplay::paramFor (const Hit& h) const
{
    const char* id = nullptr;
    switch (h.target)
    {
        case Target::AboveHandle: id = ott::BandIDs::aboveThresh; break;
        case Target::BelowHandle: id = ott::BandIDs::belowThresh; break;
        case Target::AboveRegion: id = ott::BandIDs::aboveRatio;  break;
        case Target::BelowRegion: id = ott::BandIDs::belowRatio;  break;
        case Target::None:        return nullptr;
    }
    return processor.apvts.getParameter (ott::bandParam (h.band, id));
}

void DynamicsDisplay::setParam (RangedAudioParameter& p, float v)
{
    const auto& id = p.getParameterID();
    const auto range = p.getNormalisableRange();
    v = jlimit (range.start, range.end, v);

    // The below threshold can't pass the above threshold, and vice versa.
    for (int band = 0; band < 3; ++band)
    {
        if (id == ott::bandParam (band, ott::BandIDs::aboveThresh))
            v = std::max (v, get (band, ott::BandIDs::belowThresh));
        else if (id == ott::bandParam (band, ott::BandIDs::belowThresh))
            v = std::min (v, get (band, ott::BandIDs::aboveThresh));
    }
    p.setValueNotifyingHost (p.convertTo0to1 (v));
}

String DynamicsDisplay::describe (const Hit& h) const
{
    if (h.band < 0 || h.target == Target::None)
        return {};

    const String band = String (ott::bandName (h.band)).toUpperCase();
    switch (h.target)
    {
        case Target::AboveHandle: return band + "   ABOVE THRESHOLD   " + ott::formatDb (get (h.band, ott::BandIDs::aboveThresh), 1);
        case Target::BelowHandle: return band + "   BELOW THRESHOLD   " + ott::formatDb (get (h.band, ott::BandIDs::belowThresh), 1);
        case Target::AboveRegion: return band + "   ABOVE RATIO (DOWNWARD)   " + ott::formatRatio (get (h.band, ott::BandIDs::aboveRatio)) + " : 1";
        case Target::BelowRegion: return band + "   BELOW RATIO (UPWARD)   1 : " + ott::formatRatio (get (h.band, ott::BandIDs::belowRatio));
        case Target::None:        break;
    }
    return {};
}

String DynamicsDisplay::getTooltip()
{
    const auto text = describe (hover);
    if (text.isEmpty())
        return {};
    const bool handle = hover.target == Target::AboveHandle || hover.target == Target::BelowHandle;
    return text + (handle ? "\nDrag left/right or use the wheel (1 dB, Shift = 0.1). Double-click = default."
                          : "\nDrag up/down or use the wheel to set the ratio (Shift = fine). Double-click = default.");
}

void DynamicsDisplay::mouseMove (const MouseEvent& e)
{
    const auto h = findHit (e.position);
    if (h.band != hover.band || h.target != hover.target)
    {
        hover = h;
        setMouseCursor (h.target == Target::AboveHandle || h.target == Target::BelowHandle ? MouseCursor::LeftRightResizeCursor
                        : h.target == Target::None ? MouseCursor::NormalCursor : MouseCursor::UpDownResizeCursor);
        repaint();
    }
    if (onStatus) onStatus (describe (hover));
}

void DynamicsDisplay::mouseExit (const MouseEvent&)
{
    hover = {};
    if (onStatus) onStatus ({});
    repaint();
}

void DynamicsDisplay::mouseDown (const MouseEvent& e)
{
    active = findHit (e.position);
    if (auto* p = paramFor (active))
    {
        dragStartValue = p->convertFrom0to1 (p->getValue());
        dragStartProportion = p->getValue();
        dragStartX = e.position.x;
        dragStartY = e.position.y;
        p->beginChangeGesture();
    }
}

void DynamicsDisplay::mouseDrag (const MouseEvent& e)
{
    auto* p = paramFor (active);
    if (p == nullptr)
        return;

    const auto& lane = lanes[(size_t) active.band];
    if (active.target == Target::AboveHandle || active.target == Target::BelowHandle)
    {
        const float dbPerPixel = -ott::kMinDb / lane.getWidth() * (e.mods.isShiftDown() ? 0.1f : 1.0f);
        setParam (*p, dragStartValue + (e.position.x - dragStartX) * dbPerPixel);
    }
    else
    {
        const float prop = jlimit (0.0f, 1.0f, dragStartProportion + (dragStartY - e.position.y) / dragPixelsForFullRange (e.mods));
        setParam (*p, p->convertFrom0to1 (prop));
    }
    if (onStatus) onStatus (describe (active));
    repaint();
}

void DynamicsDisplay::mouseUp (const MouseEvent&)
{
    if (auto* p = paramFor (active))
        p->endChangeGesture();
    active = {};
}

void DynamicsDisplay::mouseDoubleClick (const MouseEvent& e)
{
    if (auto* p = paramFor (findHit (e.position)))
    {
        p->beginChangeGesture();
        setParam (*p, p->convertFrom0to1 (p->getDefaultValue()));
        p->endChangeGesture();
        repaint();
    }
}

void DynamicsDisplay::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    const auto h = findHit (e.position);
    auto* p = paramFor (h);
    if (p == nullptr)
        return;

    if (const int steps = ParamControl::wheelSteps (wheel, wheelAccumulator); steps != 0)
    {
        p->beginChangeGesture();
        setParam (*p, p->convertFrom0to1 (p->getValue()) + (float) steps * ParamControl::wheelStepSize (e.mods));
        p->endChangeGesture();
        if (onStatus) onStatus (describe (h));
        repaint();
    }
}

void DynamicsDisplay::updateMeters (float dt)
{
    const float fall = 36.0f * dt;   // dB per frame
    for (int b = 0; b < 3; ++b)
    {
        auto& m = processor.meters[(size_t) b];
        auto& s = meterState[(size_t) b];
        const float in = m.inputDb.load(), out = m.outputDb.load();
        s.in  = in  > s.in  ? in  : std::max (in,  s.in  - fall);
        s.out = out > s.out ? out : std::max (out, s.out - fall);
        s.gain += (m.gainDb.load() - s.gain) * 0.5f;
        s.autoGain = m.autoGainDb.load();
    }
    repaint();
}

void DynamicsDisplay::paint (Graphics& g)
{
    for (int b = 0; b < 3; ++b)
        paintLane (g, b, lanes[(size_t) b]);

    // dB scale: 80 .. 0
    g.setFont (CustomLookAndFeel::uiFont (10.5f, true));
    const auto& ref = lanes[0];
    for (int db = -80; db <= 0; db += 10)
    {
        const float x = xForDb ((float) db, ref);
        g.setColour (Theme::labelText.withAlpha (0.5f));
        g.drawVerticalLine ((int) x, scaleArea.getY(), scaleArea.getY() + 3.0f);
        g.setColour (Theme::labelText);
        g.drawText (String (-db), Rectangle<float> (x - 14.0f, scaleArea.getY() + 3.0f, 28.0f, scaleArea.getHeight() - 3.0f),
                    Justification::centred);
    }
}

void DynamicsDisplay::paintLane (Graphics& g, int band, Rectangle<float> lane)
{
    CustomLookAndFeel::drawInsetBox (g, lane.expanded (3.0f, 3.0f), 3.0f);

    // grid
    g.setColour (Colours::white.withAlpha (0.05f));
    for (int db = -70; db < 0; db += 10)
        g.drawVerticalLine ((int) xForDb ((float) db, lane), lane.getY(), lane.getBottom());

    g.setFont (CustomLookAndFeel::uiFont (9.5f, true));
    g.setColour (Theme::labelText.withAlpha (0.45f));
    g.drawText (String (ott::bandName (band)).toUpperCase(), lane.reduced (4.0f, 2.0f), Justification::topLeft);

    if (! bandEnabled (band))
    {
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRect (lane);
        g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
        g.setColour (Theme::labelText.withAlpha (0.6f));
        g.drawText ("BAND OFF", lane, Justification::centred);
        return;
    }

    const float aboveDb = get (band, ott::BandIDs::aboveThresh);
    const float belowDb = get (band, ott::BandIDs::belowThresh);
    const float ax = xForDb (aboveDb, lane), bx = xForDb (belowDb, lane);
    const bool above = showAbove(), below = showBelow();

    // threshold regions (the strip under the band name)
    auto regionStrip = lane.withTrimmedTop (lane.getHeight() * 0.18f).withTrimmedBottom (lane.getHeight() * 0.30f);
    {
        auto belowRect = regionStrip.withRight (bx);
        auto aboveRect = regionStrip.withLeft (ax);

        g.setColour (Theme::upRegion.withAlpha (below ? 0.85f : 0.25f));
        g.fillRect (belowRect);
        g.setColour (Theme::downRegion.withAlpha (above ? 0.9f : 0.25f));
        g.fillRect (aboveRect);

        const bool hoverBelow = hover.band == band && hover.target == Target::BelowRegion;
        const bool hoverAbove = hover.band == band && hover.target == Target::AboveRegion;
        if (hoverBelow) { g.setColour (Colours::white.withAlpha (0.06f)); g.fillRect (belowRect); }
        if (hoverAbove) { g.setColour (Colours::white.withAlpha (0.06f)); g.fillRect (aboveRect); }

        g.setFont (CustomLookAndFeel::uiFont (10.0f, true));
        if (below && belowRect.getWidth() > 70.0f)
        {
            g.setColour (Colour (0xff8fc7ff).withAlpha (0.8f));
            g.drawText ("1:" + ott::formatRatio (get (band, ott::BandIDs::belowRatio)),
                        Rectangle<float> (belowRect.getX(), lane.getY(), belowRect.getWidth() - 6.0f, regionStrip.getY() - lane.getY()),
                        Justification::centredRight);
        }
        if (above && aboveRect.getWidth() > 34.0f)
        {
            g.setColour (Colour (0xffffc9a0).withAlpha (0.8f));
            g.drawText (ott::formatRatio (get (band, ott::BandIDs::aboveRatio)) + ":1",
                        Rectangle<float> (aboveRect.getX() + 6.0f, lane.getY(), aboveRect.getWidth() - 6.0f, regionStrip.getY() - lane.getY()),
                        Justification::centredLeft);
        }
    }

    // level meters: input (thick) and band output (thin)
    const auto& m = meterState[(size_t) band];
    const float meterY = regionStrip.getY() + regionStrip.getHeight() * 0.22f;
    auto inBar  = Rectangle<float> (lane.getX(), meterY, xForDb (m.in, lane) - lane.getX(), 9.0f);
    auto outBar = Rectangle<float> (lane.getX(), meterY + 12.0f, xForDb (m.out, lane) - lane.getX(), 5.0f);

    g.setColour (Theme::blueGlow.withAlpha (0.3f));
    g.fillRect (inBar.expanded (0.0f, 2.0f));
    g.setColour (Theme::blue);
    g.fillRect (inBar);
    g.setColour (Colour (0xffc9e6ff).withAlpha (0.5f));
    g.fillRect (inBar.withHeight (2.0f));
    g.setColour (Theme::blue.withAlpha (0.8f));
    g.fillRect (outBar);

    // gain change: orange tick at the detector level, line to where the compressor moves it
    if (m.in > ott::kMinDb + 0.5f)
    {
        const float x0 = xForDb (m.in, lane), x1 = xForDb (m.in + m.gain, lane);
        const float y = regionStrip.getBottom() - 9.0f;
        g.setColour (Theme::orange.withAlpha (0.55f));
        g.drawLine (x0, y, x1, y, 2.0f);
        g.setColour (Theme::orange);
        g.fillRect (x1 - 1.5f, y - 5.0f, 3.0f, 10.0f);
    }

    // threshold handles
    auto drawHandle = [&] (float x, bool shown, bool isHover)
    {
        if (! shown)
        {
            g.setColour (Colours::white.withAlpha (0.15f));
            g.drawVerticalLine ((int) x, regionStrip.getY(), regionStrip.getBottom());
            return;
        }
        const auto colour = isHover ? Colour (0xffffd29a) : Theme::orange;
        g.setColour (colour.withAlpha (0.25f));
        g.fillRect (x - 2.5f, regionStrip.getY(), 5.0f, regionStrip.getHeight());
        g.setColour (colour);
        g.fillRect (x - 1.0f, regionStrip.getY(), 2.0f, regionStrip.getHeight());
        Path tab;
        tab.addTriangle (x - 5.0f, regionStrip.getY() - 1.0f, x + 5.0f, regionStrip.getY() - 1.0f, x, regionStrip.getY() + 5.0f);
        g.fillPath (tab);
    };
    drawHandle (bx, below, hover.band == band && hover.target == Target::BelowHandle);
    drawHandle (ax, above, hover.band == band && hover.target == Target::AboveHandle);

    // gain readout (like "-29.9" in the OTT display) and auto gain
    auto textRow = lane.withTop (regionStrip.getBottom() + 2.0f).reduced (4.0f, 0.0f);
    g.setFont (CustomLookAndFeel::uiFont (14.0f, true));
    g.setColour (Theme::blue);
    const String gainText = (m.gain >= 0.05f ? "+" : "") + String (m.gain, 1);
    g.drawText (gainText, textRow, Justification::centredLeft);

    if (std::abs (m.autoGain) > 0.0f || get (band, ott::BandIDs::autoGain) >= 0.5f)
    {
        g.setFont (CustomLookAndFeel::uiFont (10.5f, true));
        g.setColour (Theme::labelText);
        g.drawText ("AUTO GAIN " + String (m.autoGain >= 0.0f ? "+" : "") + String (m.autoGain, 1) + " dB",
                    textRow.withTrimmedLeft (56.0f), Justification::centredLeft);
    }

    g.setFont (CustomLookAndFeel::uiFont (10.0f, true));
    g.setColour (Theme::labelText.withAlpha (0.8f));
    g.drawText ("OUT " + (m.out <= ott::kMinDb + 0.1f ? String ("-inf") : String (m.out, 1)), textRow, Justification::centredRight);
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

    auto box = getLocalBounds().toFloat().reduced (60.0f, 30.0f);
    CustomLookAndFeel::drawPanel (g, box, "OTT - Quick Help");

    const char* lines[] =
    {
        "Three-band upward + downward compressor with a linear-phase crossover (no phase shift at the splits).",
        "SPLIT FREQ:  High / Low switch the outer bands on or off and set the split points. Mid fills the rest.",
        "Power = band on (off passes the band untouched).  S = solo.  AUTO GAIN = keep the band's loudness level.",
        "INPUT drives each band into the compressor, OUTPUT sets each band's level afterwards.",
        "DISPLAY:  drag the orange handles to set the above (downward) and below (upward) thresholds.",
        "Drag up/down inside a coloured region, or use the wheel, to set its ratio.  T / B / A = top, bottom, all.",
        "ATT / REL:  attack and release per band.  TIME scales them all.  AMOUNT scales the compression.",
        "UPWARD / DOWNWARD scale each direction (as on Xfer OTT).  MIX blends in the dry signal.",
        "SOFT KNEE rounds the threshold corners.  RMS = average detection, off = peak.",
        "Knobs and values: drag (shift = fine), wheel = 1.0 steps, shift + wheel = 0.1, double-click = default,",
        "right-click = type a value.  Drag the bottom-right corner or use SIZE to resize the window.",
        "",
        "Click anywhere to close."
    };

    auto text = box.reduced (14.0f, 0.0f).withTrimmedTop (30.0f);
    g.setFont (CustomLookAndFeel::uiFont (12.0f));
    for (auto* line : lines)
    {
        g.setColour (String (line).startsWith ("Click") ? Theme::blue : Colour (0xffdfe3e6));
        g.drawFittedText (line, text.removeFromTop (22.0f).toNearestInt(), Justification::centredLeft, 1, 0.8f);
    }
}
