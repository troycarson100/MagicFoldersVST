#include "SettingsOverlayComponent.h"

using namespace FinderTheme;

namespace
{
    constexpr int kHeaderHeight = 52;
    constexpr int kContentPad = 32;
    constexpr int kSectionGap = 24;
    constexpr int kRowHeight = 28;
    constexpr int kCardRadius = 8;
}

void SettingsOverlayComponent::ContentArea::paint(juce::Graphics& g)
{
    g.fillAll(creamBg);
    for (const auto& r : sectionRects)
    {
        auto fr = r.toFloat().reduced(1.0f);
        g.setColour(settingsCardBg);
        g.fillRoundedRectangle(fr, (float)kCardRadius);
        g.setColour(settingsDivider);
        g.drawRoundedRectangle(fr, (float)kCardRadius, 1.0f);
    }
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
    bool show = (namingFormatDropdown.getSelectedIndex() == 2);
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
    outputFolderTitleLabel.setFont(FinderTheme::interFont(14.0f, true));
    addAndMakeVisible(outputFolderTitleLabel);
    outputPathLabel.setColour(juce::Label::textColourId, textCharcoal);
    outputPathLabel.setFont(FinderTheme::interFont(12.0f));
    outputPathLabel.setText("(not set)", juce::dontSendNotification);
    addAndMakeVisible(outputPathLabel);
    browseOutputBtn.setButtonText("Browse");
    browseOutputBtn.setColour(juce::TextButton::buttonColourId, settingsAccent);
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

    tempoSectionLabel.setText("Tempo", juce::dontSendNotification);
    tempoSectionLabel.setColour(juce::Label::textColourId, textCharcoal);
    tempoSectionLabel.setFont(FinderTheme::interFont(14.0f, true));
    addAndMakeVisible(tempoSectionLabel);
    autoDetectBpmToggle.setButtonText("Auto-detect BPM");
    autoDetectBpmToggle.onClick = [this] {
        processor.useHostBpm = !autoDetectBpmToggle.getToggleState();
        bpmStepper.setEnabled(autoDetectBpmToggle.getToggleState());
    };
    addAndMakeVisible(autoDetectBpmToggle);
    bpmStepper.setRange(60, 200);
    bpmStepper.onValueChange = [this](int v) { processor.projectBPM = v; };
    bpmStepper.onDoubleClick = [this] { showBpmEditor(); };
    addAndMakeVisible(bpmStepper);
    content.addChildComponent(bpmPopupEditor);
    bpmPopupEditor.setMultiLine(false);
    bpmPopupEditor.setJustification(juce::Justification::centred);
    bpmPopupEditor.setColour(juce::TextEditor::backgroundColourId, settingsAccent);
    bpmPopupEditor.setColour(juce::TextEditor::textColourId, textOnDark);
    bpmPopupEditor.setColour(juce::TextEditor::outlineColourId, settingsDivider);
    bpmPopupEditor.onReturnKey = [this] { hideBpmEditor(); };
    bpmPopupEditor.onEscapeKey = [this] { hideBpmEditor(); };
    bpmPopupEditor.onFocusLost = [this] { hideBpmEditor(); };

    autoDetectKeyToggle.setButtonText("Auto-detect Key");
    autoDetectKeyToggle.onClick = [this] { processor.useProjectKey = !autoDetectKeyToggle.getToggleState(); };
    addAndMakeVisible(autoDetectKeyToggle);
    keyDropdown.setOptions(getKeyList());
    keyDropdown.onChange = [this](int idx) {
        processor.projectKey = getKeyList()[idx];
    };
    addAndMakeVisible(keyDropdown);

    namingFormatDropdown.setOptions(getNamingFormatOptions());
    namingFormatDropdown.onChange = [this](int idx) {
        processor.namingFormat = idx;
        updateCustomPrefixVisibility();
    };
    addAndMakeVisible(namingFormatDropdown);
    customPrefixLabel.setText("Custom Prefix", juce::dontSendNotification);
    customPrefixLabel.setColour(juce::Label::textColourId, textCharcoal);
    customPrefixLabel.setFont(FinderTheme::interFont(12.0f));
    addAndMakeVisible(customPrefixLabel);
    customPrefixEditor.setColour(juce::TextEditor::backgroundColourId, settingsAccent);
    customPrefixEditor.setColour(juce::TextEditor::textColourId, textOnDark);
    customPrefixEditor.setColour(juce::TextEditor::outlineColourId, settingsDivider);
    customPrefixEditor.onTextChange = [this] { processor.customPrefix = customPrefixEditor.getText(); };
    addAndMakeVisible(customPrefixEditor);

    behaviorSectionLabel.setText("Behavior", juce::dontSendNotification);
    behaviorSectionLabel.setColour(juce::Label::textColourId, textCharcoal);
    behaviorSectionLabel.setFont(FinderTheme::interFont(14.0f, true));
    addAndMakeVisible(behaviorSectionLabel);
    overwriteDuplicatesToggle.setButtonText("Overwrite Duplicates");
    overwriteDuplicatesToggle.onClick = [this] { processor.overwriteDuplicates = overwriteDuplicatesToggle.getToggleState(); };
    addAndMakeVisible(overwriteDuplicatesToggle);

    scrollViewport.setViewedComponent(&content, false);
    scrollViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(scrollViewport);

    content.addAndMakeVisible(outputFolderTitleLabel);
    content.addAndMakeVisible(outputPathLabel);
    content.addAndMakeVisible(browseOutputBtn);
    content.addAndMakeVisible(tempoSectionLabel);
    content.addAndMakeVisible(autoDetectBpmToggle);
    content.addAndMakeVisible(bpmStepper);
    content.addAndMakeVisible(autoDetectKeyToggle);
    content.addAndMakeVisible(keyDropdown);
    content.addAndMakeVisible(namingFormatDropdown);
    content.addAndMakeVisible(customPrefixLabel);
    content.addAndMakeVisible(customPrefixEditor);
    content.addAndMakeVisible(behaviorSectionLabel);
    content.addAndMakeVisible(overwriteDuplicatesToggle);

    syncFromProcessor();
    updateCustomPrefixVisibility();
}

void SettingsOverlayComponent::syncFromProcessor()
{
    autoDetectBpmToggle.setToggleState(!processor.useHostBpm, juce::dontSendNotification);
    bpmStepper.setEnabled(autoDetectBpmToggle.getToggleState());
    bpmStepper.setValue(processor.projectBPM);
    autoDetectKeyToggle.setToggleState(!processor.useProjectKey, juce::dontSendNotification);
    int keyIndex = getKeyList().indexOf(processor.projectKey);
    keyDropdown.setSelectedIndex(keyIndex >= 0 ? keyIndex : 0, juce::dontSendNotification);
    namingFormatDropdown.setSelectedIndex(juce::jlimit(0, 2, processor.namingFormat), juce::dontSendNotification);
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

void SettingsOverlayComponent::showBpmEditor()
{
    if (!autoDetectBpmToggle.getToggleState()) return;
    bpmPopupEditor.setBounds(bpmStepper.getBounds());
    bpmPopupEditor.setText(juce::String(processor.projectBPM), false);
    bpmPopupEditor.setVisible(true);
    bpmPopupEditor.grabKeyboardFocus();
    bpmPopupEditor.selectAll();
}

void SettingsOverlayComponent::hideBpmEditor()
{
    if (!bpmPopupEditor.isVisible()) return;
    int v = bpmPopupEditor.getText().getIntValue();
    if (v >= 60 && v <= 200)
    {
        processor.projectBPM = v;
        bpmStepper.setValue(v);
    }
    bpmPopupEditor.setVisible(false);
    bpmPopupEditor.setText("", false);
}

void SettingsOverlayComponent::paint(juce::Graphics& g)
{
    g.fillAll(creamBg);
    juce::Rectangle<int> headerBarRect(0, 0, getWidth(), kHeaderHeight);
    g.setColour(topBar);
    g.fillRect(headerBarRect);
    g.setColour(settingsDivider);
    g.fillRect(0, kHeaderHeight - 1, getWidth(), 1);
    g.setColour(textOnDark);
    g.setFont(FinderTheme::interFont(15.0f, true));
    g.drawText("Settings", 20, 0, getWidth() - 80, kHeaderHeight, juce::Justification::centredLeft, true);
}

void SettingsOverlayComponent::resized()
{
    closeBtn.setBounds(getWidth() - 44, (kHeaderHeight - 28) / 2, 28, 28);

    const int labelW = 160;
    const int controlW = 280;
    const int cw = juce::jmax(getWidth(), labelW + controlW + kContentPad * 2 + 40);
    int y = kContentPad;
    juce::Array<juce::Rectangle<int>> sectionRects;

    scrollViewport.setBounds(0, kHeaderHeight, getWidth(), getHeight() - kHeaderHeight);
    content.setSize(cw, 620);

    constexpr int kExtraPad = 14;

    // Output section
    int sectionTop = y;
    outputFolderTitleLabel.setBounds(kContentPad + 12, y, cw - kContentPad * 2, 22);
    y += 26;
    outputPathLabel.setBounds(kContentPad + 12, y, cw - kContentPad * 2, kRowHeight);
    y += kRowHeight + 8;
    browseOutputBtn.setBounds(kContentPad + 12, y, 84, 28);
    y += 36 + kExtraPad;
    sectionRects.add(juce::Rectangle<int>(kContentPad, sectionTop - 8, cw - kContentPad * 2, y - sectionTop + 4));
    y += kSectionGap;

    // Tempo section
    sectionTop = y;
    tempoSectionLabel.setBounds(kContentPad + 12, y, cw - kContentPad * 2, 22);
    y += 26;
    autoDetectBpmToggle.setBounds(kContentPad + 12, y, cw - kContentPad * 2 - 24, 32);
    y += 38;
    bpmStepper.setBounds(kContentPad + 12, y, 180, 36);
    if (bpmPopupEditor.isVisible())
        bpmPopupEditor.setBounds(bpmStepper.getBounds());
    y += 36 + kExtraPad;
    sectionRects.add(juce::Rectangle<int>(kContentPad, sectionTop - 8, cw - kContentPad * 2, y - sectionTop + 4));
    y += kSectionGap;

    // Key section (no "Key Detection" or "Manual Key" labels)
    sectionTop = y;
    autoDetectKeyToggle.setBounds(kContentPad + 12, y, cw - kContentPad * 2 - 24, 32);
    y += 38;
    keyDropdown.setBounds(kContentPad + 12, y, controlW, 32);
    y += 32 + kExtraPad;
    sectionRects.add(juce::Rectangle<int>(kContentPad, sectionTop - 8, cw - kContentPad * 2, y - sectionTop + 4));
    y += kSectionGap;

    // Naming (no "Naming" or "Naming Format" labels)
    sectionTop = y;
    namingFormatDropdown.setBounds(kContentPad + 12, y, controlW, 32);
    y += 32 + kExtraPad;
    customPrefixLabel.setBounds(kContentPad + 12, y, labelW, kRowHeight);
    y += kRowHeight + 4;
    customPrefixEditor.setBounds(kContentPad + 12, y, controlW, 32);
    y += 32 + kExtraPad;
    sectionRects.add(juce::Rectangle<int>(kContentPad, sectionTop - 8, cw - kContentPad * 2, y - sectionTop + 4));
    y += kSectionGap;

    // Behavior section
    sectionTop = y;
    behaviorSectionLabel.setBounds(kContentPad + 12, y, cw - kContentPad * 2, 22);
    y += 26;
    overwriteDuplicatesToggle.setBounds(kContentPad + 12, y, cw - kContentPad * 2 - 24, 32);
    y += 44;
    sectionRects.add(juce::Rectangle<int>(kContentPad, sectionTop - 8, cw - kContentPad * 2, y - sectionTop + 4));

    content.setSectionRects(sectionRects);
}
