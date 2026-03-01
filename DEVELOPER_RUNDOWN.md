# Magic Folders / Sample Organizer — Developer Rundown

This document describes the plugin architecture, data flow, and key code so a developer can understand and extend it.

---

## 1. Project overview

- **Product name:** Sample Organizer (UI brand: "Magic Folders")
- **Role:** Organize and tag audio samples (WAV/AIF/AIFF) using Essentia analysis, then copy them into a category/type folder structure.
- **Formats:** VST3, AU, Standalone (JUCE).
- **Build:** CMake + JUCE; requires **Essentia** static library for analysis.

### Build (CMakeLists.txt)

```cmake
juce_add_plugin(SampleOrganizer
    ...
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Sample Organizer"
)

# Essentia: build separately, then link
# cd essentia && python3 waf configure --build-static --lightweight= --fft=KISS --std=c++14 && python3 waf
set(ESSENTIA_DIR ${CMAKE_SOURCE_DIR}/essentia)
target_link_libraries(SampleOrganizer PRIVATE
    SampleOrganizerAssets
    juce::juce_audio_utils
    juce::juce_gui_basics
    ...
    ${ESSENTIA_DIR}/build/src/libessentia.a
)

# UI assets (SVGs) embedded via BinaryData
juce_add_binary_data(SampleOrganizerAssets SOURCES assets/*.svg)
```

**Source files:**

| File | Role |
|------|------|
| `PluginProcessor.cpp/h` | AudioProcessor; no real-time DSP; holds queue, settings, Essentia analysis, file copy. |
| `PluginEditor.cpp/h` | Main UI: sidebar, header, column browser, drag zone, Process button. |
| `ColumnBrowserComponent.cpp/h` | Finder-style 3-column browser (categories → subfolders → files). |
| `SettingsOverlayComponent.cpp/h` | Settings panel (output path, BPM/key, genre, naming, overwrite, theme). |
| `FinderTheme.h` | Colour constants (cream, dark bar, text, dividers). |
| `AssetLoader.h` | Loads SVG drawables from `BinaryData` (logo, arrows, plus, gear). |

---

## 2. Architecture: Processor vs Editor

- **SampleOrganizerProcessor** is a `juce::AudioProcessor`. It does **not** process audio in `processBlock`; it exists to host the editor and to own:
  - **Queue:** `juce::Array<SampleInfo> queue` — files the user has dropped.
  - **Settings:** `outputDirectory`, `projectBPM`, `projectKey`, `useHostBpm`, `useProjectKey`, `namingFormat`, `customPrefix`, `overwriteDuplicates`, `themeLight`.
  - **Processing:** `processAll()` runs Essentia analysis on each queued file and `copyToFolder()` writes to disk.
- **SampleOrganizerEditor** holds a reference to the processor, builds the UI, and:
  - Adds files to `processor.queue` when the user drops them on the drag zone.
  - Sets `processor.currentProcessDirectory` from the column browser’s selected folder before calling `processAll()`.
  - Reads/writes processor settings in the settings overlay.

Data flow in one sentence: **Editor adds files to processor queue → user clicks Process Samples → editor sets `currentProcessDirectory` → processor runs `processAll()` (analyze + copy) → editor refreshes pack list and UI.**

---

## 3. Processor: data structures and API

### Core types (PluginProcessor.h)

```cpp
struct AnalysisResult
{
    juce::String category;   // e.g. "Kicks", "Bass", "Melodic"
    juce::String type;       // "One-Shot" or "Loop"
    juce::String suggestedName;
    juce::String key;        // e.g. "C Major"
    int bpm = 0;
    juce::String melodicVibe; // Pad, Pluck, Lead, Keys (when category == Melodic)
};

struct SampleInfo
{
    juce::File sourceFile;
    juce::String name, type, category, key, genre;
    int bpm = 120;
    bool success = false;
    juce::String outputPath, suggested_name;
};
```

### Key processor members

```cpp
juce::File outputDirectory;           // Magic Folders root (set in Settings)
juce::File currentProcessDirectory;  // Where to write this run (editor sets from column browser)
juce::Array<SampleInfo> queue;       // Files to process (added by editor on drop)
juce::Array<SampleInfo> processed;   // Last run’s results

juce::String projectKey = "C Major";
int projectBPM = 120;
bool useHostBpm = false;
bool useProjectKey = true;
int namingFormat = 0;        // 0/1/2 for naming schemes
juce::String customPrefix;
bool overwriteDuplicates = false;
```

### Adding files (PluginProcessor.cpp)

```cpp
void SampleOrganizerProcessor::addFiles(const juce::Array<juce::File>& files)
{
    for (auto& f : files)
    {
        juce::String ext = f.getFileExtension().toLowerCase().trimCharactersAtStart(".");
        bool isAudio = (ext == "wav" || ext == "aif" || ext == "aiff");
        if (f.existsAsFile() && isAudio)
        {
            SampleInfo info;
            info.sourceFile = f;
            info.name = f.getFileNameWithoutExtension();
            info.category = "Other";
            info.type = "One-Shot";
            info.key = projectKey;
            info.bpm = projectBPM;
            info.genre = defaultGenre;
            queue.add(info);
        }
    }
}
```

### Processing pipeline (processAll)

```cpp
void SampleOrganizerProcessor::processAll()
{
    juce::File targetDir = (currentProcessDirectory.isDirectory() ? currentProcessDirectory : outputDirectory);
    if (!targetDir.isDirectory()) return;

    double hostBpm = 0.0;
    if (useHostBpm && getPlayHead())
        if (auto pos = getPlayHead()->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm = *bpm;

    for (auto& info : queue)
    {
        auto analysis = analyzeAudio(info.sourceFile, hostBpm);
        info.category = analysis.category;
        info.type = analysis.type;
        info.name = analysis.suggestedName;
        info.suggested_name = analysis.suggestedName;
        info.bpm = analysis.bpm;
        info.key = useProjectKey ? projectKey : analysis.key;
        copyToFolder(info);
        processed.add(info);
    }
    queue.clear();
    if (onComplete) onComplete();
}
```

### Copy layout (copyToFolder)

Files are written under:

`targetDir / category / (Loops|One-Shots) / suggestedName.ext`

Example: `MyPack/Kicks/One-Shots/Kick_01.wav`. Duplicate names get `_1`, `_2`, etc. (unless overwrite is enabled in the future).

```cpp
juce::File folder = baseDir
    .getChildFile(info.category)
    .getChildFile(info.type == "Loop" ? "Loops" : "One-Shots");
folder.createDirectory();
// ... build newName from suggested_name or name_key_bpm, then copyFileTo(dest)
```

### Analysis (analyzeAudio) — Essentia

- **Input:** `juce::File` → loaded with JUCE `AudioFormatManager`; converted to mono float; resampled to 44.1 kHz if needed.
- **One-Shot vs Loop:** `OnsetRate` (onset times + duration). Long + many onsets → Loop.
- **BPM:** For loops, `RhythmExtractor2013`; or host BPM when `useHostBpm` and override &gt; 0.
- **Key:** One frame (8192 samples) → Spectrum → SpectralPeaks → HPCP → Key (Temperley).
- **Category:** MFCC, spectral centroid, ZCR, rolloff, loudness, attack vs body RMS. Rules map these to: Kicks, Hi-Hats, Snares, Bass, FX, Percussion, Guitar, Melodic, Loops, Other. Melodic gets a `melodicVibe` (Pad/Pluck/Lead/Keys).
- **Suggested name:** Built from category short name, vibe, key, BPM, and a per-category counter (e.g. `Kick_01`, `Loop_Guitar_Cm_120bpm_01`).

All of this lives in `PluginProcessor.cpp`; no GUI code there.

---

## 4. Editor: layout and sections

The editor is one window with fixed layout constants and a full-height dark left bar.

### Layout constants (PluginEditor.h)

```cpp
static constexpr int kLogoPanelWidth = 220;
static constexpr int kSidebarWidth = 220;
static constexpr int kHeaderHeight = 52;
static constexpr int kDragAreaHeight = 100;
static constexpr int kProcessButtonHeight = 52;
// Pack list row
static constexpr int kPackRowHeight = 34;
static constexpr int kPackPaddingH = 14, kPackPaddingV = 10;
```

### Bounds helpers (PluginEditor.cpp)

- **Left bar (full height):** `(0, 0, kSidebarWidth, getHeight())` — dark; contains logo at top + pack list below.
- **Logo area (top of left bar):** `(0, 0, kLogoPanelWidth, kHeaderHeight)`.
- **Pack list:** `(0, kHeaderHeight, kSidebarWidth, getHeight() - kHeaderHeight - kDragAreaHeight - kProcessButtonHeight)`.
- **Header (breadcrumb + gear):** `(kLogoPanelWidth + 1, 0, getWidth() - kLogoPanelWidth - 1, kHeaderHeight)`.
- **Column browser:** `(kSidebarWidth, kHeaderHeight, getWidth() - kSidebarWidth, same height as pack list)`.
- **Drag zone:** `(0, getHeight() - kDragAreaHeight - kProcessButtonHeight, getWidth(), kDragAreaHeight)`.
- **Process button:** `(0, getHeight() - kProcessButtonHeight, getWidth(), kProcessButtonHeight)`.

### Paint order (high level)

1. Fill background cream.
2. Fill full-height left bar dark (`sidebarDarkBar`).
3. Draw “MAGIC FOLDERS” in logo area.
4. Fill header strip (right of left bar) dark; draw breadcrumb if `breadcrumbParts.size() > 1`.
5. Vertical divider at `x = kSidebarWidth`.
6. Drag zone (cream, dashed or solid border).
7. Process button (dark).

Pack list and column browser draw themselves (list model and `ColumnBrowserComponent::paint`).

---

## 5. Left sidebar: pack list

- **Data:** `packNames` (display names) and `packDirs` (juce::File) filled from `processor.outputDirectory`’s child directories in `refreshPackList()`.
- **Control:** `juce::ListBox` with `PackListModel` (row count, paint, click).
- **Selection:** `selectedPackIndex`; on click, `columnBrowser.setRootFolder(packDirs[row])`, `columnPath` cleared, breadcrumb updated.
- **Styling:** Dark background; white text; selected row uses `sidebarRowSelected` and bold + white “&gt;” (arrow drawable). Hover uses `sidebarRowHover` via `PackListHoverListener`.
- **Placeholder:** When `!processor.outputDirectory.isDirectory()`, a single “Set destination folder in Settings (gear icon) →” button is shown instead of the list; it opens the settings overlay.

```cpp
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
```

---

## 6. Header: back/forward, breadcrumb, settings

- **Back/Forward:** `pathHistory` and `pathForward` store `juce::Array<juce::File>` stacks. Back: push current path to forward, pop from history, set path. Forward: symmetric.
- **Breadcrumb:** `breadcrumbParts` = `[packName, path[0].getFileName(), path[1].getFileName(), ...]`; if a file is selected in the last column, its name is appended. Drawn in `paint()` when `breadcrumbParts.size() > 1`; otherwise the label shows a single line (e.g. pack name or “Select a pack”).
- **Clickable breadcrumb:** `mouseDown` on the editor calls `handleBreadcrumbClick(x,y)`. Hit-test by measuring segment widths (bold first segment, plain rest); clicking segment `i` truncates `columnPath` to length `i` and calls `columnBrowser.setPath(columnPath)`.
- **Settings:** `settingsBtn` (gear) shows `SettingsOverlayComponent`; overlay’s `onClose` hides it and refreshes the pack list.

---

## 7. Column browser (3-column Finder style)

### Role

- **Column 1:** Fixed category folders under the selected pack root: Bass, Drums, Guitar, Melodic, Textures, FX, Loops, Percussion, Vocals, Other (see `getDefaultCategories()`).
- **Column 2:** Subfolders of the selected category (e.g. One-Shots, Loops) — from disk.
- **Column 3:** Contents of the selected subfolder (dirs + audio files).

Path is at most two folders: `path[0]` = category dir, `path[1]` = subfolder dir. So the hierarchy is **Pack → Category → Subfolder → Files**.

### Data (ColumnBrowserComponent)

```cpp
juce::File rootFolder;                          // Selected pack directory
juce::Array<juce::File> path;                   // [categoryDir, subfolderDir]
juce::Array<juce::Array<juce::File>> columnItems; // 3 columns of items
juce::Array<int> selectedRowInColumn;
juce::Array<int> columnWidths;  // kCol1Width, kCol2Width, rest
```

### refreshColumns() (simplified)

```cpp
// Column 0: fixed categories
juce::StringArray categories = getDefaultCategories(); // Bass, Drums, ...
for (const auto& name : categories)
    col0.add(rootFolder.getChildFile(name));
columnItems.add(col0);

if (path.isEmpty())
    columnItems.add(empty), columnItems.add(empty);
else if (path.size() == 1)
    columnItems.add(subdirs of path[0]), columnItems.add(empty);
else
    columnItems.add(subdirs of path[0]), columnItems.add(contents of path[1] /* dirs + files */);
```

### Interaction

- **mouseDown:** Divider hit → start resize. Otherwise: column/row from coordinates; set `selectedRowInColumn[col] = row`; if folder → `onFolderSelected(col, row)`; if file in last column → `onFileSelected(row)`.
- **Editor callbacks:** `onFolderSelected` pushes path to history, truncates `columnPath` to the clicked column, appends the selected folder, calls `columnBrowser.setPath(columnPath)`, clears forward stack, updates breadcrumb. `onFileSelected` triggers `playSelectedFile()` (JUCE `AudioFormatManager` + `AudioTransportSource` for preview).
- **Resize:** `mouseDrag`/`mouseUp` adjust `columnWidths` for the dragged divider; `layoutColumns()` keeps col1/col2 fixed and resizes the last column to fill width.

### Sizing

- Row height 34 px; divider 1 px; column widths 200, 200, remainder. Cream background; selected folder = border only; selected file = dark fill + white text (see `FinderTheme`).

---

## 8. Drag zone and Process Samples

- **Drag:** Editor implements `FileDragAndDropTarget`. `isInterestedInFileDrag` / `filesDropped` use `expandDroppedPaths()` and `isAudioPath()`; then `processor.addFiles(fileArray)`. Drag-over state toggles `isDragOver` and redraws the zone (solid border, slightly darker).
- **Process button:** On click, sets `processor.currentProcessDirectory = columnBrowser.getSelectedFolder()` (or `outputDirectory`), then `juce::Timer::callAfterDelay(50, [this]{ processor.processAll(); ... })`. Success/error message is shown in the breadcrumb label.

---

## 9. Settings overlay

- **SettingsOverlayComponent** gets `SampleOrganizerProcessor&`. It shows:
  - Output folder (path label + Browse button)
  - Auto-detect BPM toggle (maps to `!processor.useHostBpm`)
  - Manual BPM 60–200 (+/-)
  - Auto-detect Key toggle (maps to `!processor.useProjectKey`)
  - Manual Key combo (24 keys)
  - Genre tag (text editor → `processor.defaultGenre`)
  - Naming format combo (0/1/2) + optional Custom prefix
  - Overwrite duplicates toggle
  - Theme Light/Dark toggle
- **syncFromProcessor()** copies processor state into controls when the overlay is opened. Controls write back to the processor on change.
- **Back arrow** invokes `onClose` (editor hides overlay and refreshes pack list). No audio or file logic here; UI only.

---

## 10. Theme and assets

### FinderTheme.h

Central place for colours:

- **Cream / content:** `creamBg` `#F0EDE8`, `textCharcoal` `#1a1a1a`
- **Dark bar / header / selected file:** `headerBar` / `sidebarDarkBar` `#1e1e1e`, `textOnDark` white
- **Sidebar list:** `sidebarRowSelected` / `sidebarRowHover` `#2a2a2a`
- **Column browser:** `columnDivider` `#C8C4BE`, `selectedFolderBorder`, `selectedFileBg` = headerBar

Use these names in UI code instead of hardcoding hex.

### AssetLoader.h

Loads SVGs from `BinaryData` (generated by `juce_add_binary_data`):

- `getLogo()`, `getPlusIcon()`, `getWhiteArrowLeft()`, `getWhiteArrowRight()`, `getDarkGreyArrowRight()`, `getGearSettingsIcon()`.

Each returns `std::unique_ptr<juce::Drawable>`. Used by the editor and settings (e.g. back arrow).

---

## 11. Entry point and plugin creation

```cpp
// PluginProcessor.cpp
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SampleOrganizerProcessor();
}
```

Editor is created by the processor:

```cpp
juce::AudioProcessorEditor* SampleOrganizerProcessor::createEditor()
{
    return new SampleOrganizerEditor(*this);
}
```

---

## 12. Where to change what

| Goal | Where |
|------|--------|
| Add/change analysis (BPM, key, category) | `PluginProcessor::analyzeAudio()` |
| Change output folder structure or naming | `PluginProcessor::copyToFolder()`; consider `namingFormat` / `customPrefix` |
| Add a processor setting | `PluginProcessor.h` (member) and `SettingsOverlayComponent` (control + sync) |
| Change layout (sizes, positions) | `PluginEditor` constants and `get*Bounds()`; `ColumnBrowserComponent` constants and `layoutColumns()` |
| Change colours | `FinderTheme.h` |
| Add/change column browser categories | `ColumnBrowserComponent::getDefaultCategories()` |
| Change pack list behaviour | `PluginEditor::refreshPackList()`, `PackListModel` |
| Add new UI assets | Add SVG under `assets/`, add to `CMakeLists.txt` `juce_add_binary_data`, then use in `AssetLoader` or equivalent |

This should be enough for a developer to navigate the plugin and implement new features or fixes without changing behaviour by accident.
