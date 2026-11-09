#pragma once

#include <juce_dsp/juce_dsp.h>

namespace ott
{

//==============================================================================
/**
    Zero-latency three-band crossover (used when Linear Phase is off), with a
    choice of 6, 12, 18 or 24 dB/oct slopes. The bands always sum to a flat response:

      6 dB/oct:  1st-order low/high pair, high = x - low. Sums back to x exactly.
      12 dB/oct: 2nd-order Linkwitz-Riley (high side inverted). Sums to a 1st-order all-pass.
      18 dB/oct: 3rd-order Butterworth. Sums to a 2nd-order all-pass (Q = 1).
      24 dB/oct: 4th-order Linkwitz-Riley. Sums to a 2nd-order all-pass (Q = 0.707).

    The input is split at the high frequency, the low part is split again at the
    low frequency, and the high band goes through the matching all-pass at the
    low frequency, so all three bands stay phase-aligned with each other.
    The filters are trapezoidal (TPT) one-poles and state-variable filters, so
    the split frequencies can move smoothly without allocating.
*/
template <typename T>
class MinPhaseCrossoverCore
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        reset();
    }

    void reset()
    {
        for (auto* s : { &split2, &split1 })
            s->reset();
        compPole.reset();
        compSvf.reset();
    }

    void process (const juce::AudioBuffer<float>& input, int numSamples,
                  juce::AudioBuffer<float>& low, juce::AudioBuffer<float>& mid,
                  juce::AudioBuffer<float>& high, juce::AudioBuffer<float>& dry,
                  float lowHz, bool lowOn, float highHz, bool highOn, int order)
    {
        order = juce::jlimit (1, 4, order);
        const float nyquistLimit = (float) (fs * 0.45);
        highHz = juce::jlimit (20.0f, nyquistLimit, highHz);
        lowHz  = juce::jlimit (20.0f, nyquistLimit, highOn ? std::min (lowHz, highHz) : lowHz);
        const T g2 = warp (highHz), g1 = warp (lowHz);

        const int numCh = std::min ({ input.getNumChannels(), low.getNumChannels(), 2 });
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
                const T in = (T) x[i];

                T lowPart = in, highPart = 0, lo = 0, mi = 0, hi = 0;
                if (highOn)
                    split2.process (ch, in, g2, order, lowPart, highPart);

                if (lowOn)
                {
                    split1.process (ch, lowPart, g1, order, lo, mi);
                    hi = highOn ? allpass (ch, highPart, g1, order) : T (0);
                }
                else
                {
                    mi = lowPart;
                    hi = highPart;
                }
                l[i] = (float) lo;
                m[i] = (float) mi;
                h[i] = (float) hi;
            }
        }
    }

private:
    // Trapezoidal one-pole: low-pass and its complementary high-pass.
    struct OnePole
    {
        T s[2] {};
        void reset() { s[0] = s[1] = 0; }
        void process (int ch, T x, T g, T& lp, T& hp) noexcept
        {
            const T v = (x - s[ch]) * g / (T (1) + g);
            lp = v + s[ch];
            s[ch] = lp + v;
            hp = x - lp;
        }
    };

    // Trapezoidal state-variable filter (k = 1 / Q).
    struct Svf
    {
        T ic1[2] {}, ic2[2] {};
        void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0; }
        void process (int ch, T x, T g, T k, T& lp, T& bp, T& hp) noexcept
        {
            const T a1 = T (1) / (T (1) + g * (g + k));
            const T a2 = g * a1, a3 = g * a2;
            const T v3 = x - ic2[ch];
            const T v1 = a1 * ic1[ch] + a2 * v3;
            const T v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = T (2) * v1 - ic1[ch];
            ic2[ch] = T (2) * v2 - ic2[ch];
            lp = v2;
            bp = v1;
            hp = x - k * v1 - v2;
        }
    };

    // One split point at the given slope.
    struct Split
    {
        OnePole pole;
        Svf svfLow, svfHigh, svfLow2, svfHigh2;
        void reset() { pole.reset(); svfLow.reset(); svfHigh.reset(); svfLow2.reset(); svfHigh2.reset(); }

        void process (int ch, T x, T g, int order, T& lo, T& hi) noexcept
        {
            T lp, bp, hp;
            switch (order)
            {
                case 1:   // 1st order: lo + hi = x
                    pole.process (ch, x, g, lo, hi);
                    break;

                case 2:   // Linkwitz-Riley 2nd order (Q = 0.5), high side inverted: lo + hi = all-pass
                    svfLow.process (ch, x, g, T (2), lp, bp, hp);
                    lo = lp;
                    hi = -hp;
                    break;

                case 4:   // Linkwitz-Riley 4th order = Butterworth 2nd order squared: lo + hi = all-pass
                {
                    T unused, l1, h1;
                    svfLow.process (ch, x, g, kButterworth, l1, unused, unused);
                    svfLow2.process (ch, l1, g, kButterworth, lo, unused, unused);
                    svfHigh.process (ch, x, g, kButterworth, unused, unused, h1);
                    svfHigh2.process (ch, h1, g, kButterworth, unused, unused, hi);
                    break;
                }

                default:  // Butterworth 3rd order = 1st order x 2nd order (Q = 1): lo + hi = all-pass
                {
                    T pl, ph, unused;
                    pole.process (ch, x, g, pl, ph);
                    svfLow.process (ch, pl, g, T (1), lo, unused, unused);
                    svfHigh.process (ch, ph, g, T (1), unused, unused, hi);
                    break;
                }
            }
        }
    };

    // The all-pass that a split at this frequency adds (applied to the high band).
    T allpass (int ch, T x, T g, int order) noexcept
    {
        T lp, bp, hp;
        switch (order)
        {
            case 1:  return x;
            case 2:  compPole.process (ch, x, g, lp, hp); return lp - hp;
            case 4:  compSvf.process (ch, x, g, kButterworth, lp, bp, hp); return x - T (2) * kButterworth * bp;
            default: compSvf.process (ch, x, g, T (1), lp, bp, hp); return x - T (2) * bp;
        }
    }

    T warp (float hz) const noexcept
    {
        return (T) std::tan (juce::MathConstants<double>::pi * (double) hz / fs);
    }

    static constexpr T kButterworth = (T) 1.4142135623730951;   // k = 1 / Q for Q = 0.707

    double fs = 48000.0;
    Split split2, split1;
    OnePole compPole;
    Svf compSvf;
};

//==============================================================================
/** The crossover used with Linear Phase off. Clean XOV runs the same filters in double
    precision (lower noise and rounding at low split frequencies); otherwise float. */
class MinPhaseCrossover
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/)
    {
        single.prepare (sampleRate);
        precise.prepare (sampleRate);
    }

    void reset()
    {
        single.reset();
        precise.reset();
    }

    /** Splits `input` into the three band buffers; `dry` gets an undelayed copy.
        `order` is 1..4 (6, 12, 18 or 24 dB/oct). */
    void process (const juce::AudioBuffer<float>& input, int numSamples,
                  juce::AudioBuffer<float>& low, juce::AudioBuffer<float>& mid,
                  juce::AudioBuffer<float>& high, juce::AudioBuffer<float>& dry,
                  float lowHz, bool lowOn, float highHz, bool highOn, int order, bool clean)
    {
        if (clean != lastClean)
        {
            // switching precision: start the other set of filters from silence
            if (clean) precise.reset();
            else       single.reset();
            lastClean = clean;
        }
        if (clean)
            precise.process (input, numSamples, low, mid, high, dry, lowHz, lowOn, highHz, highOn, order);
        else
            single.process (input, numSamples, low, mid, high, dry, lowHz, lowOn, highHz, highOn, order);
    }

private:
    MinPhaseCrossoverCore<float> single;
    MinPhaseCrossoverCore<double> precise;
    bool lastClean = false;
};

} // namespace ott
