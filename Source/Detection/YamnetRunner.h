#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>

namespace Detection
{
    /** Output of the Yamnet backend after mapping to Magic Folders classes. */
    struct YamnetPrediction
    {
        // 13 canonical logits in the same order as Detection::Class in ModelRunner.h
        std::array<float, 13> logits { {} };
        bool valid = false;
    };

    /** Lightweight ONNX Runtime wrapper for the YAMNet pipeline.
        When yamnet_head.onnx (trained MLP classifier) is available, the
        inference runs as a two-stage pipeline:
            1. yamnet.onnx  : waveform [15600]  →  AudioSet probabilities [1,521]
            2. yamnet_head.onnx : features [1,521]  →  instrument logits [1,13]
        When only yamnet.onnx is present, a legacy hand-crafted mapping is used
        as a fallback.
    */
    class YamnetRunner
    {
    public:
        YamnetRunner();
        ~YamnetRunner();

        YamnetRunner(const YamnetRunner&) = delete;
        YamnetRunner& operator=(const YamnetRunner&) = delete;

        bool isAvailable() const noexcept { return available; }

        /** Run YAMNet + MLP head on a mono buffer and return 13-class logits.
            - mono       : single-channel analysis buffer
            - sampleRate : buffer sample rate
        */
        YamnetPrediction predict(const juce::AudioBuffer<float>& mono,
                                 double sampleRate) const;

    private:
        bool available = false;

#if defined(USE_ONNXRUNTIME) && defined(MAGICFOLDERS_HAS_YAMNET_MODEL)
        struct Impl;
        std::unique_ptr<Impl> impl;
#endif
    };
} // namespace Detection
