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
    std::unique_ptr<juce::Drawable> closeIconDrawable;
    juce::DrawableButton closeBtn { "Close", juce::DrawableButton::ImageFitted };

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

    juce::Label namingFormatLabel;
    juce::ComboBox namingFormatCombo;
    juce::Label customPrefixLabel;
    juce::TextEditor customPrefixEditor;

    juce::ToggleButton overwriteDuplicatesToggle;

    juce::Viewport scrollViewport;
    struct ContentArea : juce::Component
    {
        void paint(juce::Graphics& g) override { g.fillAll(FinderTheme::creamBg); }
    };
    ContentArea content;

    static juce::StringArray getKeyList();
    static juce::StringArray getNamingFormatOptions();
    void updateCustomPrefixVisibility();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsOverlayComponent)
};
