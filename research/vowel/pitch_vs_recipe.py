"""2x2 control: is the soprano E/I collapse the RECIPE or the PITCH?

harmonic_sampling.py shows E-I separability falling 24.2 -> 18.9 -> 12.0 ->
10.7 dB as f0 goes 110 -> 165 -> 220 -> 440. That is consistent with two very
different stories:

  (a) the soprano E/I recipes are simply worse than the alto ones, or
  (b) the harmonic comb at 440 Hz cannot resolve them no matter the recipe.

They are separated by crossing recipe with pitch. Render each of the four
recipes {alto E, alto I, soprano E, soprano I} at BOTH f0 = 220 and f0 = 440
and measure E-I distance in each cell:

  if (a): the alto recipe stays separable at 440    -> fixable by tuning
  if (b): every recipe collapses at 440             -> a sampling limit

Only the pitch is changed; formants are untouched. NOTE: these patches drive
pitch from `score[].note` through `instrument.paramMap.frequency`, so setting
`src.frequency` alone does NOTHING -- the score note wins. Both are set here.

Usage:  python research/vowel/pitch_vs_recipe.py
"""
import json
import pathlib
import shutil
import subprocess
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from harmonic_sampling import (harmonic_levels, pattern_distance,  # noqa: E402
                               read_wav_mono)

ROOT = pathlib.Path(__file__).resolve().parents[2]
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
PATCH_OUT = ROOT / "patches" / "sweep" / "vowel_pitch_control"
REND_OUT = ROOT / "renders" / "sweep" / "vowel_pitch_control"

RECIPES = {
    "altoE": "patches/vowel_tweak/Sing_Alto_E_v1_open.json",       # locked winner
    "altoI": "patches/vowel_tweak/Sing_Alto_I_v2_f2up.json",       # locked winner
    "sopE": "patches/vowel_tweak/Sing_Soprano_E_v0_grid.json",
    "sopI": "patches/vowel_tweak/Sing_Soprano_I_v1_highF2.json",   # pass-1 winner
}
PITCHES = [220.0, 440.0]
# A3 = 220 Hz, A4 = 440 Hz. The score note is what actually sets pitch.
NOTE_FOR_F0 = {220.0: 57, 440.0: 69}


def retune(src_rel, f0, dest):
    p = json.loads((ROOT / src_rel).read_text())
    nodes = {n["id"]: n for n in p["graph"]["nodes"]}
    nodes["src"]["params"]["frequency"] = f0          # cosmetic; paramMap wins
    note = NOTE_FOR_F0[f0]
    if not p.get("score"):
        raise ValueError(f"{src_rel}: no score to retune")
    for ev in p["score"]:
        ev["note"] = note
    p["_provenance"] = {"source": src_rel, "retuned_f0": f0, "note": note,
                        "purpose": "pitch-vs-recipe control, dsp run 25"}
    dest.write_text(json.dumps(p, indent=2) + "\n")


def comb_spacing_is(wav, f0):
    """Confirm the render's harmonic comb really is spaced at f0.

    Peak-picking the fundamental is unreliable here: a vowel patch can leave h1
    formant-suppressed, and adjacent-peak spacing reads 2*f0 whenever a weak
    partial is skipped. Instead compare energy ON the k*f0 comb against energy
    on the HALF-OFFSET comb (k+0.5)*f0. If the true fundamental were 2*f0, the
    odd multiples of f0 would sit in the gaps and this ratio collapses.

    Returns (ok, on_comb_db_over_off_comb).
    """
    x, sr = read_wav_mono(wav)
    seg = x[sr // 2: sr // 2 + sr]
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    freqs = np.fft.rfftfreq(len(seg), 1.0 / sr)
    df = freqs[1] - freqs[0]

    def comb_energy(mults):
        tot = 0.0
        for m in mults:
            f = m * f0
            if f >= sr / 2:
                continue
            c = int(round(f / df))
            tot += float(spec[max(0, c - 2):c + 3].max()) ** 2
        return tot

    odd = comb_energy([1, 3, 5, 7, 9, 11])
    half = comb_energy([1.5, 2.5, 3.5, 4.5, 5.5, 6.5])
    ratio_db = 10.0 * np.log10(max(odd, 1e-20) / max(half, 1e-20))
    return ratio_db > 12.0, ratio_db


def main():
    if not CLI.exists():
        print(f"missing CLI: {CLI}", file=sys.stderr)
        return 1
    for d in (PATCH_OUT, REND_OUT):
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)

    levels = {}
    for name, src in RECIPES.items():
        for f0 in PITCHES:
            tag = f"{name}_f{int(f0)}"
            patch = PATCH_OUT / f"{tag}.json"
            wav = REND_OUT / f"{tag}.wav"
            retune(src, f0, patch)
            r = subprocess.run([str(CLI), str(patch), str(wav)],
                               capture_output=True, text=True)
            if r.returncode != 0 or not wav.exists():
                print(f"RENDER FAILED {tag}\n{r.stdout}\n{r.stderr}",
                      file=sys.stderr)
                return 1
            ok, ratio_db = comb_spacing_is(wav, f0)
            if not ok:
                print(f"  RETUNE DID NOT TAKE: {tag} asked f0={f0:.0f} Hz but "
                      f"the odd-harmonic comb is only {ratio_db:.1f} dB above "
                      f"the half-offset comb", file=sys.stderr)
                return 1
            levels[(name, f0)] = harmonic_levels(wav, f0)
            print(f"  rendered {tag}  (comb at f0 confirmed, "
                  f"{ratio_db:5.1f} dB over half-offset)")

    print()
    print("=" * 70)
    print("E-vs-I harmonic-pattern distance, recipe x pitch")
    print("=" * 70)
    print(f"  {'recipe pair':22s} {'f0=220':>10s} {'f0=440':>10s}")
    rows = {}
    for pair, (e, i) in {"alto E/I": ("altoE", "altoI"),
                         "soprano E/I": ("sopE", "sopI")}.items():
        vals = []
        for f0 in PITCHES:
            vals.append(pattern_distance(levels[(e, f0)], levels[(i, f0)]))
        rows[pair] = vals
        print(f"  {pair:22s} {vals[0]:9.2f} dB {vals[1]:9.2f} dB")

    print()
    alto220, alto440 = rows["alto E/I"]
    sop220, sop440 = rows["soprano E/I"]
    print(f"  alto recipe loses {alto220 - alto440:5.2f} dB of E/I contrast "
          f"when moved 220 -> 440")
    print(f"  soprano recipe gains {sop220 - sop440:+5.2f} dB "
          f"(220 minus 440) -- i.e. it is {'better' if sop220 > sop440 else 'no better'} "
          f"at alto pitch too")
    print()
    if alto440 < alto220 * 0.7 and sop220 > sop440:
        print("  VERDICT: both recipes lose contrast at 440 and both keep it at")
        print("           220 -> the limit is the PITCH (harmonic sampling),")
        print("           not the formant recipe.")
    elif alto440 >= alto220 * 0.7:
        print("  VERDICT: the alto recipe SURVIVES 440 -> the soprano recipe is")
        print("           the problem and is worth re-tuning.")
    else:
        print("  VERDICT: mixed -- read the numbers above.")

    out = ROOT / "out" / "vowel_pitch_control.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(
        {"distances": {k: {"f220": v[0], "f440": v[1]} for k, v in rows.items()},
         "recipes": RECIPES}, indent=2))
    print(f"\nwrote {out.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
