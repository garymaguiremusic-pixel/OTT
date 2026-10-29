#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>

//==============================================================================
// Parameter IDs, ranges, defaults and value formatting shared by the DSP, the
// processor and the editor.
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
    // Global
    inline constexpr auto bypass      = "bypass";
    inline constexpr auto inGain      = "inGain";
    inline constexpr auto output      = "output";
    inline constexpr auto inOutLink   = "inOutLink";
    inline constexpr auto autoGain    = "autoGain";    // on: match the output loudness to the input
    inline constexpr auto linearPhase = "linearPhase";
    inline constexpr auto time        = "time";
    inline constexpr auto upward      = "upward";
    inline constexpr auto downward    = "downward";
    inline constexpr auto mix         = "mix";
    inline constexpr auto softKnee    = "softKnee";
    inline constexpr auto rms         = "rms";
    inline constexpr auto displayMode = "displayMode";

    // Crossover
    inline constexpr auto highOn   = "highOn";
    inline constexpr auto highFreq = "highFreq";
    inline constexpr auto lowOn    = "lowOn";
    inline constexpr auto lowFreq  = "lowFreq";
    inline constexpr auto slope    = "slope";      // 0..3 = 6, 12, 18, 24 dB/oct
}

/** Per-band parameter ID, e.g. bandParam (BandHigh, "input") -> "h_input". */
inline juce::String bandParam (int band, const char* suffix)
{
    static const char* prefixes[] = { "h_", "m_", "l_" };
    return juce::String (prefixes[band]) + suffix;
}

namespace BandIDs
{
    inline constexpr auto input       = "input";
    inline constexpr auto output      = "output";
    inline constexpr auto active      = "active";
    inline constexpr auto solo        = "solo";
    inline constexpr auto threshold   = "threshold";   // offset applied to both thresholds
    inline constexpr auto amount      = "amount";      // 0..150 %: how much of the band's compression is used
    inline constexpr auto attack      = "attack";
    inline constexpr auto release     = "release";
    inline constexpr auto autoRelease = "autoRelease";
    inline constexpr auto autoGain    = "autoGain";      // band make-up
    inline constexpr auto aboveThresh = "aboveThresh";
    inline constexpr auto aboveRatio  = "aboveRatio";
    inline constexpr auto belowThresh = "belowThresh";
    inline constexpr auto belowRatio  = "belowRatio";
}

enum DisplayMode : int { DisplayTop = 0, DisplayBottom, DisplayAll };

//==============================================================================
// Ranges
inline constexpr float kMinDb      = -80.0f;   // bottom of the meters / detector floor
inline constexpr float kRatioMax   = 1000.0f;  // shown as "inf"
inline constexpr float kRatioInf   = 999.5f;   // at or above this, treat the ratio as infinite
inline constexpr float kMaxBoostDb = 36.0f;    // ceiling for upward gain

//==============================================================================
/** The default patch: Ableton Live's "OTT" Multiband Dynamics preset. */
struct BandDefaults
{
    float input, output, attack, release, aboveThresh, aboveRatio, belowThresh, belowRatio;
};

inline const std::array<BandDefaults, NumBands> ottBandDefaults
{{
    //  in     out    att    rel     above   ratio     below   ratio
    {  5.2f, 10.3f, 13.5f, 132.0f, -35.5f, kRatioMax, -40.8f, 4.17f },   // High
    {  5.2f,  5.7f, 22.4f, 282.0f, -30.2f, 66.7f,     -41.8f, 4.17f },   // Mid
    {  5.2f, 10.3f, 47.8f, 282.0f, -33.8f, 66.7f,     -40.8f, 4.17f },   // Low
}};

inline constexpr float kDefaultHighFreq = 2500.0f;
inline constexpr float kDefaultLowFreq  = 88.3f;

//==============================================================================
// Formatting
inline juce::String formatDb (float db, int decimals = 2)
{
    if (db <= -99.0f)
        return "-inf dB";
    return juce::String (db, decimals) + " dB";
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

inline juce::String formatRatio (float r)
{
    if (r >= kRatioInf)
        return "inf";
    return juce::String (r, r < 10.0f ? 2 : 1);
}

inline juce::String formatPercent (float p)
{
    return juce::String (juce::roundToInt (p)) + " %";
}

/** Parses "2.5k", "2500", "2.5 kHz" etc. */
inline float parseHz (const juce::String& s)
{
    const auto t = s.trim().toLowerCase();
    const float v = t.getFloatValue();
    return t.containsChar ('k') ? v * 1000.0f : v;
}

/** Parses "inf" / "4.17" / "1:4.17". */
inline float parseRatio (const juce::String& s)
{
    const auto t = s.trim().toLowerCase();
    if (t.contains ("inf"))
        return kRatioMax;
    return (t.containsChar (':') ? t.fromLastOccurrenceOf (":", false, false) : t).getFloatValue();
}

inline juce::NormalisableRange<float> logRange (float start, float end, float centre)
{
    juce::NormalisableRange<float> r (start, end);
    r.setSkewForCentre (centre);
    return r;
}

} // namespace ott
