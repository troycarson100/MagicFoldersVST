#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "FinderTheme.h"

class SampleOrganizerEditor : public juce::AudioProcessorEditor,
                              public juce::FileDragAndDropTarget,
                              public juce::DragAndDropTarget
{
public:
    SampleOrganizerEditor(SampleOrganizerProcessor&);
    ~SampleOrganizerEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& details) override;

private:
    SampleOrganizerProcessor& processor;

    // --- Top bar ---
    juce::Label titleLabel;
    juce::Label outputPathLabel;
    juce::TextButton folderBtn;

    // --- Metadata bar ---
    juce::ComboBox keySelector;
    juce::TextButton bpmDownBtn;
    juce::Label bpmLabel;
    juce::TextButton bpmUpBtn;
    juce::TextEditor genreInput;
    juce::TextButton processBtn;
    juce::TextButton clearBtn;

    // --- Finder: folder list (left) ---
    juce::ListBox folderList;
    juce::StringArray folderNames;
    juce::Array<juce::File> categoryDirs;  // same order as folderNames
    int selectedFolderIndex = -1;
    int dragOverFolderIndex = -1;

    // --- Finder: file list (right) ---
    juce::ListBox fileList;
    juce::Array<juce::File> filesInSelectedCategory;  // full paths
    int selectedFileIndex = -1;
    juce::String playingFilePath;  // full path of file being previewed
    int fileDragStartRow = -1;     // for starting drag from file list

    // --- Rename state ---
    enum class RenameTarget { None, Folder, File };
    RenameTarget renameTarget = RenameTarget::None;
    int renamingRow = -1;
    juce::String renameInitialValue;
    juce::TextEditor renameEditor;

    // --- Drop zone & status ---
    juce::Label dropZoneLabel;
    juce::Label statusLabel;
    bool isDragOver = false;

    // --- Playback ---
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    juce::AudioTransportSource transportSource;
    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;

    // --- List models (reference editor data) ---
    class FolderListModel : public juce::ListBoxModel
    {
    public:
        FolderListModel(SampleOrganizerEditor& e) : editor(e) {}
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    private:
        SampleOrganizerEditor& editor;
    };
    class FileListModel : public juce::ListBoxModel
    {
    public:
        FileListModel(SampleOrganizerEditor& e) : editor(e) {}
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    private:
        SampleOrganizerEditor& editor;
    };
    FolderListModel folderListModel;
    FileListModel fileListModel;

    // --- Helpers ---
    void refreshFolderList();
    void refreshFileList();
    void updateStatus(const juce::String& msg);
    void showFolderContextMenu(int row, int screenX, int screenY);
    void showFileContextMenu(int row, int screenX, int screenY);
    void startRenameFolder(int row);
    void startRenameFile(int row);
    void commitRename();
    void cancelRename();
    void revealInFinder(const juce::File& fileOrFolder);
    void deleteFile(const juce::File& file);
    void moveFileToCategory(const juce::File& file, const juce::String& categoryName);
    void playFile(int row);
    void stopPlayback();
    void startDragFile(int row);
    juce::Rectangle<int> getDropZoneBounds() const;
    juce::Rectangle<int> getFolderListBounds() const;
    juce::Rectangle<int> getFileListBounds() const;

    static constexpr int kFolderListWidth = 200;
    static constexpr int kFinderRowHeight = 26;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleOrganizerEditor)
};
