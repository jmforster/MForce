"""afp31_v3 — from-scratch rebuild of the AFNoding-031 piano per
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

# v3: AF units ARE plain MIDI (frame z_velxfade settled it: the velocity
# crossfade 12..64 is ADDED to limited pitch in semitones — only plain
# MIDI makes 60.82+64 land at a sane 12.5 kHz). The earlier x6 came from
# conflating the 2021 patch's 4P output knee with THIS patch's excitation
# cutoff — and was exactly Matt's "chuff a little too high frequency".
STIFF_HZ = 277.0   # MIDI 60.82, plain

def stage(a, b, sec, t="Linear", power=0.0):
    return {"startVal": a, "endVal": b, "type": t, "percent": sec,
            "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": power}

# v3 hit envelope, read off the frames (z_envfollow): Trg->Gate makes a
# 10 ms PULSE; the "Hit length" follower is attack 0.0 ms / release
# 361 ms. So: INSTANT attack, 10 ms hold, ~exponential 361 ms release
# (two-segment approximation of the exp tail). v2's 10 ms Sine ramp was
# wrong on both ends — Matt's "attack too harsh + tail too long" verdict
# pointed here and at the cutoff units.
EXC_ENV = [stage(0.0, 1.0, 0.001),
           stage(1.0, 1.0, 0.010),
           stage(1.0, 0.15, 0.110, "Sine"),
           stage(0.15, 0.0, 0.250, "Sine"),
           stage(0.0, 0.0, 0.0)]

# All tracking curves use "interp": "loglog" (2026-08-17): log-value over
# log-frequency interpolation makes a 2-point curve EXACTLY y = k*f^n, so
# the power laws below need only their endpoints + explicit clamp points
# (Matt: "that's a LOT of points... can we fit a formula?" — this is the
# formula, in curve clothing).

def clamp2f_curve():
    # limited pitch: min(pitch+12, stiffness) == min(2*f0, 277 Hz).
    knee = round(STIFF_HZ / 2.0, 2)
    return [[20.0, 40.0], [knee, STIFF_HZ], [16000.0, STIFF_HZ]]

def clampf_curve(hi):
    # min(f0, hi) — the Body highpass tracks the note, clamped.
    return [[20.0, 20.0], [hi, hi], [16000.0, hi]]

def track_curve(mult, lo, hi):
    # min(max(mult*f, lo), hi): flat - power - flat.
    f_lo = round(lo / mult, 2)
    f_hi = round(hi / mult, 2)
    return [[20.0, lo], [f_lo, lo], [f_hi, hi], [16000.0, hi]]

def t60_curve():
    # t60 = 6900/f (== fixed 0.999 per-pass feedback): pure power law.
    return [[20.0, 345.0], [16000.0, 0.431]]

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
          {"startVal": 0.0, "endVal": 0.0, "type": "Linear", "percent": 0.0,
           "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": 0.0},
          {"startVal": 0.0, "endVal": 1.0, "type": "Linear", "percent": 0.5,
           "minSec": 0.21, "maxSec": 0.21, "holdPct": 0.0, "power": 0.0},
          {"startVal": 1.0, "endVal": 1.0, "type": "Linear", "percent": 0.5,
           "minSec": 0.0, "maxSec": 0.25, "holdPct": 0.0, "power": 0.0}]}},
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
    "volume": 0.8,
    "paramMap": {
      "frequency": [
        "string.frequency",
        {"target": "exc_svf.cutoffFreq", "curve": clamp2f_curve(), "interp": "loglog"},
        {"target": "exc_1p.cutoffFreq",  "curve": clamp2f_curve(), "interp": "loglog"},
        {"target": "string.t60",         "curve": t60_curve(), "interp": "loglog"},
        {"target": "string.brightness",  "curve": [[20.0, 0.82], [65.0, 0.85],
            [262.0, 0.93], [1046.0, 0.985], [4186.0, 0.995], [16000.0, 0.995]]},
        {"target": "body_hp.cutoffFreq", "curve": clampf_curve(138.6), "interp": "loglog"},
        {"target": "vel_lp.cutoffFreq",  "curve": clamp2f_curve(),
         "interp": "loglog", "vcurve": [[0.0, 2.0], [1.0, 40.3]]},  # limited + 12..64 semitones
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

def treble_floor_curve():
    # v3b concession: max(min(2*f0, 277), f0) — faithful clamp through the
    # mids, but the excitation is never darker than the note itself, so
    # the top octaves stay alive (his video never demos above ~C5; a
    # literal 277 Hz clamp leaves C6 at -48 dB into the string).
    knee = round(STIFF_HZ / 2.0, 2)
    return [[20.0, 40.0], [knee, STIFF_HZ], [STIFF_HZ, STIFF_HZ],
            [16000.0, 16000.0]]

def main():
    PDIR.mkdir(parents=True, exist_ok=True)
    RDIR.mkdir(parents=True, exist_ok=True)
    for name, floor in (("afp31_v3", False), ("afp31_v3b", True)):
        doc = json.loads(json.dumps(PATCH))
        if floor:
            for e in doc["instrument"]["paramMap"]["frequency"]:
                if isinstance(e, dict) and e["target"] in (
                        "exc_svf.cutoffFreq", "exc_1p.cutoffFreq"):
                    e["curve"] = treble_floor_curve()
        p2 = PDIR / (name + ".json")
        p2.write_text(json.dumps(doc, indent=2), encoding="utf-8")
        wav = RDIR / (name + ".wav")
        r = subprocess.run([str(CLI), str(p2), str(wav)], capture_output=True, text=True)
        line = [l for l in r.stdout.splitlines() if "peak=" in l]
        print(name, "OK" if r.returncode == 0 else "FAIL",
              line[-1].strip() if line else (r.stdout + r.stderr)[-150:])
    return 0

if __name__ == "__main__":
    sys.exit(main())
