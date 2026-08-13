#!/usr/bin/env python3
"""Per-note level/tuning sweep of a ks_piano_v6 patch.

Renders every note (default midi 21..107, house A1..B8) as its own WAV at
calibration volume (soft clip inactive), measures attack peak, sustain RMS
at 1 s and 2 s, and FFT-estimated f0, then prints each against the patch's
own paramMap curve predictions so level variation can be attributed to a
curve we consciously included vs. some other mechanism.

Usage: python tools/measure_ks_v6_levels.py [patch.json] [outdir]
"""
import json
import math
import os
import struct
import subprocess
import sys
import wave

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
PAN = 0.70710678
CAL_VOL = 0.01
NOTE_LO, NOTE_HI = 21, 107
NAMES = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"]


def house_name(midi):
    return "%s%d" % (NAMES[midi % 12], midi // 12)


def note_to_freq(midi):
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


def read_wav_mono(path):
    w = wave.open(path)
    n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
    s = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    w.close()
    # L channel, undo pan + cal volume -> pre-master linear units
    return [s[i * ch] / 32767.0 / PAN / CAL_VOL for i in range(n)], sr


def rms(x, sr, t0, t1):
    a, b = int(t0 * sr), min(int(t1 * sr), len(x))
    if b <= a:
        return 0.0
    return math.sqrt(sum(v * v for v in x[a:b]) / (b - a))


def db(v, ref=1.0):
    return 20.0 * math.log10(max(v, 1e-12) / ref)


def estimate_f0(x, sr, target):
    """FFT peak within +/-3 semitones of target, parabolic interpolation."""
    a, b = int(0.3 * sr), min(int(1.8 * sr), len(x))
    seg = x[a:b]
    n = len(seg)
    if n < 4096:
        return float("nan")
    # zero-pad to next pow2 for resolution
    m = 1
    while m < n:
        m *= 2
    m *= 4
    # hann window
    seg = [v * (0.5 - 0.5 * math.cos(2 * math.pi * i / (n - 1)))
           for i, v in enumerate(seg)]
    try:
        import numpy as np
        sp = np.abs(np.fft.rfft(np.array(seg), m))
        lo = max(1, int(target * 2 ** (-3 / 12.0) * m / sr))
        hi = min(len(sp) - 2, int(target * 2 ** (3 / 12.0) * m / sr))
        if hi <= lo:
            return float("nan")
        k = lo + int(np.argmax(sp[lo:hi + 1]))
        y0, y1, y2 = sp[k - 1], sp[k], sp[k + 1]
        denom = (y0 - 2 * y1 + y2)
        d = 0.5 * (y0 - y2) / denom if abs(denom) > 1e-20 else 0.0
        return (k + d) * sr / m
    except ImportError:
        return float("nan")


def curve_map(curve, freq):
    """Mirror ParamSlot::map — linear interp in log-frequency, end-clamped."""
    if not curve:
        return freq
    if freq <= curve[0][0]:
        return curve[0][1]
    if freq >= curve[-1][0]:
        return curve[-1][1]
    for i in range(1, len(curve)):
        if freq <= curve[i][0]:
            lf = math.log(freq / curve[i - 1][0]) / \
                 math.log(curve[i][0] / curve[i - 1][0])
            return curve[i - 1][1] + (curve[i][1] - curve[i - 1][1]) * lf
    return curve[-1][1]


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        ROOT, "patches", "pending", "ks_piano_v6", "v6l_cmaes3.json")
    outdir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.environ.get("TEMP", "."), "ks_v6_sweep")
    os.makedirs(outdir, exist_ok=True)

    base = json.load(open(src))
    committed_vol = base["instrument"].get("volume", 1.0)
    base["instrument"]["volume"] = CAL_VOL

    # pull the frequency-driven curves for prediction columns
    curves = {}
    for slot in base["instrument"]["paramMap"]["frequency"]:
        if isinstance(slot, dict):
            curves[slot["target"]] = slot["curve"]

    rows = []
    csv = open(os.path.join(outdir, "sweep.csv"), "w")
    csv.write("midi,name,freq,attack_peak,peak_db,rms1s_db,rms2s_db,"
              "sus1_rel_db,sus2_rel_db,f0_meas,cents_err,"
              "exc_level,exc_lp,t60,brightness,clickgain,detune\n")
    for midi in range(NOTE_LO, NOTE_HI + 1):
        p = json.loads(json.dumps(base))
        p["score"] = [{"note": midi, "velocity": 0.85,
                       "time": 0.0, "duration": 3.5}]
        pj = os.path.join(outdir, "n%03d.json" % midi)
        pw = os.path.join(outdir, "n%03d.wav" % midi)
        json.dump(p, open(pj, "w"))
        r = subprocess.run([CLI, pj, pw], capture_output=True)
        if r.returncode != 0 or not os.path.exists(pw):
            print("%3d %-4s RENDER FAILED" % (midi, house_name(midi)))
            continue
        x, sr = read_wav_mono(pw)
        f = note_to_freq(midi)
        apk = max(abs(v) for v in x[:int(0.25 * sr)])
        pk = max(abs(v) for v in x)
        r1 = rms(x, sr, 0.9, 1.1)
        r2 = rms(x, sr, 1.9, 2.1)
        f0 = estimate_f0(x, sr, f)
        cents = 1200.0 * math.log2(f0 / f) if f0 == f0 and f0 > 0 else float("nan")
        row = dict(
            midi=midi, name=house_name(midi), freq=f, attack_peak=apk,
            peak_db=db(pk), rms1s_db=db(r1), rms2s_db=db(r2),
            sus1_rel_db=db(r1) - db(apk), sus2_rel_db=db(r2) - db(apk),
            f0_meas=f0, cents_err=cents,
            exc_level=curve_map(curves.get("exc_level.source2", []), f),
            exc_lp=curve_map(curves.get("exc_lp.cutoffFreq", []), f),
            t60=curve_map(curves.get("string.t60", []), f),
            brightness=curve_map(curves.get("string.brightness", []), f),
            clickgain=curve_map(curves.get("exc_clickgain.source2", []), f),
            detune=curve_map(curves.get("string.detune", []), f),
        )
        rows.append(row)
        csv.write("%(midi)d,%(name)s,%(freq).2f,%(attack_peak).4f,"
                  "%(peak_db).2f,%(rms1s_db).2f,%(rms2s_db).2f,"
                  "%(sus1_rel_db).2f,%(sus2_rel_db).2f,%(f0_meas).2f,"
                  "%(cents_err).1f,%(exc_level).4f,%(exc_lp).0f,%(t60).2f,"
                  "%(brightness).4f,%(clickgain).4f,%(detune).2f\n" % row)
        os.remove(pj)
        os.remove(pw)
    csv.close()

    print("committed volume %.4f  (all levels pre-master, linear units)"
          % committed_vol)
    print("%4s %-4s %9s | %7s %7s %7s %7s | %7s | %8s" %
          ("midi", "note", "freq", "atkPk", "pk dB", "sus1dB", "sus2dB",
           "excLvl", "cents"))
    for row in rows:
        print("%4d %-4s %9.2f | %7.3f %7.2f %7.2f %7.2f | %7.3f | %8.1f" %
              (row["midi"], row["name"], row["freq"], row["attack_peak"],
               row["peak_db"], row["rms1s_db"], row["rms2s_db"],
               row["exc_level"], row["cents_err"]))
    print("csv: %s" % os.path.join(outdir, "sweep.csv"))


if __name__ == "__main__":
    main()
