#include "PluginEditor.h"
#include <thread>
#include <memory>

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

juce::StringArray MagicFoldersEditor::expandDroppedPaths(const juce::StringArray& list) { return ::expandDroppedPaths(list); }
bool MagicFoldersEditor::isAudioPath(const juce::String& path) { return ::isAudioPath(path); }

// --- PackListBox (double-click to rename) ---
void MagicFoldersEditor::PackListBox::mouseDoubleClick(const juce::MouseEvent& e)
{
    int row = getRowContainingPosition(e.getPosition().getX(), e.getPosition().getY());
    if (onDoubleClickRow && row >= 0)
        onDoubleClickRow(row);
    juce::ListBox::mouseDoubleClick(e);
}

// --- PackListModel ---
int MagicFoldersEditor::PackListModel::getNumRows()
{
    return editor.packNames.size();
}

void MagicFoldersEditor::PackListModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
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

void MagicFoldersEditor::PackListModel::listBoxItemClicked(int row, const juce::MouseEvent&)
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

// --- MagicFoldersEditor ---
MagicFoldersEditor::MagicFoldersEditor(MagicFoldersProcessor& p)
    : AudioProcessorEditor(&p), processor(p), packListModel(*this)
{
    settingsOverlay = std::make_unique<SettingsOverlayComponent>(processor);
    setSize(920, 600);
    setResizeLimits(860, 580, 4096, 4096);
    setWantsKeyboardFocus(true);
    addKeyListener(this);

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
    // Inline rename should look like simple text, without a grey border.
    packRenameEditor.setBorder(juce::BorderSize<int>(0));
    packRenameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    packRenameEditor.setColour(juce::TextEditor::textColourId, juce::Colour(0xff1a1a1a));
    packRenameEditor.setColour(juce::TextEditor::highlightColourId, juce::Colour(0xffb0d4f0));
    packRenameEditor.setColour(juce::TextEditor::highlightedTextColourId, juce::Colour(0xff1a1a1a));
    packRenameEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
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
    breadcrumbLabel.setColour(juce::Label::backgroundColourId, FinderTheme::topBar);
    breadcrumbLabel.setOpaque(true);
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
    columnBrowser.onFilePreviewToggled = [this](int row, bool expand) {
        if (expand)
        {
            int lastCol = columnBrowser.getPath().isEmpty() ? 0 : (int)columnBrowser.getPath().size();
            columnBrowser.setSelectedRowInColumn(lastCol, row);
            columnBrowser.setExpandedPreviewRow(row);
            playSelectedFile();
            columnBrowser.setPlayingFilePath(playingFilePath);
            startTimer(100);
        }
        else
        {
            processor.stopPreview();
            playingFilePath.clear();
            columnBrowser.setPlayingFilePath({});
            columnBrowser.setExpandedPreviewRow(-1);
            hideAudioPreview();
            stopTimer();
        }
    };
    columnBrowser.onPathChanged = [this] {
        columnPath = columnBrowser.getPath();
        updateBreadcrumb();
    };
    columnBrowser.onColumnWidthsChanged = [this] {
        juce::Rectangle<int> colBounds = getColumnBrowserBounds();
        int cw = columnBrowser.getTotalContentWidth();
        columnBrowser.setBounds(0, 0, juce::jmax(colBounds.getWidth(), cw), colBounds.getHeight());
    };
    columnViewport.setViewedComponent(&columnBrowser, false);
    columnViewport.setScrollBarsShown(true, true);
    addAndMakeVisible(columnViewport);

    audioPreviewStrip.editor = this;
    audioPreviewStrip.setVisible(false);  // no popup strip – play/pause is inline only
    addChildComponent(audioPreviewStrip);

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
    batchPlusDrawable = AssetLoader::getBatchPlusIcon();
    batchPlusBtn.setImages(batchPlusDrawable.get());
    batchPlusBtn.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
    batchPlusBtn.setColour(juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentBlack);
    batchPlusBtn.onClick = [this] {
        // Guard against re-entry while a scan is already running
        if (isBatchScanning.exchange(true)) return;
        batchPlusBtn.setEnabled(false);

        // Show "Scanning..." directly in the drag area so the user can see it
        dragLabel.setText("Scanning...", juce::dontSendNotification);
        dragLabel.setVisible(true);
        repaint();

        if (!processor.batchPlusFolder.isDirectory())
        {
            processor.tryAutoDetectAbletonSamplesFolder();
            if (!processor.batchPlusFolder.isDirectory())
            {
                dragLabel.setText("Drag Sample", juce::dontSendNotification);
                breadcrumbLabel.setVisible(true);
                breadcrumbLabel.setText("Set Batch + Folder in Settings first.", juce::dontSendNotification);
                settingsOverlay->syncFromProcessor();
                settingsOverlay->setVisible(true);
                settingsOverlay->toFront(true);
                batchPlusBtn.setEnabled(true);
                isBatchScanning = false;
                return;
            }
        }

        juce::File folderToScan = processor.batchPlusFolder;
        juce::Component::SafePointer<MagicFoldersEditor> safeThis(this);
        auto audioFilesPtr = std::make_shared<juce::Array<juce::File>>();

        std::thread([safeThis, folderToScan, audioFilesPtr]() {
            // Collect audio files on background thread — doesn't block the UI
            try
            {
                juce::Array<juce::File> allFiles;
                folderToScan.findChildFiles(allFiles, juce::File::findFiles, true);
                for (auto& f : allFiles)
                {
                    juce::String ext = f.getFileExtension().toLowerCase().trimCharactersAtStart(".");
                    if (ext == "wav" || ext == "aif" || ext == "aiff")
                        audioFilesPtr->add(f);
                }
            }
            catch (...)
            {
                // Scan failed (permission denied, I/O error, etc.) — always recover cleanly
                juce::MessageManager::callAsync([safeThis]() {
                    if (safeThis == nullptr) return;
                    safeThis->isBatchScanning = false;
                    safeThis->dragLabel.setText("Drag Sample", juce::dontSendNotification);
                    safeThis->dragLabel.setVisible(true);
                    safeThis->batchPlusBtn.setEnabled(true);
                    safeThis->updateBreadcrumb();
                    safeThis->repaint();
                });
                return;
            }

            // Hand results back to the message thread for UI update
            juce::MessageManager::callAsync([safeThis, audioFilesPtr]() {
                if (safeThis == nullptr) return;
                safeThis->isBatchScanning = false;
                safeThis->processor.clearQueue();
                safeThis->processor.addFiles(*audioFilesPtr);
                safeThis->selectedQueueIndices.clear();
                juce::Rectangle<int> dragInner = safeThis->getDragAreaBounds().reduced(16, 10);
                juce::Rectangle<int> queueViewportBounds = dragInner.withTrimmedRight(MagicFoldersEditor::kBatchPlusRightMargin);
                const int kQueueLineHeight = 18;
                const int kQueueHeaderHeight = 20;
                int contentH = kQueueHeaderHeight + kQueueLineHeight * (int)safeThis->processor.queue.size();
                safeThis->queueListContent.setSize(queueViewportBounds.getWidth(),
                                                   juce::jmax(queueViewportBounds.getHeight(), contentH));
                safeThis->queueViewport.setVisible(true);
                safeThis->dragLabel.setVisible(false);
                safeThis->queueLabel.setVisible(false);
                safeThis->queueViewport.setBounds(queueViewportBounds);
                safeThis->queueListContent.repaint();
                safeThis->updateBreadcrumb();
                safeThis->repaint();
                // Always re-enable — folder was validated before the thread launched
                safeThis->batchPlusBtn.setEnabled(true);
            });
        }).detach();
    };
    // Auto-detect only runs when user clicks Batch+ (not on load), to avoid freezing host and Apple Music permission
    batchPlusBtn.setEnabled(processor.batchPlusFolder.isDirectory());
    addAndMakeVisible(batchPlusBtn);
    queueListContent.editor = this;
    queueListContent.setWantsKeyboardFocus(true);
    queueViewport.setViewedComponent(&queueListContent, false);
    queueViewport.setScrollBarsShown(true, false);
    addChildComponent(queueViewport);
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
        // Always use the selected pack folder (left sidebar) as destination, not the column browser selection.
        // Prefer the ListBox's actual selected row as the source of truth, fall back to selectedPackIndex.
        {
            int listRow = packList.getSelectedRow();
            if (listRow >= 0 && listRow < packDirs.size())
                selectedPackIndex = listRow;
        }
        if (selectedPackIndex >= 0 && selectedPackIndex < packDirs.size())
            processor.currentProcessDirectory = packDirs[selectedPackIndex];
        else
            processor.currentProcessDirectory = processor.outputDirectory;
        if (!processor.currentProcessDirectory.isDirectory())
            processor.currentProcessDirectory = processor.outputDirectory;
        // Immediately swap queue out and show "Processing Samples..." so the user gets feedback right away
        queueViewport.setVisible(false);
        queueLabel.setVisible(false);
        dragLabel.setText("Processing Samples...", juce::dontSendNotification);
        dragLabel.setVisible(true);
        repaint();
        juce::Timer::callAfterDelay(50, [this]() {
            try
            {
                processor.processAll();
                refreshPackList();
                columnBrowser.refreshFromDisk();
                dragLabel.setText("Drag Sample", juce::dontSendNotification);
                dragLabel.setVisible(true);
                updateBreadcrumb();
                repaint();
            }
            catch (const std::exception& ex)
            {
                dragLabel.setText("Drag Sample", juce::dontSendNotification);
                breadcrumbLabel.setVisible(true);
                breadcrumbLabel.setText("Error: " + juce::String(ex.what()), juce::dontSendNotification);
                repaint();
            }
            catch (...)
            {
                dragLabel.setText("Drag Sample", juce::dontSendNotification);
                breadcrumbLabel.setVisible(true);
                breadcrumbLabel.setText("Error: analysis failed.", juce::dontSendNotification);
                repaint();
            }
        });
    };
    addAndMakeVisible(processBtn);

    settingsOverlay->onClose = [this] {
        settingsOverlay->setVisible(false);
        refreshPackList();
        batchPlusBtn.setEnabled(processor.batchPlusFolder.isDirectory());
    };
    addChildComponent(settingsOverlay.get());

    refreshPackList();
    updateBreadcrumb();
    if (processor.outputDirectory.isDirectory() && selectedPackIndex >= 0 && selectedPackIndex < packDirs.size())
    {
        columnBrowser.setRootFolder(packDirs[selectedPackIndex]);
        columnBrowser.setPath(columnPath);
    }
}

MagicFoldersEditor::~MagicFoldersEditor()
{
    if (packListHoverListener)
        removeMouseListener(packListHoverListener.get());
    processor.stopPreview();
    transportSource.setSource(nullptr);
    readerSource.reset();
    sourcePlayer.setSource(nullptr);
    deviceManager.closeAudioDevice();
}

juce::Rectangle<int> MagicFoldersEditor::QueueListContent::getClearBtnBounds() const
{
    if (!editor) return {};
    const int headerHeight = 20;
    const int btnSize = 15;
    const int padH = 8;
    const int gap = 6;
    auto font = interFont(12.0f, true);
    auto& q = editor->processor.queue;
    juce::String header = juce::String(q.size()) + (q.size() == 1 ? " sample in queue" : " samples in queue");
    int textWidth = (int)font.getStringWidth(header);
    int btnX = padH + textWidth + gap;
    int btnY = (headerHeight - btnSize) / 2 - 2;
    return juce::Rectangle<int>(btnX, btnY, btnSize, btnSize);
}

void MagicFoldersEditor::QueueListContent::paint(juce::Graphics& g)
{
    if (!editor) return;
    auto& q = editor->processor.queue;
    if (q.isEmpty()) return;
    using namespace FinderTheme;
    const int padH = 8;
    const int headerTopPad = 2;
    const int lineHeight = 18;
    const int headerHeight = headerTopPad + lineHeight;  // 20
    g.setColour(textCharcoal);
    // Bold header at top: "X samples in queue" (same size as queue items, bold)
    g.setFont(interFont(12.0f, true));
    juce::String header = juce::String(q.size()) + (q.size() == 1 ? " sample in queue" : " samples in queue");
    g.drawText(header, padH, headerTopPad, getWidth() - 2 * padH, lineHeight, juce::Justification::topLeft, true);
    // × clear-queue circle button — sits right after the header text
    auto clearBtn = getClearBtnBounds();
    g.setColour(hoveringClearBtn ? textCharcoal.withAlpha(0.30f) : textCharcoal.withAlpha(0.18f));
    g.fillEllipse(clearBtn.toFloat());
    g.setColour(hoveringClearBtn ? juce::Colour(0xff1a1a1a) : textCharcoal.withAlpha(0.7f));
    g.setFont(interFont(10.0f, true));
    g.drawText(juce::String::fromUTF8("\xc3\x97"), clearBtn, juce::Justification::centred, false);
    // Sample names below, with selection highlight
    g.setFont(interFont(12.0f));
    int y = headerHeight;
    for (int i = 0; i < q.size(); ++i)
    {
        if (editor->selectedQueueIndices.contains(i))
        {
            g.setColour(FinderTheme::sidebarRowSelected);
            g.fillRect(0, y, getWidth(), lineHeight);
            g.setColour(FinderTheme::textOnDark);
        }
        juce::String name = q.getReference(i).sourceFile.getFileName();
        g.drawText(name, padH, y, getWidth() - 2 * padH, lineHeight, juce::Justification::centredLeft, true);
        if (editor->selectedQueueIndices.contains(i))
            g.setColour(textCharcoal);
        y += lineHeight;
    }
}

void MagicFoldersEditor::QueueListContent::mouseMove(const juce::MouseEvent& e)
{
    bool over = getClearBtnBounds().contains(e.getPosition());
    if (over != hoveringClearBtn)
    {
        hoveringClearBtn = over;
        repaint();
    }
}

void MagicFoldersEditor::QueueListContent::mouseExit(const juce::MouseEvent&)
{
    if (hoveringClearBtn)
    {
        hoveringClearBtn = false;
        repaint();
    }
}

void MagicFoldersEditor::QueueListContent::mouseDown(const juce::MouseEvent& e)
{
    if (!editor) return;
    auto& q = editor->processor.queue;
    if (q.isEmpty()) return;
    const int headerHeight = 20;
    const int lineHeight = 18;
    int y = e.getPosition().getY();
    // × button: clear entire queue
    if (y < headerHeight && getClearBtnBounds().contains(e.getPosition()))
    {
        editor->processor.clearQueue();
        editor->selectedQueueIndices.clear();
        editor->dragLabel.setVisible(true);
        editor->queueViewport.setVisible(false);
        editor->repaint();
        return;
    }
    if (y < headerHeight) return;
    int row = (y - headerHeight) / lineHeight;
    if (row < 0 || row >= q.size()) return;
    grabKeyboardFocus();

    if (e.mods.isRightButtonDown())
    {
        if (!editor->selectedQueueIndices.contains(row))
        {
            editor->selectedQueueIndices.clear();
            editor->selectedQueueIndices.add(row);
            editor->queueAnchorIndex = row;
        }
        juce::PopupMenu m;
        m.addItem(1, "Remove");
        m.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {
            if (result == 1 && editor)
                editor->removeSelectedQueueItems();
        });
        repaint();
        return;
    }

    bool cmdOrCtrl = (e.mods.isCommandDown() || e.mods.isCtrlDown());
    if (e.mods.isShiftDown())
    {
        int anchor = editor->queueAnchorIndex >= 0 ? editor->queueAnchorIndex : row;
        int lo = juce::jmin(anchor, row);
        int hi = juce::jmax(anchor, row);
        editor->selectedQueueIndices.clear();
        for (int i = lo; i <= hi; ++i)
            editor->selectedQueueIndices.add(i);
    }
    else if (cmdOrCtrl)
    {
        if (editor->selectedQueueIndices.contains(row))
            editor->selectedQueueIndices.removeAllInstancesOf(row);
        else
            editor->selectedQueueIndices.add(row);
        editor->selectedQueueIndices.sort();
    }
    else
    {
        editor->selectedQueueIndices.clear();
        editor->selectedQueueIndices.add(row);
        editor->queueAnchorIndex = row;
    }
    editor->selectedQueueIndices.sort();
    repaint();
}

bool MagicFoldersEditor::QueueListContent::keyPressed(const juce::KeyPress& key)
{
    if (!editor || editor->selectedQueueIndices.isEmpty())
        return false;
    if (key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey)
    {
        editor->removeSelectedQueueItems();
        return true;
    }
    return false;
}

void MagicFoldersEditor::removeSelectedQueueItems()
{
    if (selectedQueueIndices.isEmpty()) return;
    juce::Array<juce::File> removedFiles;
    for (int idx : selectedQueueIndices)
        if (idx >= 0 && idx < processor.queue.size())
            removedFiles.add(processor.queue.getReference(idx).sourceFile);
    processor.removeQueueItemsAt(selectedQueueIndices);
    if (removedFiles.size() > 0)
    {
        undoQueueRemoveStack.push_back(removedFiles);
        if (undoQueueRemoveStack.size() > (size_t)kMaxUndoQueueRemove)
            undoQueueRemoveStack.erase(undoQueueRemoveStack.begin());
    }
    selectedQueueIndices.clear();
    juce::Rectangle<int> dragInner = getDragAreaBounds().reduced(16, 10);
    juce::Rectangle<int> queueViewportBounds = dragInner.withTrimmedRight(kBatchPlusRightMargin);
    const int kQueueLineHeight = 18;
    const int kQueueHeaderHeight = 20;
    int contentH = kQueueHeaderHeight + kQueueLineHeight * (int)processor.queue.size();
    queueListContent.setSize(queueViewportBounds.getWidth(), juce::jmax(queueViewportBounds.getHeight(), contentH));
    queueListContent.repaint();
    if (processor.queue.isEmpty())
    {
        dragLabel.setVisible(true);
        queueViewport.setVisible(false);
    }
    repaint();
}

void MagicFoldersEditor::undoLastQueueRemove()
{
    if (undoQueueRemoveStack.empty()) return;
    juce::Array<juce::File> files = undoQueueRemoveStack.back();
    undoQueueRemoveStack.pop_back();
    processor.addFiles(files);
    juce::Rectangle<int> dragInner = getDragAreaBounds().reduced(16, 10);
    juce::Rectangle<int> queueViewportBounds = dragInner.withTrimmedRight(kBatchPlusRightMargin);
    const int kQueueLineHeight = 18;
    const int kQueueHeaderHeight = 20;
    int contentH = kQueueHeaderHeight + kQueueLineHeight * (int)processor.queue.size();
    queueListContent.setSize(queueViewportBounds.getWidth(), juce::jmax(queueViewportBounds.getHeight(), contentH));
    queueViewport.setVisible(true);
    dragLabel.setVisible(false);
    queueLabel.setVisible(false);
    queueViewport.setBounds(queueViewportBounds);
    queueListContent.repaint();
    repaint();
}

bool MagicFoldersEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    int k = key.getKeyCode();
    if ((key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown()) && (k == 'z' || k == 'Z'))
    {
        undoLastQueueRemove();
        return true;
    }
    // Forward arrow keys and space to column browser
    if (!settingsOverlay->isVisible() && (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey
            || key == juce::KeyPress::upKey || key == juce::KeyPress::downKey || key == juce::KeyPress::spaceKey))
    {
        if (columnBrowser.keyPressed(key))
        {
            columnBrowser.grabKeyboardFocus();
            return true;
        }
    }
    return false;
}

void MagicFoldersEditor::paint(juce::Graphics& g)
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
    // Queue list is drawn by queueListContent inside queueViewport (scrollable)
    // Process Samples button: full width dark
    juce::Rectangle<int> btnBounds = getProcessButtonBounds();
    g.setColour(processBtnBg);
    g.fillRect(btnBounds);
    // Thick bottom border under Process Samples
    g.setColour(FinderTheme::topBar);
    g.fillRect(0, getHeight() - kThickBorderHeight, getWidth(), kThickBorderHeight);
}

void MagicFoldersEditor::resized()
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
    columnViewport.setBounds(colBounds);
    int cw = columnBrowser.getTotalContentWidth();
    columnBrowser.setBounds(0, 0, juce::jmax(colBounds.getWidth(), cw), colBounds.getHeight());
    bool hasPackSelected = selectedPackIndex >= 0 && selectedPackIndex < packDirs.size();
    columnPlaceholderLabel.setVisible(false);
    columnPlaceholderLabel.setBounds(colBounds);
    juce::Rectangle<int> dragBounds = getDragAreaBounds();
    dragArea.setBounds(dragBounds);
    const int kBatchPlusInsetH = 8;
    const int kBatchPlusW = 80;
    const int kBatchPlusH = 36;
    const int kBatchPlusTopInset = -6;  // slightly below top so button doesn't overlap dash border
    batchPlusBtn.setBounds(dragBounds.getRight() - kBatchPlusW - kBatchPlusInsetH,
                           dragBounds.getY() + kBatchPlusTopInset,
                           kBatchPlusW, kBatchPlusH);
    batchPlusBtn.toFront(true);
    juce::Rectangle<int> dragInner = dragBounds.reduced(16, 10);
    juce::Rectangle<int> queueViewportBounds = dragInner.withTrimmedRight(kBatchPlusRightMargin);
    dragLabel.setBounds(dragInner);
    queueLabel.setBounds(dragInner);
    const int kQueueLineHeight = 18;
    const int kQueueHeaderHeight = 20;  // headerTopPad(2) + lineHeight(18)
    if (!processor.queue.isEmpty())
    {
        dragLabel.setVisible(false);
        queueLabel.setVisible(false);
        queueViewport.setBounds(queueViewportBounds);
        int contentH = kQueueHeaderHeight + kQueueLineHeight * (int)processor.queue.size();
        queueListContent.setSize(queueViewportBounds.getWidth(), juce::jmax(queueViewportBounds.getHeight(), contentH));
        queueViewport.setVisible(true);
    }
    else
    {
        dragLabel.setVisible(true);
        queueViewport.setVisible(false);
        queueLabel.setVisible(false);
    }
    processBtn.setBounds(getProcessButtonBounds());

    settingsOverlay->setBounds(0, 0, w, h);
}

void MagicFoldersEditor::refreshPackList()
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
    // Keep the ListBox visual selection in sync so it never drifts from selectedPackIndex
    if (selectedPackIndex >= 0)
        packList.selectRow(selectedPackIndex, false, true);
    else
        packList.deselectAllRows();
}

void MagicFoldersEditor::createNewPack()
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

void MagicFoldersEditor::updateForwardButtonState()
{
    if (pathForward.isEmpty())
        forwardBtn.setImages(forwardArrowDimmedDrawable.get());
    else
        forwardBtn.setImages(forwardArrowDrawable.get());
}

void MagicFoldersEditor::updateBreadcrumb()
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
    repaint();
}

void MagicFoldersEditor::pushPathToHistory()
{
    pathHistory.add(columnPath);
}

void MagicFoldersEditor::goBack()
{
    if (!pathHistory.isEmpty())
    {
        pathForward.add(columnPath);
        columnPath = pathHistory.getLast();
        pathHistory.removeLast();
    }
    else if (!columnPath.isEmpty())
    {
        pathForward.add(columnPath);
        columnPath.clear();
    }
    else
        return;
    columnBrowser.setPath(columnPath);
    updateBreadcrumb();
    updateForwardButtonState();
    repaint();
}

void MagicFoldersEditor::goForward()
{
    if (pathForward.isEmpty()) return;
    pathHistory.add(columnPath);
    columnPath = pathForward.getLast();
    pathForward.removeLast();
    columnBrowser.setPath(columnPath);
    updateBreadcrumb();
    updateForwardButtonState();
    repaint();
}

void MagicFoldersEditor::PackListHoverListener::mouseMove(const juce::MouseEvent& e)
{
    if (!editor) return;
    juce::Point<int> posInEditor = editor->getLocalPoint(e.eventComponent, e.getPosition());
    if (!editor->getPackListBounds().contains(posInEditor)) { editor->setHoveredPackRow(-1); return; }
    juce::Point<int> listPos = editor->packList.getLocalPoint(editor, posInEditor);
    int row = editor->packList.getRowContainingPosition(listPos.getX(), listPos.getY());
    int total = editor->packNames.size();
    editor->setHoveredPackRow(row >= 0 && row < total ? row : -1);
}

void MagicFoldersEditor::PackListHoverListener::mouseExit(const juce::MouseEvent& e)
{
    if (editor)
        editor->setHoveredPackRow(-1);
}

void MagicFoldersEditor::PackListHoverListener::mouseDown(const juce::MouseEvent& e)
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
            MagicFoldersEditor* ed = this->editor;
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

void MagicFoldersEditor::PackListHoverListener::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (!editor) return;
    juce::Point<int> posInEditor = editor->getLocalPoint(e.eventComponent, e.getPosition());
    if (!editor->getPackListBounds().contains(posInEditor)) return;
    juce::Point<int> listPos = editor->packList.getLocalPoint(editor, posInEditor);
    int row = editor->packList.getRowContainingPosition(listPos.getX(), listPos.getY());
    if (row >= 0)
        editor->tryRenamePack(row);
}

void MagicFoldersEditor::tryRenamePack(int row)
{
    startPackInlineRename(row);
}

void MagicFoldersEditor::startPackInlineRename(int row)
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
    juce::Rectangle<int> textRect = rowRect.withTrimmedLeft(kPackPaddingH).withTrimmedRight(32);
    // Give the inline editor a bit more vertical breathing room inside the row.
    textRect = textRect.reduced(0, kPackPaddingV - 1).expanded(0, 2);
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

void MagicFoldersEditor::commitPackRename()
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

void MagicFoldersEditor::hidePackRenameEditor()
{
    if (editingPackRow < 0) return;
    packRenameEditor.onFocusLost = nullptr;
    removeChildComponent(&packRenameEditor);
    editingPackRow = -1;
    packList.repaint();
}


void MagicFoldersEditor::setHoveredPackRow(int row)
{
    if (row != hoveredPackRow)
    {
        hoveredPackRow = row;
        packList.repaint();
    }
}

void MagicFoldersEditor::mouseMove(const juce::MouseEvent& e)
{
    bool inside = getDragAreaBounds().contains(e.getPosition());
    if (inside != isHoveringDragArea)
    {
        isHoveringDragArea = inside;
        repaint();
    }
}

void MagicFoldersEditor::mouseEnter(const juce::MouseEvent& e)
{
    if (e.eventComponent == &dragArea && !isHoveringDragArea)
    {
        isHoveringDragArea = true;
        repaint();
    }
}

void MagicFoldersEditor::mouseExit(const juce::MouseEvent&)
{
    if (isHoveringDragArea)
    {
        isHoveringDragArea = false;
        repaint();
    }
}

void MagicFoldersEditor::mouseDown(const juce::MouseEvent& e)
{
    handleBreadcrumbClick(e.getPosition().getX(), e.getPosition().getY());
}

void MagicFoldersEditor::handleBreadcrumbClick(int x, int y)
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

void MagicFoldersEditor::playSelectedFile()
{
    juce::File file = columnBrowser.getSelectedFileInLastColumn();
    if (!file.existsAsFile()) return;
    juce::String path = file.getFullPathName();
    if (path == playingFilePath)
    {
        processor.stopPreview();
        playingFilePath.clear();
        columnBrowser.setPlayingFilePath({});
        stopTimer();
        return;
    }
    processor.stopPreview();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (!reader) return;
    double fileSampleRate = juce::jmax(1.0, reader->sampleRate);
    previewLengthSeconds = reader->lengthInSamples / fileSampleRate;
    if (previewLengthSeconds <= 0.0) previewLengthSeconds = 1.0;
    auto source = std::make_unique<juce::AudioFormatReaderSource>(reader.get(), true);
    reader.release();
    processor.setPreviewSource(std::move(source), fileSampleRate, previewLengthSeconds);
    processor.startPreview();
    playingFilePath = path;
    columnBrowser.setPlayingFilePath(path);
    updateBreadcrumb();
    repaint();
    startTimer(100);
}

void MagicFoldersEditor::AudioPreviewStrip::PlayPauseButton::paintButton(juce::Graphics& g, bool highlighted, bool)
{
    g.fillAll(FinderTheme::topBar);
    if (highlighted)
        g.fillAll(FinderTheme::topBar.brighter(0.1f));
    g.setColour(FinderTheme::textOnDark);
    auto b = getLocalBounds().reduced(8).toFloat();
    juce::Path path;
    if (showingPlay)
    {
        path.addTriangle(b.getX(), b.getY(), b.getX(), b.getBottom(), b.getRight(), b.getCentreY());
    }
    else
    {
        float w = juce::jmax(2.0f, b.getWidth() * 0.25f);
        path.addRoundedRectangle(b.getX(), b.getY(), w, b.getHeight(), 1.0f);
        path.addRoundedRectangle(b.getRight() - w, b.getY(), w, b.getHeight(), 1.0f);
    }
    g.fillPath(path);
}

namespace
{
    struct ScrubBarLookAndFeel : juce::LookAndFeel_V4
    {
        void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                             float /*sliderPos*/, float /*minSliderPos*/, float /*maxSliderPos*/,
                             juce::Slider::SliderStyle /*style*/, juce::Slider& slider) override
        {
            const float corner = 2.0f;
            auto full = juce::Rectangle<float>(float(x), float(y), float(width), float(height));
            auto bar = full.reduced(1.0f); // inset so white background shows
            const double val = slider.getValue();
            const double minVal = slider.getMinimum();
            const double maxVal = slider.getMaximum();
            const double range = juce::jmax(0.001, maxVal - minVal);
            const float fillRatio = (float)((val - minVal) / range);

            g.setColour(juce::Colour(0xffffffff)); // white background
            g.fillRoundedRectangle(full, corner);
            g.setColour(juce::Colour(0xffE0E0E0)); // light grey track
            g.fillRoundedRectangle(bar, corner);
            if (fillRatio > 0.0f)
            {
                auto fillBar = bar.withWidth(bar.getWidth() * fillRatio);
                g.setColour(FinderTheme::accent); // blue fill
                g.fillRoundedRectangle(fillBar, corner);
            }
        }

        void drawLinearSliderThumb(juce::Graphics&, int /*x*/, int /*y*/, int /*width*/, int /*height*/,
                                  float /*sliderPos*/, float /*minSliderPos*/, float /*maxSliderPos*/,
                                  juce::Slider::SliderStyle /*style*/, juce::Slider&) override
        {
            // No thumb – progress shown only by fill
        }
    };
}

MagicFoldersEditor::AudioPreviewStrip::AudioPreviewStrip()
{
    scrubBarLook = std::make_unique<ScrubBarLookAndFeel>();
    playPauseBtn.onClick = [this] {
        if (!editor) return;
        if (editor->playingFilePath.isEmpty()) return;
        auto* transport = editor->processor.getPreviewTransport();
        if (!transport) return;
        if (transport->isPlaying())
        {
            transport->stop();
            setPlayPauseLabel(false);
        }
        else
        {
            transport->start();
            setPlayPauseLabel(true);
        }
    };
    addAndMakeVisible(playPauseBtn);
    scrubSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    scrubSlider.setLookAndFeel(scrubBarLook.get());
    scrubSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    scrubSlider.setRange(0.0, 1.0);
    scrubSlider.onValueChange = [this] {
        if (!editor) return;
        if (auto* transport = editor->processor.getPreviewTransport())
            transport->setPosition(scrubSlider.getValue());
    };
    addAndMakeVisible(scrubSlider);
    setOpaque(true);
}

MagicFoldersEditor::AudioPreviewStrip::~AudioPreviewStrip()
{
    scrubSlider.setLookAndFeel(nullptr);
}

void MagicFoldersEditor::AudioPreviewStrip::resized()
{
    auto r = getLocalBounds().reduced(8);
    const int btnW = 24;
    playPauseBtn.setBounds(r.removeFromLeft(btnW));
    scrubSlider.setBounds(r.reduced(4, 6));
}

void MagicFoldersEditor::AudioPreviewStrip::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(FinderTheme::topBar);
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(FinderTheme::settingsDivider);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.0f);
}

void MagicFoldersEditor::AudioPreviewStrip::setPlayPauseLabel(bool playing)
{
    playPauseBtn.showingPlay = !playing;
    playPauseBtn.repaint();
}

void MagicFoldersEditor::AudioPreviewStrip::updateScrubFromTransport()
{
    if (!editor) return;
    if (auto* transport = editor->processor.getPreviewTransport())
        scrubSlider.setValue(transport->getCurrentPosition(), juce::dontSendNotification);
}

void MagicFoldersEditor::showAudioPreview(int row)
{
    (void)row;
    // No popup strip – playback is controlled only by the inline play/pause icon next to each sample.
    // Strip is kept hidden; do not call setVisible(true) here.
}

void MagicFoldersEditor::hideAudioPreview()
{
    audioPreviewStrip.setVisible(false);
}

void MagicFoldersEditor::collapseAudioPreview()
{
    columnBrowser.setExpandedPreviewRow(-1);
    columnBrowser.setPlayingFilePath({});
    processor.stopPreview();
    playingFilePath.clear();
    hideAudioPreview();
    stopTimer();
    columnBrowser.repaint();
}

void MagicFoldersEditor::timerCallback()
{
    auto* transport = processor.getPreviewTransport();
    if (transport && !transport->isPlaying() && !playingFilePath.isEmpty())
    {
        playingFilePath.clear();
        columnBrowser.setPlayingFilePath({});
        columnBrowser.setExpandedPreviewRow(-1);
        stopTimer();
        columnBrowser.repaint();
    }
}

juce::Rectangle<int> MagicFoldersEditor::getLogoPanelBounds() const
{
    return juce::Rectangle<int>(0, 0, kLogoPanelWidth, kHeaderHeight);
}

int MagicFoldersEditor::getContentBottom() const
{
    return getHeight() - kDragAreaHeight - kProcessButtonHeight - 2 * kDragAreaPaddingVertical;
}

juce::Rectangle<int> MagicFoldersEditor::getPackListBounds() const
{
    int contentBottom = getContentBottom();
    return juce::Rectangle<int>(0, kHeaderHeight, kSidebarWidth, contentBottom - kHeaderHeight);
}

juce::Rectangle<int> MagicFoldersEditor::getHeaderBounds() const
{
    return juce::Rectangle<int>(kLogoPanelWidth + 1, 0, getWidth() - kLogoPanelWidth - 1, kHeaderHeight);
}

juce::Rectangle<int> MagicFoldersEditor::getColumnBrowserBounds() const
{
    int contentTop = kHeaderHeight;
    int contentBottom = getContentBottom();
    return juce::Rectangle<int>(kSidebarWidth, contentTop, getWidth() - kSidebarWidth, contentBottom - contentTop);
}

juce::Rectangle<int> MagicFoldersEditor::getDragAreaBounds() const
{
    int contentBottom = getContentBottom();
    int y = contentBottom + kDragAreaPaddingVertical;
    return juce::Rectangle<int>(kDragAreaPadding, y, getWidth() - 2 * kDragAreaPadding, kDragAreaHeight);
}

juce::Rectangle<int> MagicFoldersEditor::getProcessButtonBounds() const
{
    int contentBottom = getContentBottom();
    int y = contentBottom + kDragAreaPaddingVertical + kDragAreaHeight + kDragAreaPaddingVertical;
    return juce::Rectangle<int>(0, y, getWidth(), kProcessButtonHeight);
}

bool MagicFoldersEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    juce::StringArray expanded = expandDroppedPaths(files);
    for (const auto& f : expanded)
        if (isAudioPath(f)) return true;
    return false;
}

void MagicFoldersEditor::fileDragEnter(const juce::StringArray&, int x, int y)
{
    if (getDragAreaBounds().contains(x, y) && !isDragOver)
    {
        isDragOver = true;
        repaint();
    }
}

void MagicFoldersEditor::fileDragMove(const juce::StringArray&, int x, int y)
{
    bool in = getDragAreaBounds().contains(x, y);
    if (in != isDragOver) { isDragOver = in; repaint(); }
}

void MagicFoldersEditor::fileDragExit(const juce::StringArray&)
{
    if (isDragOver) { isDragOver = false; repaint(); }
}

void MagicFoldersEditor::filesDropped(const juce::StringArray& files, int, int)
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
    auto dragInner = getDragAreaBounds().reduced(16, 10);
    juce::Rectangle<int> queueViewportBounds = dragInner.withTrimmedRight(kBatchPlusRightMargin);
    const int kQueueLineHeight = 18;
    const int kQueueHeaderHeight = 20;
    queueViewport.setBounds(queueViewportBounds);
    int contentH = kQueueHeaderHeight + kQueueLineHeight * (int)processor.queue.size();
    queueListContent.setSize(queueViewportBounds.getWidth(), juce::jmax(queueViewportBounds.getHeight(), contentH));
    queueViewport.setVisible(true);
    dragLabel.setVisible(false);
    queueLabel.setVisible(false);
    queueListContent.repaint();
    repaint();
}
