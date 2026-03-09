#include "DetectionPipeline.h"

#include "DSP/Resampler16kMono.h"
#include "DSP/LoopOneShotDetector.h"
#include "DSP/PercussiveHarmonicGate.h"
#include "Mapping/CategoryMapper.h"
#include "Gating/ConfidenceGate.h"
#include "YamnetRunner.h"

namespace DetectionPipeline
{
    using namespace DetectionDSP;
    using namespace DetectionMapping;
    using namespace DetectionGating;

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

        // Run YamnetRunner on the full resampled buffer in one shot.
        // This produces the correct mean‖std‖max 1563-d features the head was
        // trained on. The old per-window approach double-windowed the audio
        // (pipeline pre-cut + YamnetRunner internal loop), collapsing std to ~0.
        Detection::YamnetRunner yamnetRunner;
        if (!yamnetRunner.isAvailable())
        {
            result.debug.gateReason = GateReason::ModelUnavailable;
            return result;
        }

        juce::AudioBuffer<float> mono16kBuf(1, (int) mono16k.size());
        for (int i = 0; i < (int) mono16k.size(); ++i)
            mono16kBuf.setSample(0, i, mono16k[(size_t) i]);

        const Detection::YamnetPrediction pred = yamnetRunner.predict(mono16kBuf, 16000.0);
        if (!pred.valid)
        {
            result.debug.gateReason = GateReason::ModelUnavailable;
            return result;
        }

        std::vector<float> aggregated(pred.logits.begin(), pred.logits.end());

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

        // Confidence gating (Unknown when not confident).
        ConfidenceGate gate;
        auto decision = gate.apply(categoryScores, {}, config.strictMode);

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

