"""CMA-ES stage a — render one additive patch and score it against the Iowa
viola reference (out/iowa_reference.json), printing a per-term breakdown.

Score = weighted sum of four normalised distances (spec 2026-07-27-cmaes):
  term1 harmonic-envelope   0.35   (log-amp L2 over harmonics 1-32)
  term2 motion-stats        0.25   (derive_motion metrics vs Iowa medians)
  term3 inter-harm broadband0.20   (bow-noise energy ratio per band)
  term4 attack-envelope     0.20   (per-band rise + inter-band onset spread)
Lower is better. Per-term (and motion sub-metric) values always printed so
weight tuning stays a Matt's-ears decision, not a hidden constant.

Usage:
  python score_candidate.py <patch.json>            # score one patch
  python score_candidate.py <patch.json> --control  # also score a motion-
      zeroed control and assert term2(patch) < term2(control)  [stage-a check]
"""
import copy
import json
import os
import subprocess
import sys

import numpy as np
import soundfile as sf

import refmetrics as rm

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
REF_PATH = os.path.join(HERE, "out", "iowa_reference.json")
SCRATCH = os.path.join(HERE, "out", "scratch")
os.makedirs(SCRATCH, exist_ok=True)

# Scoring notes span low/mid/high registers (3 strings); a subset of the
# iowa_reference SCORE_NOTES to keep the render-bound eval affordable (~9s).
NOTE_MIDIS = [48, 62, 69]         # C3 D4 A4
NOTE_GAP, NOTE_DUR = 2.5, 2.2     # note start spacing / duration (sustain fits)
SUS_START, SUS_LEN = 0.55, 1.4    # sustain window inside each note
ATTACK_LEN = 0.55                 # onset window inside each note
WEIGHTS = (0.35, 0.25, 0.20, 0.20)

# Per-metric normalisers for term2 (cents / Hz / correlation ranges).
MOTION_SCALE = {"resid_cents_rms": 7.3, "resid_rate_hz": 8.3,
                "resid_coherence": 0.5, "amp_frac_rms": 0.5,
                "amp_rate_hz": 1.4, "amp_coherence": 0.5}


def set_score(patch, midis):
    patch = copy.deepcopy(patch)
    patch["score"] = [{"note": m, "velocity": 0.85,
                       "time": round(i * NOTE_GAP, 3), "duration": NOTE_DUR}
                      for i, m in enumerate(midis)]
    return patch


def render(patch, tag):
    ppath = os.path.join(SCRATCH, f"cand_{tag}.json")
    wpath = os.path.join(SCRATCH, f"cand_{tag}.wav")
    json.dump(patch, open(ppath, "w"))
    r = subprocess.run([CLI, ppath, wpath], capture_output=True, text=True)
    if not os.path.exists(wpath):
        raise RuntimeError(f"render failed: {r.stderr.strip()[:300]}")
    x, sr = sf.read(wpath)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return x.astype(np.float64), sr


def measure(patch, tag):
    x, sr = render(patch, tag)
    per_note = {}
    for i, midi in enumerate(NOTE_MIDIS):
        t0 = i * NOTE_GAP
        f0 = rm.midi_to_freq(midi)
        s0 = int((t0 + SUS_START) * sr)
        sus = x[s0: s0 + int(SUS_LEN * sr)]
        atk = x[int(t0 * sr): int((t0 + ATTACK_LEN) * sr)]
        m = rm.motion_stats(sus, sr, f0)
        per_note[midi] = {
            "harm_env_db": rm.harmonic_env(sus, sr, f0),
            "broadband": rm.broadband_ratios(sus, sr, f0),
            "attack": rm.attack_stats(atk, sr),
            "motion": m,
        }
    return per_note


def score(per_note, ref):
    rnotes = ref["notes"]
    name_by_midi = {v["midi"]: k for k, v in rnotes.items()}

    # term1 — harmonic envelope L2 (dB), 12 dB RMS == 1.0
    t1s = []
    for midi, meas in per_note.items():
        rdb = np.array(rnotes[name_by_midi[midi]]["harm_env_db"])
        cdb = meas["harm_env_db"][:len(rdb)]
        t1s.append(np.sqrt(np.mean((cdb - rdb) ** 2)) / 12.0)
    term1 = float(np.mean(t1s))

    # term2 — motion stats vs Iowa medians (metric means across notes)
    med = ref["motion_medians"]
    cand = {k: np.nanmean([per_note[m]["motion"][k]
                           for m in per_note if per_note[m]["motion"]])
            for k in rm.MOTION_KEYS}
    motion_terms = {k: abs(cand[k] - med[k]) / MOTION_SCALE[k] for k in rm.MOTION_KEYS}
    term2 = float(np.mean(list(motion_terms.values())))

    # term3 — inter-harmonic broadband (log10 ratio, 1 decade == 1.0)
    eps = 1e-5
    t3s = []
    for midi, meas in per_note.items():
        rb = np.array(rnotes[name_by_midi[midi]]["broadband"])
        cb = np.array(meas["broadband"])
        t3s.append(np.mean(np.abs(np.log10(cb + eps) - np.log10(rb + eps))))
    term3 = float(np.mean(t3s))

    # term4 — attack (lag scale 50 ms, rise scale 100 ms)
    t4s = []
    for midi, meas in per_note.items():
        ra = np.array(rnotes[name_by_midi[midi]]["attack"])
        ca = np.array(meas["attack"])
        d = np.abs(ca - ra) / np.array([0.05, 0.10])
        t4s.append(np.nanmean(d))
    term4 = float(np.nanmean(t4s))

    total = sum(w * t for w, t in zip(WEIGHTS, (term1, term2, term3, term4)))
    return {"term1_harm": term1, "term2_motion": term2, "term3_broadband": term3,
            "term4_attack": term4, "total": total,
            "motion_metrics": {k: float(cand[k]) for k in rm.MOTION_KEYS},
            "motion_terms": {k: float(v) for k, v in motion_terms.items()}}


def score_patch(patch_path, ref, tag="p"):
    patch = json.load(open(patch_path))
    return score(measure(set_score(patch, NOTE_MIDIS), tag), ref)


def zero_motion(patch):
    patch = copy.deepcopy(patch)
    for n in patch["graph"]["nodes"]:
        if n.get("type") == "ExplicitPartials":
            for k in ("motionDepth1", "motionDepth2", "shimmerDepth1", "shimmerDepth2"):
                if k in n["params"]:
                    n["params"][k] = 0.0
    return patch


def report(name, s):
    print(f"\n== {name} ==")
    print(f"  term1 harm      {s['term1_harm']:.4f}")
    print(f"  term2 motion    {s['term2_motion']:.4f}")
    print(f"  term3 broadband {s['term3_broadband']:.4f}")
    print(f"  term4 attack    {s['term4_attack']:.4f}")
    print(f"  TOTAL           {s['total']:.4f}")
    mm, mt = s["motion_metrics"], s["motion_terms"]
    print("  motion metrics: " + "  ".join(
        f"{k}={mm[k]:.2f}(d{mt[k]:.2f})" for k in rm.MOTION_KEYS))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    patch_path = sys.argv[1]
    ref = json.load(open(REF_PATH))
    s = score_patch(patch_path, ref, tag="main")
    report(os.path.basename(patch_path), s)

    if "--control" in sys.argv:
        ctrl = zero_motion(json.load(open(patch_path)))
        cs = score(measure(set_score(ctrl, NOTE_MIDIS), "ctrl"), ref)
        report("motion-zeroed control", cs)
        ok = s["term2_motion"] < cs["term2_motion"]
        print(f"\n[stage-a check] term2(patch)={s['term2_motion']:.4f} "
              f"{'<' if ok else '>='} term2(control)={cs['term2_motion']:.4f}  "
              f"=> {'PASS' if ok else 'FAIL'}")
        sys.exit(0 if ok else 2)


if __name__ == "__main__":
    main()
