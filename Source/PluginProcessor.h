#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "DSP/LinearPhaseCrossover.h"
#include "DSP/MinPhaseCrossover.h"
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
    void reset() override;
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
    /** Message thread: random values for the sound-shaping parameters, within musically useful
        ranges. Bypass, solo, linear phase, In / Out Gain, the link and global Auto Gain are left alone. */
    void randomise();
    std::atomic<bool> isRandomised { false };   // the preset menu shows "Random" until a preset is chosen

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    /** Meter values for the editor, written by the audio thread (lock-free). */
    struct BandMeter
    {
        std::atomic<float> envelopeDb { ott::kMinDb }, gainDb { 0.0f }, makeupDb { 0.0f };
    };
    std::array<BandMeter, ott::NumBands> meters;
    std::array<std::atomic<float>, 2> inputPeak {};   // linear, per channel
    std::atomic<float> latencyMs { 0.0f };
    std::atomic<float> autoGainMeterDb { 0.0f };       // gain Auto Gain is applying right now

    /** Editor scale, saved with the session. */
    std::atomic<float> editorScale { 1.0f };

private:
    void timerCallback() override;
    void updateBandSettings();
    void updateLatency (bool linear);
    int slopeOrder() const;   // 1..4 = 6, 12, 18, 24 dB/oct
    bool settingsChanged();

    // Raw parameter values, looked up once (no string lookups on the audio thread).
    struct BandParams
    {
        std::atomic<float> *threshold, *gain, *bypUp, *bypDown, *amount, *active, *solo,
                           *attack, *release, *autoRelease, *autoGain;
    };
    struct Params
    {
        std::atomic<float> *depth, *time, *inGain, *outGain, *cleanXov, *upStrength, *downStrength, *bypass,
                           *inOutLink, *autoGain, *linearPhase, *softKnee, *rms,
                           *highOn, *highFreq, *lowOn, *lowFreq, *slope;
        std::array<BandParams, ott::NumBands> band;
    } p {};
    static bool on (const std::atomic<float>* v) noexcept  { return v->load() >= 0.5f; }
    std::vector<std::atomic<float>*> watchedParams;   // everything the band coefficients depend on
    std::vector<float> lastWatched;

    ott::LinearPhaseCrossover crossover;
    ott::MinPhaseCrossover minPhaseCrossover;
    bool linearActive = true;                 // audio thread: which crossover is running
    int activeSlope = 2;                      // audio thread: zero-latency crossover slope (1..4)
    bool activeClean = false;                 // audio thread: zero-latency crossover precision
    std::atomic<bool> reportedLinear { true }; // latency currently reported to the host
    juce::SmoothedValue<float> modeFade { 1.0f };
    std::array<ott::BandCompressor, ott::NumBands> compressors;

    juce::AudioBuffer<float> inputChunk, dryDelayed;
    std::array<juce::AudioBuffer<float>, ott::NumBands> bands;

    // Look-ahead delay for the dry path (keeps Bypass and Auto Gain aligned with the bands)
    std::array<std::array<float, 4>, 2> dryRing {};
    int dryRingPos = 0;

    // Parameter smoothing (one-pole, spec coefficient) for Depth, In Gain, Out Gain
    double smoothCoef = 0.0;
    float depthSmoothed = 1.0f, inGainSmoothed = 1.0f, outGainSmoothed = 1.0f;
    std::vector<float> preGainBuffer, depthBuffer, outGainBuffer;

    juce::SmoothedValue<float> bypassMix;
    // After Random every setting jumps at once: restart the compressors on the new settings
    // and fade the output back in, so the jump can't produce a level spike.
    std::atomic<bool> randomRestartPending { false };
    juce::SmoothedValue<float> randomFade { 1.0f };
    std::array<juce::SmoothedValue<float>, ott::NumBands> bandSoloGain;
    std::array<float, 2> peak {};

    // Auto Gain: K-weighted loudness of the input and of the output
    struct KWeighting
    {
        std::array<juce::dsp::IIR::Filter<float>, 2> shelf, highPass;
        float process (int ch, float x) noexcept
        {
            return highPass[(size_t) ch].processSample (shelf[(size_t) ch].processSample (x));
        }
    };
    KWeighting refWeighting, outWeighting;
    void resetLoudness();
    void pushLoudnessBlock();
    static constexpr int kLoudnessBlocks = 10;                 // 10 x 100 ms = 1 s window
    std::array<double, kLoudnessBlocks> loudnessRef {}, loudnessOut {};
    double blockRef = 0.0, blockOut = 0.0;
    int loudnessBlockLength = 4800, loudnessBlocks = 0, loudnessIndex = 0, blockCount = 0;
    float autoGainDb = 0.0f, autoGainTargetDb = 0.0f;
    bool autoGainWasOn = false;

    int maxBlock = 512;
    double currentSampleRate = 0.0;
    std::atomic<int> currentProgram { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OttAudioProcessor)
};
