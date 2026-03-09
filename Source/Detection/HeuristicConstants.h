#pragma once

namespace Heuristic
{
    // Kick: three-level rule (rolloff → centroid low → centroid mid).
    // kKickRolloffMax — Level 0: if 85% of energy is below this frequency,
    //   the sound is sub-bass regardless of ZCR (catches layered/noisy house kicks).
    //   Guitar/snare/hihat cannot have rolloff this low.
    // kKickCentroidLow  — Level 1: unambiguously a kick even when tonal
    //   (e.g. 808 with pitch sweep): centroid < 600 Hz + sharp attack.
    //   Restricted to One-Shots; CNN14 handles kick loop detection.
    // kKickCentroidMax  — Level 2: moderate centroid, requires !isTonal so
    //   bass guitar plucks don't match.
    // kKickZcrMax = 0.20 creates a clean non-overlapping boundary with hi-hats.
    constexpr float kKickRolloffMax    = 250.0f;
    constexpr float kKickCentroidLow   = 600.0f;
    constexpr float kKickCentroidMax   = 1400.0f;
    constexpr float kKickZcrMax        = 0.20f;

    // Snare: mid centroid range, starts above kick max to avoid overlap.
    constexpr float kSnareCentroidMin  = 1400.0f;
    constexpr float kSnareCentroidMax  = 4000.0f;

    // Hi-hat: high ZCR, bright, very short, and very high spectral centroid.
    // Real hi-hats sit at 4000–12000 Hz centroid; snares typically at 800–3000 Hz.
    // The centroid floor prevents bright snares from misfiring as Hi-Hat.
    constexpr float kHiHatZcrMin       = 0.20f;
    constexpr float kHiHatRolloffMin   = 4000.0f;
    constexpr float kHiHatDurationMax  = 0.8f;
    constexpr float kHiHatCentroidMin  = 4000.0f;

    // Bass: very low centroid, no sharp attack, dark MFCC
    constexpr float kBassCentroidMax   = 600.0f;
    constexpr float kBassMfcc1Max      = -10.0f;

    // FX: noisy, very bright (brightness = rolloff/centroid helps separate from dark textures)
    constexpr float kFxZcrMin          = 0.1f;
    constexpr float kFxRolloffMin      = 6000.0f;
    constexpr float kFxBrightnessMin   = 2.5f;  // rolloff/centroid ratio

    // Percussion: short, sharp attack.
    // Raised from 0.5 → 1.5 s: longer one-shots (e.g. layered snares with
    // long reverb tail, 808s with pitch decay) should still be Percussion
    // rather than falling to Other.
    constexpr float kPercDurationMax   = 1.5f;

    // Attack vs body RMS ratio for "sharp attack".
    // Lowered from 2.5 → 1.8: some kicks and snares have a moderately punchy
    // attack (ratio ~2.0) that still classifies clearly as percussive.
    constexpr float kSharpAttackRatio  = 1.8f;

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
