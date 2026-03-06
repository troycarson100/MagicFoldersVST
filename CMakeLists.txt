cmake_minimum_required(VERSION 3.22)
project(MagicFolders VERSION 1.0.0)

add_subdirectory(JUCE)

juce_add_plugin(MagicFolders
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
    PRODUCT_NAME "Magic Folders"
)

juce_generate_juce_header(MagicFolders)

# Essentia static library (built separately: cd essentia && python3 waf configure --build-static --lightweight= --fft=KISS --std=c++14 && python3 waf)
set(ESSENTIA_DIR ${CMAKE_SOURCE_DIR}/essentia)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(Eigen3 QUIET eigen3)
endif()
target_include_directories(MagicFolders PRIVATE
    ${ESSENTIA_DIR}/src
    ${ESSENTIA_DIR}/src/algorithms
    ${ESSENTIA_DIR}/third-party/kiss_fft
    ${Eigen3_INCLUDE_DIRS}
)

# Optional embedded ONNX model (add assets/InstrumentClassifier.onnx after training)
set(MAGIC_FOLDERS_ASSET_SOURCES
    assets/Batch_Plus.svg
    assets/DarkGreyArrowRight.svg
    assets/FolderDarkGrey_Icon.svg
    assets/GearSettingsIcon.png
    assets/GearSettingsIcon.svg
    assets/MagicFolders_Logo_White.svg
    assets/Pause.svg
    assets/Play.svg
    assets/Plus_Icon.svg
    assets/WhiteArrowLeft.svg
    assets/WhiteArrowRight.svg
)
set(MODEL_ONNX "${CMAKE_SOURCE_DIR}/assets/InstrumentClassifier.onnx")
set(MODEL_ONNX_DATA "${CMAKE_SOURCE_DIR}/training/InstrumentClassifier.onnx.data")
if(EXISTS "${MODEL_ONNX}")
    list(APPEND MAGIC_FOLDERS_ASSET_SOURCES "${MODEL_ONNX}")
    target_compile_definitions(MagicFolders PRIVATE MAGICFOLDERS_HAS_EMBEDDED_MODEL=1)
    # When the external weight file from export is present, embed it too so we
    # can write both files to disk at runtime and let ONNX Runtime resolve the
    # external data reference.
    if(EXISTS "${MODEL_ONNX_DATA}")
        list(APPEND MAGIC_FOLDERS_ASSET_SOURCES "${MODEL_ONNX_DATA}")
        target_compile_definitions(MagicFolders PRIVATE MAGICFOLDERS_HAS_EMBEDDED_MODEL_DATA=1)
    endif()
endif()

# Optional YAMNet ONNX model + labels (download via training/yamnet_to_onnx.py)
set(YAMNET_ONNX "${CMAKE_SOURCE_DIR}/assets/Models/yamnet.onnx")
set(YAMNET_LABELS "${CMAKE_SOURCE_DIR}/assets/Models/yamnet_labels.txt")
if(EXISTS "${YAMNET_ONNX}")
    list(APPEND MAGIC_FOLDERS_ASSET_SOURCES "${YAMNET_ONNX}")
    if(EXISTS "${YAMNET_LABELS}")
        list(APPEND MAGIC_FOLDERS_ASSET_SOURCES "${YAMNET_LABELS}")
    endif()
    target_compile_definitions(MagicFolders PRIVATE MAGICFOLDERS_HAS_YAMNET_MODEL=1)
endif()

# Optional YAMNet transfer-learning MLP head (train via training/train_yamnet_transfer.py)
set(YAMNET_HEAD_ONNX "${CMAKE_SOURCE_DIR}/assets/Models/yamnet_head.onnx")
if(EXISTS "${YAMNET_HEAD_ONNX}")
    list(APPEND MAGIC_FOLDERS_ASSET_SOURCES "${YAMNET_HEAD_ONNX}")
    target_compile_definitions(MagicFolders PRIVATE MAGICFOLDERS_HAS_YAMNET_HEAD=1)
endif()

juce_add_binary_data(MagicFoldersAssets SOURCES ${MAGIC_FOLDERS_ASSET_SOURCES})

# Optional ONNX Runtime (download prebuilt from https://github.com/microsoft/onnxruntime/releases)
# e.g. onnxruntime-osx-x86_64-1.16.0 or onnxruntime-osx-arm64-1.16.0
# If the known third_party bundle exists, use it as the default so the
# ML-backed detector "just works" out of the box, while still allowing
# users to override ONNXRUNTIME_ROOT via CMake cache / command line.
set(ONNXRUNTIME_ROOT_DEFAULT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-osx-arm64-1.20.0")
if(NOT DEFINED ONNXRUNTIME_ROOT OR ONNXRUNTIME_ROOT STREQUAL "")
    if(EXISTS "${ONNXRUNTIME_ROOT_DEFAULT}")
        set(ONNXRUNTIME_ROOT "${ONNXRUNTIME_ROOT_DEFAULT}")
    else()
        set(ONNXRUNTIME_ROOT "")
    endif()
endif()
set(ONNXRUNTIME_ROOT "${ONNXRUNTIME_ROOT}" CACHE PATH "Path to ONNX Runtime prebuilt (include/ and lib/)")

if(ONNXRUNTIME_ROOT)
    target_include_directories(MagicFolders PRIVATE ${ONNXRUNTIME_ROOT}/include)
    if(APPLE)
        target_link_libraries(MagicFolders PRIVATE ${ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib)
    elseif(UNIX)
        target_link_libraries(MagicFolders PRIVATE ${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so)
    else()
        target_link_libraries(MagicFolders PRIVATE ${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib)
    endif()
    target_compile_definitions(MagicFolders PRIVATE USE_ONNXRUNTIME=1)
endif()

target_sources(MagicFolders PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp
    SettingsOverlayComponent.cpp
    SettingsComponents.cpp
    ColumnBrowserComponent.cpp
    Source/Detection/ModelRunner.cpp
    Source/Detection/MelSpectrogram.cpp
    Source/Detection/FilenameBias.cpp
    Source/Detection/ConfidenceGate.cpp
    Source/Detection/DetectionV2.cpp
    Source/Detection/HeuristicCategory.cpp
    Source/Detection/YamnetRunner.cpp
    Source/Detection/DetectionPipeline.cpp
    Source/Detection/DSP/Resampler16kMono.cpp
    Source/Detection/DSP/Windowing.cpp
    Source/Detection/DSP/LoopOneShotDetector.cpp
    Source/Detection/DSP/PercussiveHarmonicGate.cpp
    Source/Detection/Mapping/CategoryMapper.cpp
    Source/Detection/Gating/ConfidenceGate.cpp
    Source/Detection/Yamnet/YamnetModel.cpp
)

target_compile_definitions(MagicFolders PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(MagicFolders PRIVATE
    MagicFoldersAssets
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    ${ESSENTIA_DIR}/build/src/libessentia.a
)
