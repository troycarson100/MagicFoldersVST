## Magic Folders – Analysis, Detection & Naming

This document explains **how Magic Folders currently analyzes audio, decides categories / type / BPM / key, and names and places files on disk**. It is meant as a working reference for developers who need to adjust behaviour or debug mis‑classifications.

- Core logic lives in `PluginProcessor.cpp` / `PluginProcessor.h` (`MagicFoldersProcessor`).
- UI lives in `PluginEditor.cpp` / `PluginEditor.h` and does not do any analysis.

---

### 1. High‑level pipeline

1. **Editor queues audio files**
   - Drag & drop or “Batch +” yields a `juce::Array<juce::File>`.
   - `MagicFoldersProcessor::addFiles` / `addFilesFromFolder*` filter to `.wav`, `.aif`, `.aiff` and create a `SampleInfo` for each:
     - `name = file.getFileNameWithoutExtension()`
     - Defaults: `category = "Other"`, `type = "One-Shot"`, `key = projectKey`, `bpm = projectBPM`, `genre = defaultGenre`.
   - These go into `processor.queue`.

2. **User clicks “Process Samples”**
   - Editor sets `processor.currentProcessDirectory` from the column browser’s selected folder (or leaves it empty to use `outputDirectory` from Settings).
   - Calls `processor.processAll()`.

3. **`processAll()` workflow**
   - Pick **target directory**:
     - `targetDir = currentProcessDirectory` if it’s a directory, else `outputDirectory`.
     - If no valid directory, early‑return (no processing).
   - Reset per‑run state:
     - `categoryCounters.clear();`
     - `lastRunDuplicatesSkipped = 0;`
     - `lastRunBlankSkipped = 0;`
   - Optionally read **host BPM** (if `useHostBpm == true`):
     - `hostBpm` pulled from `AudioPlayHead` (if available) and passed into `analyzeAudio()` as `hostBpmOverride`.
   - Seed **duplicate detection**:
     - Scan all existing audio files under `targetDir`.
     - For each, compute a `WaveformFingerprint` (RMS envelope, duration, quick hash).
     - Store fingerprints in `std::vector<WaveformFingerprint> seenFingerprints`.
   - For each `SampleInfo info : queue`:
     1. Compute fingerprint for `info.sourceFile`.  
        If `isDuplicate(fp, seenFingerprints)` → increment `lastRunDuplicatesSkipped` and `continue` (file is NOT copied).
     2. Call `AnalysisResult analysis = analyzeAudio(info.sourceFile, hostBpm);`.
        - If `analysis.isBlank` (silent/near‑silent audio) → `++lastRunBlankSkipped; continue;`.
     3. Copy analysis into `info`:
        - `info.category = analysis.category;`
        - `info.type = analysis.type;`
        - `info.name = analysis.suggestedName;`
        - `info.suggested_name = analysis.suggestedName;`
        - `info.bpm = analysis.bpm;`
        - `info.key = useProjectKey ? projectKey : analysis.key;`
     4. Call `copyToFolder(info)` to build folder path + final filename and physically copy the file.
        - On success, push `info` into `processed`.
     5. Push the new fingerprint into `seenFingerprints` so later items in the batch can’t duplicate it.
   - Clear `queue` and call `onComplete` callback for the editor.

---

### 2. Duplicate and blank detection

#### 2.1 Waveform fingerprints & duplicate check

Implementation: anonymous namespace at the top of `PluginProcessor.cpp`.

**Fingerprint (`computeFingerprint(const juce::File&)`)**

- Reads the file with `AudioFormatReader` (basic JUCE formats).
- Up to 10M samples, mixed down to a **mono buffer**.
- Computes:
  - `durationSeconds` = `lengthInSamples / sampleRate`.
  - `bins[64]` = normalised RMS envelope:
    - Audio is divided into 64 equal segments; RMS per segment is computed and then normalised so the peak bin is 1.0.
  - `quickHash` = FNV‑1a 64‑bit hash of 8‑bit quantised PCM (simple, but stable enough for exact matches).

**Duplicate decision (`isDuplicate(const WaveformFingerprint&, const std::vector<WaveformFingerprint>&)`)**

For each previous fingerprint `s`:

1. If `quickHash != 0 && fp.quickHash == s.quickHash` → **immediate duplicate**.
2. Else, compare durations:
   - If relative duration difference > 3% (`kDupDurationTolerance = 0.03`) → treat as different and continue.
3. Else, compute cosine similarity between the 64‑bin envelopes:
   - If similarity ≥ `kDupSimilarityThreshold = 0.992` → **duplicate**.

This prevents copying the same or near‑identical sample multiple times across runs or within a single batch.

#### 2.2 Blank / silent sample detection

At the top of `analyzeAudio()`:

- After mixing to mono, scan for the absolute peak amplitude.
- Convert to dB:
  - `peakDb = 20 * log10(peak)` or `-100 dB` if extremely small.
- If `peakDb < -60 dB`:
  - Mark `result.isBlank = true;`
  - Set fallback values:
    - `result.category = "Other";`
    - `result.type = "One-Shot";`
    - `result.suggestedName = "Sample_01";`
  - Return early; `processAll()` counts this in `lastRunBlankSkipped` and **does not copy** the file.

This is designed to skip Ableton “empty clip” exports and similar edge cases.

---

### 3. Audio analysis (`analyzeAudio`)

`AnalysisResult analyzeAudio(const juce::File& file, double hostBpmOverride = 0.0);`

**Inputs & preprocessing**

1. Load file via JUCE `AudioFormatManager`.
2. Mix all channels to mono (`juce::AudioBuffer<float> mono`).
3. If sample rate != 44.1 kHz:
   - Resample to 44.1 kHz using a simple linear interpolator.
   - Store as `std::vector<Real> audioBuffer`, with `workRate = 44100` and `workSamples = audioBuffer.size()`.
4. Compute `duration = workSamples / workRate`.

#### 3.1 One‑Shot vs Loop classification

- Essentia `OnsetRate` on the full `audioBuffer`:
  - Outputs `onsetTimes` (vector of onset timestamps) and `onsetRate`.
- Rule:
  - `type = "Loop"` if:
    - `duration > 2.0` seconds **and**
    - `onsetTimes.size() > 3`.
  - Otherwise `type = "One-Shot"`.

#### 3.2 BPM detection (loops only)

Only applied when `result.type == "Loop"`:

- If `hostBpmOverride > 0` (from `processAll` when `useHostBpm` is enabled and host reports BPM):
  - `result.bpm = clamp(round(hostBpmOverride), 60–200)`.
- Else:
  - Essentia `RhythmExtractor2013("method"="multifeature")`:
    - Inputs: `audioBuffer`.
    - Outputs: `bpm`, `confidence`, ticks, estimates, etc.
    - Final BPM: `result.bpm = clamp(round(bpm), 60–200)`.

#### 3.3 Key detection

Single‑frame key estimate:

1. Take the first `min(8192, workSamples)` samples of `audioBuffer` as `frameFrame`.
2. `Spectrum` → `spectrum`.
3. `SpectralPeaks` → `frequencies`, `magnitudes`.
4. `HPCP` → `hpcp`.
5. `Key("profileType"="temperley")` → `keyStr` (e.g. `"C"`), `scaleStr` (`"major"`/`"minor"`), `keyStrength`, `firstToSecondRelativeStrength`.

Decision:

- If `keyStrength > 0.35`:
  - `result.key = keyStr + (scaleStr == "minor" ? "m" : "")`.  
    Example: `"C"` + `"minor"` → `"Cm"`.

Additional tonal flag:

- `isTonal` is true when:
  - `(keyStrength > 0.3 && firstToSecondRelativeStrength > 0.1)` **or**
  - `result.key` is non‑empty.

This flag is used later in category heuristics (e.g. to distinguish melodic/guitar from FX/textures).

#### 3.4 Spectral / timbral features

From `audioBuffer` and `spectrum`:

- `MFCC` → `mfccCoeffs` (MFCCs) and `mfccBands`.
- `SpectralCentroidTime` (`sampleRate = workRate`) → `spectralCentroid`.
- `ZeroCrossingRate` → `zeroCrossingRate`.
- `RollOff` (`RollOff` algorithm on `spectrum`) → `spectralRolloff`.
- `Loudness` (`Loudness` algorithm on `audioBuffer`) → `loudness`.

Transient vs sustain energy:

- Attack window length = `0.05 s` (`attackSamples = workRate * 0.05`), clamped to buffer length.
- `attackRMS` = RMS over first `attackSamples`.
- `bodyRMS` = RMS over the rest.
- Sharp‑attack flag:
  - `hasSharpAttack = (attackRMS > bodyRMS * 2.5f)`.

Convenience vars:

- `mfcc1 = MFCC[1]` (low‑frequency tone / brightness hint).
- `mfcc2 = MFCC[2]` (used as a brightness / melodic hint).
- `centroidF = (float)spectralCentroid;`
- `zcrF = (float)zeroCrossingRate;`
- `rolloffF = (float)spectralRolloff;`

---

### 4. Category decision logic

Category is chosen by a **priority chain of heuristics**. First matching rule wins.

Variables used:

- `duration` — in seconds.
- `hasSharpAttack`, `centroidF`, `zcrF`, `rolloffF`, `mfcc1`, `mfcc2`, `isTonal`, `onsetTimes.size()`.
- `result.type` — `"One-Shot"` or `"Loop"`.

#### 4.1 Drums & FX (highest priority)

1. **Kicks**
   - `hasSharpAttack`
   - `centroidF < 800 Hz`
   - `zcrF < 0.1`
   - → `result.category = "Kicks"`.

2. **Hi‑Hats**
   - `zcrF > 0.15`
   - `rolloffF > 4000 Hz`
   - `duration < 1.0 s`
   - → `result.category = "Hi-Hats"`.

3. **Snares**
   - `hasSharpAttack`
   - `800 Hz < centroidF < 3000 Hz`
   - → `result.category = "Snares"`.

4. **Bass**
   - `centroidF < 600 Hz`
   - `!hasSharpAttack`
   - `mfcc1 < -10.0`
   - → `result.category = "Bass"`.

5. **FX**
   - `zcrF > 0.1`
   - `rolloffF > 6000 Hz`
   - → `result.category = "FX"`.

6. **Percussion (misc drummy one‑shots)**
   - `hasSharpAttack`
   - `duration < 0.5 s`
   - → `result.category = "Percussion"`.

#### 4.2 Guitar / Melodic / Loops

If none of the above matched:

1. **Guitar‑like loop (broad)**  
   Only if `result.type == "Loop"`:

   - `isGuitarLikeLoop(isTonal, hasSharpAttack, duration, centroidF, zcrF, onsetCount)`:
     - Duration 0.75–16s, onset count 2–48.
     - Rhythm guitar branch: sharp attack, centroid in a mid range, moderate ZCR.
     - Picked guitar branch: softer attack but more onsets, mid‑range centroid, low ZCR.
   - If returns true → `result.category = "Guitar"`.

2. **Guitar (tonal, mid‑range)**

   - `isTonal`
   - `400 Hz ≤ centroidF ≤ 4500 Hz`
   - And:
     - Either `!hasSharpAttack`, **or**
     - (`result.type == "Loop"` and onset count between 2 and 24).
   - → `result.category = "Guitar"`.

3. **Melodic (tonal, less percussive, brighter MFCC)**

   - `!hasSharpAttack`
   - `isTonal`
   - `mfcc2 > 0.0`
   - → `result.category = "Melodic"`.

4. **Guitar loop fallback**

   - `result.type == "Loop"`
   - `isTonal`
   - `400 Hz ≤ centroidF ≤ 4500 Hz`
   - `zcrF < 0.12`
   - onset count 2–30
   - → `result.category = "Guitar"`.

5. **Loop‑specific buckets (when `result.type == "Loop"`)**

   - **Songstarter**
     - `isTonal`
     - `duration ≥ 4.0 s`
     - onset count 4–32
     - `result.bpm > 0`
     - → `result.category = "Songstarter"`.

   - **Textures**
     - `!isTonal`
     - `isNoisyTextureCandidate(...)` true, where:
       - `veryFewOnsets` (≤ 4),
       - `quiteLong` (≥ 3 s),
       - `darkAndSoft` (low centroid, low ZCR, no sharp attack), **or**
       - `noisySwish` (high ZCR and high rolloff).
     - → `result.category = "Textures"`.

   - **Melodic fallback**
     - `isTonal` but did not satisfy Guitar / Textures / Songstarter.
     - → `result.category = "Melodic"`.

   - **Other fallback**
     - For non‑tonal loops that didn’t match Textures.
     - → `result.category = "Other"`.

6. **Non‑loop fallback**

   - If none of the above matched and `result.type != "Loop"`:
     - → `result.category = "Other"`.

#### 4.3 Melodic vibe sub‑tag

If `result.category == "Melodic"`, a `melodicVibe` tag is assigned:

- `Pad`
  - `!hasSharpAttack`
  - `duration > 1.5 s`
  - ≤ 6 onsets.
- `Pluck`
  - `hasSharpAttack`
  - (`duration < 2.5 s` **or** many onsets).
- `Lead`
  - `centroidF > 2400 Hz`.
- `Keys`
  - Default if none of the above apply.

This vibe string is incorporated into the suggested filenames.

#### 4.4 Filename hints post‑processing

Finally, `applyFilenameHints(file, result)` tweaks the `category` using **only the filename** (no extension) — *loops only*:

- Detects hint flags:
  - Guitar‑ish: `"gtr"`, `"guitar"`, `"egtr"`, `"agtr"`, `"strum"`, `"riff"`, `"chug"`, `"powerchord"`, `"pluck"`, `"chord"`, etc.
  - Bass‑ish: `"bass"`, `"808"`, `"sub"`, `"lowend"`, `"bs_"`, `"subdrop"`, `"slide"`, etc.
  - Texture‑ish: `"texture"`, `"atmo"`, `"ambience"`, `"ambient"`, `"drone"`, `"noise"`, `"fx"`, etc.
  - Melodic‑ish: `"melod"`, `"lead"`, `"keys"`, `"piano"`, `"synth"`, `"arp"`, `"hook"`, etc.

Key rules:

- Only nudges loops; one‑shots are unaffected.
- If current `category` is in a generic bucket (`Textures`, `Melodic`, `Other`, `Loops`):
  - Guitar hints → `category = "Guitar"`.
  - Bass hints → `category = "Bass"`.
- For ambiguous textures/others:
  - Strong melodic hints and no texture hints → `category = "Melodic"`.
- If current `category == "Textures"` but there are strong Guitar/Bass/Melodic hints:
  - Reassign to `Guitar`, `Bass`, or `Melodic` accordingly.

---

### 5. Naming and folder structure

#### 5.1 Per‑category counters

- `std::map<juce::String, int> categoryCounters;`
- Each time `analyzeAudio` runs and decides a category:
  - `int count = ++categoryCounters[result.category];`
  - `indexStr = juce::String(count).paddedLeft('0', 2);` → `"01"`, `"02"`, etc.

#### 5.2 Short category names

Mapping used inside `analyzeAudio`:

- `"Kicks"` → `"Kick"`
- `"Snares"` → `"Snare"`
- `"Hi-Hats"` → `"HiHat"`
- `"Bass"` → `"Bass"`
- `"Guitar"` → `"Guitar"`
- `"FX"` → `"FX"`
- `"Percussion"` → `"Perc"`
- `"Melodic"` → `"Melody"`
- `"Textures"` → `"Texture"`
- `"Songstarter"` → `"Songstarter"`
- `"Loops"` → `"Loop"`
- Anything else → `"Sample"`.

This value is called `shortName` and forms the base of the suggested name.

#### 5.3 Suggested name construction

Pieces:

- `shortName` — from map above.
- `vibeStr` — `""` or `"_Pad"`, `"_Pluck"`, `"_Lead"`, `"_Keys"`.
- `bpmStr` — `""` or `"_120bpm"` (loops only, when `bpm > 0`).
- `keyPart` — `""` or `"_Cm"`, `"_F#"` etc.  
  - For **non‑loop drum one‑shots** (`Kicks`, `Snares`, `Hi-Hats`, `Percussion`), `keyPart` is forcibly cleared (no key in name).
- `indexStr` — two digits, e.g. `"_01"`.

Behaviour depends on `generateFunNames`:

1. **Fun names (`generateFunNames == true`)**
   - Category‑specific adjective is chosen randomly (e.g. `Big`, `Crispy`, `Groove`, etc.).
   - **Loop:**
     - `Adj + "_Loop_" + shortName + vibeStr + keyPart + bpmStr + "_" + indexStr`
     - Example: `Groove_Loop_Guitar_Pluck_Cm_120bpm_03`.
   - **One‑Shot:**
     - `Adj + "_" + shortName + vibeStr + keyPart + "_" + indexStr`
     - Example: `Big_Kick_01`, `Crispy_Snare_02`.

2. **Non‑fun names (`generateFunNames == false`)**
   - **Loop:**
     - `"Loop_" + shortName + vibeStr + keyPart + bpmStr + "_" + indexStr`
     - Example: `Loop_Guitar_Pluck_Cm_120bpm_01`.
   - **One‑Shot:**
     - `shortName + vibeStr + keyPart + "_" + indexStr`
     - Example: `Kick_01`, `Melody_Pad_Cm_01`.

The final string goes into `AnalysisResult.suggestedName`, and later into `SampleInfo.suggested_name`.

#### 5.4 Output folder layout (`copyToFolder`)

`bool copyToFolder(SampleInfo& info);`

1. **Base dir**
   - `baseDir = currentProcessDirectory` if it’s a directory; otherwise `outputDirectory` from Settings.

2. **Type folder**
   - `typeFolderName = (info.type == "Loop") ? "Loop" : "One-Shot";`
   - `typeFolder = baseDir.getChildFile(typeFolderName); typeFolder.createDirectory();`

3. **Category folder (with drums grouping & loops tweak)**
   - Start with `categoryFolderName = info.category`.
   - Special case:
     - For loops with `category == "Loops"`, rename category folder to `"Melodic"` to avoid `Loop/Loops/...`.
   - Drum grouping:
     - Drum categories: `"Kicks"`, `"Snares"`, `"Hi-Hats"`, `"Percussion"`, `"Claps"`.
     - These are placed under an extra `"Drums"` folder:
       - `Loop/Drums/Kicks`, `One-Shot/Drums/Snares`, etc.
   - All others go directly under `typeFolder`:
     - `Loop/Melodic`, `Loop/Guitar`, `One-Shot/Bass`, etc.

4. **Target filename**
   - If `info.suggested_name` is non‑empty:
     - `newName = info.suggested_name + "." + ext;`
   - Else (rare fallback):
     - Construct from `info.name` + key/BPM pieces:
       - `info.name + keyPart + bpmPart + "." + ext`.

5. **Collision handling**
   - `dest = folder.getChildFile(newName);`
   - While `dest.existsAsFile()`:
     - Keep base name (`suggested_name` or `info.name`).
     - Append `"_1"`, `"_2"`, … until a free filename is found.

6. **Copy and verification**
   - `info.success = info.sourceFile.copyFileTo(dest);`
   - Sleep 10 ms, then verify sizes match; if mismatch or `dest` doesn’t exist, `info.success = false;`.
   - On success, `info.outputPath = dest.getFullPathName();`.

---

### 6. Key settings that affect behaviour

- `projectKey` (default `"C Major"`)  
  - Used as default key and, when `useProjectKey == true`, **overrides detected key** in `SampleInfo` / naming.

- `projectBPM` (default `120`)  
  - Base BPM for non‑loops and as an initial guess for some UI; loops normally use detected BPM or host BPM.

- `useHostBpm`  
  - When true and host reports a tempo, loops will use **host BPM** instead of Essentia detection.

- `useProjectKey`  
  - When true, all processed files are labelled with `projectKey` in metadata / naming, regardless of detected key.

- `namingFormat` / `customPrefix`  
  - Currently not heavily used in `analyzeAudio`; most naming uses the “smart naming” described above. Any future work to respect explicit naming formats should likely hook here.

- `generateFunNames`  
  - Controls whether adjectives (Big/Crispy/Groove/etc.) are added.

- `overwriteDuplicates`  
  - Present as a flag but **not currently used** inside `copyToFolder`; the copy logic always avoids overwriting by appending `_1`, `_2`, etc. If overwrite behaviour is desired, that logic would need to be adjusted.

---

### 7. Where to plug in changes

- **Category / loop vs one‑shot decisions** → `analyzeAudio()` in `PluginProcessor.cpp`.
- **Filename hints (e.g. guitar/bass/texture detection from names)** → `applyFilenameHints()` in `PluginProcessor.cpp`.
- **Duplicate logic and thresholds** → `computeFingerprint`, `fingerprintSimilarity`, `isDuplicate` (anonymous namespace in `PluginProcessor.cpp`).
- **Key/BPM handling and host/project overrides** → `analyzeAudio()` and `processAll()` (look for `hostBpmOverride`, `useHostBpm`, `useProjectKey`).
- **Folder structure and final filenames** → `copyToFolder()` in `PluginProcessor.cpp`.

If you’re correcting classification or naming behaviour, almost everything you’ll touch lives in `PluginProcessor.cpp` and is wired via the code paths described above.

