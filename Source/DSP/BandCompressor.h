#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../Parameters.h"
#include <array>

namespace ott
{

//==============================================================================
/**
    One band of the OTT: upward + downward compression on a stereo-linked
    detector, in the style of Ableton's Multiband Dynamics.

      - Above threshold / ratio: downward compression (ratio "inf" = limiting).
      - Below threshold / ratio: upward compression (quiet signals are raised
        towards the threshold, capped at +36 dB).
      - Upward and Downward scale the gain change in dB.
      - Time scales attack and release.
      - Auto release (program dependent): two gain envelopes, a fast one (60 ms
        release) and a slow one (250 ms attack, 1.2 s release); the band follows
        whichever compresses more. Short peaks recover quickly, sustained
        compression recovers slowly, so there's less pumping on dense material.
      - Auto gain (make-up): keeps 8 s of the band's incoming level (10 ms steps)
        and works out the exact loudness change the current gain curve applies to
        it; the make-up cancels that. Changing a threshold, ratio or Upward /
        Downward updates it at once (it re-evaluates the history), and it only
        drifts at the pace of an 8 s loudness window, so it doesn't ride the music.
        A running comparison of the real compressor output with the static curve
        corrects for attack / release, so the long-term level matches to ~0.2 dB.
        The band's output gain still trims on top.
*/
class BandCompressor
{
public:
    struct Settings
    {
        float inputDb = 0.0f, outputDb = 0.0f;
        float attackMs = 10.0f, releaseMs = 100.0f;
        float aboveThresh = -30.0f, aboveRatio = 4.0f;
        float belowThresh = -40.0f, belowRatio = 4.0f;
        float upward = 1.0f, downward = 1.0f, timeScale = 1.0f;
        bool active = true, softKnee = true, rms = true, autoRelease = false, autoGain = false;
    };

    struct Meter
    {
        float inputDb = kMinDb;   // detector level (after input gain)
        float outputDb = kMinDb;  // band output peak
        float gainDb = 0.0f;      // gain change applied by the compressor
        float makeupDb = 0.0f;    // auto gain make-up
    };

    void prepare (double fs)
    {
        sampleRate = fs;
        inGain.reset (fs, 0.02);
        outGain.reset (fs, 0.02);
        activeMix.reset (fs, 0.03);
        rmsAlpha = 1.0f - coefFor (10.0f);
        makeupAlpha = 1.0f - coefFor (100.0f);
        blockLength = std::max (1, juce::roundToInt (0.01 * fs));
        reset();
    }

    void reset()
    {
        envMs = 0.0f;
        gainDb = fastDb = slowDb = 0.0f;
        resetMakeup();
        makeupGain = makeupTarget = 1.0f;
        inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (settings.inputDb));
        outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (settings.outputDb));
        activeMix.setCurrentAndTargetValue (settings.active ? 1.0f : 0.0f);
    }

    void setSettings (const Settings& s)
    {
        using juce::exactlyEqual;
        const bool curveChanged = ! exactlyEqual (s.aboveThresh, settings.aboveThresh) || ! exactlyEqual (s.belowThresh, settings.belowThresh)
                               || ! exactlyEqual (s.aboveRatio, settings.aboveRatio) || ! exactlyEqual (s.belowRatio, settings.belowRatio)
                               || ! exactlyEqual (s.upward, settings.upward) || ! exactlyEqual (s.downward, settings.downward)
                               || s.softKnee != settings.softKnee || s.rms != settings.rms;
        const bool switchedOn = s.autoGain && ! settings.autoGain;
        settings = s;
        if (curveChanged)
            blocksSinceChange = 0;               // the ballistics correction re-learns from new blocks
        if ((curveChanged || switchedOn) && settings.autoGain)
            updateMakeupTarget();                // re-evaluate the history with the new curve
        inGain.setTargetValue (juce::Decibels::decibelsToGain (s.inputDb));
        outGain.setTargetValue (juce::Decibels::decibelsToGain (s.outputDb));
        activeMix.setTargetValue (s.active ? 1.0f : 0.0f);

        const float t = std::max (0.01f, s.timeScale);
        attackCoef  = coefFor (std::max (0.01f, s.attackMs * t));
        releaseCoef = coefFor (std::max (0.1f,  s.releaseMs * t));
        fastReleaseCoef = coefFor (60.0f * t);
        slowAttackCoef  = coefFor (std::max (250.0f, s.attackMs) * t);
        slowReleaseCoef = coefFor (1200.0f * t);

        knee = s.softKnee ? 10.0f : 0.0f;
        downSlope = s.aboveRatio >= kRatioInf ? 1.0f : 1.0f - 1.0f / std::max (1.0f, s.aboveRatio);
        upSlope   = s.belowRatio >= kRatioInf ? 1.0f : 1.0f - 1.0f / std::max (1.0f, s.belowRatio);
    }

    /** Static gain curve in dB for a detector level in dB (before attack/release). */
    float computeGainDb (float levelDb) const noexcept
    {
        const float x = juce::jlimit (kMinDb, 24.0f, levelDb);

        // downward, above threshold
        float down = 0.0f;
        const float over = x - settings.aboveThresh;
        if (knee > 0.0f && std::abs (over) * 2.0f <= knee)
            down = -downSlope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
        else if (over > 0.0f)
            down = -downSlope * over;

        // upward, below threshold
        float up = 0.0f;
        const float under = settings.belowThresh - x;
        if (knee > 0.0f && std::abs (under) * 2.0f <= knee)
            up = upSlope * (under + knee * 0.5f) * (under + knee * 0.5f) / (2.0f * knee);
        else if (under > 0.0f)
            up = upSlope * under;

        up = std::min (up, kMaxBoostDb);
        return settings.downward * down + settings.upward * up;
    }

    /** Processes one band in place. `numChannels` is 1 or 2. */
    void process (float* const* data, int numChannels, int numSamples)
    {
        float* l = data[0];
        float* r = numChannels > 1 ? data[1] : data[0];
        const float chanNorm = numChannels > 1 ? 0.5f : 1.0f;

        float maxLevelDb = kMinDb, maxOut = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float xl = l[i], xr = r[i];

            const float gIn = inGain.getNextValue();
            const float yl = xl * gIn, yr = xr * gIn;

            // stereo-linked detector
            float levelDb;
            if (settings.rms)
            {
                envMs += rmsAlpha * ((yl * yl + yr * yr) * chanNorm - envMs);
                levelDb = 10.0f * std::log10 (envMs + 1.0e-12f);
            }
            else
            {
                levelDb = 20.0f * std::log10 (std::max (std::abs (yl), std::abs (yr)) + 1.0e-9f);
            }
            maxLevelDb = std::max (maxLevelDb, levelDb);

            // ballistics on the gain: falling gain = attack, rising gain = release
            const float target = computeGainDb (levelDb);
            if (settings.autoRelease)
            {
                fastDb = target + (target < fastDb ? attackCoef : fastReleaseCoef) * (fastDb - target);
                slowDb = target + (target < slowDb ? slowAttackCoef : slowReleaseCoef) * (slowDb - target);
                gainDb = std::min (fastDb, slowDb);
            }
            else
            {
                const float coef = target < gainDb ? attackCoef : releaseCoef;
                gainDb = target + coef * (gainDb - target);
                fastDb = slowDb = gainDb;   // ready for a seamless switch to auto
            }

            const float g = juce::Decibels::decibelsToGain (gainDb, -200.0f);
            float zl = yl * g, zr = yr * g;

            // auto gain: collect the incoming level, then apply the make-up
            blockIn += (double) (yl * yl + yr * yr) * chanNorm;
            blockLevel += (double) std::max (kMinDb, levelDb);
            blockOut += (double) (zl * zl + zr * zr) * chanNorm;
            if (++blockCount >= blockLength)
                pushMakeupBlock();
            makeupGain += makeupAlpha * ((settings.autoGain ? makeupTarget : 1.0f) - makeupGain);

            const float post = outGain.getNextValue() * makeupGain;
            zl *= post;
            zr *= post;

            // band power button: crossfade to the untouched band
            const float a = activeMix.getNextValue();
            l[i] = xl + a * (zl - xl);
            if (numChannels > 1)
                r[i] = xr + a * (zr - xr);

            maxOut = std::max (maxOut, std::max (std::abs (l[i]), std::abs (numChannels > 1 ? r[i] : 0.0f)));
        }

        // denormal guard
        if (envMs < 1.0e-20f) envMs = 0.0f;

        meter.inputDb = std::max (kMinDb, maxLevelDb);
        meter.outputDb = std::max (kMinDb, juce::Decibels::gainToDecibels (maxOut, -100.0f));
        meter.gainDb = settings.active ? gainDb : 0.0f;
        meter.makeupDb = juce::Decibels::gainToDecibels (makeupGain);
    }

    const Meter& getMeter() const noexcept     { return meter; }

private:
    void resetMakeup()
    {
        historyPower.fill (0.0f);
        historyLevel.fill (kMinDb);
        historyActual.fill (0.0f);
        historyPredicted.fill (0.0f);
        historyCount = historyIndex = blockCount = blocksSinceChange = 0;
        blockIn = blockLevel = blockOut = 0.0;
        ballistics = 1.0;
        makeupTarget = makeupGain;
    }

    void pushMakeupBlock()
    {
        historyPower[(size_t) historyIndex] = (float) (blockIn / (double) blockLength);
        historyLevel[(size_t) historyIndex] = (float) (blockLevel / (double) blockLength);
        // what the compressor really did vs. what the static curve predicts (attack / release
        // smooth the gain, so the real average differs a little from the curve)
        historyActual[(size_t) historyIndex] = (float) (blockOut / (double) blockLength);
        historyPredicted[(size_t) historyIndex] = historyPower[(size_t) historyIndex]
            * (float) std::pow (10.0, 0.1 * (double) computeGainDb (historyLevel[(size_t) historyIndex]));
        historyIndex = (historyIndex + 1) % kHistory;
        historyCount = std::min (historyCount + 1, kHistory);
        blocksSinceChange = std::min (blocksSinceChange + 1, kHistory);
        blockIn = blockLevel = blockOut = 0.0;
        blockCount = 0;
        if (settings.autoGain && historyIndex % 5 == 0)   // every 50 ms (setting changes update at once)
            updateMakeupTarget();
    }

    // Make-up = loudness of the history / loudness of the history after the current gain curve
    // (each 10 ms step weighted by its power, its gain taken at the detector's level).
    void updateMakeupTarget()
    {
        double in = 0.0, out = 0.0;
        for (int i = 0; i < historyCount; ++i)
        {
            const float p = historyPower[(size_t) i];
            if (p < 1.0e-10f)
                continue;
            in  += (double) p;
            out += (double) p * std::pow (10.0, 0.1 * (double) computeGainDb (historyLevel[(size_t) i]));
        }

        // Ballistics correction (limited to +/- 12 dB), learnt only from blocks recorded since the
        // last settings change, skipping the first 0.5 s while the compressor's gain moves over to
        // the new setting; until there's 0.5 s of settled blocks, the previous correction is kept.
        constexpr int settleBlocks = 50;
        if (blocksSinceChange >= 2 * settleBlocks)
        {
            double actual = 0.0, predicted = 0.0;
            for (int k = 1; k <= std::min (blocksSinceChange - settleBlocks, historyCount); ++k)
            {
                const size_t i = (size_t) ((historyIndex - k + kHistory) % kHistory);
                actual += (double) historyActual[i];
                predicted += (double) historyPredicted[i];
            }
            if (predicted > 1.0e-20 && actual > 1.0e-20)
                ballistics = juce::jlimit (0.063, 15.8, actual / predicted);
        }

        // Hold through silence (band below -70 dB on average); +/- 30 dB at most.
        if (historyCount > 0 && in / (double) historyCount > 1.0e-7 && out > 1.0e-20)
            makeupTarget = (float) juce::jlimit (0.0316, 31.6, std::sqrt (in / (out * ballistics)));
    }

    float coefFor (float ms) const
    {
        return std::exp (-1.0f / (0.001f * ms * (float) sampleRate));
    }

    Settings settings;
    Meter meter;
    double sampleRate = 48000.0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inGain { 1.0f }, outGain { 1.0f };
    juce::SmoothedValue<float> activeMix { 1.0f };

    float envMs = 0.0f, gainDb = 0.0f, fastDb = 0.0f, slowDb = 0.0f;
    float attackCoef = 0.0f, releaseCoef = 0.0f, rmsAlpha = 0.0f;
    float fastReleaseCoef = 0.0f, slowAttackCoef = 0.0f, slowReleaseCoef = 0.0f;
    float knee = 10.0f, downSlope = 0.0f, upSlope = 0.0f;

    // auto gain
    static constexpr int kHistory = 800;              // 800 x 10 ms = 8 s
    std::array<float, kHistory> historyPower {}, historyLevel {}, historyActual {}, historyPredicted {};
    double blockIn = 0.0, blockLevel = 0.0, blockOut = 0.0;
    int blockLength = 480, blockCount = 0, historyCount = 0, historyIndex = 0, blocksSinceChange = 0;
    double ballistics = 1.0;
    float makeupGain = 1.0f, makeupTarget = 1.0f, makeupAlpha = 0.0f;
};

} // namespace ott
