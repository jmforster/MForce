"""afp31_gt — GROUND TRUTH build of the AFNoding-031 piano from the
decoded original patch file (docs/research/afpiano/parse_full.txt,
recovered via Wayback Machine 2026-08-18). Every value below is read
from the file, not frames. Taste layers (timeScale, bass rebalance,
treble body tracking, hammer pitch-tracking) deliberately EXCLUDED —
this is what HIS patch computes.

Approximations (engine gaps, noted): per-string damping cutoffs
(20.1k/15.0k/11.4k) collapse to one brightness 0.926 ~= 20 kHz;
middle-string fb 0.995 (t60 = 1380/f double-decay layer) collapses to
0.999 (6900/f); allpass FB trio 0/0.1/0.2 approximated via ap1-3;
hit/reverb follower exp tails approximated by two Sine segments.
"""
import json, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PDIR = ROOT / "patches/pending/afp31"
RDIR = ROOT / "renders/dsp/pending/afp31"
CLI  = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"

STIFF_HZ  = 264.0    # Stf knob 60.16 MIDI (file: norm 0.47 x 128)
BRIGHT_HZ = 1605.0   # Brightness 91.405 MIDI (norm 0.609367 x 150)
EXC_RES   = 8.46     # R knob: 0.001 + 0.705 x 11.999
REV_LP_HZ = 1917.0   # Reverb Damp 94.5 MIDI
REV_HP_HZ = 298.0    # Reverb Lo cut 62.25 MIDI

def stage(a, b, sec, t="Linear", power=0.0):
    return {"startVal": a, "endVal": b, "type": t, "percent": sec,
            "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": power}

# Trg->Gate 9 ms pulse -> follower attack 0 / release 144 ms.
EXC_ENV = [stage(0.0, 1.0, 0.001),
           stage(1.0, 1.0, 0.009),
           stage(1.0, 0.15, 0.060, "Sine"),
           stage(0.15, 0.0, 0.140, "Sine"),
           stage(0.0, 0.0, 0.0)]
# Reverb follower: attack 0 / release 1444 ms, x velocity x 0.155.
REV_ENV = [stage(0.0, 1.0, 0.001),
           stage(1.0, 0.15, 0.600, "Sine"),
           stage(0.15, 0.0, 1.200, "Sine"),
           stage(0.0, 0.0, 0.0)]

def clamp2f_curve():
    # limited pitch: min(pitch+12, Stf) == min(2*f0, 264 Hz).
    return [[20.0, 40.0], [132.0, 264.0], [16000.0, 264.0]]

def ident2f_curve():
    # pitch+12 unclamped == 2*f0 (final HP tracks this).
    return [[20.0, 40.0], [8000.0, 16000.0], [16000.0, 16000.0]]

def t60_curve():
    # held fb 0.999 -> t60 = 6900/f.
    return [[20.0, 345.0], [16000.0, 0.431]]

PATCH = {
  "sampleRate": 48000,
  "graph": {
    "nodes": [
      {"id": "exc_env", "type": "Envelope",
       "params": {"timeMode": "seconds", "stages": EXC_ENV}},
      {"id": "noise", "type": "WhiteNoiseSource",
       "params": {"amplitude": {"ref": "exc_env"}}},
      {"id": "env_rev", "type": "Envelope",
       "params": {"timeMode": "seconds", "stages": REV_ENV}},
      {"id": "rev_amp", "type": "CombinedSource",
       "params": {"source1": {"ref": "env_rev"}, "source2": 0.07,
                   "operation": "multiply"}},  # file value 0.155; Matt: bed not subtle enough (-7 dB)
      {"id": "rev_noise", "type": "WhiteNoiseSource",
       "params": {"amplitude": {"ref": "rev_amp"}}},
      {"id": "rev_lp", "type": "SVFSource",
       "params": {"source": {"ref": "rev_noise"}, "cutoffFreq": REV_LP_HZ,
                   "resonance": 0.707, "mode": "Lowpass1P", "normalize": False}},
      {"id": "rev_hp", "type": "SVFSource",
       "params": {"source": {"ref": "rev_lp"}, "cutoffFreq": REV_HP_HZ,
                   "resonance": 0.707, "mode": "Highpass1P", "normalize": False}},
      {"id": "exc_sum", "type": "CombinedSource",
       "params": {"source1": {"ref": "noise"}, "source2": {"ref": "rev_hp"},
                   "operation": "sum"}},
      {"id": "exc_svf", "type": "SVFSource",
       "params": {"source": {"ref": "exc_sum"}, "cutoffFreq": BRIGHT_HZ,
                   "resonance": EXC_RES, "mode": "Lowpass", "normalize": True}},
      {"id": "exc_gain", "type": "CombinedSource",
       "params": {"source1": {"ref": "exc_svf"}, "source2": 2.0,
                   "operation": "multiply"}},
      {"id": "exc_1p", "type": "SVFSource",
       "params": {"source": {"ref": "exc_gain"}, "cutoffFreq": 200.0,
                   "resonance": 0.707, "mode": "Lowpass1P", "normalize": False}},
      {"id": "env_damper", "type": "Envelope",
       "params": {"stages": [
          {"startVal": 0.0, "endVal": 0.0, "type": "Linear", "percent": 0.0,
           "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0},
          {"startVal": 0.0, "endVal": 1.0, "type": "Linear", "percent": 0.5,
           "minSec": 0.196, "maxSec": 0.196, "holdPct": 0.0, "power": 0.0},
          {"startVal": 1.0, "endVal": 1.0, "type": "Linear", "percent": 0.5,
           "minSec": 0.0, "maxSec": 0.25, "holdPct": 0.0, "power": 0.0}]}},
      {"id": "string", "type": "KSPianoString",
       "params": {"source": {"ref": "exc_1p"}, "frequency": 220.0,
                   "numCombs": 3, "detune": 0.001, "t60": 10.0,
                   "brightness": 0.926,
                   "exciteGain": 1.0, "direct": 0.0,
                   "dispersion": 0.12,
                   "inharmGain": 0.0, "inharmFb": 0.0, "inharmHp": 150.0,
                   "ap1": 0.0, "ap2": 0.1, "ap3": 0.2,
                   "fbCoeff": 0.0, "releaseFb": 0.8, "damperNoise": 0.0,
                   "damper": {"ref": "env_damper"}}},
      {"id": "body_hp", "type": "SVFSource",
       "params": {"source": {"ref": "string"}, "cutoffFreq": 100.0,
                   "resonance": 3.0, "mode": "Highpass", "normalize": False}},
      {"id": "vel_lp", "type": "SVFSource",
       "params": {"source": {"ref": "body_hp"}, "cutoffFreq": 2000.0,
                   "resonance": 8.0, "mode": "Lowpass", "normalize": True}},
      {"id": "final_hp", "type": "SVFSource",
       "params": {"source": {"ref": "vel_lp"}, "cutoffFreq": 440.0,
                   "resonance": 0.707, "mode": "Highpass1P", "normalize": False}},
    ],
    "output": "final_hp"
  },
  "instrument": {
    "polyphony": 4,
    "volume": 24.0,
    "paramMap": {
      "frequency": [
        "string.frequency",
        {"target": "exc_1p.cutoffFreq",   "curve": clamp2f_curve(), "interp": "loglog"},
        {"target": "string.t60",          "curve": t60_curve(), "interp": "loglog"},
        {"target": "body_hp.cutoffFreq",  "curve": clamp2f_curve(), "interp": "loglog"},
        {"target": "vel_lp.cutoffFreq",   "curve": clamp2f_curve(),
         "interp": "loglog", "vcurve": [[0.0, 2.0], [0.2, 3.65],
            [0.4, 6.65], [0.6, 12.13], [0.8, 22.11], [1.0, 40.32]]},
        {"target": "final_hp.cutoffFreq", "curve": ident2f_curve(), "interp": "loglog"},
      ]
    }
  },
  "score": [
    {"note": 36, "velocity": 0.85, "time": 0.0,  "duration": 3.5},
    {"note": 60, "velocity": 0.85, "time": 8.0,  "duration": 3.5},
    {"note": 84, "velocity": 0.85, "time": 16.0, "duration": 3.5},
    {"note": 60, "velocity": 0.30, "time": 24.0, "duration": 3.5},
    {"note": 96, "velocity": 0.85, "time": 32.0, "duration": 3.5},
    {"note": 100, "velocity": 0.85, "time": 40.0, "duration": 3.5},
    {"note": 24, "velocity": 0.85, "time": 48.0, "duration": 3.5},
  ]
}

def main():
    PDIR.mkdir(parents=True, exist_ok=True)
    RDIR.mkdir(parents=True, exist_ok=True)
    p2 = PDIR / "afp31_gt.json"
    p2.write_text(json.dumps(PATCH, indent=2), encoding="utf-8")
    wav = RDIR / "afp31_gt.wav"
    r = subprocess.run([str(CLI), str(p2), str(wav)], capture_output=True, text=True)
    line = [l for l in r.stdout.splitlines() if "peak=" in l]
    print("afp31_gt", "OK" if r.returncode == 0 else "FAIL",
          line[-1].strip() if line else (r.stdout + r.stderr)[-150:])
    return 0 if r.returncode == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
