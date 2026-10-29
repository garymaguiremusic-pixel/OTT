#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "DSP/LinearPhaseCrossover.h"
#include "DSP/BandCompressor.h"

//==============================================================================
class OttAudioProcessor : public juce::AudioProcessor,
                          private juce::Timer
{
public:
    OttAudioProcessor();
    ~OttAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.5; }
    juce::AudioProcessorParameter* getBypassParameter() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override                    { return currentProgram.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    /** Meter values for the editor, written by the audio thread. */
    struct BandMeter
    {
        std::atomic<float> inputDb { ott::kMinDb }, outputDb { ott::kMinDb }, gainDb { 0.0f }, autoGainDb { 0.0f };
    };
    std::array<BandMeter, ott::NumBands> meters;
    std::atomic<float> latencyMs { 0.0f };

    /** Editor scale, saved with the session. */
    std::atomic<float> editorScale { 1.0f };

private:
    void timerCallback() override;
    void updateBandSettings();
    float param (const juce::String& id) const;
    bool paramOn (const juce::String& id) const;

    ott::LinearPhaseCrossover crossover;
    std::array<ott::BandCompressor, ott::NumBands> compressors;

    juce::AudioBuffer<float> inputChunk, dryDelayed;
    std::array<juce::AudioBuffer<float>, ott::NumBands> bands;

    juce::SmoothedValue<float> mixSmoothed, bypassMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain { 1.0f };
    std::array<juce::SmoothedValue<float>, ott::NumBands> bandSoloGain;

    int maxBlock = 512;
    std::atomic<int> currentProgram { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OttAudioProcessor)
};
