"""Soprano E/I/U candidates derived from the pitch-vs-recipe control.

Matt rejected the whole pass-2 soprano set (dsp/REVIEW.md item 23). The 2x2 in
pitch_vs_recipe.py says why the soprano recipes are weak and, more usefully,
that there is still ~7 dB of recipe headroom at 440 Hz:

    E/I harmonic-pattern distance     f0=220     f0=440
      alto formant recipe             27.05 dB   16.90 dB
      soprano formant recipe          18.45 dB   10.20 dB

Both recipes lose ~10 / ~8 dB when the pitch is raised -- that part is the
harmonic comb and no recipe fixes it. But the ALTO formants, left where they
are and simply sung at 440, keep 16.90 dB where the soprano recipe keeps
10.20. So the soprano recipe is giving away 6.7 dB it does not have to.

This is physically the right move and it is NOT what pass 2's `e3_altoXpose`
did. That variant TRANSPOSED the alto formants up by 2x along with the pitch,
which is wrong -- a singer's vocal tract does not shrink when she goes up an
octave, formant frequencies are a property of the tract, not the note. Matt
heard the result as "a pennywhistle", which is what scaling formants with f0
sounds like. Here the formants stay put and only the note moves.

Builds, for E / I / U:
  <v>_cur   the current soprano winner (control)
  <v>_altf  the alto formant recipe, unchanged, sung at 440

Usage:  python research/vowel/soprano_candidates.py
"""
import json
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from harmonic_sampling import harmonic_levels, pattern_distance  # noqa: E402
from pitch_vs_recipe import comb_spacing_is  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
PATCH_OUT = ROOT / "patches" / "pending" / "vowel_soprano_alt"
REND_OUT = ROOT / "renders" / "pending" / "vowel_soprano_alt"

SOPRANO_NOTE = 69      # A4 = 440 Hz
F0 = 440.0

# vowel -> (current soprano winner, alto donor recipe)
CASES = {
    "E": ("patches/vowel_tweak/Sing_Soprano_E_v0_grid.json",
          "patches/vowel_tweak/Sing_Alto_E_v1_open.json"),
    "I": ("patches/vowel_tweak/Sing_Soprano_I_v1_highF2.json",
          "patches/vowel_tweak/Sing_Alto_I_v2_f2up.json"),
    "U": ("patches/vowel_tweak/Sing_Soprano_U_v2_h2mid.json",
          "patches/vowel_tweak/Sing_Alto_U_v1_speechUW.json"),
}
# O is the family Matt already rates best; carried only as a distance anchor.
O_REF = "patches/vowel_grid/Sing_Soprano_O.json"
O_ALT = "patches/vowel_grid/Sing_Alto_O.json"


def build(src_rel, dest, note, why):
    p = json.loads((ROOT / src_rel).read_text())
    for ev in p.get("score", []):
        ev["note"] = note
    nodes = {n["id"]: n for n in p["graph"]["nodes"]}
    nodes["src"]["params"]["frequency"] = F0        # cosmetic; paramMap wins
    p["_provenance"] = {"source": src_rel, "note": note, "why": why,
                        "run": "dsp run 25 (2026-08-10)"}
    dest.write_text(json.dumps(p, indent=2) + "\n")


def render(patch, wav):
    r = subprocess.run([str(CLI), str(patch), str(wav)],
                       capture_output=True, text=True)
    if r.returncode != 0 or not wav.exists():
        raise RuntimeError(f"render failed {patch.name}\n{r.stdout}\n{r.stderr}")
    ok, ratio = comb_spacing_is(wav, F0)
    if not ok:
        raise RuntimeError(f"{wav.name}: comb is not at {F0} Hz "
                           f"({ratio:.1f} dB over half-offset)")
    return harmonic_levels(wav, F0)


def main():
    for d in (PATCH_OUT, REND_OUT):
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)

    levels = {}
    for v, (cur, alt) in CASES.items():
        if not (ROOT / cur).exists():
            print(f"MISSING {cur}", file=sys.stderr)
            return 1
        for tag, src, why in (
            ("cur", cur, "current soprano winner (control)"),
            ("altf", alt, "alto formant recipe, unchanged, sung at A4"),
        ):
            name = f"soprano_{v}_{tag}"
            patch = PATCH_OUT / f"{name}.json"
            build(src, patch, SOPRANO_NOTE, why)
            levels[(v, tag)] = render(patch, REND_OUT / f"{name}.wav")
            print(f"  {name}")

    if (ROOT / O_REF).exists():
        for tag, src, why in (
            ("cur", O_REF, "O anchor - Matt's best family (control)"),
            ("altf", O_ALT, "alto O formants sung at A4"),
        ):
            patch = PATCH_OUT / f"soprano_O_{tag}.json"
            build(src, patch, SOPRANO_NOTE, why)
            levels[("O", tag)] = render(patch, REND_OUT / f"soprano_O_{tag}.wav")
            print(f"  soprano_O_{tag} (anchor)")

    print()
    print("=" * 66)
    print("Pairwise vowel separability at f0=440, current vs alto-formant")
    print("=" * 66)
    print(f"  {'pair':10s} {'current':>12s} {'alto-formant':>14s} {'delta':>9s}")
    results = {}
    pairs = [("E", "I"), ("E", "U"), ("I", "U"), ("E", "O"), ("I", "O"),
             ("U", "O")]
    for a, b in pairs:
        if (a, "cur") not in levels or (b, "cur") not in levels:
            continue
        cur = pattern_distance(levels[(a, "cur")], levels[(b, "cur")])
        alt = pattern_distance(levels[(a, "altf")], levels[(b, "altf")])
        results[f"{a}-{b}"] = {"current": cur, "alto_formant": alt,
                               "delta": alt - cur}
        print(f"  {a}-{b:8s} {cur:9.2f} dB {alt:11.2f} dB {alt-cur:+8.2f}")

    core = [results[k] for k in ("E-I", "E-U", "I-U") if k in results]
    if core:
        mc = sum(r["current"] for r in core) / len(core)
        ma = sum(r["alto_formant"] for r in core) / len(core)
        print(f"\n  mean over E/I/U pairs: {mc:.2f} -> {ma:.2f} dB "
              f"({ma-mc:+.2f})")

    out = ROOT / "out" / "vowel_soprano_candidates.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({"f0": F0, "pairs": results, "cases": CASES},
                              indent=2))
    print(f"\nwrote {out.relative_to(ROOT)}")
    print(f"renders in {REND_OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
