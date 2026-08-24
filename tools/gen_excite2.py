"""gen_excite2.py — excitation phase, round 2 prep: SUSTAINED (gated) excitation.

The bow experiment. Candidate textures loop (SegmentSource oneShot=false)
with widthVarPct/valVarPct breaking loop periodicity (each pass
re-randomizes), and an Envelope drives seg.amplitude over the note —
rise / hold (expand) / settle as fractions of duration. Same string harness
as excite1 (afp31 damper + t60 keytrack) for comparability. Two cells
compose a one-shot creak LEAD-IN (fast reverse bounce) summed with a
sustained bed — the string-attack idea end to end.

Built ahead of the full excite-r1 verdicts at Matt's go; a probe set the
verdicts will re-aim.

Output: patches/audition/excite2_sustain/ + renders/dsp/pending/excite2_sustain/
"""
import json, subprocess
from pathlib import Path
import numpy as np
from scipy.io import wavfile

import gen_segment_sweep as g1
from gen_segment_sweep import cluster
from gen_segment_sweep2 import scrape2, jagged, dots2, bounce2

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
PD = ROOT / "patches/audition/excite2_sustain"
RD = ROOT / "renders/dsp/pending/excite2_sustain"

DAMPER = {"stages": [
    {"startVal": 0.0, "endVal": 0.0, "type": "Linear", "percent": 0.0, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0},
    {"startVal": 0.0, "endVal": 1.0, "type": "Linear", "percent": 0.5, "minSec": 0.196, "maxSec": 0.196, "holdPct": 0.0, "power": 0.0},
    {"startVal": 1.0, "endVal": 1.0, "type": "Linear", "percent": 0.5, "minSec": 0.0, "maxSec": 0.25, "holdPct": 0.0, "power": 0.0}]}

# bow-pressure envelope: rise, hold (expand stage), settle — fractions of note
BOW_ENV = {"stages": [
    {"startVal": 0.0, "endVal": 1.0, "type": "Sine", "percent": 0.08, "minSec": 0.02, "maxSec": 0.6, "holdPct": 0.0, "power": 0.0},
    {"startVal": 1.0, "endVal": 1.0, "type": "Linear", "percent": 0.0, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0},
    {"startVal": 1.0, "endVal": 0.0, "type": "Sine", "percent": 0.18, "minSec": 0.05, "maxSec": 1.0, "holdPct": 0.0, "power": 0.0}]}

def trim(buf):
    nz = np.nonzero(np.abs(buf) > 1e-6)[0]
    return buf[: int(nz[-1]) + 1] if len(nz) else buf[:1]

def sample_values(buf):
    b = trim(np.asarray(buf, dtype=np.float32))
    b = b / max(np.abs(b).max(), 1e-9) * 0.9
    out = []
    for v in b: out += [1, round(float(v), 5)]
    return out, 0.5

def pair_values(pairs, sm):
    vmax = max((abs(v) for _, v in pairs), default=1.0) or 1.0
    out = []
    for w, v in pairs: out += [int(w), round(float(v) / vmax * 0.9, 5)]
    return out, sm

def candidates():
    c = {}
    c["scrape_buzz_bed"] = sample_values(scrape2(0.30, 90, 0.10, 0.0008, peak=0.5, seed=100))
    c["scrape_fine_bed"] = sample_values(scrape2(0.25, 400, 0.3, 0.0003, peak=0.4, seed=101))
    c["cluster_tailoff_bed"] = sample_values(cluster(8, 0.002, 0.024, "tailoff", 0.1, "grow", "tri"))
    c["shrinkgrow_bed"] = sample_values(cluster(12, 0.002, 0.026, "both", 0.0, "shrink", "tri"))
    c["jagged_out_bed"] = sample_values(jagged(0.0, 0.40, 1.0, "coarse", 900, 1.5, seed=605))
    c["dots_coarse_wander"] = pair_values(dots2(0.40, 300, 300, "fixed", cont=0.9, seed=890), 1.0)
    c["crunch_bed"] = sample_values(g1.crunch(2, 0.09, 0.05, 700, seed=901))
    return c

def patch(values, sm, volume, lead=None):
    nodes = [
        {"id": "bow_env", "type": "Envelope", "params": BOW_ENV},
        {"id": "seg", "type": "SegmentSource",
         "params": {"seed": 42, "values": values, "amplitude": {"ref": "bow_env"},
                    "smoothness": sm, "widthVarPct": 0.15, "valVarPct": 0.15,
                    "gap": 0.0, "gapVarPct": 0.0, "oneShot": False}}]
    src = "seg"
    if lead is not None:
        lvals, lsm = lead
        nodes.append({"id": "lead", "type": "SegmentSource",
                      "params": {"seed": 43, "values": lvals, "amplitude": 1.0, "smoothness": lsm,
                                 "widthVarPct": 0.0, "valVarPct": 0.0, "gap": 0.0, "gapVarPct": 0.0,
                                 "oneShot": True}})
        nodes.append({"id": "mixsum", "type": "CombinedSource",
                      "params": {"source1": {"ref": "lead"}, "source2": {"ref": "seg"}, "operation": "sum"}})
        src = "mixsum"
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

def emit(name, values, sm, lead=None):
    vol, pk = 1.0, None
    pp = PD / f"{name}.json"; wp = RD / f"{name}.wav"
    for _ in range(5):
        pp.write_text(json.dumps(patch(values, sm, round(vol, 3), lead), indent=1), encoding="utf-8")
        pk = render(pp, wp)
        if pk is None: break
        if pk >= 0.69: vol *= 0.4; continue
        if not (0.2 <= pk <= 0.6) and pk > 0: vol *= 0.45 / pk; continue
        break
    print(f"{name:24s}", "FAIL" if pk is None else f"peak {pk:.3f} vol {vol:.3f}")

def main():
    PD.mkdir(parents=True, exist_ok=True); RD.mkdir(parents=True, exist_ok=True)
    for name, (values, sm) in candidates().items():
        emit(name, values, sm)
    creak = sample_values(bounce2(0.18 * 0.15, 0.82, 0.78, 0.008, reverse=True, min_int=0.004 * 0.15))
    bed = sample_values(scrape2(0.30, 90, 0.10, 0.0008, peak=0.5, seed=100))
    emit("creak_into_buzz_bed", bed[0], bed[1], lead=creak)
    bed2 = sample_values(cluster(8, 0.002, 0.024, "tailoff", 0.1, "grow", "tri"))
    emit("creak_into_cluster_bed", bed2[0], bed2[1], lead=creak)

if __name__ == "__main__":
    main()
