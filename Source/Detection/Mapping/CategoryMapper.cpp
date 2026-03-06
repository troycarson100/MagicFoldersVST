#include "CategoryMapper.h"

namespace DetectionMapping
{
    std::vector<CategoryScore> CategoryMapper::map(const std::vector<float>& yamnetScores,
                                                   const PercussiveHarmonicFeatures& feats) const
    {
        // YamnetRunner returns 13 logits in instrument-class order.
        const int n = (int) yamnetScores.size();
        if (n <= 0)
            return {};

        std::vector<CategoryScore> out;
        out.reserve(n);

        auto add = [&out](DetectionCategory cat, float score)
        {
            CategoryScore cs;
            cs.category = cat;
            cs.score = score;
            out.push_back(cs);
        };

        const float percussive = feats.percussiveLikelihood;
        const float harmonic = feats.harmonicLikelihood;

        // Index order must match Detection::Class in ModelRunner.h / YamnetRunner.
        if (n > 0)  add(DetectionCategory::Kick,         yamnetScores[0]);
        if (n > 1)  add(DetectionCategory::Snare,        yamnetScores[1]);
        if (n > 2)  add(DetectionCategory::HiHat,        yamnetScores[2]);
        if (n > 3)  add(DetectionCategory::Perc,         yamnetScores[3]);
        if (n > 4)  add(DetectionCategory::Bass,         yamnetScores[4]);
        if (n > 5)  add(DetectionCategory::Guitar,       yamnetScores[5]);
        if (n > 6)  add(DetectionCategory::Keys,         yamnetScores[6]);
        if (n > 7)  add(DetectionCategory::Pad,          yamnetScores[7]);
        if (n > 8)  add(DetectionCategory::Lead,         yamnetScores[8]);
        if (n > 9)  add(DetectionCategory::FX,           yamnetScores[9]);
        if (n > 10) add(DetectionCategory::TextureAtmos, yamnetScores[10]);
        if (n > 11) add(DetectionCategory::Vocal,        yamnetScores[11]);
        if (n > 12) add(DetectionCategory::Drums,        yamnetScores[12]);

        // DSP-dependent weighting: emphasize drums when percussive, tonal when harmonic.
        for (auto& cs : out)
        {
            const bool drum =
                cs.category == DetectionCategory::Kick ||
                cs.category == DetectionCategory::Snare ||
                cs.category == DetectionCategory::HiHat ||
                cs.category == DetectionCategory::Perc ||
                cs.category == DetectionCategory::Drums;

            const bool tonal =
                cs.category == DetectionCategory::Bass ||
                cs.category == DetectionCategory::Guitar ||
                cs.category == DetectionCategory::Keys ||
                cs.category == DetectionCategory::Pad ||
                cs.category == DetectionCategory::Lead ||
                cs.category == DetectionCategory::Vocal ||
                cs.category == DetectionCategory::TextureAtmos;

            if (drum)
                cs.score *= (1.0f + 0.5f * percussive);
            if (tonal)
                cs.score *= (1.0f + 0.5f * harmonic);
        }

        // Softmax over categories.
        float maxScore = out.empty() ? 0.0f : out.front().score;
        for (const auto& cs : out)
            maxScore = std::max(maxScore, cs.score);

        float sum = 0.0f;
        for (auto& cs : out)
        {
            cs.score = std::exp(cs.score - maxScore);
            sum += cs.score;
        }
        if (sum > 0.0f)
        {
            for (auto& cs : out)
                cs.score /= sum;
        }

        return out;
    }
} // namespace DetectionMapping

