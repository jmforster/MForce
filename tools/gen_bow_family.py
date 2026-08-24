"""gen_bow_family.py — the bow gets a family (day after THE BOW, 2026-08-24).

Matt's 08-23 discovery: BowedStringEvolution + RedNoiseSource on the bow pin
at UI descriptor defaults = "a pretty fucking nice bowed string." His
parameter notes (ROUND1_VERDICTS.md) are the sweep axes:
  - density down -> "hesitant, tentative character"
  - frequency (RedNoise) -> brightness, as does bow position
  - smoothness / continuity / rampVariation "directly translate"
  - boost + zeroCrossTendency TBD "but all have an impact"

Baseline = exact discovery bytes (patches/baselines/bow_evolution_discovery.json),
one param moved per cell, plus:
  - rnfreq_track: RedNoise.frequency follows the note (the accidental 350 Hz
    is fixed regardless of register; the stochastic-bow reading says events
    near the string period should track it)
  - three combo cells straight from Matt's characterizations (hypotheses only)

Score: the 36/60/84 triple from the excite sweeps, 3.5 s bows.
Output: patches/audition/bow_family/ + renders/dsp/pending/bow_family/
"""
import copy, json, subprocess
from pathlib import Path
import numpy as np
from scipy.io import wavfile

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
BASE = ROOT / "patches/baselines/bow_evolution_discovery.json"
PD = ROOT / "patches/audition/bow_family"
RD = ROOT / "renders/dsp/pending/bow_family"

SCORE = [{"note": n, "velocity": 0.85, "time": t, "duration": 3.5}
         for n, t in ((36, 0.0), (60, 4.0), (84, 8.0))]


def make_patch(volume, rn=None, bow=None, rn_freq_ref=False):
    p = copy.deepcopy(json.loads(BASE.read_text(encoding="utf-8")))
    nodes = {n["id"]: n for n in p["graph"]["nodes"]}
    for k, v in (rn or {}).items():
        nodes["RedNoise"]["params"][k] = v
    if rn_freq_ref:
        nodes["RedNoise"]["params"]["frequency"] = {"ref": "__perf_frequency"}
    for k, v in (bow or {}).items():
        nodes["BowedStringEvolution2"]["params"][k] = v
    p["score"] = SCORE
    p["seconds"] = 12.5
    p["instrument"]["volume"] = volume
    return p


def render(pp, wp):
    r = subprocess.run([str(CLI), str(pp), str(wp)], capture_output=True, text=True)
    if r.returncode != 0:
        print("   ", (r.stderr or r.stdout).strip().splitlines()[-1:])
        return None
    sr, d = wavfile.read(wp)
    if d.dtype.kind in "iu":
        d = d.astype(np.float32) / np.iinfo(d.dtype).max
    return float(np.abs(d).max())


def emit(name, **kw):
    vol, pk = 1.0, None
    pp = PD / f"{name}.json"; wp = RD / f"{name}.wav"
    for _ in range(5):
        pp.write_text(json.dumps(make_patch(round(vol, 3), **kw), indent=1), encoding="utf-8")
        pk = render(pp, wp)
        if pk is None: break
        if pk >= 0.69: vol *= 0.4; continue
        if not (0.2 <= pk <= 0.6) and pk > 0: vol *= 0.45 / pk; continue
        break
    print(f"{name:24s}", "FAIL" if pk is None else f"peak {pk:.3f} vol {vol:.3f}")


def main():
    PD.mkdir(parents=True, exist_ok=True); RD.mkdir(parents=True, exist_ok=True)
    emit("baseline")
    # frequency -> brightness (base 350)
    for f in (100, 200, 700, 1400):
        emit(f"rnfreq{f}", rn={"frequency": float(f)})
    emit("rnfreq_track", rn_freq_ref=True)
    # density -> hesitancy (base 0.5)
    for d in (0.15, 0.3, 0.7, 1.0):
        emit(f"rnden{int(d*100):03d}", rn={"density": d})
    # the "directly translate" trio (bases 0.5 / 0.0 / 0.2)
    for s in (0.1, 0.9):
        emit(f"rnsm{int(s*100):03d}", rn={"smoothness": s})
    for c in (0.4, 0.8):
        emit(f"rncont{int(c*100):03d}", rn={"continuity": c})
    for v in (0.0, 0.6):
        emit(f"rnramp{int(v*100):03d}", rn={"rampVariation": v})
    # the TBD pair (bases 0.0)
    for b in (0.5, 1.0):
        emit(f"rnboost{int(b*100):03d}", rn={"boost": b})
    for z in (0.5, 1.0):
        emit(f"rnzct{int(z*100):03d}", rn={"zeroCrossTendency": z})
    # evolution-side (bases: pos 0.14, speed 0.3, friction 4, loss 0.005)
    for pos in (0.05, 0.25, 0.40):
        emit(f"bowpos{int(pos*100):03d}", bow={"bowPosition": pos})
    # bowSpeed x frictionGain is ONE axis: the Friedlander recursion obeys
    # S(lam*speed, gain) = lam * S(speed, lam*gain) exactly (verified corr=1.0
    # on rendered pairs), so waveform shape depends only on the product
    # ("drive"; baseline 0.3*4 = 1.2) and the leftover is pure gain. Sweep the
    # product via frictionGain alone: fric2 = drive 0.6, fric8 = drive 2.4.
    for g in (2.0, 8.0):
        emit(f"fric{int(g)}", bow={"frictionGain": g})
    for tl in (0.001, 0.02):
        emit(f"loss{int(tl*10000):04d}", bow={"tubeLoss": tl})
    # combos from Matt's characterizations
    emit("combo_hesitant", rn={"density": 0.25, "smoothness": 0.7})
    emit("combo_bright", rn={"frequency": 900.0}, bow={"bowPosition": 0.06})
    emit("combo_darklow", rn={"frequency": 150.0}, bow={"bowPosition": 0.30})


if __name__ == "__main__":
    main()
