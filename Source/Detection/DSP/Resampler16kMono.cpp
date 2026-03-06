#include "Resampler16kMono.h"

namespace DetectionDSP
{
    void Resampler16kMono::process(const juce::AudioBuffer<float>& in,
                                   double inSampleRate,
                                   std::vector<float>& out16kMono)
    {
        const int numSamples = in.getNumSamples();
        const int numChannels = in.getNumChannels();
        if (numSamples <= 0 || numChannels <= 0 || inSampleRate <= 0.0)
        {
            out16kMono.clear();
            return;
        }

        // Downmix to mono.
        juce::AudioBuffer<float> mono(1, numSamples);
        mono.clear();
        for (int ch = 0; ch < numChannels; ++ch)
            mono.addFrom(0, 0, in, ch, 0, numSamples, 1.0f / (float) numChannels);

        const double targetRate = 16000.0;
        const double ratio = targetRate / inSampleRate;
        const int outSamples = (int) std::max(1.0, std::floor(numSamples * ratio));

        out16kMono.resize((size_t) outSamples);
        const float* src = mono.getReadPointer(0);
        for (int i = 0; i < outSamples; ++i)
        {
            const double idx = i / ratio;
            const int i0 = juce::jlimit(0, numSamples - 1, (int) idx);
            out16kMono[(size_t) i] = src[i0];
        }

        // Normalize to [-1, 1].
        float maxAbs = 0.0f;
        for (float v : out16kMono)
            maxAbs = std::max(maxAbs, std::abs(v));
        if (maxAbs > 0.0f)
        {
            const float inv = 1.0f / maxAbs;
            for (float& v : out16kMono)
                v *= inv;
        }
    }
} // namespace DetectionDSP

