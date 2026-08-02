"""Passage-level strategies as authored engine templates (comp backlog #6, G2).

Backlog #6 asks for connective/transitional, pedal-point buildup, discursive
wandering and circle-of-fifths passages. Those are C++ PassageStrategy classes
eventually — but the engine template format already expresses multi-phrase
passages, multi-part textures and per-phrase anchoring, so the *musical design*
can be prototyped, rendered and measured here first, exactly the way
markov_phrase prototyped the phrase layer before any C++ landed.

Seven strategies, each a function returning (template, meta):

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

Matt's run-12 verdict added the missing half of the connective idea: a chain of
repeated figures is common, but "typically there is a TRANSFORMATION at the
end, prior to the arrival at the next passage". His three examples are three
strategies, all built as CHAIN -> GOAL:

  chain_chromatic_peak      cell x5-7 ascending, then a CHROMATIC rise to a
                            held peak, then a scalar descent to a tonic cadence
  chain_tonic_fanfare       cell x4-6 descending, landing exactly on the tonic,
                            which is then repeated as a rhythmic fanfare
                            (q e. s q q h q)
  chain_arpeggio_fallback   cell x2-3 ascending, an arpeggio in thirds falling
                            back ALMOST to the start (nets +1 per cycle), 3
                            cycles, then a climactic rising cadence

Chromaticism does NOT need a chromatic scale: FigureUnit.accidental is applied
by the engine as a transient shift that leaves the scale-degree cursor alone
(composer.h: soundNN = currentNN + accidental), so a raised passing tone is
(step 0, accidental +1). Verified on rendered output, not assumed — the rise
sounds 57-58-59-60-61-62-63.

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
    """Widest predicted semitone span of any melody phrase in the template,
    engine-free (same cursor math the engine runs — verified in run 3).
    Per phrase, because each phrase re-anchors at its own startingPitch."""
    motifs = {m["name"]: m["figure"] for m in t["motifs"]}
    worst = 0
    for passage in t["parts"][0]["passages"].values():
        for ph in passage["phrases"]:
            refs = [f["motifName"] for f in ph["figures"]]
            semis = predict_relative_semitones(motifs, refs, ph["connectors"])
            worst = max(worst, max(semis) - min(semis))
    return worst


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


# --------------------------------------------------------------------------- #
# strategy 5 — repeated-chain connective (Matt's run-12 shapes)
# --------------------------------------------------------------------------- #
# Matt on the run-12 connective render: "Long chains of repeated figures moving
# up or down the scale are very common in connective passages, but typically
# there is a TRANSFORMATION at the end, prior to the arrival at the next
# passage." The v1 `connective` above sequences a cell 3x and stops — it has
# the chain and no goal. The three shapes below are his three examples, and
# they share one skeleton:
#
#     CHAIN (cell x N, stepwise up or down)  ->  GOAL (the transformation)
#
# The goal is what makes it read as arriving somewhere rather than just
# stopping, so it is a first-class part of the strategy, not an ending.
MAJOR_ASC = [2, 2, 1, 2, 2, 2, 1]   # semitones between adjacent degrees (major)


def _chromatic_units(start_degree, semis, dur):
    """Units rising `semis` SEMITONES chromatically from a note sitting on
    absolute scale degree `start_degree`.

    Chromatic motion is expressible without a chromatic scale: FigureUnit has
    an `accidental` field that the engine applies as a transient shift
    (composer.h: soundNN = currentNN + accidental) WITHOUT moving the
    scale-degree cursor. So the raised fourth between two whole-tone degrees is
    (step 0, accidental +1) — the cursor stays on the lower degree and the next
    (step +1) still lands correctly. Where the diatonic gap is already a
    semitone (mi-fa, ti-do) no accidental is inserted, or it would duplicate
    the next degree.

    Returns units WITHOUT a leading anchor note (caller supplies it)."""
    # Sounding semitone offset of each degree above the start.
    deg_semis, acc = [0], 0
    for i in range(8):
        acc += MAJOR_ASC[(start_degree + i) % 7]
        deg_semis.append(acc)
    units, cur_deg_idx = [], 0
    for off in range(1, semis + 1):
        if off in deg_semis:                      # the offset IS a scale degree
            d = deg_semis.index(off)
            units.append({"duration": dur, "step": d - cur_deg_idx})
            cur_deg_idx = d
        else:                                     # one semitone above a degree
            d = max(i for i, s in enumerate(deg_semis) if s < off)
            units.append({"duration": dur, "step": d - cur_deg_idx,
                          "accidental": 1})
            cur_deg_idx = d
    return units


def _chain(cell, n, direction):
    """Anchors for a cell repeated n times moving one scale step per entry."""
    return [direction * j for j in range(n)]


def _sample_chain_cell(model, rng, k, lo, hi, tries=12):
    """A cell for a repeated chain, rejecting monotone draws.

    A chain restates its cell N times, so a cell whose steps are all 0 renders
    as one pitch hammered 20+ times — caught in the first render here, not a
    hypothetical. The chain amplifies whatever the cell is, which makes the
    usual "occasionally monotone is fine" tolerance wrong at this layer."""
    best = None
    for _ in range(tries):
        cell = _sample_figure_sized(model, rng, kmin=k, kmax=k,
                                    beats_lo=lo, beats_hi=hi)
        moving = sum(1 for u in cell["units"][1:] if u["step"] != 0)
        if best is None or moving > best[0]:
            best = (moving, cell)
        if moving >= max(2, (k - 1) // 2):
            return cell
    return best[1]


def chain_chromatic_peak(model, rng, bpm=92.0, seed=1):
    """Matt shape 1: "4-note figure repeats 7 times, ascending, then a
    chromatic rise reaches a peak and pauses, then descends to a cadence"."""
    n_rep = rng.choice([5, 6, 7])
    cell = _sample_chain_cell(model, rng, 4, 1.0, 2.0)
    motifs = {"CH": cell}
    refs = ["CH"] * n_rep
    anchors = _chain(cell, n_rep, +1)

    # GOAL part 1: the chromatic rush to the peak, starting where the chain
    # left the cursor and ending on a held note (the "pause").
    top = anchors[-1] + net_step(cell)
    semis = rng.choice([4, 5, 6])
    rise = ([{"duration": 0.25, "step": 0}]
            + _chromatic_units(top, semis, 0.25))
    rise[-1]["duration"] = rng.choice([2.0, 3.0])          # peak, held
    motifs["RISE"] = {"units": rise}
    refs.append("RISE")
    anchors.append(top)

    # GOAL part 2: the descent to a cadence. Diatonic, accelerating into the
    # arrival, landing on the tonic (degree 0 mod 7) with a long final note.
    # Land on a tonic at least a fifth below the peak: the nearest tonic below
    # can be one step away (measured: peak degree 8 -> land 7), which renders
    # as a two-note "descent" and does not read as one at all.
    peak_deg = top + sum(u["step"] for u in rise)
    land = peak_deg - ((peak_deg - 0) % 7)                 # tonic at/below peak
    while peak_deg - land < 4:
        land -= 7
    n_down = peak_deg - land
    desc = [{"duration": 0.5, "step": 0}]
    desc += [{"duration": 0.5, "step": -1} for _ in range(n_down)]
    desc[-1]["duration"] = 4.0                             # cadence, held
    motifs["CAD"] = {"units": desc}
    refs.append("CAD")
    anchors.append(peak_deg)

    figs = [motifs[r] for r in refs]
    conns = anchor_connectors(figs, anchors)
    t = template(motifs, [phrase("P", refs, conns)],
                 beats=sum(total_beats(f) for f in figs), bpm=bpm, seed=seed)
    n_acc = sum(1 for u in rise if u.get("accidental"))
    return t, {"strategy": "chain_chromatic_peak", "n_entries": len(refs),
               "chain_reps": n_rep, "rise_semis": semis,
               "accidentals": n_acc, "peak_degree": peak_deg,
               "landed_degree": land, "landed_tonic": land % 7 == 0}


def chain_tonic_fanfare(model, rng, bpm=92.0, seed=1):
    """Matt shape 2: "4-note figure repeats 6 times, descending to the tonic,
    and upon arrival at the tonic it is repeated in a rhythmic fanfare eg
    Cq Ce. Cs Cq Cq Ch Cq"."""
    n_rep = rng.choice([4, 5, 6])
    cell = _sample_chain_cell(model, rng, 4, 1.0, 2.0)
    motifs = {"CH": cell}
    refs = ["CH"] * n_rep
    anchors = _chain(cell, n_rep, -1)

    # The chain must LAND on the tonic, so the last entry is back-anchored by
    # its own net motion (same trick as `connective`): the arrival is the
    # point, and "descending to the tonic" has to be true, not approximate.
    tonic = anchors[-1] + net_step(cell)
    tonic -= tonic % 7                       # nearest tonic at/below
    anchors[-1] = tonic - net_step(cell)

    # GOAL: Matt's fanfare rhythm, all on the arrived tonic (step 0 throughout
    # — accidental-free, one repeated pitch). q e. s q q h q.
    fan = [1.0, 0.75, 0.25, 1.0, 1.0, 2.0, 1.0]
    motifs["FAN"] = {"units": [{"duration": d, "step": 0} for d in fan]}
    refs.append("FAN")
    anchors.append(tonic)

    figs = [motifs[r] for r in refs]
    conns = anchor_connectors(figs, anchors)
    t = template(motifs, [phrase("P", refs, conns)],
                 beats=sum(total_beats(f) for f in figs), bpm=bpm, seed=seed)
    return t, {"strategy": "chain_tonic_fanfare", "n_entries": len(refs),
               "chain_reps": n_rep, "arrival_degree": tonic,
               "landed_tonic": tonic % 7 == 0,
               "fanfare_beats": sum(fan)}


def chain_arpeggio_fallback(model, rng, bpm=92.0, seed=1):
    """Matt shape 3: "6-note figure repeats 3 times, ascending, then an
    arpeggio falls back almost but not quite to the start and the figure
    ascends again .. this happens 3x followed by a climactic cadence"."""
    n_cyc = 3
    reps = rng.choice([2, 3])
    cell = _sample_chain_cell(model, rng, 6, 1.5, 3.0)
    motifs, refs, anchors = {"CY": cell}, [], []
    cursor = 0
    for c in range(n_cyc):
        for _ in range(reps):
            refs.append("CY")
            anchors.append(cursor)
            cursor += 1                       # ascend one step per restatement
        if c == n_cyc - 1:
            break
        # The arpeggio falls in THIRDS back to one degree above where the
        # cycle began — "almost but not quite", so each cycle nets +1 and the
        # passage keeps climbing across the three.
        start_of_cycle = anchors[-reps]
        fall_to = start_of_cycle + 1
        top = cursor - 1 + net_step(cell)
        n_thirds = max(1, (top - fall_to) // 2)
        arp = [{"duration": 0.375, "step": 0}]
        arp += [{"duration": 0.375, "step": -2} for _ in range(n_thirds)]
        name = f"ARP{c}"
        motifs[name] = {"units": arp}
        refs.append(name)
        anchors.append(top)
        cursor = top + net_step(motifs[name])

    # GOAL: the climactic cadence — a rising scalar approach to a held note an
    # octave (7 degrees) above where the whole passage started.
    goal = 7
    approach = [{"duration": 0.25, "step": 0}]
    approach += [{"duration": 0.25, "step": 1} for _ in range(4)]
    approach[-1]["duration"] = 4.0
    motifs["CLIMAX"] = {"units": approach}
    refs.append("CLIMAX")
    anchors.append(goal - 4)

    figs = [motifs[r] for r in refs]
    conns = anchor_connectors(figs, anchors)
    t = template(motifs, [phrase("P", refs, conns)],
                 beats=sum(total_beats(f) for f in figs), bpm=bpm, seed=seed)
    return t, {"strategy": "chain_arpeggio_fallback", "n_entries": len(refs),
               "cycles": n_cyc, "reps_per_cycle": reps,
               "climax_degree": goal,
               "anchor_span": max(anchors) - min(anchors)}


STRATEGIES = {
    "pedal_buildup": pedal_buildup,
    "wandering": wandering,
    "connective": connective,
    "fifths_sequence": fifths_sequence,
    "chain_chromatic_peak": chain_chromatic_peak,
    "chain_tonic_fanfare": chain_tonic_fanfare,
    "chain_arpeggio_fallback": chain_arpeggio_fallback,
}


# --------------------------------------------------------------------------- #
# suite — the four strategies chained as one piece
# --------------------------------------------------------------------------- #
SUITE_ORDER = ["wandering", "connective", "pedal_buildup", "fifths_sequence"]

# A second running order built around the chain shapes, so the "chain -> goal"
# passages can be heard doing the job they exist for: leading INTO something.
CHAIN_SUITE_ORDER = ["wandering", "chain_chromatic_peak", "pedal_buildup",
                     "chain_arpeggio_fallback", "chain_tonic_fanfare"]


def suite(model, rng, bpm=92.0, seed=1, order=None):
    """One template, one section per strategy, played in sequence.

    Passage strategies are only worth having if they COMBINE, so this renders
    the four back to back: discursive opening -> bridge that lands on a target
    -> pedal buildup -> fifths walk out. Each strategy's own template is built
    first and its melody phrase is lifted into a section here; motif names are
    prefixed per section so the four namespaces can't collide.
    """
    order = order or SUITE_ORDER
    motifs, sections, passages, extra = {}, [], {}, []
    for idx, name in enumerate(order):
        t, _meta = STRATEGIES[name](model, rng, bpm=bpm, seed=seed + idx)
        sec = f"S{idx}_{name}"
        pre = f"s{idx}_"
        for m in t["motifs"]:
            motifs[pre + m["name"]] = m["figure"]
        mel_phr = t["parts"][0]["passages"]["Main"]["phrases"][0]
        beats = sum(sum(u["duration"] for u in motifs[pre + f["motifName"]]["units"])
                    for f in mel_phr["figures"])
        sections.append({"name": sec, "beats": beats})
        passages[sec] = {
            "startingPitch": {"octave": 4, "pitch": "C"},
            "phrases": [{"name": f"P{idx}",
                         "startingPitch": {"octave": 4, "pitch": "C"},
                         "figures": [{"source": "reference",
                                      "motifName": pre + f["motifName"]}
                                     for f in mel_phr["figures"]],
                         "connectors": mel_phr["connectors"]}]}
        # Carry the pedal part through, but only under its own section.
        for part in t["parts"][1:]:
            pphr = part["passages"]["Main"]["phrases"][0]
            extra.append((sec, part, pre, pphr))

    parts = [{"name": "melody", "role": "melody", "passages": passages}]
    if extra:
        ped_passages = {}
        for sec, part, pre, pphr in extra:
            ped_passages[sec] = {
                "startingPitch": part["passages"]["Main"]["startingPitch"],
                "phrases": [{"name": "PED",
                             "startingPitch": pphr["startingPitch"],
                             "figures": [{"source": "reference",
                                          "motifName": pre + f["motifName"]}
                                         for f in pphr["figures"]],
                             "connectors": pphr["connectors"]}]}
        parts.append({"name": "pedal", "role": "bass", "passages": ped_passages})

    t = {"keyName": "C", "scaleName": "Major", "bpm": bpm, "masterSeed": seed,
         "motifs": [{"name": n, "figure": f, "userProvided": True}
                    for n, f in motifs.items()],
         "sections": sections, "parts": parts}
    return t, {"strategy": "suite", "n_entries": len(order),
               "order": "-".join(order),
               "beats": round(sum(s["beats"] for s in sections), 2)}


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
    ap.add_argument("--outroot", default=OUTROOT,
                    help="render dir (repo-relative); a new set goes in its "
                         "own dir so it can be A/B'd against the last one")
    args = ap.parse_args()
    outroot = args.outroot

    import markov_model as mm
    tok = (mm.TOKENS if args.corpus == "mtd"
           else mm.ROOT / f"{args.corpus.split('_')[0]}_tokens.json")
    model = MarkovModel.load(tok)
    cs = sg.corpus_stats(args.corpus)
    all_names = list(STRATEGIES) + ["suite", "chain_suite"]
    names = [args.only] if args.only else all_names
    fns = dict(STRATEGIES, suite=suite,
               chain_suite=lambda m, r, **kw: suite(
                   m, r, order=CHAIN_SUITE_ORDER, **kw))
    outdir = REPO / outroot
    outdir.mkdir(parents=True, exist_ok=True)

    rows = []
    for name in names:
        fn = fns[name]
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
            prefix = f"{outroot}/{name}_{k}"
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
