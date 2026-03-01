#pragma once
#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <map>

class SampleOrganizerProcessor : public juce::AudioProcessor
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

    SampleOrganizerProcessor();
    ~SampleOrganizerProcessor() override;

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sample Organizer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    // === Core Logic ===
    juce::File outputDirectory;
    juce::String projectKey = "C Major";
    int projectBPM = 120;
    juce::String defaultGenre = "Unknown";
    bool useHostBpm = false;
    bool useProjectKey = true;

    /** 0 = Category_Key_BPM_Index, 1 = Index_Category_Key_BPM, 2 = Custom prefix + Index */
    int namingFormat = 0;
    juce::String customPrefix;
    bool overwriteDuplicates = false;
    bool themeLight = true;

    /** When set by the editor, processAll() writes here instead of outputDirectory. */
    juce::File currentProcessDirectory;

    juce::Array<SampleInfo> queue;
    juce::Array<SampleInfo> processed;

    void setOutputDirectory(const juce::File& dir);
    void addFiles(const juce::Array<juce::File>& files);
    void addFilesFromFolder(const juce::File& directory);
    void processAll();
    void clearQueue();

    juce::String detectCategory(const juce::String& filename);
    juce::String detectType(const juce::String& filename);
    bool copyToFolder(SampleInfo& info);

    /** hostBpmOverride: when useHostBpm is true and > 0, use for loops instead of detecting */
    AnalysisResult analyzeAudio(const juce::File& file, double hostBpmOverride = 0.0);

    std::function<void()> onComplete;

    std::map<juce::String, int> categoryCounters;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleOrganizerProcessor)
};
