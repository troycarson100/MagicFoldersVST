#!/usr/bin/env python3
"""
Magic Folders instrument classifier: train on folder-per-class WAVs and export ONNX.
Run locally or in Google Colab (free GPU). No cost required.

Improvements for accuracy: stratified val split, class-weighted loss, augmentation,
LR scheduler, dropout, optional early stopping.

Dataset layout:
  data/
    Kick/
      sample1.wav
      ...
    Snare/
    HiHat/
    ...

Usage:
  python train_and_export.py --data_dir ./data [--epochs 50] [--out_model model.onnx]
"""

import argparse
import os
import random
import sys

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import Dataset, DataLoader, Subset

try:
    import torchaudio
except ImportError:
    print("Install torchaudio: pip install torchaudio")
    sys.exit(1)

from model_constants import (
    SAMPLE_RATE,
    NUM_SAMPLES,
    N_FFT,
    HOP_LENGTH,
    N_MELS,
    N_FRAMES,
    CLASS_NAMES,
    NUM_CLASSES,
)

# Augmentation: random gain range (multiply waveform by value in [gain_min, gain_max])
GAIN_MIN, GAIN_MAX = 0.85, 1.15
# SpecAugment: max time mask size (frames), max freq mask size (mel bins), max number of masks
SPEC_T_MASK, SPEC_F_MASK, SPEC_N_MASK = 8, 12, 2


def load_wav(path: str) -> np.ndarray:
    """Load mono float32 waveform; resample to SAMPLE_RATE and trim/pad to NUM_SAMPLES."""
    waveform, sr = torchaudio.load(path)
    if waveform.shape[0] > 1:
        waveform = waveform.mean(dim=0, keepdim=True)
    if sr != SAMPLE_RATE:
        waveform = torchaudio.functional.resample(waveform, sr, SAMPLE_RATE)
    waveform = waveform.squeeze(0).numpy()
    if len(waveform) > NUM_SAMPLES:
        waveform = waveform[:NUM_SAMPLES]
    elif len(waveform) < NUM_SAMPLES:
        waveform = np.pad(waveform, (0, NUM_SAMPLES - len(waveform)), mode="constant", constant_values=0)
    return waveform.astype(np.float32)


def mel_spectrogram(waveform: np.ndarray) -> np.ndarray:
    """Compute log-mel spectrogram (N_MELS, N_FRAMES)."""
    tensor = torch.from_numpy(waveform).unsqueeze(0)
    mel = torchaudio.transforms.MelSpectrogram(
        sample_rate=SAMPLE_RATE,
        n_fft=N_FFT,
        hop_length=HOP_LENGTH,
        n_mels=N_MELS,
        f_min=0.0,
        f_max=SAMPLE_RATE / 2,
    )(tensor)
    log_mel = (mel + 1e-9).log()
    return log_mel.squeeze(0).numpy()


def spec_augment(mel: np.ndarray, t_mask: int, f_mask: int, n_mask: int) -> np.ndarray:
    """Apply SpecAugment: mask random time and freq bands. In-place style; returns mel."""
    mel = mel.copy()
    n_mels, n_frames = mel.shape
    for _ in range(n_mask):
        if t_mask > 0 and n_frames > t_mask:
            t0 = random.randint(0, n_frames - t_mask)
            mel[:, t0 : t0 + t_mask] = mel.min()
        if f_mask > 0 and n_mels > f_mask:
            f0 = random.randint(0, n_mels - f_mask)
            mel[f0 : f0 + f_mask, :] = mel.min()
    return mel


class InstrumentDataset(Dataset):
    def __init__(self, data_dir: str, transform_mel=True, augment=False):
        self.samples = []  # (filepath, class_index)
        self.transform_mel = transform_mel
        self.augment = augment
        for ci, name in enumerate(CLASS_NAMES):
            folder = os.path.join(data_dir, name)
            if not os.path.isdir(folder):
                continue
            # Walk recursively so sub-folders (e.g. Pad/Soft Pad/) are included.
            for root, _dirs, files in os.walk(folder):
                for f in files:
                    if f.lower().endswith((".wav", ".aif", ".aiff", ".flac", ".mp3")):
                        self.samples.append((os.path.join(root, f), ci))

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, idx):
        path, label = self.samples[idx]
        try:
            wav = load_wav(path)
        except Exception:
            wav = np.zeros(NUM_SAMPLES, dtype=np.float32)
        if self.augment:
            gain = random.uniform(GAIN_MIN, GAIN_MAX)
            wav = (wav * gain).astype(np.float32)
        if self.transform_mel:
            mel = mel_spectrogram(wav)
            if self.augment:
                mel = spec_augment(mel, SPEC_T_MASK, SPEC_F_MASK, SPEC_N_MASK)
            return torch.from_numpy(mel).unsqueeze(0).float(), label  # (1, N_MELS, N_FRAMES)
        return torch.from_numpy(wav).float(), label


def stratified_split(dataset: InstrumentDataset, val_ratio: float, seed: int = 42):
    """Split dataset by class so train/val have similar class distribution."""
    random.seed(seed)
    by_class = [[] for _ in range(NUM_CLASSES)]
    for i, (_, label) in enumerate(dataset.samples):
        by_class[label].append(i)
    train_idx, val_idx = [], []
    for indices in by_class:
        random.shuffle(indices)
        n_val = max(0, int(len(indices) * val_ratio))
        if n_val == 0 and indices:
            n_val = 1
        n_train = len(indices) - n_val
        train_idx.extend(indices[:n_train])
        val_idx.extend(indices[n_train:])
    random.shuffle(train_idx)
    random.shuffle(val_idx)
    return train_idx, val_idx


def compute_class_weights(samples: list) -> torch.Tensor:
    """Inverse frequency weights (total / (n_classes * count)) for balanced loss."""
    counts = [0] * NUM_CLASSES
    for _, label in samples:
        counts[label] += 1
    total = len(samples)
    weights = []
    for c in counts:
        if c > 0:
            w = total / (NUM_CLASSES * c)
        else:
            w = 1.0
        weights.append(w)
    return torch.tensor(weights, dtype=torch.float32)


class SmallCNN(nn.Module):
    """Small CNN on mel spectrogram (1, N_MELS, N_FRAMES) -> logits (NUM_CLASSES)."""

    def __init__(self, dropout=0.25):
        super().__init__()
        self.conv = nn.Sequential(
            nn.Conv2d(1, 32, 3, padding=1),
            nn.BatchNorm2d(32),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2),
            nn.Conv2d(32, 64, 3, padding=1),
            nn.BatchNorm2d(64),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2),
            nn.Conv2d(64, 128, 3, padding=1),
            nn.BatchNorm2d(128),
            nn.ReLU(inplace=True),
            nn.AdaptiveAvgPool2d(1),
        )
        self.dropout = nn.Dropout(p=dropout)
        self.fc = nn.Linear(128, NUM_CLASSES)

    def forward(self, x):
        # x: (B, 1, N_MELS, N_FRAMES)
        x = self.conv(x)
        x = x.flatten(1)
        x = self.dropout(x)
        return self.fc(x)


def train_one_epoch(model, loader, optimizer, device, class_weights=None):
    model.train()
    total_loss = 0.0
    criterion = nn.CrossEntropyLoss(weight=class_weights)
    for mel, target in loader:
        mel, target = mel.to(device), target.to(device)
        optimizer.zero_grad()
        logits = model(mel)
        loss = criterion(logits, target)
        loss.backward()
        optimizer.step()
        total_loss += loss.item()
    return total_loss / len(loader)


def main():
    parser = argparse.ArgumentParser(description="Train instrument classifier and export ONNX")
    parser.add_argument("--data_dir", type=str, default="./data", help="Root folder with class subdirs (Kick, Snare, ...)")
    parser.add_argument("--epochs", type=int, default=50, help="Training epochs")
    parser.add_argument("--batch_size", type=int, default=32)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--out_model", type=str, default="InstrumentClassifier.onnx", help="Output ONNX path")
    parser.add_argument("--val_ratio", type=float, default=0.15, help="Fraction of data for validation")
    parser.add_argument("--no_class_weights", action="store_true", help="Disable class-weighted loss")
    parser.add_argument("--no_augment", action="store_true", help="Disable data augmentation")
    parser.add_argument("--early_stop", type=int, default=12, help="Stop if no val_acc improvement for this many epochs (0=off)")
    args = parser.parse_args()

    if not os.path.isdir(args.data_dir):
        print(f"Data directory not found: {args.data_dir}")
        print("Create it with subdirs: Kick, Snare, HiHat, Perc, Bass, Guitar, Keys, Pad, Lead, FX, TextureAtmos, Vocal, Other")
        sys.exit(1)

    dataset_full = InstrumentDataset(args.data_dir, augment=not args.no_augment)
    if len(dataset_full) == 0:
        print("No audio files found under", args.data_dir)
        sys.exit(1)

    # Per-class counts
    counts = [0] * NUM_CLASSES
    for _, label in dataset_full.samples:
        counts[label] += 1
    print("Per-class counts:", dict(zip(CLASS_NAMES, counts)))

    train_idx, val_idx = stratified_split(dataset_full, args.val_ratio)
    train_ds = Subset(dataset_full, train_idx)
    val_dataset = InstrumentDataset(args.data_dir, augment=False)  # no augmentation for validation
    val_ds = Subset(val_dataset, val_idx)
    train_loader = DataLoader(train_ds, batch_size=args.batch_size, shuffle=True, num_workers=0, pin_memory=True)
    val_loader = DataLoader(val_ds, batch_size=args.batch_size)

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print("Using device:", device)

    class_weights = None
    if not args.no_class_weights:
        class_weights = compute_class_weights(dataset_full.samples).to(device)
        print("Class weights (inverse frequency):", class_weights.tolist())

    model = SmallCNN(dropout=0.25).to(device)
    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr)
    scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(
        optimizer, mode="max", factor=0.5, patience=5, min_lr=1e-5
    )

    best_val_acc = 0.0
    best_state = None
    epochs_no_improve = 0

    for epoch in range(args.epochs):
        loss = train_one_epoch(model, train_loader, optimizer, device, class_weights)
        model.eval()
        correct, total = 0, 0
        with torch.no_grad():
            for mel, target in val_loader:
                mel, target = mel.to(device), target.to(device)
                logits = model(mel)
                pred = logits.argmax(dim=1)
                correct += (pred == target).sum().item()
                total += target.size(0)
        acc = correct / total if total else 0
        scheduler.step(acc)
        print(f"Epoch {epoch + 1}/{args.epochs}  loss={loss:.4f}  val_acc={acc:.4f}  lr={optimizer.param_groups[0]['lr']:.2e}")

        if acc > best_val_acc:
            best_val_acc = acc
            best_state = {k: v.cpu().clone() for k, v in model.state_dict().items()}
            epochs_no_improve = 0
        else:
            epochs_no_improve += 1
        if args.early_stop > 0 and epochs_no_improve >= args.early_stop:
            print(f"Early stopping at epoch {epoch + 1} (no improvement for {args.early_stop} epochs). Best val_acc={best_val_acc:.4f}")
            if best_state is not None:
                model.load_state_dict(best_state)
            break

    if best_state is not None:
        model.load_state_dict(best_state)
    model.eval()
    # Export: ensure dropout is off (model.eval() already does that)
    dummy = torch.zeros(1, 1, N_MELS, N_FRAMES)
    torch.onnx.export(
        model,
        dummy,
        args.out_model,
        input_names=["mel"],
        output_names=["logits"],
        dynamic_axes={"mel": {0: "batch"}, "logits": {0: "batch"}},
        opset_version=18,  # ORT 1.20 supports opset 18
    )
    print("Exported ONNX to", args.out_model)

    # Also save a PyTorch checkpoint for offline evaluation tools.
    ckpt_path = os.path.splitext(args.out_model)[0] + ".pt"
    torch.save(model.state_dict(), ckpt_path)
    print("Saved PyTorch weights to", ckpt_path)
    print("Copy the ONNX file to MagicFoldersVST/assets/InstrumentClassifier.onnx and rebuild the plugin.")


if __name__ == "__main__":
    main()
