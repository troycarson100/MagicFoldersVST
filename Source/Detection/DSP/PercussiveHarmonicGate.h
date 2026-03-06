#pragma once

#include <JuceHeader.h>

namespace DetectionDSP
{
    struct PercussiveHarmonicFeatures
    {
        float percussiveLikelihood = 0.0f;
        float harmonicLikelihood = 0.0f;
        float lowFreqEnergyRatio = 0.0f;
        float highFreqEnergyRatio = 0.0f;
    };

    class PercussiveHarmonicGate
    {
    public:
        static PercussiveHarmonicFeatures analyze(const juce::AudioBuffer<float>& buffer,
                                                  double sampleRate);
    };
} // namespace DetectionDSP

