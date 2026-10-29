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
        { "OTT 50%", { { mix, 50.0f } } },
        { "OTT 25%", { { mix, 25.0f } } },
        { "Upward Only", { { downward, 0.0f } } },
        { "Downward Only", { { upward, 0.0f } } },
        { "Slow Glue", { { mix, 35.0f }, { time, 300.0f } } },
        { "Fast Smash", { { time, 40.0f }, { inGain, 4.0f }, { output, -4.0f } } },
        { "Two Band (No Low)", { { lowOn, 0.0f } } },
        { "Init (Transparent)", { { mix, 0.0f } } },
    };
    return presets;
}

} // namespace ott
