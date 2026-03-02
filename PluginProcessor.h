#pragma once
#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <map>

class MagicFoldersProcessor : public juce::AudioProcessor
{
public:
    struct AnalysisResult
    {
        juce::String category;
        juce::String type;
        juce::String suggestedName;
        juce::String key;
        int bpm = 0;
        juce::String melodicVibe;  // Pad, Pluck, Lead, Keys (only set when category == Melodic)
        /** True if audio is silent/near-silent (e.g. blank Ableton slot); skip copying. */
        bool isBlank = false;
    };

    struct SampleInfo
    {
        juce::File sourceFile;
        juce::String name;
        juce::String type;      // "One-Shot" or "Loop"
        juce::String category;  // "Kick", "Snare", "Hi-Hat", "Loop", "FX", etc.
        juce::String key;
        int bpm = 120;
        juce::String genre;
        bool success = false;
        juce::String outputPath;
        juce::String suggested_name;  // smart rename from analysis
    };

    MagicFoldersProcessor();
    ~MagicFoldersProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Magic Folders"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // === Core Logic ===
    juce::File outputDirectory;
    /** Folder used by Batch + to add all audio (e.g. Ableton project recordings). Must be set in Settings. */
    juce::File batchPlusFolder;
    juce::String projectKey = "C Major";
    int projectBPM = 120;
    juce::String defaultGenre = "Unknown";
    bool useHostBpm = false;
    bool useProjectKey = true;

    /** 0 = Category_Key_BPM_Index, 1 = Index_Category_Key_BPM, 2 = Custom prefix + Index */
    int namingFormat = 0;
    juce::String customPrefix;
    /** When true, suggested names use random adjectives (e.g. Big Kick, Liquid Snare) while keeping category/BPM/key. */
    bool generateFunNames = false;
    bool overwriteDuplicates = false;
    bool themeLight = true;

    /** When set by the editor, processAll() writes here instead of outputDirectory. */
    juce::File currentProcessDirectory;

    juce::Array<SampleInfo> queue;
    juce::Array<SampleInfo> processed;

    void setOutputDirectory(const juce::File& dir);
    void setBatchPlusFolder(const juce::File& dir);
    /** If batchPlusFolder is not set, try to find an Ableton project "Samples" folder in Music/Documents. */
    void tryAutoDetectAbletonSamplesFolder();
    void addFiles(const juce::Array<juce::File>& files);
    void addFilesFromFolder(const juce::File& directory);
    /** Adds all WAV/AIFF files from directory and all subdirectories to the queue. */
    void addFilesFromFolderRecursive(const juce::File& directory);
    void processAll();
    void clearQueue();
    /** Remove queue items at the given indices (0-based). Indices are removed in reverse order. */
    void removeQueueItemsAt(const juce::Array<int>& indices);

    juce::String detectCategory(const juce::String& filename);
    juce::String detectType(const juce::String& filename);
    bool copyToFolder(SampleInfo& info);

    /** hostBpmOverride: when useHostBpm is true and > 0, use for loops instead of detecting */
    AnalysisResult analyzeAudio(const juce::File& file, double hostBpmOverride = 0.0);

    std::function<void()> onComplete;

    std::map<juce::String, int> categoryCounters;

    /** Set after each processAll() call — counts files skipped as waveform duplicates. */
    int lastRunDuplicatesSkipped = 0;
    /** Set after each processAll() call — counts files skipped as blank/silent. */
    int lastRunBlankSkipped = 0;

    // Preview playback (uses host/standalone audio output)
    void setPreviewSource(std::unique_ptr<juce::AudioFormatReaderSource> source, double fileSampleRate, double lengthInSeconds);
    void startPreview();
    void stopPreview();
    juce::AudioTransportSource* getPreviewTransport() { return &previewTransport; }
    double getPreviewLengthSeconds() const { return previewLengthSeconds; }

private:
    double previewSampleRate = 44100;
    int previewBlockSize = 512;
    juce::TimeSliceThread previewReadAheadThread { "Preview read-ahead" };
    juce::AudioTransportSource previewTransport;
    std::unique_ptr<juce::AudioFormatReaderSource> previewReaderSource;
    double previewLengthSeconds = 0.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagicFoldersProcessor)
};
