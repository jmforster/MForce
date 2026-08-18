"""afks_v1 — rebuild of the recovered AF_KS patch "20211206_9403"
(docs/research/af_ks/parse_full.txt, an early AF KS patch a user called
"beautiful"). All values from the decoded file.

Key character difference from the piano: the exciter noise is GATED —
it runs while the key is held (release ~ Stiffness x velocity ms), and
the string loop's effective feedback is lower (0.998^2 = 0.996, both
half-loops multiply by Fb), so the string reaches a bowed/ebow-like
equilibrium instead of piano decay.

Approximations (noted):
- Pickup Position = 1.0 in the file, which degenerates his dual-delay
  waveguide into a single loop with fb^2 and two 20.8 kHz dampers ->
  KSPianoString numCombs 1, t60 = 1723/f, releaseFb 0.954^2 = 0.911,
  brightness 0.9 (two dampers folded into one; TUNE).
- Abs(noise) skipped (no abs op); plain enveloped noise.
- SVF#1 cutoff = pitch x velocity in MIDI units — pitch-dependent
  velocity darkening our vcurve can't express exactly; vcurve tuned
  at C4 ([[0,0.03],[0.5,0.18],[1,1]]).
- Strum (per-voice PolyID stagger) and random pitch drift: skipped v1.
- His exciter env-follower cascade -> gated Envelope with 2 ms attack,
  hold-while-held, 75 ms Sine release, timeScale vcurve ~ velocity.
"""
import json, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PDIR = ROOT / "patches/pending/afks"
RDIR = ROOT / "renders/dsp/pending/afks"
CLI  = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"

POST_HP_HZ = 466.0            # "Filter" subpatch: In - 1P(MIDI 70)
BODY_HZ = [196.0, 1008.0, 1605.0, 2960.0, 11470.0]   # MIDI 55/83.4/91.2/102/125.5
BODY_RES = 0.4
BODY_MIX = 0.675              # Direct/Body crossfade

def stage(a, b, sec, t="Linear", power=0.0):
    return {"startVal": a, "endVal": b, "type": t, "percent": sec,
            "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": power}

# Gated: attack 2 ms, expand (hold while held), release 75 ms.
EXC_ENV = [stage(0.0, 1.0, 0.002),
           stage(1.0, 1.0, 0.0),
           stage(1.0, 0.0, 0.075, "Sine")]

def ident_curve():
    # cutoff = f0.
    return [[20.0, 20.0], [8000.0, 8000.0], [16000.0, 16000.0]]

def t60_curve():
    # fb_eff 0.996 -> t60 = 1723/f.
    return [[20.0, 86.2], [16000.0, 0.108]]

def body(i, hz):
    return {"id": f"bp{i}", "type": "SVFSource",
            "params": {"source": {"ref": "string"}, "cutoffFreq": hz,
                        "resonance": BODY_RES, "mode": "Bandpass",
                        "normalize": False}}

PATCH = {
  "sampleRate": 48000,
  "graph": {
    "nodes": [
      {"id": "exc_env", "type": "Envelope",
       "params": {"timeMode": "seconds", "stages": EXC_ENV}},
      {"id": "noise", "type": "WhiteNoiseSource",
       "params": {"amplitude": {"ref": "exc_env"}}},
      {"id": "svf1", "type": "SVFSource",
       "params": {"source": {"ref": "noise"}, "cutoffFreq": 262.0,
                   "resonance": 2.0, "mode": "Lowpass", "normalize": False}},
      {"id": "svf2", "type": "SVFSource",
       "params": {"source": {"ref": "svf1"}, "cutoffFreq": 262.0,
                   "resonance": 2.0, "mode": "Lowpass", "normalize": False}},
      {"id": "raw_1p", "type": "SVFSource",
       "params": {"source": {"ref": "noise"}, "cutoffFreq": 262.0,
                   "resonance": 0.707, "mode": "Lowpass1P", "normalize": False}},
      {"id": "neg_1p", "type": "CombinedSource",
       "params": {"source1": {"ref": "raw_1p"}, "source2": -1.0,
                   "operation": "multiply"}},
      {"id": "exc", "type": "CombinedSource",
       "params": {"source1": {"ref": "svf2"}, "source2": {"ref": "neg_1p"},
                   "operation": "sum"}},
      {"id": "post_hp", "type": "SVFSource",
       "params": {"source": {"ref": "exc"}, "cutoffFreq": POST_HP_HZ,
                   "resonance": 0.707, "mode": "Highpass1P", "normalize": False}},
      {"id": "env_damper", "type": "Envelope",
       "params": {"stages": [
          {"startVal": 0.0, "endVal": 0.0, "type": "Linear", "percent": 0.0,
           "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0},
          {"startVal": 0.0, "endVal": 1.0, "type": "Linear", "percent": 0.5,
           "minSec": 0.02, "maxSec": 0.02, "holdPct": 0.0, "power": 0.0},
          {"startVal": 1.0, "endVal": 1.0, "type": "Linear", "percent": 0.5,
           "minSec": 0.0, "maxSec": 0.25, "holdPct": 0.0, "power": 0.0}]}},
      {"id": "string", "type": "KSPianoString",
       "params": {"source": {"ref": "post_hp"}, "frequency": 220.0,
                   "numCombs": 1, "detune": 0.0, "t60": 10.0,
                   "brightness": 0.9,
                   "exciteGain": 1.0, "direct": 0.0,
                   "dispersion": 0.0,
                   "inharmGain": 0.0, "inharmFb": 0.0, "inharmHp": 150.0,
                   "ap1": 0.0, "ap2": 0.0, "ap3": 0.0,
                   "fbCoeff": 0.0, "releaseFb": 0.911, "damperNoise": 0.0,
                   "damper": {"ref": "env_damper"}}},
    ] + [body(i, hz) for i, hz in enumerate(BODY_HZ)] + [
      {"id": "bsum1", "type": "CombinedSource",
       "params": {"source1": {"ref": "bp0"}, "source2": {"ref": "bp1"},
                   "operation": "sum"}},
      {"id": "bsum2", "type": "CombinedSource",
       "params": {"source1": {"ref": "bsum1"}, "source2": {"ref": "bp2"},
                   "operation": "sum"}},
      {"id": "bsum3", "type": "CombinedSource",
       "params": {"source1": {"ref": "bsum2"}, "source2": {"ref": "bp3"},
                   "operation": "sum"}},
      {"id": "bsum4", "type": "CombinedSource",
       "params": {"source1": {"ref": "bsum3"}, "source2": {"ref": "bp4"},
                   "operation": "sum"}},
      {"id": "bneg", "type": "CombinedSource",
       "params": {"source1": {"ref": "bsum4"}, "source2": -BODY_MIX,
                   "operation": "multiply"}},
      {"id": "out_mix", "type": "CombinedSource",
       "params": {"source1": {"ref": "string"}, "source2": {"ref": "bneg"},
                   "operation": "sum"}},
    ],
    "output": "out_mix"
  },
  "instrument": {
    "polyphony": 4,
    "volume": 0.55,
    "paramMap": {
      "frequency": [
        "string.frequency",
        {"target": "string.t60",     "curve": t60_curve(), "interp": "loglog"},
        {"target": "svf1.cutoffFreq", "curve": ident_curve(), "interp": "loglog",
         "vcurve": [[0.0, 0.03], [0.5, 0.18], [1.0, 1.0]]},
        {"target": "svf2.cutoffFreq", "curve": ident_curve(), "interp": "loglog"},
        {"target": "raw_1p.cutoffFreq", "curve": ident_curve(), "interp": "loglog"},
        {"target": "exc_env.timeScale", "curve": [[20.0, 1.0], [16000.0, 1.0]],
         "vcurve": [[0.0, 0.1], [1.0, 1.0]]},
      ]
    }
  },
  "score": [
    {"note": 36, "velocity": 0.85, "time": 0.0,  "duration": 3.5},
    {"note": 60, "velocity": 0.85, "time": 8.0,  "duration": 3.5},
    {"note": 84, "velocity": 0.85, "time": 16.0, "duration": 3.5},
    {"note": 60, "velocity": 0.30, "time": 24.0, "duration": 3.5},
    {"note": 96, "velocity": 0.85, "time": 32.0, "duration": 3.5},
    {"note": 48, "velocity": 0.60, "time": 40.0, "duration": 3.5},
  ]
}

def main():
    PDIR.mkdir(parents=True, exist_ok=True)
    RDIR.mkdir(parents=True, exist_ok=True)
    p2 = PDIR / "afks_v1.json"
    p2.write_text(json.dumps(PATCH, indent=2), encoding="utf-8")
    wav = RDIR / "afks_v1.wav"
    r = subprocess.run([str(CLI), str(p2), str(wav)], capture_output=True, text=True)
    line = [l for l in r.stdout.splitlines() if "peak=" in l]
    print("afks_v1", "OK" if r.returncode == 0 else "FAIL",
          line[-1].strip() if line else (r.stdout + r.stderr)[-200:])
    return 0 if r.returncode == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
