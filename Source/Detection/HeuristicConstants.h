#pragma once

namespace Heuristic
{
    // Kick: low centroid, low ZCR, sharp attack
    constexpr float kKickCentroidMax   = 800.0f;
    constexpr float kKickZcrMax        = 0.1f;

    // Snare: mid centroid range
    constexpr float kSnareCentroidMin  = 800.0f;
    constexpr float kSnareCentroidMax  = 3000.0f;

    // Hi-hat: high ZCR, bright, very short
    constexpr float kHiHatZcrMin       = 0.20f;
    constexpr float kHiHatRolloffMin   = 4000.0f;
    constexpr float kHiHatDurationMax  = 0.8f;

    // Bass: very low centroid, no sharp attack, dark MFCC
    constexpr float kBassCentroidMax   = 600.0f;
    constexpr float kBassMfcc1Max      = -10.0f;

    // FX: noisy, very bright (brightness = rolloff/centroid helps separate from dark textures)
    constexpr float kFxZcrMin          = 0.1f;
    constexpr float kFxRolloffMin      = 6000.0f;
    constexpr float kFxBrightnessMin   = 2.5f;  // rolloff/centroid ratio

    // Percussion: short, sharp attack
    constexpr float kPercDurationMax   = 0.5f;

    // Attack vs body RMS ratio for "sharp attack"
    constexpr float kSharpAttackRatio  = 2.5f;

    // Guitar-like loop: duration and onset count range
    constexpr double kGuitarDurationMin = 0.75;
    constexpr double kGuitarDurationMax = 16.0;
    constexpr int    kGuitarOnsetMin     = 2;
    constexpr int    kGuitarOnsetMax     = 48;
    constexpr float  kGuitarCentroidMin  = 300.0f;
    constexpr float  kGuitarCentroidMax  = 5500.0f;
    constexpr float  kGuitarZcrMaxRhythm = 0.22f;
    constexpr float  kGuitarCentroidMinPicked = 250.0f;
    constexpr float  kGuitarCentroidMaxPicked = 4500.0f;
    constexpr float  kGuitarZcrMaxPicked = 0.25f;

    // Guitar (non-loop or loop): tonal mid-range
    constexpr float  kGuitarTonalCentroidMin = 400.0f;
    constexpr float  kGuitarTonalCentroidMax = 4500.0f;
    constexpr int    kGuitarTonalOnsetMin   = 2;
    constexpr int    kGuitarTonalOnsetMax   = 24;
    constexpr int    kGuitarTonalOnsetMax2  = 30;
    constexpr float  kGuitarTonalZcrMax     = 0.12f;

    // Songstarter: long loop, groove
    constexpr double kSongstarterDurationMin = 4.0;
    constexpr int    kSongstarterOnsetMin    = 4;
    constexpr int    kSongstarterOnsetMax    = 32;

    // Textures: dark/sparse or noisy swish
    constexpr float  kTextureCentroidMax     = 1800.0f;
    constexpr float  kTextureZcrMax          = 0.12f;
    constexpr float  kTextureNoisyZcrMin      = 0.18f;
    constexpr float  kTextureNoisyRolloffMin  = 6000.0f;
    constexpr double kTextureDurationMin     = 3.0;
    constexpr int    kTextureOnsetMax        = 4;

    // Melodic vibe (Pad / Pluck / Lead / Keys)
    constexpr double kMelodicPadDurationMin  = 1.5;
    constexpr int    kMelodicPadOnsetMax     = 6;
    constexpr double kMelodicPluckDurationMax = 2.5;
    constexpr int    kMelodicPluckOnsetMin   = 5;
    constexpr float  kMelodicLeadCentroidMin = 2400.0f;

    // Key strength thresholds
    constexpr float  kKeyStrengthForKeyResult = 0.35f;
    constexpr float  kKeyStrengthTonalMin     = 0.3f;
    constexpr float  kFirstToSecondStrengthMin = 0.1f;
}
