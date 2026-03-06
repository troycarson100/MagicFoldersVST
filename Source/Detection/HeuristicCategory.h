#pragma once

#include <JuceHeader.h>

namespace HeuristicCategory
{
    /** Input features for the heuristic category pipeline (from Essentia + plugin). */
    struct Features
    {
        float centroidF   = 0.0f;
        float zcrF        = 0.0f;
        float rolloffF    = 0.0f;
        float mfcc1       = 0.0f;
        float mfcc2       = 0.0f;
        bool hasSharpAttack = false;
        bool isTonal      = false;
        double duration   = 0.0;
        int onsetCount    = 0;
        int bpm           = 0;
        juce::String type;  // "Loop" or "One-Shot"
    };

    /** Output of the heuristic category pipeline. */
    struct Result
    {
        juce::String category;     // Kicks, Snares, Hi-Hats, Bass, Guitar, Melodic, FX, Textures, etc.
        juce::String melodicVibe;  // Pad, Pluck, Lead, Keys (only when category == Melodic)
        juce::String type;        // "Loop" or "One-Shot" (set from input for filename hints)
    };

    /** Run the heuristic category decision and filename hints. */
    Result run(const Features& f, const juce::File& file);
}
