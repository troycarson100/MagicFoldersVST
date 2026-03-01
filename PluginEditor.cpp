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

    class RenamePackDialogContent : public juce::Component
    {
    public:
        RenamePackDialogContent(const juce::String& defaultName_, juce::String* resultOut_ = nullptr)
            : defaultName(defaultName_), resultOut(resultOut_)
        {
            label.setText("Enter a name for the new sample pack:", juce::dontSendNotification);
            label.setColour(juce::Label::textColourId, juce::Colours::black);
            addAndMakeVisible(label);
            te.setText(defaultName);
            te.setSelectAllWhenFocused(true);
            addAndMakeVisible(te);
            okBtn.setButtonText("OK");
            okBtn.onClick = [this] {
                juce::String name = te.getText().trim();
                if (name.isEmpty()) name = defaultName;
                if (onOk)
                    onOk(name);
                else if (resultOut)
                {
                    *resultOut = name;
                    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                        dw->exitModalState(1);
                }
            };
            addAndMakeVisible(okBtn);
            cancelBtn.setButtonText("Cancel");
            cancelBtn.onClick = [this] {
                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                    dw->exitModalState(0);
            };
            addAndMakeVisible(cancelBtn);
            setSize(380, 120);
        }
        void setOnOk(std::function<void(juce::String)> f) { onOk = std::move(f); }
        void setRenameLabel(const juce::String& s) { label.setText(s, juce::dontSendNotification); }
        void resized() override
        {
            auto r = getLocalBounds().reduced(12);
            label.setBounds(r.removeFromTop(22));
            r.removeFromTop(6);
            te.setBounds(r.removeFromTop(28));
            r.removeFromTop(14);
            auto row = r.removeFromTop(28);
            cancelBtn.setBounds(row.removeFromRight(80).reduced(2, 0));
            row.removeFromRight(8);
            okBtn.setBounds(row.removeFromRight(80).reduced(2, 0));
        }
    private:
        juce::String defaultName;
        juce::String* resultOut = nullptr;
        std::function<void(juce::String)> onOk;
        juce::Label label;
        juce::TextEditor te;
        juce::TextButton okBtn;
        juce::TextButton cancelBtn;
    };
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

// --- PackListBox (double-click to rename) ---
void SampleOrganizerEditor::PackListBox::mouseDoubleClick(const juce::MouseEvent& e)
{
    int row = getRowContainingPosition(e.getPosition().getX(), e.getPosition().getY());
    if (onDoubleClickRow && row >= 0)
        onDoubleClickRow(row);
    juce::ListBox::mouseDoubleClick(e);
}

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
    g.setFont(interFont(13.0f, selected));
    juce::String name = row < editor.packNames.size() ? editor.packNames[row] : juce::String();
    if (name.length() > 28)
        name = name.dropLastCharacters(name.length() - 25) + "...";
    const int padH = editor.kPackPaddingH;
    const int padV = editor.kPackPaddingV;
    g.drawText(name, padH, padV, w - padH - 32, h - 2 * padV, juce::Justification::centredLeft);
    if (selected && editor.forwardArrowDimmedDrawable)
        editor.forwardArrowDimmedDrawable->drawWithin(g, juce::Rectangle<float>((float)(w - 24), (float)((h - 14) / 2), 14.0f, 14.0f), juce::RectanglePlacement::centred, 1.0f);
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
    plusBtn.onClick = [this] { createNewPack(); };
    addAndMakeVisible(plusBtn);

    packList.setModel(&packListModel);
    packList.setRowHeight(kPackRowHeight);
    packList.onDoubleClickRow = [this](int row) { tryRenamePack(row); };
    packList.setColour(juce::ListBox::backgroundColourId, FinderTheme::sidebarDarkBar);
    packListHoverListener = std::make_unique<PackListHoverListener>();
    packListHoverListener->editor = this;
    packList.addMouseListener(packListHoverListener.get(), true);
    if (auto* vp = packList.getViewport())
        vp->addMouseListener(packListHoverListener.get(), true);
    packList.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    packList.setOutlineThickness(0);
    addAndMakeVisible(packList);

    packRenameEditor.setMultiLine(false);
    packRenameEditor.setBorder(juce::BorderSize<int>(1));
    packRenameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    packRenameEditor.setColour(juce::TextEditor::textColourId, juce::Colour(0xff1a1a1a));
    packRenameEditor.setColour(juce::TextEditor::highlightColourId, juce::Colour(0xffb0d4f0));
    packRenameEditor.setColour(juce::TextEditor::highlightedTextColourId, juce::Colour(0xff1a1a1a));
    packRenameEditor.setColour(juce::TextEditor::outlineColourId, FinderTheme::topBar);
    packRenameEditor.onReturnKey = [this] { commitPackRename(); };
    packRenameEditor.onEscapeKey = [this] { hidePackRenameEditor(); };

    sidebarPlaceholderBtn.setButtonText("");
    sidebarPlaceholderBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    sidebarPlaceholderBtn.setColour(juce::TextButton::textColourOffId, textOnDark);
    sidebarPlaceholderBtn.onClick = [this] { settingsOverlay->syncFromProcessor(); settingsOverlay->setVisible(true); settingsOverlay->toFront(true); };
    addAndMakeVisible(sidebarPlaceholderBtn);

    // Header
    backArrowDrawable = AssetLoader::getWhiteArrowLeft();
    forwardArrowDrawable = AssetLoader::getWhiteArrowRight();
    forwardArrowDimmedDrawable = AssetLoader::getDarkGreyArrowRight();
    gearDrawable = AssetLoader::getGearSettingsIconPng();
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
    breadcrumbLabel.setFont(interFont(14.0f));
    breadcrumbLabel.setText("Set destination in Settings " + juce::String::fromUTF8("\xe2\x86\x92"), juce::dontSendNotification);
    breadcrumbLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(breadcrumbLabel);
    settingsBtn.setImages(gearDrawable.get());
    settingsBtn.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
    settingsBtn.setColour(juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentBlack);
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
    columnBrowser.onKeyLeft = [this] { goBack(); };
    columnBrowser.onPathChanged = [this] {
        columnPath = columnBrowser.getPath();
        updateBreadcrumb();
    };
    addAndMakeVisible(columnBrowser);

    columnPlaceholderLabel.setText("", juce::dontSendNotification);
    columnPlaceholderLabel.setColour(juce::Label::textColourId, textCharcoal);
    columnPlaceholderLabel.setFont(interFont(14.0f));
    columnPlaceholderLabel.setJustificationType(juce::Justification::centred);
    columnPlaceholderLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(columnPlaceholderLabel);

    // Drag area (mouse listener for hover)
    dragArea.setInterceptsMouseClicks(false, false);
    dragArea.addMouseListener(this, false);
    addAndMakeVisible(dragArea);
    dragLabel.setText("Drag Sample", juce::dontSendNotification);
    dragLabel.setColour(juce::Label::textColourId, textCharcoal);
    dragLabel.setFont(interFont(15.0f, true));
    dragLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(dragLabel);
    queueLabel.setColour(juce::Label::textColourId, textCharcoal);
    queueLabel.setFont(interFont(13.0f));
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
        removeMouseListener(packListHoverListener.get());
    transportSource.setSource(nullptr);
    readerSource.reset();
    sourcePlayer.setSource(nullptr);
    deviceManager.closeAudioDevice();
}

void SampleOrganizerEditor::paint(juce::Graphics& g)
{
    g.fillAll(creamBg);
    int contentBottom = getContentBottom();
    // Left dark bar (sidebar) — ends at content bottom to line up with column dividers on the right
    juce::Rectangle<int> leftBar(0, 0, kSidebarWidth, contentBottom);
    g.setColour(FinderTheme::sidebarDarkBar);
    g.fillRect(leftBar);
    // Logo icon in top of left bar (replaces MAGIC FOLDERS text)
    juce::Rectangle<int> logoPanel = getLogoPanelBounds();
    juce::Rectangle<int> logoArea = logoPanel.reduced(12, 8);
    if (logoDrawable)
        logoDrawable->drawWithin(g, logoArea.toFloat(), juce::RectanglePlacement(juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid), 1.0f);
    // Top bar (breadcrumbs + gear): #393E46
    juce::Rectangle<int> headerBarRect(kSidebarWidth, 0, getWidth() - kSidebarWidth, kHeaderHeight);
    g.setColour(FinderTheme::topBar);
    g.fillRect(headerBarRect);
    // Vertical divider between left bar and content (stops at content bottom)
    g.setColour(FinderTheme::topBar);
    g.fillRect(kSidebarWidth, 0, 1, contentBottom);
    // Thick horizontal border under content (above drag strip)
    g.setColour(FinderTheme::topBar);
    g.fillRect(0, contentBottom, getWidth(), kThickBorderHeight);
    // Breadcrumb in header (drawn over dark area) — always draw from breadcrumbParts when non-empty
    juce::Rectangle<int> header = getHeaderBounds();
    if (!breadcrumbParts.isEmpty())
    {
        juce::Rectangle<int> bcBounds = header.withTrimmedLeft(78).withTrimmedRight(120);
        int x = bcBounds.getX();
        int y = bcBounds.getY();
        int h = bcBounds.getHeight();
        g.setColour(textOnDark);
        g.setFont(interFont(14.0f, true));
        g.drawText(breadcrumbParts[0], x, y, 400, h, juce::Justification::centredLeft, true);
        x += (int)g.getCurrentFont().getStringWidth(breadcrumbParts[0]);
        g.setFont(interFont(14.0f));
        for (int i = 1; i < breadcrumbParts.size(); ++i)
        {
            juce::String seg = " / " + breadcrumbParts[i];
            g.setColour(textOnDark.withAlpha(0.9f));
            g.drawText(seg, x, y, 400, h, juce::Justification::centredLeft, true);
            x += (int)g.getCurrentFont().getStringWidth(seg);
        }
    }
    // Drag zone: hover colour when hovering, solid border when dragging or when queue has items
    juce::Rectangle<int> dragBounds = getDragAreaBounds();
    if (isDragOver)
        g.setColour(creamBg.darker(0.06f));
    else if (isHoveringDragArea)
        g.setColour(creamBg.darker(0.03f));
    else
        g.setColour(creamBg);
    g.fillRect(dragBounds);
    juce::Colour borderCol = textCharcoal;
    bool useSolidBorder = isDragOver || !processor.queue.isEmpty();
    if (useSolidBorder)
    {
        g.setColour(borderCol);
        g.drawRect(dragBounds, 1);
    }
    else
    {
        float dash[] = { 4.0f, 4.0f };
        g.setColour(borderCol);
        g.drawDashedLine(juce::Line<float>((float)dragBounds.getX(), (float)dragBounds.getY(), (float)dragBounds.getRight(), (float)dragBounds.getY()), dash, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>((float)dragBounds.getRight(), (float)dragBounds.getY(), (float)dragBounds.getRight(), (float)dragBounds.getBottom()), dash, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>((float)dragBounds.getRight(), (float)dragBounds.getBottom(), (float)dragBounds.getX(), (float)dragBounds.getBottom()), dash, 2, 1.0f);
        g.drawDashedLine(juce::Line<float>((float)dragBounds.getX(), (float)dragBounds.getBottom(), (float)dragBounds.getX(), (float)dragBounds.getY()), dash, 2, 1.0f);
    }
    if (!processor.queue.isEmpty())
    {
        g.setColour(textCharcoal);
        g.setFont(interFont(12.0f));
        juce::Rectangle<int> textArea = dragBounds.reduced(12, 8);
        juce::StringArray names;
        for (const auto& info : processor.queue)
            names.add(info.sourceFile.getFileName());
        g.drawFittedText(names.joinIntoString("\n"), textArea, juce::Justification::topLeft, juce::jmin(8, names.size()), 1.0f);
    }
    // Process Samples button: full width dark
    juce::Rectangle<int> btnBounds = getProcessButtonBounds();
    g.setColour(processBtnBg);
    g.fillRect(btnBounds);
    // Thick bottom border under Process Samples
    g.setColour(FinderTheme::topBar);
    g.fillRect(0, getHeight() - kThickBorderHeight, getWidth(), kThickBorderHeight);
}

void SampleOrganizerEditor::resized()
{
    int w = getWidth();
    int h = getHeight();

    juce::Rectangle<int> logoPanel = getLogoPanelBounds();
    plusBtn.setBounds(logoPanel.getRight() - 40, 0, 32, 35);
    plusBtn.toFront(true);
    juce::Rectangle<int> packListArea = getPackListBounds();
    packList.setBounds(packListArea);
    sidebarPlaceholderBtn.setVisible(false);
    packList.setVisible(true);

    juce::Rectangle<int> header = getHeaderBounds();
    backBtn.setBounds(header.getX() + 8, header.getY() + (header.getHeight() - 28) / 2, 28, 28);
    forwardBtn.setBounds(header.getX() + 40, header.getY() + (header.getHeight() - 28) / 2, 28, 28);
    breadcrumbLabel.setBounds(header.getX() + 78, header.getY(), header.getWidth() - 120, header.getHeight());
    settingsBtn.setBounds(header.getRight() - 44, header.getY() + (header.getHeight() - 28) / 2, 28, 28);
    updateForwardButtonState();

    juce::Rectangle<int> colBounds = getColumnBrowserBounds();
    columnBrowser.setBounds(colBounds);
    bool hasPackSelected = selectedPackIndex >= 0 && selectedPackIndex < packDirs.size();
    columnPlaceholderLabel.setVisible(false);
    columnPlaceholderLabel.setBounds(colBounds);
    juce::Rectangle<int> dragBounds = getDragAreaBounds();
    dragArea.setBounds(dragBounds);
    juce::Rectangle<int> dragInner = dragBounds.reduced(16, 10);
    dragLabel.setBounds(dragInner);
    queueLabel.setBounds(dragInner);
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

void SampleOrganizerEditor::createNewPack()
{
    if (!processor.outputDirectory.isDirectory())
    {
        settingsOverlay->syncFromProcessor();
        settingsOverlay->setVisible(true);
        settingsOverlay->toFront(true);
        return;
    }
    refreshPackList();
    juce::String baseName("New Pack");
    juce::File newDir = processor.outputDirectory.getChildFile(baseName);
    int suffix = 0;
    while (newDir.exists())
        newDir = processor.outputDirectory.getChildFile(baseName + " " + juce::String(++suffix));
    if (!newDir.createDirectory())
    {
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "New Pack",
            "Could not create the pack folder. Check permissions and that the destination path is valid.",
            nullptr);
        return;
    }
    juce::String createdPath = newDir.getFullPathName();
    juce::File createdFile = newDir;
    refreshPackList();
    // Defer selection + rename to next message loop (same as ColumnBrowserComponent::showNewFolderDialog).
    juce::MessageManager::callAsync([this, createdPath, createdFile]()
    {
        if (!isVisible())
            return;
        refreshPackList();
        int row = -1;
        juce::String normalCreated = createdFile.getFullPathName();
        for (int i = 0; i < packDirs.size(); ++i)
        {
            if (packDirs[i].getFullPathName() == normalCreated)
            {
                row = i;
                break;
            }
        }
        if (row < 0)
        {
            juce::String createdFileName = createdFile.getFileName();
            for (int i = 0; i < packDirs.size(); ++i)
            {
                if (packDirs[i].getFileName() == createdFileName)
                {
                    row = i;
                    break;
                }
            }
        }
        // If FS hasn't picked up the new dir yet, add it to the list so we always have a row (same theory as new folder).
        if (row < 0 && createdFile.exists())
        {
            packDirs.add(createdFile);
            packNames.add(createdFile.getFileName());
            packList.updateContent();
            row = packDirs.size() - 1;
        }
        if (row >= 0)
        {
            selectedPackIndex = row;
            columnPath.clear();
            pathHistory.clear();
            pathForward.clear();
            columnBrowser.setRootFolder(packDirs[selectedPackIndex]);
            columnBrowser.setPath(columnPath);
            updateBreadcrumb();
            updateForwardButtonState();
            packList.repaint();
            startPackInlineRename(row);
        }
    });
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
    if (breadcrumbParts.size() >= 1)
        breadcrumbLabel.setVisible(false);
    else
        breadcrumbLabel.setVisible(true);
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
    if (!editor) return;
    juce::Point<int> posInEditor = editor->getLocalPoint(e.eventComponent, e.getPosition());
    if (!editor->getPackListBounds().contains(posInEditor)) { editor->setHoveredPackRow(-1); return; }
    juce::Point<int> listPos = editor->packList.getLocalPoint(editor, posInEditor);
    int row = editor->packList.getRowContainingPosition(listPos.getX(), listPos.getY());
    int total = editor->packNames.size();
    editor->setHoveredPackRow(row >= 0 && row < total ? row : -1);
}

void SampleOrganizerEditor::PackListHoverListener::mouseExit(const juce::MouseEvent& e)
{
    if (editor)
        editor->setHoveredPackRow(-1);
}

void SampleOrganizerEditor::PackListHoverListener::mouseDown(const juce::MouseEvent& e)
{
    if (!editor) return;
    juce::Point<int> posInEditor = editor->getLocalPoint(e.eventComponent, e.getPosition());
    if (!editor->getPackListBounds().contains(posInEditor)) return;
    juce::Point<int> listPos = editor->packList.getLocalPoint(editor, posInEditor);
    int row = editor->packList.getRowContainingPosition(listPos.getX(), listPos.getY());
    // Only right-click in empty area below packs → "+ new pack" (left-click does nothing)
    if (row < 0 || row >= editor->packDirs.size())
    {
        if (e.mods.isLeftButtonDown() || !e.mods.isRightButtonDown())
            return;
        juce::PopupMenu m;
        m.addItem(1, "+ new pack");
        auto opts = juce::PopupMenu::Options()
            .withTargetComponent(&editor->packList)
            .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards)
            .withMousePosition();
        m.showMenuAsync(opts, [editor = this->editor](int result) {
            if (editor && result == 1)
                editor->createNewPack();
        });
        return;
    }
    if (!e.mods.isPopupMenu())
    {
        uint32 now = (uint32) juce::Time::getMillisecondCounter();
        uint32 dt = now - editor->lastPackClickTime;
        if (row == editor->lastPackClickRow && dt >= editor->kPackDoubleClickMinMs && dt < editor->kPackDoubleClickMaxMs)
        {
            editor->lastPackClickRow = -1;
            int rowToEdit = row;
            SampleOrganizerEditor* ed = this->editor;
            juce::Timer::callAfterDelay(250, [ed, rowToEdit]() {
                if (ed && ed->isVisible())
                    ed->tryRenamePack(rowToEdit);
            });
            return;
        }
        editor->lastPackClickRow = row;
        editor->lastPackClickTime = now;
        return;
    }
    juce::File packDir = editor->packDirs[row];
    juce::PopupMenu m;
    m.addItem(1, "Rename");
    bool isMac = (juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX) != 0;
    m.addItem(2, isMac ? "Reveal in Finder" : "Reveal in File Explorer");
    m.addSeparator();
    m.addItem(3, "Delete");
    auto opts = juce::PopupMenu::Options()
        .withParentComponent(editor->getTopLevelComponent())
        .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards)
        .withMousePosition();
    m.showMenuAsync(opts, [editor = this->editor, row, packDir](int result) {
        if (!editor) return;
        if (result == 1)
            editor->startPackInlineRename(row);
        else if (result == 2 && packDir.exists())
            packDir.revealToUser();
        else if (result == 3 && packDir.exists())
        {
            packDir.moveToTrash();
            editor->refreshPackList();
            editor->columnPath.clear();
            editor->pathHistory.clear();
            editor->pathForward.clear();
            if (editor->selectedPackIndex >= 0 && editor->selectedPackIndex < editor->packDirs.size())
                editor->columnBrowser.setRootFolder(editor->packDirs[editor->selectedPackIndex]);
            else
                editor->columnBrowser.setRootFolder(juce::File());
            editor->columnBrowser.setPath(editor->columnPath);
            editor->updateBreadcrumb();
            editor->updateForwardButtonState();
            editor->packList.repaint();
        }
    });
}

void SampleOrganizerEditor::PackListHoverListener::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (!editor) return;
    juce::Point<int> posInEditor = editor->getLocalPoint(e.eventComponent, e.getPosition());
    if (!editor->getPackListBounds().contains(posInEditor)) return;
    juce::Point<int> listPos = editor->packList.getLocalPoint(editor, posInEditor);
    int row = editor->packList.getRowContainingPosition(listPos.getX(), listPos.getY());
    if (row >= 0)
        editor->tryRenamePack(row);
}

void SampleOrganizerEditor::tryRenamePack(int row)
{
    startPackInlineRename(row);
}

void SampleOrganizerEditor::startPackInlineRename(int row)
{
    if (row < 0 || row >= packDirs.size()) return;
    hidePackRenameEditor();
    editingPackRow = row;
    packRenameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    packRenameEditor.setColour(juce::TextEditor::textColourId, juce::Colour(0xff1a1a1a));
    packRenameEditor.setColour(juce::TextEditor::highlightedTextColourId, juce::Colour(0xff1a1a1a));
    juce::Rectangle<int> listBounds = packList.getBounds();
    int rowY = row * kPackRowHeight;
    if (auto* vp = packList.getViewport())
        rowY -= vp->getViewPosition().getY();
    juce::Rectangle<int> rowRect(listBounds.getX(), listBounds.getY() + rowY, listBounds.getWidth(), kPackRowHeight);
    juce::Rectangle<int> textRect = rowRect.withTrimmedLeft(kPackPaddingH).withTrimmedRight(32).reduced(0, kPackPaddingV);
    packRenameEditor.setText(packNames[row], false);
    packRenameEditor.setBounds(textRect);
    packRenameEditor.onFocusLost = [this] { commitPackRename(); };
    addAndMakeVisible(packRenameEditor);
    packRenameEditor.toFront(true);
    packRenameEditor.selectAll();
    packRenameEditor.grabKeyboardFocus();
    juce::Timer::callAfterDelay(80, [this]() {
        if (editingPackRow >= 0 && packRenameEditor.isVisible())
            packRenameEditor.grabKeyboardFocus();
    });
}

void SampleOrganizerEditor::commitPackRename()
{
    if (editingPackRow < 0 || editingPackRow >= packDirs.size()) { hidePackRenameEditor(); return; }
    juce::File packDir = packDirs[editingPackRow];
    juce::String newName = juce::File::createLegalFileName(packRenameEditor.getText().trim());
    hidePackRenameEditor();
    juce::String currentName = packDir.getFileName();
    if (newName.isEmpty() || newName == currentName) return;
    juce::File dest = processor.outputDirectory.getChildFile(newName);
    if (dest.exists()) return;
    if (!packDir.moveFileTo(dest)) return;
    refreshPackList();
    for (int i = 0; i < packDirs.size(); ++i)
    {
        if (packDirs[i] == dest)
        {
            selectedPackIndex = i;
            columnBrowser.setRootFolder(packDirs[selectedPackIndex]);
            columnPath.clear();
            pathHistory.clear();
            pathForward.clear();
            columnBrowser.setPath(columnPath);
            updateBreadcrumb();
            break;
        }
    }
    packList.repaint();
}

void SampleOrganizerEditor::hidePackRenameEditor()
{
    if (editingPackRow < 0) return;
    packRenameEditor.onFocusLost = nullptr;
    removeChildComponent(&packRenameEditor);
    editingPackRow = -1;
    packList.repaint();
}


void SampleOrganizerEditor::setHoveredPackRow(int row)
{
    if (row != hoveredPackRow)
    {
        hoveredPackRow = row;
        packList.repaint();
    }
}

void SampleOrganizerEditor::mouseMove(const juce::MouseEvent& e)
{
    bool inside = getDragAreaBounds().contains(e.getPosition());
    if (inside != isHoveringDragArea)
    {
        isHoveringDragArea = inside;
        repaint();
    }
}

void SampleOrganizerEditor::mouseEnter(const juce::MouseEvent& e)
{
    if (e.eventComponent == &dragArea && !isHoveringDragArea)
    {
        isHoveringDragArea = true;
        repaint();
    }
}

void SampleOrganizerEditor::mouseExit(const juce::MouseEvent&)
{
    if (isHoveringDragArea)
    {
        isHoveringDragArea = false;
        repaint();
    }
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
    juce::Font boldFont = interFont(14.0f, true);
    juce::Font plainFont = interFont(14.0f);
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

int SampleOrganizerEditor::getContentBottom() const
{
    return getHeight() - kDragAreaHeight - kProcessButtonHeight - 2 * kDragAreaPaddingVertical;
}

juce::Rectangle<int> SampleOrganizerEditor::getPackListBounds() const
{
    int contentBottom = getContentBottom();
    return juce::Rectangle<int>(0, kHeaderHeight, kSidebarWidth, contentBottom - kHeaderHeight);
}

juce::Rectangle<int> SampleOrganizerEditor::getHeaderBounds() const
{
    return juce::Rectangle<int>(kLogoPanelWidth + 1, 0, getWidth() - kLogoPanelWidth - 1, kHeaderHeight);
}

juce::Rectangle<int> SampleOrganizerEditor::getColumnBrowserBounds() const
{
    int contentTop = kHeaderHeight;
    int contentBottom = getContentBottom();
    return juce::Rectangle<int>(kSidebarWidth, contentTop, getWidth() - kSidebarWidth, contentBottom - contentTop);
}

juce::Rectangle<int> SampleOrganizerEditor::getDragAreaBounds() const
{
    int contentBottom = getContentBottom();
    int y = contentBottom + kDragAreaPaddingVertical;
    return juce::Rectangle<int>(kDragAreaPadding, y, getWidth() - 2 * kDragAreaPadding, kDragAreaHeight);
}

juce::Rectangle<int> SampleOrganizerEditor::getProcessButtonBounds() const
{
    int contentBottom = getContentBottom();
    int y = contentBottom + kDragAreaPaddingVertical + kDragAreaHeight + kDragAreaPaddingVertical;
    return juce::Rectangle<int>(0, y, getWidth(), kProcessButtonHeight);
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
