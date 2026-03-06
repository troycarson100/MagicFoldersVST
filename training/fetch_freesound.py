#!/usr/bin/env python3
"""
Fetch CC0 samples from Freesound.org into ~/Documents/MagicFoldersTraining/<Class>/.

Loops are downloaded into <Class>/loops/ and one-shots into <Class>/oneshots/ so
the training script can walk recursively and pick up both.

Get a FREE API key at https://freesound.org/apiv2/apply/ (takes ~1 minute).

Usage:
    # Download samples for ALL classes:
    FREESOUND_API_KEY=your_key python3 training/fetch_freesound.py

    # Download 500 samples for specific classes only:
    FREESOUND_API_KEY=your_key python3 training/fetch_freesound.py --classes HiHat Kick --per-class 500

    # Include CC-BY sounds (more results, but requires attribution):
    FREESOUND_API_KEY=your_key python3 training/fetch_freesound.py --classes Lead HiHat --cc-by

    # Save key to training/.env so you don't have to export it every time:
    echo "FREESOUND_API_KEY=your_key" > training/.env
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import sys
import time
from pathlib import Path

try:
    import requests
except ImportError:
    print("Install requests: pip install requests", file=sys.stderr)
    sys.exit(1)

try:
    from dotenv import load_dotenv
except ImportError:
    load_dotenv = None

# ─── Class → queries ──────────────────────────────────────────────────────────
# Each class has two lists: one-shot queries and loop queries.
# Loops are stored in <Class>/loops/, one-shots in <Class>/oneshots/.
# Max duration is applied per sub-type (loops can be longer).
#
# One-shot max: 4.0 s   Loop max: 32.0 s   Loop min: 1.5 s

CLASS_CONFIG: dict[str, dict] = {
    "Kick": {
        "oneshot_queries": [
            "kick drum one shot",
            "bass drum hit sample",
            "kick drum hit",
            "808 kick one shot",
            "deep kick drum",
            "acoustic kick drum hit",
            "trap kick drum",
            "punchy kick sample",
        ],
        "loop_queries": [
            "kick drum loop",
            "kick loop beat",
            "bass drum loop beat",
            "kick drum pattern loop",
            "four on floor kick loop",
            "trap kick loop",
            "hip hop kick loop",
        ],
    },
    "Snare": {
        "oneshot_queries": [
            "snare drum hit one shot",
            "snare drum sample",
            "clap drum hit",
            "snare crack hit",
            "acoustic snare hit",
            "snare rimshot hit",
            "trap snare hit",
        ],
        "loop_queries": [
            "snare drum loop",
            "snare roll loop",
            "snare pattern loop",
            "snare clap loop beat",
            "trap snare loop",
            "hip hop snare loop",
            "clap loop beat",
        ],
    },
    "HiHat": {
        "oneshot_queries": [
            "closed hi hat one shot",
            "open hi hat hit",
            "cymbal hi hat",
            "closed hihat sample",
            "hi hat tick",
            "hi hat chick",
        ],
        "loop_queries": [
            "hi hat loop",
            "hihat loop beat",
            "hi-hat rhythm loop",
            "closed hi hat loop beat",
            "open hi hat loop",
            "cymbal loop pattern",
            "trap hi hat loop",
            "808 hihat loop",
            "hi hat pattern loop",
            "electronic hihat loop",
            "drum machine hi hat loop",
            "noisy hi hat loop",
        ],
    },
    "Perc": {
        "oneshot_queries": [
            "percussion one shot",
            "rim shot drum hit",
            "tom drum hit",
            "conga bongo hit",
            "shaker hit",
            "tambourine hit",
            "cowbell hit",
            "woodblock hit",
        ],
        "loop_queries": [
            "percussion loop",
            "latin percussion loop",
            "conga loop",
            "shaker loop beat",
            "tambourine loop",
            "percussion rhythm loop",
            "afro percussion loop",
            "drum perc loop",
        ],
    },
    "Bass": {
        "oneshot_queries": [
            "bass guitar one shot",
            "synth bass note",
            "808 bass note hit",
            "sub bass hit",
            "electric bass note",
            "bass pluck note",
        ],
        "loop_queries": [
            "bass guitar loop",
            "synth bass loop",
            "808 bass loop",
            "bass line loop",
            "funk bass loop",
            "bass riff loop",
            "sub bass loop",
        ],
    },
    "Guitar": {
        "oneshot_queries": [
            "electric guitar strum one shot",
            "acoustic guitar strum sample",
            "guitar chord hit",
            "guitar pluck note",
            "guitar note one shot",
        ],
        "loop_queries": [
            "electric guitar loop",
            "guitar riff loop",
            "acoustic guitar loop",
            "guitar chord loop",
            "guitar strum loop",
            "funk guitar loop",
            "rhythm guitar loop",
        ],
    },
    "Keys": {
        "oneshot_queries": [
            "piano note one shot",
            "electric piano note",
            "rhodes piano hit",
            "keyboard note sample",
            "piano key hit",
            "synth piano note",
        ],
        "loop_queries": [
            "piano loop",
            "electric piano loop",
            "rhodes loop",
            "keyboard loop",
            "piano melody loop",
            "keys loop music",
        ],
    },
    "Pad": {
        "oneshot_queries": [
            "synth pad sound",
            "ambient pad synthesizer",
            "string pad sample",
            "atmospheric pad hit",
            "soft pad tone",
        ],
        "loop_queries": [
            "synth pad loop",
            "ambient pad loop",
            "atmospheric pad loop",
            "string pad loop",
            "evolving pad loop",
            "ambient drone loop",
        ],
    },
    "Lead": {
        "oneshot_queries": [
            "synth lead one shot",
            "sawtooth lead synth note",
            "supersaw synth note",
            "reese bass synth",
            "lead synthesizer note",
            "analog synth lead note",
            "wavetable synth lead",
        ],
        "loop_queries": [
            "synth lead loop",
            "lead melody loop",
            "arp synth loop",
            "supersaw lead loop",
            "electronic lead loop",
            "synth melody loop",
        ],
    },
    "FX": {
        "oneshot_queries": [
            "sound effect riser",
            "synth sweep fx",
            "impact sound effect",
            "transition fx hit",
            "noise sweep one shot",
            "whoosh sound effect",
            "downlifter fx",
        ],
        "loop_queries": [
            "fx loop",
            "riser loop",
            "sweep loop",
            "noise loop fx",
            "atmospheric fx loop",
            "glitch fx loop",
        ],
    },
    "TextureAtmos": {
        "oneshot_queries": [
            "ambient texture sound",
            "atmospheric drone sound",
            "ambient soundscape",
            "nature texture ambient",
            "wind ambient texture",
        ],
        "loop_queries": [
            "ambient texture loop",
            "atmospheric drone loop",
            "ambient soundscape loop",
            "nature ambient loop",
            "rain ambient loop",
            "wind texture loop",
            "dark ambient loop",
        ],
    },
    "Vocal": {
        "oneshot_queries": [
            "vocal one shot",
            "acapella voice sample",
            "vocal chop hit",
            "choir vocal hit",
            "singing voice sample",
            "voice sample music",
        ],
        "loop_queries": [
            "vocal loop",
            "acapella loop",
            "choir loop",
            "vocal melody loop",
            "vocal adlib loop",
        ],
    },
}

# ─── Paths ────────────────────────────────────────────────────────────────────
TRAINING_DATA_DIR = Path.home() / "Documents" / "MagicFoldersTraining"
SCRIPT_DIR        = Path(__file__).resolve().parent
BASE_URL          = "https://freesound.org/apiv2"
SEARCH_URL        = f"{BASE_URL}/search/text/"
PAGE_SIZE         = 150

ONESHOT_MAX_DURATION = 4.0
LOOP_MIN_DURATION    = 1.5
LOOP_MAX_DURATION    = 32.0


def slug(s: str, max_len: int = 60) -> str:
    s = re.sub(r"[^\w\s-]", "", s)
    s = re.sub(r"[-\s]+", "_", s).strip("_")
    return s[:max_len] if s else "sound"


def get_api_key() -> str:
    if load_dotenv is not None:
        load_dotenv(SCRIPT_DIR / ".env")
    return os.environ.get("FREESOUND_API_KEY", "").strip()


def search_freesound(api_key: str, query: str, license_filter: str,
                     page: int = 1, min_dur: float = 0.0, max_dur: float = 999.0) -> dict:
    params = {
        "query": query,
        "filter": f'{license_filter} duration:[{min_dur} TO {max_dur}]',
        "page": page,
        "page_size": PAGE_SIZE,
        "fields": "id,name,license,previews,url,duration",
        "sort": "score",
    }
    headers = {"Authorization": f"Token {api_key}"}
    r = requests.get(SEARCH_URL, params=params, headers=headers, timeout=30)
    r.raise_for_status()
    return r.json()


def download_file(url: str, path: Path) -> bool:
    try:
        r = requests.get(url, timeout=60, stream=True)
        r.raise_for_status()
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "wb") as f:
            for chunk in r.iter_content(chunk_size=65536):
                f.write(chunk)
        return True
    except Exception as e:
        print(f"  Download failed: {e}", file=sys.stderr)
        return False


def fetch_queries(queries: list[str], api_key: str, target: int,
                  license_filter: str, out_dir: Path, csv_writer,
                  min_dur: float, max_dur: float, label: str) -> int:
    """Download up to `target` sounds for one query list. Returns count downloaded."""
    seen_ids: set[int] = set()
    existing = set(f.stem for f in out_dir.glob("freesound_*") if f.is_file())
    downloaded = len(existing)

    if downloaded >= target:
        print(f"    {label}: already has {downloaded} files, skipping.")
        return downloaded

    still_needed = target - downloaded
    print(f"    {label}: {downloaded} existing → need {still_needed} more")

    fetched_this_run = 0
    for query in queries:
        if fetched_this_run >= still_needed:
            break
        page = 1
        while fetched_this_run < still_needed:
            try:
                data = search_freesound(api_key, query, license_filter, page,
                                        min_dur=min_dur, max_dur=max_dur)
            except requests.RequestException as e:
                print(f"  Search error ({query}): {e}", file=sys.stderr)
                break

            results = data.get("results") or []
            if not results:
                break

            for s in results:
                if fetched_this_run >= still_needed:
                    break
                sid = s.get("id")
                if not sid or sid in seen_ids:
                    continue
                seen_ids.add(sid)

                previews = s.get("previews") or {}
                preview_url = (previews.get("preview-hq-mp3")
                               or previews.get("preview-lq-mp3"))
                if not preview_url:
                    continue

                name     = (s.get("name") or "sound").strip()
                lic_name = s.get("license") or "Unknown"
                safe     = slug(name)
                filename = f"freesound_{sid}_{safe}.mp3"
                dest     = out_dir / filename

                if dest.exists() or f"freesound_{sid}" in "".join(existing):
                    continue

                if download_file(preview_url, dest):
                    csv_writer.writerow([
                        str(dest), label, str(sid),
                        s.get("url", ""), lic_name,
                    ])
                    fetched_this_run += 1
                    total_now = downloaded + fetched_this_run
                    print(f"      [{total_now}/{target}] {filename}")
                time.sleep(0.25)

            if not data.get("next"):
                break
            page += 1
            time.sleep(0.3)

    return downloaded + fetched_this_run


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Download CC0 audio samples from Freesound into MagicFoldersTraining/."
    )
    parser.add_argument(
        "--classes", nargs="+", default=list(CLASS_CONFIG.keys()),
        metavar="CLASS",
        help="Classes to download (default: all).",
    )
    parser.add_argument(
        "--per-class", type=int, default=400,
        help="Target number of one-shots per class (default 400)",
    )
    parser.add_argument(
        "--loops-per-class", type=int, default=300,
        help="Target number of loops per class (default 300). Stored in <Class>/loops/.",
    )
    parser.add_argument(
        "--cc-by", action="store_true",
        help="Also include CC-BY licensed sounds (more results, requires attribution)",
    )
    parser.add_argument(
        "--out-dir", type=Path, default=TRAINING_DATA_DIR,
        help=f"Root output directory (default: {TRAINING_DATA_DIR})",
    )
    args = parser.parse_args()

    api_key = get_api_key()
    if not api_key:
        print(
            "\nERROR: No Freesound API key found.\n"
            "  1. Go to https://freesound.org/apiv2/apply/ (free, ~1 min)\n"
            "  2. Run: export FREESOUND_API_KEY=your_key\n"
            "     OR:  echo 'FREESOUND_API_KEY=your_key' > training/.env\n",
            file=sys.stderr,
        )
        sys.exit(1)

    if args.cc_by:
        license_filter = 'license:("Creative Commons 0" OR "Attribution")'
    else:
        license_filter = 'license:"Creative Commons 0"'

    csv_path = SCRIPT_DIR / "data_sources.csv"
    write_header = not csv_path.exists()

    print(f"Output directory  : {args.out_dir}")
    print(f"License filter    : {'CC0 + CC-BY' if args.cc_by else 'CC0 only'}")
    print(f"One-shots target  : {args.per_class} per class")
    print(f"Loops target      : {args.loops_per_class} per class")
    print(f"Classes           : {', '.join(args.classes)}\n")

    args.out_dir.mkdir(parents=True, exist_ok=True)

    with open(csv_path, "a", newline="", encoding="utf-8") as csvfile:
        writer = csv.writer(csvfile)
        if write_header:
            writer.writerow(["file_path", "class", "source_id", "url", "license"])

        total = 0
        for cls in args.classes:
            if cls not in CLASS_CONFIG:
                print(f"WARNING: unknown class '{cls}', skipping.")
                continue

            cfg = CLASS_CONFIG[cls]
            print(f"\n── {cls} ───────────────────────────────────────")

            # One-shots → <Class>/ (root, for backwards compat with existing files)
            oneshot_dir = args.out_dir / cls
            oneshot_dir.mkdir(parents=True, exist_ok=True)
            n_shots = fetch_queries(
                cfg["oneshot_queries"], api_key, args.per_class,
                license_filter, oneshot_dir, writer,
                min_dur=0.05, max_dur=ONESHOT_MAX_DURATION,
                label=f"{cls}/oneshots",
            )

            # Loops → <Class>/loops/
            loop_dir = args.out_dir / cls / "loops"
            loop_dir.mkdir(parents=True, exist_ok=True)
            n_loops = fetch_queries(
                cfg["loop_queries"], api_key, args.loops_per_class,
                license_filter, loop_dir, writer,
                min_dur=LOOP_MIN_DURATION, max_dur=LOOP_MAX_DURATION,
                label=f"{cls}/loops",
            )

            print(f"  → {n_shots} one-shots + {n_loops} loops = {n_shots + n_loops} total for {cls}")
            total += n_shots + n_loops

    print(f"\nDone. Attribution log: {csv_path}")
    print(f"Total files: {total}")
    print("\nNext: retrain with:")
    print("  python3 training/train_yamnet_transfer.py")


if __name__ == "__main__":
    main()
