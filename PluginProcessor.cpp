#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Source/Detection/HeuristicConstants.h"
#include "Source/Detection/HeuristicCategory.h"
#include "Source/Detection/DetectionPipeline.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <essentia/essentia.h>
#include <essentia/algorithmfactory.h>
#include <essentia/essentiamath.h>
#include <essentia/pool.h>

using namespace essentia;
using namespace essentia::standard;

// ─── Waveform duplicate detection ────────────────────────────────────────────
namespace
{
    static constexpr int kFingerprintBins = 64;  // envelope resolution
    static constexpr float kDupSimilarityThreshold = 0.992f;  // cosine sim
    static constexpr double kDupDurationTolerance  = 0.03;    // ±3% duration

    struct WaveformFingerprint
    {
        float    bins[kFingerprintBins] = {};  // normalised RMS envelope
        double   durationSeconds = 0.0;
        uint64_t quickHash = 0;  // fast PCM hash for exact-match short-circuit
        juce::String filePath;
    };

    /** Compute a compact waveform fingerprint for duplicate detection.
     *  Returns an empty fingerprint (durationSeconds == 0) on read failure. */
    WaveformFingerprint computeFingerprint(const juce::File& file)
    {
        WaveformFingerprint fp;
        fp.filePath = file.getFullPathName();

        juce::AudioFormatManager fmt;
        fmt.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(file));
        if (!reader || reader->lengthInSamples <= 0) return fp;

        const int totalSamples = (int)juce::jmin(reader->lengthInSamples, (juce::int64)10'000'000);
        const int numChannels  = (int)reader->numChannels;
        fp.durationSeconds     = (double)reader->lengthInSamples / reader->sampleRate;

        // Read all samples into a mono buffer (mix down channels).
        // Limit to first 10 M samples (~3–4 min at 44 kHz) to keep memory sane.
        juce::AudioBuffer<float> mono(1, totalSamples);
        {
            juce::AudioBuffer<float> raw(numChannels, totalSamples);
            reader->read(&raw, 0, totalSamples, 0, true, true);
            mono.clear();
            float* dst = mono.getWritePointer(0);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* src = raw.getReadPointer(ch);
                for (int s = 0; s < totalSamples; ++s)
                    dst[s] += src[s];
            }
            if (numChannels > 1)
                for (int s = 0; s < totalSamples; ++s)
                    dst[s] /= (float)numChannels;
        }

        // Quick 64-bit FNV-1a hash of quantised PCM — fast exact-match shortcut.
        // 8-bit quantisation tolerates trivial float rounding while staying stable.
        {
            const float* src = mono.getReadPointer(0);
            uint64_t h = 14695981039346656037ULL;  // FNV offset basis
            for (int s = 0; s < totalSamples; ++s)
            {
                uint8_t b = (uint8_t)juce::jlimit(0, 255, (int)((src[s] * 0.5f + 0.5f) * 255.0f));
                h ^= b;
                h *= 1099511628211ULL;  // FNV prime
            }
            fp.quickHash = h;
        }

        // Compute RMS envelope: divide into kFingerprintBins segments.
        const int binSize = juce::jmax(1, totalSamples / kFingerprintBins);
        const float* src  = mono.getReadPointer(0);
        float peak = 0.0f;
        for (int b = 0; b < kFingerprintBins; ++b)
        {
            int start = b * binSize;
            int end   = juce::jmin(start + binSize, totalSamples);
            double sum = 0.0;
            for (int s = start; s < end; ++s)
                sum += (double)src[s] * src[s];
            float rms = (float)std::sqrt(sum / juce::jmax(1, end - start));
            fp.bins[b] = rms;
            peak = juce::jmax(peak, rms);
        }
        // Normalise so the peak bin == 1.0 (makes comparison amplitude-invariant).
        if (peak > 1e-6f)
            for (int b = 0; b < kFingerprintBins; ++b)
                fp.bins[b] /= peak;

        return fp;
    }

    /** Cosine similarity in [0, 1] between two fingerprint bin arrays. */
    float fingerprintSimilarity(const WaveformFingerprint& a, const WaveformFingerprint& b)
    {
        double dot = 0.0, normA = 0.0, normB = 0.0;
        for (int i = 0; i < kFingerprintBins; ++i)
        {
            dot   += (double)a.bins[i] * b.bins[i];
            normA += (double)a.bins[i] * a.bins[i];
            normB += (double)b.bins[i] * b.bins[i];
        }
        double denom = std::sqrt(normA) * std::sqrt(normB);
        return (denom < 1e-9) ? 0.0f : (float)(dot / denom);
    }

    /** Returns true if fp matches any fingerprint in the seen list. */
    bool isDuplicate(const WaveformFingerprint& fp,
                     const std::vector<WaveformFingerprint>& seen)
    {
        if (fp.durationSeconds <= 0.0) return false;
        for (const auto& s : seen)
        {
            // Fast path: identical PCM hash → exact duplicate.
            if (fp.quickHash != 0 && fp.quickHash == s.quickHash)
                return true;
            // Duration gate: if lengths differ by >3 % it's a different sample.
            double durationRatio = std::abs(fp.durationSeconds - s.durationSeconds)
                                   / juce::jmax(fp.durationSeconds, s.durationSeconds);
            if (durationRatio > kDupDurationTolerance)
                continue;
            // Slow path: compare waveform shape.
            if (fingerprintSimilarity(fp, s) >= kDupSimilarityThreshold)
                return true;
        }
        return false;
    }
} // namespace
// ─── End duplicate detection ─────────────────────────────────────────────────

MagicFoldersProcessor::MagicFoldersProcessor()
    : previewTransport()
{
    setPlayConfigDetails(0, 2, 44100.0, 512);
    // Run the read-ahead thread at high priority so the buffer stays full even at
    // small host buffer sizes. Normal priority caused read starvation in Ableton.
    previewReadAheadThread.startThread(juce::Thread::Priority::high);
    essentia::init();
}

MagicFoldersProcessor::~MagicFoldersProcessor()
{
    stopPreview();
    previewReadAheadThread.stopThread(2000);
    essentia::shutdown();
}

void MagicFoldersProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    previewSampleRate = sampleRate;
    previewBlockSize = samplesPerBlock;
    previewTransport.prepareToPlay(samplesPerBlock, sampleRate);
}

void MagicFoldersProcessor::releaseResources()
{
    previewTransport.releaseResources();
}

void MagicFoldersProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Only replace the buffer when we are actively previewing a sample.
    // Otherwise pass the host track audio through unchanged so the plugin
    // does not silence the channel when it is inserted on a track.
    // Note: isPlaying() is a thread-safe atomic check; no need to also check
    // previewReaderSource (which is owned by the message thread).
    if (previewTransport.isPlaying())
    {
        buffer.clear();
        juce::AudioSourceChannelInfo info(buffer);
        previewTransport.getNextAudioBlock(info);
    }
}

void MagicFoldersProcessor::setPreviewSource(std::unique_ptr<juce::AudioFormatReaderSource> source, double fileSampleRate, double lengthInSeconds)
{
    // Keep the old reader alive until AFTER the transport has switched to the
    // new source. AudioTransportSource::setSource() atomically swaps sources
    // under its internal lock in a single acquisition — replacing old with new
    // in one step. Resetting oldSource afterwards is safe because the transport
    // has already released its raw pointer to it.
    auto oldSource = std::move(previewReaderSource);
    previewReaderSource = std::move(source);
    previewLengthSeconds = lengthInSeconds;

    if (previewReaderSource)
    {
        // 131072 samples ≈ ~3 s at 44.1 kHz. Generous buffer so the read-ahead
        // thread keeps the audio callback fed even at small host buffer sizes.
        // 131072 samples ≈ ~3 s at 44.1 kHz. Generous buffer so the read-ahead
        // thread keeps the audio callback fed even at small host buffer sizes.
        const int readAheadSize = 131072;
        // AudioTransportSource::setSource() already calls prepareToPlay()
        // internally when the transport has been prepared, so an explicit
        // prepareToPlay() here is redundant and wastes an audio-lock acquisition.
        previewTransport.setSource(previewReaderSource.get(), readAheadSize, &previewReadAheadThread, fileSampleRate);
    }
    else
    {
        previewTransport.setSource(nullptr);
    }
    // oldSource destroyed here — safe because transport already switched away
}

void MagicFoldersProcessor::startPreview()
{
    if (previewReaderSource)
    {
        previewTransport.setPosition(0.0);
        previewTransport.start();
    }
}

void MagicFoldersProcessor::stopPreview()
{
    // setSource(nullptr) internally sets playing = false (under the callback
    // lock), so the explicit stop() call beforehand is redundant — removing it
    // saves one extra lock acquisition on the message thread.
    previewTransport.setSource(nullptr);
    previewReaderSource.reset();
    previewLengthSeconds = 0.0;
}

juce::AudioProcessorEditor* MagicFoldersProcessor::createEditor()
{
    return new MagicFoldersEditor(*this);
}

void MagicFoldersProcessor::setOutputDirectory(const juce::File& dir)
{
    outputDirectory = dir;
}

void MagicFoldersProcessor::setBatchPlusFolder(const juce::File& dir)
{
    batchPlusFolder = dir;
}

void MagicFoldersProcessor::tryAutoDetectAbletonSamplesFolder()
{
    if (batchPlusFolder.isDirectory())
        return;
    // Only search Documents to avoid triggering Apple Music permission (userMusicDirectory)
    // and to avoid blocking on a huge Music library. User can set Music-based projects in Settings.
    juce::File docsDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    if (!docsDir.isDirectory())
        return;
    juce::File bestSamples;
    int64 bestTime = 0;
    const int maxAls = 30;
    juce::Array<juce::File> alsFiles;
    docsDir.findChildFiles(alsFiles, juce::File::findFiles, true, "*.als");
    int alsCount = 0;
    for (const juce::File& als : alsFiles)
        {
            if (alsCount++ >= maxAls)
                break;
            juce::File projectDir = als.getParentDirectory();
            for (const juce::String& subName : { "Samples", "Recorded" })
            {
                juce::File candidate = projectDir.getChildFile(subName);
                if (candidate.isDirectory())
                {
                    int64 modTime = candidate.getLastModificationTime().toMilliseconds();
                    if (modTime > bestTime)
                    {
                        bestTime = modTime;
                        bestSamples = candidate;
                    }
                    break;
                }
            }
        if (alsCount >= maxAls)
            break;
    }
    if (bestSamples.isDirectory())
        batchPlusFolder = bestSamples;
}

void MagicFoldersProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream os(destData, true);
    os.writeString(outputDirectory.getFullPathName());
    os.writeString(batchPlusFolder.getFullPathName());
    os.writeString(generateFunNames ? "1" : "0");
    os.writeString(useAccurateDetection ? "1" : "0");
}

void MagicFoldersProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;
    juce::MemoryInputStream is(data, static_cast<size_t>(sizeInBytes), false);
    juce::String path = is.readString();
    if (path.isNotEmpty())
    {
        juce::File dir(path);
        if (dir.exists())
            outputDirectory = dir;
    }
    if (is.getNumBytesRemaining() > 0)
    {
        path = is.readString();
        if (path.isNotEmpty())
        {
            juce::File dir(path);
            if (dir.isDirectory())
                batchPlusFolder = dir;
        }
    }
    if (is.getNumBytesRemaining() > 0)
    {
        path = is.readString();
        generateFunNames = (path == "1");
    }
    if (is.getNumBytesRemaining() > 0)
    {
        path = is.readString();
        useAccurateDetection = (path == "1");
    }
}

void MagicFoldersProcessor::addFiles(const juce::Array<juce::File>& files)
{
    for (auto& f : files)
    {
        juce::String ext = f.getFileExtension().toLowerCase().trimCharactersAtStart(".");
        bool isAudio = (ext == "wav" || ext == "aif" || ext == "aiff");
        if (f.existsAsFile() && isAudio)
        {
            juce::String path = f.getFullPathName();
            bool alreadyInQueue = false;
            for (const auto& q : queue)
                if (q.sourceFile.getFullPathName() == path) { alreadyInQueue = true; break; }
            if (alreadyInQueue) continue;
            SampleInfo info;
            info.sourceFile = f;
            info.name = f.getFileNameWithoutExtension();
            info.category = "Other";
            info.type = "One-Shot";
            info.key = projectKey;
            info.bpm = projectBPM;
            info.genre = defaultGenre;
            queue.add(info);
        }
    }
}

void MagicFoldersProcessor::addFilesFromFolder(const juce::File& directory)
{
    if (!directory.isDirectory())
        return;
    juce::Array<juce::File> files;
    directory.findChildFiles(files, juce::File::findFiles, false);
    juce::Array<juce::File> audioFiles;
    for (auto& f : files)
    {
        juce::String ext = f.getFileExtension().toLowerCase().trimCharactersAtStart(".");
        if (ext == "wav" || ext == "aif" || ext == "aiff")
            audioFiles.add(f);
    }
    addFiles(audioFiles);
}

void MagicFoldersProcessor::addFilesFromFolderRecursive(const juce::File& directory)
{
    if (!directory.isDirectory())
        return;
    juce::Array<juce::File> files;
    directory.findChildFiles(files, juce::File::findFiles, true);
    juce::Array<juce::File> audioFiles;
    for (auto& f : files)
    {
        juce::String ext = f.getFileExtension().toLowerCase().trimCharactersAtStart(".");
        if (ext == "wav" || ext == "aif" || ext == "aiff")
            audioFiles.add(f);
    }
    addFiles(audioFiles);
}

juce::String MagicFoldersProcessor::detectCategory(const juce::String& fn)
{
    if (fn.contains("kick") || fn.contains("bd") || fn.contains("bass drum")) return "Kicks";
    if (fn.contains("snare") || fn.contains("snr") || fn.contains("clap")) return "Snares";
    if (fn.contains("hihat") || fn.contains("hi-hat") || fn.contains("hh") || fn.contains("cymbal")) return "Hi-Hats";
    if (fn.contains("perc") || fn.contains("rim") || fn.contains("tom")) return "Percussion";
    if (fn.contains("bass") || fn.contains("sub")) return "Bass";
    if (fn.contains("loop")) return "Loops";
    if (fn.contains("fx") || fn.contains("riser") || fn.contains("sweep") || fn.contains("impact")) return "FX";
    if (fn.contains("vocal") || fn.contains("vox") || fn.contains("chop")) return "Vocals";
    if (fn.contains("melody") || fn.contains("lead") || fn.contains("synth")) return "Melodic";
    return "Other";
}

juce::String MagicFoldersProcessor::detectType(const juce::String& fn)
{
    if (fn.contains("loop") || fn.contains("lp_") || fn.contains("_lp")) return "Loop";
    return "One-Shot";
}

namespace
{
    enum class CoarseType
    {
        Drum,
        Tonal,
        NoiseFx,
        Unknown
    };

    static bool isDrumCategory(const juce::String& cat)
    {
        return cat == "Kicks"
            || cat == "Snares"
            || cat == "Hi-Hats"
            || cat == "Percussion"
            || cat == "Claps";
    }

    static CoarseType coarseTypeFromCategory(const juce::String& category,
                                             bool isTonal,
                                             const juce::String& type)
    {
        if (isDrumCategory(category))
            return CoarseType::Drum;

        if (category == "Bass"
            || category == "Guitar"
            || category == "Melodic"
            || category == "Songstarter")
            return CoarseType::Tonal;

        if (category == "FX" || category == "Textures")
            return CoarseType::NoiseFx;

        if (category == "Loops")
        {
            if (isTonal)
                return CoarseType::Tonal;
            return CoarseType::NoiseFx;
        }

        juce::ignoreUnused(type);
        return CoarseType::Unknown;
    }

    static CoarseType coarseTypeFromClass(Detection::Class c)
    {
        using Detection::Class;
        switch (c)
        {
            case Class::Kick:
            case Class::Snare:
            case Class::HiHat:
            case Class::Perc:
                return CoarseType::Drum;

            case Class::Bass:
            case Class::Guitar:
            case Class::Keys:
            case Class::Pad:
            case Class::Lead:
            case Class::Vocal:
                return CoarseType::Tonal;

            case Class::FX:
            case Class::TextureAtmos:
                return CoarseType::NoiseFx;

            case Class::Other:
            case Class::Count:
            default:
                return CoarseType::Unknown;
        }
    }

    // Toggleable debug logging to help tune thresholds against real-world packs.
    static constexpr bool kLogAnalysisDebug = true;

    void logAnalysisDebug(const juce::File& file,
                          const MagicFoldersProcessor::AnalysisResult& result,
                          double duration,
                          int onsetCount,
                          float centroidF,
                          float zcrF,
                          float rolloffF,
                          float attackRMS,
                          float bodyRMS,
                          float keyStrength,
                          float firstToSecondRelativeStrength)
    {
        if (!kLogAnalysisDebug)
            return;

        juce::String msg;
        msg << "[Analysis] "
            << file.getFileName() << " | type=" << result.type
            << " category=" << result.category
            << " dur=" << juce::String(duration, 2)
            << " onsets=" << onsetCount
            << " centroid=" << juce::String(centroidF, 1)
            << " zcr=" << juce::String(zcrF, 3)
            << " rolloff=" << juce::String(rolloffF, 1)
            << " atkRMS=" << juce::String(attackRMS, 4)
            << " bodyRMS=" << juce::String(bodyRMS, 4)
            << " keyStr=" << juce::String(keyStrength, 3);
        DBG(msg);

        // Also write to the detection log file so it's visible without the IDE.
        juce::File logDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                .getChildFile("MagicFoldersLogs");
        logDir.createDirectory();
        logDir.getChildFile("detection.log").appendText(msg + "\n");
    }

    /** Append one row to detection_predictions.csv in Documents/MagicFoldersLogs. */
    void appendDetectionPredictionLog(const juce::File& sourceFile,
                                      const MagicFoldersProcessor::AnalysisResult& result)
    {
        juce::File logDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                .getChildFile("MagicFoldersLogs");
        if (!logDir.createDirectory())
            return;
        juce::File logFile = logDir.getChildFile("detection_predictions.csv");
        if (!logFile.existsAsFile())
            if (!logFile.replaceWithText("file_path,predicted_category,type_loop,top1_prob,timestamp\n"))
                return;
        juce::String path = sourceFile.getFullPathName();
        juce::String cat = result.category;
        juce::String typeLoop = result.type;
        juce::String top1 = result.top1Prob >= 0.0f ? juce::String(result.top1Prob, 4) : "";
        juce::String ts = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S");
        auto escape = [](const juce::String& s) -> juce::String {
            if (s.contains(",") || s.contains("\"") || s.contains("\n") || s.contains("\r"))
            {
                juce::String r;
                r.preallocateBytes(s.length() + 4);
                r << "\"";
                for (juce::juce_wchar c : s)
                    r << (c == '"' ? "\"\"" : juce::String::charToString(c));
                r << "\"";
                return r;
            }
            return s;
        };
        juce::String line = escape(path) + "," + escape(cat) + "," + escape(typeLoop) + ","
                         + escape(top1) + "," + escape(ts) + "\n";
        logFile.appendText(line);
    }
}

void MagicFoldersProcessor::processAll()
{
    juce::File targetDir = (currentProcessDirectory.isDirectory() ? currentProcessDirectory : outputDirectory);
    if (!targetDir.isDirectory()) return;

    categoryCounters.clear();
    lastRunDuplicatesSkipped = 0;
    lastRunBlankSkipped      = 0;

    double hostBpm = 0.0;
    if (useHostBpm)
    {
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto bpm = pos->getBpm())
                    hostBpm = *bpm;
    }

    // ── Seed fingerprint map from files already in the destination pack ──────
    // This prevents re-copying samples that were processed in a previous run.
    std::vector<WaveformFingerprint> seenFingerprints;
    {
        juce::Array<juce::File> existingFiles;
        targetDir.findChildFiles(existingFiles, juce::File::findFiles, true);
        juce::AudioFormatManager checkFmt;
        checkFmt.registerBasicFormats();
        for (const auto& ef : existingFiles)
        {
            if (!checkFmt.findFormatForFileExtension(ef.getFileExtension())) continue;
            auto fp = computeFingerprint(ef);
            if (fp.durationSeconds > 0.0)
                seenFingerprints.push_back(std::move(fp));
        }
    }

    // ── Process each queued file ─────────────────────────────────────────────
    for (auto& info : queue)
    {
        // Waveform duplicate check (against existing files + earlier items in this batch)
        auto fp = computeFingerprint(info.sourceFile);
        if (fp.durationSeconds > 0.0 && isDuplicate(fp, seenFingerprints))
        {
            ++lastRunDuplicatesSkipped;
            continue;
        }

        auto analysis = analyzeAudio(info.sourceFile, hostBpm);
        if (analysis.isBlank)
        {
            ++lastRunBlankSkipped;
            continue;
        }

        appendDetectionPredictionLog(info.sourceFile, analysis);

        info.category      = analysis.category;
        info.type          = analysis.type;
        info.name          = analysis.suggestedName;
        info.suggested_name = analysis.suggestedName;
        info.bpm           = analysis.bpm;
        info.key           = useProjectKey ? projectKey : analysis.key;
        copyToFolder(info);
        processed.add(info);

        // Register this file's fingerprint so subsequent queue items can't duplicate it.
        if (fp.durationSeconds > 0.0)
            seenFingerprints.push_back(std::move(fp));
    }

    queue.clear();

    if (onComplete) onComplete();
}

bool MagicFoldersProcessor::copyToFolder(SampleInfo& info)
{
    juce::File baseDir = (currentProcessDirectory.isDirectory() ? currentProcessDirectory : outputDirectory);
    // Default structure: Pack Name / Loop | One-Shot / [Drums /] Category / files
    juce::String typeFolderName = (info.type == "Loop") ? "Loop" : "One-Shot";
    juce::File typeFolder = baseDir.getChildFile(typeFolderName);
    typeFolder.createDirectory();
    // Avoid redundant "Loops" inside "Loop" folder; use "Melodic" for generic loops
    juce::String categoryFolderName = info.category;
    if (info.type == "Loop" && categoryFolderName == "Loops")
        categoryFolderName = "Melodic";
    // Drum categories are grouped under a "Drums" subfolder
    static const juce::StringArray kDrumCategories { "Kicks", "Snares", "Hi-Hats", "Percussion", "Claps" };
    juce::File folder;
    if (kDrumCategories.contains(categoryFolderName))
    {
        juce::File drumsFolder = typeFolder.getChildFile("Drums");
        drumsFolder.createDirectory();
        folder = drumsFolder.getChildFile(categoryFolderName);
    }
    else
    {
        folder = typeFolder.getChildFile(categoryFolderName);
    }
    folder.createDirectory();

    juce::String ext = info.sourceFile.getFileExtension().trimCharactersAtStart(".").toLowerCase();
    juce::String newName;

    if (info.suggested_name.isNotEmpty())
    {
        newName = info.suggested_name + "." + ext;
    }
    else
    {
        juce::String keyPart = info.key.isNotEmpty() ? ("_" + info.key.replace(" ", "")) : juce::String();
        juce::String bpmPart = (info.type == "Loop" && info.bpm > 0) ? ("_" + juce::String(info.bpm) + "bpm") : juce::String();
        newName = info.name + keyPart + bpmPart + "." + ext;
    }

    juce::File dest = folder.getChildFile(newName);

    int counter = 1;
    while (dest.existsAsFile())
    {
        juce::String base = info.suggested_name.isNotEmpty() ? info.suggested_name : info.name;
        dest = folder.getChildFile(base + "_" + juce::String(counter++) + "." + ext);
    }

    info.success = info.sourceFile.copyFileTo(dest);
    juce::Thread::sleep(10);
    if (info.success && dest.existsAsFile())
    {
        auto srcSize = info.sourceFile.getSize();
        auto dstSize = dest.getSize();
        if (srcSize != dstSize)
            info.success = false;
    }
    if (!dest.existsAsFile())
        info.success = false;
    info.outputPath = dest.getFullPathName();
    return info.success;
}

MagicFoldersProcessor::AnalysisResult MagicFoldersProcessor::analyzeAudio(const juce::File& file, double hostBpmOverride)
{
    AnalysisResult result;

    // Load audio with JUCE (Essentia AudioLoader not available in lightweight build)
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (!reader)
    {
        result.isBlank = true;
        return result;
    }

    const int numSamples = (int) reader->lengthInSamples;
    const int numChannels = (int) reader->numChannels;
    const Real sampleRate = (Real) reader->sampleRate;
    if (numSamples <= 0)
    {
        result.isBlank = true;
        return result;
    }

    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    reader->read(&buffer, 0, numSamples, 0, true, true);
    reader.reset();

    juce::AudioBuffer<float> mono(1, numSamples);
    mono.clear();
    for (int ch = 0; ch < numChannels; ch++)
        mono.addFrom(0, 0, buffer, ch, 0, numSamples, 1.0f / (float) numChannels);

    // Skip silent/blank samples (e.g. deleted clips in Ableton)
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = juce::jmax(peak, std::abs(mono.getSample(0, i)));
    const float peakDb = (peak > 1e-6f) ? (20.0f * std::log10(peak)) : -100.0f;
    if (peakDb < -60.0f)
    {
        result.category = "Other";
        result.type = "One-Shot";
        result.suggestedName = "Sample_01";
        result.isBlank = true;
        return result;
    }

    // ── YAMNet-based detection pipeline (coarse-guarded ML) ──────────────────
    // This is the primary decision-maker when YAMNet is available (yamnet.onnx
    // embedded). It runs multiple windows over the file and gates on confidence
    // + vote consistency, returning Unknown when not sure.
    bool pipelineMadeDecision = false;
    DetectionPipeline::DetectionResult dpResult;
    {
        // Non-strict: minTopScore=0.20, minMargin=0.05, voteConsistency=0.50.
        // Loops with varying content (different kick sounds per bar, pluck notes
        // with silent gaps) fail the strict 60% vote-consistency threshold even
        // when the correct category is obvious.  DetectionV2 provides a second
        // guard against false positives, so we can afford to be more lenient here.
        DetectionPipeline::DetectionConfig cfg;
        cfg.strictMode = false;
        cfg.maxWindows = 8;
        cfg.autoDetectType = true;

        dpResult = DetectionPipeline::detectFile(file, cfg);
        auto& dp = dpResult;

        auto mapCategoryToString = [](DetectionPipeline::DetectionCategory c) -> juce::String
        {
            using DC = DetectionPipeline::DetectionCategory;
            switch (c)
            {
                // Drum categories intentionally return empty — drum classification
                // is handled exclusively by acoustic heuristics (centroid/ZCR/attack).
                // The InstrumentClassifier model is unreliable for isolated drum hits
                // and was the source of consistent kick→HiHat mislabels.
                // Bass also returns empty: InstrumentClassifier labels deep 808 kicks
                // as "Bass" (low centroid), which then triggers the tonal Bass→Melodic
                // guard. The acoustic heuristic correctly distinguishes Bass (!attack)
                // from Kicks (sharp attack), so we let the heuristic handle Bass too.
                case DC::Kick:
                case DC::Snare:
                case DC::HiHat:
                case DC::Perc:
                case DC::Drums:
                case DC::Bass:          return juce::String();
                case DC::Guitar:        return "Guitar";
                case DC::Keys:          return "Melodic";
                case DC::Pad:           return "Melodic";
                case DC::Lead:          return "Melodic";
                case DC::FX:            return "FX";
                case DC::TextureAtmos:  return "Textures";
                case DC::Vocal:         return "Vocals";
                case DC::Unknown:       return juce::String();
            }
            return juce::String();
        };

        // DetectionPipeline (InstrumentClassifier.onnx) is disabled for category
        // assignment. It ran first and set result.category before the heuristic,
        // causing the heuristic's correct calls (e.g. 808 kick → Kicks) to be
        // silently ignored by the isEmpty guard. CNN14 + acoustic heuristics
        // cover every category with far greater accuracy.
        // result.type is determined at line ~825 from duration+onsets anyway.
        // The dpResult struct is still populated above for its debug.top1Label
        // used in the last-resort fallback further down.
        if (false && dp.category != DetectionPipeline::DetectionCategory::Unknown && dp.confidence >= 0.35f)
        {
            result.category = mapCategoryToString(dp.category);
            if (dp.isLoop)
                result.type = "Loop";
            result.top1Prob = dp.confidence;
            pipelineMadeDecision = true;
        }
    }

    // Essentia's OnsetRate and others expect 44100 Hz; resample if needed
    Real workRate = (Real) sampleRate;
    int workSamples = numSamples;
    std::vector<Real> audioBuffer;
    const float* data = mono.getReadPointer(0);

    if (std::abs(sampleRate - 44100.0) > 1.0)
    {
        workRate = 44100.0f;
        workSamples = (int) std::max(1.0, std::floor((double) numSamples * 44100.0 / sampleRate));
        audioBuffer.resize(static_cast<size_t>(workSamples));
        for (int i = 0; i < workSamples; i++)
        {
            double srcIdx = (double) i * sampleRate / 44100.0;
            int i0 = (int) srcIdx;
            int i1 = std::min(i0 + 1, numSamples);
            float f = (float) (srcIdx - i0);
            float s0 = (i0 < numSamples) ? data[i0] : 0.0f;
            float s1 = (i1 < numSamples) ? data[i1] : s0;
            audioBuffer[static_cast<size_t>(i)] = static_cast<Real>(s0 + f * (s1 - s0));
        }
    }
    else
    {
        audioBuffer.resize(static_cast<size_t>(numSamples));
        for (int i = 0; i < numSamples; i++)
            audioBuffer[static_cast<size_t>(i)] = static_cast<Real>(data[i]);
    }

    const double duration = workSamples / (double) workRate;

    AlgorithmFactory& factory = AlgorithmFactory::instance();

    // One-Shot vs Loop: onset detection + duration
    std::vector<Real> onsetTimes;
    Real onsetRate = 0.0f;
    Algorithm* onsetDetector = factory.create("OnsetRate");
    onsetDetector->input("signal").set(audioBuffer);
    onsetDetector->output("onsets").set(onsetTimes);
    onsetDetector->output("onsetRate").set(onsetRate);
    onsetDetector->compute();
    delete onsetDetector;

    result.type = (duration > 2.0 && onsetTimes.size() > 3) ? "Loop" : "One-Shot";

    // BPM (loops only); apply host override when provided
    if (result.type == "Loop")
    {
        if (hostBpmOverride > 0.0)
        {
            result.bpm = juce::jlimit(60, 200, (int) std::round(hostBpmOverride));
        }
        else
        {
            Real bpm = 0.0f;
            Real confidence = 0.0f;
            std::vector<Real> ticks, estimates, bpmIntervals;
            Algorithm* rhythmExtractor = factory.create("RhythmExtractor2013", "method", "multifeature");
            rhythmExtractor->input("signal").set(audioBuffer);
            rhythmExtractor->output("bpm").set(bpm);
            rhythmExtractor->output("confidence").set(confidence);
            rhythmExtractor->output("ticks").set(ticks);
            rhythmExtractor->output("estimates").set(estimates);
            rhythmExtractor->output("bpmIntervals").set(bpmIntervals);
            rhythmExtractor->compute();
            delete rhythmExtractor;
            result.bpm = juce::jlimit(60, 200, (int) std::round(bpm));
        }
    }

    // Key detection: use a single frame (power-of-2) for Spectrum/HPCP/Key
    const int frameSize = 8192;
    const size_t frameLen = (size_t) std::min(frameSize, workSamples);
    std::vector<Real> frameFrame(frameLen);
    for (size_t i = 0; i < frameLen; i++)
        frameFrame[i] = audioBuffer[i];

    std::vector<Real> spectrum, frequencies, magnitudes, hpcp;
    std::string keyStr, scaleStr;
    Real keyStrength = 0.0f;
    Real firstToSecondRelativeStrength = 0.0f;

    Algorithm* spectrumAlgo = factory.create("Spectrum");
    spectrumAlgo->input("frame").set(frameFrame);
    spectrumAlgo->output("spectrum").set(spectrum);
    spectrumAlgo->compute();

    Algorithm* spectralPeaks = factory.create("SpectralPeaks");
    spectralPeaks->input("spectrum").set(spectrum);
    spectralPeaks->output("frequencies").set(frequencies);
    spectralPeaks->output("magnitudes").set(magnitudes);
    spectralPeaks->compute();

    Algorithm* hpcpAlgo = factory.create("HPCP");
    hpcpAlgo->input("frequencies").set(frequencies);
    hpcpAlgo->input("magnitudes").set(magnitudes);
    hpcpAlgo->output("hpcp").set(hpcp);
    hpcpAlgo->compute();

    Algorithm* keyDetector = factory.create("Key", "profileType", "temperley");
    keyDetector->input("pcp").set(hpcp);
    keyDetector->output("key").set(keyStr);
    keyDetector->output("scale").set(scaleStr);
    keyDetector->output("strength").set(keyStrength);
    keyDetector->output("firstToSecondRelativeStrength").set(firstToSecondRelativeStrength);
    keyDetector->compute();

    delete spectrumAlgo;
    delete spectralPeaks;
    delete hpcpAlgo;
    delete keyDetector;

    if (keyStrength > Heuristic::kKeyStrengthForKeyResult)
        result.key = juce::String(keyStr) + (scaleStr == "minor" ? "m" : "");

    // Instrument/category: MFCC + spectral features
    std::vector<Real> mfccCoeffs, mfccBands;
    Real spectralCentroid = 0.0f;
    Real zeroCrossingRate = 0.0f;
    Real spectralRolloff = 0.0f;
    Real loudness = 0.0f;

    Algorithm* mfccAlgo = factory.create("MFCC");
    mfccAlgo->input("spectrum").set(spectrum);
    mfccAlgo->output("bands").set(mfccBands);
    mfccAlgo->output("mfcc").set(mfccCoeffs);
    mfccAlgo->compute();

    Algorithm* centroid = factory.create("SpectralCentroidTime");
    centroid->configure("sampleRate", workRate);
    centroid->input("array").set(audioBuffer);
    centroid->output("centroid").set(spectralCentroid);
    centroid->compute();

    Algorithm* zcr = factory.create("ZeroCrossingRate");
    zcr->input("signal").set(audioBuffer);
    zcr->output("zeroCrossingRate").set(zeroCrossingRate);
    zcr->compute();

    Algorithm* rolloff = factory.create("RollOff");
    rolloff->input("spectrum").set(spectrum);
    rolloff->output("rollOff").set(spectralRolloff);
    rolloff->compute();

    Algorithm* loudnessAlgo = factory.create("Loudness");
    loudnessAlgo->input("signal").set(audioBuffer);
    loudnessAlgo->output("loudness").set(loudness);
    loudnessAlgo->compute();

    delete mfccAlgo;
    delete centroid;
    delete zcr;
    delete rolloff;
    delete loudnessAlgo;

    int attackSamples = (int) (workRate * 0.05);
    attackSamples = std::min(attackSamples, workSamples);
    float attackRMS = 0.0f, bodyRMS = 0.0f;
    for (int i = 0; i < attackSamples; i++)
        attackRMS += (float) (audioBuffer[static_cast<size_t>(i)] * audioBuffer[static_cast<size_t>(i)]);
    attackRMS = std::sqrt(attackRMS / (float) attackSamples);
    int bodyCount = workSamples - attackSamples;
    for (int i = attackSamples; i < workSamples; i++)
        bodyRMS += (float) (audioBuffer[static_cast<size_t>(i)] * audioBuffer[static_cast<size_t>(i)]);
    bodyRMS = std::sqrt(bodyRMS / (float) std::max(1, bodyCount));
    bool hasSharpAttack = (attackRMS > bodyRMS * Heuristic::kSharpAttackRatio);

    float mfcc1 = mfccCoeffs.size() > 1 ? (float) mfccCoeffs[1] : 0.0f;
    float mfcc2 = mfccCoeffs.size() > 2 ? (float) mfccCoeffs[2] : 0.0f;
    float centroidF = (float) spectralCentroid;
    float zcrF = (float) zeroCrossingRate;
    float rolloffF = (float) spectralRolloff;

    // Treat anything with a reasonably confident key as tonal.
    const bool isTonal = (keyStrength > Heuristic::kKeyStrengthTonalMin && firstToSecondRelativeStrength > Heuristic::kFirstToSecondStrengthMin) || result.key.isNotEmpty();

    // Always run the heuristic detector to get a broad sense of the sound when
    // the YAMNet-based path did not already make a confident decision.
    HeuristicCategory::Features hf;
    hf.centroidF = centroidF;
    hf.zcrF = zcrF;
    hf.rolloffF = rolloffF;
    hf.mfcc1 = mfcc1;
    hf.mfcc2 = mfcc2;
    hf.hasSharpAttack = hasSharpAttack;
    hf.isTonal = isTonal;
    hf.duration = duration;
    hf.onsetCount = (int) onsetTimes.size();
    hf.bpm = result.bpm;
    hf.type = result.type;
    HeuristicCategory::Result hr = HeuristicCategory::run(hf, file);
    if (result.category.isEmpty())
    {
        result.category = hr.category;
        result.melodicVibe = hr.melodicVibe;
    }

    // Base coarse type on the pure heuristic result (not pipeline-contaminated result.category),
    // so the coarse-type guard in DetectionV2 reflects actual acoustic features, not a prior
    // (possibly wrong) pipeline decision.
    CoarseType heuristicCoarse = coarseTypeFromCategory(hr.category, isTonal, result.type);

    // Tracks whether CNN14 predicted a drum type for the current file.
    // Used by the tonal-override guard below to avoid redirecting tonal kicks
    // (where CNN14 says "Kick" at low confidence) to Melodic.
    bool mlSaidDrum = false;

    // Run DetectionV2 (YAMNet + trained MLP head) when available.
    // It cross-checks the heuristic coarse type and overrides DetectionPipeline when
    // confident — preventing e.g. guitar/piano from landing in Kick folders.
    if (useAccurateDetection && detectionV2.isAvailable())
    {
        static bool once = true;
        if (once) { DBG("MagicFolders: v2 path active (model available)"); once = false; }

        // Build a simple hash from full path + modification time so we can
        // reuse decisions when the same file is processed multiple times.
        juce::String key = file.getFullPathName() + "|" +
                           juce::String(file.getLastModificationTime().toMilliseconds());
        const juce::uint64 hash = (juce::uint64) key.hashCode64();

        Detection::DetectionResult cached{};
        auto it = detectionCache.find(hash);
        if (it != detectionCache.end())
        {
            cached = it->second;
        }
        else
        {
            // Use the mono buffer at the original reader rate; resampling for
            // Essentia has already happened, but DetectionV2 is decoupled and
            // just needs consistent analysis audio (not on the audio thread).
            juce::AudioBuffer<float> monoForDetection(1, numSamples);
            monoForDetection.copyFrom(0, 0, mono, 0, 0, numSamples);
            cached = detectionV2.classify(monoForDetection,
                                          (double) sampleRate,
                                          result.type == "Loop",
                                          file.getFileNameWithoutExtension());
            detectionCache[hash] = cached;
        }

        // Record whether CNN14 predicted a drum type (used in tonal-override below).
        mlSaidDrum = (coarseTypeFromClass(cached.primary) == CoarseType::Drum);

        if (!cached.hasDecision)
        {
            DBG(juce::String("MagicFolders: v2 rejected (top1=")
                + juce::String(cached.top1Prob, 2)
                + " margin=" + juce::String(cached.top1Prob - cached.top2Prob, 2)
                + ") primary=" + Detection::classToString(cached.primary));
        }
        else
        {
            CoarseType mlCoarse = coarseTypeFromClass(cached.primary);

            bool allowMl = false;

            if (heuristicCoarse == CoarseType::Unknown)
            {
                // Heuristic has no strong opinion.
                // For drums: trust CNN14 only at high confidence (>= 0.70).
                // HiHat requires extra bar (0.85) since it was the original
                // source of kick→hi-hat false positives at low confidence.
                if (mlCoarse == CoarseType::Drum)
                {
                    float drumThresh = (cached.primary == Detection::Class::HiHat) ? 0.85f : 0.70f;
                    allowMl = (cached.top1Prob >= drumThresh);
                }
                else
                {
                    allowMl = true;
                }
            }
            else if (mlCoarse == heuristicCoarse)
            {
                if (heuristicCoarse == CoarseType::Drum)
                {
                    // Let CNN14 refine the drum sub-type at high confidence.
                    // HiHat still requires extra confidence due to historical
                    // false-positives (kicks mislabeled as hi-hats at ~30%).
                    float drumThresh = (cached.primary == Detection::Class::HiHat) ? 0.85f : 0.70f;
                    // When the heuristic already conceded it can't determine a specific
                    // drum type (gave generic "Percussion"), lower the bar so CNN14 can
                    // refine snare/kick at 45%+ rather than requiring 70%.
                    if (hr.category == "Percussion" && cached.primary != Detection::Class::HiHat)
                        drumThresh = (cached.primary == Detection::Class::Kick) ? 0.30f : 0.45f;
                    allowMl = (cached.top1Prob >= drumThresh);
                }
                else
                {
                    // Same coarse type (non-drum) — let ML refine within that type.
                    allowMl = true;
                }
            }
            else
            {
                // Coarse-type disagreement. Strategy depends on direction:
                //
                // Drums → heuristics always win. CNN14 validation shows Kick F1=0.33
                // and Snare F1=0.47 — the model is unreliable for isolated drum
                // samples because CNN14 was trained on full music mixes in AudioSet.
                // Acoustic heuristics (attack shape, centroid, ZCR) are far more
                // reliable for isolated one-shots and loops.
                //
                // Tonal/FX → ML can override at high confidence (≥ 0.65) since
                // CNN14 achieves Guitar F1=0.78, Keys=0.75, Vocal=0.73.
                if (heuristicCoarse == CoarseType::Drum)
                {
                    // Heuristic says Drum but ML says Tonal/FX.
                    // Deep kicks (centroid < 600Hz): always trust heuristic — 808s are
                    // unambiguously kicks even when tonal.
                    // Mid-range (centroid >= 600Hz): Level-2 kick rule can false-trigger
                    // on non-tonal guitar plucks; allow a confident CNN14 tonal call to win.
                    // Guitar specifically: CNN14 achieves F1=0.78 for Guitar in our
                    // training set, so trust it at 55%+ even below the 600Hz centroid
                    // gate (a low guitar note could have centroid < 600Hz).
                    const bool mlSaysGuitar = (cached.primary == Detection::Class::Guitar);
                    if (mlCoarse == CoarseType::Tonal
                        && ((cached.top1Prob >= 0.65f && centroidF >= Heuristic::kKickCentroidLow)
                            || (mlSaysGuitar && cached.top1Prob >= 0.55f)))
                    {
                        DBG(juce::String("MagicFolders: CNN14 tonal override of drum heuristic (")
                            + Detection::classToString(cached.primary)
                            + " @ " + juce::String(cached.top1Prob, 2) + ")");
                        allowMl = true;
                    }
                    else
                    {
                        DBG(juce::String("MagicFolders: drum heuristic wins over ML (")
                            + Detection::classToString(cached.primary)
                            + " @ " + juce::String(cached.top1Prob, 2) + ")");
                        allowMl = false;
                    }
                }
                else if (cached.top1Prob >= 0.65f
                         || (mlCoarse == CoarseType::NoiseFx && cached.top1Prob >= 0.45f))
                {
                    // ML is highly confident and heuristic says non-drum.
                    // Trust ML to upgrade/downgrade within tonal/FX buckets.
                    // Lower bar (0.45) when ML predicts NoiseFx (Texture/Atmos/FX):
                    // heuristics often mistake slow-attack ambient loops for Guitar.
                    DBG(juce::String("MagicFolders: high-confidence ML overrides heuristic (")
                        + juce::String(cached.top1Prob, 2)
                        + ") -> " + Detection::classToString(cached.primary));
                    allowMl = true;
                }
                else
                {
                    // Low confidence — keep heuristic.
                    DBG(juce::String("MagicFolders: low-conf ML conflict, keeping heuristic (")
                        + juce::String(cached.top1Prob, 2)
                        + ") heuristic=" + result.category
                        + " ml=" + Detection::classToString(cached.primary));
                    allowMl = false;
                }
            }

            // Always record the ML confidence so the tonal-override guard below
            // (`result.top1Prob < 0.45f`) sees the real ML certainty even when
            // allowMl=false (e.g. 808 kick: heuristic=Kicks, CNN14=Pad@95% →
            // top1Prob=0.0 was previously causing the tonal override to fire and
            // change the kick to Melodic).
            result.top1Prob = (float) cached.top1Prob;

            if (allowMl)
            {
                DBG(juce::String("MagicFolders: v2 accepted (within coarse type) -> ")
                    + Detection::classToString(cached.primary));

                switch (cached.primary)
                {
                    case Detection::Class::Kick:           result.category = "Kicks"; break;
                    case Detection::Class::Snare:          result.category = "Snares"; break;
                    case Detection::Class::HiHat:          result.category = "Hi-Hats"; break;
                    case Detection::Class::Perc:           result.category = "Percussion"; break;
                    case Detection::Class::Bass:           result.category = "Bass"; break;
                    case Detection::Class::Guitar:         result.category = "Guitar"; break;
                    case Detection::Class::Keys:           result.category = "Melodic"; result.melodicVibe = "Keys"; break;
                    case Detection::Class::Pad:            result.category = "Melodic"; result.melodicVibe = "Pad"; break;
                    case Detection::Class::Lead:           result.category = "Melodic"; result.melodicVibe = "Lead"; break;
                    case Detection::Class::Vocal:          result.category = "Vocals"; break;
                    case Detection::Class::FX:             result.category = "FX"; break;
                    case Detection::Class::TextureAtmos:   result.category = "Textures"; break;
                    case Detection::Class::Other:
                    default:                               result.category = "Other"; break;
                }
            }
        }
    }

    // Final safety net: avoid clearly tonal material ending up as Bass unless
    // Tonal-signal guard: if Essentia detected a musical key AND the model is
    // not highly confident, the sample is likely melodic — don't place it in a
    // drum folder.  We skip this guard when top1Prob >= 0.85 so a very
    // confident model prediction (e.g. a tonal/filtered kick at 90%+) is not
    // wrongly blocked by the key detector.
    // Exception: filename explicitly names a drum type.
    // Tonal-signal guard: only override a drum classification when (a) the ML is
    // not very confident AND (b) Essentia detected a STRONG tonal key.  Many
    // synthesised kicks / hi-hats have a faint tonal fundamental that barely
    // passes the minimum key-strength threshold — we no longer want those to
    // route to Unknown.  Require top1Prob < 0.90 (was 0.80) so confident drum
    // predictions survive, and require a stronger key signal (> 0.50) before
    // overriding.
    // Tonal-override guard: redirect to Melodic only when the file has a VERY strong
    // musical key (> 0.70) AND the ML is genuinely uncertain about the drum label
    // (< 0.45) AND it is a one-shot (loops are inherently rhythmic — a kick loop with
    // keyStr 0.58 is still a kick loop).  Kicks/snares naturally have a tonal
    // fundamental; the old threshold of 0.50 caught nearly every synthesized drum and
    // sent it to Unknown even at 85 % ML confidence.
    // Route to Melodic (not Unknown) — if we know it's tonal it belongs somewhere useful.
    static const juce::StringArray kPercussiveCategories { "Kicks", "Snares", "Hi-Hats", "Percussion" };
    if (result.type != "Loop"
        && isTonal
        && keyStrength > 0.70f
        && kPercussiveCategories.contains(result.category)
        && result.top1Prob < 0.45f
        && !mlSaidDrum)  // CNN14 leans toward drum → don't redirect to Melodic
    {
        const juce::String lowerName = file.getFileNameWithoutExtension().toLowerCase();
        const bool nameSuggestsDrum = lowerName.contains("kick") || lowerName.contains("kik")
                                   || lowerName.contains("bd") || lowerName.contains("snare")
                                   || lowerName.contains("snr") || lowerName.contains("hihat")
                                   || lowerName.contains("hi-hat") || lowerName.contains(" hat")
                                   || lowerName.contains("hh") || lowerName.contains("cymbal")
                                   || lowerName.contains("clap") || lowerName.contains("perc")
                                   || lowerName.contains("drum") || lowerName.contains("hat loop")
                                   || lowerName.contains("rim") || lowerName.contains("tom");
        if (!nameSuggestsDrum)
        {
            DBG("MagicFolders: very strong tonal key overrides uncertain drum label " + result.category
                + " (top1=" + juce::String(result.top1Prob, 2)
                + ", keyStrength=" + juce::String(keyStrength, 2)
                + ") -> Melodic for " + file.getFileName());
            result.category = "Melodic";
            if (result.melodicVibe.isEmpty())
                result.melodicVibe = "Keys";
        }
    }

    // the filename strongly suggests a bass instrument. This runs after both
    // heuristics and ML have had a chance to decide.
    if (isTonal && result.category == "Bass")
    {
        const juce::String lowerName = file.getFileNameWithoutExtension().toLowerCase();
        const bool nameSuggestsBass = lowerName.contains("bass")
                                   || lowerName.contains("808")
                                   || lowerName.contains("sub")
                                   || lowerName.contains("subbass")
                                   || lowerName.contains("lowend")
                                   || lowerName.contains("bs_")
                                   || lowerName.contains("bss");
        const bool nameSuggestsMelodic = lowerName.contains("piano")
                                      || lowerName.contains("keys")
                                      || lowerName.contains("rhodes")
                                      || lowerName.contains("melod")
                                      || lowerName.contains("lead")
                                      || lowerName.contains("synth")
                                      || lowerName.contains("arp")
                                      || lowerName.contains("pad")
                                      || lowerName.contains("hook");

        if (!nameSuggestsBass)
        {
            // Tonal + no bass name cue → assume melodic rather than Unknown.
            // "synth", "lead", "pad", "keys" etc. in the name → Melodic.
            // Anything else that's tonal and was mis-tagged Bass → also Melodic
            // (better than Unknown since the ML at least knew it was tonal).
            result.category = "Melodic";
            if (result.melodicVibe.isEmpty())
                result.melodicVibe = nameSuggestsMelodic ? "Keys" : "Keys";
        }
    }

    // Last-resort soft fallback: if we still have no category (or Other), use the
    // pipeline's top1 debug label as a weak hint.
    // Drums are EXCLUDED: the DetectionPipeline runs on YAMNet which is unreliable
    // for isolated drum samples, and a 18% confidence drum guess causes consistent
    // false classifications (e.g. kicks landing in Hi-Hats).
    // If acoustic heuristics didn't identify it as a drum, no low-confidence ML
    // guess should either. Only tonal/FX categories are useful here.
    if ((result.category.isEmpty() || result.category == "Other") && dpResult.debug.top1Score >= 0.40f)
    {
        const auto& label = dpResult.debug.top1Label;
        // Drums intentionally omitted — heuristics handle all drum classification.
        if      (label == "Bass")         result.category = "Bass";
        else if (label == "Guitar")       result.category = "Guitar";
        else if (label == "Keys" ||
                 label == "Pad"  ||
                 label == "Lead")         { result.category = "Melodic"; if (result.melodicVibe.isEmpty()) result.melodicVibe = label; }
        else if (label == "FX")           result.category = "FX";
        else if (label == "TextureAtmos") result.category = "Textures";
        else if (label == "Vocal")        result.category = "Vocals";

        if (result.category != "Other" && result.category.isNotEmpty())
            result.top1Prob = dpResult.debug.top1Score;
    }

    logAnalysisDebug(file,
                     result,
                     duration,
                     (int) onsetTimes.size(),
                     centroidF,
                     zcrF,
                     rolloffF,
                     attackRMS,
                     bodyRMS,
                     (float) keyStrength,
                     (float) firstToSecondRelativeStrength);

    // Smart naming
    std::map<juce::String, juce::String> shortNames = {
        {"Kicks", "Kick"}, {"Snares", "Snare"}, {"Hi-Hats", "HiHat"},
        {"Bass", "Bass"}, {"Guitar", "Guitar"}, {"FX", "FX"},
        {"Percussion", "Perc"}, {"Melodic", "Melody"},
        {"Textures", "Texture"}, {"Songstarter", "Songstarter"},
        {"Other", "Sample"}, {"Loops", "Loop"}
    };
    juce::String shortName = (shortNames.count(result.category) > 0) ? shortNames[result.category] : "Sample";
    juce::String indexStr = juce::String(++categoryCounters[result.category]).paddedLeft('0', 2);

    juce::String vibeStr = result.melodicVibe.isNotEmpty() ? ("_" + result.melodicVibe) : juce::String();
    juce::String bpmStr = (result.type == "Loop" && result.bpm > 0) ? ("_" + juce::String(result.bpm) + "bpm") : "";
    juce::String keyPart = result.key.isNotEmpty() ? ("_" + result.key.replace(" ", "")) : juce::String();
    if (result.type != "Loop" && (result.category == "Kicks" || result.category == "Snares"
            || result.category == "Hi-Hats" || result.category == "Percussion"))
        keyPart = juce::String();

    if (generateFunNames)
    {
        static const char* kickAdj[] = { "Big", "Liquid", "House_Club", "Holy", "Punchy", "Deep", "Tight", "Boom", "Sub", "Club", "Heavy", "Round" };
        static const char* snareAdj[] = { "Crispy", "Fat", "Room", "Tight", "Crack", "Ghost", "Rim", "Side", "Dry", "Snap", "Phat", "Layered" };
        static const char* hihatAdj[] = { "Shiny", "Dark", "Open", "Closed", "Trash", "Tight", "Room", "Crunch", "Sizzle", "Soft", "Bright" };
        static const char* bassAdj[] = { "Subby", "Punchy", "Round", "Gritty", "Smooth", "Deep", "Warm", "Growl", "Clean", "Saturated" };
        static const char* loopAdj[] = { "Groove", "Vibe", "Section", "Hook", "Drop", "Build", "Break", "Main", "Fill", "Loop" };
        static const char* otherAdj[] = { "Cool", "Nice", "Solid", "Fresh", "Smooth", "Clean", "Warm", "Bright", "Dark", "Chill" };
        const char** list = otherAdj;
        int listSize = 10;
        if (result.category == "Kicks") { list = kickAdj; listSize = (int)(sizeof(kickAdj) / sizeof(kickAdj[0])); }
        else if (result.category == "Snares") { list = snareAdj; listSize = (int)(sizeof(snareAdj) / sizeof(snareAdj[0])); }
        else if (result.category == "Hi-Hats") { list = hihatAdj; listSize = (int)(sizeof(hihatAdj) / sizeof(hihatAdj[0])); }
        else if (result.category == "Bass") { list = bassAdj; listSize = (int)(sizeof(bassAdj) / sizeof(bassAdj[0])); }
        else if (result.type == "Loop" || result.category == "Loops" || result.category == "Textures" || result.category == "Songstarter") { list = loopAdj; listSize = (int)(sizeof(loopAdj) / sizeof(loopAdj[0])); }
        juce::String adj(list[juce::Random::getSystemRandom().nextInt(listSize)]);
        if (result.type == "Loop")
            result.suggestedName = adj + "_Loop_" + shortName + vibeStr + keyPart + bpmStr + "_" + indexStr;
        else
            result.suggestedName = adj + "_" + shortName + vibeStr + keyPart + "_" + indexStr;
    }
    else
    {
        if (result.type == "Loop")
            result.suggestedName = "Loop_" + shortName + vibeStr + keyPart + bpmStr + "_" + indexStr;
        else
            result.suggestedName = shortName + vibeStr + keyPart + "_" + indexStr;
    }

    return result;
}

void MagicFoldersProcessor::clearQueue()
{
    queue.clear();
}

void MagicFoldersProcessor::removeQueueItemsAt(const juce::Array<int>& indices)
{
    if (indices.isEmpty() || queue.isEmpty())
        return;
    juce::Array<int> sorted(indices);
    sorted.sort();
    for (int i = sorted.size() - 1; i >= 0; --i)
    {
        int idx = sorted.getUnchecked(i);
        if (idx >= 0 && idx < queue.size())
            queue.remove(idx);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MagicFoldersProcessor();
}
