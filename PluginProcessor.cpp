#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <essentia/essentia.h>
#include <essentia/algorithmfactory.h>
#include <essentia/essentiamath.h>
#include <essentia/pool.h>

using namespace essentia;
using namespace essentia::standard;

SampleOrganizerProcessor::SampleOrganizerProcessor()
    : previewTransport()
{
    setPlayConfigDetails(0, 2, 44100.0, 512);
    previewReadAheadThread.startThread();
    essentia::init();
}

SampleOrganizerProcessor::~SampleOrganizerProcessor()
{
    stopPreview();
    previewReadAheadThread.stopThread(2000);
    essentia::shutdown();
}

void SampleOrganizerProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    previewSampleRate = sampleRate;
    previewBlockSize = samplesPerBlock;
    previewTransport.prepareToPlay(samplesPerBlock, sampleRate);
}

void SampleOrganizerProcessor::releaseResources()
{
    previewTransport.releaseResources();
}

void SampleOrganizerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();
    if (previewReaderSource == nullptr)
        return;
    juce::AudioSourceChannelInfo info(buffer);
    previewTransport.getNextAudioBlock(info);
}

void SampleOrganizerProcessor::setPreviewSource(std::unique_ptr<juce::AudioFormatReaderSource> source, double fileSampleRate, double lengthInSeconds)
{
    previewTransport.setSource(nullptr);
    previewReaderSource = std::move(source);
    previewLengthSeconds = lengthInSeconds;
    if (previewReaderSource)
    {
        const int readAheadSize = 32768;
        previewTransport.setSource(previewReaderSource.get(), readAheadSize, &previewReadAheadThread, fileSampleRate);
        if (previewBlockSize > 0 && previewSampleRate > 0)
            previewTransport.prepareToPlay(previewBlockSize, previewSampleRate);
    }
}

void SampleOrganizerProcessor::startPreview()
{
    if (previewReaderSource)
    {
        previewTransport.setPosition(0.0);
        previewTransport.start();
    }
}

void SampleOrganizerProcessor::stopPreview()
{
    previewTransport.stop();
    previewTransport.setSource(nullptr);
    previewReaderSource.reset();
    previewLengthSeconds = 0.0;
}

juce::AudioProcessorEditor* SampleOrganizerProcessor::createEditor()
{
    return new SampleOrganizerEditor(*this);
}

void SampleOrganizerProcessor::setOutputDirectory(const juce::File& dir)
{
    outputDirectory = dir;
}

void SampleOrganizerProcessor::setBatchPlusFolder(const juce::File& dir)
{
    batchPlusFolder = dir;
}

void SampleOrganizerProcessor::tryAutoDetectAbletonSamplesFolder()
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

void SampleOrganizerProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream os(destData, true);
    os.writeString(outputDirectory.getFullPathName());
    os.writeString(batchPlusFolder.getFullPathName());
    os.writeString(generateFunNames ? "1" : "0");
}

void SampleOrganizerProcessor::setStateInformation(const void* data, int sizeInBytes)
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
}

void SampleOrganizerProcessor::addFiles(const juce::Array<juce::File>& files)
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

void SampleOrganizerProcessor::addFilesFromFolder(const juce::File& directory)
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

void SampleOrganizerProcessor::addFilesFromFolderRecursive(const juce::File& directory)
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

juce::String SampleOrganizerProcessor::detectCategory(const juce::String& fn)
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

juce::String SampleOrganizerProcessor::detectType(const juce::String& fn)
{
    if (fn.contains("loop") || fn.contains("lp_") || fn.contains("_lp")) return "Loop";
    return "One-Shot";
}

namespace
{
    /** Lightweight filename-based hints to refine ambiguous analysis results.
        Only meant to gently steer between Guitar/Bass/Melodic/Textures, not override clear drum/FX hits. */
    void applyFilenameHints(const juce::File& file, SampleOrganizerProcessor::AnalysisResult& result)
    {
        const juce::String lowerName = file.getFileNameWithoutExtension().toLowerCase();

        const bool nameSuggestsGuitar =
            lowerName.contains("gtr") || lowerName.contains("guitar") ||
            lowerName.contains("egtr") || lowerName.contains("agtr") ||
            lowerName.contains("eg_") || lowerName.contains("ag_") ||
            lowerName.contains("strum") || lowerName.contains("strummed") ||
            lowerName.contains("riff") || lowerName.contains("chug") ||
            lowerName.contains("palm") || lowerName.contains("powerchord") ||
            lowerName.contains("power_chord") || lowerName.contains("rhythm") ||
            lowerName.contains("pluck") || lowerName.contains("chord");

        const bool nameSuggestsBass =
            lowerName.contains("bass") || lowerName.contains("808") ||
            lowerName.contains("sub") || lowerName.contains("lowend") ||
            lowerName.contains("bs_") || lowerName.contains("bss") ||
            lowerName.contains("subdrop") || lowerName.contains("sub_drop") ||
            lowerName.contains("slide");

        const bool nameSuggestsTexture =
            lowerName.contains("texture") || lowerName.contains("atmo") ||
            lowerName.contains("atmos") || lowerName.contains("ambience") ||
            lowerName.contains("ambient") || lowerName.contains("drone") ||
            lowerName.contains("noise") || lowerName.contains("fx");

        const bool nameSuggestsMelodic =
            lowerName.contains("melod") || lowerName.contains("lead") ||
            lowerName.contains("keys") || lowerName.contains("piano") ||
            lowerName.contains("synth") || lowerName.contains("arp") ||
            lowerName.contains("pluck") || lowerName.contains("hook");

        // Only nudge loops; one-shots are already handled well by attack-based rules.
        if (result.type != "Loop")
            return;

        // If the filename is clearly an instrument, prefer that over a generic bucket.
        if (result.category == "Textures" || result.category == "Melodic" || result.category == "Other" || result.category == "Loops")
        {
            if (nameSuggestsGuitar)
            {
                result.category = "Guitar";
                return;
            }
            if (nameSuggestsBass)
            {
                result.category = "Bass";
                return;
            }
        }

        // For ambiguous \"Textures\" / \"Other\", lean toward Melodic when the name sounds musical.
        if ((result.category == "Textures" || result.category == "Other" || result.category == "Loops") && nameSuggestsMelodic && !nameSuggestsTexture)
        {
            result.category = "Melodic";
            return;
        }

        // Only keep Textures when either the name explicitly calls it out or no strong melodic hints exist.
        if (result.category == "Textures" && nameSuggestsTexture && !nameSuggestsMelodic && !nameSuggestsGuitar && !nameSuggestsBass)
        {
            return;
        }
        if (result.category == "Textures" && (nameSuggestsGuitar || nameSuggestsBass || nameSuggestsMelodic))
        {
            if (nameSuggestsGuitar)      result.category = "Guitar";
            else if (nameSuggestsBass)   result.category = "Bass";
            else                         result.category = "Melodic";
        }
    }

    /** Decide whether a loop is a good candidate for \"Textures\" based purely on audio features. */
    bool isNoisyTextureCandidate(bool hasSharpAttack,
                                 double duration,
                                 float centroidF,
                                 float zcrF,
                                 float rolloffF,
                                 const std::vector<essentia::Real>& onsetTimes)
    {
        const int onsetCount = (int) onsetTimes.size();
        const bool veryFewOnsets = onsetCount <= 4;
        const bool quiteLong = duration >= 3.0;
        const bool darkAndSoft = !hasSharpAttack && centroidF < 1800.0f && zcrF < 0.12f;
        const bool noisySwish = zcrF > 0.18f && rolloffF > 6000.0f;

        // Long, sparse, dark pads or noisy swishes without much transient structure.
        return (quiteLong && veryFewOnsets && darkAndSoft) || noisySwish;
    }

    /** Heuristic detector for guitar-like loops: tonal, plucked attacks, mid-centric spectrum, moderate onset density. */
    bool isGuitarLikeLoop(bool isTonal,
                          bool hasSharpAttack,
                          double duration,
                          float centroidF,
                          float zcrF,
                          int onsetCount)
    {
        if (!isTonal)
            return false;

        // Broad but reasonable musical loop duration.
        if (duration < 0.75 || duration > 16.0)
            return false;

        if (onsetCount < 2 || onsetCount > 48)
            return false;

        // Allow both sharper rhythm parts and softer picked patterns.
        const bool rhythmGuitar =
            hasSharpAttack &&
            centroidF >= 300.0f && centroidF <= 5500.0f &&
            zcrF < 0.22f;

        const bool pickedGuitar =
            !hasSharpAttack &&
            onsetCount >= 4 &&
            centroidF >= 250.0f && centroidF <= 4500.0f &&
            zcrF < 0.25f;

        return rhythmGuitar || pickedGuitar;
    }

    // Toggleable debug logging to help tune thresholds against real-world packs.
    static constexpr bool kLogAnalysisDebug = false;

    void logAnalysisDebug(const juce::File& file,
                          const SampleOrganizerProcessor::AnalysisResult& result,
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
            << " bpm=" << result.bpm
            << " key=" << result.key
            << " dur=" << juce::String(duration, 2)
            << " onsets=" << onsetCount
            << " centroid=" << juce::String(centroidF, 1)
            << " zcr=" << juce::String(zcrF, 3)
            << " rolloff=" << juce::String(rolloffF, 1)
            << " atkRMS=" << juce::String(attackRMS, 4)
            << " bodyRMS=" << juce::String(bodyRMS, 4)
            << " keyStr=" << juce::String(keyStrength, 3)
            << " relKeyStr=" << juce::String(firstToSecondRelativeStrength, 3);
        DBG(msg);
    }
}

void SampleOrganizerProcessor::processAll()
{
    juce::File targetDir = (currentProcessDirectory.isDirectory() ? currentProcessDirectory : outputDirectory);
    if (!targetDir.isDirectory()) return;

    categoryCounters.clear();

    double hostBpm = 0.0;
    if (useHostBpm)
    {
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto bpm = pos->getBpm())
                    hostBpm = *bpm;
    }

    for (auto& info : queue)
    {
        auto analysis = analyzeAudio(info.sourceFile, hostBpm);
        if (analysis.isBlank)
            continue;  // don't copy or count silent/blank samples
        info.category = analysis.category;
        info.type = analysis.type;
        info.name = analysis.suggestedName;
        info.suggested_name = analysis.suggestedName;
        info.bpm = analysis.bpm;
        info.key = useProjectKey ? projectKey : analysis.key;
        copyToFolder(info);
        processed.add(info);
    }

    queue.clear();

    if (onComplete) onComplete();
}

bool SampleOrganizerProcessor::copyToFolder(SampleInfo& info)
{
    juce::File baseDir = (currentProcessDirectory.isDirectory() ? currentProcessDirectory : outputDirectory);
    // Default structure: Pack Name / Loop | One-Shot / Category (instruments) / files
    juce::String typeFolderName = (info.type == "Loop") ? "Loop" : "One-Shot";
    juce::File typeFolder = baseDir.getChildFile(typeFolderName);
    typeFolder.createDirectory();
    // Avoid redundant "Loops" inside "Loop" folder; use "Melodic" for generic loops
    juce::String categoryFolderName = info.category;
    if (info.type == "Loop" && categoryFolderName == "Loops")
        categoryFolderName = "Melodic";
    juce::File folder = typeFolder.getChildFile(categoryFolderName);
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

SampleOrganizerProcessor::AnalysisResult SampleOrganizerProcessor::analyzeAudio(const juce::File& file, double hostBpmOverride)
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

    if (keyStrength > 0.35f)
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
    bool hasSharpAttack = (attackRMS > bodyRMS * 2.5f);

    float mfcc1 = mfccCoeffs.size() > 1 ? (float) mfccCoeffs[1] : 0.0f;
    float mfcc2 = mfccCoeffs.size() > 2 ? (float) mfccCoeffs[2] : 0.0f;
    float centroidF = (float) spectralCentroid;
    float zcrF = (float) zeroCrossingRate;
    float rolloffF = (float) spectralRolloff;

    // Treat anything with a reasonably confident key as tonal.
    const bool isTonal = (keyStrength > 0.3f && firstToSecondRelativeStrength > 0.1f) || result.key.isNotEmpty();

    // Drums and percussion first (most specific)
    if (hasSharpAttack && centroidF < 800.0f && zcrF < 0.1f)
        result.category = "Kicks";
    else if (zcrF > 0.15f && rolloffF > 4000.0f && duration < 1.0)
        result.category = "Hi-Hats";
    else if (hasSharpAttack && centroidF > 800.0f && centroidF < 3000.0f)
        result.category = "Snares";
    else if (centroidF < 600.0f && !hasSharpAttack && mfcc1 < -10.0f)
        result.category = "Bass";
    else if (zcrF > 0.1f && rolloffF > 6000.0f)
        result.category = "FX";
    else if (hasSharpAttack && duration < 0.5f)
        result.category = "Percussion";
    // Broad guitar-like loop detector before more specific melodic checks.
    else if (result.type == "Loop" && isGuitarLikeLoop(isTonal, hasSharpAttack, duration, centroidF, zcrF, (int) onsetTimes.size()))
        result.category = "Guitar";
    // Guitar: key + mid-range harmonic content; check before Melodic so guitar loops don't become "Keys"
    else if (isTonal && centroidF >= 400.0f && centroidF <= 4500.0f
             && (!hasSharpAttack || (result.type == "Loop" && onsetTimes.size() >= 2 && onsetTimes.size() <= 24)))
        result.category = "Guitar";
    // Melodic: key + brighter/synth-like (higher centroid or different timbre)
    else if (!hasSharpAttack && isTonal && mfcc2 > 0.0f)
        result.category = "Melodic";
    // Loop with guitar-like timbre but no key detected → still call it Guitar
    else if (result.type == "Loop" && isTonal && centroidF >= 400.0f && centroidF <= 4500.0f
             && zcrF < 0.12f && onsetTimes.size() >= 2 && onsetTimes.size() <= 30)
        result.category = "Guitar";
    else if (result.type == "Loop")
    {
        const int onsetCount = (int) onsetTimes.size();
        // Long, evolving tonal loops with a clear sense of groove or song section → Songstarter
        if (isTonal && duration >= 4.0 && onsetCount >= 4 && onsetCount <= 32 && result.bpm > 0)
            result.category = "Songstarter";
        // Strict Texture: only when non‑tonal and audio looks like a pad/atmosphere or noisy swish
        else if (!isTonal && isNoisyTextureCandidate(hasSharpAttack, duration, centroidF, zcrF, rolloffF, onsetTimes))
            result.category = "Textures";
        // Fallback melodic loop when tonal but we didn't hit more specific Melodic/Guitar/Bass rules
        else if (isTonal)
            result.category = "Melodic";
        else
            result.category = "Other";
    }
    else
        result.category = "Other";

    if (result.category == "Melodic")
    {
        if (!hasSharpAttack && duration > 1.5f && onsetTimes.size() <= 6)
            result.melodicVibe = "Pad";
        else if (hasSharpAttack && (duration < 2.5f || onsetTimes.size() > 5))
            result.melodicVibe = "Pluck";
        else if (centroidF > 2400.0f)
            result.melodicVibe = "Lead";
        else
            result.melodicVibe = "Keys";
    }

    // Apply filename-based hints last so they can gently correct ambiguous loop cases (e.g. \"Guitar\" vs \"Textures\").
    applyFilenameHints(file, result);

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

void SampleOrganizerProcessor::clearQueue()
{
    queue.clear();
}

void SampleOrganizerProcessor::removeQueueItemsAt(const juce::Array<int>& indices)
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
    return new SampleOrganizerProcessor();
}
