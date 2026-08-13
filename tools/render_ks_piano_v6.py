#!/usr/bin/env python3
"""Calibrate + render the ks_piano_v6 audition set.

Two-pass per patch: render at instrument.volume 0.01 (soft_clip inactive),
measure true peak, scale to 0.85 pre-clip, write the calibrated volume back
into the patch file, render the final WAV to renders/dsp/pending/ks_piano_v6/.
"""
import json
import os
import struct
import subprocess
import sys
import wave

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
SRC = os.path.join(ROOT, "patches", "pending", "ks_piano_v6")
OUT = os.path.join(ROOT, "renders", "dsp", "pending", "ks_piano_v6")
PAN = 0.70710678  # centre equal-power pan factor in the WAV


def wav_peak(path):
    w = wave.open(path)
    n, ch = w.getnframes(), w.getnchannels()
    s = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    w.close()
    return max(abs(x) for x in s) / 32767.0


def main():
    # Optional argv filter: render only the named patch files (e.g.
    # `render_ks_piano_v6.py v6m.json`) so a re-run doesn't overwrite the
    # audition history with renders from a changed engine.
    only = set(sys.argv[1:])
    os.makedirs(OUT, exist_ok=True)
    for f in sorted(os.listdir(SRC)):
        if not f.endswith(".json"):
            continue
        if only and f not in only:
            continue
        path = os.path.join(SRC, f)
        p = json.load(open(path))
        # pass 1: measure with clipper inactive
        vol_final = p["instrument"].get("volume", 1.0)
        p["instrument"]["volume"] = 0.01
        cal = path + ".cal"
        json.dump(p, open(cal, "w"))
        calwav = os.path.join(OUT, "_cal.wav")
        subprocess.run([CLI, cal, calwav], capture_output=True)
        true_peak = wav_peak(calwav) / PAN / 0.01
        vol = round(0.85 / true_peak, 4) if true_peak > 0 else 1.0
        # pass 2: write calibrated volume into the real patch, render final
        p["instrument"]["volume"] = vol
        json.dump(p, open(path, "w"), indent=2)
        os.remove(cal)
        wavout = os.path.join(OUT, f[:-5] + ".wav")
        subprocess.run([CLI, path, wavout], capture_output=True)
        print("%-24s truePeak=%6.2f vol=%.4f wavPeak=%.3f"
              % (f, true_peak, vol, wav_peak(wavout)))
    calpath = os.path.join(OUT, "_cal.wav")
    if os.path.exists(calpath):
        os.remove(calpath)


if __name__ == "__main__":
    main()
