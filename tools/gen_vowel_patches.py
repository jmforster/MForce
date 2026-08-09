"""Generate vowel-morph patches and render them.

Updated 2026-05-30:
  - Decomposed AdditiveSource + FullPartials + wire (no consolidated form).
  - formantWeight = 1.0 (full bandpass; legacy crossfade math requires this
    to produce vowel character).
  - rolloff = 0 (Matt: keep the buzz, just formant-shape it).
  - Widths bumped to 250/400/600/900 (60-280 was too narrow vs partial
    spacing at 130 Hz fundamental).
  - Instrument block + score + output->fas so the patches play in both
    UI keyboard mode and CLI render mode.
"""
import json
import subprocess
import sys
from pathlib import Path

REPO = Path("C:/@dev/repos/mforce")
CLI = REPO / "build/tools/mforce_cli/Release/mforce_cli.exe"
PATCHES_DIR = REPO / "patches/vowels"
RENDERS_BASE = REPO / "renders/explore"

# vowel_key -> (F1, F2, F3)
VOWELS = {
    "L":      (380, 1000, 2700),  # lateral
    "Ieye":   (500, 1700, 2500),  # I = EYE diphthong midpoint
    "Auh":    (500, 1500, 2500),  # A schwa (LIAR)
    "R":      (430, 1200, 1600),  # rhotic
    "OO":     (300,  870, 2240),  # boot
    "EE":     (270, 2290, 3010),  # heat
    "Ogoat":  (430,  870, 2400),  # goat midpoint
    "Ahat":   (660, 1720, 2410),  # hat
    "AH":     (730, 1090, 2440),  # hot
    "Acake":  (450, 2000, 2500),  # cake midpoint
}

GAINS = [1.0, 0.7, 0.5]  # F1, F2, F3 peak gains
POWER = 2.0

PROGRESSIONS = {
    "LIAR":       ["L", "Ieye", "Auh", "R"],
    "OOEE":       ["OO", "EE"],
    "EEOO":       ["EE", "OO"],
    "OOEEOO":     ["OO", "EE", "OO"],
    "OEEAhat":    ["Ogoat", "EE", "Ahat"],
    "AHEEGOO":    ["AH", "EE", "Ogoat", "OO"],
    "AcakeEEO":   ["Acake", "EE", "Ogoat"],
    "AcakeEEOO":  ["Acake", "EE", "OO"],
}

# Wider than before — at 130Hz fundamental, partial spacing is 130Hz,
# so a band needs to be at least ~200Hz wide to catch >1 partial.
WIDTHS = [250, 400, 600, 900]

# Locked per Matt's instruction:
ROLLOFF = 0
FORMANT_WEIGHT = 1.0
SECONDS = 4.0
FUNDAMENTAL = 130.0


def make_patch(prog_name: str, vowel_keys: list, width: int) -> dict:
    nodes = []

    # Envelopes
    nodes.append({"id": "ampEnv", "type": "Envelope",
                  "params": {"preset": "adsr", "attack": 0.05, "decay": 0.05,
                             "sustainLevel": 0.8, "release": 0.5}})
    nodes.append({"id": "blendEnv", "type": "Envelope",
                  "params": {"preset": "ar", "attack": 0.0, "attackMax": 0.0}})

    # Per-vowel formants + spectra
    spec_refs = []
    for occ_idx, vk in enumerate(vowel_keys):
        f1, f2, f3 = VOWELS[vk]
        formant_refs = []
        for fi, freq in enumerate([f1, f2, f3]):
            fid = f"f_{prog_name}_{occ_idx}_{vk}_{fi+1}"
            nodes.append({
                "id": fid, "type": "Formant",
                "params": {
                    "frequency": float(freq),
                    "gain": GAINS[fi],
                    "width": float(width),
                    "power": POWER,
                },
            })
            formant_refs.append({"ref": fid})
        sid = f"spec_{prog_name}_{occ_idx}_{vk}"
        nodes.append({
            "id": sid, "type": "FormantSpectrum",
            "params": {"formants": formant_refs},
        })
        spec_refs.append({"ref": sid})

    # FormantSequence (morphs across spectra by blendEnv)
    nodes.append({
        "id": "fseq", "type": "FormantSequence",
        "params": {
            "formants": spec_refs,
            "blend": {"ref": "blendEnv"},
        },
    })

    # Decomposed: separate FullPartials node...
    nodes.append({
        "id": "fas_partials", "type": "FullPartials",
        "params": {
            "maxPartials": 60,
            "minMult": 1,
            "evenWeight1": 1.0, "evenWeight2": 1.0,
            "oddWeight1": 1.0, "oddWeight2": 1.0,
            "rolloff1": float(ROLLOFF),
            "rolloff2": float(ROLLOFF),
        },
    })
    # ...wired into AdditiveSource via partials ref.
    nodes.append({
        "id": "fas", "type": "AdditiveSource",
        "params": {
            "seed": 42,
            "frequency": FUNDAMENTAL,
            "amplitude": {"ref": "ampEnv"},
            "partials": {"ref": "fas_partials"},
            "formant": {"ref": "fseq"},
            "formantWeight": FORMANT_WEIGHT,
        },
    })

    return {
        "sampleRate": 48000,
        "seconds": SECONDS,
        "graph": {"nodes": nodes, "output": "fas"},
        "instrument": {
            "paramMap": {"frequency": "fas.frequency"},
            "polyphony": 1,
        },
        "score": [
            {"time": 0.0, "note": 48, "velocity": 0.8,
             "duration": SECONDS - 0.5},
        ],
    }


def main():
    do_render = "--no-render" not in sys.argv

    PATCHES_DIR.mkdir(parents=True, exist_ok=True)
    built = 0
    rendered = 0
    failures = []

    for prog_name, vowel_keys in PROGRESSIONS.items():
        out_render_dir = RENDERS_BASE / f"vowels-{prog_name}"
        out_render_dir.mkdir(parents=True, exist_ok=True)

        for width in WIDTHS:
            base = f"{prog_name}_w{width}"
            patch_path = PATCHES_DIR / f"{base}.json"
            wav_path = out_render_dir / f"{base}.wav"
            patch_copy_path = out_render_dir / f"{base}.patch.json"

            patch = make_patch(prog_name, vowel_keys, width)
            with open(patch_path, "w") as f:
                json.dump(patch, f, indent=2)
            # Companion patch.json next to WAV so audition window can save.
            with open(patch_copy_path, "w") as f:
                json.dump(patch, f, indent=2)
            built += 1

            if do_render:
                res = subprocess.run(
                    [str(CLI), str(patch_path), str(wav_path)],
                    capture_output=True, text=True, cwd=str(REPO),
                )
                if res.returncode != 0 or not wav_path.exists():
                    failures.append((base, res.returncode, res.stderr[-500:]))
                else:
                    rendered += 1

    print(f"Built {built} patches.")
    print(f"Rendered {rendered} WAVs.")
    if failures:
        print(f"FAILURES ({len(failures)}):")
        for name, rc, err in failures[:10]:
            print(f"  {name}: rc={rc}\n    stderr: {err}")


if __name__ == "__main__":
    main()
