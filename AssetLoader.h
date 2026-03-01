#pragma once
#include <JuceHeader.h>
#include "BinaryData.h"

/** Loads SVG assets from BinaryData for the Magic Folders UI. */
struct AssetLoader
{
    static std::unique_ptr<juce::Drawable> getLogo()
    {
        return juce::Drawable::createFromImageData(BinaryData::MagicFolders_Logo_White_svg, BinaryData::MagicFolders_Logo_White_svgSize);
    }
    static std::unique_ptr<juce::Drawable> getPlusIcon()
    {
        return juce::Drawable::createFromImageData(BinaryData::Plus_Icon_svg, BinaryData::Plus_Icon_svgSize);
    }
    static std::unique_ptr<juce::Drawable> getWhiteArrowLeft()
    {
        return juce::Drawable::createFromImageData(BinaryData::WhiteArrowLeft_svg, BinaryData::WhiteArrowLeft_svgSize);
    }
    static std::unique_ptr<juce::Drawable> getWhiteArrowRight()
    {
        return juce::Drawable::createFromImageData(BinaryData::WhiteArrowRight_svg, BinaryData::WhiteArrowRight_svgSize);
    }
    static std::unique_ptr<juce::Drawable> getDarkGreyArrowRight()
    {
        return juce::Drawable::createFromImageData(BinaryData::DarkGreyArrowRight_svg, BinaryData::DarkGreyArrowRight_svgSize);
    }
    static std::unique_ptr<juce::Drawable> getGearSettingsIcon()
    {
        return juce::Drawable::createFromImageData(BinaryData::GearSettingsIcon_svg, BinaryData::GearSettingsIcon_svgSize);
    }
};
