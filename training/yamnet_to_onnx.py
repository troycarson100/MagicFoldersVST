#!/usr/bin/env python3
"""
Download the pretrained YAMNet model and export it to ONNX so Magic Folders
can use it via ONNX Runtime.

This script is **only** for you as the developer – end users do NOT need
Python or TensorFlow. You run it once, then rebuild the plugin.

Outputs:
  - assets/Models/yamnet.onnx
  - assets/Models/yamnet_labels.txt

Usage (from the training/ folder):

  pip install tensorflow==2.16.1 tf2onnx==1.16.0 numpy
  python yamnet_to_onnx.py
"""

from __future__ import annotations

import os
from pathlib import Path

import numpy as np
import tensorflow as tf  # type: ignore
import tf2onnx  # type: ignore


def download_yamnet(saved_model_dir: Path, labels_path: Path) -> None:
    """Download YAMNet SavedModel + labels from TFHub."""
    if saved_model_dir.exists() and labels_path.exists():
        print(f"YAMNet already downloaded at {saved_model_dir}")
        return

    import tensorflow_hub as hub  # type: ignore

    print("Downloading YAMNet from TensorFlow Hub...")
    model_handle = "https://tfhub.dev/google/yamnet/1"
    yamnet = hub.load(model_handle)

    print(f"Saving SavedModel to {saved_model_dir} ...")
    tf.saved_model.save(yamnet, str(saved_model_dir))

    # YAMNet labels file from the TF Hub asset
    labels = yamnet.class_map_path().numpy().decode("utf-8")
    labels_txt = Path(labels)
    if labels_txt.is_file():
        labels_text = labels_txt.read_text(encoding="utf-8")
        labels_path.parent.mkdir(parents=True, exist_ok=True)
        labels_path.write_text(labels_text, encoding="utf-8")
        print(f"Saved labels to {labels_path}")
    else:
        print("WARNING: could not locate class_map_path.txt; labels will be missing.")


def export_to_onnx(saved_model_dir: Path, onnx_out: Path) -> None:
    """Export the YAMNet SavedModel to ONNX (waveform input -> logits)."""
    print("Loading SavedModel for export...")
    model = tf.saved_model.load(str(saved_model_dir))

    # YAMNet expects mono waveforms at 16 kHz, arbitrary length.
    # We'll export with a dynamic-length 1D input: [batch, num_samples].
    concrete = model.signatures["serving_default"]
    input_name = list(concrete.structured_input_signature[1].keys())[0]

    # Build a dummy input for tracing.
    dummy = tf.random.normal([1, 15600], dtype=tf.float32)

    print("Converting to ONNX (this can take a minute)...")
    spec = (tf.TensorSpec(dummy.shape, tf.float32, name=input_name),)
    output_path = str(onnx_out)
    onnx_model, _ = tf2onnx.convert.from_function(
        concrete,
        input_signature=spec,
        opset=18,
        output_path=output_path,
        dynamic_axes={input_name: {1: "num_samples"}},
    )
    print(f"Exported YAMNet ONNX to {onnx_out} (inputs: {onnx_model.graph.input[0].type})")


def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    saved_model_dir = repo_root / "training" / "yamnet_saved_model"
    labels_path = repo_root / "assets" / "Models" / "yamnet_labels.txt"
    onnx_out = repo_root / "assets" / "Models" / "yamnet.onnx"

    download_yamnet(saved_model_dir, labels_path)
    onnx_out.parent.mkdir(parents=True, exist_ok=True)
    export_to_onnx(saved_model_dir, onnx_out)

    print("\nDone.")
    print("Next steps:")
    print("  1) Rebuild the plugin so yamnet.onnx and yamnet_labels.txt are embedded.")
    print("  2) Enable the Yamnet-based detection mode in Magic Folders.")


if __name__ == "__main__":
    main()

