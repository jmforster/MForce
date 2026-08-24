"""gen_excite3.py — excitation phase, round 3: the TRIPTYCH (bow without zither).

From Matt's REVIEW-47 verdict: the loop rhythm ("zither/mandolin picking")
is CONTOUR INSIDE THE LOOP — every excite2 bed was a repurposed one-shot, so
each pass restarted through an onset. Fix, per his attack/sustain(/release)
proposal:
  - ATTACK: one-shot SegmentSource (creak / chaotic scrape / jagged-in),
    faded out by a fixed-seconds envelope (hold 0.15 s, sine fall 0.25 s);
  - SUSTAIN: a STATIONARY bed — uniform statistics end to end, ~1.2 s per
    pass, seam at zero, looping with varPct 0.12 so the pass length wobbles
    and passes never repeat — rising under the attack via the bow envelope
    (the overlap IS the crossfade);
  - RELEASE (two cells): a third seg gated by an envelope silent until 78%
    of the note, swelling as the bow settles.
Summed by chained CombinedSources into the same KS string harness as
excite1/2. 3 attacks x 3 beds + 2 release cells = 11.

Output: patches/audition/excite3_triptych/ + renders/dsp/pending/excite3_triptych/
"""
import json, subprocess
from pathlib import Path
import numpy as np
from scipy.io import wavfile

import gen_segment_sweep as g1
from gen_segment_sweep import atom, place
from gen_segment_sweep2 import bounce2, jagged, scrape

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
PD = ROOT / "patches/audition/excite3_triptych"
RD = ROOT / "renders/dsp/pending/excite3_triptych"
SR = 48000

DAMPER = {"stages": [
    {"startVal": 0.0, "endVal": 0.0, "type": "Linear", "percent": 0.0, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0},
    {"startVal": 0.0, "endVal": 1.0, "type": "Linear", "percent": 0.5, "minSec": 0.196, "maxSec": 0.196, "holdPct": 0.0, "power": 0.0},
    {"startVal": 1.0, "endVal": 1.0, "type": "Linear", "percent": 0.5, "minSec": 0.0, "maxSec": 0.25, "holdPct": 0.0, "power": 0.0}]}

BOW_ENV = {"stages": [
    {"startVal": 0.0, "endVal": 1.0, "type": "Sine", "percent": 0.08, "minSec": 0.02, "maxSec": 0.6, "holdPct": 0.0, "power": 0.0},
    {"startVal": 1.0, "endVal": 1.0, "type": "Linear", "percent": 0.0, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0},
    {"startVal": 1.0, "endVal": 0.0, "type": "Sine", "percent": 0.18, "minSec": 0.05, "maxSec": 1.0, "holdPct": 0.0, "power": 0.0}]}

ATTACK_ENV = {"timeMode": "seconds", "stages": [
    {"startVal": 1.0, "endVal": 1.0, "type": "Linear", "percent": 0.15, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0},
    {"startVal": 1.0, "endVal": 0.0, "type": "Sine", "percent": 0.25, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0},
    {"startVal": 0.0, "endVal": 0.0, "type": "Linear", "percent": 0.0, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0}]}

RELEASE_ENV = {"stages": [
    {"startVal": 0.0, "endVal": 0.0, "type": "Linear", "percent": 0.78, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0},
    {"startVal": 0.0, "endVal": 1.0, "type": "Sine", "percent": 0.08, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0},
    {"startVal": 1.0, "endVal": 0.0, "type": "Sine", "percent": 0.14, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0},
    {"startVal": 0.0, "endVal": 0.0, "type": "Linear", "percent": 0.0, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0}]}

def trim(buf):
    nz = np.nonzero(np.abs(buf) > 1e-6)[0]
    return buf[: int(nz[-1]) + 1] if len(nz) else buf[:1]

def sample_values(buf):
    b = trim(np.asarray(buf, dtype=np.float32))
    b = b / max(np.abs(b).max(), 1e-9) * 0.9
    out = []
    for v in b: out += [1, round(float(v), 5)]
    return out

# ---- STATIONARY beds: uniform statistics, no contour, seam at zero ----
def slip_train(dur_s, rate, cv, slip_w, peak, seed, power=1.2):
    r = np.random.default_rng(seed)
    ev, t, sign = [], 0.0, 1.0
    while t < dur_s:
        sign = -sign
        ev.append((t, atom(slip_w, peak * r.uniform(0.7, 1.3), "saw", power) * sign))
        t += max((1.0 / rate) * (1 + cv * r.normal()), 2.0 / SR)
    return place(ev, tail=0)

def velvet_train(dur_s, density, peak, seed):
    r = np.random.default_rng(seed)
    ev, t = [], 0.0
    while t < dur_s:
        sign = 1.0 if r.random() < 0.5 else -1.0
        ev.append((t, atom(0.0004, peak * r.uniform(0.5, 1.0), "sharp", 1.5) * sign))
        t += r.exponential(1.0 / density)
    return place(ev, tail=0)

BEDS = {
    "buzz":   lambda: slip_train(1.2, 90, 0.10, 0.0008, 0.5, seed=910),
    "fine":   lambda: slip_train(1.2, 400, 0.30, 0.0003, 0.4, seed=911),
    "velvet": lambda: velvet_train(1.2, 150, 0.5, seed=912),
}
ATTACKS = {
    "creak":    lambda: bounce2(0.18 * 0.15, 0.82, 0.78, 0.008, reverse=True, min_int=0.004 * 0.15),
    "chaos":    lambda: scrape(0.25, 140, "accel", peak=0.45, grain="exp", seed=920),
    "jaggedin": lambda: jagged(0.30, 0.0, 1.0, "white", 2500, 1.0, seed=921),
}
REL = lambda: slip_train(0.5, 250, 0.4, 0.0004, 0.35, seed=930)

def patch(att_vals, bed_vals, volume, rel_vals=None):
    nodes = [
        {"id": "att_env", "type": "Envelope", "params": ATTACK_ENV},
        {"id": "att", "type": "SegmentSource",
         "params": {"seed": 41, "values": att_vals, "amplitude": {"ref": "att_env"}, "smoothness": 0.5,
                    "widthVarPct": 0.0, "valVarPct": 0.0, "gap": 0.0, "gapVarPct": 0.0, "oneShot": True}},
        {"id": "bow_env", "type": "Envelope", "params": BOW_ENV},
        {"id": "bed", "type": "SegmentSource",
         "params": {"seed": 42, "values": bed_vals, "amplitude": {"ref": "bow_env"}, "smoothness": 0.5,
                    "widthVarPct": 0.12, "valVarPct": 0.12, "gap": 0.0, "gapVarPct": 0.0, "oneShot": False}},
        {"id": "sum1", "type": "CombinedSource",
         "params": {"source1": {"ref": "att"}, "source2": {"ref": "bed"}, "operation": "sum"}}]
    src = "sum1"
    if rel_vals is not None:
        nodes += [
            {"id": "rel_env", "type": "Envelope", "params": RELEASE_ENV},
            {"id": "rel", "type": "SegmentSource",
             "params": {"seed": 43, "values": rel_vals, "amplitude": {"ref": "rel_env"}, "smoothness": 0.5,
                        "widthVarPct": 0.15, "valVarPct": 0.15, "gap": 0.0, "gapVarPct": 0.0, "oneShot": False}},
            {"id": "sum2", "type": "CombinedSource",
             "params": {"source1": {"ref": "sum1"}, "source2": {"ref": "rel"}, "operation": "sum"}}]
        src = "sum2"
    nodes += [
        {"id": "env_damper", "type": "Envelope", "params": DAMPER},
        {"id": "string", "type": "KSPianoString",
         "params": {"source": {"ref": src}, "frequency": 220.0, "numCombs": 3,
                    "detune": 0.5, "t60": 10.0, "brightness": 0.926, "exciteGain": 1.0,
                    "direct": 0.0, "dispersion": 0.12, "inharmGain": 0.0, "inharmFb": 0.0,
                    "inharmHp": 150.0, "ap1": 0.0, "ap2": 0.1, "ap3": 0.2, "fbCoeff": 0.0,
                    "releaseFb": 0.8, "damperNoise": 0.0, "damper": {"ref": "env_damper"}}}]
    return {"sampleRate": 48000, "graph": {"nodes": nodes, "output": "string"},
            "instrument": {"polyphony": 4, "volume": volume,
                           "paramMap": {"frequency": ["string.frequency",
                               {"target": "string.t60", "curve": [[20.0, 345.0], [16000.0, 0.431]], "interp": "loglog"}]}},
            "score": [{"note": 36, "velocity": 0.85, "time": 0.0, "duration": 3.5},
                      {"note": 60, "velocity": 0.85, "time": 4.0, "duration": 3.5},
                      {"note": 84, "velocity": 0.85, "time": 8.0, "duration": 3.5}]}

def render(pp, wp):
    r = subprocess.run([str(CLI), str(pp), str(wp)], capture_output=True, text=True)
    if r.returncode != 0: return None
    sr, d = wavfile.read(wp)
    if d.dtype.kind in "iu": d = d.astype(np.float32) / np.iinfo(d.dtype).max
    return float(np.abs(d).max())

def emit(name, att_vals, bed_vals, rel_vals=None):
    vol, pk = 1.0, None
    pp = PD / f"{name}.json"; wp = RD / f"{name}.wav"
    for _ in range(5):
        pp.write_text(json.dumps(patch(att_vals, bed_vals, round(vol, 3), rel_vals), indent=1), encoding="utf-8")
        pk = render(pp, wp)
        if pk is None: break
        if pk >= 0.69: vol *= 0.4; continue
        if not (0.2 <= pk <= 0.6) and pk > 0: vol *= 0.45 / pk; continue
        break
    print(f"{name:22s}", "FAIL" if pk is None else f"peak {pk:.3f} vol {vol:.3f}")

def main():
    PD.mkdir(parents=True, exist_ok=True); RD.mkdir(parents=True, exist_ok=True)
    beds = {k: sample_values(f()) for k, f in BEDS.items()}
    atts = {k: sample_values(f()) for k, f in ATTACKS.items()}
    for an, av in atts.items():
        for bn, bv in beds.items():
            emit(f"{an}__{bn}", av, bv)
    rel = sample_values(REL())
    emit("creak__buzz__rel", atts["creak"], beds["buzz"], rel)
    emit("jaggedin__fine__rel", atts["jaggedin"], beds["fine"], rel)

if __name__ == "__main__":
    main()
