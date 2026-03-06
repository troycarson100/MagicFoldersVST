#pragma once

#include <vector>
#include "../DetectionPipeline.h"
#include "../DSP/PercussiveHarmonicGate.h"

namespace DetectionMapping
{
    using DetectionPipeline::CategoryScore;
    using DetectionPipeline::DetectionCategory;
    using DetectionDSP::PercussiveHarmonicFeatures;

    class CategoryMapper
    {
    public:
        /** Map aggregated YAMNet scores (in the current runner's class order)
            into Magic Folders categories, applying simple DSP-dependent
            weighting.
        */
        std::vector<CategoryScore> map(const std::vector<float>& yamnetScores,
                                       const PercussiveHarmonicFeatures& feats = {}) const;
    };
} // namespace DetectionMapping

