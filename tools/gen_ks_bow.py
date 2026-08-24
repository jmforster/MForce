"""gen_ks_bow.py — KSString bow mode, first listen (2026-08-24).

The MSW consolidation: BowedStringEvolution's Friedlander junction moved
inside KSString's damped, keytracked, tuned loop (KSPianoString renamed;
bow pin + bowSpeed/frictionGain/bowGain settings). Pressure = the flutter
fix from the conditioning ladder: const 0.4 + HP200 RedNoise at half depth,
gated by a bow envelope. String harness = the excite4 values (t60 keytrack
curve, brightness 0.926, damper).

Output: patches/audition/bow_family/ksbow_*.json +
renders/dsp/pending/bow_family/ksbow_*.wav
"""
import json, subprocess
from pathlib import Path
import numpy as np
from scipy.io import wavfile

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
PD = ROOT / "patches/audition/bow_family"
RD = ROOT / "renders/dsp/pending/bow_family"

BOW_ENV = {"stages": [
    {"startVal": 0.0, "endVal": 1.0, "type": "Sine", "percent": 0.06, "minSec": 0.03, "maxSec": 0.5, "holdPct": 0.0, "power": 0.0},
    {"startVal": 1.0, "endVal": 1.0, "type": "Linear", "percent": 0.0, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0},
    {"startVal": 1.0, "endVal": 0.0, "type": "Sine", "percent": 0.12, "minSec": 0.05, "maxSec": 0.8, "holdPct": 0.0, "power": 0.0}]}

DAMPER = {"stages": [
    {"startVal": 0.0, "endVal": 0.0, "type": "Linear", "percent": 0.0, "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0},
    {"startVal": 0.0, "endVal": 1.0, "type": "Linear", "percent": 0.5, "minSec": 0.196, "maxSec": 0.196, "holdPct": 0.0, "power": 0.0},
    {"startVal": 1.0, "endVal": 1.0, "type": "Linear", "percent": 0.5, "minSec": 0.0, "maxSec": 0.25, "holdPct": 0.0, "power": 0.0}]}


def patch(volume, string_over=None):
    string = {"bow": {"ref": "pressure"}, "frequency": 220.0,
              "numCombs": 3, "detune": 0.5, "t60": 10.0, "brightness": 0.926,
              "exciteGain": 1.0, "direct": 0.0, "dispersion": 0.12,
              "inharmGain": 0.0, "inharmFb": 0.0, "inharmHp": 150.0,
              "ap1": 0.0, "ap2": 0.1, "ap3": 0.2, "fbCoeff": 0.0,
              "releaseFb": 0.8, "damperNoise": 0.0,
              "bowSpeed": 0.3, "frictionGain": 4.0, "bowGain": 1.0,
              "damper": {"ref": "env_damper"}}
    string.update(string_over or {})
    nodes = [
        {"id": "rn", "type": "RedNoiseSource",
         "params": {"frequency": 350.0, "amplitude": 0.5, "density": 0.5,
                    "smoothness": 0.5, "rampVariation": 0.2, "boost": 0.0,
                    "continuity": 0.0, "zeroCrossTendency": 0.0}},
        {"id": "hp", "type": "BWHighpassFilter",
         "params": {"source": {"ref": "rn"}, "cutoffFreq": 200.0}},
        {"id": "cond", "type": "CombinedSource",
         "params": {"source1": {"ref": "hp"}, "source2": 0.4, "operation": "sum"}},
        {"id": "bow_env", "type": "Envelope", "params": BOW_ENV},
        {"id": "pressure", "type": "CombinedSource",
         "params": {"source1": {"ref": "cond"}, "source2": {"ref": "bow_env"},
                    "operation": "multiply"}},
        {"id": "env_damper", "type": "Envelope", "params": DAMPER},
        {"id": "string", "type": "KSString", "params": string}]
    pm = ["string.frequency",
          {"target": "string.t60", "curve": [[20.0, 345.0], [16000.0, 0.431]], "interp": "loglog"}]
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
        pp.write_text(json.dumps(patch(round(vol, 4), **kw), indent=1), encoding="utf-8")
        pk = render(pp, wp)
        if pk is None: break
        if pk >= 0.69: vol *= 0.4; continue
        if not (0.2 <= pk <= 0.6) and pk > 0: vol *= 0.45 / pk; continue
        break
    print(f"{name:20s}", "FAIL" if pk is None else f"peak {pk:.3f} vol {vol:.4f}")


def main():
    PD.mkdir(parents=True, exist_ok=True); RD.mkdir(parents=True, exist_ok=True)
    emit("ksbow_a")                                        # straight port
    emit("ksbow_b", string_over={"dispersion": 0.0})       # pure string, no stretch
    emit("ksbow_c", string_over={"frictionGain": 8.0})     # grittier friction


if __name__ == "__main__":
    main()
