#include "SettingsOverlayComponent.h"

using namespace FinderTheme;

namespace
{
    constexpr int kHeaderHeight = 52;
    constexpr int kRowHeight = 28;
    constexpr int kSectionGap = 20;
    constexpr int kContentPad = 28;
}

juce::StringArray SettingsOverlayComponent::getKeyList()
{
    return { "C Major", "C Minor", "C# Major", "C# Minor", "D Major", "D Minor",
             "D# Major", "D# Minor", "E Major", "E Minor", "F Major", "F Minor",
             "F# Major", "F# Minor", "G Major", "G Minor", "G# Major", "G# Minor",
             "A Major", "A Minor", "A# Major", "A# Minor", "B Major", "B Minor" };
}

juce::StringArray SettingsOverlayComponent::getNamingFormatOptions()
{
    return { "Category_Key_BPM_Index", "Index_Category_Key_BPM", "Custom prefix + Index" };
}

void SettingsOverlayComponent::updateCustomPrefixVisibility()
{
    bool show = (namingFormatCombo.getSelectedItemIndex() == 2);
    customPrefixLabel.setVisible(show);
    customPrefixEditor.setVisible(show);
}

namespace
{
    std::unique_ptr<juce::Drawable> createCloseXIcon()
    {
        juce::Path p;
        p.addLineSegment(juce::Line<float>(9.f, 9.f, 15.f, 15.f), 0.f);
        p.addLineSegment(juce::Line<float>(15.f, 9.f, 9.f, 15.f), 0.f);
        juce::Path strokePath;
        juce::PathStrokeType(1.8f).createStrokedPath(strokePath, p);
        auto drawable = std::make_unique<juce::DrawablePath>();
        static_cast<juce::DrawablePath*>(drawable.get())->setPath(strokePath);
        static_cast<juce::DrawablePath*>(drawable.get())->setFill(juce::FillType(FinderTheme::textOnDark));
        return drawable;
    }
}

SettingsOverlayComponent::SettingsOverlayComponent(SampleOrganizerProcessor& proc)
    : processor(proc)
{
    setAlwaysOnTop(true);
    closeIconDrawable = createCloseXIcon();
    closeBtn.setImages(closeIconDrawable.get());
    closeBtn.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
    closeBtn.setColour(juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentBlack);
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeBtn);

    outputFolderTitleLabel.setText("Output Folder", juce::dontSendNotification);
    outputFolderTitleLabel.setColour(juce::Label::textColourId, textCharcoal);
    outputFolderTitleLabel.setFont(FinderTheme::interFont(13.0f, true));
    addAndMakeVisible(outputFolderTitleLabel);
    outputPathLabel.setColour(juce::Label::textColourId, textCharcoal);
    outputPathLabel.setText("(not set)", juce::dontSendNotification);
    addAndMakeVisible(outputPathLabel);
    browseOutputBtn.setButtonText("Browse");
    browseOutputBtn.setColour(juce::TextButton::buttonColourId, processBtnBg);
    browseOutputBtn.setColour(juce::TextButton::textColourOffId, textOnDark);
    browseOutputBtn.onClick = [this] {
        auto chooser = std::make_shared<juce::FileChooser>("Magic Folders root directory",
            processor.outputDirectory.exists() ? processor.outputDirectory : juce::File(), juce::String());
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser](const juce::FileChooser& fc) {
                auto r = fc.getResult();
                if (r != juce::File()) {
                    processor.setOutputDirectory(r);
                    juce::String p = r.getFullPathName();
                    if (p.length() > 60) p = p.dropLastCharacters(p.length() - 57) + "...";
                    outputPathLabel.setText(p, juce::dontSendNotification);
                }
            });
    };
    addAndMakeVisible(browseOutputBtn);

    autoDetectBpmToggle.setButtonText("Auto-detect BPM");
    autoDetectBpmToggle.setColour(juce::ToggleButton::tickColourId, textCharcoal);
    autoDetectBpmToggle.setColour(juce::ToggleButton::textColourId, textCharcoal);
    autoDetectBpmToggle.onClick = [this] { processor.useHostBpm = !autoDetectBpmToggle.getToggleState(); };
    addAndMakeVisible(autoDetectBpmToggle);

    manualBpmLabel.setText("Manual BPM (60–200)", juce::dontSendNotification);
    manualBpmLabel.setColour(juce::Label::textColourId, textCharcoal);
    addAndMakeVisible(manualBpmLabel);
    bpmDownBtn.setButtonText("-");
    bpmDownBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    bpmDownBtn.setColour(juce::TextButton::textColourOffId, textCharcoal);
    bpmDownBtn.onClick = [this] {
        processor.projectBPM = juce::jmax(60, processor.projectBPM - 1);
        bpmValueLabel.setText(juce::String(processor.projectBPM), juce::dontSendNotification);
    };
    bpmUpBtn.setButtonText("+");
    bpmUpBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    bpmUpBtn.setColour(juce::TextButton::textColourOffId, textCharcoal);
    bpmUpBtn.onClick = [this] {
        processor.projectBPM = juce::jmin(200, processor.projectBPM + 1);
        bpmValueLabel.setText(juce::String(processor.projectBPM), juce::dontSendNotification);
    };
    bpmValueLabel.setColour(juce::Label::textColourId, textCharcoal);
    addAndMakeVisible(bpmDownBtn);
    addAndMakeVisible(bpmValueLabel);
    addAndMakeVisible(bpmUpBtn);

    autoDetectKeyToggle.setButtonText("Auto-detect Key");
    autoDetectKeyToggle.setColour(juce::ToggleButton::tickColourId, textCharcoal);
    autoDetectKeyToggle.setColour(juce::ToggleButton::textColourId, textCharcoal);
    autoDetectKeyToggle.onClick = [this] { processor.useProjectKey = !autoDetectKeyToggle.getToggleState(); };
    addAndMakeVisible(autoDetectKeyToggle);

    manualKeyLabel.setText("Manual Key", juce::dontSendNotification);
    manualKeyLabel.setColour(juce::Label::textColourId, textCharcoal);
    addAndMakeVisible(manualKeyLabel);
    keySelector.addItemList(getKeyList(), 1);
    keySelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xffE0DDD6));
    keySelector.setColour(juce::ComboBox::textColourId, textCharcoal);
    keySelector.setColour(juce::ComboBox::outlineColourId, topBar);
    keySelector.setColour(juce::ComboBox::buttonColourId, juce::Colour(0xff393E46));
    keySelector.setColour(juce::ComboBox::arrowColourId, textOnDark);
    keySelector.onChange = [this] { processor.projectKey = keySelector.getText(); };
    addAndMakeVisible(keySelector);

    namingFormatLabel.setText("Naming Format", juce::dontSendNotification);
    namingFormatLabel.setColour(juce::Label::textColourId, textCharcoal);
    addAndMakeVisible(namingFormatLabel);
    namingFormatCombo.addItemList(getNamingFormatOptions(), 1);
    namingFormatCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xffE0DDD6));
    namingFormatCombo.setColour(juce::ComboBox::textColourId, textCharcoal);
    namingFormatCombo.setColour(juce::ComboBox::outlineColourId, topBar);
    namingFormatCombo.setColour(juce::ComboBox::buttonColourId, juce::Colour(0xff393E46));
    namingFormatCombo.setColour(juce::ComboBox::arrowColourId, textOnDark);
    namingFormatCombo.onChange = [this] {
        processor.namingFormat = namingFormatCombo.getSelectedItemIndex();
        updateCustomPrefixVisibility();
    };
    addAndMakeVisible(namingFormatCombo);

    customPrefixLabel.setText("Custom Prefix", juce::dontSendNotification);
    customPrefixLabel.setColour(juce::Label::textColourId, textCharcoal);
    addAndMakeVisible(customPrefixLabel);
    customPrefixEditor.setColour(juce::TextEditor::backgroundColourId, creamBg);
    customPrefixEditor.setColour(juce::TextEditor::textColourId, textCharcoal);
    customPrefixEditor.setColour(juce::TextEditor::outlineColourId, topBar);
    customPrefixEditor.onTextChange = [this] { processor.customPrefix = customPrefixEditor.getText(); };
    addAndMakeVisible(customPrefixEditor);

    overwriteDuplicatesToggle.setButtonText("Overwrite Duplicates");
    overwriteDuplicatesToggle.setColour(juce::ToggleButton::tickColourId, textCharcoal);
    overwriteDuplicatesToggle.setColour(juce::ToggleButton::textColourId, textCharcoal);
    overwriteDuplicatesToggle.onClick = [this] { processor.overwriteDuplicates = overwriteDuplicatesToggle.getToggleState(); };
    addAndMakeVisible(overwriteDuplicatesToggle);

    scrollViewport.setViewedComponent(&content, false);
    scrollViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(scrollViewport);

    content.addAndMakeVisible(outputFolderTitleLabel);
    content.addAndMakeVisible(outputPathLabel);
    content.addAndMakeVisible(browseOutputBtn);
    content.addAndMakeVisible(autoDetectBpmToggle);
    content.addAndMakeVisible(manualBpmLabel);
    content.addAndMakeVisible(bpmDownBtn);
    content.addAndMakeVisible(bpmValueLabel);
    content.addAndMakeVisible(bpmUpBtn);
    content.addAndMakeVisible(autoDetectKeyToggle);
    content.addAndMakeVisible(manualKeyLabel);
    content.addAndMakeVisible(keySelector);
    content.addAndMakeVisible(namingFormatLabel);
    content.addAndMakeVisible(namingFormatCombo);
    content.addAndMakeVisible(customPrefixLabel);
    content.addAndMakeVisible(customPrefixEditor);
    content.addAndMakeVisible(overwriteDuplicatesToggle);

    syncFromProcessor();
    updateCustomPrefixVisibility();
}

void SettingsOverlayComponent::syncFromProcessor()
{
    autoDetectBpmToggle.setToggleState(!processor.useHostBpm, juce::dontSendNotification);
    bpmValueLabel.setText(juce::String(processor.projectBPM), juce::dontSendNotification);
    autoDetectKeyToggle.setToggleState(!processor.useProjectKey, juce::dontSendNotification);
    int keyIndex = getKeyList().indexOf(processor.projectKey);
    keySelector.setSelectedItemIndex(keyIndex >= 0 ? keyIndex : 0, juce::dontSendNotification);
    namingFormatCombo.setSelectedItemIndex(juce::jlimit(0, 2, processor.namingFormat), juce::dontSendNotification);
    customPrefixEditor.setText(processor.customPrefix, juce::dontSendNotification);
    overwriteDuplicatesToggle.setToggleState(processor.overwriteDuplicates, juce::dontSendNotification);
    if (processor.outputDirectory.isDirectory()) {
        juce::String p = processor.outputDirectory.getFullPathName();
        if (p.length() > 60) p = p.dropLastCharacters(p.length() - 57) + "...";
        outputPathLabel.setText(p, juce::dontSendNotification);
    } else {
        outputPathLabel.setText("(not set)", juce::dontSendNotification);
    }
    updateCustomPrefixVisibility();
}

void SettingsOverlayComponent::paint(juce::Graphics& g)
{
    g.fillAll(creamBg);
    juce::Rectangle<int> headerBarRect(0, 0, getWidth(), kHeaderHeight);
    g.setColour(FinderTheme::topBar);
    g.fillRect(headerBarRect);
    g.setColour(textOnDark);
    g.setFont(FinderTheme::interFont(15.0f, true));
    g.drawText("Settings", 20, 0, getWidth() - 80, kHeaderHeight, juce::Justification::centredLeft, true);
}

void SettingsOverlayComponent::resized()
{
    closeBtn.setBounds(getWidth() - 44, (kHeaderHeight - 28) / 2, 28, 28);

    const int labelW = 180;
    const int controlW = 260;
    const int cw = juce::jmax(getWidth(), labelW + controlW + kContentPad * 2 + 40);
    int y = kContentPad;

    scrollViewport.setBounds(0, kHeaderHeight, getWidth(), getHeight() - kHeaderHeight);
    content.setSize(cw, 580);

    outputFolderTitleLabel.setBounds(kContentPad, y, labelW + controlW, kRowHeight);
    y += kRowHeight + 6;
    outputPathLabel.setBounds(kContentPad, y, cw - kContentPad * 2 - 94, kRowHeight);
    browseOutputBtn.setBounds(cw - kContentPad - 84, y, 80, 26);
    y += kRowHeight + 6 + kSectionGap;

    autoDetectBpmToggle.setBounds(kContentPad, y, cw - kContentPad * 2, kRowHeight);
    y += kRowHeight + 6;
    manualBpmLabel.setBounds(kContentPad, y, labelW, kRowHeight);
    y += kRowHeight + 6;
    bpmDownBtn.setBounds(kContentPad, y, 36, 26);
    bpmValueLabel.setBounds(kContentPad + 40, y, 48, 26);
    bpmUpBtn.setBounds(kContentPad + 92, y, 36, 26);
    y += kRowHeight + 6 + kSectionGap;

    autoDetectKeyToggle.setBounds(kContentPad, y, cw - kContentPad * 2, kRowHeight);
    y += kRowHeight + 6;
    manualKeyLabel.setBounds(kContentPad, y, labelW, kRowHeight);
    y += kRowHeight + 6;
    keySelector.setBounds(kContentPad, y, controlW, 26);
    y += kRowHeight + 6 + kSectionGap;

    namingFormatLabel.setBounds(kContentPad, y, labelW, kRowHeight);
    y += kRowHeight + 6;
    namingFormatCombo.setBounds(kContentPad, y, controlW, 26);
    y += kRowHeight + 6;
    customPrefixLabel.setBounds(kContentPad, y, labelW, kRowHeight);
    y += kRowHeight + 6;
    customPrefixEditor.setBounds(kContentPad, y, controlW, 26);
    y += kRowHeight + 6 + kSectionGap;

    overwriteDuplicatesToggle.setBounds(kContentPad, y, cw - kContentPad * 2, kRowHeight);
}
