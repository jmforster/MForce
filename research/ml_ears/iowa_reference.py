"""Precompute the Iowa viola reference target for the CMA-ES scorer (stage a).

Writes out/iowa_reference.json holding, for the 4 open-string scoring notes
(C3/G3/D4/A4), the per-note harmonic envelope, broadband ratios, and attack
stats; plus motion medians pooled over derive_motion's wider sample set.

Run once (samples don't change); score_candidate.py loads the JSON so no eval
re-analyses the .aif files. Regenerate if the metric extractors change.
"""
import json
import os

import numpy as np
import soundfile as sf

import derive_motion as dm
import refmetrics as rm

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLE_DIR = os.path.join(HERE, "..", "inst_samples", "viola")
OUT = os.path.join(HERE, "out", "iowa_reference.json")

# Open-string scoring notes -> the Iowa file that plays them on that string.
SCORE_NOTES = [("C3", "sulC"), ("G3", "sulG"), ("D4", "sulD"), ("A4", "sulA")]

SUS_START, SUS_LEN = 0.55, 1.4     # sustain window (must match score_candidate)
ATTACK_LEN = 0.55                  # onset window for attack_stats


def find_file(string, note):
    for f in os.listdir(SAMPLE_DIR):
        if f".{string}." in f and f".{note}." in f and f.endswith(".aif"):
            return os.path.join(SAMPLE_DIR, f)
    raise FileNotFoundError(f"{string} {note}")


def load_mono(path):
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return x.astype(np.float64), sr


def main():
    notes = {}
    for note, string in SCORE_NOTES:
        x, sr = load_mono(find_file(string, note))
        midi = rm.note_to_midi(note)
        f0 = rm.midi_to_freq(midi)
        n0 = int(SUS_START * sr)
        sus = x[n0: n0 + int(SUS_LEN * sr)]
        atk = x[: int(ATTACK_LEN * sr)]
        notes[note] = {
            "midi": midi,
            "f0": round(f0, 3),
            "harm_env_db": [round(v, 3) for v in rm.harmonic_env(sus, sr, f0)],
            "broadband": [round(v, 6) for v in rm.broadband_ratios(sus, sr, f0)],
            "attack": [[round(a, 5), round(b, 5)]
                       for a, b in rm.attack_stats(atk, sr)],
        }
        m = rm.motion_stats(sus, sr, f0)
        print(f"{note:3s} f0={f0:6.1f}  broadband={notes[note]['broadband']}  "
              f"resid={m['resid_cents_rms']:.2f}c amp={100*m['amp_frac_rms']:.0f}%")

    # Motion medians pooled over derive_motion's broader 8-sample set (more
    # robust than 4 notes; motion is roughly note-invariant).
    rows = []
    for tag in dm.SAMPLES:
        f0_nom = dm.note_to_freq(tag.split(".")[1])
        try:
            rows.append(dm.analyze(dm.find_file(tag), f0_nom))
        except Exception as e:  # noqa: BLE001
            print(f"  motion {tag}: FAILED ({e})")
    motion_med = {k: float(np.median([r[k] for r in rows])) for k in rm.MOTION_KEYS}

    ref = {"notes": notes, "motion_medians": motion_med,
           "source": "Iowa MIS viola arco ff", "sus_start": SUS_START,
           "sus_len": SUS_LEN}
    json.dump(ref, open(OUT, "w"), indent=2)
    print("\nmotion medians:", {k: round(v, 3) for k, v in motion_med.items()})
    print("wrote", OUT)


if __name__ == "__main__":
    main()
