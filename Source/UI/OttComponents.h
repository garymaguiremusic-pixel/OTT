#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <optional>

class OttAudioProcessor;

//==============================================================================
/**
    Shared mouse handling for every parameter control (knobs and value fields):
      - vertical drag (shift = fine)
      - mouse wheel: one notch = 1.0 in the control's units (dB, ms, %, Hz, ratio),
        shift + wheel = 0.1
      - double-click resets (to 0 dB for gain controls, otherwise to the default)
      - right-click (or ctrl/cmd-click) to type a value
      - tooltips show the parameter name and value
*/
class ParamControl : public juce::Component,
                     public juce::TooltipClient
{
public:
    explicit ParamControl (juce::RangedAudioParameter&);
    ~ParamControl() override;

    float getValue() const noexcept         { return value; }
    float getProportion() const             { return param.convertTo0to1 (value); }
    juce::String getValueText() const;
    juce::RangedAudioParameter& getParameter() noexcept { return param; }

    /** Text for the value; defaults to the parameter's own text. */
    std::function<juce::String (float)> formatter;
    /** Fired on hover / drag so the editor can show a status line. */
    std::function<void (const juce::String&)> onStatus;
    /** Fired after every change the user makes with the mouse or keyboard (not automation). */
    std::function<void (float oldValue, float newValue)> onUserChange;
    /** Value for double-click; when unset, the parameter's default. */
    std::optional<float> resetValue;

    juce::String getTooltip() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Wheel notches in a wheel event (+/-1 per notch; trackpads are accumulated). */
    static int wheelSteps (const juce::MouseWheelDetails&, float& accumulator);
    /** Step size for the wheel: 1.0, or 0.1 with shift held. */
    static float wheelStepSize (const juce::ModifierKeys& mods) { return mods.isShiftDown() ? 0.1f : 1.0f; }

protected:
    virtual juce::Rectangle<int> getTextEditorArea() const  { return getLocalBounds(); }
    void sendStatus();

    juce::RangedAudioParameter& param;
    bool dragging = false, hovering = false;

private:
    void setValueFromUser (float newValue);   // one complete gesture
    void showTextEditor();

    juce::ParameterAttachment attachment;
    float value = 0.0f;
    float dragProportion = 0.0f, lastDragY = 0.0f, wheelAccumulator = 0.0f;
    std::unique_ptr<juce::TextEditor> editor;
};

//==============================================================================
/** Rotary knob with an optional title above and the value below (e.g. "5.20 dB"). */
class OttKnob : public ParamControl
{
public:
    OttKnob (juce::RangedAudioParameter&, const juce::String& title = {}, bool bipolar = false);

    bool isBipolar() const noexcept  { return bipolar; }
    bool showValue = true;           // value text under the knob
    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<int> getTextEditorArea() const override;

    juce::String title;
    bool bipolar;
};

//==============================================================================
/** Draggable numeric readout: a blue LCD (split frequencies) or plain blue text (attack / release). */
class ValueField : public ParamControl
{
public:
    enum class Style { Lcd, Text };
    ValueField (juce::RangedAudioParameter&, Style);
    void paint (juce::Graphics&) override;

private:
    Style style;
};

//==============================================================================
/** Toggle with a power symbol (band active). */
class PowerButton : public juce::Button
{
public:
    PowerButton() : juce::Button ("power") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
};

//==============================================================================
/**
    The centre display, after Ableton's Multiband Dynamics: one lane per band on
    a -80 .. 0 dB scale with
      - the input level (thick bar) and the band output level (thin bar)
      - the below-threshold (upward) and above-threshold (downward) regions,
        with draggable threshold handles; drag up/down inside a region (or use
        the wheel) to set its ratio
      - the gain change in dB (and the auto-gain offset when enabled)
    T / B / A selects which thresholds are shown and editable.
*/
class DynamicsDisplay : public juce::Component,
                        public juce::TooltipClient
{
public:
    explicit DynamicsDisplay (OttAudioProcessor&);

    /** Lane rectangles in this component's coordinates (set by the editor). */
    void setLaneBounds (const std::array<juce::Rectangle<float>, 3>& lanes, juce::Rectangle<float> scaleArea);
    void updateMeters (float deltaSeconds);

    std::function<void (const juce::String&)> onStatus;

    void paint (juce::Graphics&) override;
    juce::String getTooltip() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Target { None, AboveHandle, BelowHandle, AboveRegion, BelowRegion };
    struct Hit { int band = -1; Target target = Target::None; };

    Hit findHit (juce::Point<float>) const;
    juce::RangedAudioParameter* paramFor (const Hit&) const;
    float xForDb (float db, const juce::Rectangle<float>& lane) const;
    float dbForX (float x, const juce::Rectangle<float>& lane) const;
    bool showAbove() const;
    bool showBelow() const;
    bool bandEnabled (int band) const;
    float get (int band, const char* id) const;
    void setParam (juce::RangedAudioParameter&, float value);   // clamps thresholds against each other
    juce::String describe (const Hit&) const;
    void paintLane (juce::Graphics&, int band, juce::Rectangle<float> lane);

    OttAudioProcessor& processor;
    std::array<juce::Rectangle<float>, 3> lanes;
    juce::Rectangle<float> scaleArea;

    struct MeterState { float in = -80.0f, out = -80.0f, gain = 0.0f, autoGain = 0.0f; };
    std::array<MeterState, 3> meterState;

    Hit active, hover;
    float dragStartValue = 0.0f, dragStartProportion = 0.0f, dragStartX = 0.0f, dragStartY = 0.0f, wheelAccumulator = 0.0f;
};

//==============================================================================
/** Round "?" button in the bottom bar. */
class HelpButton : public juce::Button
{
public:
    HelpButton() : juce::Button ("help") {}
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
};

/** Quick reference shown over the editor; click anywhere to close. */
class HelpOverlay : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
};
