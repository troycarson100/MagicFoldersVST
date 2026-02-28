#include "PluginEditor.h"

static juce::Colour bgColor        = juce::Colour(0xff111111);
static juce::Colour panelColor     = juce::Colour(0xff1a1a1a);
static juce::Colour accentColor    = juce::Colour(0xff6c63ff);
static juce::Colour textColor      = juce::Colours::white;
static juce::Colour subTextColor   = juce::Colours::white.withAlpha(0.45f);
static juce::Colour dropZoneColor  = juce::Colour(0xff222222);
static juce::Colour borderColor    = juce::Colour(0xff333333);

SampleOrganizerEditor::SampleOrganizerEditor(SampleOrganizerProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(640, 520);
    setWantsKeyboardFocus(true);

    // Title
    titleLabel.setText("SAMPLE ORGANIZER", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, textColor);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    // Drop Zone Label (pass-through so drag/drop hits the editor)
    dropZoneLabel.setText("Drop WAV / AIF files here", juce::dontSendNotification);
    dropZoneLabel.setFont(juce::Font(13.0f));
    dropZoneLabel.setColour(juce::Label::textColourId, subTextColor);
    dropZoneLabel.setJustificationType(juce::Justification::centred);
    dropZoneLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(dropZoneLabel);

    // Status
    statusLabel.setText("No output folder selected.", juce::dontSendNotification);
    statusLabel.setFont(juce::Font(11.0f));
    statusLabel.setColour(juce::Label::textColourId, subTextColor);
    addAndMakeVisible(statusLabel);

    // Key
    keyLabel.setText("Key", juce::dontSendNotification);
    keyLabel.setFont(juce::Font(11.0f));
    keyLabel.setColour(juce::Label::textColourId, subTextColor);
    addAndMakeVisible(keyLabel);

    juce::StringArray keys = {"C Major","C Minor","C# Major","C# Minor","D Major","D Minor",
        "D# Major","D# Minor","E Major","E Minor","F Major","F Minor",
        "F# Major","F# Minor","G Major","G Minor","G# Major","G# Minor",
        "A Major","A Minor","A# Major","A# Minor","B Major","B Minor"};
    keySelector.addItemList(keys, 1);
    keySelector.setSelectedItemIndex(0);
    keySelector.setColour(juce::ComboBox::backgroundColourId, panelColor);
    keySelector.setColour(juce::ComboBox::textColourId, textColor);
    keySelector.setColour(juce::ComboBox::outlineColourId, borderColor);
    keySelector.onChange = [this] {
        processor.projectKey = keySelector.getText();
    };
    addAndMakeVisible(keySelector);

    // BPM
    bpmLabel.setText("BPM", juce::dontSendNotification);
    bpmLabel.setFont(juce::Font(11.0f));
    bpmLabel.setColour(juce::Label::textColourId, subTextColor);
    addAndMakeVisible(bpmLabel);

    bpmSlider.setRange(60, 200, 1);
    bpmSlider.setValue(120);
    bpmSlider.setSliderStyle(juce::Slider::IncDecButtons);
    bpmSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 50, 24);
    bpmSlider.setColour(juce::Slider::textBoxTextColourId, textColor);
    bpmSlider.setColour(juce::Slider::textBoxBackgroundColourId, panelColor);
    bpmSlider.setColour(juce::Slider::textBoxOutlineColourId, borderColor);
    bpmSlider.onValueChange = [this] {
        processor.projectBPM = (int)bpmSlider.getValue();
    };
    addAndMakeVisible(bpmSlider);

    useHostBpmToggle.setButtonText("Use project BPM");
    useHostBpmToggle.setColour(juce::ToggleButton::tickColourId, accentColor);
    useHostBpmToggle.setColour(juce::ToggleButton::textColourId, textColor);
    useHostBpmToggle.onClick = [this] {
        processor.useHostBpm = useHostBpmToggle.getToggleState();
    };
    addAndMakeVisible(useHostBpmToggle);

    // Genre
    genreLabel.setText("Genre", juce::dontSendNotification);
    genreLabel.setFont(juce::Font(11.0f));
    genreLabel.setColour(juce::Label::textColourId, subTextColor);
    addAndMakeVisible(genreLabel);

    genreInput.setText("Unknown");
    genreInput.setColour(juce::TextEditor::backgroundColourId, panelColor);
    genreInput.setColour(juce::TextEditor::textColourId, textColor);
    genreInput.setColour(juce::TextEditor::outlineColourId, borderColor);
    genreInput.onTextChange = [this] {
        processor.defaultGenre = genreInput.getText();
    };
    addAndMakeVisible(genreInput);

    // Buttons
    auto styleBtn = [&](juce::TextButton& btn, const juce::String& label, bool primary = false)
    {
        btn.setButtonText(label);
        btn.setColour(juce::TextButton::buttonColourId, primary ? accentColor : panelColor);
        btn.setColour(juce::TextButton::textColourOnId, textColor);
        btn.setColour(juce::TextButton::textColourOffId, textColor);
        addAndMakeVisible(btn);
    };

    styleBtn(chooseOutputBtn, "Choose Output Folder");
    styleBtn(processBtn, "Process Samples", true);
    styleBtn(clearBtn, "Clear");
    styleBtn(addFolderBtn, "Add Folder");
    styleBtn(openFolderBtn, "Open Folder");

    chooseOutputBtn.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser>("Select Output Folder",
            juce::File(),
            juce::String());
        chooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result != juce::File())
                {
                    processor.setOutputDirectory(result);
                    updateStatus("Output: " + result.getFullPathName());
                }
            });
    };

    processBtn.onClick = [this]
    {
        if (!processor.outputDirectory.isDirectory())
        {
            updateStatus("Please choose an output folder first.");
            return;
        }
        if (processor.queue.isEmpty())
        {
            updateStatus("Queue is empty. Add or drop samples first.");
            return;
        }
        const int count = processor.queue.size();
        updateStatus("Processing " + juce::String(count) + " samples…");
        refreshList();
        repaint();
        juce::Timer::callAfterDelay(150, [this, count]()
        {
            try
            {
                processor.processAll();
                updateStatus("Done! " + juce::String(processor.processed.size()) + " samples organized.");
            }
            catch (const std::exception& e)
            {
                updateStatus("Error: " + juce::String(e.what()));
            }
            catch (...)
            {
                updateStatus("Error: analysis failed.");
            }
            refreshList();
            repaint();
        });
    };

    clearBtn.onClick = [this]
    {
        processor.clearQueue();
        listModel.items.clear();
        sampleList.updateContent();
        updateStatus("Queue cleared.");
    };

    addFolderBtn.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser>("Select Folder of Samples",
            juce::File(),
            juce::String());
        chooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result != juce::File())
                {
                    processor.addFilesFromFolder(result);
                    refreshList();
                    updateStatus(juce::String(processor.queue.size()) + " samples queued from folder.");
                }
            });
    };

    openFolderBtn.onClick = [this]
    {
        if (processor.outputDirectory.isDirectory())
            processor.outputDirectory.revealToUser();
    };

    // List
    sampleList.setModel(&listModel);
    sampleList.setColour(juce::ListBox::backgroundColourId, panelColor);
    sampleList.setColour(juce::ListBox::outlineColourId, borderColor);
    sampleList.setOutlineThickness(1);
    addAndMakeVisible(sampleList);
}

SampleOrganizerEditor::~SampleOrganizerEditor() {}

void SampleOrganizerEditor::paint(juce::Graphics& g)
{
    g.fillAll(bgColor);

    // Drop zone
    juce::Rectangle<int> dropZone(20, 130, getWidth() - 40, 80);
    g.setColour(isDragOver ? accentColor.withAlpha(0.2f) : dropZoneColor);
    g.fillRoundedRectangle(dropZone.toFloat(), 8.0f);
    g.setColour(isDragOver ? accentColor : borderColor);
    g.drawRoundedRectangle(dropZone.toFloat(), 8.0f, 1.5f);

    // Divider
    g.setColour(borderColor);
    g.drawHorizontalLine(230, 20, getWidth() - 20);
}

void SampleOrganizerEditor::resized()
{
    int pad = 20;
    int w = getWidth();

    titleLabel.setBounds(pad, 16, 300, 28);
    statusLabel.setBounds(pad, 48, w - pad * 2, 20);

    // Controls row
    int cy = 76;
    keyLabel.setBounds(pad, cy, 30, 14);
    keySelector.setBounds(pad, cy + 16, 120, 28);

    bpmLabel.setBounds(pad + 135, cy, 30, 14);
    bpmSlider.setBounds(pad + 135, cy + 16, 70, 28);

    useHostBpmToggle.setBounds(pad + 212, cy + 16, 110, 28);

    genreLabel.setBounds(pad + 328, cy, 40, 14);
    genreInput.setBounds(pad + 328, cy + 16, 90, 28);

    chooseOutputBtn.setBounds(w - pad - 160, cy + 10, 160, 28);

    // Drop zone label centered
    dropZoneLabel.setBounds(20, 130, w - 40, 80);

    // Buttons row
    int by = 225;
    processBtn.setBounds(pad, by, 160, 32);
    clearBtn.setBounds(pad + 170, by, 70, 32);
    openFolderBtn.setBounds(w - pad - 90, by, 90, 32);
    addFolderBtn.setBounds(w - pad - 90 - 94, by, 90, 32);

    // Sample list
    sampleList.setBounds(pad, 270, w - pad * 2, getHeight() - 290);
}

static bool isAudioPath(const juce::String& path)
{
    juce::String lower(path.trim().toLowerCase());
    return lower.endsWith(".wav") || lower.endsWith(".aif") || lower.endsWith(".aiff");
}

static juce::StringArray expandDroppedPaths(const juce::StringArray& list)
{
    juce::StringArray out;
    for (const auto& s : list)
    {
        juce::String t = s.trim();
        if (t.isEmpty()) continue;
        if (t.contains(","))
        {
            for (const auto& part : juce::StringArray::fromTokens(t, ",", ""))
            {
                juce::String p = part.trim();
                if (p.isNotEmpty()) out.add(p);
            }
        }
        else if (t.contains("\n"))
        {
            for (const auto& part : juce::StringArray::fromTokens(t, "\n\r", ""))
            {
                juce::String p = part.trim();
                if (p.isNotEmpty()) out.add(p);
            }
        }
        else
            out.add(t);
    }
    return out;
}

bool SampleOrganizerEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    juce::StringArray expanded = expandDroppedPaths(files);
    for (const auto& f : expanded)
        if (isAudioPath(f))
            return true;
    return false;
}

void SampleOrganizerEditor::fileDragEnter(const juce::StringArray&, int x, int y)
{
    juce::Rectangle<int> dropZone(20, 130, getWidth() - 40, 80);
    bool inZone = dropZone.contains(x, y);
    if (inZone && !isDragOver)
    {
        isDragOver = true;
        repaint();
    }
}

void SampleOrganizerEditor::fileDragMove(const juce::StringArray&, int x, int y)
{
    juce::Rectangle<int> dropZone(20, 130, getWidth() - 40, 80);
    bool inZone = dropZone.contains(x, y);
    if (inZone != isDragOver)
    {
        isDragOver = inZone;
        repaint();
    }
}

void SampleOrganizerEditor::fileDragExit(const juce::StringArray&)
{
    if (isDragOver)
    {
        isDragOver = false;
        repaint();
    }
}

void SampleOrganizerEditor::filesDropped(const juce::StringArray& files, int, int)
{
    isDragOver = false;
    repaint();

    // Multi-drag from Ableton often yields only one path (host pasteboard limitation).
    // Use "Add Folder" or drag from Finder to queue multiple files at once.
    juce::StringArray expanded = expandDroppedPaths(files);
    juce::Array<juce::File> fileArray;
    for (const auto& f : expanded)
    {
        if (!isAudioPath(f))
            continue;
        juce::File file(f);
        if (file.existsAsFile())
            fileArray.add(file);
    }
    processor.addFiles(fileArray);
    refreshList();
    updateStatus(juce::String(processor.queue.size()) + " samples queued. Hit Process.");
}

void SampleOrganizerEditor::refreshList()
{
    listModel.items.clear();
    for (auto& s : processor.queue)
        listModel.items.add("[ QUEUED ]  " + s.name + "  →  " + s.category + " / " + s.type);
    for (auto& s : processor.processed)
        listModel.items.add(juce::String(s.success ? "[ DONE ]  " : "[ FAILED ]  ") + s.name + "  →  " + s.outputPath);
    sampleList.updateContent();
}

void SampleOrganizerEditor::updateStatus(const juce::String& msg)
{
    statusLabel.setText(msg, juce::dontSendNotification);
}
