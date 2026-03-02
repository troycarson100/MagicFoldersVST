#pragma once
#include <JuceHeader.h>
#include "FinderTheme.h"
#include "AssetLoader.h"

/** Finder-style column browser: N columns of folders/files, resizable dividers. */
class ColumnBrowserComponent : public juce::Component,
                               public juce::DragAndDropTarget
{
public:
    ColumnBrowserComponent();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

    // DragAndDropTarget: accept drops of files/folders onto folder rows
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragEnter(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragMove(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragExit(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& details) override;

    void setRootFolder(const juce::File& root);
    void setPath(const juce::Array<juce::File>& path);
    /** Re-read folder contents from disk and repaint (e.g. after Process Samples creates new folders). */
    void refreshFromDisk();
    juce::File getSelectedFolder() const;  // last folder in path, or root if path empty
    juce::Array<juce::File> getPath() const { return path; }
    juce::File getFileAt(int column, int row) const;
    juce::File getSelectedFileInLastColumn() const;  // selected item in last column if it's a file
    /** Total width needed to show all columns and dividers (for viewport content size). */
    int getTotalContentWidth() const;
    /** Row rectangle in local coordinates (for positioning preview strip). */
    juce::Rectangle<int> getRowBoundsInColumn(int column, int row) const;
    /** Row in last column that is expanded for audio preview (-1 if none). */
    int getExpandedPreviewRow() const { return expandedPreviewRow; }
    void setExpandedPreviewRow(int row) { expandedPreviewRow = row; repaint(); }
    /** Path of file currently playing (for drawing pause on that row). */
    void setPlayingFilePath(const juce::String& path);
    /** Set selected row in a column (for preview: ensure last column selection matches expanded row). */
    void setSelectedRowInColumn(int column, int row);

    std::function<void(int column, int row)> onFolderSelected;
    std::function<void(int row)> onFileSelected;
    std::function<void()> onKeyLeft;  // back (e.g. when user presses Left)
    std::function<void()> onPathChanged;  // called after rename so parent can sync
    std::function<void()> onColumnWidthsChanged;  // called after divider drag so parent can update content size
    /** (row in last column, expand true = show/play, false = hide/stop) */
    std::function<void(int row, bool expand)> onFilePreviewToggled;

    static constexpr int kMinColumnWidth = 80;
    static constexpr int kDividerWidth = 1;
    /** Hit-test width for divider drag (wider than drawn line so it's easier to grab). */
    static constexpr int kDividerGrabWidth = 8;
    static constexpr int kRowHeight = 34;
    static constexpr int kCol1Width = 200;
    static constexpr int kCol2Width = 200;
    /** Prefix for internal drag description so we accept our own file/folder drags. */
    static const juce::String kInternalDragPrefix;
    static juce::StringArray getDefaultCategories();

private:
    juce::File rootFolder;
    juce::Array<juce::File> path;
    juce::Array<juce::Array<juce::File>> columnItems;  // per column: dirs then files in last column
    juce::Array<int> selectedRowInColumn;
    juce::Array<juce::Array<int>> selectedRowsPerColumn;  // multi-select per column
    juce::Array<int> anchorRowInColumn;  // for shift-click range in each column
    juce::Array<int> columnWidths;
    int draggingDivider = -1;
    int lastDividerX = 0;
    int pendingClickCol = -1;
    int pendingClickRow = -1;
    juce::Point<int> mouseDownPosition;
    bool didStartFileDrag = false;
    int dropHighlightCol = -1;
    int dropHighlightRow = -1;
    int editingColumn = -1;
    int editingRow = -1;
    int expandedPreviewRow = -1;  // row in last column with audio preview expanded
    juce::String playingFilePath;  // full path of file currently playing (set by editor)
    juce::TextEditor renameEditor;
    std::unique_ptr<juce::Drawable> folderIcon;
    std::unique_ptr<juce::Drawable> folderIconWhite;
    std::unique_ptr<juce::Drawable> playIcon;
    std::unique_ptr<juce::Drawable> pauseIcon;
    std::unique_ptr<juce::Drawable> playIconDark;
    std::unique_ptr<juce::Drawable> pauseIconAccent;

    juce::Rectangle<int> getColumnBounds(int column) const;
    juce::Rectangle<int> getTextBoundsForCell(int column, int row) const;
    juce::Rectangle<int> getPlayButtonBounds(int column, int row) const;
    juce::File getParentForColumn(int column) const;
    void startInlineRename(int column, int row);
    void showNewFolderDialog(int column);
    void commitRename();
    void cancelRename();
    void hideRenameEditor();
    void refreshColumns();
    int getColumnAtX(int x) const;
    int getDividerAtX(int x) const;
    void layoutColumns();
    void paintColumn(juce::Graphics& g, int columnIndex, juce::Rectangle<int> bounds);
    juce::Image createDragImageForFile(const juce::File& f) const;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColumnBrowserComponent)
};
