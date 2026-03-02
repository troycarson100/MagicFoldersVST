#include "ColumnBrowserComponent.h"

using namespace FinderTheme;

namespace
{
struct RenameDialogContent : juce::Component
{
    RenameDialogContent(const juce::String& initialName, juce::String* resultOut_)
        : resultOut(resultOut_)
    {
        label.setText("Enter new name:", juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, juce::Colours::black);
        addAndMakeVisible(label);
        te.setText(initialName);
        te.setSelectAllWhenFocused(true);
        addAndMakeVisible(te);
        okBtn.setButtonText("OK");
        okBtn.onClick = [this] {
            if (resultOut) *resultOut = te.getText().trim();
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState(1);
        };
        addAndMakeVisible(okBtn);
        cancelBtn.setButtonText("Cancel");
        cancelBtn.onClick = [this] {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState(0);
        };
        addAndMakeVisible(cancelBtn);
        setSize(360, 110);
    }
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
    juce::String* resultOut = nullptr;
    juce::Label label;
    juce::TextEditor te;
    juce::TextButton okBtn;
    juce::TextButton cancelBtn;
};

}

static juce::String sanitiseRename(const juce::String& s)
{
    juce::String t = s.trim();
    while (t.contains("/") || t.contains("\\"))
        t = t.replace("/", "").replace("\\", "");
    return t;
}

juce::StringArray ColumnBrowserComponent::getDefaultCategories()
{
    return { "Bass", "Drums", "Guitar", "Melodic", "Textures", "Songstarter", "FX", "Vocals", "Other" };
}

const juce::String ColumnBrowserComponent::kInternalDragPrefix = "MagicFolders:";

ColumnBrowserComponent::ColumnBrowserComponent()
{
    setWantsKeyboardFocus(true);
    folderIcon = AssetLoader::getFolderDarkGreyIcon();
    if (folderIcon)
    {
        folderIconWhite = folderIcon->createCopy();
        if (folderIconWhite)
            folderIconWhite->replaceColour(juce::Colour(0xff393E46), juce::Colours::white);
    }
    if (auto basePlay = AssetLoader::getPlayIcon())
    {
        // Base white icon (used when row is selected)
        playIcon = std::move(basePlay);
        if (playIcon)
        {
            playIconDark = playIcon->createCopy();
            if (playIconDark)
                playIconDark->replaceColour(juce::Colours::white, FinderTheme::textCharcoal);
        }
    }
    if (auto basePause = AssetLoader::getPauseIcon())
    {
        // Base white icon (used when row is selected)
        pauseIcon = std::move(basePause);
        if (pauseIcon)
        {
            pauseIconAccent = pauseIcon->createCopy();
            if (pauseIconAccent)
                pauseIconAccent->replaceColour(juce::Colours::white, FinderTheme::accent);
        }
    }
    renameEditor.setMultiLine(false);
    // Inline rename should be a clean white box without a visible grey border.
    renameEditor.setBorder(juce::BorderSize<int>(0));
    renameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    renameEditor.setColour(juce::TextEditor::textColourId, juce::Colour(0xff1a1a1a));
    renameEditor.setColour(juce::TextEditor::highlightColourId, juce::Colour(0xffb0d4f0));
    renameEditor.setColour(juce::TextEditor::highlightedTextColourId, juce::Colour(0xff1a1a1a));
    renameEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    renameEditor.onReturnKey = [this] { commitRename(); };
    renameEditor.onEscapeKey = [this] { cancelRename(); };
}

juce::Rectangle<int> ColumnBrowserComponent::getColumnBounds(int column) const
{
    int x = 0;
    for (int i = 0; i < columnWidths.size() && i <= column; ++i)
    {
        if (i == column)
            return juce::Rectangle<int>(x, 0, columnWidths[i], getHeight());
        x += columnWidths[i] + kDividerWidth;
    }
    return {};
}

juce::Rectangle<int> ColumnBrowserComponent::getTextBoundsForCell(int column, int row) const
{
    const int padH = 12;
    const int padV = 9;
    const int fullIconWidth = 22;
    juce::Rectangle<int> bounds = getColumnBounds(column);
    if (bounds.isEmpty()) return {};
    int textLeft = bounds.getX() + padH + fullIconWidth + 6;
    juce::Rectangle<int> rowRect(bounds.getX(), row * kRowHeight, bounds.getWidth(), kRowHeight);
    return juce::Rectangle<int>(textLeft, rowRect.getY() + padV, rowRect.getRight() - textLeft - padH, kRowHeight - 2 * padV);
}

juce::Rectangle<int> ColumnBrowserComponent::getPlayButtonBounds(int column, int row) const
{
    const int padH = 12;
    const int fullIconWidth = 22;
    juce::Rectangle<int> bounds = getColumnBounds(column);
    if (bounds.isEmpty()) return {};
    return juce::Rectangle<int>(bounds.getX() + padH, row * kRowHeight, fullIconWidth, kRowHeight);
}

void ColumnBrowserComponent::setPlayingFilePath(const juce::String& path)
{
    if (playingFilePath == path)
        return;
    playingFilePath = path;
    repaint();
}

void ColumnBrowserComponent::startInlineRename(int column, int row)
{
    if (column < 0 || column >= columnItems.size()) return;
    const auto& items = columnItems.getReference(column);
    if (row < 0 || row >= items.size()) return;
    juce::File f = items.getReference(row);
    hideRenameEditor();
    editingColumn = column;
    editingRow = row;
    renameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    renameEditor.setColour(juce::TextEditor::textColourId, juce::Colour(0xff1a1a1a));
    renameEditor.setColour(juce::TextEditor::highlightedTextColourId, juce::Colour(0xff1a1a1a));
    renameEditor.setText(f.getFileName(), false);
    auto editBounds = getTextBoundsForCell(column, row);
    // Slightly expand vertically so there is more padding above/below the text.
    editBounds = editBounds.expanded(0, 2);
    renameEditor.setBounds(editBounds);
    renameEditor.onFocusLost = [this] { commitRename(); };
    addAndMakeVisible(renameEditor);
    renameEditor.selectAll();
    renameEditor.grabKeyboardFocus();
}

void ColumnBrowserComponent::commitRename()
{
    if (editingColumn < 0 || editingRow < 0) return;
    if (editingColumn >= columnItems.size()) { hideRenameEditor(); return; }
    const auto& items = columnItems.getReference(editingColumn);
    if (editingRow >= items.size()) { hideRenameEditor(); return; }
    juce::File f = items.getReference(editingRow);
    juce::String currentName = f.getFileName();
    juce::String newName = sanitiseRename(renameEditor.getText());
    hideRenameEditor();
    if (newName.isEmpty() || newName == currentName) return;
    juce::File dest = f.getParentDirectory().getChildFile(newName);
    if (dest.exists()) return;
    if (!f.moveFileTo(dest)) return;
    if (f == rootFolder)
        setRootFolder(dest);
    else if (path.contains(f))
    {
        juce::Array<juce::File> newPath;
        for (auto& p : path)
            newPath.add(p == f ? dest : p);
        setPath(newPath);
    }
    else
        refreshColumns();
    if (onPathChanged)
        onPathChanged();
}

void ColumnBrowserComponent::cancelRename()
{
    hideRenameEditor();
}

void ColumnBrowserComponent::showNewFolderDialog(int column)
{
    juce::File parentDir = getParentForColumn(column);
    if (!parentDir.isDirectory())
        return;
    // Create new folder immediately with default name, then open rename so user can name it.
    juce::String baseName("New Folder");
    juce::File newDir = parentDir.getChildFile(baseName);
    int suffix = 0;
    while (newDir.exists())
        newDir = parentDir.getChildFile(baseName + " " + juce::String(++suffix));
    if (!newDir.createDirectory())
    {
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "New Folder",
            "Could not create the folder. Check permissions and path.",
            nullptr);
        return;
    }
    juce::String createdPath = newDir.getFullPathName();
    refreshColumns();
    if (onPathChanged)
        onPathChanged();
    // Defer opening rename so the list has painted and focus is ready.
    juce::MessageManager::callAsync([this, column, createdPath]()
    {
        if (!isVisible())
            return;
        refreshColumns();
        if (onPathChanged)
            onPathChanged();
        if (column < 0 || column >= columnItems.size())
            return;
        const auto& items = columnItems.getReference(column);
        int row = -1;
        juce::String normalCreated = juce::File(createdPath).getFullPathName();
        for (int i = 0; i < items.size(); ++i)
        {
            if (items.getReference(i).getFullPathName() == normalCreated)
            {
                row = i;
                break;
            }
        }
        if (row < 0)
        {
            juce::String createdFileName = juce::File(createdPath).getFileName();
            for (int i = 0; i < items.size(); ++i)
            {
                if (items.getReference(i).getFileName() == createdFileName)
                {
                    row = i;
                    break;
                }
            }
        }
        if (row >= 0)
        {
            selectedRowInColumn.set(column, row);
            repaint();
            startInlineRename(column, row);
        }
    });
}

void ColumnBrowserComponent::hideRenameEditor()
{
    if (editingColumn < 0) return;
    renameEditor.onFocusLost = nullptr;
    removeChildComponent(&renameEditor);
    editingColumn = -1;
    editingRow = -1;
    repaint();
}


void ColumnBrowserComponent::setRootFolder(const juce::File& root)
{
    if (rootFolder != root)
    {
        rootFolder = root;
        path.clear();
        refreshColumns();
    }
}

void ColumnBrowserComponent::setPath(const juce::Array<juce::File>& newPath)
{
    if (path != newPath)
    {
        path = newPath;
        refreshColumns();
    }
}

void ColumnBrowserComponent::refreshFromDisk()
{
    refreshColumns();
    repaint();
}

juce::File ColumnBrowserComponent::getSelectedFolder() const
{
    if (path.isEmpty())
        return rootFolder;
    return path.getLast();
}

juce::File ColumnBrowserComponent::getFileAt(int column, int row) const
{
    if (column < 0 || column >= columnItems.size())
        return {};
    const auto& items = columnItems.getReference(column);
    if (row < 0 || row >= items.size())
        return {};
    return items.getReference(row);
}

juce::File ColumnBrowserComponent::getParentForColumn(int column) const
{
    if (column == 0)
        return rootFolder;
    if (column >= 1 && column - 1 < path.size())
        return path.getReference(column - 1);
    return juce::File();
}

int ColumnBrowserComponent::getTotalContentWidth() const
{
    int total = 0;
    for (int i = 0; i < columnWidths.size(); ++i)
        total += columnWidths[i];
    if (columnItems.size() > 1)
        total += (int)(columnItems.size() - 1) * kDividerWidth;
    return juce::jmax(1, total);
}

juce::File ColumnBrowserComponent::getSelectedFileInLastColumn() const
{
    if (columnItems.isEmpty() || selectedRowInColumn.size() < columnItems.size())
        return {};
    int lastCol = columnItems.size() - 1;
    int row = selectedRowInColumn[lastCol];
    juce::File f = getFileAt(lastCol, row);
    return (f.existsAsFile() ? f : juce::File());
}

juce::Rectangle<int> ColumnBrowserComponent::getRowBoundsInColumn(int column, int row) const
{
    juce::Rectangle<int> bounds = getColumnBounds(column);
    if (bounds.isEmpty() || row < 0)
        return {};
    return juce::Rectangle<int>(bounds.getX(), row * kRowHeight, bounds.getWidth(), kRowHeight);
}

void ColumnBrowserComponent::setSelectedRowInColumn(int column, int row)
{
    if (column >= 0 && column < selectedRowInColumn.size())
    {
        selectedRowInColumn.set(column, row);
        repaint();
    }
}

void ColumnBrowserComponent::refreshColumns()
{
    expandedPreviewRow = -1;
    columnItems.clear();
    selectedRowInColumn.clear();
    selectedRowsPerColumn.clear();
    anchorRowInColumn.clear();
    if (!rootFolder.isDirectory())
    {
        repaint();
        return;
    }
    struct FileCmp { int compareElements(const juce::File& a, const juce::File& b) const { return a.getFileName().compareNatural(b.getFileName()); } };
    FileCmp cmp;

    // Column 0: root's children (folders only)
    juce::Array<juce::File> col0;
    for (const auto& f : rootFolder.findChildFiles(juce::File::findDirectories, false))
        col0.add(f);
    col0.sort(cmp);
    columnItems.add(col0);
    selectedRowInColumn.add(-1);
    selectedRowsPerColumn.add(juce::Array<int>());
    anchorRowInColumn.add(-1);

    // Columns 1 .. path.size(): one column per path level; last column also gets files
    const int numColumns = 1 + (int)path.size();
    for (int i = 0; i < (int)path.size(); ++i)
    {
        juce::File current = path.getReference(i);
        juce::Array<juce::File> items;
        if (current.isDirectory())
        {
            for (const auto& f : current.findChildFiles(juce::File::findDirectories, false))
                items.add(f);
            items.sort(cmp);
            bool isLastColumn = (i == (int)path.size() - 1);
            if (isLastColumn)
            {
                juce::Array<juce::File> files;
                current.findChildFiles(files, juce::File::findFiles, false, "*.wav;*.aif;*.aiff");
                files.sort(cmp);
                for (const auto& f : files)
                    items.add(f);
            }
        }
        columnItems.add(items);
        selectedRowInColumn.add(-1);
        selectedRowsPerColumn.add(juce::Array<int>());
        anchorRowInColumn.add(-1);
    }

    // Ensure at least 3 column slots (like Finder) when path is shallow or empty
    while (columnItems.size() < 3)
    {
        columnItems.add(juce::Array<juce::File>());
        selectedRowInColumn.add(-1);
        selectedRowsPerColumn.add(juce::Array<int>());
        anchorRowInColumn.add(-1);
    }

    // Column widths: grow array with default width for new columns
    const int defaultW = kCol1Width;
    while (columnWidths.size() < columnItems.size())
    {
        int idx = (int)columnWidths.size();
        columnWidths.add(idx < 2 ? (idx == 0 ? kCol1Width : kCol2Width) : defaultW);
    }
    if (columnWidths.size() > columnItems.size())
        columnWidths.resize(columnItems.size());

    // Restore selection in each column from path so previously selected folders stay highlighted
    for (int c = 0; c < path.size() && c < columnItems.size(); ++c)
    {
        const auto& items = columnItems.getReference(c);
        juce::File target = path.getReference(c);
        for (int r = 0; r < items.size(); ++r)
        {
            if (items.getReference(r).getFullPathName() == target.getFullPathName())
            {
                selectedRowInColumn.set(c, r);
                break;
            }
        }
    }
    // When navigating right (or after setPath), highlight first item in the rightmost column so it's visible
    const int lastCol = (int)path.size();
    if (lastCol >= 0 && lastCol < columnItems.size() && lastCol < selectedRowInColumn.size())
    {
        const auto& lastItems = columnItems.getReference(lastCol);
        if (!lastItems.isEmpty() && selectedRowInColumn[lastCol] < 0)
            selectedRowInColumn.set(lastCol, 0);
    }
    repaint();

    if (onColumnWidthsChanged)
        onColumnWidthsChanged();
}

int ColumnBrowserComponent::getColumnAtX(int x) const
{
    int cx = 0;
    for (int i = 0; i < columnWidths.size(); ++i)
    {
        if (x < cx + columnWidths[i])
            return i;
        cx += columnWidths[i] + kDividerWidth;
    }
    return -1;
}

int ColumnBrowserComponent::getDividerAtX(int x) const
{
    int cx = 0;
    const int halfGrab = kDividerGrabWidth / 2;
    for (int i = 0; i < columnWidths.size(); ++i)
    {
        cx += columnWidths[i];
        // Divider drawn at cx (1px); hit-test a wider strip so it's easier to grab
        if (x >= cx - halfGrab && x < cx + kDividerWidth + halfGrab)
            return i;
        cx += kDividerWidth;
    }
    return -1;
}

void ColumnBrowserComponent::layoutColumns()
{
    int n = (int)columnWidths.size();
    if (n == 0 || getWidth() <= 0)
        return;
    int total = 0;
    for (int i = 0; i < n; ++i)
        total += columnWidths[i];
    total += (n - 1) * kDividerWidth;
    int diff = getWidth() - total;
    // Only expand the last column to fill (never shrink it), so divider drags aren't undone
    if (diff > 0 && n > 0)
        columnWidths.set(n - 1, juce::jmax(kMinColumnWidth, columnWidths[n - 1] + diff));
}

void ColumnBrowserComponent::paintColumn(juce::Graphics& g, int columnIndex, juce::Rectangle<int> bounds)
{
    if (columnIndex < 0 || columnIndex >= columnItems.size())
        return;
    const auto& items = columnItems.getReference(columnIndex);
    int sel = (columnIndex < selectedRowInColumn.size()) ? selectedRowInColumn[columnIndex] : -1;
    const bool hasMultiSelect = (columnIndex < selectedRowsPerColumn.size() && !selectedRowsPerColumn.getReference(columnIndex).isEmpty());
    g.setColour(FinderTheme::creamBg);
    g.fillRect(bounds);
    const int padH = 12;
    const int padV = 9;
    // Folder SVG is 30x24; use full width of icon slot and height to match aspect (no warp)
    const int fullIconWidth = 22;
    const int iconHeight = (int)(fullIconWidth * 24.0f / 30.0f);
    const int textLeft = bounds.getX() + padH + fullIconWidth + 6;
    for (int row = 0; row < items.size(); ++row)
    {
        juce::Rectangle<int> rowRect(bounds.getX(), bounds.getY() + row * kRowHeight, bounds.getWidth(), kRowHeight);
        if (!rowRect.intersects(bounds))
            continue;
        juce::File item = items.getReference(row);
        bool isDir = item.isDirectory();
        bool isCategoryRow = (columnIndex == 0);
        bool selected = hasMultiSelect
            ? selectedRowsPerColumn.getReference(columnIndex).contains(row)
            : (row == sel);
        bool dropTarget = (columnIndex == dropHighlightCol && row == dropHighlightRow);
        if (selected)
        {
            g.setColour(FinderTheme::headerBar);
            g.fillRect(rowRect);
        }
        if (dropTarget)
        {
            g.setColour(FinderTheme::accent.withAlpha(0.35f));
            g.fillRect(rowRect);
            g.setColour(FinderTheme::accent);
            g.drawRect(rowRect.reduced(1), 2);
        }
        // Column that contains files is at path.size() (not columnItems.size()-1, which may be empty placeholder)
        const bool isLastCol = (columnIndex == (int)path.size());
        const bool isFile = !isDir && !isCategoryRow;
        const bool isPlayingThis = isFile && isLastCol && item.getFullPathName() == playingFilePath;

        if (isDir || isCategoryRow)
        {
            juce::Drawable* icon = (selected && folderIconWhite) ? folderIconWhite.get() : folderIcon.get();
            if (icon)
            {
                float iconX = (float)(rowRect.getX() + padH);
                float iconY = (float)(rowRect.getY() + (kRowHeight - iconHeight) / 2);
                icon->drawWithin(g, juce::Rectangle<float>(iconX, iconY, (float)fullIconWidth, (float)iconHeight),
                    juce::RectanglePlacement::stretchToFit, 1.0f);
            }
        }
        else if (isLastCol && isFile)
        {
            // Play/pause button – use SVG assets
            const float iconSize = 16.0f;
            float cx = rowRect.getX() + padH + fullIconWidth * 0.5f;
            float cy = rowRect.getY() + kRowHeight * 0.5f;
            auto iconArea = juce::Rectangle<float>(cx - iconSize * 0.5f, cy - iconSize * 0.5f, iconSize, iconSize);
            // When the row is selected, keep icons white for contrast.
            // When not selected: dark play icon by default, accent pause icon while playing.
            juce::Drawable* icon = nullptr;
            if (selected)
                icon = isPlayingThis ? pauseIcon.get() : playIcon.get();
            else
                icon = isPlayingThis ? pauseIconAccent.get() : playIconDark.get();
            if (icon)
            {
                icon->drawWithin(g, iconArea, juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, 1.0f);
            }
        }
        g.setColour(selected ? FinderTheme::textOnDark : FinderTheme::textCharcoal);
        g.setFont(FinderTheme::interFont(13.0f, selected));
        juce::Rectangle<int> textRect(textLeft, rowRect.getY() + padV, rowRect.getRight() - textLeft - padH, kRowHeight - 2 * padV);
        if (columnIndex != editingColumn || row != editingRow)
        {
            g.drawText(item.getFileName(), textRect, juce::Justification::centredLeft, true);
        }
    }
    // Highlight empty area when dropping onto column's parent folder (dropHighlightRow == -1)
    if (columnIndex == dropHighlightCol && dropHighlightRow == -1)
    {
        int emptyTop = bounds.getY() + (int)items.size() * kRowHeight;
        juce::Rectangle<int> emptyRect(bounds.getX(), emptyTop, bounds.getWidth(), bounds.getBottom() - emptyTop);
        if (emptyRect.getHeight() > 0)
        {
            g.setColour(FinderTheme::accent.withAlpha(0.25f));
            g.fillRect(emptyRect);
            g.setColour(FinderTheme::accent);
            g.drawRect(emptyRect.reduced(1), 2);
        }
    }
}

void ColumnBrowserComponent::paint(juce::Graphics& g)
{
    g.fillAll(FinderTheme::creamBg);
    int x = 0;
    for (int i = 0; i < columnItems.size(); ++i)
    {
        int w = i < columnWidths.size() ? columnWidths[i] : kMinColumnWidth;
        juce::Rectangle<int> colBounds(x, 0, w, getHeight());
        paintColumn(g, i, colBounds);
        x += w;
        if (i < columnItems.size() - 1)
        {
            g.setColour(FinderTheme::topBar);
            g.fillRect(x, 0, kDividerWidth, getHeight());
            x += kDividerWidth;
        }
    }
}

juce::Image ColumnBrowserComponent::createDragImageForFile(const juce::File& f) const
{
    const int w = 200;
    const int h = 44;
    juce::Image img(juce::Image::ARGB, w, h, true);
    juce::Graphics g(img);
    g.fillAll(juce::Colour(0x00000000));
    g.setColour(juce::Colour(0xe8ffffff));
    g.fillRoundedRectangle(1, 1, w - 2, h - 2, 6);
    g.setColour(FinderTheme::textCharcoal.withAlpha(0.4f));
    g.drawRoundedRectangle(1, 1, w - 2, h - 2, 6, 1);
    const int iconSize = 28;
    const int pad = 8;
    if (folderIcon)
    {
        folderIcon->drawWithin(g, juce::Rectangle<float>((float)pad, (float)((h - iconSize) / 2), (float)iconSize, (float)iconSize),
            juce::RectanglePlacement::stretchToFit, 1.0f);
    }
    juce::String name = f.getFileName();
    if (name.length() > 28)
        name = name.dropLastCharacters(name.length() - 25) + "...";
    g.setColour(FinderTheme::textCharcoal);
    g.setFont(FinderTheme::interFont(13.0f));
    g.drawText(name, pad + iconSize + 6, 0, w - (pad + iconSize + 6) - pad, h, juce::Justification::centredLeft, true);
    return img;
}

void ColumnBrowserComponent::resized()
{
    if (columnWidths.size() < columnItems.size())
    {
        while (columnWidths.size() < columnItems.size())
        {
            int idx = (int)columnWidths.size();
            columnWidths.add(idx < 2 ? (idx == 0 ? kCol1Width : kCol2Width) : kCol1Width);
        }
    }
    layoutColumns();
}

void ColumnBrowserComponent::mouseMove(const juce::MouseEvent& e)
{
    int div = getDividerAtX(e.getPosition().getX());
    if (div >= 0)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void ColumnBrowserComponent::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    didStartFileDrag = false;
    pendingClickCol = -1;
    pendingClickRow = -1;
    int div = getDividerAtX(e.getPosition().getX());
    if (div >= 0)
    {
        draggingDivider = div;
        lastDividerX = e.getPosition().getX();
        return;
    }
    int col = getColumnAtX(e.getPosition().getX());
    if (col < 0 || col >= columnItems.size())
        return;
    int row = e.getPosition().getY() / kRowHeight;
    const auto& items = columnItems.getReference(col);
    // Right-click only in empty area below folders → show "(+ new folder)" (left-click does nothing)
    if (row >= items.size())
    {
        if (!e.mods.isPopupMenu())
            return;
        if (getParentForColumn(col).isDirectory())
        {
            juce::PopupMenu m;
            m.addItem(1, "+ new folder");
            auto opts = juce::PopupMenu::Options()
                .withTargetComponent(this)
                .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards)
                .withMousePosition();
            m.showMenuAsync(opts, [this, col](int result) {
                if (result == 1)
                    showNewFolderDialog(col);
            });
        }
        return;
    }
    if (row < 0)
        return;

    juce::File f = items.getReference(row);
    const bool cmdOrCtrl = (e.mods.isCommandDown() || e.mods.isCtrlDown());

    if (!e.mods.isRightButtonDown())
    {
        while (selectedRowsPerColumn.size() < columnItems.size())
            selectedRowsPerColumn.add(juce::Array<int>());
        while (anchorRowInColumn.size() < columnItems.size())
            anchorRowInColumn.add(-1);
        juce::Array<int>& selRows = selectedRowsPerColumn.getReference(col);
        int& anchor = anchorRowInColumn.getReference(col);

        if (e.mods.isShiftDown())
        {
            // Anchor = last set anchor, or current selection (e.g. from keyboard), or clicked row
            int a = anchor >= 0 ? anchor : ((col < selectedRowInColumn.size() && selectedRowInColumn[col] >= 0) ? selectedRowInColumn[col] : row);
            int lo = juce::jmin(a, row);
            int hi = juce::jmax(a, row);
            selRows.clear();
            for (int r = lo; r <= hi; ++r)
                selRows.add(r);
            selectedRowInColumn.set(col, row);
            anchor = row;  // next shift+click uses this as one end (like queue / Finder)
            repaint();
            return;
        }
        if (cmdOrCtrl)
        {
            if (selRows.contains(row))
                selRows.removeAllInstancesOf(row);
            else
                selRows.add(row);
            selectedRowInColumn.set(col, selRows.isEmpty() ? -1 : row);
            anchor = row;
            repaint();
            return;
        }
        selRows.clear();
        selRows.add(row);
        anchor = row;
        repaint();
    }

    if (e.mods.isRightButtonDown())
    {
        while (selectedRowsPerColumn.size() < columnItems.size())
            selectedRowsPerColumn.add(juce::Array<int>());
        if (col < selectedRowsPerColumn.size() && selectedRowsPerColumn.getReference(col).contains(row))
            { /* keep multi-selection for Remove */ }
        else
        {
            selectedRowsPerColumn.getReference(col).clear();
            selectedRowsPerColumn.getReference(col).add(row);
        }
        selectedRowInColumn.set(col, row);
        repaint();
        juce::Array<juce::File> toDelete;
        if (col < selectedRowsPerColumn.size() && !selectedRowsPerColumn.getReference(col).isEmpty())
        {
            const auto& colItems = columnItems.getReference(col);
            for (int r : selectedRowsPerColumn.getReference(col))
                if (r >= 0 && r < colItems.size())
                    toDelete.add(colItems.getReference(r));
        }
        else
            toDelete.add(f);
        juce::PopupMenu m;
        m.addItem(1, "Rename");
        bool isMac = (juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX) != 0;
        juce::String revealLabel = isMac ? "Reveal in Finder" : "Reveal in File Explorer";
        m.addItem(2, revealLabel);
        m.addSeparator();
        m.addItem(3, toDelete.size() > 1 ? "Remove " + juce::String(toDelete.size()) + " items" : "Remove");
        auto opts = juce::PopupMenu::Options()
            .withParentComponent(getTopLevelComponent())
            .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards)
            .withMousePosition();
        m.showMenuAsync(opts, [this, toDelete, col, row](int result) {
                if (result == 1)
                    startInlineRename(col, row);
                else if (result == 2 && toDelete.size() == 1 && toDelete.getReference(0).exists())
                    toDelete.getReference(0).revealToUser();
                else if (result == 3)
                {
                    for (const auto& file : toDelete)
                    {
                        if (!file.exists() || file == rootFolder) continue;
                        file.moveToTrash();
                        if (path.contains(file))
                        {
                            juce::Array<juce::File> newPath;
                            for (const auto& p : path)
                            {
                                if (p == file) break;
                                newPath.add(p);
                            }
                            setPath(newPath);
                        }
                    }
                    refreshColumns();
                    if (onPathChanged)
                        onPathChanged();
                }
            });
        return;
    }

    selectedRowInColumn.set(col, row);
    pendingClickCol = col;
    pendingClickRow = row;
    mouseDownPosition = e.getPosition();
    repaint();
}

void ColumnBrowserComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    int col = getColumnAtX(e.getPosition().getX());
    if (col < 0 || col >= columnItems.size())
        return;
    int row = e.getPosition().getY() / kRowHeight;
    const auto& items = columnItems.getReference(col);
    if (row < 0 || row >= items.size())
        return;
    startInlineRename(col, row);
}

static const int kDragStartThresholdPx = 6;

void ColumnBrowserComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingDivider >= 0)
    {
        const int leftCol = draggingDivider;
        const int rightCol = draggingDivider + 1;
        if (rightCol >= columnWidths.size())
            return;
        int dx = e.getPosition().getX() - lastDividerX;
        lastDividerX = e.getPosition().getX();
        int newLeft = juce::jmax(kMinColumnWidth, columnWidths[leftCol] + dx);
        int newRight = juce::jmax(kMinColumnWidth, columnWidths[rightCol] - dx);
        columnWidths.set(leftCol, newLeft);
        columnWidths.set(rightCol, newRight);
        repaint();
        return;
    }
    // Start file/folder drag if we have a valid row and moved past threshold
    if (pendingClickCol >= 0 && pendingClickRow >= 0 && !didStartFileDrag)
    {
        if (pendingClickCol >= columnItems.size())
            return;
        const auto& items = columnItems.getReference(pendingClickCol);
        if (pendingClickRow >= items.size())
            return;
        juce::Point<int> delta = e.getPosition() - mouseDownPosition;
        if (delta.getDistanceFromOrigin() >= kDragStartThresholdPx)
        {
            juce::File f = items.getReference(pendingClickRow);
            juce::String pathStr = f.getFullPathName();
            juce::var desc(kInternalDragPrefix + pathStr);
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
            {
                juce::Image dragImage = createDragImageForFile(f);
                container->startDragging(desc, this, dragImage, true);
                didStartFileDrag = true;
            }
        }
    }
}

void ColumnBrowserComponent::mouseUp(const juce::MouseEvent&)
{
    if (draggingDivider >= 0 && onColumnWidthsChanged)
        onColumnWidthsChanged();
    draggingDivider = -1;
    // Deferred click: navigate into folder or trigger file selection if we didn't start a drag
    if (pendingClickCol >= 0 && pendingClickRow >= 0 && !didStartFileDrag)
    {
        int col = pendingClickCol;
        int row = pendingClickRow;
        pendingClickCol = -1;
        pendingClickRow = -1;
        if (col < columnItems.size())
        {
            const auto& items = columnItems.getReference(col);
            if (row < items.size())
            {
                juce::File f = items.getReference(row);
                if (f.isDirectory() && onFolderSelected)
                    onFolderSelected(col, row);
                else if (col == (int)path.size())
                {
                    juce::Rectangle<int> playBounds = getPlayButtonBounds(col, row);
                    if (onFilePreviewToggled && playBounds.contains(mouseDownPosition))
                    {
                        bool isPlayingThis = (f.getFullPathName() == playingFilePath);
                        onFilePreviewToggled(row, !isPlayingThis);
                    }
                    else if (onFileSelected)
                        onFileSelected(row);
                }
            }
        }
    }
    else
    {
        pendingClickCol = -1;
        pendingClickRow = -1;
    }
}

bool ColumnBrowserComponent::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details)
{
    juce::String desc = details.description.toString();
    return desc.startsWith(kInternalDragPrefix);
}

void ColumnBrowserComponent::itemDragEnter(const juce::DragAndDropTarget::SourceDetails& details)
{
    int col = getColumnAtX(details.localPosition.getX());
    int row = details.localPosition.getY() / kRowHeight;
    if (col >= 0 && col < columnItems.size() && row >= 0)
    {
        const auto& items = columnItems.getReference(col);
        juce::File parentFolder = getParentForColumn(col);
        if (row < items.size())
        {
            if (items.getReference(row).isDirectory())
            {
                dropHighlightCol = col;
                dropHighlightRow = row;
                repaint();
            }
            else
                dropHighlightCol = dropHighlightRow = -1;
        }
        else if (parentFolder.isDirectory())
        {
            // Drop in empty area below items = drop into this column's parent folder
            dropHighlightCol = col;
            dropHighlightRow = -1;  // sentinel: means "column parent"
            repaint();
        }
        else
            dropHighlightCol = dropHighlightRow = -1;
    }
    else
        dropHighlightCol = dropHighlightRow = -1;
}

void ColumnBrowserComponent::itemDragMove(const juce::DragAndDropTarget::SourceDetails& details)
{
    int col = getColumnAtX(details.localPosition.getX());
    int row = details.localPosition.getY() / kRowHeight;
    if (col >= 0 && col < columnItems.size() && row >= 0)
    {
        const auto& items = columnItems.getReference(col);
        juce::File parentFolder = getParentForColumn(col);
        if (row < items.size())
        {
            if (items.getReference(row).isDirectory())
            {
                if (col != dropHighlightCol || row != dropHighlightRow)
                {
                    dropHighlightCol = col;
                    dropHighlightRow = row;
                    repaint();
                }
            }
            else
            {
                if (dropHighlightCol >= 0 || dropHighlightRow >= 0)
                {
                    dropHighlightCol = dropHighlightRow = -1;
                    repaint();
                }
            }
        }
        else if (parentFolder.isDirectory())
        {
            if (col != dropHighlightCol || dropHighlightRow != -1)
            {
                dropHighlightCol = col;
                dropHighlightRow = -1;
                repaint();
            }
        }
        else
        {
            if (dropHighlightCol >= 0 || dropHighlightRow >= 0)
            {
                dropHighlightCol = dropHighlightRow = -1;
                repaint();
            }
        }
    }
    else
    {
        if (dropHighlightCol >= 0 || dropHighlightRow >= 0)
        {
            dropHighlightCol = dropHighlightRow = -1;
            repaint();
        }
    }
}

void ColumnBrowserComponent::itemDragExit(const juce::DragAndDropTarget::SourceDetails&)
{
    if (dropHighlightCol >= 0 || dropHighlightRow >= 0)
    {
        dropHighlightCol = dropHighlightRow = -1;
        repaint();
    }
}

void ColumnBrowserComponent::itemDropped(const juce::DragAndDropTarget::SourceDetails& details)
{
    dropHighlightCol = dropHighlightRow = -1;
    repaint();
    juce::String desc = details.description.toString();
    if (!desc.startsWith(kInternalDragPrefix))
        return;
    juce::String pathStr = desc.substring(kInternalDragPrefix.length()).trim();
    juce::File sourceFile(pathStr);
    if (!sourceFile.exists())
        return;
    int col = getColumnAtX(details.localPosition.getX());
    int row = details.localPosition.getY() / kRowHeight;
    if (col < 0 || col >= columnItems.size() || row < 0)
        return;
    const auto& items = columnItems.getReference(col);
    juce::File destFolder;
    if (row < items.size())
        destFolder = items.getReference(row);
    else
        destFolder = getParentForColumn(col);  // drop in empty area below items = move into column's parent folder
    if (!destFolder.isDirectory())
        return;
    if (sourceFile.getFullPathName() == destFolder.getFullPathName())
        return;
    if (sourceFile.getParentDirectory().getFullPathName() == destFolder.getFullPathName())
        return; // already in this folder
    // Don't move a folder into itself or into one of its descendants
    juce::String sourcePath = sourceFile.getFullPathName();
    juce::String destPath = destFolder.getFullPathName();
    if (sourceFile.isDirectory() && destPath.startsWith(sourcePath + juce::File::getSeparatorString()))
        return;
    juce::File destFile = destFolder.getChildFile(sourceFile.getFileName());
    if (destFile.exists() && destFile.getFullPathName() != sourceFile.getFullPathName())
        return; // would overwrite; could add suffix later
    if (!sourceFile.moveFileTo(destFile))
        return;
    // If we moved a folder that's in the current path, truncate path so columns stay valid
    juce::Array<juce::File> newPath;
    for (int i = 0; i < path.size(); ++i)
    {
        if (path.getReference(i).getFullPathName() == sourcePath)
            break;
        newPath.add(path.getReference(i));
    }
    if (newPath.size() < path.size())
        setPath(newPath);
    else
        refreshColumns();
    if (onPathChanged)
        onPathChanged();
}

bool ColumnBrowserComponent::keyPressed(const juce::KeyPress& key)
{
    if (columnItems.isEmpty())
        return false;
    int col = 0;
    for (int i = (int)selectedRowInColumn.size() - 1; i >= 0; --i)
    {
        if (selectedRowInColumn[i] >= 0)
        {
            col = i;
            break;
        }
    }
    const auto& items = columnItems.getReference(col);
    int sel = (col < selectedRowInColumn.size()) ? selectedRowInColumn[col] : -1;
    if (sel < 0 && !items.isEmpty())
        sel = 0;

    if (key == juce::KeyPress::upKey)
    {
        if (sel > 0)
        {
            selectedRowInColumn.set(col, sel - 1);
            repaint();
            return true;
        }
        return false;
    }
    if (key == juce::KeyPress::downKey)
    {
        if (sel < items.size() - 1)
        {
            selectedRowInColumn.set(col, sel + 1);
            repaint();
            return true;
        }
        return false;
    }
    if (key == juce::KeyPress::leftKey)
    {
        if (onKeyLeft)
        {
            onKeyLeft();
            return true;
        }
        return false;
    }
    if (key == juce::KeyPress::rightKey)
    {
        if (sel >= 0 && sel < items.size())
        {
            juce::File f = items.getReference(sel);
            if (f.isDirectory() && onFolderSelected)
            {
                onFolderSelected(col, sel);
                return true;
            }
        }
        return false;
    }
    if (key == juce::KeyPress::spaceKey)
    {
        const int fileCol = (int)path.size();
        if (col == fileCol && sel >= 0 && sel < items.size())
        {
            juce::File f = items.getReference(sel);
            if (f.existsAsFile() && onFilePreviewToggled)
            {
                if (expandedPreviewRow == sel)
                {
                    expandedPreviewRow = -1;
                    onFilePreviewToggled(sel, false);
                }
                else
                {
                    expandedPreviewRow = sel;
                    onFilePreviewToggled(sel, true);
                }
                repaint();
                return true;
            }
        }
        return false;
    }
    return false;
}
