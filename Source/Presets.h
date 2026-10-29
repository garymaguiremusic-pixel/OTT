#pragma once

#include "Parameters.h"
#include <utility>
#include <vector>

namespace ott
{

/** Factory preset: every parameter starts at its default (the OTT patch),
    then the listed values are applied on top. */
struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<juce::String, float>> values;
};

inline const std::vector<FactoryPreset>& getFactoryPresets()
{
    using namespace ParamIDs;
    static const std::vector<FactoryPreset> presets
    {
        { "OTT", {} },
        { "OTT 50%", { { amount, 50.0f } } },
        { "OTT 25%", { { amount, 25.0f } } },
        { "Upward Only", { { downward, 0.0f } } },
        { "Downward Only", { { upward, 0.0f } } },
        { "Slow Glue", { { amount, 35.0f }, { time, 300.0f } } },
        { "Fast Smash", { { time, 40.0f },
                          { bandParam (BandHigh, BandIDs::input), 9.0f },
                          { bandParam (BandMid,  BandIDs::input), 9.0f },
                          { bandParam (BandLow,  BandIDs::input), 9.0f } } },
        { "Two Band (No Low)", { { lowOn, 0.0f } } },
        { "Auto Gain OTT", { { bandParam (BandHigh, BandIDs::autoGain), 1.0f },
                             { bandParam (BandMid,  BandIDs::autoGain), 1.0f },
                             { bandParam (BandLow,  BandIDs::autoGain), 1.0f },
                             { bandParam (BandHigh, BandIDs::output), 0.0f },
                             { bandParam (BandMid,  BandIDs::output), 0.0f },
                             { bandParam (BandLow,  BandIDs::output), 0.0f } } },
        { "Init (Transparent)", { { amount, 0.0f },
                                  { bandParam (BandHigh, BandIDs::input), 0.0f },
                                  { bandParam (BandMid,  BandIDs::input), 0.0f },
                                  { bandParam (BandLow,  BandIDs::input), 0.0f },
                                  { bandParam (BandHigh, BandIDs::output), 0.0f },
                                  { bandParam (BandMid,  BandIDs::output), 0.0f },
                                  { bandParam (BandLow,  BandIDs::output), 0.0f } } },
    };
    return presets;
}

} // namespace ott
