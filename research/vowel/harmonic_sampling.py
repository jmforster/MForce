"""Why the soprano vowels collapse: harmonic-comb sampling of the formant envelope.

Matt's run-24 verdict (dsp/REVIEW.md item 23) says Soprano E "sounds like I",
Soprano I is "organy", and Soprano U "lacks vowel character" -- while the same
recipes at alto/tenor/bass pitch are fine. The hypothesis under test is that
this is not a formant-tuning miss but a SAMPLING limit: an additive voice only
reproduces the formant envelope where it has a harmonic to carry it, and at
f0 = 440 the comb is too coarse to resolve the differences between the vowels.

Two independent measurements, both grounded in the rendered WAVs (not in the
patch tables), plus one model-side check:

  1. SEPARABILITY -- harmonic level vector per render, then pairwise distance
     between vowels within a voice. If Soprano E/I really are the same sound,
     their measured harmonic patterns are close; the same pair at alto pitch
     should be far.
  2. RESOLUTION   -- for each formant in each patch, how many harmonics fall
     inside its -3 dB band, and how far F2 must move before the peak harmonic
     changes. That is the granularity of the control Matt actually has.
  3. SPARSITY     -- how many harmonics live below 3 kHz (where vowel identity
     is carried). This is the "organy" axis: few, loud, widely spaced lines.

Usage:  python research/vowel/harmonic_sampling.py
Writes: out/vowel_sampling.json  (+ prints a report)
"""
import json
import pathlib
import wave

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
GRID_RENDERS = ROOT / "renders" / "vowel_grid"
GRID_PATCHES = ROOT / "patches" / "vowel_grid"
OUT = ROOT / "out" / "vowel_sampling.json"

VOWELS = ["A", "E", "I", "O", "U"]
# voice -> f0 as authored in the grid
VOICES = {"Bass": 110.0, "Tenor": 165.0, "Alto": 220.0, "Soprano": 440.0}

# Vowel identity lives below ~3 kHz (F1/F2/F3).
IDENTITY_HZ = 3000.0
N_HARM = 24          # harmonics compared in the distance metric
FLOOR_DB = -60.0     # clamp so silent harmonics don't dominate the distance


def read_wav_mono(path):
    with wave.open(str(path), "rb") as w:
        ch, sw, sr, nf = (w.getnchannels(), w.getsampwidth(),
                          w.getframerate(), w.getnframes())
        raw = w.readframes(nf)
    if sw != 2:
        raise ValueError(f"{path}: expected 16-bit PCM, got {sw*8}-bit")
    x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x, sr


def harmonic_levels(path, f0, n_harm=N_HARM):
    """dB level of each harmonic, relative to the loudest, from a steady window.

    Takes a 1 s window from the sustain (skipping the attack) and reads the
    magnitude at each k*f0 bin. Long window -> the bin sits on the line.
    """
    x, sr = read_wav_mono(path)
    start = int(0.5 * sr)
    seg = x[start:start + sr]
    if len(seg) < sr // 2:
        seg = x[-(sr // 2):]
    win = np.hanning(len(seg))
    spec = np.abs(np.fft.rfft(seg * win))
    freqs = np.fft.rfftfreq(len(seg), 1.0 / sr)
    df = freqs[1] - freqs[0]
    levels = []
    for k in range(1, n_harm + 1):
        fk = k * f0
        if fk >= sr / 2:
            levels.append(0.0)
            continue
        # peak-pick within +-2 bins to be robust to any drift
        c = int(round(fk / df))
        lo, hi = max(0, c - 2), min(len(spec), c + 3)
        levels.append(float(spec[lo:hi].max()))
    levels = np.array(levels)
    ref = levels.max()
    if ref <= 0:
        return np.full(n_harm, FLOOR_DB)
    db = 20.0 * np.log10(np.maximum(levels, 1e-12) / ref)
    return np.maximum(db, FLOOR_DB)


def pattern_distance(a, b):
    """RMS dB difference between two harmonic level vectors."""
    return float(np.sqrt(np.mean((a - b) ** 2)))


def load_formants(patch_path):
    p = json.loads(patch_path.read_text())
    nodes = {n["id"]: n for n in p["graph"]["nodes"]}
    out = []
    for fid in ("f1", "f2", "f3", "f4", "f5"):
        if fid in nodes:
            q = nodes[fid]["params"]
            out.append({"id": fid, "freq": float(q["frequency"]),
                        "gain": float(q["gain"]), "width": float(q["width"])})
    f0 = float(nodes["src"]["params"]["frequency"])
    return f0, out


def harmonics_in_band(f0, centre, width, f_max=20000.0):
    """Harmonics inside the formant's stated width (its full span, centre+-w/2)."""
    lo, hi = centre - width / 2.0, centre + width / 2.0
    ks = [k for k in range(1, int(f_max / f0) + 1) if lo <= k * f0 <= hi]
    return ks


def nearest_harmonic(f0, freq):
    k = max(1, int(round(freq / f0)))
    return k, k * f0, freq - k * f0


def main():
    report = {"voices": {}, "separability": {}, "resolution": {}}

    # ---- 1. separability -------------------------------------------------
    print("=" * 74)
    print("1. VOWEL SEPARABILITY  (RMS dB distance between harmonic patterns)")
    print("=" * 74)
    patterns = {}
    for voice, f0 in VOICES.items():
        for v in VOWELS:
            wav = GRID_RENDERS / f"Sing_{voice}_{v}.wav"
            if not wav.exists():
                print(f"  MISSING {wav.name}")
                continue
            patterns[(voice, v)] = harmonic_levels(wav, f0)

    for voice, f0 in VOICES.items():
        present = [v for v in VOWELS if (voice, v) in patterns]
        dists = {}
        for i, a in enumerate(present):
            for b in present[i + 1:]:
                dists[f"{a}-{b}"] = pattern_distance(
                    patterns[(voice, a)], patterns[(voice, b)])
        if not dists:
            continue
        vals = np.array(list(dists.values()))
        closest = min(dists, key=dists.get)
        print(f"\n  {voice} (f0={f0:.0f} Hz)  "
              f"mean={vals.mean():5.2f} dB  min={vals.min():5.2f} dB "
              f"({closest})")
        for pair in sorted(dists, key=dists.get):
            mark = "  <-- closest pair" if pair == closest else ""
            print(f"      {pair:6s} {dists[pair]:6.2f} dB{mark}")
        report["separability"][voice] = {
            "f0": f0, "pairs": dists,
            "mean": float(vals.mean()), "min": float(vals.min()),
            "closest_pair": closest,
        }

    print("\n  E-vs-I across the four voices (Matt: soprano E 'sounds like I'):")
    for voice, f0 in VOICES.items():
        d = report["separability"].get(voice, {}).get("pairs", {}).get("E-I")
        if d is not None:
            print(f"      {voice:8s} f0={f0:6.1f}  E-I = {d:6.2f} dB")

    # ---- 2. resolution ---------------------------------------------------
    print()
    print("=" * 74)
    print("2. FORMANT RESOLUTION  (harmonics available to carry each formant)")
    print("=" * 74)
    for voice, f0_nom in VOICES.items():
        for v in VOWELS:
            patch = GRID_PATCHES / f"Sing_{voice}_{v}.json"
            if not patch.exists():
                patch = ROOT / "patches" / "vowel_tweak" / \
                    f"Sing_{voice}_{v}_v0_grid.json"
            if not patch.exists():
                continue
            f0, fmts = load_formants(patch)
            rows = []
            for fm in fmts[:3]:
                ks = harmonics_in_band(f0, fm["freq"], fm["width"])
                k, kf, err = nearest_harmonic(f0, fm["freq"])
                rows.append({
                    "formant": fm["id"], "freq": fm["freq"],
                    "width": fm["width"], "n_harmonics": len(ks),
                    "harmonics": ks, "peak_k": k, "peak_hz": kf,
                    "detune_hz": round(err, 1),
                })
            n_below = int(IDENTITY_HZ // f0)
            report["resolution"][f"{voice}_{v}"] = {
                "f0": f0, "formants": rows,
                "harmonics_below_3k": n_below,
            }
            f1f2 = " ".join(
                f"{r['formant']}:{r['freq']:.0f}->h{r['peak_k']}"
                f"({r['n_harmonics']}h in band)" for r in rows[:2])
            print(f"  {voice:8s} {v}  f0={f0:5.0f}  h<3k={n_below:2d}   {f1f2}")

    # ---- 3. the E/I collapse, stated numerically -------------------------
    print()
    print("=" * 74)
    print("3. WHY E AND I COLLAPSE AT SOPRANO PITCH")
    print("=" * 74)
    for voice, f0 in VOICES.items():
        ei = {}
        for v in ("E", "I"):
            key = f"{voice}_{v}"
            if key not in report["resolution"]:
                continue
            r = report["resolution"][key]
            f1 = r["formants"][0]
            f2 = r["formants"][1]
            ei[v] = (f1["freq"], f1["peak_k"], f2["freq"], f2["peak_k"])
        if len(ei) != 2:
            continue
        e, i = ei["E"], ei["I"]
        same_f1 = e[1] == i[1]
        same_f2 = e[3] == i[3]
        d = report["separability"].get(voice, {}).get("pairs", {}).get("E-I")
        print(f"  {voice:8s} f0={f0:5.0f}  "
              f"E: F1 {e[0]:6.0f}->h{e[1]}, F2 {e[2]:6.0f}->h{e[3]}   |   "
              f"I: F1 {i[0]:6.0f}->h{i[1]}, F2 {i[2]:6.0f}->h{i[3]}")
        verdict = []
        if same_f1:
            verdict.append("F1 on the SAME harmonic")
        if same_f2:
            verdict.append("F2 on the SAME harmonic")
        if not verdict:
            verdict.append("both formants land on different harmonics")
        print(f"            -> {'; '.join(verdict)}"
              f"   (measured E-I distance {d:.2f} dB)")
        report.setdefault("ei_collapse", {})[voice] = {
            "f0": f0, "E": e, "I": i,
            "same_f1_harmonic": same_f1, "same_f2_harmonic": same_f2,
            "measured_distance_db": d,
        }

    # F2 travel needed to move the peak harmonic one step
    print()
    print("  Control granularity: how far F2 must move to change peak harmonic")
    for voice, f0 in VOICES.items():
        print(f"      {voice:8s} f0={f0:6.1f} Hz -> F2 must move "
              f">= {f0/2:6.1f} Hz to cross to the next harmonic")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(report, indent=2))
    print(f"\nwrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
