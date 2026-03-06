#include "ConfidenceGate.h"

namespace Detection
{
    GateResult ConfidenceGate::apply(const std::array<float, kNumClasses>& probs, bool isLoop)
    {
        // Drums need a higher bar because false drum assignments are jarring.
        // For loops, energy is spread across many windows so the averaged score
        // is inherently lower — relax the threshold so genuine kick/snare loops
        // are not rejected and fall through to the heuristic as "Melodic".
        const float kMinTop1Drum   = isLoop ? 0.28f : 0.40f;
        const float kMinTop1Tonal  = 0.25f;
        const float kMinMarginDrum = isLoop ? 0.04f : 0.06f;
        const float kMinMarginTonal = 0.04f;
        GateResult result;

        int top1Index = 0;
        int top2Index = 1;
        float top1 = probs[0];
        float top2 = probs[1];

        for (int i = 0; i < kNumClasses; ++i)
        {
            float p = probs[(size_t) i];
            if (p > top1)
            {
                top2 = top1;
                top2Index = top1Index;
                top1 = p;
                top1Index = i;
            }
            else if (p > top2 && i != top1Index)
            {
                top2 = p;
                top2Index = i;
            }
        }

        result.top1Prob = top1;
        result.top2Prob = top2;
        result.primary = static_cast<Class>(top1Index);

        const float margin = top1 - top2;

        // Kick=0, Snare=1, HiHat=2, Perc=3 — everything else is tonal/FX.
        const bool isDrum = (top1Index >= static_cast<int>(Class::Kick) &&
                             top1Index <= static_cast<int>(Class::Perc));
        const float minTop1  = isDrum ? kMinTop1Drum  : kMinTop1Tonal;
        const float minMargin= isDrum ? kMinMarginDrum : kMinMarginTonal;

        if (top1 >= minTop1 && margin >= minMargin)
            result.accepted = true;

        return result;
    }
} // namespace Detection

