"""Render the ENGINE's anchor-driven passage strategies (comp backlog #6 stage 1).

The Python prototype in passage_strategies.py authored every anchor and every
connector itself and handed the engine a fully-resolved template. This driver
does the opposite: it authors a nearly-empty passage that names a registered
C++ PassageStrategy, and the engine builds the cell, the anchors, the
connectors and the range-guarded candidate internally.

So this script is the VERIFICATION of the port, not a second prototype — if
these renders match the prototype's shape claims (buildup accelerates, fifths
walk lands on the right roots, spans stay under the guard), stage 1 works.

  python engine_passage_strategies.py                  # both strategies
  python engine_passage_strategies.py --only sequence_passage --takes 3
"""
import argparse
import csv
import json
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import score_generated as sg                                       # noqa: E402

REPO = HERE.parent.parent
CLI = REPO / "build/tools/mforce_cli/Release/mforce_cli.exe"
PATCH = REPO / "patches/Additive1.json"
OUTROOT = "renders/engine_passage_strategies"


def melody_part(strategy, config_key, config, section="Main"):
    return {"name": "melody", "role": "melody",
            "passages": {section: {
                "startingPitch": {"octave": 4, "pitch": "C"},
                "strategy": strategy,
                config_key: config,
                "phrases": []}}}


def pedal_part(section="Main", reps=8, octave=3, pitch="G"):
    """Template-authored pedal. Stage 1 emits melody only from the strategy —
    a PassageStrategy is passage-scoped and cannot add parts — so the pedal
    stays here, exactly as the spec says."""
    return {"name": "pedal", "role": "melody",
            "passages": {section: {
                "startingPitch": {"octave": octave, "pitch": pitch},
                "phrases": [{
                    "name": "PED",
                    "startingPitch": {"octave": octave, "pitch": pitch},
                    "figures": [{"source": "reference", "motifName": "PED"}
                                for _ in range(reps)],
                    "connectors": [None] * reps}]}}}


def base_template(parts, *, beats, bpm, seed, motifs=()):
    return {"keyName": "C", "scaleName": "Major", "bpm": bpm,
            "masterSeed": seed,
            "motifs": [{"name": n, "figure": f, "userProvided": True}
                       for n, f in motifs],
            "sections": [{"name": "Main", "beats": beats}],
            "parts": list(parts)}


# --------------------------------------------------------------------------- #
# the two stage-1 strategies
# --------------------------------------------------------------------------- #
def pedal_buildup(seed, bpm, *, levels=0, climb=2, cell=3.0):
    cfg = {"levels": levels, "climbStep": climb, "cellBeats": cell,
           "holdBeats": 2.0, "rangeCap": 19, "maxTries": 8, "seed": seed}
    parts = [melody_part("pedal_buildup", "pedalBuildupConfig", cfg),
             pedal_part(reps=10)]
    motifs = [("PED", {"units": [{"duration": 4.0, "step": 0}]})]
    return base_template(parts, beats=48, bpm=bpm, seed=seed, motifs=motifs)


def sequence_passage(seed, bpm, *, entries=8, step_a=-4, step_b=3, cell=1.5):
    cfg = {"entries": entries, "stepA": step_a, "stepB": step_b,
           "cellBeats": cell, "rangeCap": 19, "maxTries": 8, "seed": seed}
    parts = [melody_part("sequence_passage", "sequenceConfig", cfg)]
    return base_template(parts, beats=48, bpm=bpm, seed=seed)


def connective(seed, bpm, *, entries=3, target=-2, tail=3, lead="sequence"):
    """Two sections: a thematic passage, then the bridge that harvests ITS
    tail. A one-section connective would have nothing to connect from, which
    is exactly the case the strategy reports as `source=sampled-fallback`."""
    lead_cfg = {"entries": 6, "stepA": -4, "stepB": 3, "cellBeats": 1.5,
                "rangeCap": 19, "maxTries": 8, "seed": seed}
    conn_cfg = {"entries": entries, "targetDegree": target, "tailUnits": tail,
                "finalAugment": 1.5, "cellBeats": 2.0, "rangeCap": 19,
                "maxTries": 8, "seed": seed + 1}
    part = {"name": "melody", "role": "melody", "passages": {
        "Main": {"startingPitch": {"octave": 4, "pitch": "C"},
                 "strategy": "sequence_passage", "sequenceConfig": lead_cfg,
                 "phrases": []},
        "Bridge": {"startingPitch": {"octave": 4, "pitch": "C"},
                   "strategy": "connective_passage",
                   "connectiveConfig": conn_cfg, "phrases": []}}}
    t = base_template([part], beats=32, bpm=bpm, seed=seed)
    t["sections"] = [{"name": "Main", "beats": 32},
                     {"name": "Bridge", "beats": 24}]
    return t


def suite(seed, bpm):
    """All three stage-1/2 strategies chained as one piece — passage
    strategies are only worth having if they COMBINE. The connective sits
    between the sequence and the buildup so it has a real tail to harvest."""
    part = {"name": "melody", "role": "melody", "passages": {
        "S0_sequence": {"startingPitch": {"octave": 4, "pitch": "C"},
                        "strategy": "sequence_passage",
                        "sequenceConfig": {"entries": 8, "stepA": -4,
                                           "stepB": 3, "cellBeats": 1.5,
                                           "rangeCap": 19, "maxTries": 8,
                                           "seed": seed},
                        "phrases": []},
        "S1_bridge": {"startingPitch": {"octave": 4, "pitch": "C"},
                      "strategy": "connective_passage",
                      "connectiveConfig": {"entries": 3, "targetDegree": 2,
                                           "tailUnits": 3, "finalAugment": 1.5,
                                           "cellBeats": 2.0, "rangeCap": 19,
                                           "maxTries": 8, "seed": seed + 1},
                      "phrases": []},
        "S2_buildup": {"startingPitch": {"octave": 4, "pitch": "C"},
                       "strategy": "pedal_buildup",
                       "pedalBuildupConfig": {"levels": 3, "climbStep": 2,
                                              "cellBeats": 3.0,
                                              "holdBeats": 2.0,
                                              "rangeCap": 19, "maxTries": 8,
                                              "seed": seed + 2},
                       "phrases": []}}}
    ped = {"name": "pedal", "role": "melody", "passages": {
        "S2_buildup": {"startingPitch": {"octave": 3, "pitch": "G"},
                       "phrases": [{"name": "PED",
                                    "startingPitch": {"octave": 3,
                                                      "pitch": "G"},
                                    "figures": [{"source": "reference",
                                                 "motifName": "PED"}
                                                for _ in range(6)],
                                    "connectors": [None] * 6}]}}}
    t = base_template([part, ped], beats=32, bpm=bpm, seed=seed,
                      motifs=[("PED", {"units": [{"duration": 4.0,
                                                  "step": 0}]})])
    t["sections"] = [{"name": "S0_sequence", "beats": 24},
                     {"name": "S1_bridge", "beats": 16},
                     {"name": "S2_buildup", "beats": 24}]
    return t


STRATEGIES = {"pedal_buildup": pedal_buildup,
              "sequence_passage": sequence_passage,
              "connective": connective,
              "suite": lambda seed, bpm: suite(seed, bpm)}

# A few deliberate outliers alongside the norm-respecting takes, per the
# autonomy ground rule: shapes no textbook would ask for, in case one is
# interesting. Each entry is (label, strategy, kwargs).
OUTLIERS = [
    ("pedal_buildup_6lv",    "pedal_buildup",    {"levels": 6}),
    ("sequence_17x",         "sequence_passage", {"entries": 17}),
    ("sequence_thirds_up",   "sequence_passage", {"step_a": 2, "step_b": 2}),
    ("sequence_static",      "sequence_passage", {"step_a": 0, "step_b": 0}),
]


def render(template, out_prefix):
    tpath = REPO / (out_prefix + "_tmpl.json")
    tpath.parent.mkdir(parents=True, exist_ok=True)
    tpath.write_text(json.dumps(template, indent=1), encoding="utf-8")
    p = subprocess.run([str(CLI), "--compose", str(PATCH),
                        str(REPO / out_prefix), "1", "--template", str(tpath)],
                       capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(f"{out_prefix}: CLI failed\n{p.stdout}\n{p.stderr}")
    # The strategies log their own shape claim to stderr; keep it, it IS the
    # verification (anchor roots, level count, predicted range).
    claim = [ln.split(": ", 1)[1] for ln in (p.stdout + p.stderr).splitlines()
             if ln.startswith(("pedal_buildup:", "sequence_passage:",
                               "connective_passage:"))]
    return " | ".join(claim)


def melody_durations(out_prefix):
    ev = json.loads((REPO / (out_prefix + "_1.json")).read_text(encoding="utf-8"))
    return [float(e["data"].get("duration", 0.0)) for e in ev["parts"][0]["events"]]


def accel_ratio(durs):
    """Mean pulse of the first quarter vs the last quarter, excluding the
    deliberately-held arrival note — the buildup's whole claim in one number."""
    if len(durs) < 8:
        return 0.0
    body = durs[:-1]
    q = max(1, len(body) // 4)
    first, last = body[:q], body[-q:]
    return (sum(first) / len(first)) / max(1e-6, sum(last) / len(last))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--takes", type=int, default=3)
    ap.add_argument("--only", default=None)
    ap.add_argument("--seed", type=int, default=7311)
    ap.add_argument("--bpm", type=float, default=92.0)
    ap.add_argument("--corpus", default="mtd")
    ap.add_argument("--no-outliers", action="store_true")
    args = ap.parse_args()

    cs = sg.corpus_stats(args.corpus)
    names = [args.only] if args.only else list(STRATEGIES)
    jobs = []
    for name in names:
        for k in range(args.takes):
            jobs.append((f"{name}_{k}", name, {}, args.seed + 1000 * k))
    if not args.only and not args.no_outliers:
        for label, name, kw in OUTLIERS:
            jobs.append((label, name, kw, args.seed + 77))

    (REPO / OUTROOT).mkdir(parents=True, exist_ok=True)
    rows = []
    for label, name, kw, seed in jobs:
        prefix = f"{OUTROOT}/{label}"
        claim = render(STRATEGIES[name](seed, args.bpm, **kw), prefix)
        mel = sg.load_melody(REPO / (prefix + "_1.json"))
        durs = melody_durations(prefix)
        r = sg.score(mel, cs)
        r.update(file=label, strategy=name,
                 accel=round(accel_ratio(durs), 2), claim=claim)
        rows.append(r)
        print(f"{label}: {r['n_notes']} notes range={r['range']} "
              f"rep={r['rep_LxCount']} selfsim={r['selfsim']} "
              f"composite={r['composite']} accel={r['accel']}  [{r['claim']}]")

    cols = ["file", "strategy", "n_notes", "range", "int_jsd", "ctr_jsd",
            "rep_LxCount", "zero_rate", "max_run_frac", "selfsim", "big_leap",
            "composite", "accel", "claim"]
    csv_path = REPO / OUTROOT / "scores.csv"
    with open(csv_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    print(f"\nwrote {csv_path} ({len(rows)} passages)")
    print(f"corpus floor: rep_p10={cs['rep_p10']} selfsim_p95={cs['selfsim_p95']}")


if __name__ == "__main__":
    main()
