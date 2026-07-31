#!/usr/bin/env python3
"""Alias-suppression measurement for an arbitrary FM patch (item 7, G3).

measure.py answers the question for one fixed test tone. This generalises it:
give it any patch containing an FMSource node, and it re-renders that patch at
oversample = 1,2,4,8,16, treats M=16 as the near-alias-free ground truth, and
reports the in-band residual of each M against it.

Why it exists: the run-8/run-11 alias numbers were all taken at HIGH carrier
frequencies (C7/C8), where oversampling helps a lot. The open default-policy
question — "oversample 4-8 for bright/high-index FM?" — conflates high
frequency with high index. A low carrier driven at a huge modulation index is
the case that separates them, and nothing had measured it.

Suppression(M) dB = 20*log10(residual_1 / residual_M): how much of the M=1
alias energy factor M removes.

Usage: python research/fm_alias/measure_patch.py <patch.json> [fm_node_id]
"""
import copy
import json
import os
import subprocess
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
SCRATCH = os.path.join(REPO, "renders", "fm_oversample")
os.makedirs(SCRATCH, exist_ok=True)

FACTORS = [1, 2, 4, 8, 16]
T0, T1 = 0.3, 1.7
BAND_HI = 18000.0


def read_mono(path):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        raw = w.readframes(n)
    a = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    if ch > 1:
        a = a.reshape(-1, ch).mean(axis=1)
    return a, sr


def spectrum(sig, sr):
    seg = sig[int(T0 * sr):int(T1 * sr)]
    win = np.hanning(len(seg))
    mag = np.abs(np.fft.rfft(seg * win))
    freqs = np.fft.rfftfreq(len(seg), 1.0 / sr)
    return freqs, mag


def find_fm_node(patch, node_id=None):
    for n in patch["graph"]["nodes"]:
        if node_id and n["id"] == node_id:
            return n
        if not node_id and n.get("type") == "FMSource":
            return n
    sys.exit("no FMSource node found")


def main():
    patch_path = sys.argv[1]
    node_id = sys.argv[2] if len(sys.argv) > 2 else None
    base = json.load(open(patch_path))
    tag = os.path.splitext(os.path.basename(patch_path))[0]

    specs = {}
    for m in FACTORS:
        p = copy.deepcopy(base)
        find_fm_node(p, node_id)["params"]["oversample"] = m
        pf = os.path.join(SCRATCH, f"_{tag}_os{m}.json")
        wf = os.path.join(SCRATCH, f"_{tag}_os{m}.wav")
        json.dump(p, open(pf, "w"), indent=1)
        r = subprocess.run([CLI, pf, wf], capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f"render failed at M={m}: {r.stderr[:300]}")
        x, sr = read_mono(wf)
        specs[m] = spectrum(x, sr)

    freqs, ref = specs[16]
    band = freqs <= BAND_HI
    resid = {}
    for m in FACTORS:
        d = specs[m][1][band] - ref[band]
        resid[m] = float(np.sqrt(np.mean(d * d)))

    print(f"patch: {tag}   (M=16 taken as ground truth)")
    print(f"{'M':>3} {'residual':>12} {'suppression':>13}")
    print("-" * 31)
    for m in FACTORS:
        s = (20 * np.log10(resid[1] / resid[m])) if resid[m] > 0 else float("inf")
        tail = "  (reference)" if m == 16 else ""
        print(f"{m:>3} {resid[m]:>12.6f} {s:>12.1f} dB{tail}")

    ref_rms = float(np.sqrt(np.mean(ref[band] ** 2)))
    print(f"\nM=1 alias residual is {20*np.log10(resid[1]/ref_rms):.1f} dB "
          f"relative to the in-band signal spectrum.")


if __name__ == "__main__":
    main()
