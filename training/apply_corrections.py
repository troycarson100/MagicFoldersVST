#!/usr/bin/env python3
"""
Apply manual corrections: read a CSV of (file_path, correct_category) and copy
those files into training/data/<correct_category>/ for future training or analysis.

Usage:
  python apply_corrections.py [--corrections training/labels/corrections.csv] [--data-dir training/data] [--symlink]

Corrections CSV format: file_path, correct_category
  - file_path: absolute or relative path to the audio file (as logged by the plugin or from detection_predictions.csv)
  - correct_category: one of Kick, Snare, HiHat, Perc, Bass, Guitar, Keys, Pad, Lead, FX, TextureAtmos, Vocal, Other

If --symlink is set, creates symlinks instead of copying. Default is copy.
"""

from __future__ import annotations

import argparse
import csv
import os
import shutil
from pathlib import Path

from model_constants import CLASS_NAMES

VALID_CLASSES = frozenset(CLASS_NAMES)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Copy (or symlink) corrected files into training/data/<correct_category>/"
    )
    parser.add_argument(
        "--corrections",
        type=Path,
        default=Path(__file__).resolve().parent / "labels" / "corrections.csv",
        help="Path to corrections CSV (file_path, correct_category)",
    )
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=Path(__file__).resolve().parent / "data",
        help="Data root (default: training/data)",
    )
    parser.add_argument(
        "--symlink",
        action="store_true",
        help="Create symlinks instead of copying",
    )
    args = parser.parse_args()

    if not args.corrections.exists():
        print(f"Corrections file not found: {args.corrections}")
        print("Create it with columns: file_path, correct_category")
        print("Example:")
        print("  /path/to/sample.wav,Kick")
        print("  /path/to/other.wav,Snare")
        return

    args.data_dir.mkdir(parents=True, exist_ok=True)
    copied = 0
    skipped = 0
    errors = 0

    with open(args.corrections, newline="", encoding="utf-8") as f:
        reader = csv.reader(f)
        rows = list(reader)

    for row in rows:
        if len(row) < 2:
            continue
        path_str, category = row[0].strip(), row[1].strip()
        if path_str.lower() == "file_path" and category.lower() == "correct_category":
            continue
        if not path_str or not category:
            continue
        if category not in VALID_CLASSES:
            print(f"Unknown class '{category}' for {path_str}")
            errors += 1
            continue
        src = Path(path_str)
        if not src.is_file():
            print(f"File not found: {src}")
            errors += 1
            continue
        dest_dir = args.data_dir / category
        dest_dir.mkdir(parents=True, exist_ok=True)
        dest = dest_dir / src.name
        if dest.exists() and dest.samefile(src):
            skipped += 1
            continue
        try:
            if args.symlink:
                if dest.exists():
                    dest.unlink()
                dest.symlink_to(src.resolve())
            else:
                shutil.copy2(src, dest)
            copied += 1
            print(f"  {category}/ {src.name}")
        except OSError as e:
            print(f"  Error: {e}")
            errors += 1

    print(f"\nCopied/symlinked: {copied}, skipped (same file): {skipped}, errors: {errors}")


if __name__ == "__main__":
    main()
