"""afp31_v1 — from-scratch rebuild of the AFNoding-031 piano per
docs/research/afpiano_scratch/RECIPE.md (build-video archaeology).

Chain: WhiteNoise x gated env (10 ms curved attack)
       -> SVF LP (res = brightness, normalized, cutoff = min(2*f0, STIFF))
       -> x6 -> SVF LP gentle (his 1P; we lack a true one-pole — noted)
       -> KSPianoString (3 combs = his 3 strings; t60 = 6900/f == his
          fixed 0.999 held feedback; releaseFb 0.8 == his released value;
          damper drop ~210 ms == his env-follower smoothing)
       -> SVF HP res 3 (Body: kills lows below the note)
       -> SVF LP res 8 normalized (velocity-dependent brightness:
          cutoff = f0 x 2^(12..64 semitones / 12) via curve x vcurve)
       -> Output.

STIFF = MIDI 60.82 -> 277 Hz. All tracked cutoffs use the clamp curve —
his "key insight". Unknowns marked TUNE are first A/B targets.
"""
import json, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PDIR = ROOT / "patches/pending/afp31"
RDIR = ROOT / "renders/dsp/pending/afp31"
CLI  = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"

# AF filter-cutoff units are NOT plain MIDI-to-Hz: the measured knee of
# his final excitation filter is ~1.66 kHz where naive MIDI mapping of
# stiffness 60.82 gives 277 Hz. Calibrate with the measured knee:
# scale = 1660/277 ~= 6.0 applied to the pitch-tracked excitation cutoffs.
STIFF_HZ = 277.0 * 6.0   # ~1660 Hz, matches the measured step_3 knee

def stage(a, b, sec, t="Linear", power=0.0):
    return {"startVal": a, "endVal": b, "type": t, "percent": sec,
            "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": power}

# Gated hit: 10 ms curved attack -> full while held (his env follower on
# the gate) -> 120 ms release (TUNE; his release knob not yet read).
EXC_ENV = [stage(0.0, 1.0, 0.010, "Sine"),
           stage(1.0, 1.0, 0.0),
           stage(1.0, 0.0, 0.120, "Sine")]

def clamp2f_curve():
    # cutoff = min(2*f0, STIFF_HZ), dense points on the 2f leg (curve
    # interpolation is linear-value over log-frequency).
    pts = []
    f = 20.0
    while f * 12.0 < STIFF_HZ:
        pts.append([round(f, 2), round(f * 12.0, 2)])
        f *= 1.35
    pts.append([round(STIFF_HZ / 12.0, 2), STIFF_HZ])
    pts.append([16000.0, STIFF_HZ])
    return pts

def track_curve(mult, lo, hi):
    pts = []
    f = 20.0
    while f < 16000.0:
        pts.append([round(f, 2), round(min(max(f * mult, lo), hi), 2)])
        f *= 1.6
    pts.append([16000.0, min(max(16000.0 * mult, lo), hi)])
    return pts

def t60_curve():
    # t60 = 6900/f  (== fixed per-pass feedback 0.999 at every pitch)
    pts = []
    f = 20.0
    while f < 16000.0:
        pts.append([round(f, 2), round(min(6900.0 / f, 60.0), 2)])
        f *= 1.6
    pts.append([16000.0, round(6900.0 / 16000.0, 3)])
    return pts

PATCH = {
  "sampleRate": 48000,
  "graph": {
    "nodes": [
      {"id": "exc_env", "type": "Envelope",
       "params": {"timeMode": "seconds", "stages": EXC_ENV}},
      {"id": "noise", "type": "WhiteNoiseSource",
       "params": {"amplitude": {"ref": "exc_env"}}},
      {"id": "exc_svf", "type": "SVFSource",
       "params": {"source": {"ref": "noise"}, "cutoffFreq": 200.0,
                   "resonance": 6.0,  # TUNE: brightness 1.01-12
                   "mode": "Lowpass", "normalize": True}},
      {"id": "exc_gain", "type": "CombinedSource",
       "params": {"source1": {"ref": "exc_svf"}, "source2": 6.0,
                   "operation": "multiply"}},
      {"id": "exc_1p", "type": "SVFSource",
       "params": {"source": {"ref": "exc_gain"}, "cutoffFreq": 200.0,
                   "resonance": 0.707, "mode": "Lowpass", "normalize": False}},
      {"id": "env_damper", "type": "Envelope",
       "params": {"stages": [
          stage(0.0, 0.0, 0.0),
          stage(0.0, 1.0, 0.5, "Linear"),   # 210 ms felt landing ~ his smoothing
          stage(1.0, 1.0, 0.5)]}},
      {"id": "string", "type": "KSPianoString",
       "params": {"source": {"ref": "exc_1p"}, "frequency": 220.0,
                   "numCombs": 3, "detune": 1.0, "t60": 10.0,
                   "brightness": 0.72,       # TUNE: his in-loop damping 1P
                   "exciteGain": 1.0, "direct": 0.0,
                   "dispersion": 0.12,       # TUNE: his per-string allpass fb 0.1-0.2
                   "inharmGain": 0.0, "inharmFb": 0.0, "inharmHp": 150.0,
                   "ap1": 0.55, "ap2": 0.35, "ap3": 0.2,
                   "fbCoeff": 0.0, "releaseFb": 0.8, "damperNoise": 0.0,
                   "damper": {"ref": "env_damper"}}},
      {"id": "body_hp", "type": "SVFSource",
       "params": {"source": {"ref": "string"}, "cutoffFreq": 100.0,
                   "resonance": 3.0, "mode": "Highpass", "normalize": False}},
      {"id": "vel_lp", "type": "SVFSource",
       "params": {"source": {"ref": "body_hp"}, "cutoffFreq": 2000.0,
                   "resonance": 8.0, "mode": "Lowpass", "normalize": True}},
    ],
    "output": "vel_lp"
  },
  "instrument": {
    "polyphony": 4,
    "volume": 1.2,
    "paramMap": {
      "frequency": [
        "string.frequency",
        {"target": "exc_svf.cutoffFreq", "curve": clamp2f_curve()},
        {"target": "exc_1p.cutoffFreq",  "curve": clamp2f_curve()},
        {"target": "string.t60",         "curve": t60_curve()},
        {"target": "body_hp.cutoffFreq", "curve": track_curve(0.75, 60.0, 800.0)},
        {"target": "vel_lp.cutoffFreq",  "curve": track_curve(1.0, 30.0, 12000.0),
         "vcurve": [[0.0, 2.0], [1.0, 40.0]]},  # +12 .. +64 semitones
      ]
    }
  },
  "score": [
    {"note": 36, "velocity": 0.85, "time": 0.0,  "duration": 3.5},
    {"note": 60, "velocity": 0.85, "time": 8.0,  "duration": 3.5},
    {"note": 84, "velocity": 0.85, "time": 16.0, "duration": 3.5},
    {"note": 60, "velocity": 0.30, "time": 24.0, "duration": 3.5},
  ]
}

def main():
    PDIR.mkdir(parents=True, exist_ok=True)
    RDIR.mkdir(parents=True, exist_ok=True)
    p = PDIR / "afp31_v1.json"
    p.write_text(json.dumps(PATCH, indent=2), encoding="utf-8")
    wav = RDIR / "afp31_v1.wav"
    r = subprocess.run([str(CLI), str(p), str(wav)], capture_output=True, text=True)
    print(r.stdout[-400:] if r.returncode == 0 else "FAIL\n" + (r.stdout + r.stderr)[-400:])
    return r.returncode

if __name__ == "__main__":
    sys.exit(main())
