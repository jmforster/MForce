"""gen_excite1.py — excitation phase, round 1: candidates into a pitched string.

Per Matt (2026-08-23): pitched string first. Harness = SegmentSource
(candidate waveform, oneShot) -> KSPianoString with the afp31_gt damper and
t60 keytrack curve (proven note-contained). Candidates = the excitation list
from docs/research/oneshot_sweep/ROUND1_VERDICTS.md, rebuilt with the same
seeds the auditions used, normalized to peak 0.9. Score: notes 36/60/84
(low/mid/high), velocity 0.85, 3.5 s each. Auto-level: render, then rescale
instrument.volume once if the WAV peak is outside [0.15, 0.75].

Output: patches/audition/excite1/*.json + renders/dsp/pending/excite1/*.wav
"""
import json, subprocess
from pathlib import Path
import numpy as np
from scipy.io import wavfile

import gen_segment_sweep as g1
from gen_segment_sweep import cluster, scrape
from gen_segment_sweep2 import seq, thump, scrape2, bounce2, jagged, dots2

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
PD = ROOT / "patches/audition/excite1"
RD = ROOT / "renders/dsp/pending/excite1"

DAMPER = {"stages": [
    {"startVal": 0.0, "endVal": 0.0, "type": "Linear", "percent": 0.0, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0},
    {"startVal": 0.0, "endVal": 1.0, "type": "Linear", "percent": 0.5, "minSec": 0.196, "maxSec": 0.196, "holdPct": 0.0, "power": 0.0},
    {"startVal": 1.0, "endVal": 1.0, "type": "Linear", "percent": 0.5, "minSec": 0.0, "maxSec": 0.25, "holdPct": 0.0, "power": 0.0}]}

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

# --- candidates, same constructions/seeds as the auditioned cells ---
def candidates():
    c = {}
    th_s = thump(0.02, 1.0); th_f = thump(0.035, 1.0, "tri")
    c["thump_cluster_tailoff"] = sample_values(seq(th_s, 0.015, cluster(6, 0.002, 0.03, "tailoff", 0.0, "grow", "tri")))
    c["fatthump_cluster_tailoff"] = sample_values(seq(th_f, 0.02, cluster(8, 0.002, 0.024, "tailoff", 0.1, "grow", "tri")))
    c["cluster_dense_thump"] = sample_values(seq(cluster(12, 0.0015, 0.014, "leadin", 0.25, "shrink", "tri"), 0.006, th_s))
    c["both_shrink_then_grow"] = sample_values(cluster(12, 0.002, 0.026, "both", 0.0, "shrink", "tri"))
    c["reverse_full_faster"] = sample_values(bounce2(0.18 * 0.3, 0.82, 0.78, 0.008, reverse=True, min_int=0.004 * 0.3))
    c["reverse_full_fastest"] = sample_values(bounce2(0.18 * 0.15, 0.82, 0.78, 0.008, reverse=True, min_int=0.004 * 0.15))
    c["med_twocomp"] = sample_values(bounce2(0.12 * 0.6, 0.78, 0.75, 0.008, two_comp=True, min_int=0.004 * 0.6))
    c["dots_grow_boost"] = pair_values(dots2(0.25, 2, 1600, "grow", boost=0.6, seed=886), 0.5)
    c["dots_halfhump_6k"] = pair_values(dots2(0.20, 8, 1600, "halfhump", seed=800), 0.5)
    c["jagged_in_white"] = sample_values(jagged(0.30, 0.0, 1.0, "white", 2500, 1.0, seed=600))
    c["jagged_out_coarse"] = sample_values(jagged(0.0, 0.60, 1.0, "coarse", 900, 1.5, seed=605))
    c["scrape_buzz"] = sample_values(scrape2(0.30, 90, 0.10, 0.0008, peak=0.5, seed=100))
    c["combo_ref"] = sample_values(g1.place([(0, g1.atom(0.025, 0.9, "tri")), (0, g1.atom(0.0008, 1.0, "sharp", 2.0))], tail=0))
    return c

def patch(name, values, sm, volume):
    return {
        "sampleRate": 48000,
        "graph": {"nodes": [
            {"id": "seg", "type": "SegmentSource",
             "params": {"seed": 42, "values": values, "amplitude": 1.0, "smoothness": sm,
                        "widthVarPct": 0.0, "valVarPct": 0.0, "gap": 0.0, "gapVarPct": 0.0,
                        "oneShot": True}},
            {"id": "env_damper", "type": "Envelope", "params": DAMPER},
            {"id": "string", "type": "KSPianoString",
             "params": {"source": {"ref": "seg"}, "frequency": 220.0, "numCombs": 3,
                        "detune": 0.5, "t60": 10.0, "brightness": 0.926, "exciteGain": 1.0,
                        "direct": 0.0, "dispersion": 0.12, "inharmGain": 0.0, "inharmFb": 0.0,
                        "inharmHp": 150.0, "ap1": 0.0, "ap2": 0.1, "ap3": 0.2, "fbCoeff": 0.0,
                        "releaseFb": 0.8, "damperNoise": 0.0, "damper": {"ref": "env_damper"}}}],
            "output": "string"},
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

def main():
    PD.mkdir(parents=True, exist_ok=True); RD.mkdir(parents=True, exist_ok=True)
    for name, (values, sm) in candidates().items():
        # The instrument output limiter pins at 0.7 — a 0.700 peak means
        # CLIPPED, not leveled. Walk volume down until unpinned, then scale
        # to a 0.45 target.
        vol = 2.0
        pp = PD / f"{name}.json"; wp = RD / f"{name}.wav"
        pk = None
        for _ in range(5):
            pp.write_text(json.dumps(patch(name, values, sm, round(vol, 3)), indent=1), encoding="utf-8")
            pk = render(pp, wp)
            if pk is None: break
            if pk >= 0.69: vol *= 0.4; continue
            if not (0.2 <= pk <= 0.6) and pk > 0: vol *= 0.45 / pk; continue
            break
        if pk is None: print(name, "FAIL"); continue
        print(f"{name:26s} peak {pk:.3f} vol {vol:.3f} values {len(values)//2}")

if __name__ == "__main__":
    main()
