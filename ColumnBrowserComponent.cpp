#include "ColumnBrowserComponent.h"

using namespace FinderTheme;

juce::StringArray ColumnBrowserComponent::getDefaultCategories()
{
    return { "Bass", "Drums", "Guitar", "Melodic", "Textures", "FX", "Loops", "Percussion", "Vocals", "Other" };
}

ColumnBrowserComponent::ColumnBrowserComponent()
{
    setWantsKeyboardFocus(false);
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
    juce::StringArray categories = getDefaultCategories();
    juce::Array<juce::File> col0;
    for (const auto& name : categories)
        col0.add(rootFolder.getChildFile(name));
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

static void drawFolderIcon(juce::Graphics& g, juce::Colour colour, float x, float y, float size)
{
    const float w = size * 1.15f;
    const float h = size;
    g.setColour(colour);
    g.fillRoundedRectangle(x + 2, y + 5, w - 2, h - 5, 1.5f);
    g.fillRoundedRectangle(x, y + 2, w * 0.5f, 5, 1.0f);
}

void ColumnBrowserComponent::paintColumn(juce::Graphics& g, int columnIndex, juce::Rectangle<int> bounds)
{
    if (columnIndex < 0 || columnIndex >= columnItems.size())
        return;
    const auto& items = columnItems.getReference(columnIndex);
    int sel = (columnIndex < selectedRowInColumn.size()) ? selectedRowInColumn[columnIndex] : -1;
    g.setColour(FinderTheme::creamBg);
    g.fillRect(bounds);
    const int iconSize = 16;
    const int padH = 12;
    const int padV = 9;
    const int textLeft = bounds.getX() + padH + iconSize + 6;
    for (int row = 0; row < items.size(); ++row)
    {
        juce::Rectangle<int> rowRect(bounds.getX(), bounds.getY() + row * kRowHeight, bounds.getWidth(), kRowHeight);
        if (!rowRect.intersects(bounds))
            continue;
        bool isDir = items.getReference(row).isDirectory();
        bool selected = (row == sel);
        if (selected)
        {
            if (isDir)
            {
                g.setColour(FinderTheme::selectedFolderBorder);
                g.drawRect(rowRect.reduced(1), 1);
            }
            else
            {
                g.setColour(FinderTheme::selectedFileBg);
                g.fillRect(rowRect);
            }
        }
        if (isDir)
        {
            float iconX = (float)(rowRect.getX() + padH);
            float iconY = (float)(rowRect.getY() + (kRowHeight - iconSize) / 2);
            drawFolderIcon(g, FinderTheme::textCharcoal, iconX, iconY, (float)iconSize);
        }
        g.setColour(selected && !isDir ? FinderTheme::textOnDark : FinderTheme::textCharcoal);
        g.setFont(selected && isDir ? juce::Font(11.0f, juce::Font::bold) : juce::Font(11.0f, juce::Font::plain));
        juce::Rectangle<int> textRect(textLeft, rowRect.getY() + padV, rowRect.getRight() - textLeft - padH, kRowHeight - 2 * padV);
        juce::String name = items.getReference(row).getFileName();
        g.drawText(name, textRect, juce::Justification::centredLeft, true);
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
            g.setColour(FinderTheme::columnDivider);
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
    if (row < 0 || row >= items.size())
        return;
    selectedRowInColumn.set(col, row);
    repaint();
    juce::File f = items.getReference(row);
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
