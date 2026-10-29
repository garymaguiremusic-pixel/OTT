#include "LinearPhaseCrossover.h"

namespace ott
{

namespace
{
    // Zeroth-order modified Bessel function of the first kind (for the Kaiser window).
    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        const double halfX = 0.5 * x;
        for (int k = 1; k < 64; ++k)
        {
            term *= (halfX / (double) k) * (halfX / (double) k);
            sum += term;
            if (term < sum * 1.0e-12)
                break;
        }
        return sum;
    }

    // ~80 dB stop band.
    constexpr double kKaiserBeta = 8.0;

    // The filter is ~85 ms long at every sample rate (4097 taps at 48 kHz), giving
    // ~43 ms latency and a transition band of roughly 60 Hz, steep enough for an
    // 88 Hz split.
    int firLengthFor (double sampleRate)
    {
        const int half = juce::roundToInt (2048.0 * sampleRate / 48000.0);
        return 2 * juce::jlimit (256, 16384, half) + 1;
    }
}

LinearPhaseCrossover::LinearPhaseCrossover() = default;

void LinearPhaseCrossover::designLowPass (float* dest, int length, double cutoffHz, double fs, double beta)
{
    const int centre = (length - 1) / 2;
    const double fc = juce::jlimit (1.0, 0.49 * fs, cutoffHz) / fs;    // cycles per sample
    const double i0Beta = besselI0 (beta);

    double sum = 0.0;
    for (int n = 0; n < length; ++n)
    {
        const int m = n - centre;
        const double sinc = (m == 0) ? 2.0 * fc
                                     : std::sin (juce::MathConstants<double>::twoPi * fc * (double) m)
                                         / (juce::MathConstants<double>::pi * (double) m);
        const double r = (double) m / (double) centre;
        const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0Beta;
        const double h = sinc * window;
        dest[n] = (float) h;
        sum += h;
    }

    // Exact unity gain at DC.
    if (std::abs (sum) > 1.0e-12)
        for (int n = 0; n < length; ++n)
            dest[n] = (float) ((double) dest[n] / sum);
}

void LinearPhaseCrossover::designFilters (const Settings& s, juce::AudioBuffer<float>& lp1,
                                          juce::AudioBuffer<float>& lp2) const
{
    lp1.setSize (1, firLength, false, false, false);
    lp2.setSize (1, firLength, false, false, false);
    lp1.clear();
    lp2.clear();

    // High split: LP2. When the high band is off, LP2 is a pure delay so "high" is silent.
    if (s.highOn)
        designLowPass (lp2.getWritePointer (0), firLength, s.highHz, sampleRate, kKaiserBeta);
    else
        lp2.setSample (0, centreTap, 1.0f);

    // Low split: LP1. When the low band is off, LP1 is silent so "low" is silent.
    // The low split can never sit above the high one.
    if (s.lowOn)
    {
        const double lowHz = s.highOn ? std::min (s.lowHz, s.highHz) : s.lowHz;
        designLowPass (lp1.getWritePointer (0), firLength, lowHz, sampleRate, kKaiserBeta);
    }
}

void LinearPhaseCrossover::prepare (double fs, int maxBlockSize, int numChannels,
                                    float lowHz, bool lowOn, float highHz, bool highOn)
{
    sampleRate = fs;
    maxBlock = std::max (1, maxBlockSize);
    channels = juce::jlimit (1, 2, numChannels);
    firLength = firLengthFor (fs);
    centreTap = (firLength - 1) / 2;

    currentSettings = { lowHz, highHz, lowOn, highOn };

    juce::AudioBuffer<float> lp1, lp2;
    designFilters (currentSettings, lp1, lp2);

    // Loading before prepare() makes these filters active on the very first block.
    conv1.loadImpulseResponse (std::move (lp1), fs, juce::dsp::Convolution::Stereo::no,
                               juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    conv2.loadImpulseResponse (std::move (lp2), fs, juce::dsp::Convolution::Stereo::no,
                               juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);

    const juce::dsp::ProcessSpec spec { fs, (juce::uint32) maxBlock, (juce::uint32) channels };
    conv1.prepare (spec);
    conv2.prepare (spec);

    delayBuffer.setSize (channels, centreTap + maxBlock + 1);
    delayBuffer.clear();
    delayWritePos = 0;

    {
        const juce::SpinLock::ScopedLockType lock (pendingLock);
        hasPending = false;
    }
    prepared = true;
}

void LinearPhaseCrossover::reset()
{
    conv1.reset();
    conv2.reset();
    delayBuffer.clear();
    delayWritePos = 0;
}

void LinearPhaseCrossover::updateSettings (float lowHz, bool lowOn, float highHz, bool highOn)
{
    if (! prepared)
        return;

    const Settings s { lowHz, highHz, lowOn, highOn };
    if (s == currentSettings)
        return;

    currentSettings = s;

    juce::AudioBuffer<float> lp1, lp2;
    designFilters (s, lp1, lp2);

    // Swap into the pending slot; anything not yet picked up is simply replaced.
    const juce::SpinLock::ScopedLockType lock (pendingLock);
    std::swap (pendingLp1, lp1);
    std::swap (pendingLp2, lp2);
    hasPending = true;
    // lp1 / lp2 now hold the stale pending buffers and are freed here, on the message thread.
}

void LinearPhaseCrossover::installPending()
{
    const juce::SpinLock::ScopedTryLockType lock (pendingLock);
    if (! lock.isLocked() || ! hasPending)
        return;

    // Moving an AudioBuffer doesn't allocate; Convolution::loadImpulseResponse is wait-free
    // and builds the new engine on its own background thread.
    conv1.loadImpulseResponse (std::move (pendingLp1), sampleRate, juce::dsp::Convolution::Stereo::no,
                               juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    conv2.loadImpulseResponse (std::move (pendingLp2), sampleRate, juce::dsp::Convolution::Stereo::no,
                               juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    hasPending = false;
}

void LinearPhaseCrossover::process (const juce::AudioBuffer<float>& input, int numSamples,
                                    juce::AudioBuffer<float>& low, juce::AudioBuffer<float>& mid,
                                    juce::AudioBuffer<float>& high, juce::AudioBuffer<float>& dryDelayed)
{
    jassert (numSamples <= maxBlock);
    installPending();

    const int numCh = std::min (channels, input.getNumChannels());

    // LP1 -> low, LP2 -> mid (temporarily)
    {
        juce::dsp::AudioBlock<const float> in (input.getArrayOfReadPointers(), (size_t) numCh, (size_t) numSamples);
        juce::dsp::AudioBlock<float> outLow (low.getArrayOfWritePointers(), (size_t) numCh, (size_t) numSamples);
        juce::dsp::AudioBlock<float> outMid (mid.getArrayOfWritePointers(), (size_t) numCh, (size_t) numSamples);
        conv1.process (juce::dsp::ProcessContextNonReplacing<float> (in, outLow));
        conv2.process (juce::dsp::ProcessContextNonReplacing<float> (in, outMid));
    }

    // Delay the dry signal by the centre tap.
    const int delaySize = delayBuffer.getNumSamples();
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* x = input.getReadPointer (ch);
        float* d = delayBuffer.getWritePointer (ch);
        float* out = dryDelayed.getWritePointer (ch);
        int w = delayWritePos;
        int r = delayWritePos - centreTap;
        if (r < 0) r += delaySize;

        for (int i = 0; i < numSamples; ++i)
        {
            d[w] = x[i];
            out[i] = d[r];
            if (++w == delaySize) w = 0;
            if (++r == delaySize) r = 0;
        }
    }
    delayWritePos = (delayWritePos + numSamples) % delaySize;

    // high = dry - LP2, mid = LP2 - LP1
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* lp1 = low.getReadPointer (ch);
        const float* dry = dryDelayed.getReadPointer (ch);
        float* m = mid.getWritePointer (ch);
        float* h = high.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float lp2 = m[i];
            h[i] = dry[i] - lp2;
            m[i] = lp2 - lp1[i];
        }
    }
}

} // namespace ott
