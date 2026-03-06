#pragma once

#include <JuceHeader.h>
#include <vector>
#include "DetectionConstants.h"

namespace Detection
{
    /** Computes log-mel spectrogram matching training/model_constants.py.
     *  Output shape: (kModelNMelBins, kModelNFrames) stored row-major in a flat vector.
     */
    class MelSpectrogram
    {
    public:
        MelSpectrogram();

        /** Compute log-mel from mono float buffer at given sample rate.
         *  Audio is resampled to kModelSampleRate, trimmed/padded to kModelNumSamples, then processed.
         *  Output written to 'out': must be at least kModelNMelBins * kModelNFrames floats.
         */
        void compute(const float* audio, int numSamples, double sampleRate, float* out) const;

    private:
        void buildMelFilterbank();
        std::vector<float> melFilters_;  // kModelNMelBins * (1 + kModelNfft/2)
        std::unique_ptr<juce::dsp::FFT> fft_;
        mutable std::vector<float> fftBuffer_;
        std::vector<float> window_;
    };
}
