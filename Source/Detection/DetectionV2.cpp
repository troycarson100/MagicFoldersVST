#include "DetectionV2.h"
#include <cmath>

namespace Detection
{
    namespace
    {
        void appendDetectionLog(const juce::String& line)
        {
            juce::File dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                 .getChildFile("MagicFoldersLogs");
            dir.createDirectory();
            juce::File logFile = dir.getChildFile("detection.log");
            logFile.appendText(line + "\n");
        }
    }

    void DetectionV2::softmax(const std::array<float, kNumClasses>& logits,
                              std::array<float, kNumClasses>& probs)
    {
        float maxLogit = logits[0];
        for (int i = 1; i < kNumClasses; ++i)
            maxLogit = std::max(maxLogit, logits[(size_t) i]);

        float sum = 0.0f;
        for (int i = 0; i < kNumClasses; ++i)
        {
            float e = std::exp(logits[(size_t) i] - maxLogit);
            probs[(size_t) i] = e;
            sum += e;
        }
        if (sum <= 0.0f)
        {
            const float uniform = 1.0f / (float) kNumClasses;
            for (auto& v : probs)
                v = uniform;
            return;
        }
        for (auto& v : probs)
            v /= sum;
    }

    DetectionResult DetectionV2::classify(const juce::AudioBuffer<float>& mono,
                                          double sampleRate,
                                          bool isLoop,
                                          const juce::String& originalName) const
    {
        DetectionResult result;
        const bool haveAnyModel = (modelRunner.isAvailable() || yamnetRunner.isAvailable());
        if (!haveAnyModel || mono.getNumChannels() <= 0 || mono.getNumSamples() <= 0 || sampleRate <= 0.0)
        {
            appendDetectionLog("DetectionV2: classify skipped (modelAvailable=" + juce::String((int)haveAnyModel)
                               + ", samples=" + juce::String(mono.getNumSamples())
                               + ", sr=" + juce::String(sampleRate, 1) + ")");
            return result;
        }

        Prediction pred;
        for (auto& v : pred.logits) v = 0.0f;

        // YAMNet + trained MLP head is the primary path.
        // The mel-CNN (InstrumentClassifier.onnx) produces near-uniform ~1/13
        // probabilities due to unresolved mel-spectrogram scaling issues, so
        // using it in a geometric mean actively hurts YAMNet's good predictions.
        // It is kept only as a last-resort fallback when YAMNet is unavailable.
        if (yamnetRunner.isAvailable())
        {
            auto y = yamnetRunner.predict(mono, sampleRate);
            if (y.valid)
                pred.logits = y.logits;
        }
        else if (modelRunner.isAvailable())
        {
            pred = modelRunner.predict(mono, sampleRate, isLoop);
        }

        // Apply soft filename-based priors.
        FilenameBias::apply(originalName, pred.logits);

        // Softmax to probabilities.
        std::array<float, kNumClasses> probs {};
        softmax(pred.logits, probs);

        // Confidence gating (pass isLoop to relax drum threshold for loop files).
        auto gate = ConfidenceGate::apply(probs, isLoop);
        result.hasDecision = gate.accepted;
        result.primary = gate.primary;
        result.top1Prob = gate.top1Prob;
        result.top2Prob = gate.top2Prob;

        appendDetectionLog("DetectionV2: classify name=\"" + originalName
                           + "\" isLoop=" + juce::String((int)isLoop)
                           + " accepted=" + juce::String((int)gate.accepted)
                           + " primary=" + juce::String(classToString(gate.primary))
                           + " top1=" + juce::String(gate.top1Prob, 3)
                           + " top2=" + juce::String(gate.top2Prob, 3));
        return result;
    }

    const char* classToString(Class c)
    {
        switch (c)
        {
            case Class::Kick: return "Kick";
            case Class::Snare: return "Snare";
            case Class::HiHat: return "HiHat";
            case Class::Perc: return "Perc";
            case Class::Bass: return "Bass";
            case Class::Guitar: return "Guitar";
            case Class::Keys: return "Keys";
            case Class::Pad: return "Pad";
            case Class::Lead: return "Lead";
            case Class::Vocal: return "Vocal";
            case Class::FX: return "FX";
            case Class::TextureAtmos: return "TextureAtmos";
            case Class::Other: return "Other";
            case Class::Count: return "?";
        }
        return "?";
    }
} // namespace Detection

