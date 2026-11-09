#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>

//==============================================================================
// Parameter IDs, ranges, defaults, the engine's constants and value formatting
// shared by the DSP, the processor and the editor.
//==============================================================================
namespace ott
{
enum Band : int { BandHigh = 0, BandMid, BandLow, NumBands };

inline const char* bandName (int band)
{
    static const char* names[] = { "High", "Mid", "Low" };
    return names[band];
}

namespace ParamIDs
{
    // Engine (spec parameters 0-4 and 17-19)
    inline constexpr auto depth       = "depth";
    inline constexpr auto time        = "time";
    inline constexpr auto inGain      = "inGain";
    inline constexpr auto output      = "outGain";
    inline constexpr auto cleanXov    = "cleanXov";
    inline constexpr auto upward      = "upStrength";
    inline constexpr auto downward    = "downStrength";
    inline constexpr auto bypass      = "bypass";

    // Additions
    inline constexpr auto inOutLink   = "inOutLink";
    inline constexpr auto autoGain    = "autoGain";    // on: match the output loudness to the input
    inline constexpr auto linearPhase = "linearPhase";
    inline constexpr auto softKnee    = "softKnee";
    inline constexpr auto rms         = "rms";

    // Crossover
    inline constexpr auto highOn   = "highOn";
    inline constexpr auto highFreq = "highFreq";
    inline constexpr auto lowOn    = "lowOn";
    inline constexpr auto lowFreq  = "lowFreq";
    inline constexpr auto slope    = "slope";      // 0..3 = 6, 12, 18, 24 dB/oct
}

/** Per-band parameter ID, e.g. bandParam (BandHigh, "gain") -> "h_gain". */
inline juce::String bandParam (int band, const char* suffix)
{
    static const char* prefixes[] = { "h_", "m_", "l_" };
    return juce::String (prefixes[band]) + suffix;
}

namespace BandIDs
{
    // Engine (spec parameters 5-16)
    inline constexpr auto threshold   = "thresh";       // offset applied to both thresholds
    inline constexpr auto output      = "gain";         // band output gain
    inline constexpr auto bypUp       = "bypUp";
    inline constexpr auto bypDown     = "bypDown";

    // Additions
    inline constexpr auto amount      = "amount";       // 0..150 %: how much of the band's gain change is used
    inline constexpr auto active      = "active";
    inline constexpr auto solo        = "solo";
    inline constexpr auto attack      = "attack";       // at Time = 100 %
    inline constexpr auto release     = "release";      // at Time = 100 %
    inline constexpr auto autoRelease = "autoRelease";
    inline constexpr auto autoGain    = "autoGain";     // band make-up
}

//==============================================================================
// Engine constants. Arrays are indexed by Band (High, Mid, Low).
// Values marked [INFERRED] / [UNKNOWN] were not given by the spec; see the README.
namespace Engine
{
    inline constexpr float kDefaultLowFreq  = 88.3f;
    inline constexpr float kDefaultHighFreq = 2500.0f;

    // Thresholds below the band's Threshold offset, in dB
    inline constexpr std::array<float, NumBands> kDownThresholdDb { 35.5f, 30.2f, 33.8f };
    inline constexpr std::array<float, NumBands> kUpThresholdDb   { 40.8f, 41.8f, 40.8f };

    // downRatio = 1 - s * kDownRatioScale, s = clamp (2 * Depth * DownStrength, 0, 1)
    inline constexpr std::array<float, NumBands> kDownRatioScale  { 0.99f, 0.99f, 0.999f };
    // upAmount = Depth * UpStrength * kUpAmountScale
    inline constexpr float kUpAmountScale   = 1.5f;
    inline constexpr float kMaxBoostDb      = 36.0f;     // upward ceiling
    inline constexpr float kMinGain         = 0.01f;     // downward floor (-40 dB)
    inline constexpr float kUpwardFloorDb   = -496.0f;   // no upward gain below this envelope level
    inline constexpr float kEnvelopeFloor   = 1.0e-10f;  // envelope values below this are flushed to 0
    inline constexpr float kDetectorOffset  = 1.0e-25f;  // added to L^2 + R^2

    // Attack / release at Time = 100 % (T = 1), in ms [UNKNOWN units: treated as ms]
    inline constexpr float kAttackScale  = 0.15f;
    inline constexpr float kReleaseScale = 0.25f;
    inline constexpr std::array<float, NumBands> kAttackBase  { 13.5f, 22.4f, 47.8f };
    inline constexpr std::array<float, NumBands> kReleaseBase { 132.0f, 282.0f, 282.0f };
    inline float defaultAttackMs (int band)  { return kAttackScale  * kAttackBase[(size_t) band]; }
    inline float defaultReleaseMs (int band) { return kReleaseScale * kReleaseBase[(size_t) band]; }
    inline constexpr float kMinTime = 0.01f;

    // Depth-scaled band gains: input (0.52 * D + 1), output per band (k * D + 1)
    inline constexpr float kInputDepthScale = 0.52f;
    inline constexpr std::array<float, NumBands> kOutputDepthScale { 2.273047f, 0.927525f, 2.273047f };

    // Look-ahead: the detector reads the current sample, gain goes on the sample this many earlier.
    inline constexpr int kLookaheadSamples = 2;

    // [UNKNOWN] RMS detector window
    inline constexpr float kRmsWindowMs = 10.0f;

    // Parameter smoothing: coef = kSmoothRate / (fs' * kSmoothScale + kSmoothRate), fs' >= 44100
    inline constexpr double kSmoothRate  = 44100.0;
    inline constexpr double kSmoothScale = 300.0;
    inline constexpr double kSmoothMinFs = 44000.0;

    // Input peak meter: linear decay per sample
    inline constexpr float kPeakDecayPerSample = 2.5e-5f;

    // [INFERRED] Soft knee width (an addition; off by default, the spec is hard-knee)
    inline constexpr float kSoftKneeDb = 6.0f;
}

//==============================================================================
// Ranges and mappings
inline constexpr float kMinDb       = -80.0f;   // bottom of the meters
inline constexpr float kGainFloorDb = -60.0f;   // gain parameters show -inf at or below this

inline float gainToDb (float linear)    { return linear <= 0.0f ? kGainFloorDb : std::max (kGainFloorDb, 20.0f * std::log10 (linear)); }
inline float dbToGain (float db)        { return db <= kGainFloorDb + 1.0e-4f ? 0.0f : std::pow (10.0f, db / 20.0f); }

// In / Out Gain: x <= 0.5 -> 2x, else 1 + 16 (x - 0.5)   (linear 0..9, 0.5 = unity)
inline float inOutGainFromNorm (float x)  { return x <= 0.5f ? 2.0f * x : 1.0f + 16.0f * (x - 0.5f); }
inline float normFromInOutGain (float g)  { return g <= 1.0f ? 0.5f * g : 0.5f + (g - 1.0f) / 16.0f; }

// Band Gain: 2x (linear 0..2, 0.5 = unity)
inline float bandGainFromNorm (float x)   { return 2.0f * x; }
inline float normFromBandGain (float g)   { return 0.5f * g; }

// Time: x <= 0.5 -> 2x, else 1 + 18 (x - 0.5), at least 0.01 (shown as %, 100 % = 1)
inline float timeFromNorm (float x)       { return std::max (Engine::kMinTime, x <= 0.5f ? 2.0f * x : 1.0f + 18.0f * (x - 0.5f)); }
inline float normFromTime (float t)       { return t <= 1.0f ? 0.5f * t : 0.5f + (t - 1.0f) / 18.0f; }

/** A parameter range in dB whose normalised value follows a linear-gain mapping. */
inline juce::NormalisableRange<float> dbRange (float (*gainFromNorm) (float), float (*normFromGain) (float))
{
    const float maxDb = gainToDb (gainFromNorm (1.0f));
    return juce::NormalisableRange<float> (kGainFloorDb, maxDb,
        [gainFromNorm] (float, float, float x)  { return gainToDb (gainFromNorm (x)); },
        [normFromGain] (float, float, float db) { return juce::jlimit (0.0f, 1.0f, normFromGain (dbToGain (db))); });
}

/** Time in % (1 .. 1000) with the spec's mapping. */
inline juce::NormalisableRange<float> timeRange()
{
    return juce::NormalisableRange<float> (100.0f * Engine::kMinTime, 1000.0f,
        [] (float, float, float x) { return 100.0f * timeFromNorm (x); },
        [] (float, float, float p) { return juce::jlimit (0.0f, 1.0f, normFromTime (p * 0.01f)); });
}

inline juce::NormalisableRange<float> logRange (float start, float end, float centre)
{
    juce::NormalisableRange<float> r (start, end);
    r.setSkewForCentre (centre);
    return r;
}

//==============================================================================
// Formatting (gains and thresholds in dB with one decimal place)
inline juce::String formatDb (float db, int decimals = 1)
{
    if (db <= kGainFloorDb + 0.05f)
        return "-inf dB";
    return (db > 0.0f && decimals > 0 && db >= 0.05f ? "+" : "") + juce::String (db, decimals) + " dB";
}

inline juce::String formatMs (float ms)
{
    if (ms < 10.0f)   return juce::String (ms, 2) + " ms";
    if (ms < 100.0f)  return juce::String (ms, 1) + " ms";
    if (ms < 1000.0f) return juce::String (juce::roundToInt (ms)) + " ms";
    return juce::String (ms / 1000.0f, 2) + " s";
}

inline juce::String formatHz (float hz)
{
    if (hz < 100.0f)  return juce::String (hz, 1) + " Hz";
    if (hz < 1000.0f) return juce::String (juce::roundToInt (hz)) + " Hz";
    return juce::String (hz / 1000.0f, 2) + " kHz";
}

inline juce::String formatPercent (float p, int decimals = 0)
{
    return (decimals > 0 ? juce::String (p, decimals) : juce::String (juce::roundToInt (p))) + " %";
}

/** Parses "-inf", "+3", "3 dB" ... */
inline float parseDb (const juce::String& s)
{
    const auto t = s.trim().toLowerCase();
    return t.contains ("inf") ? kGainFloorDb : t.getFloatValue();
}

/** Parses "2.5k", "2500", "2.5 kHz" etc. */
inline float parseHz (const juce::String& s)
{
    const auto t = s.trim().toLowerCase();
    const float v = t.getFloatValue();
    return t.containsChar ('k') ? v * 1000.0f : v;
}

} // namespace ott
