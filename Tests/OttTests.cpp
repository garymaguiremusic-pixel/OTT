// Unit tests for the OTT Multiband engine (JUCE UnitTest). Built as the OTT_Tests console app;
// exits with a non-zero code if anything fails.

#include "PluginProcessor.h"
#include <cstdio>

using namespace juce;

namespace
{
    constexpr double kFs = 48000.0;
    constexpr int kBlock = 512;

    void setParam (OttAudioProcessor& p, const String& id, float value)
    {
        auto* prm = p.apvts.getParameter (id);
        jassert (prm != nullptr);
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    }

    /** Runs `total` samples of `gen (sampleIndex, channel)` through the processor, in blocks. */
    void run (OttAudioProcessor& p, int total, const std::function<float (int, int)>& gen,
              std::vector<float>& in, std::vector<float>& out)
    {
        AudioBuffer<float> buffer (2, kBlock);
        MidiBuffer midi;
        in.assign ((size_t) total, 0.0f);
        out.assign ((size_t) total, 0.0f);
        for (int start = 0; start < total; start += kBlock)
        {
            const int n = std::min (kBlock, total - start);
            AudioBuffer<float> block (buffer.getArrayOfWritePointers(), 2, n);
            for (int i = 0; i < n; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    block.setSample (ch, i, gen (start + i, ch));
            for (int i = 0; i < n; ++i)
                in[(size_t) (start + i)] = block.getSample (0, i);
            p.processBlock (block, midi);
            for (int i = 0; i < n; ++i)
                out[(size_t) (start + i)] = block.getSample (0, i);
        }
    }

    double rmsDb (const std::vector<float>& v, int from, int to)
    {
        double sum = 0.0;
        for (int i = from; i < to; ++i)
            sum += (double) v[(size_t) i] * (double) v[(size_t) i];
        return 10.0 * std::log10 (sum / (double) std::max (1, to - from) + 1.0e-30);
    }

    float sine (double freq, float amp, int i)
    {
        return amp * (float) std::sin (MathConstants<double>::twoPi * freq * (double) i / kFs);
    }

    std::unique_ptr<OttAudioProcessor> makeProcessor (std::initializer_list<std::pair<const char*, float>> settings = {})
    {
        auto p = std::make_unique<OttAudioProcessor>();
        for (const auto& [id, value] : settings)
            setParam (*p, id, value);
        p->prepareToPlay (kFs, kBlock);
        return p;
    }
}

//==============================================================================
class MappingTests : public UnitTest
{
public:
    MappingTests() : UnitTest ("Parameter mappings", "OTT") {}

    void runTest() override
    {
        using namespace ott;
        beginTest ("In / Out Gain: x <= 0.5 -> 2x, else 1 + 16 (x - 0.5)");
        expectWithinAbsoluteError (inOutGainFromNorm (0.0f), 0.0f, 1.0e-6f);
        expectWithinAbsoluteError (inOutGainFromNorm (0.25f), 0.5f, 1.0e-6f);
        expectWithinAbsoluteError (inOutGainFromNorm (0.5f), 1.0f, 1.0e-6f);
        expectWithinAbsoluteError (inOutGainFromNorm (1.0f), 9.0f, 1.0e-5f);
        for (float x : { 0.1f, 0.4f, 0.6f, 0.9f })
            expectWithinAbsoluteError (normFromInOutGain (inOutGainFromNorm (x)), x, 1.0e-5f);

        beginTest ("Band Gain = 2x, Time mapping, Threshold offset");
        expectWithinAbsoluteError (bandGainFromNorm (0.5f), 1.0f, 1.0e-6f);
        expectWithinAbsoluteError (bandGainFromNorm (1.0f), 2.0f, 1.0e-6f);
        expectWithinAbsoluteError (timeFromNorm (0.5f), 1.0f, 1.0e-6f);
        expectWithinAbsoluteError (timeFromNorm (1.0f), 10.0f, 1.0e-5f);
        expectWithinAbsoluteError (timeFromNorm (0.0f), Engine::kMinTime, 1.0e-7f);

        beginTest ("Parameters use the spec mappings for their normalised values");
        OttAudioProcessor p;
        auto* in = p.apvts.getParameter (ParamIDs::inGain);
        expectWithinAbsoluteError (in->convertFrom0to1 (0.5f), 0.0f, 1.0e-4f);                      // unity
        expectWithinAbsoluteError (in->convertFrom0to1 (1.0f), 20.0f * std::log10 (9.0f), 1.0e-3f);
        expectWithinAbsoluteError (in->getDefaultValue(), 0.5f, 1.0e-6f);
        auto* gain = p.apvts.getParameter (bandParam (BandLow, BandIDs::output));
        expectWithinAbsoluteError (gain->convertFrom0to1 (1.0f), 20.0f * std::log10 (2.0f), 1.0e-3f);
        expectWithinAbsoluteError (gain->getDefaultValue(), 0.5f, 1.0e-6f);
        auto* time = p.apvts.getParameter (ParamIDs::time);
        expectWithinAbsoluteError (time->getDefaultValue(), 0.5f, 1.0e-6f);
        expectEquals (in->getText (in->getValue(), 32), String ("0.0 dB"));

        beginTest ("Defaults: Depth 1, Strengths 1, Bypass off, Clean XOV off");
        expectWithinAbsoluteError (p.apvts.getParameter (ParamIDs::depth)->getValue(), 1.0f, 1.0e-6f);
        expectWithinAbsoluteError (p.apvts.getParameter (ParamIDs::upward)->getValue(), 1.0f, 1.0e-6f);
        expectWithinAbsoluteError (p.apvts.getParameter (ParamIDs::downward)->getValue(), 1.0f, 1.0e-6f);
        expect (p.apvts.getParameter (ParamIDs::bypass)->getValue() < 0.5f);
        expect (p.apvts.getParameter (ParamIDs::cleanXov)->getValue() < 0.5f);
    }
};

//==============================================================================
class CrossoverTests : public UnitTest
{
public:
    CrossoverTests() : UnitTest ("Crossover sums flat at Depth 0", "OTT") {}

    void runTest() override
    {
        Random rng (1);

        beginTest ("Linear phase: null test against the delayed input");
        for (int slope = 0; slope < 4; ++slope)
        {
            auto p = makeProcessor ({ { ott::ParamIDs::depth, 0.0f }, { ott::ParamIDs::slope, (float) slope } });
            const int latency = p->getLatencySamples();
            std::vector<float> in, out;
            run (*p, (int) kFs * 2, [&] (int, int) { return rng.nextFloat() * 2.0f - 1.0f; }, in, out);
            std::vector<float> err;
            for (int i = latency + 4800; i < (int) in.size(); ++i)
                err.push_back (out[(size_t) i] - in[(size_t) (i - latency)]);
            const double residual = rmsDb (err, 0, (int) err.size()) - rmsDb (in, 0, (int) in.size());
            logMessage ("  slope " + String ((slope + 1) * 6) + " dB/oct: residual " + String (residual, 1) + " dB");
            expect (residual < -90.0, "residual " + String (residual));
        }

        beginTest ("Zero latency (default and Clean XOV): magnitude flat at every frequency");
        for (int clean = 0; clean < 2; ++clean)
            for (int slope = 0; slope < 4; ++slope)
            {
                double worst = 0.0;
                for (double f : { 40.0, 88.3, 300.0, 1000.0, 2500.0, 6000.0, 15000.0 })
                {
                    auto p = makeProcessor ({ { ott::ParamIDs::depth, 0.0f }, { ott::ParamIDs::linearPhase, 0.0f },
                                              { ott::ParamIDs::cleanXov, (float) clean }, { ott::ParamIDs::slope, (float) slope } });
                    std::vector<float> in, out;
                    run (*p, (int) kFs, [&] (int i, int) { return sine (f, 0.25f, i); }, in, out);
                    worst = std::max (worst, std::abs (rmsDb (out, 24000, 48000) - rmsDb (in, 24000, 48000)));
                }
                expect (worst < 0.05, "clean " + String (clean) + " slope " + String (slope) + ": " + String (worst) + " dB");
            }
    }
};

//==============================================================================
class GainTests : public UnitTest
{
public:
    GainTests() : UnitTest ("Compression", "OTT") {}

    void runTest() override
    {
        beginTest ("Depth 0: no gain change, output = input");
        for (float amp : { 0.9f, 0.01f, 0.0001f })
        {
            auto p = makeProcessor ({ { ott::ParamIDs::depth, 0.0f } });
            std::vector<float> in, out;
            run (*p, (int) kFs, [&] (int i, int) { return sine (1000.0, amp, i); }, in, out);
            for (int b = 0; b < ott::NumBands; ++b)
                expectWithinAbsoluteError (p->meters[(size_t) b].gainDb.load(), 0.0f, 1.0e-6f);
            const int latency = p->getLatencySamples();
            double maxErr = 0.0;
            for (int i = latency + 4800; i < (int) in.size(); ++i)
                maxErr = std::max (maxErr, (double) std::abs (out[(size_t) i] - in[(size_t) (i - latency)]));
            expect (maxErr < amp * 1.0e-4, "max error " + String (maxErr));
        }

        beginTest ("Loud sines are reduced, quiet sines boosted, never by more than +36 dB");
        for (double f : { 50.0, 1000.0, 8000.0 })   // low, mid and high band
        {
            const int band = f < 100.0 ? ott::BandLow : (f < 2000.0 ? ott::BandMid : ott::BandHigh);
            for (float amp : { 0.7f, 0.003f, 1.0e-5f, 1.0e-8f })
            {
                auto p = makeProcessor();
                std::vector<float> in, out;
                float maxGain = -1000.0f;
                AudioBuffer<float> buffer (2, kBlock);
                MidiBuffer midi;
                for (int start = 0; start < (int) kFs; start += kBlock)
                {
                    for (int i = 0; i < kBlock; ++i)
                        for (int ch = 0; ch < 2; ++ch)
                            buffer.setSample (ch, i, sine (f, amp, start + i));
                    p->processBlock (buffer, midi);
                    maxGain = std::max (maxGain, p->meters[(size_t) band].gainDb.load());
                }
                const float gain = p->meters[(size_t) band].gainDb.load();
                logMessage ("  " + String (f) + " Hz at " + String (20.0f * std::log10 (amp), 0) + " dBFS: gain "
                            + String (gain, 1) + " dB");
                if (amp > 0.5f)
                    expect (gain < -3.0f, "loud sine not reduced: " + String (gain));
                else if (amp > 1.0e-6f)
                    expect (gain > 3.0f, "quiet sine not boosted: " + String (gain));
                // At -160 dBFS the envelope stays under the 1e-10 flush floor, so there is no
                // upward gain at all (near-silence is never pumped up); it is only bounded here.
                expect (maxGain <= ott::Engine::kMaxBoostDb + 1.0e-4f, "boost above +36 dB: " + String (maxGain));
            }
        }

        beginTest ("Downward gain never below -40 dB (linear 0.01)");
        {
            auto p = makeProcessor ({ { ott::ParamIDs::inGain, 19.0f } });
            std::vector<float> in, out;
            run (*p, (int) kFs, [&] (int i, int) { return sine (1000.0, 1.0f, i); }, in, out);
            expect (p->meters[ott::BandMid].gainDb.load() >= -40.0f - 1.0e-4f);
        }

        beginTest ("Bypass Upward / Downward switch each direction off");
        {
            auto loud = makeProcessor ({ { ott::bandParam (ott::BandMid, ott::BandIDs::bypDown).toRawUTF8(), 1.0f } });
            std::vector<float> in, out;
            run (*loud, (int) kFs, [&] (int i, int) { return sine (1000.0, 0.7f, i); }, in, out);
            expectWithinAbsoluteError (loud->meters[ott::BandMid].gainDb.load(), 0.0f, 1.0e-6f);
            auto quiet = makeProcessor ({ { ott::bandParam (ott::BandMid, ott::BandIDs::bypUp).toRawUTF8(), 1.0f } });
            run (*quiet, (int) kFs, [&] (int i, int) { return sine (1000.0, 0.003f, i); }, in, out);
            expectWithinAbsoluteError (quiet->meters[ott::BandMid].gainDb.load(), 0.0f, 1.0e-6f);
        }
    }
};

//==============================================================================
class RobustnessTests : public UnitTest
{
public:
    RobustnessTests() : UnitTest ("Robustness", "OTT") {}

    static bool isDenormal (float v) { return ! juce::exactlyEqual (v, 0.0f) && std::abs (v) < std::numeric_limits<float>::min(); }

    void runTest() override
    {
        Random rng (3);
        beginTest ("No NaN / denormal output with silence or full-scale noise, both crossover modes");
        for (int linear = 0; linear < 2; ++linear)
            for (int noise = 0; noise < 2; ++noise)
            {
                auto p = makeProcessor ({ { ott::ParamIDs::linearPhase, (float) linear } });
                std::vector<float> in, out;
                run (*p, (int) kFs * 3, [&] (int, int) { return noise ? rng.nextFloat() * 2.0f - 1.0f : 0.0f; }, in, out);
                int bad = 0;
                for (auto v : out)
                    bad += (! std::isfinite (v) || isDenormal (v)) ? 1 : 0;
                expectEquals (bad, 0);
                if (! noise)
                    expect (std::all_of (out.begin(), out.end(), [] (float v) { return juce::exactlyEqual (v, 0.0f); }), "silence in, silence out");
            }

        beginTest ("Any block size and sample rate; mono");
        for (double fs : { 22050.0, 44100.0, 96000.0, 192000.0 })
        {
            OttAudioProcessor p;
            AudioProcessor::BusesLayout mono;
            mono.inputBuses.add (AudioChannelSet::mono());
            mono.outputBuses.add (AudioChannelSet::mono());
            const bool useMono = fs > 100000.0;
            if (useMono)
                expect (p.setBusesLayout (mono));
            p.prepareToPlay (fs, 256);
            AudioBuffer<float> buffer (useMono ? 1 : 2, 1000);   // larger than the prepared block
            MidiBuffer midi;
            bool finite = true;
            for (int k = 0; k < 50; ++k)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (ch, i, rng.nextFloat() - 0.5f);
                p.processBlock (buffer, midi);
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        finite = finite && std::isfinite (buffer.getSample (ch, i));
            }
            expect (finite, "fs " + String (fs));
        }

        beginTest ("Latency = crossover + 2 samples look-ahead");
        {
            auto lin = makeProcessor();
            expectEquals (lin->getLatencySamples(), 2048 + ott::Engine::kLookaheadSamples);
            auto zero = makeProcessor ({ { ott::ParamIDs::linearPhase, 0.0f } });
            expectEquals (zero->getLatencySamples(), ott::Engine::kLookaheadSamples);
        }

        beginTest ("Bypass passes the input through unchanged (latency-aligned)");
        {
            auto p = makeProcessor ({ { ott::ParamIDs::bypass, 1.0f }, { ott::ParamIDs::inGain, 6.0f } });
            std::vector<float> in, out;
            run (*p, (int) kFs, [&] (int, int) { return rng.nextFloat() - 0.5f; }, in, out);
            const int latency = p->getLatencySamples();
            float maxErr = 0.0f;
            for (int i = latency; i < (int) in.size(); ++i)
                maxErr = std::max (maxErr, std::abs (out[(size_t) i] - in[(size_t) (i - latency)]));
            expect (maxErr < 1.0e-6f, "max error " + String (maxErr));
        }

        beginTest ("State save / restore through the APVTS");
        {
            OttAudioProcessor a;
            setParam (a, ott::ParamIDs::depth, 42.0f);
            setParam (a, ott::bandParam (ott::BandHigh, ott::BandIDs::threshold), -12.5f);
            setParam (a, ott::ParamIDs::cleanXov, 1.0f);
            MemoryBlock state;
            a.getStateInformation (state);
            OttAudioProcessor b;
            b.setStateInformation (state.getData(), (int) state.getSize());
            expectWithinAbsoluteError (b.apvts.getRawParameterValue (ott::ParamIDs::depth)->load(), 42.0f, 1.0e-3f);
            expectWithinAbsoluteError (b.apvts.getRawParameterValue (ott::bandParam (ott::BandHigh, ott::BandIDs::threshold))->load(), -12.5f, 1.0e-3f);
            expect (b.apvts.getRawParameterValue (ott::ParamIDs::cleanXov)->load() > 0.5f);
        }
    }
};

//==============================================================================
class ExtrasTests : public UnitTest
{
public:
    ExtrasTests() : UnitTest ("Additions", "OTT") {}

    void runTest() override
    {
        Random rng (5);
        auto modulatedNoise = [&] (int i, int) {
            const float env = 0.08f + 0.25f * (0.5f + 0.5f * std::sin ((float) i * 6.2831853f * 1.3f / (float) kFs));
            return env * (rng.nextFloat() * 2.0f - 1.0f); };

        beginTest ("Band Amount 0 % = no gain change on that band");
        {
            auto p = makeProcessor ({ { ott::bandParam (ott::BandMid, ott::BandIDs::amount).toRawUTF8(), 0.0f } });
            std::vector<float> in, out;
            run (*p, (int) kFs, [&] (int i, int) { return sine (1000.0, 0.7f, i); }, in, out);
            expectWithinAbsoluteError (p->meters[ott::BandMid].gainDb.load(), 0.0f, 1.0e-6f);
        }

        beginTest ("Band auto gain holds the band's level (within 0.5 dB)");
        {
            auto level = [&] (bool autoGain) {
                rng.setSeed (11);
                auto p = makeProcessor ({ { ott::bandParam (ott::BandMid, ott::BandIDs::solo).toRawUTF8(), 1.0f },
                                          { ott::bandParam (ott::BandMid, ott::BandIDs::autoGain).toRawUTF8(), autoGain ? 1.0f : 0.0f } });
                std::vector<float> in, out;
                run (*p, (int) kFs * 8, modulatedNoise, in, out);
                return rmsDb (out, (int) kFs * 3, (int) kFs * 8); };
            auto reference = [&] {
                rng.setSeed (11);
                auto p = makeProcessor ({ { ott::bandParam (ott::BandMid, ott::BandIDs::solo).toRawUTF8(), 1.0f },
                                          { ott::bandParam (ott::BandMid, ott::BandIDs::amount).toRawUTF8(), 0.0f } });
                std::vector<float> in, out;
                run (*p, (int) kFs * 8, modulatedNoise, in, out);
                return rmsDb (out, (int) kFs * 3, (int) kFs * 8); };
            const double ref = reference();
            expect (std::abs (level (true) - ref) < 0.5, "with A: " + String (level (true) - ref));
        }

        beginTest ("Global auto gain matches the K-weighted loudness (within 1 LU)");
        {
            auto p = makeProcessor ({ { ott::ParamIDs::autoGain, 1.0f } });
            std::vector<float> in, out;
            run (*p, (int) kFs * 6, [&] (int, int) { return 0.2f * (rng.nextFloat() * 2.0f - 1.0f); }, in, out);
            dsp::IIR::Filter<float> s1, h1, s2, h2;
            s1.coefficients = s2.coefficients = dsp::IIR::Coefficients<float>::makeHighShelf (kFs, 1681.97, 0.7072, Decibels::decibelsToGain (4.0f));
            h1.coefficients = h2.coefficients = dsp::IIR::Coefficients<float>::makeHighPass (kFs, 38.13, 0.5003);
            double ein = 0.0, eout = 0.0;
            for (size_t i = 0; i < in.size(); ++i)
            {
                const float a = h1.processSample (s1.processSample (in[i])), b = h2.processSample (s2.processSample (out[i]));
                if (i > (size_t) kFs * 4) { ein += a * a; eout += b * b; }
            }
            expect (std::abs (10.0 * std::log10 (eout / ein)) < 1.0, String (10.0 * std::log10 (eout / ein)));
        }

        beginTest ("Randomise keeps every value in range and the audio finite");
        {
            auto p = makeProcessor();
            for (int k = 0; k < 20; ++k)
            {
                p->randomise();
                for (auto* prm : p->getParameters())
                    expect (prm->getValue() >= 0.0f && prm->getValue() <= 1.0f, prm->getName (32) + " out of range");
                std::vector<float> in, out;
                run (*p, 12000, [&] (int, int) { return 0.3f * (rng.nextFloat() * 2.0f - 1.0f); }, in, out);
                float worst = 0.0f;
                bool finite = true;
                for (auto v : out) { finite = finite && std::isfinite (v); worst = std::max (worst, std::abs (v)); }
                expect (finite, "non-finite output");
                expect (worst < 8.0f, "peak " + String (worst));
            }
        }
    }
};

static MappingTests mappingTests;
static CrossoverTests crossoverTests;
static GainTests gainTests;
static RobustnessTests robustnessTests;
static ExtrasTests extrasTests;

int main()
{
    ScopedJuceInitialiser_GUI juce;
    UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("OTT");

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
