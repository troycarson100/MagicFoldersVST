cmake_minimum_required(VERSION 3.22)
project(SampleOrganizer VERSION 1.0.0)

add_subdirectory(JUCE)

juce_add_plugin(SampleOrganizer
    COMPANY_NAME "YourName"
    COPY_PLUGIN_AFTER_BUILD TRUE
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    PLUGIN_MANUFACTURER_CODE Smpl
    PLUGIN_CODE Sorg
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Sample Organizer"
)

juce_generate_juce_header(SampleOrganizer)

# Essentia static library (built separately: cd essentia && python3 waf configure --build-static --lightweight= --fft=KISS --std=c++14 && python3 waf)
set(ESSENTIA_DIR ${CMAKE_SOURCE_DIR}/essentia)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(Eigen3 QUIET eigen3)
endif()
target_include_directories(SampleOrganizer PRIVATE
    ${ESSENTIA_DIR}/src
    ${ESSENTIA_DIR}/src/algorithms
    ${ESSENTIA_DIR}/third-party/kiss_fft
    ${Eigen3_INCLUDE_DIRS}
)

juce_add_binary_data(SampleOrganizerAssets
    SOURCES
        assets/DarkGreyArrowRight.svg
        assets/FolderDarkGrey_Icon.svg
        assets/GearSettingsIcon.png
        assets/GearSettingsIcon.svg
        assets/MagicFolders_Logo_White.svg
        assets/Plus_Icon.svg
        assets/WhiteArrowLeft.svg
        assets/WhiteArrowRight.svg
)

target_sources(SampleOrganizer PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp
    SettingsOverlayComponent.cpp
    SettingsComponents.cpp
    ColumnBrowserComponent.cpp
)

target_compile_definitions(SampleOrganizer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(SampleOrganizer PRIVATE
    SampleOrganizerAssets
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    ${ESSENTIA_DIR}/build/src/libessentia.a
)
