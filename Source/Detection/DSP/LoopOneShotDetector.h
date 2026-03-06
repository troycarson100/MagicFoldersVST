#pragma once

#include <JuceHeader.h>

namespace DetectionDSP
{
    struct LoopDetectionResult
    {
        bool isLoop = false;
        float confidence = 0.0f;
    };

    class LoopOneShotDetector
    {
    public:
        static LoopDetectionResult detect(const juce::AudioBuffer<float>& buffer,
                                          double sampleRate,
                                          bool enabled);
    };
} // namespace DetectionDSP

