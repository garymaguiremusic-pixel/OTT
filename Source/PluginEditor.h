#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"
#include "UI/OttComponents.h"

//==============================================================================
/**
    Everything is laid out at a fixed base size inside `Content`, which is then
    scaled with a transform. The window keeps its aspect ratio and can be resized
    from the corner or with the SIZE menu; the size is saved with the session.
*/
class OttAudioProcessorEditor : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit OttAudioProcessorEditor (OttAudioProcessor&);
    ~OttAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseWidth = 960, kBaseHeight = 436;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (OttAudioProcessorEditor& e) : editor (e) { setOpaque (true); }
        void paint (juce::Graphics& g) override  { editor.paintContent (g); }
    private:
        OttAudioProcessorEditor& editor;
    };

    void timerCallback() override;
    void paintContent (juce::Graphics&);
    void layoutContent();
    void drawBackground (juce::Graphics&);
    void drawWordmark (juce::Graphics&, juce::Rectangle<float> area);
    void setStatus (const juce::String&);
    void setScale (float scale);
    void setupToggle (juce::Button&, const juce::String& paramId, const juce::String& tooltip);
    juce::RangedAudioParameter& p (const juce::String& id) { return *processor.apvts.getParameter (id); }

    OttAudioProcessor& processor;
    CustomLookAndFeel lnf;
    Content content { *this };
    juce::Image background;
    float backgroundScale = 0.0f;
    juce::TooltipWindow tooltips { this, 600 };

    // Top strip
    juce::ComboBox presetBox, sizeBox;
    juce::TextButton bypassButton { "Bypass" };

    // Split / band buttons
    juce::TextButton highOnButton { "High" }, lowOnButton { "Low" };
    std::array<PowerButton, ott::NumBands> powerButtons;
    std::array<juce::TextButton, ott::NumBands> soloButtons, autoGainButtons;
    std::unique_ptr<ValueField> highFreqField, lowFreqField;
    juce::TextButton softKneeButton { "Soft Knee" }, rmsButton { "RMS" };

    // Per band knobs and fields
    std::array<std::unique_ptr<OttKnob>, ott::NumBands> inputKnobs, outputKnobs;
    std::array<std::unique_ptr<ValueField>, ott::NumBands> attackFields, releaseFields;

    // Global
    std::unique_ptr<OttKnob> outputKnob, timeKnob, amountKnob, upwardKnob, downwardKnob, mixKnob;

    // Display
    DynamicsDisplay display;
    std::array<juce::TextButton, 3> displayModeButtons;

    HelpButton helpButton;
    HelpOverlay helpOverlay;

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    juce::String statusText;
    int lastShownProgram = -1;
    double lastTimerMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OttAudioProcessorEditor)
};
