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

    auto db       = [] (float v) { return formatDb (v); };
    auto percent  = [] (float v) { return formatPercent (v); };

    // Global
    boolParam (ParamIDs::bypass, "Bypass", false);
    floatParam (ParamIDs::inGain,   "In Gain",  { -24.0f, 24.0f }, 0.0f, db);
    floatParam (ParamIDs::output,   "Out Gain", { -24.0f, 24.0f }, 0.0f, db);
    boolParam (ParamIDs::inOutLink, "In/Out Link", true);
    boolParam (ParamIDs::autoGain, "Auto Gain", false);
    boolParam (ParamIDs::linearPhase, "Linear Phase", true);
    floatParam (ParamIDs::time,     "Time",     logRange (20.0f, 500.0f, 100.0f), 100.0f, percent);
    floatParam (ParamIDs::upward,   "Upward",   { 0.0f, 100.0f }, 100.0f, percent);
    floatParam (ParamIDs::downward, "Downward", { 0.0f, 100.0f }, 100.0f, percent);
    floatParam (ParamIDs::mix,      "Depth",      { 0.0f, 100.0f }, 100.0f, percent);
    boolParam (ParamIDs::softKnee, "Soft Knee", true);
    boolParam (ParamIDs::rms, "RMS", true);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::displayMode, 1 }, "Display Mode",
                                                        StringArray { "Top", "Bottom", "All" }, DisplayTop));

    // Crossover
    boolParam (ParamIDs::highOn, "High Band On", true);
    floatParam (ParamIDs::highFreq, "High Split", logRange (500.0f, 16000.0f, 2500.0f), kDefaultHighFreq,
                [] (float v) { return formatHz (v); }, parseHz);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::slope, 1 }, "Crossover Slope",
                                                        StringArray { "6 dB/oct", "12 dB/oct", "18 dB/oct" }, 1));
    boolParam (ParamIDs::lowOn, "Low Band On", true);
    floatParam (ParamIDs::lowFreq, "Low Split", logRange (20.0f, 1000.0f, 120.0f), kDefaultLowFreq,
                [] (float v) { return formatHz (v); }, parseHz);

    // Bands
    for (int b = 0; b < NumBands; ++b)
    {
        const auto& d = ottBandDefaults[(size_t) b];
        const String n = bandName (b);

        floatParam (bandParam (b, BandIDs::input),  n + " Input",  { -24.0f, 24.0f }, d.input, db);
        floatParam (bandParam (b, BandIDs::output), n + " Output", { -24.0f, 24.0f }, d.output, db);
        boolParam  (bandParam (b, BandIDs::active),   n + " Active", true);
        boolParam  (bandParam (b, BandIDs::solo),     n + " Solo", false);
        floatParam (bandParam (b, BandIDs::threshold), n + " Threshold", { -24.0f, 24.0f }, 0.0f,
                    [] (float v) { return (v > 0.0f ? "+" : "") + formatDb (v, 1); });
        floatParam (bandParam (b, BandIDs::attack),  n + " Attack",  logRange (0.1f, 1000.0f, 20.0f), d.attack,
                    [] (float v) { return formatMs (v); });
        floatParam (bandParam (b, BandIDs::release), n + " Release", logRange (1.0f, 5000.0f, 250.0f), d.release,
                    [] (float v) { return formatMs (v); });
        floatParam (bandParam (b, BandIDs::aboveThresh), n + " Above Threshold", { kMinDb, 0.0f }, d.aboveThresh, db);
        floatParam (bandParam (b, BandIDs::aboveRatio),  n + " Above Ratio", logRange (1.0f, kRatioMax, 10.0f), d.aboveRatio,
                    [] (float v) { return formatRatio (v) + " : 1"; }, parseRatio);
        floatParam (bandParam (b, BandIDs::belowThresh), n + " Below Threshold", { kMinDb, 0.0f }, d.belowThresh, db);
        floatParam (bandParam (b, BandIDs::belowRatio),  n + " Below Ratio", logRange (1.0f, kRatioMax, 10.0f), d.belowRatio,
                    [] (float v) { return "1 : " + formatRatio (v); }, parseRatio);
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
    startTimerHz (30);
}

OttAudioProcessor::~OttAudioProcessor()
{
    stopTimer();
}

float OttAudioProcessor::param (const juce::String& id) const
{
    return apvts.getRawParameterValue (id)->load();
}

bool OttAudioProcessor::paramOn (const juce::String& id) const
{
    return param (id) >= 0.5f;
}

int OttAudioProcessor::slopeOrder() const
{
    return juce::jlimit (1, 3, juce::roundToInt (param (ParamIDs::slope)) + 1);
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
                       param (ParamIDs::lowFreq), paramOn (ParamIDs::lowOn),
                       param (ParamIDs::highFreq), paramOn (ParamIDs::highOn), slopeOrder());
    minPhaseCrossover.prepare (sampleRate, maxBlock, numCh);

    linearActive = paramOn (ParamIDs::linearPhase);
    activeSlope = slopeOrder();
    updateLatency (linearActive);
    modeFade.reset (sampleRate, 0.01);
    modeFade.setCurrentAndTargetValue (1.0f);

    inputChunk.setSize (numCh, maxBlock);
    dryDelayed.setSize (numCh, maxBlock);
    for (auto& b : bands)
        b.setSize (numCh, maxBlock);

    updateBandSettings();
    for (auto& c : compressors)
        c.prepare (sampleRate);

    mixSmoothed.reset (sampleRate, 0.03);
    mixSmoothed.setCurrentAndTargetValue (param (ParamIDs::mix) * 0.01f);
    bypassMix.reset (sampleRate, 0.03);
    bypassMix.setCurrentAndTargetValue (paramOn (ParamIDs::bypass) ? 1.0f : 0.0f);
    inGainBuffer.assign ((size_t) maxBlock, 1.0f);
    {
        // K-weighting (ITU-R BS.1770): the loudness measure behind LUFS.
        auto shelf = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, 1681.97, 0.7072, juce::Decibels::decibelsToGain (4.0f));
        auto hp    = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 38.13, 0.5003);
        for (auto* k : { &refWeighting, &outWeighting })
            for (int ch = 0; ch < 2; ++ch)
            {
                k->shelf[(size_t) ch].coefficients = shelf;
                k->highPass[(size_t) ch].coefficients = hp;
                k->shelf[(size_t) ch].reset();
                k->highPass[(size_t) ch].reset();
            }
    }
    loudnessAlpha = 1.0f - std::exp (-1.0f / (1.5f * (float) sampleRate));   // ~1.5 s integration
    msRef = msOut = 0.0f;
    autoGainDb = 0.0f;

    inputGain.reset (sampleRate, 0.02);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (ParamIDs::inGain)));
    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (ParamIDs::output)));
    for (auto& s : bandSoloGain)
    {
        s.reset (sampleRate, 0.02);
        s.setCurrentAndTargetValue (1.0f);
    }
}

void OttAudioProcessor::releaseResources()
{
    crossover.reset();
    minPhaseCrossover.reset();
}

void OttAudioProcessor::updateLatency (bool linear)
{
    const int samples = linear ? crossover.getLatency() : 0;
    reportedLinear = linear;
    setLatencySamples (samples);
    latencyMs.store (1000.0f * (float) samples / (float) currentSampleRate);
}

void OttAudioProcessor::timerCallback()
{
    // Filter design runs here, on the message thread; the audio thread just picks it up.
    crossover.updateSettings (param (ParamIDs::lowFreq), paramOn (ParamIDs::lowOn),
                              param (ParamIDs::highFreq), paramOn (ParamIDs::highOn), slopeOrder());

    // Linear phase on/off changes the latency; tell the host from the message thread.
    if (currentSampleRate > 0.0 && paramOn (ParamIDs::linearPhase) != reportedLinear)
        updateLatency (paramOn (ParamIDs::linearPhase));
}

void OttAudioProcessor::updateBandSettings()
{
    const float upward   = param (ParamIDs::upward) * 0.01f;
    const float downward = param (ParamIDs::downward) * 0.01f;
    const float time     = param (ParamIDs::time) * 0.01f;
    const bool softKnee  = paramOn (ParamIDs::softKnee);
    const bool rms       = paramOn (ParamIDs::rms);

    for (int b = 0; b < NumBands; ++b)
    {
        BandCompressor::Settings s;
        s.inputDb     = param (bandParam (b, BandIDs::input));
        s.outputDb    = param (bandParam (b, BandIDs::output));
        s.attackMs    = param (bandParam (b, BandIDs::attack));
        s.releaseMs   = param (bandParam (b, BandIDs::release));
        const float offset = param (bandParam (b, BandIDs::threshold));
        s.aboveThresh = param (bandParam (b, BandIDs::aboveThresh)) + offset;
        s.aboveRatio  = param (bandParam (b, BandIDs::aboveRatio));
        s.belowThresh = param (bandParam (b, BandIDs::belowThresh)) + offset;
        s.belowRatio  = param (bandParam (b, BandIDs::belowRatio));
        s.active      = paramOn (bandParam (b, BandIDs::active));
        s.upward = upward;
        s.downward = downward;
        s.timeScale = time;
        s.softKnee = softKnee;
        s.rms = rms;
        compressors[(size_t) b].setSettings (s);
    }
}

void OttAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = std::min (inputChunk.getNumChannels(), buffer.getNumChannels());
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    updateBandSettings();

    bool anySolo = false;
    std::array<bool, NumBands> solo {};
    for (int b = 0; b < NumBands; ++b)
    {
        solo[(size_t) b] = paramOn (bandParam (b, BandIDs::solo));
        anySolo = anySolo || solo[(size_t) b];
    }
    for (int b = 0; b < NumBands; ++b)
        bandSoloGain[(size_t) b].setTargetValue (! anySolo || solo[(size_t) b] ? 1.0f : 0.0f);

    // Soloing a band always listens to the processed band on its own.
    mixSmoothed.setTargetValue (anySolo ? 1.0f : param (ParamIDs::mix) * 0.01f);
    bypassMix.setTargetValue (paramOn (ParamIDs::bypass) ? 1.0f : 0.0f);
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (param (ParamIDs::output)));
    inputGain.setTargetValue (juce::Decibels::decibelsToGain (param (ParamIDs::inGain)));

    // Switching linear phase on/off (or the zero-latency slope): fade out, swap, fade back in.
    // In linear phase the slope changes through the crossover's own filter crossfade.
    const bool wantLinear = paramOn (ParamIDs::linearPhase);
    const int wantSlope = slopeOrder();
    const bool needSwitch = wantLinear != linearActive || (! wantLinear && wantSlope != activeSlope);
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

    // Process in chunks no longer than the prepared block size.
    for (int start = 0; start < buffer.getNumSamples(); start += maxBlock)
    {
        const int n = std::min (maxBlock, buffer.getNumSamples() - start);

        for (int ch = 0; ch < numCh; ++ch)
            inputChunk.copyFrom (ch, 0, buffer, ch, start, n);

        if (linearActive)
            crossover.process (inputChunk, n, bands[BandLow], bands[BandMid], bands[BandHigh], dryDelayed);
        else
            minPhaseCrossover.process (inputChunk, n, bands[BandLow], bands[BandMid], bands[BandHigh], dryDelayed,
                                       param (ParamIDs::lowFreq), paramOn (ParamIDs::lowOn),
                                       param (ParamIDs::highFreq), paramOn (ParamIDs::highOn), activeSlope);

        // In Gain goes on after the (linear) crossover, so the dry path stays the untouched
        // input: Bypass and Auto Gain both refer to exactly what came into the plugin.
        for (int i = 0; i < n; ++i)
            inGainBuffer[(size_t) i] = inputGain.getNextValue();
        for (auto& band : bands)
            for (int ch = 0; ch < numCh; ++ch)
                juce::FloatVectorOperations::multiply (band.getWritePointer (ch), inGainBuffer.data(), n);

        for (int b = 0; b < NumBands; ++b)
            compressors[(size_t) b].process (bands[(size_t) b].getArrayOfWritePointers(), numCh, n);

        // Auto Gain: K-weighted loudness of the input vs. the processed output (measured on the
        // previous chunk), gliding over ~300 ms, applied as a ramp across this chunk.
        float agTarget = 0.0f;
        if (paramOn (ParamIDs::autoGain))
            agTarget = (msRef > 1.0e-6f && msOut > 1.0e-9f)      // hold through silence (< -60 dB)
                         ? juce::jlimit (-24.0f, 24.0f, 10.0f * std::log10 (msRef / msOut))
                         : autoGainDb;
        const float agStart = juce::Decibels::decibelsToGain (autoGainDb);
        autoGainDb = agTarget + (autoGainDb - agTarget) * std::exp (-(float) n / (0.3f * (float) currentSampleRate));
        const float agStep = (juce::Decibels::decibelsToGain (autoGainDb) - agStart) / (float) n;
        float ag = agStart;

        for (int i = 0; i < n; ++i)
        {
            const float gH = bandSoloGain[BandHigh].getNextValue();
            const float gM = bandSoloGain[BandMid].getNextValue();
            const float gL = bandSoloGain[BandLow].getNextValue();
            const float mix = mixSmoothed.getNextValue();
            const float out = outputGain.getNextValue();
            const float byp = bypassMix.getNextValue();
            const float fade = modeFade.getNextValue();
            const float gIn = inGainBuffer[(size_t) i];
            ag += agStep;

            float refSq = 0.0f, outSq = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float dry = dryDelayed.getSample (ch, i);   // the untouched input
                const float wet = gH * bands[BandHigh].getSample (ch, i)
                                + gM * bands[BandMid].getSample (ch, i)
                                + gL * bands[BandLow].getSample (ch, i);
                const float processed = (dry * gIn + mix * (wet - dry * gIn)) * out;

                const float r = refWeighting.process (ch, dry);
                const float o = outWeighting.process (ch, processed);
                refSq += r * r;
                outSq += o * o;

                const float matched = processed * ag;
                buffer.setSample (ch, start + i, fade * (matched + byp * (dry - matched)));
            }
            msRef += loudnessAlpha * (refSq - msRef);
            msOut += loudnessAlpha * (outSq - msOut);
        }
    }

    if (msRef < 1.0e-20f) msRef = 0.0f;
    if (msOut < 1.0e-20f) msOut = 0.0f;
    autoGainMeterDb.store (autoGainDb);

    for (int b = 0; b < NumBands; ++b)
    {
        const auto& m = compressors[(size_t) b].getMeter();
        meters[(size_t) b].inputDb.store (m.inputDb);
        meters[(size_t) b].outputDb.store (m.outputDb);
        meters[(size_t) b].gainDb.store (m.gainDb);
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

    // Everything back to the OTT defaults (but keep bypass, linear phase and the link), then apply the preset.
    for (auto* p : getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto& id = ranged->getParameterID();
            if (id == ParamIDs::bypass || id == ParamIDs::displayMode
                || id == ParamIDs::linearPhase || id == ParamIDs::inOutLink)
                continue;
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
        }
    }

    for (const auto& [id, value] : presets[(size_t) index].values)
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
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
