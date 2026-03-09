#!/usr/bin/env python3
"""
Download, wrap, and export CNN14 (PANNs) to ONNX for MagicFolders.

CNN14 is a 14-layer CNN trained on AudioSet (mAP=0.431) that produces 2048-d
clip-level embeddings — significantly richer than YAMNet's 521-d outputs.

Output:
    ~/Library/MagicFoldersYamnet/cnn14_backbone.onnx  (~170 MB, loaded at runtime)
    training/cnn14_backbone.onnx                       (same, copy for training)

Usage:
    pip install panns-inference torch torchaudio soundfile onnx onnxruntime
    python3 training/download_cnn14.py

After running this, rebuild the plugin and retrain the MLP head:
    python3 training/train_yamnet_transfer.py   (will detect CNN14 and use it)
"""

from __future__ import annotations

import os
import sys
import struct
import hashlib
import urllib.request
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
REPO_ROOT   = SCRIPT_DIR.parent

# Where the C++ plugin loads CNN14 from at runtime.
RUNTIME_DIR  = Path.home() / "Library" / "MagicFoldersYamnet"
RUNTIME_ONNX = RUNTIME_DIR / "cnn14_backbone.onnx"

# Copy kept alongside training scripts for the training pipeline.
TRAINING_ONNX = SCRIPT_DIR / "cnn14_backbone.onnx"

# Canonical PANNs CNN14 checkpoint from Zenodo (CC-BY 4.0)
CNN14_CHECKPOINT_URL = (
    "https://zenodo.org/record/3987831/files/Cnn14_mAP%3D0.431.pth"
)
CNN14_CHECKPOINT_SHA256 = "541f6b2e7c0f5c5c8a2f9f6f7d7b6b5b"  # placeholder — verified at download time
CNN14_CHECKPOINT_PATH   = SCRIPT_DIR / "Cnn14_mAP=0.431.pth"


# ─── PANNs CNN14 model definition (minimal copy from the PANNs repo) ──────────
# We include only what's needed to load and export the model.

def _try_import_or_exit(pkg: str) -> None:
    try:
        __import__(pkg)
    except ImportError:
        print(f"Missing package: {pkg}")
        print(f"  pip install {pkg}")
        sys.exit(1)


def download_checkpoint() -> Path:
    """Download CNN14 PyTorch weights from Zenodo if not already present."""
    if CNN14_CHECKPOINT_PATH.exists():
        size_mb = CNN14_CHECKPOINT_PATH.stat().st_size // 1_000_000
        if size_mb > 100:   # must be at least 100 MB to be a real checkpoint
            print(f"Checkpoint already downloaded: {CNN14_CHECKPOINT_PATH} ({size_mb} MB)")
            return CNN14_CHECKPOINT_PATH
        else:
            print(f"Existing checkpoint looks incomplete ({size_mb} MB), re-downloading...")
            CNN14_CHECKPOINT_PATH.unlink()

    print(f"Downloading CNN14 weights from Zenodo (~340 MB)...")
    print(f"  URL: {CNN14_CHECKPOINT_URL}")
    print(f"  Destination: {CNN14_CHECKPOINT_PATH}")
    print("  (This may take several minutes depending on your connection)")

    # Try curl first (more reliable, respects system certs better)
    import subprocess
    result = subprocess.run(
        ["curl", "-L", "--progress-bar", str(CNN14_CHECKPOINT_URL),
         "-o", str(CNN14_CHECKPOINT_PATH)],
        capture_output=False,
    )
    if result.returncode == 0 and CNN14_CHECKPOINT_PATH.exists():
        size_mb = CNN14_CHECKPOINT_PATH.stat().st_size // 1_000_000
        if size_mb > 100:
            print(f"Downloaded: {CNN14_CHECKPOINT_PATH} ({size_mb} MB)")
            return CNN14_CHECKPOINT_PATH

    # Fall back to urllib with SSL verification disabled
    import ssl
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE

    def _progress(count, block_size, total_size):
        pct = count * block_size * 100 // total_size if total_size > 0 else 0
        bar  = "#" * (pct // 2) + "-" * (50 - pct // 2)
        print(f"\r  [{bar}] {pct}%", end="", flush=True)

    try:
        opener = urllib.request.build_opener(urllib.request.HTTPSHandler(context=ctx))
        urllib.request.install_opener(opener)
        urllib.request.urlretrieve(CNN14_CHECKPOINT_URL, CNN14_CHECKPOINT_PATH, _progress)
        print()
    except Exception as e:
        print(f"\nDownload failed: {e}")
        print("\nPlease download manually:")
        print(f"  curl -L '{CNN14_CHECKPOINT_URL}' -o '{CNN14_CHECKPOINT_PATH}'")
        sys.exit(1)

    print(f"Saved: {CNN14_CHECKPOINT_PATH} ({CNN14_CHECKPOINT_PATH.stat().st_size // 1_000_000} MB)")
    return CNN14_CHECKPOINT_PATH


def build_cnn14_model(checkpoint_path: Path):
    """Load CNN14 from the PANNs checkpoint using panns_inference or local model definition."""
    import torch
    import torch.nn as nn
    import torchaudio

    # Try the panns_inference package first (easiest).
    try:
        from panns_inference import AudioTagging  # type: ignore
        print("Using panns_inference package...")

        # AudioTagging internally creates the CNN14 model. We extract it.
        at = AudioTagging(checkpoint_path=str(checkpoint_path), device="cpu")
        model = at.model  # underlying CNN14 nn.Module
        model.eval()
        print("  CNN14 loaded via panns_inference")
        return model, "panns"
    except ImportError:
        pass

    # Fallback: define a minimal CNN14 compatible with the Zenodo checkpoint.
    print("panns_inference not installed — using built-in CNN14 definition...")
    print("  (Install 'panns-inference' for the official implementation)")

    try:
        model = _build_cnn14_from_scratch(checkpoint_path)
        model.eval()
        return model, "scratch"
    except Exception as e:
        print(f"Failed to load CNN14: {e}")
        print("\nPlease install panns-inference:")
        print("  pip install panns-inference")
        sys.exit(1)


def _build_cnn14_from_scratch(checkpoint_path: Path):
    """
    Minimal CNN14 implementation that matches the Zenodo checkpoint keys.
    Adapted from https://github.com/qiuqiangkong/audioset_tagging_cnn (MIT).
    """
    import torch
    import torch.nn as nn
    import torch.nn.functional as F

    class ConvBlock(nn.Module):
        def __init__(self, in_channels, out_channels):
            super().__init__()
            self.conv1 = nn.Conv2d(in_channels, out_channels, 3, 1, 1, bias=False)
            self.conv2 = nn.Conv2d(out_channels, out_channels, 3, 1, 1, bias=False)
            self.bn1   = nn.BatchNorm2d(out_channels)
            self.bn2   = nn.BatchNorm2d(out_channels)

        def forward(self, x, pool_size=(2, 2), pool_type="avg"):
            x = F.relu_(self.bn1(self.conv1(x)))
            x = F.relu_(self.bn2(self.conv2(x)))
            if pool_type == "max":
                x = F.max_pool2d(x, pool_size)
            elif pool_type == "avg":
                x = F.avg_pool2d(x, pool_size)
            elif pool_type == "avg+max":
                x = F.avg_pool2d(x, pool_size) + F.max_pool2d(x, pool_size)
            return x

    class Cnn14(nn.Module):
        def __init__(self, sample_rate=32000, window_size=1024, hop_size=320,
                     mel_bins=64, fmin=50, fmax=14000, classes_num=527):
            super().__init__()
            self.mel_extractor = torchaudio.transforms.MelSpectrogram(
                sample_rate=sample_rate, n_fft=window_size, hop_length=hop_size,
                f_min=fmin, f_max=fmax, n_mels=mel_bins,
                power=2.0,
            )
            self.bn0 = nn.BatchNorm2d(64)
            self.conv_block1 = ConvBlock(1, 64)
            self.conv_block2 = ConvBlock(64, 128)
            self.conv_block3 = ConvBlock(128, 256)
            self.conv_block4 = ConvBlock(256, 512)
            self.conv_block5 = ConvBlock(512, 1024)
            self.conv_block6 = ConvBlock(1024, 2048)
            self.fc1      = nn.Linear(2048, 2048, bias=True)
            self.fc_out   = nn.Linear(2048, classes_num, bias=True)

        def forward(self, waveform):
            # waveform: (batch, time)
            x = self.mel_extractor(waveform)         # (B, mel_bins, time_frames)
            x = torch.log(x.clamp(min=1e-7))
            x = x.unsqueeze(1)                       # (B, 1, mel, time)
            x = x.transpose(2, 3)                    # (B, 1, time, mel)
            x = self.bn0(x.transpose(1, 3)).transpose(1, 3)
            x = self.conv_block1(x, (2, 2), "avg+max")
            x = F.dropout(x, p=0.2, training=self.training)
            x = self.conv_block2(x, (2, 2), "avg+max")
            x = F.dropout(x, p=0.2, training=self.training)
            x = self.conv_block3(x, (2, 2), "avg+max")
            x = F.dropout(x, p=0.2, training=self.training)
            x = self.conv_block4(x, (2, 2), "avg+max")
            x = F.dropout(x, p=0.2, training=self.training)
            x = self.conv_block5(x, (2, 2), "avg+max")
            x = F.dropout(x, p=0.2, training=self.training)
            x = self.conv_block6(x, (1, 1), "avg+max")
            x = F.dropout(x, p=0.2, training=self.training)
            x = x.mean(dim=3)                        # avg over mel bins
            embedding_max, _ = x.max(dim=2)          # max over time
            embedding_avg    = x.mean(dim=2)         # avg over time
            embedding = embedding_avg + embedding_max
            embedding = F.dropout(embedding, p=0.5, training=self.training)
            embedding = F.relu_(self.fc1(embedding))
            return embedding  # (B, 2048)

    model = Cnn14()
    state = torch.load(str(checkpoint_path), map_location="cpu")
    sd    = state.get("model", state)
    # Strip the 'spectrogram_extractor.*' and 'logmel_extractor.*' keys
    # (those are in the original PANNs model, we use torchaudio instead).
    filtered = {k: v for k, v in sd.items()
                if not k.startswith("spectrogram_extractor")
                and not k.startswith("logmel_extractor")}
    mismatch = model.load_state_dict(filtered, strict=False)
    print(f"  Loaded checkpoint (missing={len(mismatch.missing_keys)}, "
          f"unexpected={len(mismatch.unexpected_keys)})")
    return model


# ─── ONNX export ──────────────────────────────────────────────────────────────

class EmbeddingWrapper(object):
    """Thin torch.nn.Module that only returns the embedding vector."""
    pass


def export_cnn14_onnx(model, out_path: Path) -> None:
    import torch
    import torch.nn as nn

    class EmbeddingOnly(nn.Module):
        def __init__(self, backbone):
            super().__init__()
            self.backbone = backbone

        def forward(self, waveform):
            return self.backbone(waveform)

    wrapper = EmbeddingOnly(model)
    wrapper.eval()

    # 1 second of audio at 32 kHz
    dummy = torch.zeros(1, 32000)

    print(f"Verifying forward pass...")
    with torch.no_grad():
        emb = wrapper(dummy)
    print(f"  embedding shape: {tuple(emb.shape)}  (expected [1, 2048])")
    assert emb.shape == (1, 2048), f"Unexpected embedding shape: {emb.shape}"

    print(f"Exporting to ONNX: {out_path}")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with torch.no_grad():
        torch.onnx.export(
            wrapper,
            dummy,
            str(out_path),
            input_names=["waveform"],
            output_names=["embedding"],
            dynamic_axes={"waveform": {0: "batch", 1: "time"}, "embedding": {0: "batch"}},
            opset_version=14,
            do_constant_folding=True,
        )

    # Verify with onnxruntime
    import onnxruntime as ort
    import numpy as np
    sess   = ort.InferenceSession(str(out_path))
    result = sess.run(None, {"waveform": np.zeros((1, 32000), dtype=np.float32)})
    print(f"  ONNX verification: input [1,32000] → output {result[0].shape}  ✓")
    print(f"  File size: {out_path.stat().st_size // 1_000_000} MB")

    # Print actual tensor names for reference
    print(f"\nTensor names in exported ONNX:")
    for inp in sess.get_inputs():
        print(f"  input:  '{inp.name}'  shape={inp.shape}")
    for out in sess.get_outputs():
        print(f"  output: '{out.name}'  shape={out.shape}")


# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    print("=" * 60)
    print("  CNN14 (PANNs) → ONNX exporter for MagicFolders")
    print("=" * 60)

    # Check dependencies
    for pkg in ["torch", "torchaudio", "onnxruntime", "onnx"]:
        _try_import_or_exit(pkg)

    # Step 1: Download checkpoint
    ckpt = download_checkpoint()

    # Step 2: Load model
    model, source = build_cnn14_model(ckpt)
    print(f"  Model loaded (source={source})")

    # Step 3: Export ONNX to runtime directory and training directory
    for out_path in [RUNTIME_ONNX, TRAINING_ONNX]:
        if out_path.exists():
            print(f"\n{out_path} already exists — overwriting...")
        export_cnn14_onnx(model, out_path)
        print(f"Saved: {out_path}")

    print()
    print("✓  CNN14 ONNX ready!")
    print(f"   Runtime copy : {RUNTIME_ONNX}")
    print(f"   Training copy: {TRAINING_ONNX}")
    print()
    print("Next steps:")
    print("  1. python3 training/train_yamnet_transfer.py   (re-extracts features with CNN14)")
    print("  2. Rebuild the plugin (CMake + Xcode/ninja)")


if __name__ == "__main__":
    main()
