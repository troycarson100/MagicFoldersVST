#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "FinderTheme.h"
#include "AssetLoader.h"

class SettingsOverlayComponent : public juce::Component
{
public:
    explicit SettingsOverlayComponent(SampleOrganizerProcessor& proc);
    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void()> onClose;
    void syncFromProcessor();

private:
    SampleOrganizerProcessor& processor;
    juce::DrawableButton backBtn { "Back", juce::DrawableButton::ImageFitted };
    juce::Label titleLabel;

    juce::Label outputFolderTitleLabel;
    juce::Label outputPathLabel;
    juce::TextButton browseOutputBtn;

    juce::ToggleButton autoDetectBpmToggle;
    juce::Label manualBpmLabel;
    juce::TextButton bpmDownBtn;
    juce::Label bpmValueLabel;
    juce::TextButton bpmUpBtn;

    juce::ToggleButton autoDetectKeyToggle;
    juce::Label manualKeyLabel;
    juce::ComboBox keySelector;

    juce::Label genreLabel;
    juce::TextEditor genreEditor;

    juce::Label namingFormatLabel;
    juce::ComboBox namingFormatCombo;
    juce::Label customPrefixLabel;
    juce::TextEditor customPrefixEditor;

    juce::ToggleButton overwriteDuplicatesToggle;
    juce::ToggleButton themeLightToggle;

    juce::Viewport scrollViewport;
    juce::Component content;

    static juce::StringArray getKeyList();
    static juce::StringArray getNamingFormatOptions();
    void updateCustomPrefixVisibility();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsOverlayComponent)
};
