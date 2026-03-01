#include "PluginEditor.h"

using namespace FinderTheme;

namespace
{
    struct ProcessButtonLAF : juce::LookAndFeel_V4
    {
        void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool isOver, bool isDown) override
        {
            juce::Rectangle<float> rect = b.getLocalBounds().toFloat();
            g.setColour(isOver ? processBtnHover : processBtnBg);
            g.fillRect(rect);
        }
    };
    ProcessButtonLAF s_processButtonLAF;
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

static bool isAudioPath(const juce::String& path)
{
    juce::String lower(path.trim().toLowerCase());
    return lower.endsWith(".wav") || lower.endsWith(".aif") || lower.endsWith(".aiff");
}

juce::StringArray SampleOrganizerEditor::expandDroppedPaths(const juce::StringArray& list) { return ::expandDroppedPaths(list); }
bool SampleOrganizerEditor::isAudioPath(const juce::String& path) { return ::isAudioPath(path); }

// --- PackListModel ---
int SampleOrganizerEditor::PackListModel::getNumRows()
{
    return editor.packNames.size();
}

void SampleOrganizerEditor::PackListModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    g.fillAll(FinderTheme::sidebarDarkBar);
    if (selected)
        g.fillAll(FinderTheme::sidebarRowSelected);
    else if (row == editor.hoveredPackRow)
        g.fillAll(FinderTheme::sidebarRowHover);
    g.setColour(FinderTheme::textOnDark);
    g.setFont(selected ? juce::Font(11.0f, juce::Font::bold) : juce::Font(11.0f, juce::Font::plain));
    juce::String name = row < editor.packNames.size() ? editor.packNames[row] : juce::String();
    if (name.length() > 28)
        name = name.dropLastCharacters(name.length() - 25) + "...";
    const int padH = editor.kPackPaddingH;
    const int padV = editor.kPackPaddingV;
    g.drawText(name, padH, padV, w - padH - 32, h - 2 * padV, juce::Justification::centredLeft);
    if (selected && editor.arrowRightDrawable)
        editor.arrowRightDrawable->drawWithin(g, juce::Rectangle<float>((float)(w - 24), (float)((h - 14) / 2), 14.0f, 14.0f), juce::RectanglePlacement::centred, 1.0f);
}

void SampleOrganizerEditor::PackListModel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= editor.packNames.size()) return;
    editor.selectedPackIndex = row;
    editor.columnPath.clear();
    editor.pathHistory.clear();
    editor.pathForward.clear();
    if (row >= 0 && row < editor.packDirs.size())
        editor.columnBrowser.setRootFolder(editor.packDirs[row]);
    editor.columnBrowser.setPath(editor.columnPath);
    editor.updateBreadcrumb();
    editor.packList.repaint();
}

// --- SampleOrganizerEditor ---
SampleOrganizerEditor::SampleOrganizerEditor(SampleOrganizerProcessor& p)
    : AudioProcessorEditor(&p), processor(p), packListModel(*this)
{
    settingsOverlay = std::make_unique<SettingsOverlayComponent>(processor);
    setSize(920, 600);
    setResizeLimits(860, 580, 4096, 4096);
    setWantsKeyboardFocus(true);

    formatManager.registerBasicFormats();
    sourcePlayer.setSource(&transportSource);
    deviceManager.initialiseWithDefaultDevices(0, 2);

    // Sidebar assets and plus button
    logoDrawable = AssetLoader::getLogo();
    plusDrawable = AssetLoader::getPlusIcon();
    arrowRightDrawable = AssetLoader::getWhiteArrowRight();
    plusBtn.setImages(plusDrawable.get());
    plusBtn.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
    plusBtn.setColour(juce::DrawableButton::backgroundOnColourId, headerBar);
    plusBtn.onClick = [this] {
        if (!processor.outputDirectory.isDirectory())
        {
            if (settingsOverlay->isVisible())
                settingsOverlay->setVisible(false);
            return;
        }
        refreshPackList();
        juce::String name = "New Pack " + juce::String(packDirs.size() + 1);
        juce::File newDir = processor.outputDirectory.getChildFile(name);
        if (newDir.createDirectory())
        {
            refreshPackList();
            for (int i = 0; i < packDirs.size(); ++i)
            {
                if (packDirs[i] == newDir) { selectedPackIndex = i; break; }
            }
            columnPath.clear();
            columnBrowser.setRootFolder(newDir);
            columnBrowser.setPath(columnPath);
            updateBreadcrumb();
            packList.repaint();
        }
    };
    addAndMakeVisible(plusBtn);

    packList.setModel(&packListModel);
    packList.setRowHeight(kPackRowHeight);
    packList.setColour(juce::ListBox::backgroundColourId, FinderTheme::sidebarDarkBar);
    packListHoverListener = std::make_unique<PackListHoverListener>();
    packListHoverListener->editor = this;
    packList.addMouseListener(packListHoverListener.get(), true);
    packList.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    packList.setOutlineThickness(0);
    addAndMakeVisible(packList);

    sidebarPlaceholderBtn.setButtonText("Set destination folder in\nSettings (gear icon) " + juce::String::fromUTF8("\xe2\x86\x92"));
    sidebarPlaceholderBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    sidebarPlaceholderBtn.setColour(juce::TextButton::textColourOffId, textOnDark);
    sidebarPlaceholderBtn.onClick = [this] { settingsOverlay->syncFromProcessor(); settingsOverlay->setVisible(true); settingsOverlay->toFront(true); };
    addAndMakeVisible(sidebarPlaceholderBtn);

    // Header
    backArrowDrawable = AssetLoader::getWhiteArrowLeft();
    forwardArrowDrawable = AssetLoader::getWhiteArrowRight();
    forwardArrowDimmedDrawable = AssetLoader::getDarkGreyArrowRight();
    gearDrawable = AssetLoader::getGearSettingsIcon();
    backBtn.setImages(backArrowDrawable.get());
    backBtn.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
    backBtn.onClick = [this] { goBack(); };
    addAndMakeVisible(backBtn);
    forwardBtn.setImages(forwardArrowDrawable.get());
    forwardBtn.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
    forwardBtn.onClick = [this] { goForward(); };
    forwardBtn.setEnabled(true);
    addAndMakeVisible(forwardBtn);
    breadcrumbLabel.setColour(juce::Label::textColourId, textOnDark);
    breadcrumbLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    breadcrumbLabel.setText("Set destination in Settings " + juce::String::fromUTF8("\xe2\x86\x92"), juce::dontSendNotification);
    breadcrumbLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(breadcrumbLabel);
    settingsBtn.setButtonText(juce::CharPointer_UTF8("\xe2\x9a\x99"));  // Unicode gear U+2699
    settingsBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    settingsBtn.setColour(juce::TextButton::textColourOffId, textOnDark);
    settingsBtn.onClick = [this] {
        settingsOverlay->syncFromProcessor();
        settingsOverlay->setVisible(true);
        settingsOverlay->toFront(true);
    };
    addAndMakeVisible(settingsBtn);

    // Column browser
    columnBrowser.onFolderSelected = [this](int column, int row) {
        juce::File f = columnBrowser.getFileAt(column, row);
        if (!f.isDirectory()) return;
        pushPathToHistory();
        while (columnPath.size() > column)
            columnPath.removeLast();
        columnPath.add(f);
        columnBrowser.setPath(columnPath);
        pathForward.clear();
        updateBreadcrumb();
        updateForwardButtonState();
    };
    columnBrowser.onFileSelected = [this](int row) { (void)row; playSelectedFile(); };
    addAndMakeVisible(columnBrowser);

    columnPlaceholderLabel.setText("Select a pack from the list", juce::dontSendNotification);
    columnPlaceholderLabel.setColour(juce::Label::textColourId, textCharcoal);
    columnPlaceholderLabel.setFont(juce::Font(juce::FontOptions(14.0f)));
    columnPlaceholderLabel.setJustificationType(juce::Justification::centred);
    columnPlaceholderLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(columnPlaceholderLabel);

    // Drag area
    dragArea.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(dragArea);
    dragLabel.setText("Drag Sample", juce::dontSendNotification);
    dragLabel.setColour(juce::Label::textColourId, textCharcoal);
    dragLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    dragLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(dragLabel);
    queueLabel.setColour(juce::Label::textColourId, textCharcoal);
    queueLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(queueLabel);

    processBtn.setButtonText("Process Samples");
    processBtn.setColour(juce::TextButton::buttonColourId, processBtnBg);
    processBtn.setColour(juce::TextButton::textColourOffId, textOnDark);
    processBtn.setLookAndFeel(&s_processButtonLAF);
    processBtn.onClick = [this] {
        if (!processor.outputDirectory.isDirectory())
        {
            breadcrumbLabel.setVisible(true);
            breadcrumbLabel.setText("Set destination folder in Settings first.", juce::dontSendNotification);
            return;
        }
        if (processor.queue.isEmpty())
        {
            breadcrumbLabel.setVisible(true);
            breadcrumbLabel.setText("Add samples to the queue first.", juce::dontSendNotification);
            return;
        }
        processor.currentProcessDirectory = columnBrowser.getSelectedFolder();
        if (!processor.currentProcessDirectory.isDirectory())
            processor.currentProcessDirectory = processor.outputDirectory;
        juce::Timer::callAfterDelay(50, [this]() {
            try
            {
                processor.processAll();
                breadcrumbLabel.setVisible(true);
                breadcrumbLabel.setText("Done! " + juce::String(processor.processed.size()) + " samples organized.", juce::dontSendNotification);
                refreshPackList();
                repaint();
            }
            catch (const std::exception& ex)
            {
                breadcrumbLabel.setVisible(true);
                breadcrumbLabel.setText("Error: " + juce::String(ex.what()), juce::dontSendNotification);
            }
            catch (...)
            {
                breadcrumbLabel.setVisible(true);
                breadcrumbLabel.setText("Error: analysis failed.", juce::dontSendNotification);
            }
        });
    };
    addAndMakeVisible(processBtn);

    settingsOverlay->onClose = [this] { settingsOverlay->setVisible(false); refreshPackList(); };
    addChildComponent(settingsOverlay.get());

    refreshPackList();
    updateBreadcrumb();
    if (processor.outputDirectory.isDirectory() && selectedPackIndex >= 0 && selectedPackIndex < packDirs.size())
    {
        columnBrowser.setRootFolder(packDirs[selectedPackIndex]);
        columnBrowser.setPath(columnPath);
    }
}

SampleOrganizerEditor::~SampleOrganizerEditor()
{
    if (packListHoverListener)
        packList.removeMouseListener(packListHoverListener.get());
    transportSource.setSource(nullptr);
    readerSource.reset();
    sourcePlayer.setSource(nullptr);
    deviceManager.closeAudioDevice();
}

void SampleOrganizerEditor::paint(juce::Graphics& g)
{
    g.fillAll(creamBg);
    // Full-height dark bar on the left (220px) — black bar on the left
    juce::Rectangle<int> leftBar(0, 0, kSidebarWidth, getHeight());
    g.setColour(FinderTheme::sidebarDarkBar);
    g.fillRect(leftBar);
    // Logo in top of left bar
    juce::Rectangle<int> logoPanel = getLogoPanelBounds();
    juce::Rectangle<int> logoArea = logoPanel.reduced(12);
    g.setColour(textOnDark);
    g.setFont(juce::Font(10.5f, juce::Font::plain));
    g.drawText("MAGIC", logoArea.getX(), logoArea.getY(), logoArea.getWidth(), 18, juce::Justification::centredLeft, true);
    g.setFont(juce::Font(15.0f, juce::Font::plain));
    g.drawText("FOLDERS", logoArea.getX(), logoArea.getY() + 16, logoArea.getWidth(), 22, juce::Justification::centredLeft, true);
    // Top header bar (right of left bar): dark, for breadcrumb and gear
    juce::Rectangle<int> headerBarRect(kSidebarWidth, 0, getWidth() - kSidebarWidth, kHeaderHeight);
    g.setColour(FinderTheme::headerBar);
    g.fillRect(headerBarRect);
    // Vertical divider between left bar and content
    g.setColour(dividerLine.withAlpha(0.5f));
    g.fillRect(kSidebarWidth, 0, 1, getHeight());
    // Breadcrumb in header (drawn over dark area)
    juce::Rectangle<int> header = getHeaderBounds();
    if (breadcrumbParts.size() > 1)
    {
        juce::Rectangle<int> bcBounds = header.withTrimmedLeft(78).withTrimmedRight(120);
        int x = bcBounds.getX();
        int y = bcBounds.getY();
        int h = bcBounds.getHeight();
        g.setColour(textOnDark);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(breadcrumbParts[0], x, y, 400, h, juce::Justification::centredLeft, true);
        x += g.getCurrentFont().getStringWidth(breadcrumbParts[0]);
        g.setFont(juce::Font(12.0f, juce::Font::plain));
        for (int i = 1; i < breadcrumbParts.size(); ++i)
        {
            juce::String seg = " / " + breadcrumbParts[i];
            g.setColour(textOnDark.withAlpha(0.85f));
            g.drawText(seg, x, y, 400, h, juce::Justification::centredLeft, true);
            x += g.getCurrentFont().getStringWidth(seg);
        }
    }
    // Drag zone: full width, cream, dashed border
    juce::Rectangle<int> dragBounds = getDragAreaBounds();
    if (isDragOver)
        g.setColour(creamBg.darker(0.05f));
    else
        g.setColour(creamBg);
    g.fillRect(dragBounds);
    juce::Colour borderCol = textCharcoal;
    if (processor.queue.isEmpty() && !isDragOver)
    {
        float dash[] = { 4.0f, 4.0f };
        g.setColour(borderCol);
        g.drawDashedLine(juce::Line<float>((float)dragBounds.getX(), (float)dragBounds.getY(), (float)dragBounds.getRight(), (float)dragBounds.getY()), dash, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>((float)dragBounds.getRight(), (float)dragBounds.getY(), (float)dragBounds.getRight(), (float)dragBounds.getBottom()), dash, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>((float)dragBounds.getRight(), (float)dragBounds.getBottom(), (float)dragBounds.getX(), (float)dragBounds.getBottom()), dash, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>((float)dragBounds.getX(), (float)dragBounds.getBottom(), (float)dragBounds.getX(), (float)dragBounds.getY()), dash, 2, 1.0f);
    }
    else
    {
        g.setColour(borderCol);
        g.drawRect(dragBounds, 1);
    }
    // Process Samples button: full width dark
    juce::Rectangle<int> btnBounds = getProcessButtonBounds();
    g.setColour(processBtnBg);
    g.fillRect(btnBounds);
}

void SampleOrganizerEditor::resized()
{
    int w = getWidth();
    int h = getHeight();

    juce::Rectangle<int> logoPanel = getLogoPanelBounds();
    plusBtn.setBounds(logoPanel.getRight() - 40, logoPanel.getY() + (logoPanel.getHeight() - 32) / 2, 32, 32);
    juce::Rectangle<int> packListArea = getPackListBounds();
    packList.setBounds(packListArea);
    bool hasDestination = processor.outputDirectory.isDirectory();
    sidebarPlaceholderBtn.setVisible(!hasDestination);
    sidebarPlaceholderBtn.setBounds(packListArea.reduced(12));
    packList.setVisible(hasDestination);

    juce::Rectangle<int> header = getHeaderBounds();
    backBtn.setBounds(header.getX() + 8, header.getY() + (header.getHeight() - 28) / 2, 28, 28);
    forwardBtn.setBounds(header.getX() + 40, header.getY() + (header.getHeight() - 28) / 2, 28, 28);
    breadcrumbLabel.setBounds(header.getX() + 78, header.getY(), header.getWidth() - 120, header.getHeight());
    settingsBtn.setBounds(header.getRight() - 44, header.getY() + (header.getHeight() - 28) / 2, 28, 28);
    updateForwardButtonState();

    juce::Rectangle<int> colBounds = getColumnBrowserBounds();
    columnBrowser.setBounds(colBounds);
    bool hasPackSelected = selectedPackIndex >= 0 && selectedPackIndex < packDirs.size();
    columnPlaceholderLabel.setVisible(!hasPackSelected);
    columnPlaceholderLabel.setBounds(colBounds);
    juce::Rectangle<int> dragBounds = getDragAreaBounds();
    dragArea.setBounds(dragBounds);
    dragLabel.setBounds(dragBounds);
    queueLabel.setBounds(dragBounds.reduced(12, 8));
    if (!processor.queue.isEmpty())
    {
        dragLabel.setVisible(false);
        queueLabel.setVisible(true);
        queueLabel.setText(juce::String(processor.queue.size()) + " sample(s) in queue", juce::dontSendNotification);
    }
    else
    {
        dragLabel.setVisible(true);
        queueLabel.setVisible(false);
    }
    processBtn.setBounds(getProcessButtonBounds());

    settingsOverlay->setBounds(0, 0, w, h);
}

void SampleOrganizerEditor::refreshPackList()
{
    packNames.clear();
    packDirs.clear();
    if (!processor.outputDirectory.isDirectory())
    {
        selectedPackIndex = -1;
        packList.updateContent();
        return;
    }
    for (const auto& f : processor.outputDirectory.findChildFiles(juce::File::findDirectories, false))
    {
        packNames.add(f.getFileName());
        packDirs.add(f);
    }
    if (selectedPackIndex >= packNames.size())
        selectedPackIndex = packNames.size() > 0 ? 0 : -1;
    packList.updateContent();
}

void SampleOrganizerEditor::updateForwardButtonState()
{
    if (pathForward.isEmpty())
        forwardBtn.setImages(forwardArrowDimmedDrawable.get());
    else
        forwardBtn.setImages(forwardArrowDrawable.get());
}

void SampleOrganizerEditor::updateBreadcrumb()
{
    if (!processor.outputDirectory.isDirectory())
    {
        breadcrumbParts.clear();
        breadcrumbLabel.setText("Set destination in Settings " + juce::String::fromUTF8("\xe2\x86\x92"), juce::dontSendNotification);
        breadcrumbLabel.setVisible(true);
        return;
    }
    if (selectedPackIndex < 0 || selectedPackIndex >= packNames.size())
    {
        breadcrumbParts.clear();
        breadcrumbLabel.setText("Select a pack", juce::dontSendNotification);
        breadcrumbLabel.setVisible(true);
        return;
    }
    breadcrumbParts.clear();
    breadcrumbParts.add(packNames[selectedPackIndex]);
    for (const auto& f : columnPath)
        breadcrumbParts.add(f.getFileName());
    juce::File selFile = columnBrowser.getSelectedFileInLastColumn();
    if (selFile.existsAsFile())
        breadcrumbParts.add(selFile.getFileName());
    if (breadcrumbParts.size() == 1)
    {
        breadcrumbLabel.setText(breadcrumbParts[0], juce::dontSendNotification);
        breadcrumbLabel.setVisible(true);
        return;
    }
    breadcrumbLabel.setVisible(false);
}

void SampleOrganizerEditor::pushPathToHistory()
{
    if (!columnPath.isEmpty())
        pathHistory.add(columnPath);
}

void SampleOrganizerEditor::goBack()
{
    if (pathHistory.isEmpty()) return;
    pathForward.add(columnPath);
    columnPath = pathHistory.getLast();
    pathHistory.removeLast();
    columnBrowser.setPath(columnPath);
    updateBreadcrumb();
    updateForwardButtonState();
}

void SampleOrganizerEditor::goForward()
{
    if (pathForward.isEmpty()) return;
    pathHistory.add(columnPath);
    columnPath = pathForward.getLast();
    pathForward.removeLast();
    columnBrowser.setPath(columnPath);
    updateBreadcrumb();
    updateForwardButtonState();
}

void SampleOrganizerEditor::PackListHoverListener::mouseMove(const juce::MouseEvent& e)
{
    if (!editor || e.eventComponent != &editor->packList) return;
    int row = e.getPosition().getY() / SampleOrganizerEditor::kPackRowHeight;
    int total = editor->packNames.size();
    editor->setHoveredPackRow(row >= 0 && row < total ? row : -1);
}

void SampleOrganizerEditor::PackListHoverListener::mouseExit(const juce::MouseEvent& e)
{
    if (editor && e.eventComponent == &editor->packList)
        editor->setHoveredPackRow(-1);
}

void SampleOrganizerEditor::setHoveredPackRow(int row)
{
    if (row != hoveredPackRow)
    {
        hoveredPackRow = row;
        packList.repaint();
    }
}

void SampleOrganizerEditor::mouseMove(const juce::MouseEvent&)
{
}

void SampleOrganizerEditor::mouseExit(const juce::MouseEvent&)
{
}

void SampleOrganizerEditor::mouseDown(const juce::MouseEvent& e)
{
    handleBreadcrumbClick(e.getPosition().getX(), e.getPosition().getY());
}

void SampleOrganizerEditor::handleBreadcrumbClick(int x, int y)
{
    juce::Rectangle<int> header = getHeaderBounds();
    if (!header.contains(x, y))
        return;
    int bcLeft = header.getX() + 78;
    int bcRight = header.getRight() - 52;
    if (x < bcLeft || x > bcRight || breadcrumbParts.isEmpty())
        return;
    int seg = 0;
    int px = bcLeft;
    juce::Font boldFont(12.0f, juce::Font::bold);
    juce::Font plainFont(12.0f, juce::Font::plain);
    for (int i = 0; i < breadcrumbParts.size(); ++i)
    {
        juce::String part = breadcrumbParts[i];
        float w = (i == 0 ? boldFont : plainFont).getStringWidth(part);
        if (i > 0)
            w += plainFont.getStringWidth(" / ");
        if (x >= px && x < px + (int)w)
        {
            seg = i;
            break;
        }
        px += (int)w;
        if (i < breadcrumbParts.size() - 1)
            px += (int)plainFont.getStringWidth(" / ");
    }
    if (seg <= 0)
        return;
    pathHistory.add(columnPath);
    while ((int)columnPath.size() > seg)
        columnPath.removeLast();
    columnBrowser.setPath(columnPath);
    pathForward.clear();
    updateBreadcrumb();
    updateForwardButtonState();
}

void SampleOrganizerEditor::playSelectedFile()
{
    juce::File file = columnBrowser.getSelectedFileInLastColumn();
    if (!file.existsAsFile()) return;
    juce::String path = file.getFullPathName();
    if (path == playingFilePath)
    {
        transportSource.stop();
        transportSource.setSource(nullptr);
        readerSource.reset();
        playingFilePath.clear();
        return;
    }
    transportSource.stop();
    transportSource.setSource(nullptr);
    readerSource.reset();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (!reader) return;
    readerSource.reset(new juce::AudioFormatReaderSource(reader.get(), true));
    reader.release();
    if (auto* device = deviceManager.getCurrentAudioDevice())
        transportSource.prepareToPlay(device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());
    transportSource.setSource(readerSource.get(), 0, nullptr);
    transportSource.setPosition(0.0);
    transportSource.start();
    playingFilePath = path;
    columnBrowser.repaint();
}

juce::Rectangle<int> SampleOrganizerEditor::getLogoPanelBounds() const
{
    return juce::Rectangle<int>(0, 0, kLogoPanelWidth, kHeaderHeight);
}

juce::Rectangle<int> SampleOrganizerEditor::getPackListBounds() const
{
    int contentBottom = getHeight() - kDragAreaHeight - kProcessButtonHeight;
    return juce::Rectangle<int>(0, kHeaderHeight, kSidebarWidth, contentBottom - kHeaderHeight);
}

juce::Rectangle<int> SampleOrganizerEditor::getHeaderBounds() const
{
    return juce::Rectangle<int>(kLogoPanelWidth + 1, 0, getWidth() - kLogoPanelWidth - 1, kHeaderHeight);
}

juce::Rectangle<int> SampleOrganizerEditor::getColumnBrowserBounds() const
{
    int contentTop = kHeaderHeight;
    int contentBottom = getHeight() - kDragAreaHeight - kProcessButtonHeight;
    return juce::Rectangle<int>(kSidebarWidth, contentTop, getWidth() - kSidebarWidth, contentBottom - contentTop);
}

juce::Rectangle<int> SampleOrganizerEditor::getDragAreaBounds() const
{
    int y = getHeight() - kDragAreaHeight - kProcessButtonHeight;
    return juce::Rectangle<int>(0, y, getWidth(), kDragAreaHeight);
}

juce::Rectangle<int> SampleOrganizerEditor::getProcessButtonBounds() const
{
    return juce::Rectangle<int>(0, getHeight() - kProcessButtonHeight, getWidth(), kProcessButtonHeight);
}

bool SampleOrganizerEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    juce::StringArray expanded = expandDroppedPaths(files);
    for (const auto& f : expanded)
        if (isAudioPath(f)) return true;
    return false;
}

void SampleOrganizerEditor::fileDragEnter(const juce::StringArray&, int x, int y)
{
    if (getDragAreaBounds().contains(x, y) && !isDragOver)
    {
        isDragOver = true;
        repaint();
    }
}

void SampleOrganizerEditor::fileDragMove(const juce::StringArray&, int x, int y)
{
    bool in = getDragAreaBounds().contains(x, y);
    if (in != isDragOver) { isDragOver = in; repaint(); }
}

void SampleOrganizerEditor::fileDragExit(const juce::StringArray&)
{
    if (isDragOver) { isDragOver = false; repaint(); }
}

void SampleOrganizerEditor::filesDropped(const juce::StringArray& files, int, int)
{
    isDragOver = false;
    repaint();
    juce::StringArray expanded = expandDroppedPaths(files);
    juce::Array<juce::File> fileArray;
    for (const auto& f : expanded)
    {
        if (!isAudioPath(f)) continue;
        juce::File file(f);
        if (file.existsAsFile()) fileArray.add(file);
    }
    processor.addFiles(fileArray);
    queueLabel.setText(juce::String(processor.queue.size()) + " sample(s) in queue", juce::dontSendNotification);
    queueLabel.setVisible(true);
    dragLabel.setVisible(false);
    repaint();
}
