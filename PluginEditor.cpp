#include "PluginEditor.h"

using namespace FinderTheme;

static juce::StringArray getKeyList()
{
    return { "C Major", "C Minor", "C# Major", "C# Minor", "D Major", "D Minor",
             "D# Major", "D# Minor", "E Major", "E Minor", "F Major", "F Minor",
             "F# Major", "F# Minor", "G Major", "G Minor", "G# Major", "G# Minor",
             "A Major", "A Minor", "A# Major", "A# Minor", "B Major", "B Minor" };
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

const juce::String kFileDragPrefix("SampleOrganizerFile:");

// --- FolderListModel ---
int SampleOrganizerEditor::FolderListModel::getNumRows()
{
    return editor.folderNames.size();
}

void SampleOrganizerEditor::FolderListModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    bool rowIsDragOver = (row == editor.dragOverFolderIndex);
    if (rowIsDragOver)
        g.fillAll(accent.withAlpha(0.15f));
    else if (selected)
        g.fillAll(selectedBg);
    g.setColour(selected ? textPrimary : textDim);
    g.setFont(10.0f);
    juce::String name = row < editor.folderNames.size() ? editor.folderNames[row] : juce::String();
    int count = 0;
    if (row >= 0 && row < editor.categoryDirs.size())
    {
        juce::File cat = editor.categoryDirs[row];
        juce::File oneShots = cat.getChildFile("One-Shots");
        juce::File loops = cat.getChildFile("Loops");
        if (oneShots.isDirectory()) count += oneShots.getNumberOfChildFiles(juce::File::findFiles, "*");
        if (loops.isDirectory()) count += loops.getNumberOfChildFiles(juce::File::findFiles, "*");
    }
    g.drawText("> " + name, 12, 0, w - 40, h, juce::Justification::centredLeft);
    g.setColour(textSub);
    g.setFont(9.0f);
    g.drawText(juce::String(count), w - 36, 0, 32, h, juce::Justification::centredRight);
}

void SampleOrganizerEditor::FolderListModel::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
    {
        editor.showFolderContextMenu(row, e.getScreenX(), e.getScreenY());
        return;
    }
    editor.selectedFolderIndex = row;
    editor.selectedFileIndex = -1;
    editor.refreshFileList();
    editor.folderList.repaint();
    editor.fileList.repaint();
}

void SampleOrganizerEditor::FolderListModel::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    editor.startRenameFolder(row);
}

// --- FileListModel ---
int SampleOrganizerEditor::FileListModel::getNumRows()
{
    return editor.filesInSelectedCategory.size();
}

void SampleOrganizerEditor::FileListModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (selected)
        g.fillAll(selectedBg);
    juce::String name = row < editor.filesInSelectedCategory.size()
        ? editor.filesInSelectedCategory[row].getFileName() : juce::String();
    juce::String path = row < editor.filesInSelectedCategory.size()
        ? editor.filesInSelectedCategory[row].getFullPathName() : juce::String();
    bool isPlaying = path == editor.playingFilePath;
    g.setColour(textDim);
    g.setFont(9.0f);
    g.drawText("*", 14, 0, 16, h, juce::Justification::centredLeft);
    g.setColour(selected ? textPrimary : textDim);
    g.setFont(10.0f);
    g.drawText(name, 32, 0, w - 90, h, juce::Justification::centredLeft);
    // Play/Stop button area
    int btnLeft = w - 78;
    juce::Rectangle<int> btnRect(btnLeft, (h - 18) / 2, 70, 18);
    g.setColour(isPlaying ? accent : border);
    g.fillRoundedRectangle(btnRect.toFloat(), 2.0f);
    g.setColour(isPlaying ? juce::Colours::white : textSub);
    g.setFont(8.0f);
    g.drawText(isPlaying ? "STOP" : "PLAY", btnRect, juce::Justification::centred);
}

void SampleOrganizerEditor::FileListModel::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
    {
        editor.showFileContextMenu(row, e.getScreenX(), e.getScreenY());
        return;
    }
    int w = editor.fileList.getWidth();
    int btnLeft = w - 78;
    // If click is in play button area, toggle play
    if (e.getPosition().getX() >= btnLeft)
    {
        editor.playFile(row);
        return;
    }
    editor.selectedFileIndex = row;
    editor.fileList.repaint();
}

void SampleOrganizerEditor::FileListModel::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    editor.startRenameFile(row);
}

// --- SampleOrganizerEditor ---
SampleOrganizerEditor::SampleOrganizerEditor(SampleOrganizerProcessor& p)
    : AudioProcessorEditor(&p), processor(p),
      folderListModel(*this), fileListModel(*this)
{
    setSize(700, 540);
    setWantsKeyboardFocus(true);

    formatManager.registerBasicFormats();
    sourcePlayer.setSource(&transportSource);
    deviceManager.initialiseWithDefaultDevices(0, 2);

    // Top bar
    titleLabel.setText("SAMPLE ORGANIZER  v1.0", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(10.0f));
    titleLabel.setColour(juce::Label::textColourId, textSub);
    addAndMakeVisible(titleLabel);

    outputPathLabel.setText("○ NO OUTPUT", juce::dontSendNotification);
    outputPathLabel.setFont(juce::Font(9.0f));
    outputPathLabel.setColour(juce::Label::textColourId, textSub);
    addAndMakeVisible(outputPathLabel);

    folderBtn.setButtonText("FOLDER");
    folderBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    folderBtn.setColour(juce::TextButton::textColourOffId, textDim);
    folderBtn.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser>("Select Output Folder", juce::File(), juce::String());
        chooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser](const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result != juce::File())
                {
                    processor.setOutputDirectory(result);
                    juce::String path = result.getFullPathName();
                    if (path.length() > 45)
                        path = path.substring(0, 22) + "…" + path.substring(path.length() - 20);
                    outputPathLabel.setText("● " + path, juce::dontSendNotification);
                    outputPathLabel.setColour(juce::Label::textColourId, juce::Colour(0xff27c93f));
                    updateStatus("Output: " + result.getFullPathName());
                    refreshFolderList();
                    refreshFileList();
                }
            });
    };
    addAndMakeVisible(folderBtn);

    // Metadata bar
    juce::StringArray keys = getKeyList();
    keySelector.addItemList(keys, 1);
    int keyIndex = keys.indexOf(processor.projectKey);
    keySelector.setSelectedItemIndex(keyIndex >= 0 ? keyIndex : 0);
    keySelector.setColour(juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    keySelector.setColour(juce::ComboBox::textColourId, textPrimary);
    keySelector.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    keySelector.onChange = [this] { processor.projectKey = keySelector.getText(); };
    addAndMakeVisible(keySelector);

    bpmDownBtn.setButtonText("-");
    bpmDownBtn.onClick = [this] { processor.projectBPM = juce::jmax(60, processor.projectBPM - 1); bpmLabel.setText(juce::String(processor.projectBPM), juce::dontSendNotification); };
    bpmUpBtn.setButtonText("+");
    bpmUpBtn.onClick = [this] { processor.projectBPM = juce::jmin(200, processor.projectBPM + 1); bpmLabel.setText(juce::String(processor.projectBPM), juce::dontSendNotification); };
    bpmLabel.setText(juce::String(processor.projectBPM), juce::dontSendNotification);
    bpmLabel.setColour(juce::Label::textColourId, textPrimary);
    addAndMakeVisible(bpmDownBtn);
    addAndMakeVisible(bpmLabel);
    addAndMakeVisible(bpmUpBtn);

    genreInput.setText(processor.defaultGenre);
    genreInput.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    genreInput.setColour(juce::TextEditor::textColourId, textPrimary);
    genreInput.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    genreInput.onTextChange = [this] { processor.defaultGenre = genreInput.getText(); };
    addAndMakeVisible(genreInput);

    processBtn.setButtonText("PROCESS");
    processBtn.setColour(juce::TextButton::buttonColourId, accent);
    processBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
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
        int count = processor.queue.size();
        updateStatus("Processing " + juce::String(count) + " samples…");
        juce::Timer::callAfterDelay(50, [this]()
        {
            try
            {
                processor.processAll();
                updateStatus("Done! " + juce::String(processor.processed.size()) + " samples organized.");
            }
            catch (const std::exception& ex)
            {
                updateStatus("Error: " + juce::String(ex.what()));
            }
            catch (...)
            {
                updateStatus("Error: analysis failed.");
            }
            refreshFolderList();
            refreshFileList();
        });
    };
    addAndMakeVisible(processBtn);

    clearBtn.setButtonText("CLEAR");
    clearBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    clearBtn.setColour(juce::TextButton::textColourOffId, textDim);
    clearBtn.onClick = [this]
    {
        processor.clearQueue();
        updateStatus("Queue cleared.");
    };
    addAndMakeVisible(clearBtn);

    // Folder list
    folderList.setModel(&folderListModel);
    folderList.setRowHeight(kFinderRowHeight);
    folderList.setColour(juce::ListBox::backgroundColourId, bg);
    folderList.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    folderList.setOutlineThickness(0);
    addAndMakeVisible(folderList);

    // File list (draggable)
    fileList.setModel(&fileListModel);
    fileList.setRowHeight(kFinderRowHeight);
    fileList.setColour(juce::ListBox::backgroundColourId, bg);
    fileList.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    fileList.setOutlineThickness(0);
    addAndMakeVisible(fileList);
    fileList.addMouseListener(this, false);

    // Rename editor (hidden until used)
    renameEditor.setMultiLine(false);
    renameEditor.setColour(juce::TextEditor::backgroundColourId, border);
    renameEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    renameEditor.setColour(juce::TextEditor::outlineColourId, accent);
    renameEditor.onReturnKey = [this] { commitRename(); };
    renameEditor.onEscapeKey = [this] { cancelRename(); };
    renameEditor.onFocusLost = [this] { commitRename(); };
    addChildComponent(renameEditor);

    // Drop zone
    dropZoneLabel.setText("DROP WAV / AIF FILES TO QUEUE  -  OR DRAG FROM ABLETON", juce::dontSendNotification);
    dropZoneLabel.setColour(juce::Label::textColourId, textSub);
    dropZoneLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(dropZoneLabel);

    statusLabel.setText("Ready", juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, textSub);
    addAndMakeVisible(statusLabel);

    if (processor.outputDirectory.isDirectory())
    {
        outputPathLabel.setText("● " + processor.outputDirectory.getFullPathName(), juce::dontSendNotification);
        outputPathLabel.setColour(juce::Label::textColourId, juce::Colour(0xff27c93f));
        refreshFolderList();
    }

    refreshFileList();
}

SampleOrganizerEditor::~SampleOrganizerEditor()
{
    fileList.removeMouseListener(this);
    transportSource.setSource(nullptr);
    readerSource.reset();
    sourcePlayer.setSource(nullptr);
    deviceManager.closeAudioDevice();
}

void SampleOrganizerEditor::paint(juce::Graphics& g)
{
    g.fillAll(bg);

    juce::Rectangle<int> dropZone = getDropZoneBounds();
    g.setColour(isDragOver ? accent.withAlpha(0.15f) : hover);
    g.fillRect(dropZone);
    g.setColour(border);
    g.drawRect(dropZone, 1);

    // Top bar panel
    g.setColour(panel);
    g.fillRect(0, 0, getWidth(), 44);
    g.setColour(border);
    g.drawHorizontalLine(43, 0, (float)getWidth());

    // Metadata bar
    g.setColour(panel);
    g.fillRect(0, 44, getWidth(), 44);
    g.setColour(border);
    g.drawHorizontalLine(87, 0, (float)getWidth());

    // Finder column headers
    juce::Rectangle<int> folderBounds = getFolderListBounds();
    g.setColour(panel);
    g.fillRect(folderBounds.getX(), folderBounds.getY(), folderBounds.getWidth(), 24);
    g.setColour(border);
    g.drawHorizontalLine(folderBounds.getY() + 23, (float)folderBounds.getX(), (float)(folderBounds.getX() + folderBounds.getWidth()));
    g.setColour(textSub);
    g.setFont(8.0f);
    g.drawText("FOLDERS", folderBounds.getX() + 12, folderBounds.getY() + 4, folderBounds.getWidth() - 12, 16, juce::Justification::centredLeft);

    juce::Rectangle<int> fileBounds = getFileListBounds();
    g.setColour(panel);
    g.fillRect(fileBounds.getX(), fileBounds.getY(), fileBounds.getWidth(), 24);
    g.setColour(border);
    g.drawHorizontalLine(fileBounds.getY() + 23, (float)fileBounds.getX(), (float)(fileBounds.getX() + fileBounds.getWidth()));
    g.setColour(textSub);
    g.setFont(8.0f);
    juce::String catName = selectedFolderIndex >= 0 && selectedFolderIndex < folderNames.size() ? folderNames[selectedFolderIndex].toUpperCase() : "FILES";
    g.drawText(catName + "  -  " + juce::String(filesInSelectedCategory.size()) + " FILES", fileBounds.getX() + 12, fileBounds.getY() + 4, fileBounds.getWidth() - 12, 16, juce::Justification::centredLeft);

    // Status bar
    int statusY = getHeight() - 28;
    g.setColour(panel);
    g.fillRect(0, statusY, getWidth(), 28);
    g.setColour(border);
    g.drawHorizontalLine(statusY, 0, (float)getWidth());
}

void SampleOrganizerEditor::resized()
{
    int w = getWidth();
    int topH = 44;
    int metaH = 44;
    int finderY = topH + metaH;
    int finderH = 320;
    int dropH = 52;
    int statusH = 28;

    titleLabel.setBounds(16, 12, 220, 20);
    outputPathLabel.setBounds(w - 200, 12, 120, 20);
    folderBtn.setBounds(w - 72, 10, 60, 24);

    keySelector.setBounds(14, 52, 100, 28);
    bpmDownBtn.setBounds(124, 56, 24, 20);
    bpmLabel.setBounds(148, 52, 28, 28);
    bpmUpBtn.setBounds(176, 56, 24, 20);
    genreInput.setBounds(210, 52, 70, 28);
    processBtn.setBounds(w - 170, 50, 80, 28);
    clearBtn.setBounds(w - 82, 50, 68, 28);

    juce::Rectangle<int> folderBounds = getFolderListBounds();
    folderList.setBounds(folderBounds.getX(), folderBounds.getY() + 24, folderBounds.getWidth(), folderBounds.getHeight() - 24);

    juce::Rectangle<int> fileBounds = getFileListBounds();
    fileList.setBounds(fileBounds.getX(), fileBounds.getY() + 24, fileBounds.getWidth(), fileBounds.getHeight() - 24);

    dropZoneLabel.setBounds(0, finderY + finderH, w, dropH);
    statusLabel.setBounds(16, getHeight() - statusH - 6, w - 32, 20);
}

void SampleOrganizerEditor::mouseDown(const juce::MouseEvent& e)
{
    if (e.eventComponent == &fileList)
        fileDragStartRow = fileList.getRowContainingPosition(e.getPosition().getX(), e.getPosition().getY());
}

void SampleOrganizerEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (e.eventComponent != &fileList || fileDragStartRow < 0) return;
    if (e.getDistanceFromDragStart() > 8)
    {
        startDragFile(fileDragStartRow);
        fileDragStartRow = -1;
    }
}

void SampleOrganizerEditor::refreshFolderList()
{
    folderNames.clear();
    categoryDirs.clear();
    if (!processor.outputDirectory.isDirectory())
    {
        selectedFolderIndex = -1;
        folderList.updateContent();
        return;
    }
    for (const auto& f : processor.outputDirectory.findChildFiles(juce::File::findDirectories, false))
    {
        folderNames.add(f.getFileName());
        categoryDirs.add(f);
    }
    if (selectedFolderIndex >= folderNames.size())
        selectedFolderIndex = folderNames.size() > 0 ? 0 : -1;
    folderList.updateContent();
    refreshFileList();
}

void SampleOrganizerEditor::refreshFileList()
{
    filesInSelectedCategory.clear();
    if (selectedFolderIndex < 0 || selectedFolderIndex >= categoryDirs.size())
    {
        fileList.updateContent();
        return;
    }
    juce::File cat = categoryDirs[selectedFolderIndex];
    juce::File oneShots = cat.getChildFile("One-Shots");
    juce::File loops = cat.getChildFile("Loops");
    if (oneShots.isDirectory())
        for (const auto& f : oneShots.findChildFiles(juce::File::findFiles, false, "*.wav;*.aif;*.aiff"))
            filesInSelectedCategory.add(f);
    if (loops.isDirectory())
        for (const auto& f : loops.findChildFiles(juce::File::findFiles, false, "*.wav;*.aif;*.aiff"))
            filesInSelectedCategory.add(f);
    struct FileNameCompare
    {
        int compareElements(const juce::File& a, const juce::File& b) const { return a.getFileName().compareNatural(b.getFileName()); }
    };
    FileNameCompare fc;
    filesInSelectedCategory.sort(fc);
    if (selectedFileIndex >= filesInSelectedCategory.size())
        selectedFileIndex = -1;
    fileList.updateContent();
}

void SampleOrganizerEditor::updateStatus(const juce::String& msg)
{
    statusLabel.setText(msg, juce::dontSendNotification);
}

void SampleOrganizerEditor::showFolderContextMenu(int row, int screenX, int screenY)
{
    if (row < 0 || row >= folderNames.size()) return;
    juce::PopupMenu m;
    m.addItem("Rename Folder", [this, row]() { startRenameFolder(row); });
    juce::String revealLabel =
#if JUCE_MAC
        "Reveal in Finder";
#else
        "Show in Explorer";
#endif
    m.addItem(revealLabel, [this, row]()
    {
        if (row < categoryDirs.size())
            revealInFinder(categoryDirs[row]);
    });
    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(screenX, screenY, 1, 1)));
}

void SampleOrganizerEditor::showFileContextMenu(int row, int screenX, int screenY)
{
    if (row < 0 || row >= filesInSelectedCategory.size()) return;
    juce::File file = filesInSelectedCategory[row];
    juce::PopupMenu m;
    m.addItem("Rename File", [this, row]() { startRenameFile(row); });
    m.addItem(
#if JUCE_MAC
        "Reveal in Finder"
#else
        "Show in Explorer"
#endif
        , [this, file]() { revealInFinder(file); });
    m.addSeparator();
    m.addItem("Delete", [this, file]() { deleteFile(file); });
    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(screenX, screenY, 1, 1)));
}

void SampleOrganizerEditor::startRenameFolder(int row)
{
    if (row < 0 || row >= folderNames.size()) return;
    renameTarget = RenameTarget::Folder;
    renamingRow = row;
    renameInitialValue = folderNames[row];
    renameEditor.setText(renameInitialValue);
    renameEditor.setVisible(true);
    juce::Rectangle<int> listBounds = folderList.getBounds();
    int rowY = 24 + row * kFinderRowHeight;
    renameEditor.setBounds(12, listBounds.getY() + rowY + 2, listBounds.getWidth() - 24, kFinderRowHeight - 4);
    renameEditor.toFront(true);
    renameEditor.grabKeyboardFocus();
    renameEditor.selectAll();
}

void SampleOrganizerEditor::startRenameFile(int row)
{
    if (row < 0 || row >= filesInSelectedCategory.size()) return;
    juce::File f = filesInSelectedCategory[row];
    renameTarget = RenameTarget::File;
    renamingRow = row;
    renameInitialValue = f.getFileNameWithoutExtension();
    renameEditor.setText(renameInitialValue);
    renameEditor.setVisible(true);
    juce::Rectangle<int> listBounds = fileList.getBounds();
    int rowY = 24 + row * kFinderRowHeight;
    renameEditor.setBounds(32, listBounds.getY() + rowY + 2, listBounds.getWidth() - 110, kFinderRowHeight - 4);
    renameEditor.toFront(true);
    renameEditor.grabKeyboardFocus();
    renameEditor.selectAll();
}

void SampleOrganizerEditor::commitRename()
{
    juce::String newName = renameEditor.getText().trim();
    if (renameTarget == RenameTarget::Folder && renamingRow >= 0 && renamingRow < categoryDirs.size())
    {
        if (newName.isEmpty() || newName == renameInitialValue)
        {
            cancelRename();
            return;
        }
        juce::File dir = categoryDirs[renamingRow];
        juce::File parent = dir.getParentDirectory();
        juce::File newDir = parent.getChildFile(newName);
        if (dir.moveFileTo(newDir))
        {
            categoryDirs.set(renamingRow, newDir);
            folderNames.set(renamingRow, newName);
            folderList.updateContent();
            updateStatus("Renamed folder to " + newName);
        }
        else
            updateStatus("Could not rename folder.");
    }
    else if (renameTarget == RenameTarget::File && renamingRow >= 0 && renamingRow < filesInSelectedCategory.size())
    {
        if (newName.isEmpty())
        {
            cancelRename();
            return;
        }
        juce::File file = filesInSelectedCategory[renamingRow];
        juce::String ext = file.getFileExtension();
        juce::File newFile = file.getParentDirectory().getChildFile(newName + ext);
        if (file.moveFileTo(newFile))
        {
            filesInSelectedCategory.set(renamingRow, newFile);
            fileList.updateContent();
            updateStatus("Renamed file to " + newFile.getFileName());
        }
        else
            updateStatus("Could not rename file.");
    }
    renameEditor.setVisible(false);
    renameTarget = RenameTarget::None;
    renamingRow = -1;
}

void SampleOrganizerEditor::cancelRename()
{
    renameEditor.setVisible(false);
    renameTarget = RenameTarget::None;
    renamingRow = -1;
}

void SampleOrganizerEditor::revealInFinder(const juce::File& fileOrFolder)
{
    if (fileOrFolder.exists())
    {
        fileOrFolder.revealToUser();
        updateStatus("Reveal: " + fileOrFolder.getFullPathName());
    }
}

void SampleOrganizerEditor::deleteFile(const juce::File& file)
{
    if (!file.existsAsFile()) return;
    if (file.moveToTrash())
    {
        refreshFileList();
        updateStatus("Moved to trash: " + file.getFileName());
    }
    else
        updateStatus("Could not delete file.");
}

void SampleOrganizerEditor::moveFileToCategory(const juce::File& file, const juce::String& categoryName)
{
    if (!processor.outputDirectory.isDirectory() || !file.existsAsFile()) return;
    juce::File catDir = processor.outputDirectory.getChildFile(categoryName);
    juce::File oneShots = catDir.getChildFile("One-Shots");
    juce::File loops = catDir.getChildFile("Loops");
    oneShots.createDirectory();
    loops.createDirectory();
    juce::String ext = file.getFileExtension();
    juce::String baseName = file.getFileNameWithoutExtension();
    juce::File dest = oneShots.getChildFile(baseName + ext);
    int n = 1;
    while (dest.existsAsFile())
        dest = oneShots.getChildFile(baseName + "_" + juce::String(n++) + ext);
    if (file.moveFileTo(dest))
    {
        refreshFolderList();
        refreshFileList();
        updateStatus("Moved " + file.getFileName() + " → " + categoryName);
    }
    else
        updateStatus("Could not move file.");
}

void SampleOrganizerEditor::playFile(int row)
{
    if (row < 0 || row >= filesInSelectedCategory.size()) return;
    juce::File file = filesInSelectedCategory[row];
    juce::String path = file.getFullPathName();
    if (path == playingFilePath)
    {
        stopPlayback();
        return;
    }
    stopPlayback();
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
    updateStatus("Playing: " + file.getFileName());
    fileList.repaint();
}

void SampleOrganizerEditor::stopPlayback()
{
    transportSource.stop();
    transportSource.setSource(nullptr);
    readerSource.reset();
    playingFilePath.clear();
    updateStatus("Stopped");
    fileList.repaint();
}

void SampleOrganizerEditor::startDragFile(int row)
{
    if (row < 0 || row >= filesInSelectedCategory.size()) return;
    juce::File file = filesInSelectedCategory[row];
    juce::String path = file.getFullPathName();
    juce::var desc(kFileDragPrefix + path);
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        container->startDragging(desc, this, juce::Image(), true, nullptr);
}

bool SampleOrganizerEditor::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details)
{
    juce::String desc = details.description.toString();
    return desc.startsWith(kFileDragPrefix);
}

void SampleOrganizerEditor::itemDropped(const juce::DragAndDropTarget::SourceDetails& details)
{
    juce::String desc = details.description.toString();
    if (!desc.startsWith(kFileDragPrefix)) return;
    juce::String path = desc.substring(kFileDragPrefix.length());
    juce::File file(path);
    if (!file.existsAsFile()) return;
    juce::Point<int> pos = details.localPosition;
    juce::Rectangle<int> folderBounds = getFolderListBounds();
    folderBounds.removeFromTop(24);
    if (!folderBounds.contains(pos))
        return;
    int row = (pos.getY() - folderBounds.getY()) / kFinderRowHeight;
    if (row < 0 || row >= folderNames.size()) return;
    moveFileToCategory(file, folderNames[row]);
}

juce::Rectangle<int> SampleOrganizerEditor::getDropZoneBounds() const
{
    int finderY = 88;
    int finderH = 320;
    return juce::Rectangle<int>(0, finderY + finderH, getWidth(), 52);
}

juce::Rectangle<int> SampleOrganizerEditor::getFolderListBounds() const
{
    int finderY = 88;
    int finderH = 320;
    return juce::Rectangle<int>(0, finderY, kFolderListWidth, finderH);
}

juce::Rectangle<int> SampleOrganizerEditor::getFileListBounds() const
{
    int finderY = 88;
    int finderH = 320;
    return juce::Rectangle<int>(kFolderListWidth, finderY, getWidth() - kFolderListWidth, finderH);
}

// --- File drag/drop (external) ---
bool SampleOrganizerEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    juce::StringArray expanded = expandDroppedPaths(files);
    for (const auto& f : expanded)
        if (isAudioPath(f)) return true;
    return false;
}

void SampleOrganizerEditor::fileDragEnter(const juce::StringArray&, int x, int y)
{
    if (getDropZoneBounds().contains(x, y) && !isDragOver)
    {
        isDragOver = true;
        repaint();
    }
}

void SampleOrganizerEditor::fileDragMove(const juce::StringArray&, int x, int y)
{
    bool in = getDropZoneBounds().contains(x, y);
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
    updateStatus(juce::String(processor.queue.size()) + " samples queued. Hit Process.");
}
