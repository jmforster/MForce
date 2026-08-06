"""Compare-pass variants for the vowel-grid entries Matt called out (dsp run 23).

Scope (verdict 2026-08-06): Sing_{Bass,Tenor,Alto,Soprano}_U, Sing_{Alto,Soprano}_A,
Sing_{Alto,Soprano}_{E,I}. References studied: Speech_M_UW ("much better" U) and
Sing_Alto_O / Sing_Soprano_O ("the best").

Measured grounding (scratch measure_vowels.py on renders/vowel_grid):
- What works (O, Speech UW): TWO RESOLVED low peaks — F1 on a strong harmonic at
  0 dB plus a distinct F2 harmonic at -8..-15 dB (Speech_M_UW: h3 392 + h8 1046
  -11; Alto_O: h2 440 + h4 880 -13.4; Soprano_O: h1 440 + h2 880 -15.4).
- Sung U family: F2 600-700 g0.1-0.25 is buried in the F1 skirt — a single low
  lump (Bass_U: monotonic slope off h3, no F2 peak; Alto_U: h1/h2/h3 all within
  2.2 dB = open-vowel energy at 660 -> the "between EH and OO" report;
  Soprano_U: h2 at -40, pure sine).
- Alto_A: h3..h6 all within 10 dB (h4 0, h5 -1.6) — F1 800 + F2 1150 merge into
  one 4-harmonic plateau, no resolved peaks.
- Soprano_A: h2 0, h3 -12, then NOTHING until h9 3960 (-17) — 3 audible partials
  with a 2.6-octave hole = "can hear individual partials".
- Alto E vs I: both peak h2 440; only difference h7 1540 -24 (E) vs h8 1760 -12
  (I) — E is a *darker* I. Soprano E vs I: both h1-only + one mid harmonic, and
  E's (2640, from F3) sits ABOVE I's (2200) — inverted e/i ordering.

Each variant = one hypothesis. Baselines (v0_grid) copied verbatim for A/B.
Writes patches/vowel_tweak/, renders renders/vowel_tweak/, verifies measured
peaks against prediction. Usage: python tools/gen_vowel_tweak.py [--no-render]
"""
import json
import math
import os
import shutil
import subprocess
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
GRID = os.path.join(REPO, "patches", "vowel_grid")
GRID_R = os.path.join(REPO, "renders", "vowel_grid")
OUT = os.path.join(REPO, "patches", "vowel_tweak")
OUT_R = os.path.join(REPO, "renders", "vowel_tweak")
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")

F0 = {"Sing_Bass": 110.0, "Sing_Tenor": 164.81, "Sing_Alto": 220.0,
      "Sing_Soprano": 440.0}

# entry -> list of (variant_name, {formant_id: (freq, gain, width)}, hypothesis)
# Unlisted formants keep grid values. Widths are ENGINE widths (full footprint).
# Soprano E/I/U: f1 is baked at the retune-effective 440 in every variant.
VARIANTS = {
    "Sing_Bass_U": [
        ("v1_speechF2", {"f2": (990.0, 0.38, 560.0)},
         "adopt Speech_M_UW's F2 (990/0.38) verbatim -> distinct ~1 kHz second "
         "peak (h9) at -8 dB, like the UW Matt rated 'much better'"),
        ("v2_resolved", {"f1": (350.0, 1.0, 250.0), "f2": (600.0, 0.3, 300.0)},
         "table freqs kept but narrowed + F2 gain x3 -> F2 600 becomes a "
         "resolved dark peak (h5/h6 ~ -18) instead of vanishing in F1's skirt"),
        ("v3_midF2", {"f2": (770.0, 0.32, 480.0)},
         "F2 dead-on h7 770 at -10 -> halfway between table-dark and "
         "speech-fronted u"),
    ],
    "Sing_Tenor_U": [
        ("v1_speechF2", {"f2": (990.0, 0.38, 560.0)},
         "Speech_M_UW F2 -> dead-on h6 989 at -8 dB second peak"),
        ("v2_resolved", {"f1": (350.0, 1.0, 250.0), "f2": (600.0, 0.3, 300.0)},
         "resolved dark F2: h4 659 bump ~ -19 with a real valley at h3 "
         "(grid has h4 -22.6 as a shoulder)"),
        ("v3_midF2", {"f2": (824.0, 0.32, 480.0)},
         "F2 dead-on h5 824 at -10 -> mid-dark u"),
    ],
    "Sing_Alto_U": [
        ("v1_speechUW", {"f1": (459.0, 1.0, 400.0), "f2": (1151.0, 0.4, 560.0)},
         "Speech_W_UW F1/F2 placement -> peaks h2 440 + h5 1100 (-8), valley "
         "h3/h4; the female-speech u shape at the same f0"),
        ("v2_h4F2", {"f2": (880.0, 0.1, 480.0)},
         "F2 moved off 700 (which fed the h3-660 open-vowel plateau) onto h4 "
         "880 at -7 under the h1/h2 top -> two resolved low peaks, h3 dead"),
        ("v3_resolved", {"f2": (700.0, 0.3, 300.0)},
         "table F2 700 kept but narrow+louder -> resolved peak at h3 660; "
         "control: is table F2 itself the problem (660 = EH F1 territory)?"),
    ],
    "Sing_Soprano_U": [
        ("v1_h2strong", {"f2": (880.0, 0.30, 480.0)},
         "grid render is a near-sine (h2 -40); F2 onto h2 at -10.5 -> "
         "two-peak dark vowel, brighter than Soprano_O's h2 (-15.4)"),
        ("v2_h2mid", {"f2": (880.0, 0.15, 480.0)},
         "h2 at -16.5 — same level as Soprano_O's h2; does U just become O, "
         "or does the weaker F3-5 top keep it u-ish?"),
        ("v3_h2dark", {"f2": (880.0, 0.07, 480.0)},
         "h2 at -23 — darkest variant that still isn't a sine"),
    ],
    "Sing_Alto_A": [
        ("v1_speechAH", {"f1": (936.0, 1.0, 400.0), "f2": (1551.0, 0.6, 560.0)},
         "Speech_W_AH F1/F2 (936/1551) -> resolved peaks h4 + h7 with a -24 dB "
         "valley between (grid = one h3-h6 plateau, everything within 10 dB)"),
        ("v2_tuned", {"f1": (880.0, 1.0, 560.0), "f2": (1320.0, 0.5, 560.0)},
         "formant-tuned: F1 dead-on h4, F2 dead-on h6 -> clean 0 / -6 two-peak "
         "'a' keeping the sung F1-F2 spacing"),
        ("v3_narrow", {"f1": (800.0, 1.0, 440.0), "f2": (1150.0, 0.631, 440.0)},
         "same freqs, widths cut ~1/3 -> plateau shrinks 4 harmonics -> 2 "
         "(h4/h5); tests 'merge is the whole problem' alone"),
    ],
    "Sing_Soprano_A": [
        ("v1_wide", {"f1": (800.0, 1.0, 1280.0), "f2": (1150.0, 0.5, 1440.0)},
         "widths x2 -> h1 -12 / h2 0 / h3 -8.5 all lit (grid: h1 absent, "
         "h3 then nothing to h9) -> fills the sparse-partial gap"),
        ("v2_tuned", {"f1": (880.0, 1.0, 1360.0), "f2": (1320.0, 0.4, 720.0),
                      "f3": (2900.0, 0.08, 960.0)},
         "soprano formant tuning: F1 dead-on h2, F2 dead-on h3 -> "
         "h1 -18 / h2 0 / h3 -8 slope + h9 sheen, the real-soprano 'ah' shape"),
        ("v3_wide_bright", {"f1": (800.0, 1.0, 1280.0),
                            "f2": (1150.0, 0.5, 1440.0),
                            "f3": (2900.0, 0.12, 1400.0)},
         "v1 + F3 raised/widened -> some h6/h7 (-26..-30) bridging the "
         "1.8-3.5 kHz hole"),
    ],
    "Sing_Alto_E": [
        ("v1_open", {"f1": (600.0, 1.0, 480.0), "f2": (1540.0, 0.25, 560.0)},
         "E's F1 up to 600 -> peak moves h2->h3 660 (I stays h2/h1) + strong "
         "F2 dead-on h7 at -7; maximal E-vs-I contrast, leans open (eh-ward)"),
        ("v2_f2boost", {"f2": (1450.0, 0.3, 640.0)},
         "F1 untouched; F2 down 1600->1450 and gain x4.8 -> audible h6/h7 at "
         "-13 (grid E's F2 was -24, i.e. QUIETER than I's — the whole reason "
         "E read as a dark I)"),
    ],
    "Sing_Alto_I": [
        ("v1_bright", {"f1": (300.0, 1.0, 320.0), "f2": (1980.0, 0.3, 640.0)},
         "F1 down -> peak moves h2->h1 220 (dark bottom) + F2 dead-on h9 1980 "
         "-> the classic wide i F1-F2 gap; pairs with Alto_E v1"),
        ("v2_f2up", {"f2": (2100.0, 0.25, 720.0)},
         "F2 1700->2100 -> bright cluster h9/h10 at -8 (grid peak h8 1760); "
         "conservative pair for Alto_E v2 (E 1540 vs I 2200)"),
    ],
    "Sing_Soprano_E": [
        ("v1_lowF2", {"f2": (1760.0, 0.3, 640.0), "f3": (2800.0, 0.08, 960.0)},
         "E's mid peak placed at h4 1760 (-10.5) and F3 tamed — grid E's only "
         "audible mid harmonic was 2640 (from F3), ABOVE I's 2200: inverted "
         "e/i ordering, now E < I as it should be"),
        ("v2_lowF2_soft", {"f2": (1760.0, 0.2, 640.0)},
         "same h4 target at -14 but F3 sheen kept — richer, conservative"),
    ],
    "Sing_Soprano_I": [
        ("v1_highF2", {"f2": (2640.0, 0.3, 720.0)},
         "I's mid peak up to h6 2640 (-10.5) — two harmonics above Soprano_E "
         "v1's h4; textbook e/i split at 440"),
        ("v2_f2boost", {"f2": (2140.0, 0.4, 560.0)},
         "peak stays h5 2200 but at -6 (grid -15.2), tighter band — louder, "
         "more forward i; pairs with Soprano_E v2"),
    ],
}

# Soprano retune-flagged entries: bake effective F1 (= f0 440) into variants
# (the grid curve delivers exactly this at the scored note; variants drop the
# curve and hard-set it so the tables above stay literal).
BAKED_F1 = {"Sing_Soprano_E": 440.0, "Sing_Soprano_I": 440.0,
            "Sing_Soprano_U": 440.0}

REFS = ["Speech_M_UW", "Sing_Alto_O", "Sing_Soprano_O"]


def load_grid(name):
    with open(os.path.join(GRID, name + ".json")) as f:
        return json.load(f)


def node(patch, nid):
    for n in patch["graph"]["nodes"]:
        if n["id"] == nid:
            return n
    raise KeyError(nid)


def apply_variant(patch, name, overrides):
    for fid, (fr, g, w) in overrides.items():
        p = node(patch, fid)["params"]
        p["frequency"], p["gain"], p["width"] = fr, round(g, 4), w
    if name in BAKED_F1:
        p = node(patch, "f1")["params"]
        if "f1" not in overrides:
            p["frequency"] = BAKED_F1[name]
    # Drop the retune curve — F1 is now literal (flat at the scored note for
    # bass/tenor/alto flagged entries; baked above for sopranos).
    patch["instrument"]["paramMap"]["frequency"] = "src.frequency"
    return patch


def taper(f, F, gain, w):
    lo, hi = F - w / 2.0, F + w / 2.0
    if f <= lo or f >= hi:
        return 0.0
    s = (f - lo) / (F - lo) if f < F else (hi - f) / (hi - F)
    return s * s * gain


def predicted(patch, f0, fmax=5000.0):
    fmts = [node(patch, f"f{i}")["params"] for i in range(1, 6)]
    ks = range(1, int(fmax / f0) + 1)
    return [max(taper(k * f0, fm["frequency"], fm["gain"], fm["width"])
                for fm in fmts) for k in ks]


def render(ppath, wpath):
    for attempt in range(4):
        r = subprocess.run([CLI, ppath, wpath], capture_output=True, text=True)
        if os.path.exists(wpath):
            return True
        time.sleep(2.0)  # cli may be mid-relink by another agent
    print("RENDER FAILED:", ppath, r.stderr.strip()[:200])
    return False


def measured(wpath, f0, fmax=5000.0):
    import numpy as np
    import wave
    with wave.open(wpath, "rb") as w:
        sr, n, ch, sw = (w.getframerate(), w.getnframes(), w.getnchannels(),
                         w.getsampwidth())
        raw = w.readframes(n)
    dt = {2: np.int16, 4: np.int32}[sw]
    x = np.frombuffer(raw, dtype=dt).astype(np.float64)
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    x /= np.iinfo(dt).max
    a, b = int(0.5 * sr), int(min(2.5 * sr, len(x)))
    seg = x[a:b] * np.hanning(b - a)
    t = np.arange(b - a) / sr
    ks = range(1, int(fmax / f0) + 1)
    return [float(abs(np.sum(seg * np.exp(-2j * np.pi * k * f0 * t))))
            for k in ks], float(np.sqrt(np.mean(x ** 2)))


def peaks_db(vals, f0, floor_db=-45.0):
    import numpy as np
    v = np.array(vals)
    db = 20 * np.log10(v / (v.max() + 1e-30) + 1e-12)
    out = []
    for i in range(len(v)):
        if db[i] < floor_db:
            continue
        if (i == 0 or v[i] >= v[i - 1]) and (i == len(v) - 1 or v[i] > v[i + 1]):
            out.append((i + 1, round((i + 1) * f0), round(float(db[i]), 1)))
    return out


def main():
    do_render = "--no-render" not in sys.argv[1:]
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(OUT_R, exist_ok=True)
    manifest, results = [], []

    for name, variants in VARIANTS.items():
        f0 = F0[name.rsplit("_", 1)[0]]
        # v0 = grid verbatim (patch + render)
        shutil.copyfile(os.path.join(GRID, name + ".json"),
                        os.path.join(OUT, name + "_v0_grid.json"))
        gw = os.path.join(GRID_R, name + ".wav")
        if os.path.exists(gw):
            shutil.copyfile(gw, os.path.join(OUT_R, name + "_v0_grid.wav"))
        for vname, ov, hyp in variants:
            patch = apply_variant(load_grid(name), name, ov)
            full = f"{name}_{vname}"
            ppath = os.path.join(OUT, full + ".json")
            with open(ppath, "w") as f:
                json.dump(patch, f, indent=1)
            pred = predicted(patch, f0)
            manifest.append({
                "file": full + ".json", "entry": name, "variant": vname,
                "f0": f0, "hypothesis": hyp,
                "overrides": {k: {"freq": v[0], "gain": v[1], "width": v[2]}
                              for k, v in ov.items()},
                "predicted_peaks": peaks_db(pred, f0)})
            if do_render:
                wpath = os.path.join(OUT_R, full + ".wav")
                if render(ppath, wpath):
                    meas, rms = measured(wpath, f0)
                    results.append((full, f0, rms, pred, meas))

    for r in REFS:
        src = os.path.join(GRID_R, r + ".wav")
        if os.path.exists(src):
            shutil.copyfile(src, os.path.join(OUT_R, "_REF_" + r + ".wav"))

    with open(os.path.join(OUT, "_manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)

    if do_render:
        import numpy as np
        print(f"{'variant':<32}{'rms':>7}{'corr':>7}  peaks: predicted | measured")
        for full, f0, rms, pred, meas in results:
            p, m = np.array(pred), np.array(meas)
            corr = float(np.corrcoef(p / (p.max() + 1e-30),
                                     m / (m.max() + 1e-30))[0, 1])
            pp = ", ".join(f"h{k}({hz}){db:+.0f}" for k, hz, db in
                           peaks_db(pred, f0, -30))
            mm = ", ".join(f"h{k}({hz}){db:+.0f}" for k, hz, db in
                           peaks_db(meas, f0, -30))
            print(f"{full:<32}{rms:>7.4f}{corr:>7.3f}  {pp}  |  {mm}")
    print(f"\n{len(manifest)} variants, {len(VARIANTS)} entries")


if __name__ == "__main__":
    main()
