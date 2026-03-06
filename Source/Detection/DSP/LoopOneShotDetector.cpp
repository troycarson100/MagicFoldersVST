#include "LoopOneShotDetector.h"

namespace DetectionDSP
{
    LoopDetectionResult LoopOneShotDetector::detect(const juce::AudioBuffer<float>& buffer,
                                                    double sampleRate,
                                                    bool enabled)
    {
        LoopDetectionResult out;
        if (!enabled || buffer.getNumSamples() <= 0 || sampleRate <= 0.0)
            return out;

        const int numSamples = buffer.getNumSamples();
        const float durationSeconds = (float) (numSamples / sampleRate);

        // Simple heuristic: longer than 2s with multiple energy bursts -> loop.
        juce::AudioBuffer<float> mono(1, numSamples);
        mono.clear();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            mono.addFrom(0, 0, buffer, ch, 0, numSamples, 1.0f / (float) buffer.getNumChannels());

        const int frameSize = 2048;
        const int hop = 1024;
        int numFrames = 0;
        int activeFrames = 0;

        for (int start = 0; start + frameSize <= numSamples; start += hop)
        {
            ++numFrames;
            double sum = 0.0;
            for (int i = 0; i < frameSize; ++i)
            {
                const float s = mono.getSample(0, start + i);
                sum += (double) s * (double) s;
            }
            const float rms = (float) std::sqrt(sum / frameSize);
            if (rms > 0.01f)
                ++activeFrames;
        }

        const float activity = (numFrames > 0) ? (activeFrames / (float) numFrames) : 0.0f;

        const bool looksLikeLoop = (durationSeconds > 2.0f && activity > 0.3f);
        out.isLoop = looksLikeLoop;
        out.confidence = juce::jlimit(0.0f, 1.0f, (durationSeconds / 4.0f) * 0.5f + activity * 0.5f);
        return out;
    }
} // namespace DetectionDSP

