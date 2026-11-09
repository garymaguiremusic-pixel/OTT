#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../Parameters.h"
#include <array>

namespace ott
{

//==============================================================================
/**
    One band of the compressor, stereo-linked (the same code runs for every band).

    Per sample:
      - the band is multiplied by its input gain (In Gain x (0.52 Depth + 1), given per sample)
      - detector: p = L^2 + R^2 + 1e-25, one-pole smoothed (RMS window), level = sqrt (p)
        (with RMS off the smoothing is skipped: a peak-style detector)
      - envelope (linear): rises towards the level with the attack coefficient, otherwise
        decays by the release coefficient; values below 1e-10 are flushed to 0
      - gain from the envelope in dB:
            above the down threshold:  (downRatio - 1) * (env - downThr), linear gain >= 0.01
            below the up threshold:    min ((upThr - env) * upAmount, +36 dB)
            otherwise:                 0 dB
      - look-ahead: the gain goes on the band sample kLookaheadSamples earlier

    Additions on top of that engine:
      - Amount scales the gain change (0 = none, 1 = full, up to 1.5)
      - Soft knee (off by default) rounds both thresholds over kSoftKneeDb
      - Auto release: a fast and a slow envelope, the band follows the higher one, so short
        peaks recover quickly and sustained compression slowly
      - Auto gain (make-up): keeps 8 s of the band's incoming level (10 ms steps) and works
        out the exact loudness change the current gain curve applies to it; the make-up
        cancels that. A setting change is compensated at once (the history is re-evaluated),
        and a comparison with the real output corrects for attack / release.
      - The band's output gain (spec "Gain L/M/H") and the power button (off = the band,
        delayed to match, passes untouched).
*/
class BandCompressor
{
public:
    struct Settings
    {
        float downThresholdDb = -30.0f, upThresholdDb = -40.0f;
        float downSlope = 0.0f;          // downRatio - 1  (<= 0)
        float upAmount = 0.0f;           // dB of gain per dB below the up threshold
        float attackMs = 5.0f, releaseMs = 50.0f, timeScale = 1.0f;
        float amount = 1.0f;
        float outputGain = 1.0f;         // band Gain, linear
        bool active = true, softKnee = false, rms = true, autoRelease = false, autoGain = false;
    };

    struct Meter
    {
        float envelopeDb = kMinDb;  // detector envelope
        float gainDb = 0.0f;        // gain change applied by the compressor
        float makeupDb = 0.0f;      // auto gain make-up
    };

    void prepare (double fs)
    {
        sampleRate = fs;
        outGain.reset (fs, 0.02);
        activeMix.reset (fs, 0.03);
        rmsAlpha = 1.0f - coefFor (Engine::kRmsWindowMs);
        makeupAlpha = 1.0f - coefFor (100.0f);
        blockLength = std::max (1, juce::roundToInt (0.01 * fs));
        setSettings (settings);
        reset();
    }

    void reset()
    {
        smoothedPower = 0.0f;
        envelope = fastEnvelope = slowEnvelope = 0.0f;
        for (auto* ring : { &ringY, &ringX })
            for (auto& ch : *ring)
                ch.fill (0.0f);
        ringPos = 0;
        resetMakeup();
        makeupGain = makeupTarget = 1.0f;
        outGain.setCurrentAndTargetValue (settings.outputGain);
        activeMix.setCurrentAndTargetValue (settings.active ? 1.0f : 0.0f);
        meter = {};
    }

    void setSettings (const Settings& s)
    {
        using juce::exactlyEqual;
        const bool curveChanged = ! exactlyEqual (s.downThresholdDb, settings.downThresholdDb)
                               || ! exactlyEqual (s.upThresholdDb, settings.upThresholdDb)
                               || ! exactlyEqual (s.downSlope, settings.downSlope)
                               || ! exactlyEqual (s.upAmount, settings.upAmount)
                               || ! exactlyEqual (s.amount, settings.amount)
                               || s.softKnee != settings.softKnee || s.rms != settings.rms;
        const bool switchedOn = s.autoGain && ! settings.autoGain;
        settings = s;

        outGain.setTargetValue (s.outputGain);
        activeMix.setTargetValue (s.active ? 1.0f : 0.0f);

        const float t = std::max (Engine::kMinTime, s.timeScale);
        attackAlpha  = 1.0f - coefFor (std::max (0.01f, s.attackMs));
        releaseCoef  = coefFor (std::max (0.1f, s.releaseMs));
        fastReleaseCoef = coefFor (60.0f * t);
        slowAttackAlpha = 1.0f - coefFor (std::max (250.0f * t, s.attackMs));
        slowReleaseCoef = coefFor (1200.0f * t);

        if (curveChanged)
            blocksSinceChange = 0;               // the ballistics correction re-learns from new blocks
        if ((curveChanged || switchedOn) && settings.autoGain)
            updateMakeupTarget();                // re-evaluate the history with the new curve
    }

    /** Static gain curve in dB for an envelope level in dB (before attack/release). */
    float computeGainDb (float envDb) const noexcept
    {
        const float knee = settings.softKnee ? Engine::kSoftKneeDb : 0.0f;
        float gain = 0.0f;

        // downward, above the down threshold
        const float over = envDb - settings.downThresholdDb;
        if (knee > 0.0f && std::abs (over) * 2.0f <= knee)
            gain += settings.downSlope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
        else if (over > 0.0f)
            gain += settings.downSlope * over;

        // upward, below the up threshold (not for silence)
        if (envDb > Engine::kUpwardFloorDb)
        {
            const float under = settings.upThresholdDb - envDb;
            float up = 0.0f;
            if (knee > 0.0f && std::abs (under) * 2.0f <= knee)
                up = settings.upAmount * (under + knee * 0.5f) * (under + knee * 0.5f) / (2.0f * knee);
            else if (under > 0.0f)
                up = settings.upAmount * under;
            gain += std::min (up, Engine::kMaxBoostDb);
        }

        gain *= settings.amount;
        return juce::jlimit (-40.0f, Engine::kMaxBoostDb, gain);   // floor: linear 0.01
    }

    /** Processes one band in place. `inputGain` holds the per-sample input gain. Mono
        (numChannels == 1) is treated as L = R. */
    void process (float* const* data, int numChannels, int numSamples, const float* inputGain)
    {
        float* l = data[0];
        float* r = numChannels > 1 ? data[1] : data[0];
        const bool stereo = numChannels > 1;

        float lastGainDb = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float xl = l[i], xr = stereo ? r[i] : l[i];
            const float yl = xl * inputGain[i], yr = xr * inputGain[i];

            // detector
            const float p = yl * yl + yr * yr + Engine::kDetectorOffset;
            float level;
            if (settings.rms)
            {
                smoothedPower += rmsAlpha * (p - smoothedPower);
                level = std::sqrt (smoothedPower);
            }
            else
            {
                level = std::sqrt (p);
            }

            // envelope
            if (settings.autoRelease)
            {
                fastEnvelope = level > fastEnvelope ? fastEnvelope + (level - fastEnvelope) * attackAlpha
                                                    : fastEnvelope * fastReleaseCoef;
                slowEnvelope = level > slowEnvelope ? slowEnvelope + (level - slowEnvelope) * slowAttackAlpha
                                                    : slowEnvelope * slowReleaseCoef;
                if (fastEnvelope < Engine::kEnvelopeFloor) fastEnvelope = 0.0f;
                if (slowEnvelope < Engine::kEnvelopeFloor) slowEnvelope = 0.0f;
                envelope = std::max (fastEnvelope, slowEnvelope);
            }
            else
            {
                envelope = level > envelope ? envelope + (level - envelope) * attackAlpha
                                            : envelope * releaseCoef;
                if (envelope < Engine::kEnvelopeFloor) envelope = 0.0f;
                fastEnvelope = slowEnvelope = envelope;   // ready for a seamless switch to auto
            }

            const float envDb = envelope > 0.0f ? 20.0f * std::log10 (envelope) : -1000.0f;
            const float gainDb = computeGainDb (envDb);
            lastGainDb = gainDb;
            const float g = std::pow (10.0f, gainDb * 0.05f);

            // look-ahead: write the current samples, read the ones kLookaheadSamples earlier
            ringY[0][(size_t) ringPos] = yl;  ringY[1][(size_t) ringPos] = yr;
            ringX[0][(size_t) ringPos] = xl;  ringX[1][(size_t) ringPos] = xr;
            const int readPos = (ringPos - Engine::kLookaheadSamples + kRingSize) % kRingSize;
            ringPos = (ringPos + 1) % kRingSize;
            const float dyl = ringY[0][(size_t) readPos], dyr = ringY[1][(size_t) readPos];
            const float dxl = ringX[0][(size_t) readPos], dxr = ringX[1][(size_t) readPos];

            float zl = dyl * g, zr = dyr * g;

            // auto gain: collect the incoming level, then apply the make-up
            blockIn += (double) (yl * yl + yr * yr) * 0.5;
            blockLevel += (double) std::max (kMinDb, envDb);
            blockOut += (double) (zl * zl + zr * zr) * 0.5;
            if (++blockCount >= blockLength)
                pushMakeupBlock();
            makeupGain += makeupAlpha * ((settings.autoGain ? makeupTarget : 1.0f) - makeupGain);

            const float post = outGain.getNextValue() * makeupGain;
            zl *= post;
            zr *= post;

            // band power button: crossfade to the untouched (delayed) band
            const float a = activeMix.getNextValue();
            l[i] = dxl + a * (zl - dxl);
            if (stereo)
                r[i] = dxr + a * (zr - dxr);
        }

        if (smoothedPower < 1.0e-30f) smoothedPower = 0.0f;   // denormal guard

        meter.envelopeDb = envelope > 0.0f ? std::max (kMinDb, juce::Decibels::gainToDecibels (envelope)) : kMinDb;
        meter.gainDb = settings.active ? lastGainDb : 0.0f;
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
    // (each 10 ms step weighted by its power, its gain taken at the envelope's level).
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

    juce::SmoothedValue<float> outGain { 1.0f }, activeMix { 1.0f };

    // detector / envelope
    float smoothedPower = 0.0f, envelope = 0.0f, fastEnvelope = 0.0f, slowEnvelope = 0.0f;
    float rmsAlpha = 0.0f, attackAlpha = 0.0f, releaseCoef = 0.0f;
    float fastReleaseCoef = 0.0f, slowAttackAlpha = 0.0f, slowReleaseCoef = 0.0f;

    // look-ahead
    static constexpr int kRingSize = 4;
    std::array<std::array<float, kRingSize>, 2> ringY {}, ringX {};
    int ringPos = 0;

    // auto gain
    static constexpr int kHistory = 800;              // 800 x 10 ms = 8 s
    std::array<float, kHistory> historyPower {}, historyLevel {}, historyActual {}, historyPredicted {};
    double blockIn = 0.0, blockLevel = 0.0, blockOut = 0.0;
    int blockLength = 480, blockCount = 0, historyCount = 0, historyIndex = 0, blocksSinceChange = 0;
    double ballistics = 1.0;
    float makeupGain = 1.0f, makeupTarget = 1.0f, makeupAlpha = 0.0f;
};

} // namespace ott
