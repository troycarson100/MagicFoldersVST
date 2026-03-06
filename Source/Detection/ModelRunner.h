#pragma once

#include <JuceHeader.h>
#include <array>
#if defined(USE_ONNXRUNTIME) && defined(MAGICFOLDERS_HAS_EMBEDDED_MODEL)
#include <onnxruntime_c_api.h>
#include "MelSpectrogram.h"
#include <vector>
#endif

namespace Detection
{
    // Canonical target classes for v2 detection / naming.
    enum class Class : uint8_t
    {
        Kick = 0,
        Snare,
        HiHat,
        Perc,
        Bass,
        Guitar,
        Keys,
        Pad,
        Lead,
        FX,
        TextureAtmos,
        Vocal,
        Other,
        Count
    };

    static constexpr int kNumClasses = static_cast<int>(Class::Count);

    struct Prediction
    {
        // Raw, un-normalised logits for each Class (softmax is applied later).
        std::array<float, kNumClasses> logits { {} };
    };

    /** Runs the instrument classifier (ONNX when built with model + ONNX Runtime). */
    class ModelRunner
    {
    public:
        ModelRunner();
        ~ModelRunner();

        ModelRunner(const ModelRunner&) = delete;
        ModelRunner& operator=(const ModelRunner&) = delete;
        ModelRunner(ModelRunner&&) noexcept = default;
        ModelRunner& operator=(ModelRunner&&) noexcept = default;

        /** True when a compiled-in model is available and ready. */
        bool isAvailable() const noexcept { return available; }

        /** Run the model on a mono buffer at the given sample rate.
            The buffer is assumed to be analysis-rate audio (not on the audio thread).
        */
        Prediction predict(const juce::AudioBuffer<float>& mono,
                           double sampleRate,
                           bool isLoop) const;

    private:
        bool available = false;
#if defined(USE_ONNXRUNTIME) && defined(MAGICFOLDERS_HAS_EMBEDDED_MODEL)
        MelSpectrogram mel_;
        mutable std::vector<float> melBuffer_;
        OrtEnv* onnxEnv_ = nullptr;
        OrtSession* onnxSession_ = nullptr;
#endif
    };
} // namespace Detection

