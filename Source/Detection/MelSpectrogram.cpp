#include "MelSpectrogram.h"
#include <cmath>
#include <algorithm>

namespace Detection
{
    static constexpr int kFftSize = kModelNfft;
    static constexpr int kFftOrder = 11;  // 2^11 = 2048
    static constexpr int kNumFftBins = kFftSize / 2 + 1;  // 1025

    MelSpectrogram::MelSpectrogram()
    {
        fft_ = std::make_unique<juce::dsp::FFT>(kFftOrder);
        fftBuffer_.resize(static_cast<size_t>(2 * fft_->getSize()));
        window_.resize(static_cast<size_t>(kFftSize));
        for (int i = 0; i < kFftSize; ++i)
            window_[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * (float)i / (float)(kFftSize - 1)));
        buildMelFilterbank();
    }

    void MelSpectrogram::buildMelFilterbank()
    {
        // Matches torchaudio.transforms.MelSpectrogram(mel_scale='htk', norm=None, f_min=0, f_max=sr/2)
        // Uses fractional Hz-domain interpolation (not rounded integer bins) to avoid collapsed filters.
        const float sampleRate = (float) kModelSampleRate;
        const float fMin = 0.0f;
        const float fMax = sampleRate / 2.0f;
        const int nMel = kModelNMelBins;
        const int nFftBins = kNumFftBins;

        melFilters_.resize(static_cast<size_t>(nMel * nFftBins), 0.0f);

        auto hzToMelF = [](float hz) { return 2595.0f * std::log10f(1.0f + hz / 700.0f); };
        auto melToHzF = [](float mel) { return 700.0f * (std::powf(10.0f, mel / 2595.0f) - 1.0f); };

        // N_MELS+2 points evenly spaced in mel, converted back to Hz
        const float melMin = hzToMelF(fMin);
        const float melMax = hzToMelF(fMax);
        std::vector<float> freqPts(static_cast<size_t>(nMel + 2));
        for (int i = 0; i <= nMel + 1; ++i)
            freqPts[static_cast<size_t>(i)] = melToHzF(melMin + (melMax - melMin) * (float)i / (float)(nMel + 1));

        // FFT bin centre frequencies: linspace(0, fMax, nFftBins)
        std::vector<float> fftFreqs(static_cast<size_t>(nFftBins));
        for (int k = 0; k < nFftBins; ++k)
            fftFreqs[static_cast<size_t>(k)] = fMax * (float)k / (float)(nFftBins - 1);

        // Triangular filters in Hz-domain (avoids zero-weight bins from integer rounding)
        for (int m = 0; m < nMel; ++m)
        {
            const float fLeft   = freqPts[static_cast<size_t>(m)];
            const float fCenter = freqPts[static_cast<size_t>(m + 1)];
            const float fRight  = freqPts[static_cast<size_t>(m + 2)];
            const float risingWidth  = fCenter - fLeft;
            const float fallingWidth = fRight  - fCenter;

            for (int k = 0; k < nFftBins; ++k)
            {
                const float f = fftFreqs[static_cast<size_t>(k)];
                float w = 0.0f;
                if (f >= fLeft && f < fCenter && risingWidth > 0.0f)
                    w = (f - fLeft) / risingWidth;
                else if (f >= fCenter && f <= fRight && fallingWidth > 0.0f)
                    w = (fRight - f) / fallingWidth;
                melFilters_[static_cast<size_t>(m * nFftBins + k)] = w;
            }
        }
    }

    void MelSpectrogram::compute(const float* audio, int numSamples, double sampleRate, float* out) const
    {
        std::vector<float> work(static_cast<size_t>(kModelNumSamples), 0.0f);
        if (std::abs(sampleRate - (double) kModelSampleRate) < 1.0)
        {
            int toCopy = juce::jmin(numSamples, kModelNumSamples);
            for (int i = 0; i < toCopy; ++i)
                work[static_cast<size_t>(i)] = audio[i];
        }
        else
        {
            double ratio = (double) kModelSampleRate / sampleRate;
            for (int i = 0; i < kModelNumSamples; ++i)
            {
                double srcIdx = (double) i / ratio;
                int i0 = (int) srcIdx;
                int i1 = juce::jmin(i0 + 1, numSamples);
                float f = (float) (srcIdx - i0);
                float s0 = (i0 < numSamples) ? audio[i0] : 0.0f;
                float s1 = (i1 < numSamples) ? audio[i1] : s0;
                work[static_cast<size_t>(i)] = s0 + f * (s1 - s0);
            }
        }

        const int nFftBins = kNumFftBins;
        const int nMel = kModelNMelBins;
        const int nFrames = kModelNFrames;
        const int hop = kModelHopLength;

        for (int frame = 0; frame < nFrames; ++frame)
        {
            int start = frame * hop;
            for (int i = 0; i < kFftSize; ++i)
            {
                float s = (start + i < kModelNumSamples) ? work[static_cast<size_t>(start + i)] : 0.0f;
                fftBuffer_[static_cast<size_t>(i)] = s * window_[static_cast<size_t>(i)];
            }
            for (size_t i = static_cast<size_t>(kFftSize); i < fftBuffer_.size(); ++i)
                fftBuffer_[i] = 0.0f;

            fft_->performFrequencyOnlyForwardTransform(fftBuffer_.data(), true);

            for (int m = 0; m < nMel; ++m)
            {
                float sum = 0.0f;
                for (int k = 0; k < nFftBins; ++k)
                {
                    // performFrequencyOnlyForwardTransform already returns power (|X|^2),
                    // so no need to square again — squaring twice was giving magnitude^4
                    float power = fftBuffer_[static_cast<size_t>(k)];
                    sum += power * melFilters_[static_cast<size_t>(m * nFftBins + k)];
                }
                out[m * nFrames + frame] = std::log(sum + 1e-9f);
            }
        }
    }
}
