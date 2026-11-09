#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using namespace ott;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout OttAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto floatParam = [&layout] (const String& id, const String& name, NormalisableRange<float> range, float def,
                                 std::function<String (float)> toText, std::function<float (const String&)> fromText = nullptr)
    {
        auto attributes = AudioParameterFloatAttributes()
            .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
            .withValueFromStringFunction ([fromText] (const String& s) { return fromText ? fromText (s) : s.getFloatValue(); });
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def, attributes));
    };

    auto boolParam = [&layout] (const String& id, const String& name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };

    auto db       = [] (float v) { return formatDb (v, 1); };
    auto percent  = [] (float v) { return formatPercent (v); };
    const int specBandOrder[] = { BandLow, BandMid, BandHigh };   // the spec lists bands L, M, H

    // --- Engine parameters, in the spec's order (0 - 19) ------------------------------
    floatParam (ParamIDs::depth,  "Depth",    { 0.0f, 100.0f }, 100.0f, percent);                       // 0
    floatParam (ParamIDs::time,   "Time",     timeRange(), 100.0f, percent);                            // 1
    floatParam (ParamIDs::inGain, "In Gain",  dbRange (inOutGainFromNorm, normFromInOutGain), 0.0f, db, parseDb);   // 2
    floatParam (ParamIDs::output, "Out Gain", dbRange (inOutGainFromNorm, normFromInOutGain), 0.0f, db, parseDb);   // 3
    boolParam (ParamIDs::cleanXov, "Clean XOV", false);                                                 // 4
    for (int b : specBandOrder)                                                                         // 5 - 7
        floatParam (bandParam (b, BandIDs::threshold), String (bandName (b)) + " Threshold", { -40.0f, 24.0f }, 0.0f, db, parseDb);
    for (int b : specBandOrder)                                                                         // 8 - 10
        floatParam (bandParam (b, BandIDs::output), String (bandName (b)) + " Gain",
                    dbRange (bandGainFromNorm, normFromBandGain), 0.0f, db, parseDb);
    for (int b : specBandOrder)                                                                         // 11 - 13
        boolParam (bandParam (b, BandIDs::bypUp), String (bandName (b)) + " Bypass Upward", false);
    for (int b : specBandOrder)                                                                         // 14 - 16
        boolParam (bandParam (b, BandIDs::bypDown), String (bandName (b)) + " Bypass Downward", false);
    floatParam (ParamIDs::upward,   "Upward Strength",   { 0.0f, 100.0f }, 100.0f, percent);            // 17
    floatParam (ParamIDs::downward, "Downward Strength", { 0.0f, 100.0f }, 100.0f, percent);            // 18
    boolParam (ParamIDs::bypass, "Bypass", false);                                                      // 19

    // --- Additions ---------------------------------------------------------------------
    boolParam (ParamIDs::inOutLink, "In/Out Link", true);
    boolParam (ParamIDs::autoGain, "Auto Gain", false);
    boolParam (ParamIDs::linearPhase, "Linear Phase", true);
    boolParam (ParamIDs::softKnee, "Soft Knee", false);
    boolParam (ParamIDs::rms, "RMS", true);

    boolParam (ParamIDs::highOn, "High Band On", true);
    floatParam (ParamIDs::highFreq, "High Split", logRange (500.0f, 16000.0f, 2500.0f), Engine::kDefaultHighFreq,
                [] (float v) { return formatHz (v); }, parseHz);
    boolParam (ParamIDs::lowOn, "Low Band On", true);
    floatParam (ParamIDs::lowFreq, "Low Split", logRange (20.0f, 1000.0f, 120.0f), Engine::kDefaultLowFreq,
                [] (float v) { return formatHz (v); }, parseHz);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::slope, 1 }, "Crossover Slope",
                                                        StringArray { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct" }, 1));

    for (int b : specBandOrder)
    {
        const String n = bandName (b);
        floatParam (bandParam (b, BandIDs::amount), n + " Amount", { 0.0f, 150.0f }, 100.0f,
                    [] (float v) { return String (v, 1) + " %"; });
        boolParam  (bandParam (b, BandIDs::active), n + " Active", true);
        boolParam  (bandParam (b, BandIDs::solo),   n + " Solo", false);
        floatParam (bandParam (b, BandIDs::attack),  n + " Attack",  logRange (0.1f, 1000.0f, 20.0f), Engine::defaultAttackMs (b),
                    [] (float v) { return formatMs (v); });
        floatParam (bandParam (b, BandIDs::release), n + " Release", logRange (1.0f, 5000.0f, 250.0f), Engine::defaultReleaseMs (b),
                    [] (float v) { return formatMs (v); });
        boolParam  (bandParam (b, BandIDs::autoRelease), n + " Auto Release", false);
        boolParam  (bandParam (b, BandIDs::autoGain), n + " Auto Gain", false);
    }

    return layout;
}

//==============================================================================
OttAudioProcessor::OttAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "OTTSTATE", createParameterLayout())
{
    auto raw = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };
    p.depth = raw (ParamIDs::depth);         p.time = raw (ParamIDs::time);
    p.inGain = raw (ParamIDs::inGain);       p.outGain = raw (ParamIDs::output);
    p.cleanXov = raw (ParamIDs::cleanXov);   p.upStrength = raw (ParamIDs::upward);
    p.downStrength = raw (ParamIDs::downward);
    p.bypass = raw (ParamIDs::bypass);       p.inOutLink = raw (ParamIDs::inOutLink);
    p.autoGain = raw (ParamIDs::autoGain);   p.linearPhase = raw (ParamIDs::linearPhase);
    p.softKnee = raw (ParamIDs::softKnee);   p.rms = raw (ParamIDs::rms);
    p.highOn = raw (ParamIDs::highOn);       p.highFreq = raw (ParamIDs::highFreq);
    p.lowOn = raw (ParamIDs::lowOn);         p.lowFreq = raw (ParamIDs::lowFreq);
    p.slope = raw (ParamIDs::slope);

    for (int b = 0; b < NumBands; ++b)
    {
        auto& bp = p.band[(size_t) b];
        bp.threshold = raw (bandParam (b, BandIDs::threshold));
        bp.gain = raw (bandParam (b, BandIDs::output));
        bp.bypUp = raw (bandParam (b, BandIDs::bypUp));
        bp.bypDown = raw (bandParam (b, BandIDs::bypDown));
        bp.amount = raw (bandParam (b, BandIDs::amount));
        bp.active = raw (bandParam (b, BandIDs::active));
        bp.solo = raw (bandParam (b, BandIDs::solo));
        bp.attack = raw (bandParam (b, BandIDs::attack));
        bp.release = raw (bandParam (b, BandIDs::release));
        bp.autoRelease = raw (bandParam (b, BandIDs::autoRelease));
        bp.autoGain = raw (bandParam (b, BandIDs::autoGain));

        for (auto* v : { bp.threshold, bp.gain, bp.bypUp, bp.bypDown, bp.amount, bp.active,
                         bp.attack, bp.release, bp.autoRelease, bp.autoGain })
            watchedParams.push_back (v);
    }
    for (auto* v : { p.depth, p.time, p.upStrength, p.downStrength, p.softKnee, p.rms })
        watchedParams.push_back (v);
    lastWatched.assign (watchedParams.size(), std::numeric_limits<float>::quiet_NaN());

    startTimerHz (30);
}

OttAudioProcessor::~OttAudioProcessor()
{
    stopTimer();
}

int OttAudioProcessor::slopeOrder() const
{
    return juce::jlimit (1, 4, juce::roundToInt (p.slope->load()) + 1);
}

juce::AudioProcessorParameter* OttAudioProcessor::getBypassParameter() const
{
    return apvts.getParameter (ParamIDs::bypass);
}

//==============================================================================
bool OttAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void OttAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = std::max (32, samplesPerBlock);
    currentSampleRate = sampleRate;
    const int numCh = std::max (1, std::min (2, getTotalNumOutputChannels()));

    crossover.prepare (sampleRate, maxBlock, numCh,
                       p.lowFreq->load(), on (p.lowOn), p.highFreq->load(), on (p.highOn), slopeOrder());
    minPhaseCrossover.prepare (sampleRate, maxBlock, numCh);

    linearActive = on (p.linearPhase);
    activeSlope = slopeOrder();
    activeClean = on (p.cleanXov);
    updateLatency (linearActive);

    inputChunk.setSize (numCh, maxBlock);
    dryDelayed.setSize (numCh, maxBlock);
    for (auto& b : bands)
        b.setSize (numCh, maxBlock);
    preGainBuffer.assign ((size_t) maxBlock, 1.0f);
    depthBuffer.assign ((size_t) maxBlock, 1.0f);
    outGainBuffer.assign ((size_t) maxBlock, 1.0f);

    // Spec smoothing: coef = 44100 / (fs * 300 + 44100), treating fs < 44000 as 44100.
    const double fsForSmoothing = sampleRate < Engine::kSmoothMinFs ? Engine::kSmoothRate : sampleRate;
    smoothCoef = Engine::kSmoothRate / (fsForSmoothing * Engine::kSmoothScale + Engine::kSmoothRate);

    for (auto& c : compressors)
        c.prepare (sampleRate);

    {
        // K-weighting (ITU-R BS.1770): the loudness measure behind LUFS.
        auto shelf = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, 1681.97, 0.7072, juce::Decibels::decibelsToGain (4.0f));
        auto hp    = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 38.13, 0.5003);
        for (auto* k : { &refWeighting, &outWeighting })
            for (int ch = 0; ch < 2; ++ch)
            {
                k->shelf[(size_t) ch].coefficients = shelf;
                k->highPass[(size_t) ch].coefficients = hp;
            }
    }
    loudnessBlockLength = std::max (1, juce::roundToInt (0.1 * sampleRate));   // 100 ms blocks, as in BS.1770

    modeFade.reset (sampleRate, 0.01);
    bypassMix.reset (sampleRate, 0.03);
    randomFade.reset (sampleRate, 0.3);
    for (auto& s : bandSoloGain)
        s.reset (sampleRate, 0.02);

    reset();
}

void OttAudioProcessor::reset()
{
    crossover.reset();
    minPhaseCrossover.reset();
    for (auto& c : compressors)
        c.reset();
    for (auto& ch : dryRing)
        ch.fill (0.0f);
    dryRingPos = 0;
    peak = {};

    depthSmoothed   = p.depth->load() * 0.01f;
    inGainSmoothed  = dbToGain (p.inGain->load());
    outGainSmoothed = dbToGain (p.outGain->load());

    for (auto* k : { &refWeighting, &outWeighting })
        for (int ch = 0; ch < 2; ++ch)
        {
            k->shelf[(size_t) ch].reset();
            k->highPass[(size_t) ch].reset();
        }
    resetLoudness();
    autoGainDb = 0.0f;

    modeFade.setCurrentAndTargetValue (1.0f);
    bypassMix.setCurrentAndTargetValue (on (p.bypass) ? 1.0f : 0.0f);
    randomFade.setCurrentAndTargetValue (1.0f);
    for (auto& s : bandSoloGain)
        s.setCurrentAndTargetValue (1.0f);

    std::fill (lastWatched.begin(), lastWatched.end(), std::numeric_limits<float>::quiet_NaN());
    if (settingsChanged())
        updateBandSettings();
}

void OttAudioProcessor::releaseResources()
{
    crossover.reset();
    minPhaseCrossover.reset();
}

void OttAudioProcessor::resetLoudness()
{
    loudnessRef.fill (0.0);
    loudnessOut.fill (0.0);
    loudnessBlocks = loudnessIndex = blockCount = 0;
    blockRef = blockOut = 0.0;
    autoGainTargetDb = autoGainDb;
}

void OttAudioProcessor::pushLoudnessBlock()
{
    loudnessRef[(size_t) loudnessIndex] = blockRef;
    loudnessOut[(size_t) loudnessIndex] = blockOut;
    loudnessIndex = (loudnessIndex + 1) % kLoudnessBlocks;
    loudnessBlocks = std::min (loudnessBlocks + 1, kLoudnessBlocks);
    blockRef = blockOut = 0.0;
    blockCount = 0;

    double ref = 0.0, out = 0.0;
    for (int i = 0; i < kLoudnessBlocks; ++i)
    {
        ref += loudnessRef[(size_t) i];
        out += loudnessOut[(size_t) i];
    }

    // Mean square over the window; hold the gain through silence (input below -60 dB).
    const double samples = (double) loudnessBlocks * (double) loudnessBlockLength;
    if (ref / samples > 1.0e-6 && out / samples > 1.0e-9)
        autoGainTargetDb = juce::jlimit (-24.0f, 24.0f, (float) (10.0 * std::log10 (ref / out)));
}

void OttAudioProcessor::updateLatency (bool linear)
{
    const int samples = (linear ? crossover.getLatency() : 0) + Engine::kLookaheadSamples;
    reportedLinear = linear;
    setLatencySamples (samples);
    latencyMs.store (1000.0f * (float) samples / (float) currentSampleRate);
}

void OttAudioProcessor::timerCallback()
{
    // Filter design runs here, on the message thread; the audio thread just picks it up.
    crossover.updateSettings (p.lowFreq->load(), on (p.lowOn), p.highFreq->load(), on (p.highOn), slopeOrder());

    // Linear phase on/off changes the latency; tell the host from the message thread.
    if (currentSampleRate > 0.0 && on (p.linearPhase) != reportedLinear)
        updateLatency (on (p.linearPhase));
}

bool OttAudioProcessor::settingsChanged()
{
    bool changed = false;
    for (size_t i = 0; i < watchedParams.size(); ++i)
    {
        const float v = watchedParams[i]->load();
        if (! juce::exactlyEqual (v, lastWatched[i]))
        {
            lastWatched[i] = v;
            changed = true;
        }
    }
    return changed;
}

// Coefficient recalculation (only when a parameter it depends on changes).
void OttAudioProcessor::updateBandSettings()
{
    const float depth = p.depth->load() * 0.01f;
    const float T = std::max (Engine::kMinTime, p.time->load() * 0.01f);
    const float s = juce::jlimit (0.0f, 1.0f, 2.0f * depth * p.downStrength->load() * 0.01f);
    const float upAmount = depth * p.upStrength->load() * 0.01f * Engine::kUpAmountScale;

    for (int b = 0; b < NumBands; ++b)
    {
        const auto& bp = p.band[(size_t) b];
        const float offset = bp.threshold->load();
        const float downRatio = on (bp.bypDown) ? 1.0f : 1.0f - s * Engine::kDownRatioScale[(size_t) b];

        BandCompressor::Settings c;
        c.downThresholdDb = offset - Engine::kDownThresholdDb[(size_t) b];
        c.upThresholdDb   = offset - Engine::kUpThresholdDb[(size_t) b];
        c.downSlope       = downRatio - 1.0f;
        c.upAmount        = on (bp.bypUp) ? 0.0f : upAmount;
        c.attackMs        = T * bp.attack->load();
        c.releaseMs       = T * bp.release->load();
        c.timeScale       = T;
        c.amount          = bp.amount->load() * 0.01f;
        c.outputGain      = dbToGain (bp.gain->load());
        c.active          = on (bp.active);
        c.softKnee        = on (p.softKnee);
        c.rms             = on (p.rms);
        c.autoRelease     = on (bp.autoRelease);
        c.autoGain        = on (bp.autoGain);
        compressors[(size_t) b].setSettings (c);
    }
}

void OttAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = std::min (inputChunk.getNumChannels(), buffer.getNumChannels());
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (settingsChanged())
        updateBandSettings();

    if (randomRestartPending.exchange (false))
    {
        for (auto& c : compressors)
            c.reset();
        randomFade.setCurrentAndTargetValue (0.0f);
        randomFade.setTargetValue (1.0f);
    }

    bool anySolo = false;
    for (const auto& bp : p.band)
        anySolo = anySolo || on (bp.solo);
    for (int b = 0; b < NumBands; ++b)
        bandSoloGain[(size_t) b].setTargetValue (! anySolo || on (p.band[(size_t) b].solo) ? 1.0f : 0.0f);
    bypassMix.setTargetValue (on (p.bypass) ? 1.0f : 0.0f);

    const float depthTarget = p.depth->load() * 0.01f;
    const float inTarget    = dbToGain (p.inGain->load());
    const float outTarget   = dbToGain (p.outGain->load());
    const float lowHz = p.lowFreq->load(), highHz = p.highFreq->load();
    const bool lowOn = on (p.lowOn), highOn = on (p.highOn);

    // Switching linear phase on/off (or the zero-latency slope / precision): fade out, swap,
    // fade back in. In linear phase the slope changes through the crossover's own crossfade.
    const bool wantLinear = on (p.linearPhase), wantClean = on (p.cleanXov);
    const int wantSlope = slopeOrder();
    const bool needSwitch = wantLinear != linearActive
                         || (! wantLinear && (wantSlope != activeSlope || wantClean != activeClean));
    if (needSwitch && ! modeFade.isSmoothing())
    {
        if (modeFade.getTargetValue() > 0.5f)
        {
            modeFade.setTargetValue (0.0f);
        }
        else
        {
            linearActive = wantLinear;
            activeSlope = wantSlope;
            activeClean = wantClean;
            if (linearActive) crossover.reset();
            else              minPhaseCrossover.reset();
            for (auto& c : compressors)
                c.reset();
            modeFade.setTargetValue (1.0f);
        }
    }
    else if (! needSwitch && ! modeFade.isSmoothing() && modeFade.getTargetValue() < 0.5f)
    {
        modeFade.setTargetValue (1.0f);
    }

    const bool agOn = on (p.autoGain);
    if (agOn && ! autoGainWasOn)
        resetLoudness();                                          // start measuring afresh
    autoGainWasOn = agOn;

    const float c = (float) smoothCoef;

    // Process in chunks no longer than the prepared block size.
    for (int start = 0; start < buffer.getNumSamples(); start += maxBlock)
    {
        const int n = std::min (maxBlock, buffer.getNumSamples() - start);

        for (int ch = 0; ch < numCh; ++ch)
        {
            inputChunk.copyFrom (ch, 0, buffer, ch, start, n);

            // input peak meter, linear decay per sample
            const float* x = inputChunk.getReadPointer (ch);
            float pk = peak[(size_t) ch];
            for (int i = 0; i < n; ++i)
                pk = std::max (std::abs (x[i]), pk - Engine::kPeakDecayPerSample);
            peak[(size_t) ch] = std::max (0.0f, pk);
        }

        if (linearActive)
            crossover.process (inputChunk, n, bands[BandLow], bands[BandMid], bands[BandHigh], dryDelayed);
        else
            minPhaseCrossover.process (inputChunk, n, bands[BandLow], bands[BandMid], bands[BandHigh], dryDelayed,
                                       lowHz, lowOn, highHz, highOn, activeSlope, activeClean);

        // The dry path gets the same look-ahead delay as the bands.
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* d = dryDelayed.getWritePointer (ch);
            auto& ring = dryRing[(size_t) ch];
            int pos = dryRingPos;
            for (int i = 0; i < n; ++i)
            {
                ring[(size_t) pos] = d[i];
                d[i] = ring[(size_t) ((pos - Engine::kLookaheadSamples + 4) % 4)];
                pos = (pos + 1) % 4;
            }
        }
        dryRingPos = (dryRingPos + n) % 4;

        // Smoothed Depth / In / Out, and each band's input gain: In Gain x (0.52 Depth + 1).
        // (In Gain goes on after the linear crossover, so the dry path stays the untouched input.)
        for (int i = 0; i < n; ++i)
        {
            depthSmoothed   += c * (depthTarget - depthSmoothed);
            inGainSmoothed  += c * (inTarget - inGainSmoothed);
            outGainSmoothed += c * (outTarget - outGainSmoothed);
            depthBuffer[(size_t) i]   = depthSmoothed;
            outGainBuffer[(size_t) i] = outGainSmoothed;
            preGainBuffer[(size_t) i] = inGainSmoothed * (Engine::kInputDepthScale * depthSmoothed + 1.0f);
        }

        for (int b = 0; b < NumBands; ++b)
            compressors[(size_t) b].process (bands[(size_t) b].getArrayOfWritePointers(), numCh, n, preGainBuffer.data());

        // Auto Gain: K-weighted loudness of the input vs. the processed output over the last
        // second (sliding window of 100 ms blocks), gliding over ~100 ms, ramped across this chunk.
        const float agTarget = agOn ? autoGainTargetDb : 0.0f;
        const float agStart = juce::Decibels::decibelsToGain (autoGainDb);
        autoGainDb = agTarget + (autoGainDb - agTarget) * std::exp (-(float) n / (0.1f * (float) currentSampleRate));
        const float agStep = (juce::Decibels::decibelsToGain (autoGainDb) - agStart) / (float) n;
        float ag = agStart;

        for (int i = 0; i < n; ++i)
        {
            const float D = depthBuffer[(size_t) i];
            const float gH = bandSoloGain[BandHigh].getNextValue() * (Engine::kOutputDepthScale[BandHigh] * D + 1.0f);
            const float gM = bandSoloGain[BandMid].getNextValue()  * (Engine::kOutputDepthScale[BandMid]  * D + 1.0f);
            const float gL = bandSoloGain[BandLow].getNextValue()  * (Engine::kOutputDepthScale[BandLow]  * D + 1.0f);
            const float out = outGainBuffer[(size_t) i];
            const float byp = bypassMix.getNextValue();
            const float fade = modeFade.getNextValue() * randomFade.getNextValue();
            ag += agStep;

            float refSq = 0.0f, outSq = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float dry = dryDelayed.getSample (ch, i);   // the untouched input (latency-aligned)
                const float processed = (gL * bands[BandLow].getSample (ch, i)
                                       + gM * bands[BandMid].getSample (ch, i)
                                       + gH * bands[BandHigh].getSample (ch, i)) * out;

                const float r = refWeighting.process (ch, dry);
                const float o = outWeighting.process (ch, processed);
                refSq += r * r;
                outSq += o * o;

                const float matched = processed * ag;
                buffer.setSample (ch, start + i, fade * (matched + byp * (dry - matched)));
            }
            blockRef += refSq;
            blockOut += outSq;
            if (++blockCount >= loudnessBlockLength)
                pushLoudnessBlock();
        }
    }

    // Metering
    autoGainMeterDb.store (autoGainDb);
    inputPeak[0].store (peak[0]);
    inputPeak[1].store (numCh > 1 ? peak[1] : peak[0]);
    for (int b = 0; b < NumBands; ++b)
    {
        const auto& m = compressors[(size_t) b].getMeter();
        meters[(size_t) b].envelopeDb.store (m.envelopeDb);
        meters[(size_t) b].gainDb.store (m.gainDb);
        meters[(size_t) b].makeupDb.store (m.makeupDb);
    }
}

//==============================================================================
int OttAudioProcessor::getNumPrograms()
{
    return (int) getFactoryPresets().size();
}

const juce::String OttAudioProcessor::getProgramName (int index)
{
    const auto& presets = getFactoryPresets();
    return juce::isPositiveAndBelow (index, (int) presets.size()) ? juce::String (presets[(size_t) index].name) : juce::String();
}

void OttAudioProcessor::setCurrentProgram (int index)
{
    const auto& presets = getFactoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;

    currentProgram.store (index);
    isRandomised.store (false);

    // Everything back to the defaults (but keep bypass, linear phase and the link), then apply the preset.
    for (auto* prm : getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto& id = ranged->getParameterID();
            if (id == ParamIDs::bypass || id == ParamIDs::linearPhase || id == ParamIDs::inOutLink)
                continue;
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
        }
    }

    for (const auto& [id, value] : presets[(size_t) index].values)
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

//==============================================================================
void OttAudioProcessor::randomise()
{
    juce::Random rng;
    auto uniform = [&] (float lo, float hi) { return lo + (hi - lo) * rng.nextFloat(); };
    auto logUniform = [&] (float lo, float hi) { return lo * std::pow (hi / lo, rng.nextFloat()); };
    auto chance = [&] (float pr) { return rng.nextFloat() < pr ? 1.0f : 0.0f; };

    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* prm = apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
            prm->endChangeGesture();
        }
    };

    // Global
    set (ParamIDs::depth,    uniform (30.0f, 100.0f));
    set (ParamIDs::time,     logUniform (25.0f, 400.0f));
    set (ParamIDs::upward,   uniform (0.0f, 100.0f));
    set (ParamIDs::downward, uniform (0.0f, 100.0f));
    set (ParamIDs::softKnee, chance (0.3f));
    set (ParamIDs::rms,      chance (0.8f));
    set (ParamIDs::cleanXov, chance (0.5f));

    // In / Out Gain are left as they are: the gain staging stays where the user set it.

    // Crossover
    set (ParamIDs::highOn,   chance (0.9f));
    set (ParamIDs::lowOn,    chance (0.9f));
    set (ParamIDs::lowFreq,  logUniform (50.0f, 300.0f));
    set (ParamIDs::highFreq, logUniform (1000.0f, 8000.0f));
    set (ParamIDs::slope,    (float) rng.nextInt (4));

    // Bands
    for (int b = 0; b < NumBands; ++b)
    {
        set (bandParam (b, BandIDs::output),      uniform (-6.0f, 3.0f));
        set (bandParam (b, BandIDs::threshold),   uniform (-20.0f, 10.0f));
        set (bandParam (b, BandIDs::bypUp),       chance (0.1f));
        set (bandParam (b, BandIDs::bypDown),     chance (0.1f));
        set (bandParam (b, BandIDs::attack),      logUniform (0.5f, 50.0f));
        set (bandParam (b, BandIDs::release),     logUniform (10.0f, 500.0f));
        set (bandParam (b, BandIDs::autoRelease), chance (0.3f));
        set (bandParam (b, BandIDs::autoGain),    chance (0.5f));
        set (bandParam (b, BandIDs::amount),      uniform (40.0f, 130.0f));
    }

    isRandomised.store (true);
    randomRestartPending.store (true);
}

//==============================================================================
void OttAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram.load(), nullptr);
    state.setProperty ("uiScale", editorScale.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void OttAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram.store (juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("program", 0)));
            editorScale.store (juce::jlimit (0.5f, 2.0f, (float) state.getProperty ("uiScale", 1.0f)));
            apvts.replaceState (state);
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* OttAudioProcessor::createEditor()
{
    return new OttAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OttAudioProcessor();
}
