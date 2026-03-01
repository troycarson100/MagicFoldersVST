#pragma once
#include <JuceHeader.h>
#include "FinderTheme.h"

/** LookAndFeel for Settings: dark dropdowns, menu, no OS styling. */
class SettingsLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SettingsLookAndFeel();
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box) override;
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    int getPopupMenuBorderSize() override;

private:
    juce::Colour accent;
    juce::Colour accentHover;
    juce::Colour textOnDark;
};

/** BPM stepper: dark pill, - / value / + with hover. */
class SettingsStepperComponent : public juce::Component
{
public:
    SettingsStepperComponent();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    int getValue() const { return value; }
    void setValue(int v);
    void setRange(int minVal, int maxVal);
    std::function<void(int)> onValueChange;
    std::function<void()> onDoubleClick;

private:
    int value = 120;
    int minVal = 60;
    int maxVal = 200;
    int hoverPart = 0; // -1 = minus, 0 = none, 1 = plus
    juce::Rectangle<int> minusRect;
    juce::Rectangle<int> plusRect;
};

/** Modern toggle switch: dark blue when on, soft gray when off. */
class SettingsToggleComponent : public juce::Component
{
public:
    SettingsToggleComponent();
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override;

    bool getToggleState() const { return on; }
    void setToggleState(bool shouldBeOn, juce::NotificationType = juce::dontSendNotification);
    void setButtonText(const juce::String& text) { label = text; }
    std::function<void()> onClick;

private:
    bool on = false;
    juce::String label;
};

/** Custom dropdown: dark blue box, arrow, popup menu with same styling. */
class SettingsDropdownComponent : public juce::Component
{
public:
    SettingsDropdownComponent();
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override;

    void setOptions(const juce::StringArray& options);
    int getSelectedIndex() const { return selectedIndex; }
    void setSelectedIndex(int index, juce::NotificationType = juce::dontSendNotification);
    juce::String getSelectedText() const;
    std::function<void(int)> onChange;

private:
    juce::StringArray options;
    int selectedIndex = 0;
    SettingsLookAndFeel menuLookAndFeel;
};
