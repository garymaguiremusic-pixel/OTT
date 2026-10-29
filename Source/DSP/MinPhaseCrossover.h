#pragma once

#include <juce_dsp/juce_dsp.h>

namespace ott
{

//==============================================================================
/**
    Zero-latency three-band crossover (used when Linear Phase is off).

    4th-order Linkwitz-Riley splits: the input is split at the high frequency,
    the low part is split again at the low frequency, and the high band runs
    through a matching all-pass so all three bands stay phase-aligned with each
    other. The bands sum to a flat (all-pass) response: the magnitude is
    unchanged, but there is phase shift around the split points.
*/
class MinPhaseCrossover
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) numChannels };
        for (auto* f : { &highSplit, &lowSplit, &highAllpass })
            f->prepare (spec);
        highAllpass.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
        nyquistLimit = (float) (sampleRate * 0.45);
        reset();
    }

    void reset()
    {
        highSplit.reset();
        lowSplit.reset();
        highAllpass.reset();
    }

    /** Splits `input` into the three band buffers; `dry` gets an undelayed copy. */
    void process (const juce::AudioBuffer<float>& input, int numSamples,
                  juce::AudioBuffer<float>& low, juce::AudioBuffer<float>& mid,
                  juce::AudioBuffer<float>& high, juce::AudioBuffer<float>& dry,
                  float lowHz, bool lowOn, float highHz, bool highOn)
    {
        highHz = juce::jlimit (20.0f, nyquistLimit, highHz);
        lowHz  = juce::jlimit (20.0f, nyquistLimit, highOn ? std::min (lowHz, highHz) : lowHz);
        highSplit.setCutoffFrequency (highHz);
        lowSplit.setCutoffFrequency (lowHz);
        highAllpass.setCutoffFrequency (lowHz);

        const int numCh = std::min (input.getNumChannels(), low.getNumChannels());
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* x = input.getReadPointer (ch);
            float* l = low.getWritePointer (ch);
            float* m = mid.getWritePointer (ch);
            float* h = high.getWritePointer (ch);
            float* d = dry.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                d[i] = x[i];

                float lowPart = x[i], highPart = 0.0f;
                if (highOn)
                    highSplit.processSample (ch, x[i], lowPart, highPart);

                if (lowOn)
                {
                    lowSplit.processSample (ch, lowPart, l[i], m[i]);
                    h[i] = highOn ? highAllpass.processSample (ch, highPart) : 0.0f;
                }
                else
                {
                    l[i] = 0.0f;
                    m[i] = lowPart;
                    h[i] = highPart;
                }
            }
        }

        highSplit.snapToZero();
        lowSplit.snapToZero();
        highAllpass.snapToZero();
    }

private:
    juce::dsp::LinkwitzRileyFilter<float> highSplit, lowSplit, highAllpass;
    float nyquistLimit = 20000.0f;
};

} // namespace ott
