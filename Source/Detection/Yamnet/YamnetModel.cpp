#include "YamnetModel.h"

#include "../YamnetRunner.h"

namespace DetectionYamnet
{
    using Detection::YamnetRunner;
    using Detection::YamnetPrediction;

    namespace
    {
        YamnetRunner& getSharedRunner()
        {
            static YamnetRunner runner;
            return runner;
        }
    }

    YamnetModel::YamnetModel()
    {
        available = getSharedRunner().isAvailable();
    }

    bool YamnetModel::isValid() const noexcept
    {
        return available;
    }

    std::vector<float> YamnetModel::run(const std::vector<float>& mono16kWindow) const
    {
        std::vector<float> scores;
        if (!available || mono16kWindow.empty())
            return scores;

        const int numSamples = (int) mono16kWindow.size();
        juce::AudioBuffer<float> buffer(1, numSamples);
        buffer.clear();
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample(0, i, mono16kWindow[(size_t) i]);

        YamnetPrediction pred = getSharedRunner().predict(buffer, 16000.0);
        scores.assign(pred.logits.begin(), pred.logits.end());
        return scores;
    }

    std::vector<float> YamnetModel::aggregateScores(const std::vector<std::vector<float>>& windowScores)
    {
        if (windowScores.empty())
            return {};

        const size_t numClasses = windowScores.front().size();
        if (numClasses == 0)
            return {};

        const size_t numWindows = windowScores.size();
        std::vector<float> mean(numClasses, 0.0f);
        std::vector<float> maxVals(numClasses, -std::numeric_limits<float>::infinity());

        for (const auto& ws : windowScores)
        {
            if (ws.size() != numClasses)
                continue;
            for (size_t i = 0; i < numClasses; ++i)
            {
                mean[i] += ws[i];
                maxVals[i] = std::max(maxVals[i], ws[i]);
            }
        }

        for (size_t i = 0; i < numClasses; ++i)
            mean[i] /= (float) numWindows;

        std::vector<float> agg(numClasses, 0.0f);
        for (size_t i = 0; i < numClasses; ++i)
            agg[i] = 0.7f * mean[i] + 0.3f * maxVals[i];

        return agg;
    }
} // namespace DetectionYamnet

