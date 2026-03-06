#pragma once

#include <JuceHeader.h>
#include <array>
#include "ModelRunner.h"

namespace Detection
{
    /** Apply soft filename-based priors to class logits.
        This never forces a class; it only adds small positive bias to the
        relevant logits based on common abbreviations in the original filename.
        The boosts are intended to be small enough that a strongly confident
        model output is not overridden.
    */
    struct FilenameBias
    {
        static void apply(const juce::String& originalName,
                          std::array<float, kNumClasses>& logits);
    };
} // namespace Detection

