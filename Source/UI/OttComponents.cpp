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

// A side of the display is only shown (and editable) while its knob is above 0 %.
bool DynamicsDisplay::showAbove() const
{
    return processor.apvts.getRawParameterValue (ott::ParamIDs::downward)->load() > 0.05f;
}

bool DynamicsDisplay::showBelow() const
{
    return processor.apvts.getRawParameterValue (ott::ParamIDs::upward)->load() > 0.05f;
}

float DynamicsDisplay::thresholdDb (int band, const char* id) const
{
    // The band's Threshold control shifts both thresholds together.
    return get (band, id) + get (band, ott::BandIDs::threshold);
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

        const float ax = xForDb (thresholdDb (band, ott::BandIDs::aboveThresh), lane);
        const float bx = xForDb (thresholdDb (band, ott::BandIDs::belowThresh), lane);
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
        case Target::AboveHandle: return band + "   ABOVE THRESHOLD   " + ott::formatDb (thresholdDb (h.band, ott::BandIDs::aboveThresh), 1);
        case Target::BelowHandle: return band + "   BELOW THRESHOLD   " + ott::formatDb (thresholdDb (h.band, ott::BandIDs::belowThresh), 1);
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
    // Calm ballistics so the bars read as a level, not a flicker: quick rise, slow fall.
    const float rise = 1.0f - std::exp (-dt / 0.03f), fall = 1.0f - std::exp (-dt / 0.35f);
    const float gainGlide = 1.0f - std::exp (-dt / 0.12f);
    for (int b = 0; b < 3; ++b)
    {
        auto& m = processor.meters[(size_t) b];
        auto& s = meterState[(size_t) b];
        const float in = m.inputDb.load(), out = m.outputDb.load();
        s.in  += (in  - s.in)  * (in  > s.in  ? rise : fall);
        s.out += (out - s.out) * (out > s.out ? rise : fall);
        s.gain += (m.gainDb.load() - s.gain) * gainGlide;
        s.makeup += (m.makeupDb.load() - s.makeup) * gainGlide;
    }
    repaint();
}

void DynamicsDisplay::paint (Graphics& g)
{
    for (int b = 0; b < 3; ++b)
        paintLane (g, b, lanes[(size_t) b]);

    // optional dB scale: 80 .. 0
    if (scaleArea.isEmpty())
        return;

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

    if (! bandEnabled (band))
    {
        g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
        g.setColour (Theme::labelText.withAlpha (0.6f));
        g.drawText ("BAND OFF", lane, Justification::centred);
        return;
    }

    const bool above = showAbove(), below = showBelow();
    const float ax = above ? xForDb (thresholdDb (band, ott::BandIDs::aboveThresh), lane) : lane.getRight();
    const float bx = below ? xForDb (thresholdDb (band, ott::BandIDs::belowThresh), lane) : lane.getX();
    const bool hoverBelowEdge = hover.band == band && hover.target == Target::BelowHandle;
    const bool hoverAboveEdge = hover.band == band && hover.target == Target::AboveHandle;

    // Upward area (left of the below threshold) and downward area (right of the above threshold).
    const auto belowRect = lane.withRight (bx);
    const auto aboveRect = lane.withLeft (ax);
    g.setColour (Theme::upRegion.withAlpha (0.8f));
    g.fillRect (belowRect);
    g.setColour (Theme::downRegion.withAlpha (0.85f));
    g.fillRect (aboveRect);
    if (hover.band == band && hover.target == Target::BelowRegion) { g.setColour (Colours::white.withAlpha (0.06f)); g.fillRect (belowRect); }
    if (hover.band == band && hover.target == Target::AboveRegion) { g.setColour (Colours::white.withAlpha (0.06f)); g.fillRect (aboveRect); }

    // Threshold edges: a thin line, highlighted while hovered.
    auto drawEdge = [&] (float x, bool isHover)
    {
        if (isHover)
        {
            g.setColour (Theme::orange.withAlpha (0.25f));
            g.fillRect (x - 3.0f, lane.getY(), 6.0f, lane.getHeight());
        }
        g.setColour (Theme::orange.withAlpha (isHover ? 1.0f : 0.55f));
        g.fillRect (x - (isHover ? 1.0f : 0.5f), lane.getY(), isHover ? 2.0f : 1.0f, lane.getHeight());
    };
    if (below) drawEdge (bx, hoverBelowEdge);
    if (above) drawEdge (ax, hoverAboveEdge);

    // One meter strip through the middle: input level (grey), what the compressor does
    // (orange = pushed down, light blue = lifted up) and a tick where the level ends up.
    const auto& m = meterState[(size_t) band];
    const auto strip = lane.withSizeKeepingCentre (lane.getWidth(), 8.0f);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRect (strip);

    if (m.in > ott::kMinDb + 0.5f)
    {
        const float inX = xForDb (m.in, lane);
        const float endX = xForDb (m.in + m.gain, lane);

        g.setColour (Colours::white.withAlpha (0.28f));
        g.fillRect (strip.withRight (std::min (inX, endX)));

        if (std::abs (endX - inX) > 0.5f)
        {
            const bool pushedDown = endX < inX;
            g.setColour (pushedDown ? Theme::orange.withAlpha (0.85f) : Colour (0xff8fc7ff).withAlpha (0.85f));
            g.fillRect (Rectangle<float>::leftTopRightBottom (std::min (inX, endX), strip.getY(),
                                                             std::max (inX, endX), strip.getBottom()));
        }

        g.setColour (Colours::white);
        g.fillRect (endX - 1.0f, strip.getY() - 3.0f, 2.0f, strip.getHeight() + 6.0f);
    }

    // small, dim gain readout
    g.setFont (CustomLookAndFeel::uiFont (10.0f, true));
    g.setColour (Colours::white.withAlpha (0.55f));
    String text = (m.gain >= 0.05f ? "+" : "") + String (m.gain, 1) + " dB";
    if (get (band, ott::BandIDs::autoGain) >= 0.5f)
        text << "   A " << (m.makeup >= 0.0f ? "+" : "") << String (m.makeup, 1);
    g.drawText (text, lane.reduced (5.0f, 2.0f), Justification::topRight);
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
        "Three-band upward + downward compressor (OTT style).",
        "DEPTH = dry/wet.  TIME scales every attack and release.  IN GAIN drives all bands.",
        "LINK: turning IN GAIN up turns OUT GAIN down by the same amount (keeps the level matched).",
        "AUTO GAIN: keeps the output as loud (K-weighted, like LUFS) as what comes into the plugin.",
        "S = solo a band.  A = band auto gain: keeps that band's level when you change its compression.",
        "RANDOM: new random settings (Bypass, Solo, Linear Phase, LINK and Auto Gain stay as they are).",
        "BANDS: drag a region's edge to set its threshold, drag inside it (or wheel) to set its ratio.",
        "Left (blue) = below threshold, raised upward.  Right (brown) = above threshold, pushed down.",
        "The knob and number on each row are the band's output gain.",
        "UPWARD / DOWNWARD scale each direction (at 0 that side disappears from the bars).",
        "LINEAR PHASE: on = no phase shift (43 ms latency), off = zero latency.",
        "ADVANCED: split frequencies, and Threshold / Attack / Release for each band.",
        "Knobs: drag (shift = fine), wheel = 1.0 steps, shift + wheel = 0.1, double-click = reset",
        "(gain knobs reset to 0 dB), right-click = type a value.  Drag the corner or use SIZE to resize.",
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
