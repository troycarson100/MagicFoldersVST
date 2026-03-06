"""
Shared constants for the Magic Folders instrument classifier.
Must match the C++ side (ModelRunner + mel spectrogram) so that
training preprocessing and plugin inference use the same input shape.
"""

# Audio
SAMPLE_RATE = 22050
DURATION_SEC = 2.0
NUM_SAMPLES = int(SAMPLE_RATE * DURATION_SEC)  # 44100

# Mel spectrogram (match C++ MelSpectrogram)
N_FFT = 2048
HOP_LENGTH = 512
N_MELS = 128
# Frames for NUM_SAMPLES: (NUM_SAMPLES + HOP_LENGTH - 1) // HOP_LENGTH
N_FRAMES = (NUM_SAMPLES + HOP_LENGTH - 1) // HOP_LENGTH  # 87

# Classes in same order as Detection::Class in ModelRunner.h
CLASS_NAMES = [
    "Kick",
    "Snare",
    "HiHat",
    "Perc",
    "Bass",
    "Guitar",
    "Keys",
    "Pad",
    "Lead",
    "FX",
    "TextureAtmos",
    "Vocal",
    "Other",
]
NUM_CLASSES = len(CLASS_NAMES)
