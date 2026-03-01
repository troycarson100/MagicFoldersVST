#pragma once
#include <JuceHeader.h>
#include "FinderTheme.h"
#include "AssetLoader.h"

/** Finder-style column browser: N columns of folders/files, resizable dividers. */
class ColumnBrowserComponent : public juce::Component
{
public:
    ColumnBrowserComponent();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

    void setRootFolder(const juce::File& root);
    void setPath(const juce::Array<juce::File>& path);
    juce::File getSelectedFolder() const;  // last folder in path, or root if path empty
    juce::Array<juce::File> getPath() const { return path; }
    juce::File getFileAt(int column, int row) const;
    juce::File getSelectedFileInLastColumn() const;  // selected item in last column if it's a file

    std::function<void(int column, int row)> onFolderSelected;
    std::function<void(int row)> onFileSelected;
    std::function<void()> onKeyLeft;  // back (e.g. when user presses Left)
    std::function<void()> onPathChanged;  // called after rename so parent can sync

    static constexpr int kMinColumnWidth = 80;
    static constexpr int kDividerWidth = 1;
    static constexpr int kRowHeight = 34;
    static constexpr int kCol1Width = 200;
    static constexpr int kCol2Width = 200;
    static juce::StringArray getDefaultCategories();

private:
    juce::File rootFolder;
    juce::Array<juce::File> path;
    juce::Array<juce::Array<juce::File>> columnItems;  // per column: dirs then files in last column
    juce::Array<int> selectedRowInColumn;
    juce::Array<int> columnWidths;
    int draggingDivider = -1;
    int lastDividerX = 0;
    int editingColumn = -1;
    int editingRow = -1;
    juce::TextEditor renameEditor;
    std::unique_ptr<juce::Drawable> folderIcon;
    std::unique_ptr<juce::Drawable> folderIconWhite;

    juce::Rectangle<int> getColumnBounds(int column) const;
    juce::Rectangle<int> getTextBoundsForCell(int column, int row) const;
    void startInlineRename(int column, int row);
    void commitRename();
    void cancelRename();
    void hideRenameEditor();
    void refreshColumns();
    int getColumnAtX(int x) const;
    int getDividerAtX(int x) const;
    void layoutColumns();
    void paintColumn(juce::Graphics& g, int columnIndex, juce::Rectangle<int> bounds);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColumnBrowserComponent)
};
