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
    return { "Bass", "Drums", "Guitar", "Melodic", "Textures", "FX", "Loops", "Percussion", "Vocals", "Other" };
}

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
    renameEditor.setMultiLine(false);
    renameEditor.setBorder(juce::BorderSize<int>(1));
    renameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    renameEditor.setColour(juce::TextEditor::textColourId, juce::Colour(0xff1a1a1a));
    renameEditor.setColour(juce::TextEditor::highlightColourId, juce::Colour(0xffb0d4f0));
    renameEditor.setColour(juce::TextEditor::highlightedTextColourId, juce::Colour(0xff1a1a1a));
    renameEditor.setColour(juce::TextEditor::outlineColourId, FinderTheme::topBar);
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
    renameEditor.setBounds(getTextBoundsForCell(column, row));
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
    if (path.isEmpty())
        return {};
    if (column == 1)
        return path.getReference(0);
    return path.getLast();  // column 2
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

void ColumnBrowserComponent::refreshColumns()
{
    columnItems.clear();
    selectedRowInColumn.clear();
    if (!rootFolder.isDirectory())
    {
        repaint();
        return;
    }
    struct FileCmp { int compareElements(const juce::File& a, const juce::File& b) const { return a.getFileName().compareNatural(b.getFileName()); } };
    FileCmp cmp;
    juce::Array<juce::File> col0;
    for (const auto& f : rootFolder.findChildFiles(juce::File::findDirectories, false))
        col0.add(f);
    col0.sort(cmp);
    columnItems.add(col0);
    selectedRowInColumn.add(-1);

    juce::File current;
    if (path.isEmpty())
    {
        juce::Array<juce::File> empty;
        columnItems.add(empty);
        selectedRowInColumn.add(-1);
        columnItems.add(empty);
        selectedRowInColumn.add(-1);
    }
    else if (path.size() == 1)
    {
        current = path.getReference(0);
        juce::Array<juce::File> subdirs;
        if (current.isDirectory())
        {
            for (const auto& f : current.findChildFiles(juce::File::findDirectories, false))
                subdirs.add(f);
            subdirs.sort(cmp);
        }
        columnItems.add(subdirs);
        selectedRowInColumn.add(-1);
        columnItems.add(juce::Array<juce::File>());
        selectedRowInColumn.add(-1);
    }
    else
    {
        current = path.getReference(0);
        juce::Array<juce::File> subdirs;
        if (current.isDirectory())
        {
            for (const auto& f : current.findChildFiles(juce::File::findDirectories, false))
                subdirs.add(f);
            subdirs.sort(cmp);
        }
        columnItems.add(subdirs);
        selectedRowInColumn.add(-1);
        current = path.getReference(1);
        juce::Array<juce::File> lastItems;
        if (current.isDirectory())
        {
            for (const auto& f : current.findChildFiles(juce::File::findDirectories, false))
                lastItems.add(f);
            lastItems.sort(cmp);
            juce::Array<juce::File> files;
            current.findChildFiles(files, juce::File::findFiles, false, "*.wav;*.aif;*.aiff");
            files.sort(cmp);
            for (const auto& f : files)
                lastItems.add(f);
        }
        columnItems.add(lastItems);
        selectedRowInColumn.add(-1);
    }
    if (columnWidths.size() < columnItems.size())
    {
        columnWidths.clear();
        columnWidths.add(kCol1Width);
        columnWidths.add(kCol2Width);
        int rest = getWidth() - kCol1Width - kCol2Width - (columnItems.size() - 1) * kDividerWidth;
        columnWidths.add(juce::jmax(kMinColumnWidth, rest));
    }
    repaint();
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
    for (int i = 0; i < columnWidths.size(); ++i)
    {
        cx += columnWidths[i];
        if (x >= cx && x < cx + kDividerWidth)
            return i;
        cx += kDividerWidth;
    }
    return -1;
}

void ColumnBrowserComponent::layoutColumns()
{
    if (columnWidths.size() < 3)
        return;
    int total = columnWidths[0] + kDividerWidth + columnWidths[1] + kDividerWidth + columnWidths[2];
    if (getWidth() > 0 && total != getWidth())
    {
        int diff = getWidth() - total;
        columnWidths.set(2, juce::jmax(kMinColumnWidth, columnWidths[2] + diff));
    }
}

void ColumnBrowserComponent::paintColumn(juce::Graphics& g, int columnIndex, juce::Rectangle<int> bounds)
{
    if (columnIndex < 0 || columnIndex >= columnItems.size())
        return;
    const auto& items = columnItems.getReference(columnIndex);
    int sel = (columnIndex < selectedRowInColumn.size()) ? selectedRowInColumn[columnIndex] : -1;
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
        bool selected = (row == sel);
        if (selected)
        {
            g.setColour(FinderTheme::headerBar);
            g.fillRect(rowRect);
        }
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
        g.setColour(selected ? FinderTheme::textOnDark : FinderTheme::textCharcoal);
        g.setFont(FinderTheme::interFont(13.0f, selected));
        juce::Rectangle<int> textRect(textLeft, rowRect.getY() + padV, rowRect.getRight() - textLeft - padH, kRowHeight - 2 * padV);
        if (columnIndex != editingColumn || row != editingRow)
        {
            g.drawText(item.getFileName(), textRect, juce::Justification::centredLeft, true);
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

void ColumnBrowserComponent::resized()
{
    if (columnWidths.isEmpty() && columnItems.size() >= 3)
    {
        columnWidths.add(kCol1Width);
        columnWidths.add(kCol2Width);
        int rest = getWidth() - kCol1Width - kCol2Width - 2 * kDividerWidth;
        if (rest < 0) rest = getWidth() / 3;
        columnWidths.add(juce::jmax(kMinColumnWidth, rest));
    }
    layoutColumns();
}

void ColumnBrowserComponent::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
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
    if (e.mods.isRightButtonDown())
    {
        selectedRowInColumn.set(col, row);
        repaint();
        juce::PopupMenu m;
        m.addItem(1, "Rename");
        bool isMac = (juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX) != 0;
        juce::String revealLabel = isMac ? "Reveal in Finder" : "Reveal in File Explorer";
        m.addItem(2, revealLabel);
        m.addSeparator();
        m.addItem(3, "Delete");
        auto opts = juce::PopupMenu::Options()
            .withParentComponent(getTopLevelComponent())
            .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards)
            .withMousePosition();
        m.showMenuAsync(opts, [this, col, row, f](int result) {
                if (result == 1)
                    startInlineRename(col, row);
                else if (result == 2 && f.exists())
                    f.revealToUser();
                else if (result == 3 && f.exists())
                {
                    if (f == rootFolder)
                        return;
                    f.moveToTrash();
                    if (path.contains(f))
                    {
                        juce::Array<juce::File> newPath;
                        for (const auto& p : path)
                        {
                            if (p == f) break;
                            newPath.add(p);
                        }
                        setPath(newPath);
                    }
                    refreshColumns();
                    if (onPathChanged)
                        onPathChanged();
                }
            });
        return;
    }

    selectedRowInColumn.set(col, row);
    repaint();
    if (f.isDirectory())
    {
        if (onFolderSelected)
            onFolderSelected(col, row);
    }
    else
    {
        if (col == columnItems.size() - 1 && onFileSelected)
            onFileSelected(row);
    }
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

void ColumnBrowserComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingDivider < 0)
        return;
    int dx = e.getPosition().getX() - lastDividerX;
    lastDividerX = e.getPosition().getX();
    if (draggingDivider < columnWidths.size())
    {
        int newW = juce::jmax(kMinColumnWidth, columnWidths[draggingDivider] + dx);
        columnWidths.set(draggingDivider, newW);
    }
    if (draggingDivider > 0 && draggingDivider - 1 < columnWidths.size())
    {
        int newW = juce::jmax(kMinColumnWidth, columnWidths[draggingDivider - 1] - dx);
        columnWidths.set(draggingDivider - 1, newW);
    }
    repaint();
}

void ColumnBrowserComponent::mouseUp(const juce::MouseEvent&)
{
    draggingDivider = -1;
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
    return false;
}
