#pragma once

#include <JuceHeader.h>
#include <vector>

namespace DetectionDSP
{
    class Resampler16kMono
    {
    public:
        void process(const juce::AudioBuffer<float>& in,
                     double inSampleRate,
                     std::vector<float>& out16kMono);
    };
} // namespace DetectionDSP

