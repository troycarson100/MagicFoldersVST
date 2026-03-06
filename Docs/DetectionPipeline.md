## Magic Folders Detection Pipeline

This document summarizes how Magic Folders decides a sample's category and name, and how to retrain the ONNX model that powers Accuracy Mode.

### 1. High-level flow

For each file in the queue, `MagicFoldersProcessor::processAll()` calls:

1. `analyzeAudio(file, hostBpmOverride)` to get an `AnalysisResult`:
   - Essentia runs offline on a worker thread to compute:
     - Onset times, BPM, key + key strength
     - Spectral centroid, rolloff, zero-crossing rate
     - MFCCs (first few coefficients)
     - Loudness and attack vs body RMS (sharp attack flag)
     - Duration and loop vs one-shot type
   - These features feed both the **heuristic rules** and the **ML model**.
2. The final `AnalysisResult` is used to:
   - Decide category (Kicks, Snares, Hi-Hats, Percussion, Bass, Guitar, Melodic, FX, Textures, Songstarter, Other).
   - Build a smart `suggestedName` string (with optional fun adjectives).
   - Decide target subfolder when copying samples.

### 2. Heuristic path (always available)

The heuristic detector lives in:

- `Source/Detection/HeuristicConstants.h` – named thresholds for spectral features, durations, onset counts, etc.
- `Source/Detection/HeuristicCategory.{h,cpp}` – rules that map Essentia features + filename hints to a category and optional melodic vibe.

Key ideas:

- **Drum-first rules**: sharp-attack + low centroid → Kicks; mid centroid + attack → Snares; very high ZCR + bright + very short → Hi-Hats; short + sharp attack → Percussion.
- **Bass vs Guitar vs Melodic**: centroid, MFCC1/2, key strength and onset density help separate Bass, Guitar, and generic Melodic.
- **Textures/FX**: non-tonal, long, sparse, dark or noisy swish patterns become Textures; very bright, noisy material with high rolloff and ZCR tends toward FX.
- **Filename hints**: `categoryFromFilename` and `applyFilenameHints` use strong keywords (kick, snare, hihat, bass, gtr, piano, pad, texture, atmo, fx, etc.) to steer ambiguous cases, especially when the heuristic says `Other` or a generic bucket.

When Accuracy Mode is **off**, only this heuristic path is used.

### 3. ONNX model path (Accuracy Mode)

When **Accuracy Mode (ML)** is enabled and an embedded ONNX model is present:

1. `DetectionV2::classify()` runs:
   - `ModelRunner::predict()` on a mono buffer (mel-spectrogram front-end in `MelSpectrogram`), producing 12 logits in the order defined in `ModelRunner.h`:
     - Kick, Snare, HiHat, Perc, Bass, Guitar, Keys, SynthPad, SynthLead, FX, TextureAtmos, Other.
   - `FilenameBias::apply()` nudges logits based on filename keywords (e.g. “snare” boosts the Snare logit).
   - `ConfidenceGate::apply()` applies a conservative gate using:
     - `top1Prob` (highest softmax probability)
     - `top2Prob` (second highest)
     - minimum top1 and minimum margin (`top1Prob - top2Prob`)
   - If the gate **accepts**, `DetectionV2` reports `hasDecision = true` and the chosen `Class` plus top1/top2 confidence.
2. In `analyzeAudio()`:
   - If `useAccurateDetection` is true and `DetectionV2` accepts:
     - The ONNX class is mapped to a category (Kicks, Snares, Hi-Hats, etc.).
     - This replaces the heuristic category for that file.
   - If the gate rejects or the model is unavailable:
     - The heuristic path in `HeuristicCategory::run()` is used instead.

Current ConfidenceGate thresholds are tuned to favor **“not confidently wrong”**:

- Predictions are only accepted when both:
  - `top1Prob >= 0.60`
  - `top1Prob - top2Prob >= 0.12`

Anything below that falls back to heuristics.

### 4. Training and exporting the ONNX model

Training happens in the `training/` folder and never runs inside the plugin.

- **Data layout** (same as Essentia/Heuristic category names):

  ```text
  training/data/
    Kick/
    Snare/
    HiHat/
    Perc/
    Bass/
    Guitar/
    Keys/
    SynthPad/
    SynthLead/
    FX/
    TextureAtmos/
    Other/
  ```

- **Training script**: `training/train_and_export.py`
  - Uses `torchaudio` to:
    - Load and resample audio to 22,050 Hz.
    - Trim/pad to 2 seconds.
    - Compute a `(N_MELS, N_FRAMES)` log-mel spectrogram matching the C++ side.
  - Model: `SmallCNN` – a lightweight 3-layer CNN over the mel spectrogram.
  - Improvements included:
    - **Data augmentation**:
      - Random gain on waveform.
      - SpecAugment (time/frequency masking) on mel.
    - **Class-weighted loss** to compensate for class imbalance.
    - **Stratified train/val split** so all classes appear in validation.
    - **ReduceLROnPlateau** scheduler on validation accuracy.
    - Optional **early stopping** when `val_acc` stops improving.
  - At the end of training, it exports:
    - `InstrumentClassifier.onnx` – ONNX model that the plugin uses.
    - `InstrumentClassifier.pt` – PyTorch checkpoint for offline evaluation.

- **How to train and export**:

  ```bash
  cd training
  pip install -r requirements.txt
  python train_and_export.py --data_dir ./data --epochs 50 --out_model InstrumentClassifier.onnx
  ```

  Then copy the ONNX file into the plugin:

  ```bash
  cp InstrumentClassifier.onnx ../assets/InstrumentClassifier.onnx
  ```

  Rebuild the plugin and restart the Standalone to pick up the new model.

### 5. Evaluating a model on labeled data

Use `training/eval_model.py` to benchmark a checkpoint on your dataset:

```bash
cd training
python eval_model.py --data_dir ./data --ckpt InstrumentClassifier.pt
```

This prints:

- Overall accuracy.
- Per-class accuracy.
- A confusion matrix (rows = true label, cols = predicted label).

Use this to understand where the model makes the worst mistakes (e.g. Piano vs Bass, Snare vs HiHat) before wiring a new ONNX into the plugin.

### 6. Data loop: fetch, correct, retrain

To keep improving detection over time:

1. **Fetch more data** with `training/fetch_freesound.py`:
   - Downloads CC0 (and optionally CC-BY) samples from Freesound into `training/data/<Class>/`.
2. **Run Magic Folders** on real packs:
   - With Accuracy Mode on or off, process your usual sample sets.
   - The plugin logs predictions to `Documents/MagicFoldersLogs/detection_predictions.csv`.
3. **Correct mistakes**:
   - Create `training/labels/corrections.csv` with two columns: `file_path, correct_category`.
   - Use `training/apply_corrections.py` to copy or symlink misclassified files into `training/data/<correct_category>/`.
4. **Retrain and export** using the updated data, then re-evaluate with `eval_model.py` and, when satisfied, export a new ONNX to the plugin.

