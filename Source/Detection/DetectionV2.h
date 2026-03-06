#pragma once

#include <JuceHeader.h>
#include <array>
#include "ModelRunner.h"
#include "YamnetRunner.h"
#include "FilenameBias.h"
#include "ConfidenceGate.h"

namespace Detection
{
    struct DetectionResult
    {
        bool hasDecision = false;
        Class primary = Class::Other;
        float top1Prob = 0.0f;
        float top2Prob = 0.0f;
    };

    /** Orchestrates model inference, filename priors and confidence gating. */
    class DetectionV2
    {
    public:
        DetectionV2() = default;

        bool isAvailable() const noexcept { return modelRunner.isAvailable() || yamnetRunner.isAvailable(); }

        /** Run the v2 detection pipeline on a mono buffer.
            - mono: single-channel analysis buffer (not on the audio thread)
            - sampleRate: buffer sample rate
            - isLoop: true when the sample was classified as a loop
            - originalName: filename without extension
        */
        DetectionResult classify(const juce::AudioBuffer<float>& mono,
                                 double sampleRate,
                                 bool isLoop,
                                 const juce::String& originalName) const;

    private:
        static void softmax(const std::array<float, kNumClasses>& logits,
                            std::array<float, kNumClasses>& probs);

        ModelRunner modelRunner;
        YamnetRunner yamnetRunner;
    };

    const char* classToString(Class c);
} // namespace Detection

