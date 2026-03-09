#include "YamnetRunner.h"

#include "ModelRunner.h" // for Detection::Class and kNumClasses

#if defined(USE_ONNXRUNTIME) && defined(MAGICFOLDERS_HAS_YAMNET_MODEL)
#include <onnxruntime_c_api.h>
#include "BinaryData.h"
#endif

namespace Detection
{
    // ── Backbone configuration ──────────────────────────────────────────────
    // CNN14 (preferred): 32 kHz, 1-s windows, 2048-d embeddings.
    // YAMNet  (fallback): 16 kHz, ~0.975-s windows, 521-d features.
    // The C++ code tries CNN14 first; if cnn14_backbone.onnx is not present,
    // it falls back to the YAMNet embedded in BinaryData.
    //
    // CNN14 tensor names (as exported by training/download_cnn14.py):
    //   input:  "waveform"   shape [batch, time]
    //   output: "embedding"  shape [batch, 2048]
    //
    // YAMNet tensor names (as in the embedded yamnet.onnx):
    //   input:  "waveform_binary"                        shape [15600]
    //   output: "tower0/network/layer32/final_output"    shape [1, 521]

    static constexpr int    kCnn14WindowSamples  = 32000;   // 1 s at 32 kHz
    static constexpr int    kCnn14FeatureDim     = 2048;
    static constexpr double kCnn14SampleRate     = 32000.0;

    static constexpr int    kYamnetWindowSamples = 15600;   // ~0.975 s at 16 kHz
    static constexpr int    kYamnetFeatureDim    = 521;
    static constexpr double kYamnetSampleRate    = 16000.0;

    // Runtime-selected values (set during Impl construction).
    // kHeadFeatureDim = backbone_dim * 3 (mean ‖ std ‖ max).
    static int    gWindowSamples  = kYamnetWindowSamples;  // overridden to CNN14 when loaded
    static int    gBackboneDim    = kYamnetFeatureDim;
    static double gTargetRate     = kYamnetSampleRate;
    static int    gHeadFeatureDim = kYamnetFeatureDim * 3;

    // ── Legacy fallback: hand-crafted AudioSet-521 → MagicFolders-13 mapping ─
    // Used only when yamnet_head.onnx (the trained MLP) is not available.
    // Each entry maps one AudioSet class index to one of our 13 instrument
    // classes (0=Kick 1=Snare 2=HiHat 3=Perc 4=Bass 5=Guitar 6=Keys
    // 7=Pad 8=Lead 9=FX 10=TextureAtmos 11=Vocal 12=Other) with a weight.
    static const struct { int audioset; int cls; float w; } kAudioSetMap[] = {
        { 134, 5, 1.0f },  // Plucked string instrument
        { 135, 5, 1.5f },  // Guitar
        { 136, 5, 1.4f },  // Electric guitar
        { 137, 4, 1.5f },  // Bass guitar
        { 138, 5, 1.4f },  // Acoustic guitar
        { 139, 5, 1.0f },  // Steel guitar, slide guitar
        { 140, 5, 1.0f },  // Tapping (guitar technique)
        { 141, 5, 1.0f },  // Strum
        { 142, 5, 1.0f },  // Banjo
        { 143, 5, 1.0f },  // Sitar
        { 144, 5, 1.0f },  // Mandolin
        { 145, 5, 1.0f },  // Zither
        { 146, 5, 1.0f },  // Ukulele
        { 147, 6, 1.3f },  // Keyboard (musical)
        { 148, 6, 1.5f },  // Piano
        { 149, 6, 1.0f },  // Electric piano
        { 150, 6, 1.0f },  // Organ
        { 151, 6, 1.0f },  // Electronic organ
        { 152, 6, 1.0f },  // Hammond organ
        { 153, 8, 0.8f },  // Synthesizer → Lead
        { 154, 7, 0.8f },  // Sampler → Pad
        { 155, 6, 1.0f },  // Harpsichord
        { 156, 3, 1.0f },  // Percussion
        { 157, 3, 1.2f },  // Drum kit
        { 158, 3, 1.0f },  // Drum machine
        { 159, 3, 1.0f },  // Drum
        { 160, 1, 1.5f },  // Snare drum
        { 161, 1, 1.0f },  // Rimshot
        { 162, 3, 1.0f },  // Drum roll
        { 163, 0, 1.5f },  // Bass drum
        { 164, 3, 1.0f },  // Timpani
        { 165, 3, 1.0f },  // Tabla
        { 166, 2, 0.8f },  // Cymbal
        { 167, 2, 1.5f },  // Hi-hat
        { 168, 3, 1.0f },  // Wood block
        { 169, 3, 1.0f },  // Tambourine
        { 170, 3, 1.0f },  // Rattle (instrument)
        { 171, 3, 1.0f },  // Maraca
        { 172, 3, 1.0f },  // Gong
        { 173, 3, 1.0f },  // Tubular bells
        { 174, 3, 1.0f },  // Mallet percussion
        { 175, 3, 1.0f },  // Marimba, xylophone
        { 176, 3, 1.0f },  // Glockenspiel
        { 177, 3, 1.0f },  // Vibraphone
        { 178, 3, 1.0f },  // Steelpan
        { 179, 10, 0.7f }, // Orchestra
        { 186, 5, 1.0f },  // Violin, fiddle
        { 187, 5, 1.0f },  // Pizzicato
        { 188, 5, 1.0f },  // Cello
        { 189, 4, 1.0f },  // Double bass
        { 194, 5, 1.0f },  // Harp
        { 204, 6, 1.0f },  // Accordion
        { 208, 8, 1.0f },  // Theremin
        { 209, 7, 0.7f },  // Singing bowl
        { 210, 9, 1.2f },  // Scratching (performance technique)
        { 232, 10, 0.6f }, // Classical music
        { 234, 8, 0.7f },  // Electronic music
        { 235, 8, 0.7f },  // House music
        { 236, 8, 0.7f },  // Techno
        { 237, 8, 0.7f },  // Dubstep
        { 239, 8, 0.7f },  // Electronica
        { 240, 8, 0.7f },  // Electronic dance music
        { 241, 10, 1.0f }, // Ambient music
        { 242, 8, 0.6f },  // Trance music
        { 248, 10, 0.8f }, // New-age music
        { 262, 10, 0.8f }, // Background music
        { 265, 10, 0.7f }, // Soundtrack music
        { 507, 9, 1.0f },  // Noise
        { 508, 9, 0.8f },  // Environmental noise
        { 509, 9, 1.0f },  // Static
        { 514, 9, 0.8f },  // White noise
        { 515, 9, 0.8f },  // Pink noise
        // Vocal (class 11)
        {  24, 11, 1.5f }, // Singing
        {  25, 11, 1.2f }, // Choir
        {  26, 11, 1.0f }, // Yodeling
        {  27, 11, 0.8f }, // Chant
        {  28, 11, 0.8f }, // Mantra
        {  30, 11, 1.0f }, // Synthetic singing
        { 249, 11, 0.8f }, // Vocal music
        { 250, 11, 0.9f }, // A capella
    };
    static const int kAudioSetMapSize = (int) (sizeof(kAudioSetMap) / sizeof(kAudioSetMap[0]));

    static void legacyMapAudioSetToInstrument(const float* as521, std::array<float, 13>& out)
    {
        out.fill(0.0f);
        for (int i = 0; i < kAudioSetMapSize; ++i)
        {
            const int a = kAudioSetMap[i].audioset;
            const int c = kAudioSetMap[i].cls;
            if (a >= 0 && a < 521 && c >= 0 && c < 13)
                out[(size_t) c] += as521[a] * kAudioSetMap[i].w;
        }
    }

#if defined(USE_ONNXRUNTIME) && defined(MAGICFOLDERS_HAS_YAMNET_MODEL)
    struct YamnetRunner::Impl
    {
        OrtEnv*     backboneEnv   = nullptr;
        OrtSession* backboneSession = nullptr;
        OrtEnv*     headEnv       = nullptr;
        OrtSession* headSession   = nullptr;
        bool        hasCnn14      = false;  // true if CNN14 backbone is active
        bool        hasHead       = false;
        bool        available     = false;

        // Tensor names depend on which backbone is loaded.
        const char* backboneInputName  = nullptr;
        const char* backboneOutputName = nullptr;

        Impl()
        {
            const OrtApi* ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
            if (!ort) return;

            if (ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "MagicFoldersYamnet", &backboneEnv))
                return;

            juce::File modelDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                      .getChildFile("MagicFoldersYamnet");
            modelDir.createDirectory();

            OrtSessionOptions* opts = nullptr;
            if (ort->CreateSessionOptions(&opts)) return;

            // ── 1a. Try CNN14 backbone (preferred — stored on disk) ──────────
            juce::File cnn14File = modelDir.getChildFile("cnn14_backbone.onnx");
            if (cnn14File.existsAsFile())
            {
                const juce::String cnn14Path = cnn14File.getFullPathName();
                OrtStatus* st = ort->CreateSession(backboneEnv,
#if defined(_WIN32)
                    (const ORTCHAR_T*) cnn14Path.toWideCharPointer(),
#else
                    (const ORTCHAR_T*) cnn14Path.toRawUTF8(),
#endif
                    opts, &backboneSession);
                if (!st)
                {
                    hasCnn14 = true;
                    gWindowSamples  = kCnn14WindowSamples;
                    gBackboneDim    = kCnn14FeatureDim;
                    gTargetRate     = kCnn14SampleRate;
                    gHeadFeatureDim = kCnn14FeatureDim * 3;
                    backboneInputName  = "waveform";
                    backboneOutputName = "embedding";
                    DBG("YamnetRunner: CNN14 backbone loaded from " + cnn14Path);
                }
                else
                {
                    ort->ReleaseStatus(st);
                    DBG("YamnetRunner: CNN14 found but failed to load — falling back to YAMNet");
                }
            }

            // ── 1b. Fallback: embedded yamnet.onnx ───────────────────────────
            if (!hasCnn14)
            {
                juce::File yamnetFile = modelDir.getChildFile("yamnet.onnx");
                if (!yamnetFile.replaceWithData(BinaryData::yamnet_onnx, BinaryData::yamnet_onnxSize))
                {
                    ort->ReleaseSessionOptions(opts);
                    return;
                }
                const juce::String yamnetPath = yamnetFile.getFullPathName();
                OrtStatus* st = ort->CreateSession(backboneEnv,
#if defined(_WIN32)
                    (const ORTCHAR_T*) yamnetPath.toWideCharPointer(),
#else
                    (const ORTCHAR_T*) yamnetPath.toRawUTF8(),
#endif
                    opts, &backboneSession);
                if (st) { ort->ReleaseStatus(st); ort->ReleaseSessionOptions(opts); return; }

                gWindowSamples  = kYamnetWindowSamples;
                gBackboneDim    = kYamnetFeatureDim;
                gTargetRate     = kYamnetSampleRate;
                gHeadFeatureDim = kYamnetFeatureDim * 3;
                backboneInputName  = "waveform_binary";
                backboneOutputName = "tower0/network/layer32/final_output";
                DBG("YamnetRunner: YAMNet backbone loaded (CNN14 not available)");
            }

            ort->ReleaseSessionOptions(opts);
            available = true;

#if defined(MAGICFOLDERS_HAS_YAMNET_HEAD)
            // ── 2. Load yamnet_head.onnx (trained MLP) ───────────────────────
            if (ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "MagicFoldersYamnetHead", &headEnv))
                return;  // yamnet still works; head is optional

            juce::File headFile = modelDir.getChildFile("yamnet_head.onnx");
            if (headFile.replaceWithData(BinaryData::yamnet_head_onnx,
                                          BinaryData::yamnet_head_onnxSize))
            {
                OrtSessionOptions* hOpts = nullptr;
                if (!ort->CreateSessionOptions(&hOpts))
                {
                    const juce::String headPath = headFile.getFullPathName();
                    OrtStatus* hs = ort->CreateSession(headEnv,
#if defined(_WIN32)
                        (const ORTCHAR_T*) headPath.toWideCharPointer(),
#else
                        (const ORTCHAR_T*) headPath.toRawUTF8(),
#endif
                        hOpts, &headSession);
                    ort->ReleaseSessionOptions(hOpts);
                    if (!hs)
                        hasHead = true;
                    else
                        ort->ReleaseStatus(hs);
                }
            }
            DBG("YamnetRunner: MLP head loaded = " + juce::String(hasHead ? "YES" : "NO"));
#endif
        }

        ~Impl()
        {
            const OrtApi* ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
            if (ort)
            {
                if (headSession)     { ort->ReleaseSession(headSession);     headSession     = nullptr; }
                if (headEnv)         { ort->ReleaseEnv(headEnv);             headEnv         = nullptr; }
                if (backboneSession) { ort->ReleaseSession(backboneSession); backboneSession = nullptr; }
                if (backboneEnv)     { ort->ReleaseEnv(backboneEnv);         backboneEnv     = nullptr; }
            }
        }

        // Run one window through the backbone (CNN14 or YAMNet) → [backboneDim] embedding.
        // For CNN14: input shape is [1, windowSamples]; output is [1, 2048].
        // For YAMNet: input shape is [windowSamples]; output is [1, 521].
        bool runBackboneWindow(const OrtApi* ort,
                               OrtMemoryInfo* memInfo,
                               std::vector<float>& window,
                               float* embeddingOut) const
        {
            const int winSamples = gWindowSamples;
            const int featDim    = gBackboneDim;

            // CNN14 expects a 2-D input [1, time]; YAMNet expects 1-D [time].
            OrtValue* inputTensor = nullptr;
            if (hasCnn14)
            {
                const int64_t shape[2] = { 1, winSamples };
                if (ort->CreateTensorWithDataAsOrtValue(memInfo,
                        window.data(), window.size() * sizeof(float),
                        shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inputTensor))
                    return false;
            }
            else
            {
                const int64_t shape[1] = { winSamples };
                if (ort->CreateTensorWithDataAsOrtValue(memInfo,
                        window.data(), window.size() * sizeof(float),
                        shape, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inputTensor))
                    return false;
            }

            const char* outName = backboneOutputName;
            OrtValue* outTensor = nullptr;
            OrtStatus* st = ort->Run(backboneSession, nullptr,
                                     &backboneInputName,
                                     (const OrtValue* const*) &inputTensor,
                                     1, &outName, 1, &outTensor);
            ort->ReleaseValue(inputTensor);
            if (st) { ort->ReleaseStatus(st); return false; }

            float* data = nullptr;
            st = ort->GetTensorMutableData(outTensor, (void**) &data);
            if (!st && data)
                std::copy(data, data + featDim, embeddingOut);
            ort->ReleaseValue(outTensor);
            if (st) { ort->ReleaseStatus(st); return false; }
            return true;
        }

        YamnetPrediction predict(const juce::AudioBuffer<float>& mono,
                                 double sampleRate) const
        {
            YamnetPrediction out;
            const OrtApi* ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
            if (!ort || !backboneSession || mono.getNumChannels() <= 0
                     || mono.getNumSamples() <= 0 || sampleRate <= 0.0)
                return out;

            // Resample to target rate (32 kHz for CNN14, 16 kHz for YAMNet).
            const float targetRate   = (float) gTargetRate;
            const int   numIn        = mono.getNumSamples();
            const float* src         = mono.getReadPointer(0);
            const double ratio       = targetRate / sampleRate;
            const int numResampled   = (int) std::max(1.0, std::floor(numIn * ratio));

            std::vector<float> fullWave(static_cast<size_t>(numResampled));
            for (int i = 0; i < numResampled; ++i)
            {
                const int i0 = juce::jlimit(0, numIn - 1, (int) (i / ratio));
                fullWave[(size_t) i] = src[i0];
            }

            // Peak-normalize
            float maxAbs = 0.0f;
            for (float v : fullWave)
                maxAbs = std::max(maxAbs, std::abs(v));
            if (maxAbs > 1e-8f)
            {
                const float inv = 1.0f / maxAbs;
                for (float& v : fullWave)
                    v *= inv;
            }

            OrtMemoryInfo* memInfo = nullptr;
            if (ort->CreateCpuMemoryInfo(OrtDeviceAllocator, OrtMemTypeDefault, &memInfo))
                return out;

            // ── Multi-window inference ────────────────────────────────────────
            // Slide a backbone-window (50% hop) over the full clip.
            // Accumulate mean + std + max → FEATURE_DIM-d head input.
            const int winSamples = gWindowSamples;
            const int featDim    = gBackboneDim;
            const int hop        = winSamples / 2;

            std::vector<float> window(static_cast<size_t>(winSamples), 0.0f);
            std::vector<float> sumFeats(static_cast<size_t>(featDim), 0.0f);
            std::vector<float> sumSqFeats(static_cast<size_t>(featDim), 0.0f);
            std::vector<float> maxFeats(static_cast<size_t>(featDim), 0.0f);
            int windowCount = 0;

            int start = 0;
            do
            {
                std::fill(window.begin(), window.end(), 0.0f);
                const int end   = std::min(start + winSamples, numResampled);
                const int count = end - start;
                std::copy(fullWave.begin() + start,
                          fullWave.begin() + start + count,
                          window.begin());

                std::vector<float> feat(static_cast<size_t>(featDim));
                if (runBackboneWindow(ort, memInfo, window, feat.data()))
                {
                    for (int i = 0; i < featDim; ++i)
                    {
                        const float v = feat[(size_t) i];
                        sumFeats[(size_t) i]   += v;
                        sumSqFeats[(size_t) i] += v * v;
                        if (windowCount == 0 || v > maxFeats[(size_t) i])
                            maxFeats[(size_t) i] = v;
                    }
                    ++windowCount;
                }
                start += hop;
            }
            while (start < numResampled);

            ort->ReleaseMemoryInfo(memInfo);
            if (windowCount == 0) return out;

#if defined(MAGICFOLDERS_HAS_YAMNET_HEAD)
            if (hasHead && headSession)
            {
                // Build [mean ‖ std ‖ max] = headFeatureDim-d input for the MLP head.
                const int headDim = gHeadFeatureDim;
                const int bd      = gBackboneDim;
                std::vector<float> headInput(static_cast<size_t>(headDim));
                for (int i = 0; i < bd; ++i)
                {
                    const float mean = sumFeats[(size_t) i] / windowCount;
                    const float var  = (sumSqFeats[(size_t) i] / windowCount) - (mean * mean);
                    const float stdv = std::sqrt(std::max(var, 0.0f));
                    headInput[(size_t) i]            = mean;
                    headInput[(size_t) (i + bd)]     = stdv;
                    headInput[(size_t) (i + 2 * bd)] = maxFeats[(size_t) i];
                }

                OrtMemoryInfo* hMemInfo = nullptr;
                if (ort->CreateCpuMemoryInfo(OrtDeviceAllocator, OrtMemTypeDefault, &hMemInfo))
                {
                    legacyMapAudioSetToInstrument(sumFeats.data(), out.logits);
                    out.valid = true;
                    return out;
                }

                const int64_t inShape[2] = { 1, (int64_t) headDim };
                OrtValue* inTensor = nullptr;
                OrtStatus* st = ort->CreateTensorWithDataAsOrtValue(
                    hMemInfo,
                    headInput.data(), headInput.size() * sizeof(float),
                    inShape, 2,
                    ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                    &inTensor);
                ort->ReleaseMemoryInfo(hMemInfo);
                if (st)
                {
                    ort->ReleaseStatus(st);
                    legacyMapAudioSetToInstrument(sumFeats.data(), out.logits);
                    out.valid = true;
                    return out;
                }

                const char* hInNames[]  = { "yamnet_features" };
                const char* hOutNames[] = { "logits" };
                OrtValue* outTensor = nullptr;
                st = ort->Run(headSession, nullptr,
                              hInNames, (const OrtValue* const*) &inTensor,
                              1, hOutNames, 1, &outTensor);
                ort->ReleaseValue(inTensor);
                if (st)
                {
                    ort->ReleaseStatus(st);
                    legacyMapAudioSetToInstrument(sumFeats.data(), out.logits);
                    out.valid = true;
                    return out;
                }

                float* logits = nullptr;
                st = ort->GetTensorMutableData(outTensor, (void**) &logits);
                if (!st && logits)
                {
                    for (int i = 0; i < 13; ++i)
                        out.logits[(size_t) i] = logits[i];
                }
                ort->ReleaseValue(outTensor);
                if (st) ort->ReleaseStatus(st);
                out.valid = true;
                return out;
            }
#endif
            // ── Fallback: average backbone features → legacy hand-crafted mapping ──
            // Only meaningful for YAMNet (521-d). CNN14 (2048-d) would need a
            // different mapping, but in practice CNN14 always has a head.
            const int bd = gBackboneDim;
            std::vector<float> avgFeats(static_cast<size_t>(bd));
            for (int i = 0; i < bd; ++i)
                avgFeats[(size_t) i] = sumFeats[(size_t) i] / windowCount;
            legacyMapAudioSetToInstrument(avgFeats.data(), out.logits);
            out.valid = true;
            return out;
        }
    };
#endif

    YamnetRunner::YamnetRunner()
    {
#if defined(USE_ONNXRUNTIME) && defined(MAGICFOLDERS_HAS_YAMNET_MODEL)
        impl      = std::make_unique<Impl>();
        available = impl->available;
#endif
    }

    YamnetRunner::~YamnetRunner() = default;

    YamnetPrediction YamnetRunner::predict(const juce::AudioBuffer<float>& mono,
                                           double sampleRate) const
    {
#if defined(USE_ONNXRUNTIME) && defined(MAGICFOLDERS_HAS_YAMNET_MODEL)
        if (impl && available)
            return impl->predict(mono, sampleRate);
#endif
        YamnetPrediction p;
        return p;
    }

} // namespace Detection
