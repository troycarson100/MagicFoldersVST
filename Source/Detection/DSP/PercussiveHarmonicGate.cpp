#include "PercussiveHarmonicGate.h"

namespace DetectionDSP
{
    PercussiveHarmonicFeatures PercussiveHarmonicGate::analyze(const juce::AudioBuffer<float>& buffer,
                                                               double sampleRate)
    {
        PercussiveHarmonicFeatures out;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples <= 0 || numChannels <= 0 || sampleRate <= 0.0)
            return out;

        juce::AudioBuffer<float> mono(1, numSamples);
        mono.clear();
        for (int ch = 0; ch < numChannels; ++ch)
            mono.addFrom(0, 0, buffer, ch, 0, numSamples, 1.0f / (float) numChannels);

        // Simple envelope peakiness: ratio of max frame RMS to mean RMS.
        const int frameSize = 1024;
        const int hop = 512;
        float sumRms = 0.0f;
        float maxRms = 0.0f;
        int frameCount = 0;

        for (int start = 0; start + frameSize <= numSamples; start += hop)
        {
            ++frameCount;
            double sum = 0.0;
            for (int i = 0; i < frameSize; ++i)
            {
                const float s = mono.getSample(0, start + i);
                sum += (double) s * (double) s;
            }
            const float rms = (float) std::sqrt(sum / frameSize);
            sumRms += rms;
            maxRms = std::max(maxRms, rms);
        }

        const float meanRms = (frameCount > 0) ? (sumRms / frameCount) : 0.0f;
        const float peakiness = (meanRms > 0.0f) ? (maxRms / meanRms) : 0.0f;

        // Very simple ZCR.
        int zeroCrossings = 0;
        float prev = mono.getSample(0, 0);
        for (int i = 1; i < numSamples; ++i)
        {
            const float s = mono.getSample(0, i);
            if ((s >= 0.0f && prev < 0.0f) || (s < 0.0f && prev >= 0.0f))
                ++zeroCrossings;
            prev = s;
        }
        const float zcr = zeroCrossings / (float) numSamples;

        // Coarse low/high-band energy split via simple IIR filters.
        juce::dsp::IIR::Coefficients<float>::Ptr lowCoeffs =
            juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 200.0);
        juce::dsp::IIR::Coefficients<float>::Ptr highCoeffs =
            juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 4000.0);

        juce::dsp::IIR::Filter<float> lowFilter(*lowCoeffs);
        juce::dsp::IIR::Filter<float> highFilter(*highCoeffs);

        std::vector<float> low(numSamples), high(numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const float s = mono.getSample(0, i);
            low[(size_t) i] = lowFilter.processSample(s);
            high[(size_t) i] = highFilter.processSample(s);
        }

        double lowEnergy = 0.0;
        double highEnergy = 0.0;
        double fullEnergy = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const float s = mono.getSample(0, i);
            fullEnergy += (double) s * (double) s;
            lowEnergy += (double) low[(size_t) i] * (double) low[(size_t) i];
            highEnergy += (double) high[(size_t) i] * (double) high[(size_t) i];
        }

        if (fullEnergy <= 0.0)
            return out;

        out.lowFreqEnergyRatio = (float) (lowEnergy / fullEnergy);
        out.highFreqEnergyRatio = (float) (highEnergy / fullEnergy);

        // Heuristic mapping to percussive vs harmonic likelihood.
        out.percussiveLikelihood = juce::jlimit(0.0f, 1.0f, peakiness / 3.0f);
        out.harmonicLikelihood = juce::jlimit(0.0f, 1.0f, 1.0f - (zcr * 2.0f));
        return out;
    }
} // namespace DetectionDSP

