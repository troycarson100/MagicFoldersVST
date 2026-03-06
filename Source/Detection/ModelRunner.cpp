#include "ModelRunner.h"
#include "DetectionConstants.h"

#if defined(USE_ONNXRUNTIME) && defined(MAGICFOLDERS_HAS_EMBEDDED_MODEL)
#include "BinaryData.h"
#endif

namespace Detection
{
    ModelRunner::ModelRunner()
    {
        auto appendDetectionLog = [] (const juce::String& line)
        {
            juce::File dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                 .getChildFile("MagicFoldersLogs");
            dir.createDirectory();
            juce::File logFile = dir.getChildFile("detection.log");
            logFile.appendText(line + "\n");
        };

#if defined(USE_ONNXRUNTIME) && defined(MAGICFOLDERS_HAS_EMBEDDED_MODEL)
        melBuffer_.resize(static_cast<size_t>(kModelNMelBins * kModelNFrames));

        const OrtApi* ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
        if (!ort)
        {
            const juce::String msg = "ModelRunner: ONNX Runtime API not found";
            DBG("MagicFolders detection: " + msg);
            appendDetectionLog(msg);
            return;
        }

        OrtStatus* status = ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "MagicFolders", &onnxEnv_);
        if (status)
        {
            const char* err = ort->GetErrorMessage(status);
            juce::String msg = "ModelRunner: ONNX CreateEnv failed";
            if (err != nullptr)
                msg << " - " << err;
            ort->ReleaseStatus(status);
            DBG("MagicFolders detection: " + msg);
            appendDetectionLog(msg);
            return;
        }

        // Write model (and optional external data) to a private on-disk location
        // so ONNX Runtime can resolve InstrumentClassifier.onnx.data correctly.
        juce::File modelDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                  .getChildFile("MagicFoldersModel");
        modelDir.createDirectory();
        juce::File modelFile = modelDir.getChildFile("InstrumentClassifier.onnx");
        if (!modelFile.replaceWithData(BinaryData::InstrumentClassifier_onnx,
                                       BinaryData::InstrumentClassifier_onnxSize))
        {
            juce::String msg = "ModelRunner: failed to write InstrumentClassifier.onnx to "
                               + modelFile.getFullPathName();
            DBG("MagicFolders detection: " + msg);
            appendDetectionLog(msg);
            return;
        }

#if defined(MAGICFOLDERS_HAS_EMBEDDED_MODEL_DATA)
        juce::File dataFile = modelDir.getChildFile("InstrumentClassifier.onnx.data");
        if (!dataFile.replaceWithData(BinaryData::InstrumentClassifier_onnx_data,
                                      BinaryData::InstrumentClassifier_onnx_dataSize))
        {
            juce::String msg = "ModelRunner: failed to write InstrumentClassifier.onnx.data to "
                               + dataFile.getFullPathName();
            DBG("MagicFolders detection: " + msg);
            appendDetectionLog(msg);
            return;
        }
#endif

        OrtSessionOptions* options = nullptr;
        status = ort->CreateSessionOptions(&options);
        if (status)
        {
            const char* err = ort->GetErrorMessage(status);
            juce::String msg = "ModelRunner: ONNX CreateSessionOptions failed";
            if (err != nullptr)
                msg << " - " << err;
            ort->ReleaseStatus(status);
            ort->ReleaseEnv(onnxEnv_);
            onnxEnv_ = nullptr;
            DBG("MagicFolders detection: " + msg);
            appendDetectionLog(msg);
            return;
        }

        const juce::String modelPath = modelFile.getFullPathName();
        status = ort->CreateSession(onnxEnv_,
#if defined(_WIN32)
                                    (const ORTCHAR_T*) modelPath.toWideCharPointer(),
#else
                                    (const ORTCHAR_T*) modelPath.toRawUTF8(),
#endif
                                    options,
                                    &onnxSession_);
        ort->ReleaseSessionOptions(options);
        if (status)
        {
            const char* err = ort->GetErrorMessage(status);
            juce::String msg = "ModelRunner: ONNX CreateSession failed";
            if (err != nullptr)
                msg << " - " << err;
            ort->ReleaseStatus(status);
            ort->ReleaseEnv(onnxEnv_);
            onnxEnv_ = nullptr;
            DBG("MagicFolders detection: " + msg);
            appendDetectionLog(msg);
            return;
        }

        available = true;
        {
            juce::String msg = "ModelRunner: ONNX model loaded successfully from " + modelPath
                + " (embedded onnx size: " + juce::String(BinaryData::InstrumentClassifier_onnxSize) + " bytes)";
#if defined(MAGICFOLDERS_HAS_EMBEDDED_MODEL_DATA)
            msg += " (embedded data size: " + juce::String(BinaryData::InstrumentClassifier_onnx_dataSize) + " bytes)";
#endif
            DBG("MagicFolders detection: " + msg);
            appendDetectionLog(msg);
        }
#else
        DBG("MagicFolders detection: ONNX not built in (USE_ONNXRUNTIME and/or MAGICFOLDERS_HAS_EMBEDDED_MODEL not set)");
        appendDetectionLog("ModelRunner: ONNX not built in (no runtime and/or embedded model)");
#endif
    }

    ModelRunner::~ModelRunner()
    {
#if defined(USE_ONNXRUNTIME) && defined(MAGICFOLDERS_HAS_EMBEDDED_MODEL)
        const OrtApi* ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
        if (ort)
        {
            if (onnxSession_) { ort->ReleaseSession(onnxSession_); onnxSession_ = nullptr; }
            if (onnxEnv_) { ort->ReleaseEnv(onnxEnv_); onnxEnv_ = nullptr; }
        }
#endif
    }

    Prediction ModelRunner::predict(const juce::AudioBuffer<float>& mono,
                                    double sampleRate,
                                    bool) const
    {
        Prediction p;
        for (auto& v : p.logits) v = 0.0f;

#if defined(USE_ONNXRUNTIME) && defined(MAGICFOLDERS_HAS_EMBEDDED_MODEL)
        if (!available || onnxSession_ == nullptr || mono.getNumChannels() <= 0 || mono.getNumSamples() <= 0)
            return p;

        const float* audio = mono.getReadPointer(0);
        const int numSamples = mono.getNumSamples();
        mel_.compute(audio, numSamples, sampleRate, melBuffer_.data());

        const OrtApi* ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
        if (!ort) return p;

        const int64_t melShape[4] = { 1, 1, kModelNMelBins, kModelNFrames };
        const size_t melNumElements = static_cast<size_t>(kModelNMelBins * kModelNFrames);

        OrtMemoryInfo* memInfo = nullptr;
        OrtStatus* status = ort->CreateCpuMemoryInfo(OrtDeviceAllocator, OrtMemTypeDefault, &memInfo);
        if (status) { ort->ReleaseStatus(status); return p; }

        OrtValue* inputTensor = nullptr;
        status = ort->CreateTensorWithDataAsOrtValue(memInfo, melBuffer_.data(), melNumElements * sizeof(float),
                                                     melShape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inputTensor);
        ort->ReleaseMemoryInfo(memInfo);
        if (status) { ort->ReleaseStatus(status); return p; }

        const char* inputNames[] = { "mel" };
        const char* outputNames[] = { "logits" };
        OrtValue* outputTensor = nullptr;
        status = ort->Run(onnxSession_, nullptr, inputNames, (const OrtValue* const*)&inputTensor, 1,
                          outputNames, 1, &outputTensor);
        ort->ReleaseValue(inputTensor);
        if (status) { ort->ReleaseStatus(status); return p; }

        float* logitsData = nullptr;
        status = ort->GetTensorMutableData(outputTensor, (void**)&logitsData);
        if (status) { ort->ReleaseStatus(status); ort->ReleaseValue(outputTensor); return p; }

        for (int i = 0; i < kNumClasses; ++i)
            p.logits[static_cast<size_t>(i)] = logitsData[i];

        ort->ReleaseValue(outputTensor);
#endif
        return p;
    }
}
