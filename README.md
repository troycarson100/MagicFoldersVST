# MagicFoldersVST

Sample Organizer plugin (VST3, AU, Standalone) that analyzes audio with **Essentia**, then copies files into a folder tree by category, type, key, and BPM with smart naming.

## Requirements

- CMake 3.22+
- C++17 toolchain (Xcode on macOS)
- **JUCE** – clone into this directory as `JUCE/`
- **Essentia** – clone and build as static lib in `essentia/`
- **Eigen** – for Essentia (e.g. `brew install eigen` on macOS)

## Build

### 1. Clone dependencies

```bash
git clone https://github.com/juce-framework/JUCE.git
git clone https://github.com/MTG/essentia.git
```

### 2. Build Essentia (static, lightweight)

```bash
cd essentia
pip3 install pkg-config   # or brew install pkg-config
brew install eigen        # macOS
python3 waf configure --build-static --lightweight= --fft=KISS --std=c++14
python3 waf build -j4
cd ..
```

### 3. Build the plugin

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

Outputs: `SampleOrganizer_artefacts/Release/` (VST3, AU, Standalone app).

## Usage

1. Choose an output folder.
2. Add or drag WAV/AIFF files (or a folder).
3. Click **Process Samples**. The plugin analyzes each file (BPM, key, loop vs one-shot, instrument category), then copies them into `OutputFolder/Category/One-Shots|Loops/` with names like `Kick_01.wav`, `Loop_Guitar_Am_120bpm_02.wav`.

No Python or extra runtimes required for end users.
