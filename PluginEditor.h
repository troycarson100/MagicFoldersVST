#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "FinderTheme.h"
#include "AssetLoader.h"
#include "ColumnBrowserComponent.h"
#include "SettingsOverlayComponent.h"

class SampleOrganizerEditor : public juce::AudioProcessorEditor,
                              public juce::FileDragAndDropTarget
{
public:
    SampleOrganizerEditor(SampleOrganizerProcessor&);
    ~SampleOrganizerEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    SampleOrganizerProcessor& processor;

    static constexpr int kLogoPanelWidth = 220;
    static constexpr int kSidebarWidth = 220;
    static constexpr int kHeaderHeight = 52;
    static constexpr int kDragAreaHeight = 72;
    static constexpr int kDragAreaPadding = 16;       // margin on all sides of drag area
    static constexpr int kDragAreaPaddingVertical = 16;  // same as kDragAreaPadding for even spacing
    static constexpr int kProcessButtonHeight = 52;
    static constexpr int kThickBorderHeight = 2;     // thick horizontal borders

    // Sidebar
    std::unique_ptr<juce::Drawable> logoDrawable;
    std::unique_ptr<juce::Drawable> plusDrawable;
    std::unique_ptr<juce::Drawable> arrowRightDrawable;
    juce::DrawableButton plusBtn { "Plus", juce::DrawableButton::ImageFitted };
    juce::ListBox packList;
    juce::TextEditor packRenameEditor;
    int editingPackRow = -1;
    juce::StringArray packNames;
    juce::Array<juce::File> packDirs;
    int selectedPackIndex = -1;
    class PackListModel : public juce::ListBoxModel
    {
    public:
        explicit PackListModel(SampleOrganizerEditor& e) : editor(e) {}
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    private:
        SampleOrganizerEditor& editor;
    };
    PackListModel packListModel;
    juce::TextButton sidebarPlaceholderBtn;
    int hoveredPackRow = -1;
    static constexpr int kPackRowHeight = 34;
    static constexpr int kPackPaddingH = 14;
    static constexpr int kPackPaddingV = 10;

    // Header
    std::unique_ptr<juce::Drawable> backArrowDrawable;
    std::unique_ptr<juce::Drawable> forwardArrowDrawable;
    std::unique_ptr<juce::Drawable> forwardArrowDimmedDrawable;
    std::unique_ptr<juce::Drawable> gearDrawable;
    juce::DrawableButton backBtn { "Back", juce::DrawableButton::ImageFitted };
    juce::DrawableButton forwardBtn { "Forward", juce::DrawableButton::ImageFitted };
    juce::Label breadcrumbLabel;
    juce::StringArray breadcrumbParts;
    juce::DrawableButton settingsBtn { "Settings", juce::DrawableButton::ImageFitted };
    juce::Array<juce::Array<juce::File>> pathHistory;
    juce::Array<juce::Array<juce::File>> pathForward;

    // Column browser
    ColumnBrowserComponent columnBrowser;
    juce::Label columnPlaceholderLabel;
    juce::Array<juce::File> columnPath;

    // Drag area & process
    juce::Component dragArea;
    juce::Label dragLabel;
    juce::Label queueLabel;
    juce::TextButton processBtn;
    bool isDragOver = false;
    bool isHoveringDragArea = false;

    // Settings overlay (initialized in cpp with processor ref)
    std::unique_ptr<SettingsOverlayComponent> settingsOverlay;

    // Playback
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    juce::AudioTransportSource transportSource;
    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::String playingFilePath;

    void refreshPackList();
    void updateBreadcrumb();
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent&) override;
    void setHoveredPackRow(int row);
    void tryRenamePack(int row);  // starts inline rename on pack list
    void startPackInlineRename(int row);
    void commitPackRename();
    void hidePackRenameEditor();
    void handleBreadcrumbClick(int x, int y);

    struct PackListHoverListener : juce::MouseListener
    {
        SampleOrganizerEditor* editor = nullptr;
        void mouseMove(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override;
    };
    std::unique_ptr<PackListHoverListener> packListHoverListener;
    void createNewPack();
    void updateForwardButtonState();
    void pushPathToHistory();
    void goBack();
    void goForward();
    void playSelectedFile();
    int getContentBottom() const;
    juce::Rectangle<int> getLogoPanelBounds() const;
    juce::Rectangle<int> getPackListBounds() const;
    juce::Rectangle<int> getHeaderBounds() const;
    juce::Rectangle<int> getColumnBrowserBounds() const;
    juce::Rectangle<int> getDragAreaBounds() const;
    juce::Rectangle<int> getProcessButtonBounds() const;
    static juce::StringArray expandDroppedPaths(const juce::StringArray& list);
    static bool isAudioPath(const juce::String& path);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleOrganizerEditor)
};
