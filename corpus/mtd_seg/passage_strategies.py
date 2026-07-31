"""Passage-level strategies as authored engine templates (comp backlog #6, G2).

Backlog #6 asks for connective/transitional, pedal-point buildup, discursive
wandering and circle-of-fifths passages. Those are C++ PassageStrategy classes
eventually — but the engine template format already expresses multi-phrase
passages, multi-part textures and per-phrase anchoring, so the *musical design*
can be prototyped, rendered and measured here first, exactly the way
markov_phrase prototyped the phrase layer before any C++ landed.

Four strategies, each a function returning (template, meta):

  pedal_buildup    held bass pedal under a melody that climbs in register and
                   accelerates rhythmically (each phrase diminished relative to
                   the last), landing on a long final note.
  wandering        chained non-repeating figures, cursor-continuous, no motif
                   returns — the discursive passage. Deliberately violates the
                   repetition screen; that's the point, and it is reported
                   rather than tuned away.
  connective       short bridge: the antecedent's TAIL sequenced stepwise and
                   thinned, landing on a caller-specified target degree.
  fifths_sequence  the diatonic circle-of-fifths trip: one figure entering on
                   successive roots a fifth apart (-4 scale steps), lifted an
                   octave-safe fourth (+3) on alternate entries, so the passage
                   walks I-IV-vii-iii-vi-ii-V-I in diatonic space.

Engine limitation found while building this (probed, not assumed): section
`keyContexts` are parsed and stored (PieceTemplate::SectionTemplate ->
Section::keyContexts) but NOTHING reads them during melody realization —
Section::active_scale_at() has zero callers. Rendering one motif under three
sections keyed C/G/D gives byte-identical note numbers (48/53/59/60 in each).
So a REAL modulating circle-of-fifths trip is engine work; the diatonic
sequence below is the part that is expressible today.

Usage:
  python passage_strategies.py                 # all 4, 2 takes each
  python passage_strategies.py --only pedal_buildup --takes 3
"""
import argparse
import csv
import json
import pathlib
import random
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import figure_transforms as ft                                   # noqa: E402
import score_generated as sg                                     # noqa: E402
from markov_model import MarkovModel                             # noqa: E402
from markov_phrase import (REPO, net_step, render_template,      # noqa: E402
                           total_beats, _sample_figure_sized,
                           predict_relative_semitones)

OUTROOT = "renders/passage_strategies"
DEGREE_NAMES = ["I", "ii", "iii", "IV", "V", "vi", "vii"]


# --------------------------------------------------------------------------- #
# template plumbing
# --------------------------------------------------------------------------- #
def phrase(name, refs, connectors, octave=4, pitch="C"):
    return {"name": name,
            "startingPitch": {"octave": octave, "pitch": pitch},
            "figures": [{"source": "reference", "motifName": r} for r in refs],
            "connectors": [None if c is None else int(c) for c in connectors]}


def template(motifs, phrases, *, beats, bpm, seed, key="C", scale="Major",
             extra_parts=()):
    """motifs: {name: figure}; phrases: list of phrase() dicts (one passage)."""
    parts = [{"name": "melody", "role": "melody",
              "passages": {"Main": {
                  "startingPitch": {"octave": 4, "pitch": "C"},
                  "phrases": phrases}}}]
    parts.extend(extra_parts)
    return {"keyName": key, "scaleName": scale, "bpm": bpm, "masterSeed": seed,
            "motifs": [{"name": n, "figure": f, "userProvided": True}
                       for n, f in motifs.items()],
            "sections": [{"name": "Main", "beats": beats}],
            "parts": parts}


def pedal_part(motif_name, reps, octave, pitch):
    return {"name": "pedal", "role": "bass",
            "passages": {"Main": {
                "startingPitch": {"octave": octave, "pitch": pitch},
                "phrases": [phrase("PED", [motif_name] * reps,
                                   [None] + [0] * (reps - 1),
                                   octave=octave, pitch=pitch)]}}}


def template_range(t):
    """Predicted semitone span of a template's melody line, engine-free
    (same cursor math the engine runs — verified in run 3)."""
    motifs = {m["name"]: m["figure"] for m in t["motifs"]}
    ph = t["parts"][0]["passages"]["Main"]["phrases"][0]
    refs = [f["motifName"] for f in ph["figures"]]
    semis = predict_relative_semitones(motifs, refs, ph["connectors"])
    return max(semis) - min(semis)


def anchor_connectors(figs, anchors):
    """leadStep connectors that place figure i's first note on scale-degree
    anchors[i] relative to the passage start (same cursor math the engine
    runs: anchor = cursor + lead). figs[0] anchors at 0 by definition."""
    conns, cursor = [None], net_step(figs[0])
    for i in range(1, len(figs)):
        conns.append(anchors[i] - cursor)
        cursor = anchors[i] + net_step(figs[i])
    return conns


# --------------------------------------------------------------------------- #
# strategy 1 — pedal-point buildup
# --------------------------------------------------------------------------- #
def pedal_buildup(model, rng, bpm=92.0, seed=1):
    """Dominant pedal in the bass; melody climbs a third per phrase and
    accelerates (phrase j is diminished by 2^(j/2)), ending long."""
    n_lv = rng.choice([3, 4])
    base = _sample_figure_sized(model, rng, beats_lo=2.0, beats_hi=4.0)
    motifs, refs, anchors = {}, [], []
    # A buildup holds TIME constant and doubles DENSITY: level j runs the cell
    # at half the note values of level j-1, repeated twice as often, so every
    # level spans the same beats while the surface accelerates. (Levels of
    # equal span is what makes it read as a buildup rather than a rittardando
    # in reverse.)
    for j in range(n_lv):
        fig = base if j == 0 else ft.diminish(base, 2.0 ** j)
        name = f"M{j}"
        motifs[name] = fig
        # Strict restatement makes the interval sequence exactly periodic —
        # selfsim pinned at 1.0, way past the corpus p95 of 0.77. Alternate
        # repeats therefore use a rotation of the cell: same material and same
        # density, non-identical contour.
        vname = f"M{j}v"
        var = ft.rotate(fig, 1)
        if [u["step"] for u in var["units"]] == [u["step"] for u in fig["units"]]:
            var = ft.ornament(fig, rng)   # rotation is a no-op on this cell
        motifs[vname] = var
        for r in range(2 ** j):
            refs.append(name if r % 2 == 0 else vname)
            # Climb a third per LEVEL; repeats inside a level restate at pitch.
            anchors.append(2 * j)
    # Arrival: the final restatement gets its own held-last-note copy.
    last = f"M{n_lv - 1}_end"
    tailfig = {"units": [dict(u) for u in motifs[f"M{n_lv - 1}"]["units"]]}
    tailfig["units"][-1]["duration"] = max(2.0, tailfig["units"][-1]["duration"])
    motifs[last] = tailfig
    refs[-1] = last
    figs = [motifs[r] for r in refs]
    conns = anchor_connectors(figs, anchors)
    beats = sum(total_beats(f) for f in figs)
    pedal_reps = max(1, int(round(beats / 4.0)))
    motifs["PED"] = {"units": [{"duration": 4.0, "step": 0}]}
    parts = (pedal_part("PED", pedal_reps, 3, "G"),)
    t = template(motifs, [phrase("P", refs, conns)], beats=beats, bpm=bpm,
                 seed=seed, extra_parts=parts)

    def drive_pulse(name, drop_last):
        """Mean pulse of an entry, excluding the deliberately-held final note
        — otherwise the arrival note masks the acceleration it resolves."""
        us = motifs[name]["units"]
        us = us[:-1] if (drop_last and len(us) > 1) else us
        return sum(u["duration"] for u in us) / len(us)

    p_first = drive_pulse(refs[0], False)
    p_last = drive_pulse(refs[-1], True)
    return t, {"strategy": "pedal_buildup", "n_entries": len(refs),
               "n_levels": n_lv,
               "pulse_first": round(p_first, 3),
               "pulse_last": round(p_last, 3),
               "accel_ratio": round(p_first / p_last, 2),
               "anchor_span": anchors[-1] - anchors[0],
               "pedal_reps": pedal_reps}


# --------------------------------------------------------------------------- #
# strategy 2 — discursive wandering
# --------------------------------------------------------------------------- #
def wandering(model, rng, bpm=92.0, seed=1):
    """5-7 DISTINCT figures, cursor-continuous, no returns. Drift is bounded
    by a soft register pull: if the cursor strays past +-5 degrees the next
    entry is nudged back, so it wanders without running off the instrument."""
    n = rng.choice([5, 6, 7])
    motifs, refs, anchors = {}, [], []
    cursor = 0
    for j in range(n):
        fig = _sample_figure_sized(model, rng, beats_lo=1.5, beats_hi=3.0)
        name = f"W{j}"
        motifs[name] = fig
        refs.append(name)
        if j == 0:
            anchors.append(0)
            cursor = net_step(fig)
            continue
        pull = -2 if cursor > 5 else (2 if cursor < -5 else rng.choice([-1, 0, 1]))
        anchors.append(cursor + pull)
        cursor = anchors[-1] + net_step(fig)
    figs = [motifs[r] for r in refs]
    conns = anchor_connectors(figs, anchors)
    beats = sum(total_beats(f) for f in figs)
    t = template(motifs, [phrase("P", refs, conns)], beats=beats, bpm=bpm,
                 seed=seed)
    return t, {"strategy": "wandering", "n_entries": n,
               "anchor_span": max(anchors) - min(anchors),
               "distinct_motifs": len(motifs)}


# --------------------------------------------------------------------------- #
# strategy 3 — connective / transitional
# --------------------------------------------------------------------------- #
def connective(model, rng, bpm=92.0, seed=1, target=-2):
    """Bridge passage: sample an antecedent, keep its TAIL (last 3 units) as
    the link cell, sequence that cell 3x stepwise toward `target`, augmenting
    the last entry (the texture thins as it arrives)."""
    ante = _sample_figure_sized(model, rng, beats_lo=2.0, beats_hi=4.0)
    tail_units = [dict(u) for u in ante["units"][-3:]]
    tail_units[0]["step"] = 0                      # a cell always starts at 0
    cell = {"units": tail_units}
    n_seq = 3
    refs = ["A", "C0", "C1", "C2"]
    uniq = {"A": ante,
            "C0": cell,
            "C1": {"units": [dict(u) for u in cell["units"]]},
            "C2": ft.augment(cell, 1.5)}       # last entry broadens on arrival
    # Anchors interpolate linearly from where the antecedent left the cursor
    # to `target`, so the passage provably LANDS on the requested degree.
    cursor = net_step(ante)
    anchors = [0]
    for j in range(1, n_seq + 1):
        anchors.append(int(round(cursor + (target - cursor) * j / n_seq)))
    # The FINAL note is what has to arrive, so back the last entry off by its
    # own net motion: anchor + net_step(cell) == target.
    anchors[-1] = target - net_step(uniq[refs[-1]])
    figs = [uniq[r] for r in refs]
    conns = anchor_connectors(figs, anchors)
    beats = sum(total_beats(f) for f in figs)
    t = template(uniq, [phrase("P", refs, conns)], beats=beats, bpm=bpm,
                 seed=seed)
    return t, {"strategy": "connective", "n_entries": len(refs),
               "target_degree": target,
               "landed_degree": anchors[-1] + net_step(uniq[refs[-1]]),
               "cell_len": len(cell["units"])}


# --------------------------------------------------------------------------- #
# strategy 4 — diatonic circle-of-fifths sequence
# --------------------------------------------------------------------------- #
def fifths_sequence(model, rng, bpm=92.0, seed=1, n_entries=None):
    """One cell entering on successive diatonic fifths: down a fifth (-4
    steps), up a fourth (+3), alternating — the I-IV-vii-iii-vi-ii-V-I walk.
    Every other entry is the plain cell; the pattern IS the passage."""
    n = n_entries or rng.choice([6, 8])
    cell = _sample_figure_sized(model, rng, kmin=3, kmax=5,
                                beats_lo=1.0, beats_hi=2.0)
    motifs = {"S": cell}
    refs = ["S"] * n
    anchors, a = [0], 0
    for j in range(1, n):
        a += -4 if j % 2 else 3               # down a 5th / up a 4th
        anchors.append(a)
    figs = [cell] * n
    conns = anchor_connectors(figs, anchors)
    beats = total_beats(cell) * n
    t = template(motifs, [phrase("P", refs, conns)], beats=beats, bpm=bpm,
                 seed=seed)
    roots = [DEGREE_NAMES[a % 7] for a in anchors]
    return t, {"strategy": "fifths_sequence", "n_entries": n,
               "roots": "-".join(roots),
               "anchor_span": max(anchors) - min(anchors)}


STRATEGIES = {
    "pedal_buildup": pedal_buildup,
    "wandering": wandering,
    "connective": connective,
    "fifths_sequence": fifths_sequence,
}


# --------------------------------------------------------------------------- #
# driver
# --------------------------------------------------------------------------- #
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--takes", type=int, default=2, help="renders per strategy")
    ap.add_argument("--only", default=None, help="one strategy name")
    ap.add_argument("--seed", type=int, default=4242)
    ap.add_argument("--bpm", type=float, default=92.0)
    ap.add_argument("--corpus", default="mtd", help="figure-content corpus")
    ap.add_argument("--range-cap", type=int, default=19,
                    help="semitone span guard (corpus range_p90); a passage "
                         "over it is resampled, best-of kept")
    ap.add_argument("--max-tries", type=int, default=8)
    args = ap.parse_args()

    import markov_model as mm
    tok = (mm.TOKENS if args.corpus == "mtd"
           else mm.ROOT / f"{args.corpus.split('_')[0]}_tokens.json")
    model = MarkovModel.load(tok)
    cs = sg.corpus_stats(args.corpus)
    names = [args.only] if args.only else list(STRATEGIES)
    outdir = REPO / OUTROOT
    outdir.mkdir(parents=True, exist_ok=True)

    rows = []
    for name in names:
        fn = STRATEGIES[name]
        for k in range(args.takes):
            # Deterministic per (strategy, take): name hash via zlib, since
            # builtin hash() is salted per process.
            import zlib
            rng = random.Random(args.seed + 1000 * k
                                + zlib.crc32(name.encode()) % 997)
            best = None
            for tries in range(1, args.max_tries + 1):
                t, meta = fn(model, rng, bpm=args.bpm,
                             seed=args.seed + 1000 * k)
                span = template_range(t)
                if best is None or span < best[2]:
                    best = (t, meta, span, tries)
                if span <= args.range_cap:
                    break
            t, meta, span, tries = best
            meta["pred_range"] = span
            meta["tries"] = tries
            prefix = f"{OUTROOT}/{name}_{k}"
            render_template(t, prefix)
            mel = sg.load_melody(REPO / (prefix + "_1.json"))
            r = sg.score(mel, cs)
            r.update(file=f"{name}_{k}", **meta)
            rows.append(r)
            print(f"{name}_{k}: {r['n_notes']} notes range={r['range']} "
                  f"rep={r['rep_LxCount']} selfsim={r['selfsim']} "
                  f"composite={r['composite']}  "
                  + " ".join(f"{key}={val}" for key, val in meta.items()
                             if key != "strategy"))

    cols = ["file", "strategy", "n_entries", "n_notes", "range", "int_jsd",
            "ctr_jsd", "rep_LxCount", "zero_rate", "max_run_frac", "selfsim",
            "big_leap", "composite"]
    csv_path = outdir / "scores.csv"
    with open(csv_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    print(f"\nwrote {csv_path} ({len(rows)} passages)")

    # Per-strategy summary — the point is that these are DIFFERENT shapes, so
    # a single composite ranking would be the wrong read. Report the screens.
    print(f"\n{'strategy':>16s}{'n':>4s}{'notes':>7s}{'range':>7s}"
          f"{'rep':>6s}{'selfsim':>9s}{'composite':>11s}")
    for name in names:
        rs = [r for r in rows if r["strategy"] == name]
        if not rs:
            continue
        def avg(k):
            return sum(r[k] for r in rs) / len(rs)
        print(f"{name:>16s}{len(rs):>4d}{avg('n_notes'):>7.1f}"
              f"{avg('range'):>7.1f}{avg('rep_LxCount'):>6.1f}"
              f"{avg('selfsim'):>9.3f}{avg('composite'):>11.3f}")
    print(f"corpus floor: rep_p10={cs['rep_p10']} selfsim_p95={cs['selfsim_p95']}")


if __name__ == "__main__":
    main()
