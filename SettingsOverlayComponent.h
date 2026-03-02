#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "FinderTheme.h"
#include "AssetLoader.h"
#include "SettingsComponents.h"

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

    juce::Label batchPlusFolderTitleLabel;
    juce::Label batchPlusPathLabel;
    juce::TextButton browseBatchPlusBtn;

    juce::Label tempoSectionLabel;
    SettingsToggleComponent autoDetectBpmToggle;
    SettingsStepperComponent bpmStepper;
    juce::TextEditor bpmPopupEditor;

    SettingsToggleComponent autoDetectKeyToggle;
    SettingsDropdownComponent keyDropdown;

    SettingsDropdownComponent namingFormatDropdown;
    SettingsToggleComponent generateFunNamesToggle;
    juce::Label customPrefixLabel;
    juce::TextEditor customPrefixEditor;

    juce::Label behaviorSectionLabel;
    SettingsToggleComponent overwriteDuplicatesToggle;

    juce::Viewport scrollViewport;
    struct ContentArea : juce::Component
    {
        void paint(juce::Graphics& g) override;
        void setSectionRects(const juce::Array<juce::Rectangle<int>>& rects) { sectionRects = rects; }
        juce::Array<juce::Rectangle<int>> sectionRects;
    };
    ContentArea content;

    static juce::StringArray getKeyList();
    static juce::StringArray getNamingFormatOptions();
    void updateCustomPrefixVisibility();
    void showBpmEditor();
    void hideBpmEditor();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsOverlayComponent)
};
