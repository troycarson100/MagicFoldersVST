#include "SettingsOverlayComponent.h"

using namespace FinderTheme;

namespace
{
    constexpr int kHeaderHeight = 52;
    constexpr int kRowHeight = 28;
    constexpr int kSectionGap = 16;
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

SettingsOverlayComponent::SettingsOverlayComponent(SampleOrganizerProcessor& proc)
    : processor(proc)
{
    setAlwaysOnTop(true);
    std::unique_ptr<juce::Drawable> backArrow = AssetLoader::getWhiteArrowLeft();
    backBtn.setImages(backArrow.get());
    backBtn.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
    backBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(backBtn);

    titleLabel.setText("Settings", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, textOnDark);
    titleLabel.setVisible(false);
    addChildComponent(titleLabel);

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
    keySelector.setColour(juce::ComboBox::backgroundColourId, creamBg);
    keySelector.setColour(juce::ComboBox::textColourId, textCharcoal);
    keySelector.setColour(juce::ComboBox::outlineColourId, dividerLine);
    keySelector.onChange = [this] { processor.projectKey = keySelector.getText(); };
    addAndMakeVisible(keySelector);

    genreLabel.setText("Genre Tag", juce::dontSendNotification);
    genreLabel.setColour(juce::Label::textColourId, textCharcoal);
    addAndMakeVisible(genreLabel);
    genreEditor.setColour(juce::TextEditor::backgroundColourId, creamBg);
    genreEditor.setColour(juce::TextEditor::textColourId, textCharcoal);
    genreEditor.setColour(juce::TextEditor::outlineColourId, dividerLine);
    genreEditor.onTextChange = [this] { processor.defaultGenre = genreEditor.getText(); };
    addAndMakeVisible(genreEditor);

    namingFormatLabel.setText("Naming Format", juce::dontSendNotification);
    namingFormatLabel.setColour(juce::Label::textColourId, textCharcoal);
    addAndMakeVisible(namingFormatLabel);
    namingFormatCombo.addItemList(getNamingFormatOptions(), 1);
    namingFormatCombo.setColour(juce::ComboBox::backgroundColourId, creamBg);
    namingFormatCombo.setColour(juce::ComboBox::textColourId, textCharcoal);
    namingFormatCombo.setColour(juce::ComboBox::outlineColourId, dividerLine);
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
    customPrefixEditor.setColour(juce::TextEditor::outlineColourId, dividerLine);
    customPrefixEditor.onTextChange = [this] { processor.customPrefix = customPrefixEditor.getText(); };
    addAndMakeVisible(customPrefixEditor);

    overwriteDuplicatesToggle.setButtonText("Overwrite Duplicates");
    overwriteDuplicatesToggle.setColour(juce::ToggleButton::tickColourId, textCharcoal);
    overwriteDuplicatesToggle.setColour(juce::ToggleButton::textColourId, textCharcoal);
    overwriteDuplicatesToggle.onClick = [this] { processor.overwriteDuplicates = overwriteDuplicatesToggle.getToggleState(); };
    addAndMakeVisible(overwriteDuplicatesToggle);

    themeLightToggle.setButtonText("Theme: Light / Dark");
    themeLightToggle.setColour(juce::ToggleButton::tickColourId, textCharcoal);
    themeLightToggle.setColour(juce::ToggleButton::textColourId, textCharcoal);
    themeLightToggle.onClick = [this] { processor.themeLight = !themeLightToggle.getToggleState(); };
    addAndMakeVisible(themeLightToggle);

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
    content.addAndMakeVisible(genreLabel);
    content.addAndMakeVisible(genreEditor);
    content.addAndMakeVisible(namingFormatLabel);
    content.addAndMakeVisible(namingFormatCombo);
    content.addAndMakeVisible(customPrefixLabel);
    content.addAndMakeVisible(customPrefixEditor);
    content.addAndMakeVisible(overwriteDuplicatesToggle);
    content.addAndMakeVisible(themeLightToggle);

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
    genreEditor.setText(processor.defaultGenre, juce::dontSendNotification);
    namingFormatCombo.setSelectedItemIndex(juce::jlimit(0, 2, processor.namingFormat), juce::dontSendNotification);
    customPrefixEditor.setText(processor.customPrefix, juce::dontSendNotification);
    overwriteDuplicatesToggle.setToggleState(processor.overwriteDuplicates, juce::dontSendNotification);
    themeLightToggle.setToggleState(!processor.themeLight, juce::dontSendNotification);
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
    g.setColour(FinderTheme::headerBar);
    g.fillRect(headerBarRect);
    g.setColour(textOnDark);
    g.setFont(FinderTheme::interFont(17.0f, true));
    g.drawText("Settings", 44, 0, getWidth() - 100, kHeaderHeight, juce::Justification::centredLeft, true);
}

void SettingsOverlayComponent::resized()
{
    backBtn.setBounds(8, (kHeaderHeight - 28) / 2, 28, 28);
    titleLabel.setBounds(44, 0, 200, kHeaderHeight);

    const int pad = 24;
    const int labelW = 180;
    const int controlW = 240;
    const int cw = juce::jmax(getWidth(), labelW + controlW + pad * 2 + 40);
    int y = pad;

    scrollViewport.setBounds(0, kHeaderHeight, getWidth(), getHeight() - kHeaderHeight);
    content.setSize(cw, 680);

    outputFolderTitleLabel.setBounds(pad, y, labelW + controlW, kRowHeight);
    y += kRowHeight + 4;
    outputPathLabel.setBounds(pad, y, cw - pad * 2 - 90, kRowHeight);
    browseOutputBtn.setBounds(cw - pad - 84, y, 80, 24);
    y += kRowHeight + 4 + kSectionGap;

    autoDetectBpmToggle.setBounds(pad, y, cw - pad * 2, kRowHeight);
    y += kRowHeight + 4;
    manualBpmLabel.setBounds(pad, y, labelW, kRowHeight);
    y += kRowHeight + 4;
    bpmDownBtn.setBounds(pad, y, 36, 24);
    bpmValueLabel.setBounds(pad + 40, y, 48, 24);
    bpmUpBtn.setBounds(pad + 92, y, 36, 24);
    y += kRowHeight + 4 + kSectionGap;

    autoDetectKeyToggle.setBounds(pad, y, cw - pad * 2, kRowHeight);
    y += kRowHeight + 4;
    manualKeyLabel.setBounds(pad, y, labelW, kRowHeight);
    y += kRowHeight + 4;
    keySelector.setBounds(pad, y, controlW, 24);
    y += kRowHeight + 4 + kSectionGap;

    genreLabel.setBounds(pad, y, labelW, kRowHeight);
    y += kRowHeight + 4;
    genreEditor.setBounds(pad, y, controlW, 24);
    y += kRowHeight + 4 + kSectionGap;

    namingFormatLabel.setBounds(pad, y, labelW, kRowHeight);
    y += kRowHeight + 4;
    namingFormatCombo.setBounds(pad, y, controlW, 24);
    y += kRowHeight + 4;
    customPrefixLabel.setBounds(pad, y, labelW, kRowHeight);
    y += kRowHeight + 4;
    customPrefixEditor.setBounds(pad, y, controlW, 24);
    y += kRowHeight + 4 + kSectionGap;

    overwriteDuplicatesToggle.setBounds(pad, y, cw - pad * 2, kRowHeight);
    y += kRowHeight + 4;
    themeLightToggle.setBounds(pad, y, cw - pad * 2, kRowHeight);
}
