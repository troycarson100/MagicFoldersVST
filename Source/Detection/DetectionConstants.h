#pragma once

namespace Detection
{
    // Must match training/model_constants.py so plugin and Python use the same input shape.
    constexpr int kModelSampleRate   = 22050;
    constexpr int kModelNumSamples   = 44100;   // 2 seconds
    constexpr int kModelNfft        = 2048;
    constexpr int kModelHopLength   = 512;
    constexpr int kModelNMelBins    = 128;
    constexpr int kModelNFrames    = 87;       // (kModelNumSamples + kModelHopLength - 1) / kModelHopLength
}
