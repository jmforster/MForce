"""dsp run 23 — set the hammer-knock level from measurement (REVIEW 16 fix 2).

Run-22 diagnosis: the searched knock was ~150x overweight — real C2 puts
share_real = 2.5-8 kHz band energy / total energy over the first 50 ms
at ~2e-4, the smoke candidate at 0.031. Instead of searching knockLevel,
solve for it:

  E_band(L) = E_band_tonal + L^2 * E_knock_unit      (knock is band-passed
  E_tot(L)  = E_tot_tonal  + L^2 * E_knock_unit       into 2.5-8 kHz, so
                                                      all its energy lands
                                                      in the band)
  share(L) = E_band(L) / E_tot(L) = share_real  =>  solve L.

E_* come from two renders of the current template at C2 (knock level 0 and
a probe level), band energies from the same spectrum-fraction measurement
piano_analysis.attack_analysis used for the real samples. Prints the level
to paste into build_piano_template.KNOCK_LEVEL (then rebuild + verify).
"""
import copy
import json
import os
import subprocess
import sys

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import piano_analysis as pa  # noqa: E402

CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
TEMPLATE = os.path.join(REPO, "patches", "piano1", "template.json")
SCRATCH = os.path.join(HERE, "out", "scratch")
os.makedirs(SCRATCH, exist_ok=True)

BAND = (2500.0, 8000.0)
WIN_S = 0.050
PROBE = 0.05


def render_c2(level, tag):
    p = copy.deepcopy(json.load(open(TEMPLATE)))
    for n in p["graph"]["nodes"]:
        if n["id"] == "knockAmp":
            n["params"]["source2"] = float(level)
    p["score"] = [{"note": 36, "velocity": 0.85, "time": 0.0, "duration": 2.2}]
    pp = os.path.join(SCRATCH, f"knockcal_{tag}.json")
    wp = os.path.join(SCRATCH, f"knockcal_{tag}.wav")
    json.dump(p, open(pp, "w"))
    subprocess.run([CLI, pp, wp], capture_output=True, text=True)
    x, sr = sf.read(wp)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return x.astype(np.float64), sr


def first50_energies(x, sr):
    onset, _ = pa.find_onset(x, sr)
    seg = x[onset: onset + int(WIN_S * sr)]
    spec, fbin = pa.spectrum(seg, sr, pad=1)
    p = spec ** 2
    b = (fbin >= BAND[0]) & (fbin < BAND[1])
    return float(p[b].sum()), float(p.sum())


def real_share():
    x, sr = pa.load_mono("C2")
    return (lambda eb, et: eb / et)(*first50_energies(x, sr))


def main():
    tgt = real_share()
    print(f"real C2 share ({BAND[0]:.0f}-{BAND[1]:.0f} Hz / total, "
          f"first {1000*WIN_S:.0f} ms) = {tgt:.3e}")
    eb0, et0 = first50_energies(*render_c2(0.0, "L0"))
    ebp, etp = first50_energies(*render_c2(PROBE, "probe"))
    e_unit = (ebp - eb0) / PROBE ** 2
    print(f"tonal-only: band {eb0:.3e} / tot {et0:.3e} "
          f"(share {eb0/et0:.3e});  knock unit energy {e_unit:.3e}")
    num = tgt * et0 - eb0
    if num <= 0:
        # The template's TONAL treble already exceeds the real share — that
        # excess belongs to the searched dims (rolloff / env knots), not to
        # the knock. Anchor the knock on its own: give it exactly the band
        # energy the real share implies, so once the optimizer darkens the
        # tonal treble toward real, total share lands at the real order.
        L = float(np.sqrt(tgt * et0 / e_unit))
        print(f"tonal band share {eb0/et0:.3e} already > target "
              f"(search's rolloff/env debt); knock-only solve: "
              f"KNOCK_LEVEL = {L:.5f}")
    else:
        L = float(np.sqrt(num / (e_unit * (1.0 - tgt))))
        print(f"solved KNOCK_LEVEL = {L:.5f}")
    ebv, etv = first50_energies(*render_c2(L, "verify"))
    print(f"verify render at {L:.5f}: share = {ebv/etv:.3e} (target {tgt:.3e})")


if __name__ == "__main__":
    main()
