"""Vowel patch grid — Speech_{M,W,C} x 12 Hillenbrand vowels + Sing_{4 voices} x 5.

Architecture = the Matt-validated liar2 recipe (tools/gen_liar2.py), static:
flat FullPartials buzz (160 partials, rolloff 0) -> single FormantSpectrum of
Formant nodes -> formantWeight 1.0, formantFloor 0.0, instrument-style patch
(output = src, paramMap frequency), score note = nearest MIDI to the group F0.

Data (docs/research/formants/CATALOG.md is authoritative):
- Speech: DERIVED_hillenbrand1995_means.csv. M = men rows, W = women rows,
  C = children = plain average of boy/girl rows (F4 averaged weighted by
  n_f4_measured).
- Sing: csound_singing_formants.csv (soprano/alto/tenor/bass; countertenor
  skipped per the requested grid). F1-F5 + amps(dB) + bandwidths straight
  from the table.

Extrapolation (speech only):
- F4: Hillenbrand mean where trusted (n_f4_measured >= n/2 — true for every
  cell in this grid), else Klatt 3300 * group F3 ratio.
- F5: Klatt 3750 * R_g where R_g = mean_F3(group)/mean_F3(men), er excluded
  from both means (rhotic low-F3 outlier).
- Bandwidths (-3dB-style, Hz): Klatt 1980 typical B1..B5 = 50/70/110/250/200.
- Amplitudes: Hillenbrand has none; gain_i = F1/F_i (amplitude ~ 1/f, i.e.
  -6 dB/octave source-spectrum falloff), normalized so gain(F1) = 1.0.

Engine width = WIDTH_MULT * literature bandwidth. WIDTH_MULT = 8 (Matt's
current default from the liar2 x3..x14 ladder). The Csound table's bw column
is the same -3dB-style 40-200 Hz family as Klatt's B1-B3, so the SAME x8
applies to the Sing set: engine widths land at 320-1600, the same range as
the validated speech widths (400-2000).

F0: speech = Hillenbrand group F0 mean over the 12 vowels (M/W/C ~ 130/222/238);
sing = bass 110, tenor 165, alto 220, soprano 440. Score note = nearest MIDI
(equal temperament — rendered f0 is the MIDI-note frequency).

--f1-retune (dsp run 22): the 11 physics-limited entries (README) additionally
get a paramMap frequency->F1 curve on the f1 Formant node implementing the
soprano F1-retune: effective F1 = max(table F1, f0), capped at F1_RETUNE_CEIL.
The curve is knots [[F1,F1], ...semitone identity steps..., [CEIL,CEIL]] —
flat at the table F1 below it, identity (F1 tracks f0) above. The engine
interpolates linear-value in log-frequency, so identity needs dense knots;
semitone spacing keeps curve(f0) within 0.05 % of f0. Only the F1 node is
curved — F2..F5 stay at table values.
"""
import csv
import json
import math
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DATA = os.path.join(REPO, "docs", "research", "formants")
OUT = os.path.join(REPO, "patches", "vowel_grid")
os.makedirs(OUT, exist_ok=True)

SECONDS = 3.0
WIDTH_MULT = 8.0
KLATT_BW = [50.0, 70.0, 110.0, 250.0, 200.0]   # B1..B5 typical (Klatt 1980 Table I)
KLATT_F4, KLATT_F5 = 3300.0, 3750.0

# Hillenbrand vowel code -> (ASCII name, IPA)
VOWELS = {
    "ae": ("AE", "æ"), "ah": ("AH", "ɑ"), "aw": ("AW", "ɔ"),
    "eh": ("EH", "ɛ"), "ei": ("EI", "e(ɪ)"), "er": ("ER", "ɝ"),
    "ih": ("IH", "ɪ"), "iy": ("IY", "i"), "oa": ("OA", "o(ʊ)"),
    "oo": ("OO", "ʊ"), "uh": ("UH", "ʌ"), "uw": ("UW", "u"),
}

SING_F0 = {"bass": 110.0, "tenor": 165.0, "alto": 220.0, "soprano": 440.0}
SING_IPA = {"a": "ɑ~a", "e": "e", "i": "i", "o": "o", "u": "u"}

# --f1-retune: the physics-limited entries (patches/vowel_grid/README.md,
# verification 2026-08-02) that get the F1 = max(F1, f0) tracking curve.
F1_RETUNE_FLAGGED = {
    "Speech_W_AW",
    "Sing_Bass_U", "Sing_Tenor_O", "Sing_Tenor_U",
    "Sing_Alto_A", "Sing_Alto_U",
    "Sing_Soprano_A", "Sing_Soprano_E", "Sing_Soprano_I",
    "Sing_Soprano_O", "Sing_Soprano_U",
}
F1_RETUNE_CEIL = 1100.0


def f1_retune_curve(f1):
    """Breakpoints [[hz, F1_eff], ...] for effective F1 = clamp(f0, F1, CEIL).

    Engine interpolation (instrument.h ParamSlot::map) is linear-value in
    log-frequency, clamped at the end knots — so below the first knot the
    curve returns F1 (flat), and the identity segment is sampled at semitone
    steps to bound the log-interp bow to < 0.05 %.
    """
    knots = [[round(f1, 1), round(f1, 1)]]
    f = f1
    while f * 2 ** (1 / 12.0) < F1_RETUNE_CEIL:
        f *= 2 ** (1 / 12.0)
        knots.append([round(f, 1), round(f, 1)])
    knots.append([F1_RETUNE_CEIL, F1_RETUNE_CEIL])
    return knots


def curve_eval(curve, freq):
    """Python mirror of instrument.h ParamSlot::map (log-hz lerp, clamped)."""
    if freq <= curve[0][0]:
        return curve[0][1]
    if freq >= curve[-1][0]:
        return curve[-1][1]
    for i in range(1, len(curve)):
        if freq <= curve[i][0]:
            lf = (math.log(freq / curve[i - 1][0])
                  / math.log(curve[i][0] / curve[i - 1][0]))
            return curve[i - 1][1] + (curve[i][1] - curve[i - 1][1]) * lf
    return curve[-1][1]


def nearest_midi(f0):
    return int(round(69 + 12 * math.log2(f0 / 440.0)))


def midi_freq(midi):
    return 440.0 * 2 ** ((midi - 69) / 12.0)


def make_patch(f0, formants, f1_retune=False):
    """formants: list of (freq, gain_linear, lit_bw) — engine width = x WIDTH_MULT.

    f1_retune: additionally map note frequency onto the f1 node's frequency
    through the max(F1, f0) curve (soprano F1 retuning; F2+ untouched).
    """
    nodes = [{"id": "ampEnv", "type": "Envelope",
              "params": {"preset": "adsr", "attack": 0.03, "decay": 0.05,
                         "sustainLevel": 0.9, "release": 0.0}}]
    fids = []
    for i, (fr, g, bw) in enumerate(formants):
        fid = f"f{i + 1}"
        nodes.append({"id": fid, "type": "Formant",
                      "params": {"frequency": round(fr, 1), "gain": round(g, 4),
                                 "width": round(bw * WIDTH_MULT, 1), "power": 2.0}})
        fids.append({"ref": fid})
    nodes += [
        {"id": "spec", "type": "FormantSpectrum", "params": {"formants": fids}},
        {"id": "fp", "type": "FullPartials",
         "params": {"maxPartials": 160, "minMult": 1,
                    "rolloff1": 0.0, "rolloff2": 0.0}},
        {"id": "src", "type": "AdditiveSource",
         "params": {"seed": 42, "frequency": float(round(f0, 2)),
                    "amplitude": {"ref": "ampEnv"},
                    "formant": {"ref": "spec"},
                    "formantWeight": 1.0, "formantFloor": 0.0,
                    "partials": {"ref": "fp"}}},
    ]
    midi = nearest_midi(f0)
    freq_map = "src.frequency"
    if f1_retune:
        freq_map = ["src.frequency",
                    {"target": "f1.frequency",
                     "curve": f1_retune_curve(formants[0][0])}]
    return {"sampleRate": 48000,
            "graph": {"nodes": nodes, "output": "src"},
            "instrument": {"paramMap": {"frequency": freq_map},
                           "polyphony": 1},
            "score": [{"note": midi, "velocity": 0.85, "time": 0.0,
                       "duration": SECONDS}]}


def load_hillenbrand():
    rows = {}
    with open(os.path.join(DATA, "DERIVED_hillenbrand1995_means.csv")) as f:
        for r in csv.DictReader(f):
            rows[(r["speaker_group"], r["vowel_code"])] = {
                k: float(r[k]) for k in ("n", "f0_mean", "f1_mean", "f2_mean",
                                         "f3_mean", "f4_mean", "n_f4_measured")}
    groups = {}
    for g in ("m", "w"):
        groups[g] = {v: dict(rows[(g, v)]) for v in VOWELS}
    # Children = average of boys/girls; F4 weighted by n_f4_measured.
    c = {}
    for v in VOWELS:
        b, gi = rows[("b", v)], rows[("g", v)]
        nf4 = b["n_f4_measured"] + gi["n_f4_measured"]
        c[v] = {
            "n": b["n"] + gi["n"],
            "f0_mean": (b["f0_mean"] + gi["f0_mean"]) / 2,
            "f1_mean": (b["f1_mean"] + gi["f1_mean"]) / 2,
            "f2_mean": (b["f2_mean"] + gi["f2_mean"]) / 2,
            "f3_mean": (b["f3_mean"] + gi["f3_mean"]) / 2,
            "f4_mean": (b["f4_mean"] * b["n_f4_measured"]
                        + gi["f4_mean"] * gi["n_f4_measured"]) / nf4,
            "n_f4_measured": nf4,
        }
    groups["c"] = c
    return groups


def load_csound():
    tab = {}
    with open(os.path.join(DATA, "csound_singing_formants.csv")) as f:
        for r in csv.DictReader(f):
            key = (r["voice"], r["vowel_label"])
            tab.setdefault(key, {})[r["formant"]] = (
                float(r["freq_hz"]), float(r["amp_db"]), float(r["bw_hz"]))
    return tab


def main():
    retune = "--f1-retune" in sys.argv[1:]
    manifest = []  # (file, group_label, vowel, f0, midi, [(F,gain,engine_w)...], f1_retune)

    def emit(fname, f0, fmts):
        f1r = retune and fname[:-5] in F1_RETUNE_FLAGGED
        with open(os.path.join(OUT, fname), "w") as fp:
            json.dump(make_patch(f0, fmts, f1_retune=f1r), fp, indent=1)
        print("wrote", fname + ("  [f1-retune]" if f1r else ""))
        return f1r

    groups = load_hillenbrand()
    # Group F3 ratio vs men, er excluded (rhotic outlier).
    f3mean = {g: sum(d["f3_mean"] for v, d in groups[g].items() if v != "er") / 11
              for g in groups}
    ratio = {g: f3mean[g] / f3mean["m"] for g in groups}
    label = {"m": "M", "w": "W", "c": "C"}

    for g in ("m", "w", "c"):
        f0 = sum(d["f0_mean"] for d in groups[g].values()) / len(groups[g])
        for v, (name, _ipa) in VOWELS.items():
            d = groups[g][v]
            freqs = [d["f1_mean"], d["f2_mean"], d["f3_mean"]]
            # F4: trust Hillenbrand where n_f4_measured >= n/2, else Klatt scaled.
            if d["n_f4_measured"] >= d["n"] / 2:
                freqs.append(d["f4_mean"])
            else:
                freqs.append(KLATT_F4 * ratio[g])
            freqs.append(KLATT_F5 * ratio[g])  # F5: always extrapolated
            fmts = [(fr, freqs[0] / fr, KLATT_BW[i]) for i, fr in enumerate(freqs)]
            fname = f"Speech_{label[g]}_{name}.json"
            f1r = emit(fname, f0, fmts)
            manifest.append((fname, f"Speech_{label[g]}", v, f0,
                             nearest_midi(f0), fmts, f1r))

    tab = load_csound()
    for voice, f0 in SING_F0.items():
        for vow in "aeiou":
            fmts = []
            for i in range(1, 6):
                fr, db, bw = tab[(voice, vow)][f"f{i}"]
                fmts.append((fr, 10 ** (db / 20.0), bw))
            fname = f"Sing_{voice.capitalize()}_{vow.upper()}.json"
            f1r = emit(fname, f0, fmts)
            manifest.append((fname, f"Sing_{voice.capitalize()}", vow, f0,
                             nearest_midi(f0), fmts, f1r))

    entries = []
    for m in manifest:
        fmts = [{"freq": round(f, 1), "gain": round(g, 4),
                 "engine_width": round(bw * WIDTH_MULT, 1)}
                for f, g, bw in m[5]]
        e = {"file": m[0], "group": m[1], "vowel": m[2],
             "f0_nominal": round(m[3], 1), "midi": m[4],
             "f0_rendered": round(midi_freq(m[4]), 2),
             "formants": fmts}
        if m[6]:
            # Manifest carries the EFFECTIVE F1 at the rendered note (what the
            # engine curve delivers at note-on) so the verifier's prediction
            # matches the render; table value kept as table_freq.
            curve = f1_retune_curve(m[5][0][0])
            eff = curve_eval(curve, midi_freq(m[4]))
            fmts[0]["table_freq"] = fmts[0]["freq"]
            fmts[0]["freq"] = round(eff, 1)
            e["f1_retune"] = True
        entries.append(e)
    with open(os.path.join(OUT, "_manifest.json"), "w") as fp:
        json.dump(entries, fp, indent=1)
    print(f"total {len(manifest)} patches"
          + (f" ({sum(1 for m in manifest if m[6])} f1-retuned)" if retune else ""))


if __name__ == "__main__":
    main()
