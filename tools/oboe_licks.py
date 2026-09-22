"""Trading-licks analysis (REVIEW 74.1, Matt 2026-09-21).

Matt hears the phrased oboe alternate between two characters, phrase by
phrase: nasal (the oboe target) and mellow (clarinet/flute-ish). His
evidence WAV: renders/dsp/pending/oboe_compare.wav = oboe_tongue +
OTJ_Phrased2.psg, transport octave 6.

This tool (a) segments a phrased render into phrases by breath gaps,
(b) measures per-phrase character discriminators, (c) reproduces the
render offline through mforce_cli with the same onset/hold stamping the
UI transport applies, so the effect can be probed without the UI.

Usage:
  python tools/oboe_licks.py analyze <wav>          # segment + measure
  python tools/oboe_licks.py render <tag> [opts]    # OTJ ph1-4, oct 6
"""
import json
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_stk_bowed import read_mono  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH = os.path.join(ROOT, "patches", "audition", "articulation1",
                     "oboe_tongue.json")
SCRATCH = os.path.join(ROOT, "renders", "scratch", "oboe_licks")

# OTJ_Phrased2.psg lines 1-4, transport octave 6 (house: C6 = midi 72).
# (midi, beats); phrase = one line = one breath.
D, E, FS, G, A = 74, 76, 78, 79, 81
PHRASES = [
    [(FS, 2), (G, 1), (A, 1), (A, 1), (G, 1), (FS, 1), (E, 1)],
    [(D, 1), (D, 1), (E, 1), (FS, 1), (FS, 1.5), (E, 0.5), (E, 2)],
    [(FS, 2), (G, 1), (A, 1), (A, 1), (G, 1), (FS, 1), (E, 1)],
    [(D, 1), (D, 1), (E, 1), (FS, 1), (E, 1.5), (D, 0.5), (D, 2)],
]
QUARTER = 0.5  # seconds per beat in the reproduction


def stamp(phrases):
    """The transport's auto rule (stamp_passage): first of phrase =
    breath; repeated pitch = tongue; pitch change = slur. hold = true on
    every note but the phrase's last."""
    out = []
    t = 0.0
    for ph in phrases:
        for i, (n, b) in enumerate(ph):
            onset = ("breath" if i == 0
                     else "tongue" if ph[i - 1][0] == n else "slur")
            out.append({"note": n, "time": round(t, 6),
                        "duration": round(b * QUARTER, 6),
                        "velocity": 0.8, "onset": onset,
                        "hold": i < len(ph) - 1})
            t += b * QUARTER
        t += QUARTER  # breath gap between phrases
    return out


def segment(x, sr, min_gap=0.15, floor_db=-45.0):
    """Split on sustained dips below floor (breath gaps)."""
    hop = int(0.01 * sr)
    env = np.array([np.sqrt(np.mean(x[i:i + hop] ** 2) + 1e-20)
                    for i in range(0, len(x) - hop, hop)])
    thr = env.max() * 10 ** (floor_db / 20.0)
    quiet = env < thr
    bounds, in_seg, start = [], False, 0
    gap = 0
    for i, q in enumerate(quiet):
        if not q:
            if not in_seg:
                in_seg, start = True, i
            gap = 0
        elif in_seg:
            gap += 1
            if gap * hop / sr > min_gap:
                bounds.append((start * hop / sr, (i - gap) * hop / sr))
                in_seg = False
    if in_seg:
        bounds.append((start * hop / sr, len(x) / sr))
    return [b for b in bounds if b[1] - b[0] > 0.5]


def character(x, sr, t0, t1):
    """Per-phrase discriminators, computed over the phrase's sustain."""
    s = x[int((t0 + 0.05) * sr):int(t1 * sr)]
    w = np.hanning(len(s))
    S = np.abs(np.fft.rfft(s * w)) ** 2
    f = np.fft.rfftfreq(len(s), 1.0 / sr)

    def band(lo, hi):
        return float(S[(f >= lo) & (f < hi)].sum())

    total = band(100, 12000) + 1e-20
    cent = float((f * S)[(f > 100) & (f < 12000)].sum()
                 / S[(f > 100) & (f < 12000)].sum())
    return {
        "centroid": cent,
        # oboe nasality lives in the 1-3.5k formant region
        "fmt_ratio": band(1000, 3500) / (band(200, 1000) + 1e-20),
        "hi_frac": band(3500, 12000) / total,
        "rms": float(np.sqrt(np.mean(s ** 2))),
    }


def analyze(path, label=""):
    x, sr = read_mono(path)
    segs = segment(x, sr)
    print("%s: %d phrases found" % (label or path, len(segs)))
    rows = []
    for i, (t0, t1) in enumerate(segs):
        c = character(x, sr, t0, t1)
        rows.append(c)
        print("  ph%-2d %6.2f-%6.2fs  centroid %6.0f Hz  fmt %5.2f  "
              "hi%% %4.1f  rms %.3f"
              % (i + 1, t0, t1, c["centroid"], c["fmt_ratio"],
                 100 * c["hi_frac"], c["rms"]))
    return rows


def render(tag, phrases=None, patch_mut=None):
    os.makedirs(SCRATCH, exist_ok=True)
    with open(PATCH) as fh:
        patch = json.load(fh)
    patch["score"] = stamp(phrases or PHRASES)
    patch["seconds"] = patch["score"][-1]["time"] \
        + patch["score"][-1]["duration"] + 2.0
    if patch_mut:
        patch_mut(patch)
    pj = os.path.join(SCRATCH, tag + ".json")
    with open(pj, "w") as fh:
        json.dump(patch, fh, indent=1)
    wp = os.path.join(SCRATCH, tag + ".wav")
    r = subprocess.run([CLI, pj, wp], capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(wp):
        print(tag, "RENDER_FAIL", (r.stderr or "")[-200:])
        return None
    return wp


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "analyze"
    if cmd == "analyze":
        analyze(sys.argv[2])
    elif cmd == "render":
        tag = sys.argv[2] if len(sys.argv) > 2 else "otj4"
        wp = render(tag)
        if wp:
            analyze(wp, tag)
