#pragma once

#include <juce_dsp/juce_dsp.h>

namespace ott
{

//==============================================================================
/**
    Three-band linear-phase crossover.

    Two linear-phase FIR low-passes (LP1 at the low split, LP2 at the high split)
    are run as FFT convolutions, and the bands are taken as

        low  = LP1 (x)
        mid  = LP2 (x) - LP1 (x)
        high = delay (x) - LP2 (x)

    so low + mid + high is always exactly the input delayed by the FIR centre tap:
    there are no phase shifts and no comb filtering at the split points, whatever
    the split frequencies are. Every band is delayed by the same amount, which is
    reported to the host as latency.

    Each low-pass has the zero-phase magnitude 1 / (1 + (f / fc)^k), k = 1, 2, 3
    for 6, 12 or 18 dB/oct. The complementary high side, 1 - L, then has exactly
    the same slope, and both sides are -6 dB at the split. Gentle slopes also keep
    the filters short in time, so there is very little pre-ringing.

    A disabled split is handled in the filter itself: Low off makes LP1 silent
    (the mid band reaches down to DC), High off makes LP2 a pure delay (the mid
    band reaches up to Nyquist). New filters are designed on the message thread
    and handed to the audio thread without locks; juce::dsp::Convolution then
    crossfades from the old filter to the new one, so moving a split never clicks.
*/
class LinearPhaseCrossover
{
public:
    LinearPhaseCrossover();

    /** Call from prepareToPlay(). Designs the filters synchronously. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels,
                  float lowHz, bool lowOn, float highHz, bool highOn, int slopeOrder);
    void reset();

    /** Latency in samples (the FIR centre tap). */
    int getLatency() const noexcept     { return centreTap; }

    /** Message thread: redesigns the filters if the settings changed. Cheap otherwise. */
    void updateSettings (float lowHz, bool lowOn, float highHz, bool highOn, int slopeOrder);

    /** Audio thread. Splits `input` into the three band buffers and the delayed dry signal.
        All buffers must hold at least `numSamples` samples and the same channel count. */
    void process (const juce::AudioBuffer<float>& input, int numSamples,
                  juce::AudioBuffer<float>& low, juce::AudioBuffer<float>& mid,
                  juce::AudioBuffer<float>& high, juce::AudioBuffer<float>& dryDelayed);

    /** Builds a zero-phase low-pass with magnitude 1 / (1 + (f / fc)^order), centred in
        `length` taps and normalised to unity gain at DC. Public for testing. */
    static void designLowPass (float* dest, int length, double cutoffHz, double sampleRate, int order);

private:
    struct Settings
    {
        float lowHz = 88.3f, highHz = 2500.0f;
        bool lowOn = true, highOn = true;
        int order = 2;
        bool operator== (const Settings& o) const noexcept
        {
            return juce::exactlyEqual (lowHz, o.lowHz) && juce::exactlyEqual (highHz, o.highHz)
                && lowOn == o.lowOn && highOn == o.highOn && order == o.order;
        }
    };

    void designFilters (const Settings&, juce::AudioBuffer<float>& lp1, juce::AudioBuffer<float>& lp2) const;
    void installPending();

    juce::dsp::Convolution conv1, conv2;
    double sampleRate = 48000.0;
    int firLength = 4097, centreTap = 2048, maxBlock = 512, channels = 2;

    // Delay line for the dry/high path
    juce::AudioBuffer<float> delayBuffer;
    int delayWritePos = 0;

    // Message thread -> audio thread hand-off
    juce::SpinLock pendingLock;
    juce::AudioBuffer<float> pendingLp1, pendingLp2;
    bool hasPending = false;
    Settings currentSettings;
    bool prepared = false;
};

} // namespace ott
