#include "ConfidenceGate.h"

namespace DetectionGating
{
    namespace
    {
        struct Ranked
        {
            int index = -1;
            float score = 0.0f;
        };

        Ranked getTop(const std::vector<CategoryScore>& scores, int skipIndex = -1)
        {
            Ranked r;
            for (int i = 0; i < (int) scores.size(); ++i)
            {
                if (i == skipIndex) continue;
                if (scores[i].score > r.score || r.index < 0)
                {
                    r.index = i;
                    r.score = scores[i].score;
                }
            }
            return r;
        }
    }

    GateDecision ConfidenceGate::apply(const std::vector<CategoryScore>& categoryScores,
                                       const std::vector<std::vector<float>>& windowScores,
                                       bool strictMode) const
    {
        GateDecision out;
        if (categoryScores.empty())
        {
            out.reason = 1;
            return out;
        }

        const float minTopScore = strictMode ? 0.25f : 0.20f;
        const float minMargin = strictMode ? 0.08f : 0.05f;
        const float minVoteConsistency = strictMode ? 0.60f : 0.50f;

        const auto top1 = getTop(categoryScores);
        const auto top2 = getTop(categoryScores, top1.index);

        const float margin = top1.score - top2.score;
        if (top1.score < minTopScore)
        {
            out.reason = 1;
        }
        else if (margin < minMargin)
        {
            out.reason = 2;
        }

        // Window vote consistency: majority of windows should agree on top category.
        if (!windowScores.empty())
        {
            const int numWindows = (int) windowScores.size();
            int agree = 0;
            for (const auto& ws : windowScores)
            {
                if ((int) ws.size() <= top1.index || top1.index < 0)
                    continue;
                // Assume YamnetModel and CategoryMapper keep consistent index mapping.
                const float s = ws[(size_t) top1.index];
                if (s >= 0.5f * top1.score)
                    ++agree;
            }
            const float consistency = (numWindows > 0) ? (agree / (float) numWindows) : 1.0f;
            if (consistency < minVoteConsistency && out.reason == 0)
                out.reason = 3;
        }

        // Fill debug label/score info using category names.
        auto catName = [](DetectionCategory cat) -> juce::String
        {
            switch (cat)
            {
                case DetectionCategory::Kick:          return "Kick";
                case DetectionCategory::Snare:         return "Snare";
                case DetectionCategory::HiHat:         return "HiHat";
                case DetectionCategory::Perc:          return "Perc";
                case DetectionCategory::Drums:         return "Drums";
                case DetectionCategory::Bass:          return "Bass";
                case DetectionCategory::Guitar:        return "Guitar";
                case DetectionCategory::Keys:          return "Keys";
                case DetectionCategory::Pad:           return "Pad";
                case DetectionCategory::Lead:          return "Lead";
                case DetectionCategory::Vocal:         return "Vocal";
                case DetectionCategory::FX:            return "FX";
                case DetectionCategory::TextureAtmos:  return "TextureAtmos";
                case DetectionCategory::Unknown:       return "Unknown";
            }
            return "Unknown";
        };

        out.top1Label = (top1.index >= 0) ? catName(categoryScores[(size_t) top1.index].category) : "Unknown";
        out.top1Score = top1.score;
        out.top2Label = (top2.index >= 0) ? catName(categoryScores[(size_t) top2.index].category) : "";
        out.top2Score = top2.score;

        // For top3, pick next best excluding first two.
        const auto top3 = getTop(categoryScores, top2.index);
        out.top3Label = (top3.index >= 0) ? catName(categoryScores[(size_t) top3.index].category) : "";
        out.top3Score = top3.score;

        if (out.reason == 0)
        {
            out.category = categoryScores[(size_t) top1.index].category;
            out.confidence = top1.score;
        }

        return out;
    }
} // namespace DetectionGating

