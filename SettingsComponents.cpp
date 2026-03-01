#include "SettingsComponents.h"

using namespace FinderTheme;

//==============================================================================
SettingsLookAndFeel::SettingsLookAndFeel()
{
    accent = settingsAccent;
    accentHover = settingsAccentHover;
    textOnDark = FinderTheme::textOnDark;
}

int SettingsLookAndFeel::getPopupMenuBorderSize()
{
    return 1;
}

void SettingsLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int buttonW, int, juce::ComboBox&)
{
    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
    g.setColour(accent);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(settingsDivider);
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    juce::Path arrow;
    float cx = (float)(width - buttonW / 2) - 4.0f;
    float cy = (float)height * 0.5f;
    arrow.addTriangle(cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
    g.setColour(textOnDark);
    g.fillPath(arrow);
}

void SettingsLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(accent);
    g.setColour(settingsDivider);
    g.drawRect(0, 0, width, height, 1);
}

void SettingsLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool, bool isHighlighted, bool, bool, const juce::String& text, const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour(settingsDivider);
        g.fillRect(area.withHeight(1));
        return;
    }
    if (isHighlighted)
        g.fillAll(accentHover);
    g.setColour(textOnDark);
    g.setFont(FinderTheme::interFont(13.0f));
    g.drawText(text, area.reduced(12, 2), juce::Justification::centredLeft, true);
}

//==============================================================================
SettingsStepperComponent::SettingsStepperComponent()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void SettingsStepperComponent::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(settingsAccent);
    g.fillRoundedRectangle(b, b.getHeight() * 0.5f);
    g.setColour(settingsDivider);
    g.drawRoundedRectangle(b.reduced(0.5f), b.getHeight() * 0.5f, 1.0f);
    float pad = 12.0f;
    float w = b.getWidth() - pad * 2;
    float third = w / 3.0f;
    if (hoverPart == -1)
    {
        g.setColour(settingsAccentHover);
        g.fillRoundedRectangle(pad, 2, third, b.getHeight() - 4, 6.0f);
    }
    else if (hoverPart == 1)
    {
        g.setColour(settingsAccentHover);
        g.fillRoundedRectangle(pad + third * 2, 2, third, b.getHeight() - 4, 6.0f);
    }
    g.setColour(textOnDark);
    g.setFont(FinderTheme::interFont(11.0f));
    g.drawText("-", minusRect.toFloat(), juce::Justification::centred);
    g.setFont(FinderTheme::interFont(18.0f, true));
    g.drawText(juce::String(value), getLocalBounds().toFloat(), juce::Justification::centred);
    g.setFont(FinderTheme::interFont(11.0f));
    g.drawText("+", plusRect.toFloat(), juce::Justification::centred);
}

void SettingsStepperComponent::resized()
{
    int w = getWidth();
    int h = getHeight();
    int third = w / 3;
    minusRect = juce::Rectangle<int>(0, 0, third, h);
    plusRect = juce::Rectangle<int>(w - third, 0, third, h);
}

void SettingsStepperComponent::mouseDown(const juce::MouseEvent& e)
{
    if (minusRect.contains(e.getPosition()))
    {
        setValue(juce::jmax(minVal, value - 1));
    }
    else if (plusRect.contains(e.getPosition()))
    {
        setValue(juce::jmin(maxVal, value + 1));
    }
}

void SettingsStepperComponent::mouseEnter(const juce::MouseEvent& e)
{
    if (minusRect.contains(e.getPosition())) hoverPart = -1;
    else if (plusRect.contains(e.getPosition())) hoverPart = 1;
    else hoverPart = 0;
    repaint();
}

void SettingsStepperComponent::mouseExit(const juce::MouseEvent&)
{
    hoverPart = 0;
    repaint();
}

void SettingsStepperComponent::setValue(int v)
{
    if (value != v)
    {
        value = juce::jlimit(minVal, maxVal, v);
        if (onValueChange) onValueChange(value);
        repaint();
    }
}

void SettingsStepperComponent::setRange(int minV, int maxV)
{
    minVal = minV;
    maxVal = maxV;
    value = juce::jlimit(minVal, maxVal, value);
}

//==============================================================================
SettingsToggleComponent::SettingsToggleComponent()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void SettingsToggleComponent::paint(juce::Graphics& g)
{
    int h = getHeight();
    int trackH = juce::jmin(24, h - 8);
    int trackW = 44;
    int y = (h - trackH) / 2;
    int thumbSize = trackH - 4;
    int margin = 2;
    float radius = (float)trackH * 0.5f;
    juce::Rectangle<int> track(getWidth() - trackW - 4, y, trackW, trackH);
    juce::Colour trackOff(0xffb0b0b0);
    g.setColour(on ? settingsAccent : trackOff);
    g.fillRoundedRectangle(track.toFloat(), radius);
    g.setColour(settingsDivider);
    g.drawRoundedRectangle(track.toFloat(), radius, 1.0f);
    float thumbX = on ? (float)(track.getRight() - margin - thumbSize) : (float)(track.getX() + margin);
    float thumbY = (float)(track.getCentreY() - thumbSize / 2);
    g.setColour(juce::Colours::white);
    g.fillEllipse(thumbX, thumbY, (float)thumbSize, (float)thumbSize);
    g.setColour(settingsDivider);
    g.drawEllipse(thumbX, thumbY, (float)thumbSize, (float)thumbSize, 0.5f);
    g.setColour(textCharcoal);
    g.setFont(FinderTheme::interFont(13.0f));
    g.drawText(label, 0, 0, getWidth() - trackW - 12, h, juce::Justification::centredLeft, true);
}

void SettingsToggleComponent::mouseDown(const juce::MouseEvent&)
{
    setToggleState(!on, juce::sendNotification);
    if (onClick) onClick();
}

void SettingsToggleComponent::setToggleState(bool shouldBeOn, juce::NotificationType)
{
    if (on != shouldBeOn)
    {
        on = shouldBeOn;
        repaint();
    }
}

//==============================================================================
SettingsDropdownComponent::SettingsDropdownComponent()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void SettingsDropdownComponent::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(settingsAccent);
    g.fillRoundedRectangle(b, 6.0f);
    g.setColour(settingsDivider);
    g.drawRoundedRectangle(b, 6.0f, 1.0f);
    juce::String text = getSelectedText();
    g.setColour(textOnDark);
    g.setFont(FinderTheme::interFont(13.0f));
    g.drawText(text, b.reduced(12, 0), juce::Justification::centredLeft, true);
    juce::Path arrow;
    float cx = b.getRight() - 18.0f;
    float cy = b.getCentreY();
    arrow.addTriangle(cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
    g.fillPath(arrow);
}

void SettingsDropdownComponent::mouseDown(const juce::MouseEvent&)
{
    if (options.isEmpty()) return;
    juce::PopupMenu m;
    for (int i = 0; i < options.size(); ++i)
        m.addItem(i + 1, options[i], true, i == selectedIndex);
    m.setLookAndFeel(&menuLookAndFeel);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(getWidth()),
        [this](int result) {
            if (result > 0 && result <= options.size())
            {
                int idx = result - 1;
                setSelectedIndex(idx, juce::dontSendNotification);
                if (onChange) onChange(idx);
            }
        });
}

void SettingsDropdownComponent::setOptions(const juce::StringArray& opts)
{
    options = opts;
    selectedIndex = juce::jlimit(0, options.size() - 1, selectedIndex);
}

void SettingsDropdownComponent::setSelectedIndex(int index, juce::NotificationType)
{
    if (selectedIndex != index && index >= 0 && index < options.size())
    {
        selectedIndex = index;
        repaint();
    }
}

juce::String SettingsDropdownComponent::getSelectedText() const
{
    if (selectedIndex >= 0 && selectedIndex < options.size())
        return options[selectedIndex];
    return {};
}
