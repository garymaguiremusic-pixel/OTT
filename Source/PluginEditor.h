#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"
#include "UI/OttComponents.h"

//==============================================================================
/**
    Main view laid out like Xfer's OTT (global knobs, three band rows, upward /
    downward), drawn in the VIRUS DELAY style. The detailed per-band settings
    live on the ADVANCED page, which slides over the main view.

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

    static constexpr int kBaseWidth = 520, kBaseHeight = 634;

private:
    /** A component whose painting is supplied by the editor. */
    struct Page : public juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override  { if (painter) painter (g); }
    };

    void timerCallback() override;
    void paintContent (juce::Graphics&);
    void paintAdvanced (juce::Graphics&);
    void layoutContent();
    void drawBackground (juce::Graphics&);
    void drawWordmark (juce::Graphics&, juce::Rectangle<float> area);
    void setStatus (const juce::String&);
    void setScale (float scale);
    void showAdvanced (bool);
    void setupToggle (juce::Button&, const juce::String& paramId, const juce::String& tooltip, juce::Component& parent);
    void linkInputToOutput (float oldIn, float newIn);
    juce::RangedAudioParameter& p (const juce::String& id) { return *processor.apvts.getParameter (id); }
    bool isOn (const char* id) const { return processor.apvts.getRawParameterValue (id)->load() >= 0.5f; }

    OttAudioProcessor& processor;
    CustomLookAndFeel lnf;
    Page content, advanced;
    juce::Image background;
    float backgroundScale = 0.0f;
    juce::TooltipWindow tooltips { this, 600 };

    // Top strip / bottom bar
    juce::ComboBox presetBox, sizeBox;
    juce::TextButton bypassButton { "Bypass" };
    HelpButton helpButton;
    HelpOverlay helpOverlay;

    // Main view
    std::unique_ptr<OttKnob> depthKnob, timeKnob, inGainKnob, outGainKnob, upwardKnob, downwardKnob;
    juce::TextButton linkButton { "LINK" };
    std::array<std::unique_ptr<ValueField>, ott::NumBands> bandGainFields;
    std::array<std::unique_ptr<OttKnob>, ott::NumBands> bandGainKnobs;
    DynamicsDisplay display;
    juce::TextButton linearPhaseButton { "Linear Phase" }, softKneeButton { "Soft Knee" }, rmsButton { "RMS" };
    juce::TextButton advancedButton { "Advanced" };

    // Advanced page
    juce::TextButton highOnButton { "High" }, lowOnButton { "Low" }, backButton { "Back" };
    std::unique_ptr<ValueField> highFreqField, lowFreqField;
    std::array<PowerButton, ott::NumBands> powerButtons;
    std::array<juce::TextButton, ott::NumBands> soloButtons;
    std::array<std::unique_ptr<ValueField>, ott::NumBands> thresholdFields, attackFields, releaseFields;

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    juce::String statusText;
    int lastShownProgram = -1;
    double lastTimerMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OttAudioProcessorEditor)
};
