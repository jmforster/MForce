"""gen_excite4.py — excitation phase, round 4: smoothing the bed.

Matt's REVIEW-48 verdict: triptych periodicity much reduced ("in some almost
not at all") but "the bed phase is too noisy in every case... just use a
sawtooth? ;-)". The wink is the physics — a bowed string's steady state IS
the sawtooth (Helmholtz motion, slips locked to the string period), and our
beds slip at a fixed rate regardless of pitch. Three stacked fixes:

  A. DUCK the bed after the attack — bow env sustains at 0.3-0.5 instead of
     1.0; the string's ring carries the tone, the bed just feeds it.
  B. SMOOTH the bed — wide soft slips (4 ms, merging) at smoothness 1.0.
  C. HELMHOLTZ layer — a drawn ramp+jag shape fed into WavetableSource's
     inputSource: the table takes exactly one period at the NOTE's frequency
     (truncated ramp = single-period saw), read pitch-locked, bow env on its
     amplitude. The drawn waveform meets pitch tracking; no engine work.

Output: patches/audition/excite4_smooth/ + renders/dsp/pending/excite4_smooth/
"""
import json, subprocess
from pathlib import Path
import numpy as np
from scipy.io import wavfile

from gen_segment_sweep import atom, place
from gen_excite3 import (DAMPER, ATTACK_ENV, trim, sample_values, slip_train,
                         ATTACKS, BEDS)

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
PD = ROOT / "patches/audition/excite4_smooth"
RD = ROOT / "renders/dsp/pending/excite4_smooth"
SR = 48000

def bow_env(sus):
    """Rise to 1 (attack overlap), fall to `sus`, hold there, settle to 0."""
    return {"stages": [
        {"startVal": 0.0, "endVal": 1.0, "type": "Sine", "percent": 0.08, "minSec": 0.02, "maxSec": 0.6, "holdPct": 0.0, "power": 0.0},
        {"startVal": 1.0, "endVal": sus, "type": "Sine", "percent": 0.10, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0},
        {"startVal": sus, "endVal": sus, "type": "Linear", "percent": 0.0, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0},
        {"startVal": sus, "endVal": 0.0, "type": "Sine", "percent": 0.18, "minSec": 0.05, "maxSec": 1.0, "holdPct": 0.0, "power": 0.0}]}

def ramp_jag(dur_s=0.06, jag=0.15, noise_hz=3000, seed=940):
    """One long rising ramp with jag riding on it. WavetableSource's fill
    truncates it to one period at the note frequency -> a single-period saw
    whose roughness is the jag."""
    r = np.random.default_rng(seed)
    n = int(dur_s * SR)
    ramp = np.linspace(-0.9, 0.9, n)
    m = max(2, int(n / (SR / noise_hz)))
    nz = np.interp(np.arange(n), np.linspace(0, n, m + 1), r.uniform(-1, 1, m + 1))
    return (ramp + jag * nz).astype(np.float32)

SMOOTH_BED = lambda: slip_train(1.2, 150, 0.15, 0.004, 0.5, seed=915, power=1.0)

def patch(volume, att_vals, bed=None, bed_sm=0.5, sus=1.0, wt_fill=None, bed_gain=1.0):
    nodes = [
        {"id": "att_env", "type": "Envelope", "params": ATTACK_ENV},
        {"id": "att", "type": "SegmentSource",
         "params": {"seed": 41, "values": att_vals, "amplitude": {"ref": "att_env"}, "smoothness": 0.5,
                    "widthVarPct": 0.0, "valVarPct": 0.0, "gap": 0.0, "gapVarPct": 0.0, "oneShot": True}},
        {"id": "bow_env", "type": "Envelope", "params": bow_env(sus)}]
    parts = ["att"]
    freq_targets = ["string.frequency"]
    if bed is not None:
        bvals = [v if i % 2 == 0 else round(v * bed_gain, 5) for i, v in enumerate(bed)]
        nodes.append({"id": "bed", "type": "SegmentSource",
                      "params": {"seed": 42, "values": bvals, "amplitude": {"ref": "bow_env"},
                                 "smoothness": bed_sm, "widthVarPct": 0.12, "valVarPct": 0.12,
                                 "gap": 0.0, "gapVarPct": 0.0, "oneShot": False}})
        parts.append("bed")
    if wt_fill is not None:
        nodes.append({"id": "fill_seg", "type": "SegmentSource",
                      "params": {"seed": 44, "values": wt_fill, "amplitude": 1.0, "smoothness": 0.5,
                                 "widthVarPct": 0.0, "valVarPct": 0.0, "gap": 0.0, "gapVarPct": 0.0,
                                 "oneShot": True}})
        amp_ref = {"ref": "bow_env"}
        nodes.append({"id": "wt", "type": "WavetableSource",
                      "params": {"inputSource": {"ref": "fill_seg"}, "frequency": 220.0,
                                 "amplitude": amp_ref, "interpolate": True}})
        parts.append("wt")
        freq_targets.append("wt.frequency")
    # chain sums
    src = parts[0]
    for i, p in enumerate(parts[1:], 1):
        sid = f"sum{i}"
        nodes.append({"id": sid, "type": "CombinedSource",
                      "params": {"source1": {"ref": src}, "source2": {"ref": p}, "operation": "sum"}})
        src = sid
    nodes += [
        {"id": "env_damper", "type": "Envelope", "params": DAMPER},
        {"id": "string", "type": "KSPianoString",
         "params": {"source": {"ref": src}, "frequency": 220.0, "numCombs": 3,
                    "detune": 0.5, "t60": 10.0, "brightness": 0.926, "exciteGain": 1.0,
                    "direct": 0.0, "dispersion": 0.12, "inharmGain": 0.0, "inharmFb": 0.0,
                    "inharmHp": 150.0, "ap1": 0.0, "ap2": 0.1, "ap3": 0.2, "fbCoeff": 0.0,
                    "releaseFb": 0.8, "damperNoise": 0.0, "damper": {"ref": "env_damper"}}}]
    pm = freq_targets + [{"target": "string.t60", "curve": [[20.0, 345.0], [16000.0, 0.431]], "interp": "loglog"}]
    return {"sampleRate": 48000, "graph": {"nodes": nodes, "output": "string"},
            "instrument": {"polyphony": 4, "volume": volume, "paramMap": {"frequency": pm}},
            "score": [{"note": 36, "velocity": 0.85, "time": 0.0, "duration": 3.5},
                      {"note": 60, "velocity": 0.85, "time": 4.0, "duration": 3.5},
                      {"note": 84, "velocity": 0.85, "time": 8.0, "duration": 3.5}]}

def render(pp, wp):
    r = subprocess.run([str(CLI), str(pp), str(wp)], capture_output=True, text=True)
    if r.returncode != 0:
        print("   ", (r.stderr or r.stdout).strip().splitlines()[-1:])
        return None
    sr, d = wavfile.read(wp)
    if d.dtype.kind in "iu": d = d.astype(np.float32) / np.iinfo(d.dtype).max
    return float(np.abs(d).max())

def emit(name, **kw):
    vol, pk = 1.0, None
    pp = PD / f"{name}.json"; wp = RD / f"{name}.wav"
    for _ in range(5):
        pp.write_text(json.dumps(patch(round(vol, 3), **kw), indent=1), encoding="utf-8")
        pk = render(pp, wp)
        if pk is None: break
        if pk >= 0.69: vol *= 0.4; continue
        if not (0.2 <= pk <= 0.6) and pk > 0: vol *= 0.45 / pk; continue
        break
    print(f"{name:26s}", "FAIL" if pk is None else f"peak {pk:.3f} vol {vol:.3f}")

def main():
    PD.mkdir(parents=True, exist_ok=True); RD.mkdir(parents=True, exist_ok=True)
    creak = sample_values(ATTACKS["creak"]())
    jin = sample_values(ATTACKS["jaggedin"]())
    buzz = sample_values(BEDS["buzz"]())
    fine = sample_values(BEDS["fine"]())
    smooth = sample_values(SMOOTH_BED())
    # A: duck the bed
    emit("creak__buzz_sus050", att_vals=creak, bed=buzz, sus=0.5)
    emit("creak__buzz_sus030", att_vals=creak, bed=buzz, sus=0.3)
    emit("jaggedin__fine_sus030", att_vals=jin, bed=fine, sus=0.3)
    # B: smooth bed (isolated at full sustain, and combined with ducking)
    emit("creak__smooth_sus100", att_vals=creak, bed=smooth, bed_sm=1.0, sus=1.0)
    emit("creak__smooth_sus040", att_vals=creak, bed=smooth, bed_sm=1.0, sus=0.4)
    emit("jaggedin__smooth_sus040", att_vals=jin, bed=smooth, bed_sm=1.0, sus=0.4)
    # C: Helmholtz layer — pitch-locked drawn saw via WavetableSource
    emit("creak__wtsaw_jag15", att_vals=creak, wt_fill=sample_values(ramp_jag(jag=0.15)), sus=0.6)
    emit("creak__wtsaw_jag40", att_vals=creak, wt_fill=sample_values(ramp_jag(jag=0.40)), sus=0.6)
    emit("creak__wtsaw15_bedlow", att_vals=creak, bed=buzz, bed_gain=0.25, sus=0.4,
         wt_fill=sample_values(ramp_jag(jag=0.15)))

if __name__ == "__main__":
    main()
