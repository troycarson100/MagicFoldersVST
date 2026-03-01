#pragma once
#include <JuceHeader.h>

namespace FinderTheme
{
    // Cream / light redesign (Figma)
    const juce::Colour creamBg           { 0xffF0EDE8 };  // main content, drop zone
    const juce::Colour textCharcoal      { 0xff1a1a1a };  // all text
    const juce::Colour headerBar         { 0xff1e1e1e };  // top header, logo panel, selected file in column 3
    const juce::Colour processBtnBg      { 0xff1a1a1a };  // Process Samples button
    const juce::Colour processBtnHover   { 0xff2a2a2a };
    const juce::Colour sidebarDarker     { 0xffE8E4DE };  // (unused when sidebar is dark bar)
    const juce::Colour sidebarDarkBar    { 0xff1e1e1e };  // full-height left bar (same as header)
    const juce::Colour sidebarRowHover   { 0xff2a2a2a };  // pack list row hover on dark bar
    const juce::Colour sidebarRowSelected { 0xff2a2a2a }; // pack list selected row on dark bar
    const juce::Colour sidebarHover      { 0xffe0ddd6 };  // (light theme hover)
    const juce::Colour columnDivider     { 0xffC8C4BE };  // 1px solid column dividers
    const juce::Colour dividerLine       { columnDivider };
    const juce::Colour selectedFolderBorder { 0xffd0d0d0 }; // bordered highlight for selected folder row
    const juce::Colour textOnDark        { 0xffFFFFFF };  // white text on dark (header, selected file, button)

    // Legacy aliases for existing code
    const juce::Colour sidebarBg        { headerBar };
    const juce::Colour textWhite        { textOnDark };
    const juce::Colour headerBg         { headerBar };
    const juce::Colour contentBg         { creamBg };
    const juce::Colour selectedFileBg    { headerBar };
    const juce::Colour selectedBg       { sidebarDarker };
    const juce::Colour bg               { creamBg };
    const juce::Colour panel             { headerBar };
    const juce::Colour border            { 0xff1e2229 };
    const juce::Colour accent            { 0xff4a9eff };
    const juce::Colour textPrimary       { textCharcoal };
    const juce::Colour textSub           { textCharcoal.withAlpha(0.8f) };
    const juce::Colour textDim           { textCharcoal.withAlpha(0.6f) };
    const juce::Colour hover             { juce::Colour(0x08000000) };
    const juce::Colour danger            { 0xffff5f56 };
}
