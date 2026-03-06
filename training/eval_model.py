#!/usr/bin/env python3
"""
Evaluate a trained Magic Folders instrument classifier on a labeled folder tree.

Usage:
  python eval_model.py --data_dir ./data --ckpt InstrumentClassifier.pt

The data_dir layout is the same as train_and_export.py:
  data/
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
"""

from __future__ import annotations

import argparse
import os
from collections import defaultdict

import numpy as np
import torch
from torch.utils.data import DataLoader

from model_constants import (
    N_MELS,
    N_FRAMES,
    CLASS_NAMES,
    NUM_CLASSES,
)
from train_and_export import InstrumentDataset, SmallCNN


def evaluate(data_dir: str, ckpt_path: str, batch_size: int = 32) -> None:
    if not os.path.isdir(data_dir):
        print("Data directory not found:", data_dir)
        return
    if not os.path.isfile(ckpt_path):
        print("Checkpoint not found:", ckpt_path)
        return

    dataset = InstrumentDataset(data_dir, augment=False)
    if len(dataset) == 0:
        print("No audio files found under", data_dir)
        return

    loader = DataLoader(dataset, batch_size=batch_size, shuffle=False)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print("Using device:", device)

    model = SmallCNN(dropout=0.0).to(device)
    state = torch.load(ckpt_path, map_location=device)
    model.load_state_dict(state)
    model.eval()

    total = 0
    correct = 0
    per_class_total = [0] * NUM_CLASSES
    per_class_correct = [0] * NUM_CLASSES
    confusion = np.zeros((NUM_CLASSES, NUM_CLASSES), dtype=np.int32)

    with torch.no_grad():
        for mel, target in loader:
            mel, target = mel.to(device), target.to(device)
            logits = model(mel)
            pred = logits.argmax(dim=1)
            for t, p in zip(target.cpu().tolist(), pred.cpu().tolist()):
                total += 1
                per_class_total[t] += 1
                confusion[t, p] += 1
                if t == p:
                    correct += 1
                    per_class_correct[t] += 1

    overall_acc = correct / total if total else 0.0
    print(f"\nOverall accuracy: {overall_acc:.4f} ({correct}/{total})")
    print("\nPer-class accuracy:")
    for idx, name in enumerate(CLASS_NAMES):
        n = per_class_total[idx]
        c = per_class_correct[idx]
        acc = c / n if n else 0.0
        print(f"  {name:12s}: {acc:.4f} ({c}/{n})")

    print("\nConfusion matrix (rows = true, cols = pred):")
    header = " " * 14 + " ".join(f"{name[:4]:>4s}" for name in CLASS_NAMES)
    print(header)
    for i, name in enumerate(CLASS_NAMES):
        row = " ".join(f"{confusion[i, j]:4d}" for j in range(NUM_CLASSES))
        print(f"{name[:12]:12s} {row}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Evaluate Magic Folders instrument classifier")
    parser.add_argument("--data_dir", type=str, default="./data")
    parser.add_argument("--ckpt", type=str, default="InstrumentClassifier.pt")
    parser.add_argument("--batch_size", type=int, default=32)
    args = parser.parse_args()
    evaluate(args.data_dir, args.ckpt, args.batch_size)


if __name__ == "__main__":
    main()

