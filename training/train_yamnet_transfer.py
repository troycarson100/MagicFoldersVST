#!/usr/bin/env python3
"""
YAMNet Transfer Learning v2 — MagicFolders InstrumentClassifier

Architecture:
    Audio file  →  YAMNet (frozen, 15600-sample windows @ 16kHz, output [1, 521])
                →  Per-window stats: [mean ‖ std ‖ max]  →  [1563] feature vector
                →  MLP (1563 → 512 → 256 → 13)  →  instrument logits

Improvements over v1:
  • Richer features: mean+std+max of per-window YAMNet outputs (1563-d vs 521-d)
    — std captures temporal variance (hi-hat loops vs snares differ here)
    — max preserves the strongest transient signal (critical for loops)
  • Data augmentation: time-stretch ×{0.9, 1.1} + Gaussian noise, tripling data
  • Class balancing: Snare capped at MAX_PER_CLASS, Lead/Pad oversampled
  • ReduceLROnPlateau scheduler + 300 epochs + larger MLP (512 hidden)

Usage:
    cd "/Users/troycarson/Documents/JUCE Projects/MagicFoldersVST"
    python3 training/train_yamnet_transfer.py

Output:
    assets/Models/yamnet_head.onnx
    training/yamnet_features_v2_cache.npz
"""

import os
import sys
import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import Dataset, DataLoader, WeightedRandomSampler
from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report
import onnxruntime as ort
import librosa
from pathlib import Path

# ─── Class mapping ────────────────────────────────────────────────────────────
# MUST match Detection::Class order in ModelRunner.h:
#   Kick=0, Snare=1, HiHat=2, Perc=3, Bass=4, Guitar=5,
#   Keys=6, Pad=7, Lead=8, FX=9, TextureAtmos=10, Vocal=11, Other=12
CLASS_ORDER = {
    "Kick": 0, "Snare": 1, "HiHat": 2, "Perc": 3,
    "Bass": 4, "Guitar": 5, "Keys": 6, "Pad": 7,
    "Lead": 8, "FX": 9, "TextureAtmos": 10, "Vocal": 11,
}
NUM_CLASSES_TRAINED = 12   # index 12 "Other" has no training data
NUM_CLASSES_OUTPUT  = 13   # model outputs 13 logits (Other = always low)

# ─── Paths ────────────────────────────────────────────────────────────────────
SCRIPT_DIR  = Path(__file__).parent
REPO_ROOT   = SCRIPT_DIR.parent
YAMNET_ONNX = REPO_ROOT / "assets" / "Models" / "yamnet.onnx"
DATA_ROOT   = Path.home() / "Documents" / "MagicFoldersTraining"
OUT_MODEL   = REPO_ROOT / "assets" / "Models" / "yamnet_head.onnx"
CACHE_FILE  = SCRIPT_DIR / "yamnet_features_v3_cache.npz"   # v3 — includes loop sub-folders

YAMNET_RATE    = 16000
YAMNET_WINDOW  = 15600   # samples @ 16 kHz (~0.975 s)
HOP_SAMPLES    = YAMNET_WINDOW // 2   # 50% overlap
FEATURE_DIM    = 521 * 3             # mean + std + max per YAMNet window

# Class balancing: cap large classes, set floor for small ones
# Raised to 600 to accommodate new loop sub-folders (loops/ + root one-shots).
# Snare was 685 before loops; with loops it may hit 800+ so cap prevents domination.
MAX_PER_CLASS  = 600   # cap to prevent any single class dominating
MIN_PER_CLASS  = 300   # below this, add augmented copies


# ─── Feature extraction ───────────────────────────────────────────────────────

def run_yamnet(buf: np.ndarray, session: ort.InferenceSession) -> np.ndarray:
    """Run one 15600-sample window through YAMNet → [521] probabilities."""
    assert len(buf) == YAMNET_WINDOW
    return session.run(None, {"waveform_binary": buf.astype(np.float32)})[0][0]


def extract_features(waveform: np.ndarray,
                     session: ort.InferenceSession) -> np.ndarray:
    """Return 1563-d feature vector [mean ‖ std ‖ max] from all windows."""
    peak = np.abs(waveform).max()
    if peak > 1e-8:
        waveform = waveform / peak

    n = len(waveform)
    starts = list(range(0, max(1, n - YAMNET_WINDOW + 1), HOP_SAMPLES))
    if not starts:
        starts = [0]

    frames = []
    for s in starts:
        buf = np.zeros(YAMNET_WINDOW, dtype=np.float32)
        end = min(s + YAMNET_WINDOW, n)
        buf[:end - s] = waveform[s:end]
        frames.append(run_yamnet(buf, session))

    frames = np.stack(frames)                  # (W, 521)
    feat_mean = frames.mean(axis=0)
    feat_std  = frames.std(axis=0)
    feat_max  = frames.max(axis=0)
    return np.concatenate([feat_mean, feat_std, feat_max]).astype(np.float32)


def load_audio(path: str) -> "np.ndarray | None":
    try:
        audio, _ = librosa.load(path, sr=YAMNET_RATE, mono=True,
                                res_type="kaiser_fast")
        return audio.astype(np.float32)
    except Exception as e:
        print(f"    [skip] {os.path.basename(path)}: {e}")
        return None


def augment(audio: np.ndarray) -> list:
    """Return augmented variants of an audio clip (does NOT include original)."""
    variants = []
    try:
        # Time-stretch slow / fast
        variants.append(librosa.effects.time_stretch(audio, rate=0.9))
        variants.append(librosa.effects.time_stretch(audio, rate=1.1))
    except Exception:
        pass
    # Mild Gaussian noise (~30 dB SNR)
    noise = np.random.randn(len(audio)).astype(np.float32) * 0.03
    variants.append(audio + noise)
    return variants


# ─── Dataset build ────────────────────────────────────────────────────────────

def build_dataset(session: ort.InferenceSession):
    """Walk training folders, extract features, apply augmentation & balancing."""
    X_all, y_all = [], []

    for cls_name, cls_idx in CLASS_ORDER.items():
        cls_dir = DATA_ROOT / cls_name
        if not cls_dir.exists():
            print(f"  WARNING: missing folder → {cls_dir}")
            continue

        files = []
        for root, _, fnames in os.walk(cls_dir):
            for f in fnames:
                if f.lower().endswith((".wav", ".aif", ".aiff",
                                       ".mp3", ".flac", ".ogg")):
                    files.append(os.path.join(root, f))

        # Cap large classes
        if len(files) > MAX_PER_CLASS:
            rng = np.random.default_rng(42)
            files = list(rng.choice(files, MAX_PER_CLASS, replace=False))

        print(f"  {cls_name:14s} ({cls_idx}): {len(files):4d} files", flush=True)

        raw_feats = []
        for fpath in files:
            audio = load_audio(fpath)
            if audio is None or len(audio) < 512:
                continue
            feat = extract_features(audio, session)
            raw_feats.append((feat, audio))

        for feat, _ in raw_feats:
            X_all.append(feat)
            y_all.append(cls_idx)

        # Augment small classes up to MIN_PER_CLASS
        current = len(raw_feats)
        if current < MIN_PER_CLASS:
            needed = MIN_PER_CLASS - current
            rng = np.random.default_rng(42 + cls_idx)
            indices = rng.integers(0, len(raw_feats), needed)
            added = 0
            for i in indices:
                _, audio = raw_feats[i]
                variants = augment(audio)
                if not variants:
                    continue
                var_audio = variants[rng.integers(0, len(variants))]
                feat = extract_features(var_audio, session)
                X_all.append(feat)
                y_all.append(cls_idx)
                added += 1
                if added >= needed:
                    break
            print(f"  {'':14s}         augmented {added} extra samples", flush=True)

    X = np.stack(X_all)
    y = np.array(y_all, dtype=np.int64)
    print(f"\n  Total: {len(y)} samples, feature dim = {X.shape[1]}", flush=True)
    return X, y


# ─── MLP ──────────────────────────────────────────────────────────────────────

class InstrumentMLP(nn.Module):
    def __init__(self, in_dim: int = FEATURE_DIM, h1: int = 512, h2: int = 256,
                 num_classes: int = NUM_CLASSES_OUTPUT):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(in_dim, h1),
            nn.BatchNorm1d(h1),
            nn.ReLU(),
            nn.Dropout(0.35),
            nn.Linear(h1, h2),
            nn.BatchNorm1d(h2),
            nn.ReLU(),
            nn.Dropout(0.25),
            nn.Linear(h2, num_classes),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.net(x)


class FeatureDataset(Dataset):
    def __init__(self, X: np.ndarray, y: np.ndarray):
        self.X = torch.from_numpy(X)
        self.y = torch.from_numpy(y)

    def __len__(self) -> int:
        return len(self.y)

    def __getitem__(self, idx):
        return self.X[idx], self.y[idx]


def train_mlp(X: np.ndarray, y: np.ndarray, epochs: int = 300) -> InstrumentMLP:
    counts = np.bincount(y, minlength=NUM_CLASSES_TRAINED)
    total  = len(y)
    weights_12  = [total / (NUM_CLASSES_TRAINED * max(c, 1)) for c in counts]
    weights_all = weights_12 + [1.0]   # "Other" gets weight 1 (no samples)
    class_weights = torch.tensor(weights_all, dtype=torch.float32)

    print("\nClass sample counts (after balancing/augmentation):")
    for name, idx in CLASS_ORDER.items():
        print(f"  {name:14s}: {counts[idx]:4d}  (weight {weights_12[idx]:.2f})")

    X_tr, X_val, y_tr, y_val = train_test_split(
        X, y, test_size=0.15, stratify=y, random_state=42)
    print(f"\nTrain: {len(y_tr)}  Val: {len(y_val)}", flush=True)

    tr_loader  = DataLoader(FeatureDataset(X_tr, y_tr),  batch_size=64,
                            shuffle=True,  num_workers=0)
    val_loader = DataLoader(FeatureDataset(X_val, y_val), batch_size=256,
                            shuffle=False, num_workers=0)

    model     = InstrumentMLP()
    criterion = nn.CrossEntropyLoss(weight=class_weights)
    optimizer = torch.optim.AdamW(model.parameters(), lr=3e-4, weight_decay=1e-4)
    scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(
        optimizer, mode="max", factor=0.5, patience=20, min_lr=1e-6)

    best_val_acc = 0.0
    best_state   = None

    for epoch in range(epochs):
        model.train()
        running_loss = 0.0
        for Xb, yb in tr_loader:
            optimizer.zero_grad()
            loss = criterion(model(Xb), yb)
            loss.backward()
            optimizer.step()
            running_loss += loss.item() * len(yb)

        model.eval()
        correct = n_val = 0
        with torch.no_grad():
            for Xb, yb in val_loader:
                preds    = model(Xb).argmax(dim=1)
                correct += (preds == yb).sum().item()
                n_val   += len(yb)
        val_acc = correct / n_val
        scheduler.step(val_acc)

        if val_acc > best_val_acc:
            best_val_acc = val_acc
            best_state   = {k: v.clone() for k, v in model.state_dict().items()}

        if (epoch + 1) % 30 == 0 or epoch == 0:
            avg_loss = running_loss / len(y_tr)
            lr_now   = optimizer.param_groups[0]["lr"]
            print(f"  ep {epoch+1:3d}/{epochs}  loss={avg_loss:.4f}  "
                  f"val_acc={val_acc:.3f}  best={best_val_acc:.3f}  lr={lr_now:.2e}",
                  flush=True)

    print(f"\nBest validation accuracy: {best_val_acc:.3f}")
    model.load_state_dict(best_state)

    # Per-class accuracy on validation set
    model.eval()
    all_preds, all_true = [], []
    with torch.no_grad():
        for Xb, yb in val_loader:
            all_preds.extend(model(Xb).argmax(dim=1).tolist())
            all_true.extend(yb.tolist())
    print("\nPer-class report (validation):")
    print(classification_report(
        all_true, all_preds,
        labels=list(range(NUM_CLASSES_TRAINED)),
        target_names=list(CLASS_ORDER.keys()),
        zero_division=0,
    ))
    return model


# ─── ONNX export ──────────────────────────────────────────────────────────────

def export_onnx(model: InstrumentMLP, out_path: Path) -> None:
    import onnx as onnx_lib
    model.eval()
    dummy = torch.zeros(1, FEATURE_DIM)
    torch.onnx.export(
        model,
        dummy,
        str(out_path),
        input_names=["yamnet_features"],
        output_names=["logits"],
        dynamic_axes={"yamnet_features": {0: "batch"}, "logits": {0: "batch"}},
        opset_version=13,
        do_constant_folding=True,
    )
    # Ensure weights are stored inline (not split into .data sidecar file)
    data_file = out_path.with_suffix(out_path.suffix + ".data")
    if data_file.exists():
        merged = onnx_lib.load(str(out_path), load_external_data=True)
        onnx_lib.save_model(merged, str(out_path), save_as_external_data=False)
        data_file.unlink(missing_ok=True)
    sess   = ort.InferenceSession(str(out_path))
    result = sess.run(None, {"yamnet_features": np.zeros((1, FEATURE_DIM),
                                                          dtype=np.float32)})
    print(f"Exported → {out_path}")
    print(f"  ONNX verification: input [1,{FEATURE_DIM}] → output {result[0].shape}  ✓")


# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    print("=" * 60)
    print("  YAMNet Transfer Learning v2 — MagicFolders")
    print(f"  Feature dim: {FEATURE_DIM}  (mean+std+max of 521-d windows)")
    print("=" * 60)
    print()

    if not YAMNET_ONNX.exists():
        print(f"ERROR: yamnet.onnx not found at {YAMNET_ONNX}")
        sys.exit(1)
    if not DATA_ROOT.exists():
        print(f"ERROR: Training data not found at {DATA_ROOT}")
        sys.exit(1)

    # ── Feature extraction ────────────────────────────────────────────────────
    if CACHE_FILE.exists():
        ans = input(f"v2 cache found. Re-use? [Y/n] ").strip().lower()
        if ans in ("", "y", "yes"):
            print("Loading cached features...")
            data = np.load(CACHE_FILE)
            X, y = data["features"], data["labels"]
            print(f"  Loaded {len(y)} samples, dim={X.shape[1]}")
        else:
            CACHE_FILE.unlink()

    if not CACHE_FILE.exists():
        print(f"Loading YAMNet from {YAMNET_ONNX}...")
        yamnet = ort.InferenceSession(str(YAMNET_ONNX))
        print("Extracting features + augmenting (this will take 10-20 min)...")
        X, y = build_dataset(yamnet)
        np.savez(CACHE_FILE, features=X, labels=y)
        print(f"Cached to {CACHE_FILE}")

    # ── Training ─────────────────────────────────────────────────────────────
    print("\nTraining MLP head (300 epochs)...")
    model = train_mlp(X, y)

    # ── Export ───────────────────────────────────────────────────────────────
    OUT_MODEL.parent.mkdir(parents=True, exist_ok=True)
    export_onnx(model, OUT_MODEL)

    print()
    print("✓ Done!  Rebuild the plugin to embed the new yamnet_head.onnx.")
    print(f"  Feature dim = {FEATURE_DIM}  (C++ YamnetRunner will auto-detect from ONNX shape)")


if __name__ == "__main__":
    main()
