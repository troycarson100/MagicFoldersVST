# Training the Magic Folders Instrument Classifier

This folder contains everything needed to **train the ML model for free** and plug it into the plugin.

## Quick start (Google Colab, free)

1. **Upload this folder** to Google Drive or Colab, or clone the repo.
2. **Open a new Colab notebook** and set runtime to **GPU** (Runtime → Change runtime type → T4 GPU).
3. In a cell, run:
   ```python
   !pip install torch torchaudio numpy scipy onnx soundfile
   ```
4. **Prepare data**: create a zip of your dataset with this structure, then upload and unzip in Colab:
   ```
   data/
     Kick/    ← put kick samples here
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
   Supported formats: `.wav`, `.aif`, `.aiff`, `.flac`, `.mp3`. Any sample rate; the script resamples to 22050 Hz and uses the first 2 seconds.

5. **Train and export**:
   ```python
   !python train_and_export.py --data_dir ./data --epochs 30 --out_model InstrumentClassifier.onnx
   ```
6. **Download** `InstrumentClassifier.onnx` from Colab (Files panel → right‑click → Download).
7. **Add to the plugin**:
   - Put `InstrumentClassifier.onnx` in `MagicFoldersVST/assets/`.
   - The plugin’s CMake is set up to embed it when present; rebuild the plugin.

## Where to get training data (free)

- **Freesound.org** – search by tag (e.g. “kick”, “snare”, “bass”), filter by license (CC0/CC-BY). Build folders by tag.
- **Your own sample library** – drag WAVs into the right class folders; even a few hundred per class helps.
- **Free sample packs** – many “free drum kit” or “free one-shots” packs; sort into the 12 class folders.
- **Splice / Loopcloud** – if you have access, export by instrument into the same folder structure.

Aim for **at least 100–200 samples per class**; more is better. Imbalance is OK (e.g. more Kick/Snare than TextureAtmos).

### Freesound fetcher (automated)

1. Get a Freesound API key at https://freesound.org/apiv2/apply/
2. Set `FREESOUND_API_KEY` in your environment or in `training/.env`
3. From `training/`: `python fetch_freesound.py --per-class 300`
   - Downloads CC0 sounds into `data/<Class>/` and logs to `data_sources.csv`
   - Use `--cc-by` to include CC-BY (document attribution if you use them). For no attribution, use only the default CC0 downloads.

### Correction workflow (plugin log + apply_corrections)

When the plugin processes files, it appends one row per file to `Documents/MagicFoldersLogs/detection_predictions.csv` (file_path, predicted_category, type_loop, top1_prob, timestamp). To fix misclassifications and add them to training data:

1. Create `training/labels/corrections.csv` with two columns: `file_path`, `correct_category`. Use the same paths as in the predictions log (or any path to the audio file) and the correct class (Kick, Snare, HiHat, Perc, Bass, Guitar, Keys, SynthPad, SynthLead, FX, TextureAtmos, Other).
2. Run: `python apply_corrections.py` (optionally `--symlink` to symlink instead of copy, or `--data-dir ./data`).
3. Files are copied (or symlinked) into `training/data/<correct_category>/` for future training.

## Running locally (no Colab)

```bash
cd training
pip install -r requirements.txt
# Create data/Kick, data/Snare, ... and add WAVs
python train_and_export.py --data_dir ./data --epochs 30 --out_model InstrumentClassifier.onnx
```

No GPU required; CPU training is slower but works.

## After training

1. Copy `InstrumentClassifier.onnx` to `MagicFoldersVST/assets/`.
2. Rebuild the plugin (the build embeds the model when the file exists).
3. Download and set **ONNX Runtime** for your platform (see main repo README or plugin build docs) so the plugin can run the model.
4. In the plugin, enable **Accuracy Mode (ML)** in settings to use the model.

## Model input/output (for developers)

- **Input**: one batch of log-mel spectrograms, shape `(1, 1, 128, 87)` — i.e. 1 channel, 128 mel bins, 87 time frames (2 s at 22050 Hz, hop 512).
- **Output**: logits shape `(1, 12)` for the 12 classes in `ModelRunner.h` order: Kick, Snare, HiHat, Perc, Bass, Guitar, Keys, SynthPad, SynthLead, FX, TextureAtmos, Other.

The plugin computes the same mel spectrogram in C++ and runs this ONNX model; FilenameBias and ConfidenceGate are applied after inference.
