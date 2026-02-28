#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class SampleOrganizerEditor : public juce::AudioProcessorEditor,
                               public juce::DragAndDropTarget,
                               public juce::FileDragAndDropTarget
{
public:
    SampleOrganizerEditor(SampleOrganizerProcessor&);
    ~SampleOrganizerEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // File drag and drop
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // Drag and drop (generic)
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails&) override { return false; }
    void itemDropped(const juce::DragAndDropTarget::SourceDetails&) override {}

private:
    SampleOrganizerProcessor& processor;

    // UI Components
    juce::Label titleLabel;
    juce::Label dropZoneLabel;
    juce::Label statusLabel;

    juce::TextButton chooseOutputBtn;
    juce::TextButton processBtn;
    juce::TextButton clearBtn;
    juce::TextButton addFolderBtn;
    juce::TextButton openFolderBtn;

    juce::ComboBox keySelector;
    juce::Label keyLabel;

    juce::Slider bpmSlider;
    juce::Label bpmLabel;
    juce::ToggleButton useHostBpmToggle;

    juce::TextEditor genreInput;
    juce::Label genreLabel;

    juce::ListBox sampleList;
    juce::StringArray sampleListItems;

    class SampleListModel : public juce::ListBoxModel
    {
    public:
        juce::StringArray items;
        int getNumRows() override { return items.size(); }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            if (selected)
                g.fillAll(juce::Colour(0xff2d2d2d));
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            g.setFont(12.0f);
            g.drawText(items[row], 10, 0, w - 10, h, juce::Justification::centredLeft);
        }
    };

    SampleListModel listModel;

    bool isDragOver = false;

    void refreshList();
    void updateStatus(const juce::String& msg);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleOrganizerEditor)
};
