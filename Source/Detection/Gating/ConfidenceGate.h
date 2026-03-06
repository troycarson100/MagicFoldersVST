#pragma once

#include <vector>
#include <JuceHeader.h>
#include "../DetectionPipeline.h"

namespace DetectionGating
{
    using DetectionPipeline::CategoryScore;
    using DetectionPipeline::DetectionCategory;

    struct GateDecision
    {
        DetectionCategory category = DetectionCategory::Unknown;
        float confidence = 0.0f;
        int reason = 0; // 0=None, 1=BelowMinScore, 2=BelowMargin, 3=InconsistentVotes

        juce::String top1Label;
        float top1Score = 0.0f;
        juce::String top2Label;
        float top2Score = 0.0f;
        juce::String top3Label;
        float top3Score = 0.0f;
    };

    class ConfidenceGate
    {
    public:
        GateDecision apply(const std::vector<CategoryScore>& categoryScores,
                           const std::vector<std::vector<float>>& windowScores,
                           bool strictMode) const;
    };
} // namespace DetectionGating

