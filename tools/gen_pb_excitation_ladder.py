"""AFP excitation ladder (REVIEW 33 moves A+E) on the Piano_bright baseline.

Measured targets (docs/research/afpiano_2021/ANALYSIS.md, stage WAVs):
attack ~20 ms, decay ~350 ms to a held-key noise bed ~-15 dB rel peak,
release ~450 ms (the tap tail). One envelope gives both behaviors.

Arms (one mechanism each):
  pb_exc_ctrl   : Piano_bright unchanged (control)
  pb_exc_A      : release lengthened only (tail ~450 ms, no bed)
  pb_exc_AE     : attack 20 ms / decay 350 ms -> bed 0.18 / release 450 ms
  pb_exc_AE_lo  : same, bed 0.09 (~-21 dB) — bed-level variation

Only env1 (the main noise-burst envelope feeding WhiteNoise1) changes;
click/knock envelopes are transient by design and stay.
Outputs: patches/pending/ks_piano_v6/ + renders/dsp/pending/ks_piano_v6/.
"""
import copy, json, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC  = ROOT / "patches/library/keys/Piano_bright.json"
PDIR = ROOT / "patches/pending/ks_piano_v6"
RDIR = ROOT / "renders/dsp/pending/ks_piano_v6"
CLI  = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"

def stage(startVal, endVal, seconds, type_="Linear"):
    return {"startVal": startVal, "endVal": endVal, "type": type_,
            "percent": seconds, "minSec": 0.0, "maxSec": 0.0,
            "holdPct": 0.0, "power": 0.0}

def env1_stages(attack, bed, release):
    # CLACK shape, measured from AFP step_3 hits (50% in ~15-30 ms, 10%
    # by ~100 ms, tail to ~300 ms): fast two-segment drop, then the bed
    # (held-key noise), release at note end. The first ladder cut used a
    # LINEAR 350 ms decay — still at ~92% level after 30 ms — which Matt
    # correctly heard as "a chuff of white noise with a feeble hit".
    tail = max(bed, 0.01)
    return [stage(0.0, 1.0, attack),
            stage(1.0, 0.35, 0.030),           # fast drop: 50% by ~20 ms
            stage(0.35, tail, 0.100, "Sine"),  # to ~10% by ~100 ms
            stage(tail, tail, 0.0),            # held bed (expand)
            stage(tail, 0.0, release, "Sine")]

ARMS = {
    # name           attack  bed    release
    "pb_exc_A":     (0.010,  0.0,   0.250),
    "pb_exc_AE":    (0.010,  0.06,  0.450),
    "pb_exc_AE_lo": (0.010,  0.03,  0.450),
}

def main():
    base = json.loads(SRC.read_text(encoding="utf-8"))
    PDIR.mkdir(parents=True, exist_ok=True)
    RDIR.mkdir(parents=True, exist_ok=True)

    outputs = {"pb_exc_ctrl": SRC}
    for name, (atk, bed, rel) in ARMS.items():
        doc = copy.deepcopy(base)
        for n in doc["graph"]["nodes"]:
            if n["id"] == "env1":
                n["params"] = {"timeMode": "seconds",
                               "stages": env1_stages(atk, bed, rel)}
        p = PDIR / f"{name}.json"
        p.write_text(json.dumps(doc, indent=2), encoding="utf-8")
        outputs[name] = p

    for name, p in outputs.items():
        wav = RDIR / f"{name}.wav"
        r = subprocess.run([str(CLI), str(p), str(wav)],
                           capture_output=True, text=True)
        ok = "OK" if r.returncode == 0 else "FAIL"
        tail = [l for l in r.stdout.splitlines() if "peak=" in l]
        print(f"{name}: {ok} {tail[-1].strip() if tail else r.stderr.strip()[:80]}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
