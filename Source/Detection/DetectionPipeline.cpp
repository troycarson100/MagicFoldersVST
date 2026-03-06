#include "DetectionPipeline.h"

#include "DSP/Resampler16kMono.h"
#include "DSP/Windowing.h"
#include "DSP/LoopOneShotDetector.h"
#include "DSP/PercussiveHarmonicGate.h"
#include "Mapping/CategoryMapper.h"
#include "Gating/ConfidenceGate.h"
#include "Yamnet/YamnetModel.h"

namespace DetectionPipeline
{
    using namespace DetectionDSP;
    using namespace DetectionMapping;
    using namespace DetectionGating;
    using namespace DetectionYamnet;

    DetectionResult detectFile(const juce::File& file, const DetectionConfig& config)
    {
        DetectionResult result;

        if (!file.existsAsFile())
        {
            result.debug.gateReason = GateReason::ModelUnavailable;
            return result;
        }

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (!reader)
        {
            result.debug.gateReason = GateReason::ModelUnavailable;
            return result;
        }

        const int64 numSamples64 = reader->lengthInSamples;
        const int numSamples = static_cast<int>(juce::jmin<int64>(numSamples64, std::numeric_limits<int>::max()));
        const int numChannels = static_cast<int>(reader->numChannels);
        const double sampleRate = reader->sampleRate;

        if (numSamples <= 0 || numChannels <= 0 || sampleRate <= 0.0)
        {
            result.debug.gateReason = GateReason::ModelUnavailable;
            return result;
        }

        juce::AudioBuffer<float> buffer(numChannels, numSamples);
        reader->read(&buffer, 0, numSamples, 0, true, true);

        // Downmix + resample to 16k mono for YAMNet.
        std::vector<float> mono16k;
        Resampler16kMono resampler;
        resampler.process(buffer, sampleRate, mono16k);

        // Windowing at 16 kHz.
        auto windows = Windowing::makeWindows(mono16k, config.maxWindows);

        YamnetModel yamnet;
        if (!yamnet.isValid())
        {
            result.debug.gateReason = GateReason::ModelUnavailable;
            return result;
        }

        // Run YAMNet on each window and aggregate scores.
        std::vector<std::vector<float>> windowScores;
        windowScores.reserve(windows.size());
        for (const auto& w : windows)
            windowScores.push_back(yamnet.run(w));

        const auto aggregated = YamnetModel::aggregateScores(windowScores);

        // DSP features and loop/one-shot.
        PercussiveHarmonicFeatures feats = PercussiveHarmonicGate::analyze(buffer, sampleRate);
        LoopDetectionResult loop = LoopOneShotDetector::detect(buffer, sampleRate, config.autoDetectType);

        result.isLoop = loop.isLoop;
        result.loopConfidence = loop.confidence;
        result.debug.percussiveLikelihood = feats.percussiveLikelihood;
        result.debug.harmonicLikelihood = feats.harmonicLikelihood;
        result.debug.lowFreqEnergyRatio = feats.lowFreqEnergyRatio;
        result.debug.highFreqEnergyRatio = feats.highFreqEnergyRatio;

        // Map YAMNet logits (already in instrument-class order) to our categories.
        CategoryMapper mapper;
        auto categoryScores = mapper.map(aggregated, feats);

        // Confidence gating and vote consistency (Unknown when not confident).
        ConfidenceGate gate;
        auto decision = gate.apply(categoryScores, windowScores, config.strictMode);

        result.category = decision.category;
        result.confidence = decision.confidence;
        result.debug.gateReason = static_cast<GateReason>(decision.reason);

        result.debug.top1Label = decision.top1Label;
        result.debug.top1Score = decision.top1Score;
        result.debug.top2Label = decision.top2Label;
        result.debug.top2Score = decision.top2Score;
        result.debug.top3Label = decision.top3Label;
        result.debug.top3Score = decision.top3Score;

        return result;
    }
} // namespace DetectionPipeline

