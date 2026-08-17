"""afp31_v5 — from-scratch rebuild of the AFNoding-031 piano per
docs/research/afpiano_scratch/RECIPE.md (build-video archaeology).

v4 = every previously guessed value replaced by a frame-read one
(frames z_brightness / z_postsvf, 2026-08-17):
- exc SVF cutoff is FIXED at Brightness knob 91.50 MIDI = ~1614 Hz
  (NOT pitch-tracked; only the 1P tracks the clamp). Resonance 1.2.
- exc "Filter 1P" is a true one-pole (6 dB/oct) at limited pitch
  min(2*f0, 277 Hz) — new SVFSource Lowpass1P mode. Being the ONLY
  clamped stage, C6 loses just ~12 dB (v3's stacked resonant clamps
  buried it 48 dB; the v3b floor concession is no longer needed).
- Body HP cutoff = limited + Low cut knob 3.30 semitones
  = min(2*f0, 277) * 1.21, res 3 (frame: R 1 = 3).
- vel LP res 8 (frame: R 2 = 8), cutoff = limited + 12..64 semis by vel.
- NEW final 1P after vel LP ("receives simply the pitch"): one-pole at
  f0, untracked by the clamp. TUNE: could be 2*f0 if "pitch" there is
  the +12-compensated signal.

Chain: WhiteNoise x one-shot env -> SVF LP 1614 Hz res 1.2 (/res)
       -> x6 -> 1P LP @ min(2*f0, 277)
       -> KSPianoString (3 combs, t60 = 6900/f == his fixed 0.999,
          releaseFb 0.8, damper drop ~210 ms)
       -> SVF HP res 3 (Body) -> SVF LP res 8 /res (velocity brightness)
       -> 1P LP @ f0 -> Output.
"""
import json, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PDIR = ROOT / "patches/pending/afp31"
RDIR = ROOT / "renders/dsp/pending/afp31"
CLI  = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"

STIFF_HZ  = 277.0    # stiffness 60.82 MIDI, plain units
BRIGHT_HZ = 1614.0   # Brightness knob 91.50 MIDI — FIXED exc cutoff
LOWCUT    = 2.0 ** (3.30 / 12.0)   # Low cut knob 3.30 semitones -> x1.21

def stage(a, b, sec, t="Linear", power=0.0):
    return {"startVal": a, "endVal": b, "type": t, "percent": sec,
            "minSec": 0.0, "maxSec": 0.0, "holdPct": 0.0, "power": power}

# Hit envelope read off z_envfollow: Trg->Gate 10 ms pulse; follower
# attack 0.0 ms / release 361 ms -> instant attack, 10 ms hold,
# ~exponential 361 ms release (two-segment approximation).
EXC_ENV = [stage(0.0, 1.0, 0.001),
           stage(1.0, 1.0, 0.010),
           stage(1.0, 0.15, 0.110, "Sine"),
           stage(0.15, 0.0, 0.250, "Sine"),
           stage(0.0, 0.0, 0.0)]

# "Inner reverberation" layer (frames z_rev_2640/2740/2850): a SECOND
# white noise x its own follower (attack ~16.9 ms / release 462.2 ms)
# x velocity x Strength (~0.17, "lower volume than the main hit"),
# summed with the hit noise BEFORE the excitation filter stack.
REV_ENV = [stage(0.0, 1.0, 0.017, "Sine"),
           stage(1.0, 0.15, 0.140, "Sine"),
           stage(0.15, 0.0, 0.320, "Sine"),
           stage(0.0, 0.0, 0.0)]
REV_STRENGTH = 0.15   # TUNE: knob ~0.17 on-screen, exact value unread

# All tracking curves use "interp":"loglog": 2-point curve == y = k*f^n.

def clamp2f_curve(mult=1.0):
    # mult * min(2*f0, 277): the "limited pitch", optionally offset.
    knee = round(STIFF_HZ / 2.0, 2)
    return [[20.0, round(40.0 * mult, 2)],
            [knee, round(STIFF_HZ * mult, 2)],
            [16000.0, round(STIFF_HZ * mult, 2)]]

def ident_curve():
    # Final 1P cutoff = 2*f0: the pitch signal feeding every filter is the
    # +12-compensated one; only stages wired through the Min get clamped.
    # (At plain f0 the bass register measured 17 dB under C4 — too dark.)
    return [[20.0, 40.0], [8000.0, 16000.0], [16000.0, 16000.0]]

def t60_curve():
    # t60 = 13800/f: frame z_string_1940 shows held feedback 0.9995 (not
    # 0.999). Engine clamps t60 to 60 s, so the bottom ~2 octaves flatten
    # there — inaudible over a 3.5 s note.
    return [[20.0, 690.0], [16000.0, 0.863]]

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
       "params": {"source1": {"ref": "env_rev"}, "source2": REV_STRENGTH,
                   "operation": "multiply"}},
      {"id": "rev_noise", "type": "WhiteNoiseSource",
       "params": {"amplitude": {"ref": "rev_amp"}}},
      {"id": "exc_sum", "type": "CombinedSource",
       "params": {"source1": {"ref": "noise"}, "source2": {"ref": "rev_noise"},
                   "operation": "add"}},
      {"id": "exc_svf", "type": "SVFSource",
       "params": {"source": {"ref": "exc_sum"}, "cutoffFreq": BRIGHT_HZ,
                   "resonance": 1.2, "mode": "Lowpass", "normalize": True}},
      {"id": "exc_gain", "type": "CombinedSource",
       "params": {"source1": {"ref": "exc_svf"}, "source2": 6.0,
                   "operation": "multiply"}},
      {"id": "exc_1p", "type": "SVFSource",
       "params": {"source": {"ref": "exc_gain"}, "cutoffFreq": 200.0,
                   "resonance": 0.707, "mode": "Lowpass1P", "normalize": False}},
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
                   # Frame z_damp_1325: in-loop damping 1P cutoff = gate
                   # follower x knob 135 MIDI ~= 20 kHz while held (nearly
                   # open; release choke is the damper env, already modeled).
                   # 20 kHz one-pole at 48k -> coeff 1-exp(-2*pi*fc/sr) = 0.926,
                   # FLAT — not pitch-tracked as guessed in v3.
                   "brightness": 0.926,
                   "exciteGain": 1.0, "direct": 0.0,
                   "dispersion": 0.12,       # TUNE: per-string allpass fb 0.1-0.2
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
      {"id": "final_1p", "type": "SVFSource",
       "params": {"source": {"ref": "vel_lp"}, "cutoffFreq": 440.0,
                   "resonance": 0.707, "mode": "Lowpass1P", "normalize": False}},
    ],
    "output": "final_1p"
  },
  "instrument": {
    "polyphony": 4,
    "volume": 4.0,
    "paramMap": {
      "frequency": [
        "string.frequency",
        {"target": "exc_1p.cutoffFreq",   "curve": clamp2f_curve(), "interp": "loglog"},
        {"target": "string.t60",          "curve": t60_curve(), "interp": "loglog"},
        {"target": "body_hp.cutoffFreq",  "curve": clamp2f_curve(LOWCUT), "interp": "loglog"},
        {"target": "vel_lp.cutoffFreq",   "curve": clamp2f_curve(),
         "interp": "loglog", "vcurve": [[0.0, 2.0], [1.0, 40.3]]},  # +12..64 semis
        {"target": "final_1p.cutoffFreq", "curve": ident_curve(), "interp": "loglog"},
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
    name = "afp31_v5"
    p2 = PDIR / (name + ".json")
    p2.write_text(json.dumps(PATCH, indent=2), encoding="utf-8")
    wav = RDIR / (name + ".wav")
    r = subprocess.run([str(CLI), str(p2), str(wav)], capture_output=True, text=True)
    line = [l for l in r.stdout.splitlines() if "peak=" in l]
    print(name, "OK" if r.returncode == 0 else "FAIL",
          line[-1].strip() if line else (r.stdout + r.stderr)[-150:])
    return 0 if r.returncode == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
