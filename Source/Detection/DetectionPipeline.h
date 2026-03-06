#pragma once

#include <JuceHeader.h>
#include <vector>

namespace DetectionPipeline
{
    enum class DetectionCategory
    {
        Kick,
        Snare,
        HiHat,
        Perc,
        Drums,
        Bass,
        Guitar,
        Keys,
        Pad,
        Lead,
        FX,
        TextureAtmos,
        Vocal,
        Unknown
    };

    struct CategoryScore
    {
        DetectionCategory category{};
        float score = 0.0f;
    };

    enum class GateReason
    {
        None,
        BelowMinScore,
        BelowMargin,
        InconsistentVotes,
        ModelUnavailable
    };

    struct DebugInfo
    {
        juce::String top1Label;
        float top1Score = 0.0f;
        juce::String top2Label;
        float top2Score = 0.0f;
        juce::String top3Label;
        float top3Score = 0.0f;

        GateReason gateReason = GateReason::None;

        float percussiveLikelihood = 0.0f;
        float harmonicLikelihood = 0.0f;
        float lowFreqEnergyRatio = 0.0f;
        float highFreqEnergyRatio = 0.0f;
    };

    struct DetectionResult
    {
        DetectionCategory category = DetectionCategory::Unknown;
        float confidence = 0.0f;

        bool isLoop = false;
        float loopConfidence = 0.0f;

        DebugInfo debug;
    };

    struct DetectionConfig
    {
        bool strictMode = true;
        int maxWindows = 8;
        bool autoDetectType = true;
    };

    /** High-level entry point used by the plugin and batch harness. */
    DetectionResult detectFile(const juce::File& file, const DetectionConfig& config);
} // namespace DetectionPipeline

