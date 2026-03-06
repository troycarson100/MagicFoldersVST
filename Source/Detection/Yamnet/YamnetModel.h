#pragma once

#include <vector>

namespace DetectionYamnet
{
    /** Simple wrapper that presents a YAMNet-like interface to the detection
        pipeline. Internally this can be backed by the existing ONNX-based
        YamnetRunner so that inference remains fully on-device.
    */
    class YamnetModel
    {
    public:
        YamnetModel();

        bool isValid() const noexcept;

        /** Run the model on a mono 16kHz window of exactly 15600 samples. */
        std::vector<float> run(const std::vector<float>& mono16kWindow) const;

        /** Aggregate per-window scores with 0.7 * mean + 0.3 * max. */
        static std::vector<float> aggregateScores(const std::vector<std::vector<float>>& windowScores);

    private:
        bool available = false;
    };
} // namespace DetectionYamnet

