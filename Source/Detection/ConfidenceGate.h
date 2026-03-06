#pragma once

#include <array>
#include "ModelRunner.h"

namespace Detection
{
    struct GateResult
    {
        bool accepted = false;
        Class primary = Class::Other;
        float top1Prob = 0.0f;
        float top2Prob = 0.0f;
    };

    /** Confidence-based accept / reject logic for model predictions.
        @param isLoop  When true, drum confidence thresholds are relaxed because
                       multi-window averaging dilutes transient energy in loops.
    */
    struct ConfidenceGate
    {
        static GateResult apply(const std::array<float, kNumClasses>& probs,
                                bool isLoop = false);
    };
} // namespace Detection

