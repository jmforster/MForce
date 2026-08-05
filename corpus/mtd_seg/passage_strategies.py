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
             extra_parts=(), key_contexts=None):
    """motifs: {name: figure}; phrases: list of phrase() dicts (one passage).

    key_contexts: optional [(beat, "G Major"), ...] driving MODULATION. Inert
    before #6 stage 3 (Section::active_scale_at had zero callers); live now."""
    parts = [{"name": "melody", "role": "melody",
              "passages": {"Main": {
                  "startingPitch": {"octave": 4, "pitch": "C"},
                  "phrases": phrases}}}]
    parts.extend(extra_parts)
    section = {"name": "Main", "beats": beats}
    if key_contexts:
        kcs = []
        for kc in key_contexts:
            if isinstance(kc, dict):          # full form, may carry a
                kcs.append(dict(kc))          # scaleOverride (harmonic minor)
            else:
                b, k = kc
                kcs.append({"beat": float(b), "key": k})
        section["keyContexts"] = kcs
    return {"keyName": key, "scaleName": scale, "bpm": bpm, "masterSeed": seed,
            "motifs": [{"name": n, "figure": f, "userProvided": True}
                       for n, f in motifs.items()],
            "sections": [section],
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
    # Matt on the run-12 renders: "These are crazy! 8x acceleration is a bit
    # too much!" — 4 levels put the top level at 2^3 = 8x the opening note
    # values. Capping the LEVEL COUNT fixes that but also guts the passage
    # (measured: 9 notes, selfsim 1.0, composite 0.31), and the complaint was
    # about the acceleration, not the length. So the level count stays and the
    # DENSITY ratio saturates at 4x instead — later levels add duration at the
    # top speed rather than doubling again.
    n_lv = rng.choice([3, 4])
    # Same amplification problem as the chains: this cell is restated up to 7
    # times, so a monotone draw renders as one pitch throughout (measured:
    # selfsim 1.0, composite 0.57).
    base = _sample_chain_cell(model, rng, None, 2.0, 4.0)
    motifs, refs, anchors = {}, [], []
    # A buildup holds TIME constant and doubles DENSITY: level j runs the cell
    # at half the note values of level j-1, repeated twice as often, so every
    # level spans the same beats while the surface accelerates. (Levels of
    # equal span is what makes it read as a buildup rather than a rittardando
    # in reverse.)
    for j in range(n_lv):
        fig = base if j == 0 else ft.diminish(base, 2.0 ** min(j, 2))
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

    # Symmetric on both ends. Dropping the held note on the LAST entry only
    # (while keeping level 0's long final note in its mean) reported 8x for a
    # 4x diminution — measured: M0 [0.5, 0.5, 2.0] vs M2 [0.125, 0.125, 0.5],
    # which is exactly 4x. The acceleration number Matt reacted to was partly
    # this artifact.
    p_first = drive_pulse(refs[0], True)
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
        kw = {} if k is None else {"kmin": k, "kmax": k}
        cell = _sample_figure_sized(model, rng, beats_lo=lo, beats_hi=hi, **kw)
        k = k or len(cell["units"])
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
    # The cell must NET-ASCEND (>= 2 degrees): with a flat or falling cell the
    # cycle tops out at/below the fall target, the arpeggio degenerates and
    # lands ON the cycle start instead of just above it — caught mechanically
    # in run 13 (starts=[48,48] landings=[48,48], "almost but not quite"
    # violated), not by ear.
    cell = _sample_chain_cell(model, rng, 6, 1.5, 3.0)
    for _ in range(12):
        if net_step(cell) >= 2:
            break
        cand = _sample_chain_cell(model, rng, 6, 1.5, 3.0)
        if net_step(cand) > net_step(cell):
            cell = cand
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


# --------------------------------------------------------------------------- #
# strategy 6 — pedal point with rising harmonic tension (Matt's run-12 note)
# --------------------------------------------------------------------------- #
# Matt on the run-12 pedal renders: "8x acceleration is a bit too much!
# Acceleration is one strategy for injecting drama, but only one. The most
# common pedal point is CHORDS moving over the pedal tone, increasing tension
# via increasing dissonance (ie chords that do not contain the pedal tone),
# leading to a climactic cadence, say a french or german 6th to a tonic (still
# over the pedal so first inversion), followed by V(7) to a full cadence — this
# is practically a cliche in the Classical and Romantic literature."
#
# No chord plumbing is needed to do this: a chord is three held VOICES, and
# extra melodic parts already work (that is what pedal_part is). Accidentals
# make the augmented sixth reachable, exactly as they did the chromatic rise.
MAJOR_SEMIS = [0, 2, 4, 5, 7, 9, 11]
DISSONANT_ICS = {1, 2, 6, 10, 11}


def _deg_semis(deg):
    return 12 * (deg // 7) + MAJOR_SEMIS[deg % 7]


def _triad(root_deg):
    """Diatonic triad on a scale degree, as scale degrees."""
    return [root_deg, root_deg + 2, root_deg + 4]


def _tension(chord_degs, pedal_deg):
    """Dissonance of a chord against the pedal, measured not asserted.

    +2 if the chord does NOT contain the pedal pitch class (Matt's own
    criterion), +1 for every chord tone whose interval class against the pedal
    is a second, seventh or tritone."""
    ped = _deg_semis(pedal_deg) % 12
    pcs = [_deg_semis(d) % 12 for d in chord_degs]
    score = 0 if ped in pcs else 2
    for pc in pcs:
        if min((pc - ped) % 12, (ped - pc) % 12) in DISSONANT_ICS:
            score += 1
    return score


# Diatonic triad qualities of the major scale, by degree. Used only by the
# voiced path, which names a chord instead of spelling it.
TRIAD_QUALITY = ["Major", "m", "m", "Major", "Major", "m", "dim"]


def _pedal_progression(chosen, durs, cad_beats):
    """The same chord plan the hand-voiced path spells note by note, expressed
    as a progression the ENGINE voices (comp run 15).

    The German sixth is `bVI7` — degree 5, alteration -1, quality "7" — which
    resolves to Ab-C-Eb-Gb in C, enharmonically the Ger6. That chord was not
    authorable before this run: the flat progression form dropped
    `alteration`. Note it is a FULLER Ger6 than the hand-voiced one, which
    spells Ab-Eb-F# (root, fifth, augmented sixth, no third)."""
    prog = [{"degree": d, "quality": TRIAD_QUALITY[d % 7], "beats": durs[i]}
            for i, d in enumerate(chosen)]
    # The cadence chords PIN their voicing (Matt, run-15 audition of the
    # smooth A/B): "The German 6th to tonic (6/4) arrival needs to be
    # root-position bVI7 > 2nd inversion tonic, so in C major bottom notes of
    # chord move Ab > G, top note Gb > G." A motion-minimizer cannot be
    # trusted with that seam — the voicing IS the chord's cadential identity.
    #   Ger6 pin: inversion 0 -> Ab C Eb Gb (bass Ab, top Gb already).
    #   I(6/4) pin: inversion 2 -> bass G; topTone 2 doubles the G on top.
    # Octave 3 pins the cadence into the register the smooth selector had
    # already drifted the diatonic chords to (the register Matt auditioned) —
    # at the default octave 4 the whole Ger6 lurches up an octave instead of
    # arriving by semitone.
    prog += [
        {"degree": 5, "alteration": -1, "quality": "7", "beats": cad_beats[0],
         "pin": {"inversion": 0, "octave": 3}},
        {"degree": 0, "quality": "Major", "beats": cad_beats[1],
         "pin": {"inversion": 2, "topTone": 2, "octave": 3}},
        {"degree": 4, "quality": "7", "beats": cad_beats[2]},
        {"degree": 0, "quality": "Major", "beats": cad_beats[3]},
    ]
    return prog


def pedal_chords(model, rng, bpm=92.0, seed=1, melody=True, bars=None,
                 voiced=False, selector=""):
    """Dominant pedal; diatonic triads above it ORDERED by measured dissonance
    against the pedal, then a German-sixth cadence — Ger6 -> I(6/4, still over
    the pedal) -> V7 -> I.

    melody=True is Matt's variant b (a melodic figure over the chords);
    melody=False is variant a (chords only — the melody part carries the TOP
    chord voice so the exported piece still has a parts[0] line to score).

    voiced=True routes the chords through the engine's harmony tier instead of
    spelling them as countermelody voices — the capability comp run 15 added
    (PassageTemplate.chordProgression). Everything upstream of part
    construction is shared, and no rng draw differs, so voiced/unvoiced at the
    same seed is a controlled A/B of the two ways to get the same chords."""
    pedal_deg = 4                                  # dominant pedal (G in C)
    bars = bars or rng.choice([4, 5, 6])
    # Rank the diatonic triads by tension against the pedal and walk UP that
    # ranking, so "increasing dissonance" is a property of the render rather
    # than of the docstring. Always open on the tonic.
    cands = sorted(((_tension(_triad(d), pedal_deg), d) for d in range(7)),
                   key=lambda x: (x[0], x[1]))
    ordered = [0] + [d for _t, d in cands if d != 0]
    chosen = ordered[:bars]
    chosen.sort(key=lambda d: _tension(_triad(d), pedal_deg))
    tension_curve = [_tension(_triad(d), pedal_deg) for d in chosen]

    # (degree, accidental) per voice per chord. Voices 0/1/2 = the triad from
    # the bottom; the pedal sits underneath in its own part.
    chords = [[(d, 0) for d in _triad(r)] for r in chosen]
    labels = [DEGREE_NAMES[r % 7] for r in chosen]

    # Cadence, hand-voiced. German 6th in C = Ab-C-Eb-F#: flat-6, tonic,
    # flat-3, sharp-4 — all reachable as (degree, accidental) pairs. It
    # resolves to the tonic in 6/4 (the pedal is still the bass, so the tonic
    # triad above a G bass IS second inversion), then V7, then I.
    cad = [
        ([(5, -1), (2 + 7, -1), (3 + 7, +1)], "Ger6"),   # Ab  Eb  F#
        ([(0, 0), (2 + 7, 0), (4 + 7, 0)], "I(6/4)"),    # C   E   G
        ([(6, 0), (1 + 7, 0), (3 + 7, 0)], "V7"),        # B   D   F
        ([(0, 0), (2 + 7, 0), (4 + 7, 0)], "I"),         # C   E   G
    ]
    chords += [c for c, _l in cad]
    labels += [l for _c, l in cad]
    tension_curve += [_tension([d for d, _a in c], pedal_deg)
                      for c, _l in cad]

    beats_per = 4.0
    cad_beats = [4.0, 4.0, 4.0, 6.0]                 # the arrival broadens
    durs = [beats_per] * len(chosen) + cad_beats
    total = sum(durs)

    if voiced:
        # The chords as CHORDS: one harmony part carrying its own progression.
        # The voicing tier (selector / dictionary / inversion choice) does the
        # spelling, which the hand-voiced path bypasses entirely by pre-voicing
        # every note. Requires melody=True — with no VC2 motif there would be
        # nothing to put in the melody slot.
        melody = True
        hpass = {"chordConfig": {"octave": 4},
                 "chordProgression": _pedal_progression(chosen, durs, cad_beats)}
        # Without a selector the engine emits root-position triads (the legacy
        # inversion/spread path). A selector makes it choose voicings — which
        # is the half of the harmony tier the hand-voiced path can never reach.
        if selector:
            hpass["voicingSelector"] = selector
        extra = [{"name": "chords", "role": "harmony", "passages": {"Main": hpass}}]
        n_voice_parts = 0
    else:
        extra = []
        n_voice_parts = 3 if melody else 2   # chords-only: VC2 IS the melody
    # One melodic part per voice: units carry the degree DELTA from the voice's
    # previous note (the engine's cursor convention) plus any accidental.
    for v in range(n_voice_parts):
        # NOT role "harmony": that role produced ZERO events (the composer
        # realizes harmony parts from a chord progression, not from phrases).
        # Verified by inspecting the piece JSON, which is also why the voices
        # are countermelody parts carrying explicit held notes.
        # Each voice must ENTER on its own chord tone. A figure's first unit is
        # step 0 by convention, so with a shared startingPitch all three voices
        # rendered the same line in unison (verified in the piece JSON) — the
        # entry pitch is what separates them.
        vpitch = ["C", "D", "E", "F", "G", "A", "B"][chords[0][v][0] % 7]
        voct = 4 + chords[0][v][0] // 7
        extra.append({"name": f"voice{v}", "role": "countermelody",
                      "passages": {"Main": {
                          "startingPitch": {"octave": voct, "pitch": vpitch},
                          "phrases": [phrase(f"V{v}", [f"VC{v}"], [None],
                                             octave=voct, pitch=vpitch)]}}})

    # The pedal itself: held whole notes on the dominant under everything —
    # but it RELEASES to the tonic for the final chord. Matt asked for "V(7)
    # to a full cadence", and a full cadence needs the root in the bass; held
    # to the end the pedal leaves the last tonic in 6/4, which is an arrival
    # that never actually lands.
    held = total - cad_beats[-1]
    reps = max(1, int(round(held / 4.0)))
    motifs = {"PED": {"units": [{"duration": 4.0, "step": 0}]},
              "PEDRES": {"units": [{"duration": cad_beats[-1], "step": 0}]}}
    for v in range(3):
        units, prev = [], None
        for ci, ch in enumerate(chords):
            deg, acc = ch[v]
            u = {"duration": durs[ci], "step": 0 if prev is None else deg - prev}
            if acc:
                u["accidental"] = acc
            units.append(u)
            prev = deg
        motifs[f"VC{v}"] = {"units": units}

    if melody:
        # Matt's variant b: "same as above but with a melodic figure over the
        # chords". The melody is the top line, restating a cell once per chord.
        cell = _sample_chain_cell(model, rng, 4, 2.0, 4.0)
        mel_refs = ["MEL"] * len(chords)
        motifs["MEL"] = cell
        mel_anchors = [7 + (ch[2][0] - chords[0][2][0]) for ch in chords]
        mel_figs = [cell] * len(chords)
        mel_conns = anchor_connectors(mel_figs, mel_anchors)
        mel_phrase = phrase("P", mel_refs, mel_conns)
    else:
        # Variant a: the top chord voice moves into the melody slot, so the
        # texture is exactly pedal + three chord voices and nothing above.
        vpitch = ["C", "D", "E", "F", "G", "A", "B"][chords[0][2][0] % 7]
        voct = 4 + chords[0][2][0] // 7
        mel_phrase = phrase("P", ["VC2"], [None], octave=voct, pitch=vpitch)

    bass = {"name": "pedal", "role": "bass",
            "passages": {"Main": {
                "startingPitch": {"octave": 3, "pitch": "G"},
                "phrases": [phrase("PED", ["PED"] * reps + ["PEDRES"],
                                   [None] + [0] * (reps - 1) + [-4],
                                   octave=3, pitch="G")]}}}
    t = template(motifs, [mel_phrase],
                 beats=total, bpm=bpm, seed=seed,
                 extra_parts=tuple(extra) + (bass,))
    return t, {"strategy": ("pedal_chords_voiced" if voiced else
                            "pedal_chords" if melody else "pedal_chords_only"),
               "chord_source": "harmony_part" if voiced else "countermelody",
               "n_entries": len(chords),
               "progression": "-".join(labels),
               "tension_curve": "-".join(str(x) for x in tension_curve),
               "tension_rises": tension_curve[-4] >= tension_curve[0],
               "pedal_degree": pedal_deg, "beats": total}


# --------------------------------------------------------------------------- #
# strategy 7 — REAL modulating circle of fifths (unblocked by #6 stage 3)
# --------------------------------------------------------------------------- #
# Matt, run 12: "Sequence: sounds a lot like Connective due to key center
# limitation noted." The limitation was that section keyContexts had no reader,
# so the fifths "trip" was a diatonic walk that never left C. Stage 3 landed in
# run 13, so the trip can now actually modulate: the cell restates at the SAME
# scale degrees while the key underneath it moves by fifths, which is what
# makes a sequence a modulating sequence rather than a transposition.
FIFTHS_UP = ["C", "G", "D", "A", "E", "B"]
FIFTHS_DOWN = ["C", "F", "B Flat", "E Flat", "A Flat"]


def modulating_fifths(model, rng, bpm=92.0, seed=1, direction=None):
    """One cell restated once per key while the KEY walks the circle of
    fifths. Each restatement re-enters on the same degree, so the transposition
    is done by the key change, not by the anchors."""
    up = rng.choice([True, False]) if direction is None else direction
    ring = FIFTHS_UP if up else FIFTHS_DOWN
    n = rng.choice([4, 5])
    keys = ring[:n]
    cell = _sample_chain_cell(model, rng, 4, 2.0, 4.0)
    motifs = {"S": cell}
    refs = ["S"] * n
    per = total_beats(cell)
    # Stage 3 snaps the cursor's PITCH into the new scale, it does not move it
    # to the new tonic — so anchoring every entry at degree 0 renders C and G
    # identically (measured: C-E-E-C in both). To make it a real SEQUENCE the
    # entry has to follow the key, so each restatement is offset by a fifth in
    # the new scale's degree space, alternating direction to stay in register.
    anchors, a = [0], 0
    for j in range(1, n):
        a += 4 if (j % 2) else -3            # up a 5th / down a 4th
        anchors.append(a)
    figs = [cell] * n
    conns = anchor_connectors(figs, anchors)
    kctx = [(i * per, f"{keys[i]} Major") for i in range(n)]
    t = template(motifs, [phrase("P", refs, conns)], beats=per * n, bpm=bpm,
                 seed=seed, key_contexts=kctx)
    return t, {"strategy": "modulating_fifths", "n_entries": n,
               "keys": "-".join(keys),
               "direction": "up" if up else "down",
               "beats_per_key": per}


# --------------------------------------------------------------------------- #
# strategy 8 — Bruckner: the harmony MODULATES over a stationary pedal
# --------------------------------------------------------------------------- #
# Matt, run 12: "Bruckner extreme = modulating over the pedal (needs key fix)."
# It needed two fixes, not one. Stage 3 (run 13) made MELODY key-aware; chord
# realization still resolved every ScaleChord against the section scale, so a
# progression under keyContexts stayed put. Comp run 15 made chords key-aware
# too, and gave a passage its own progression — with both, one authored
# progression walks through keys while the pedal does not move.
#
# The key plan is chromatic-third related, which is the Bruckner sound and also
# the point of a pedal: G is the 5th of C, the 3rd of Eb, the b7 of A — the
# SAME held note is consonant, then differently consonant, then dissonant,
# without the bass moving at all. Gb is the outlier: G belongs to no Gb triad,
# which is the maximum-tension station.
BRUCKNER_RINGS = {
    # (key, how the held G sits in it)
    "thirds_down": ["C", "A Flat", "E", "C"],       # G = 5th, 7th, #4(!), 5th
    "thirds_up":   ["C", "E", "A Flat", "C"],
    "minor3rds":   ["C", "E Flat", "G Flat", "C"],  # G = 5th, 3rd, alien, 5th
}


def pedal_modulating(model, rng, bpm=92.0, seed=1, ring=None, selector="smooth"):
    """Chords modulating over a stationary dominant pedal, closing with the
    Ger6 cadence back in the home key — Matt's Bruckner extreme.

    Each key station gets the same local progression (I - vi - IV - V), so what
    changes between stations is the KEY, not the chord plan: the ear hears the
    same shape re-lit three times while the bass refuses to move. The cadence
    then arrives back in C with the pedal releasing to the tonic, so the
    passage lands rather than just stopping."""
    name = ring or rng.choice(list(BRUCKNER_RINGS))
    keys = BRUCKNER_RINGS[name]
    station = [(0, "Major"), (5, "m"), (3, "Major"), (4, "7")]   # I vi IV V7
    per_chord = 2.0
    per_station = per_chord * len(station)

    prog, labels = [], []
    for ki, k in enumerate(keys):
        for deg, qual in station:
            prog.append({"degree": deg, "quality": qual, "beats": per_chord})
            labels.append(f"{k}:{DEGREE_NAMES[deg]}")
    # Cadence in the home key: Ger6 -> I(6/4 over the pedal) -> V7 -> I.
    cad = [({"degree": 5, "alteration": -1, "quality": "7"}, "Ger6"),
           ({"degree": 0, "quality": "Major"}, "I(6/4)"),
           ({"degree": 4, "quality": "7"}, "V7"),
           ({"degree": 0, "quality": "Major"}, "I")]
    cad_beats = [4.0, 4.0, 4.0, 6.0]
    for (c, lab), b in zip(cad, cad_beats):
        prog.append(dict(c, beats=b))
        labels.append(lab)
    total = per_station * len(keys) + sum(cad_beats)

    # The key stations cover the modulating span only; the cadence stays home,
    # which is why the last context is C at the start of the cadence.
    kctx = [(i * per_station, f"{keys[i]} Major") for i in range(len(keys))]
    kctx.append((per_station * len(keys), "C Major"))

    # Melody: one cell per key station, re-entering at the same degree so the
    # KEY does the transposing (the modulating_fifths lesson, applied here).
    cell = _sample_chain_cell(model, rng, 4, per_station * 0.5, per_station)
    n_mel = len(keys)
    motifs = {"MEL": cell,
              "PED": {"units": [{"duration": per_station, "step": 0}]},
              "PEDRES": {"units": [{"duration": sum(cad_beats), "step": 0}]}}
    mel_refs = ["MEL"] * n_mel
    mel_anchors = [0] * n_mel
    mel_conns = anchor_connectors([cell] * n_mel, mel_anchors)

    chords = {"name": "chords", "role": "harmony",
              "passages": {"Main": {"chordConfig": {"octave": 4},
                                    "voicingSelector": selector,
                                    "chordProgression": prog}}}
    # The pedal holds the dominant through every key, then releases a fifth to
    # the tonic for the final chord — same reasoning as pedal_chords: held to
    # the end, the arrival is a 6/4 that never lands.
    # scaleOverride pins the pedal to the home scale, which is also how it
    # opts OUT of the section's key contexts. Without it the pedal is
    # key-aware like every other part and its held G gets snapped into each
    # new scale — measured before the fix: 43-43-42-41 instead of a held 43.
    # A pedal that moves is not a pedal.
    bass = {"name": "pedal", "role": "bass",
            "passages": {"Main": {
                "startingPitch": {"octave": 3, "pitch": "G"},
                "scaleOverride": "Major",
                "phrases": [phrase("PED", ["PED"] * len(keys) + ["PEDRES"],
                                   [None] + [0] * (len(keys) - 1) + [-4],
                                   octave=3, pitch="G")]}}}

    t = template(motifs, [phrase("P", mel_refs, mel_conns)],
                 beats=total, bpm=bpm, seed=seed,
                 extra_parts=(chords, bass), key_contexts=kctx)
    return t, {"strategy": "pedal_modulating", "ring": name,
               "keys": "-".join(keys), "n_entries": len(prog),
               "progression": "-".join(labels),
               "beats_per_station": per_station, "beats": total}


# --------------------------------------------------------------------------- #
# strategy 8b — Bruckner pedal v2 (Matt's run-15 verdict on the v1 renders)
# --------------------------------------------------------------------------- #
# Matt: pedal_mod_minor3rds was the best of v1, with one structural bug (his
# point d): under the Ger6 the G pedal was CHANGED to a C pedal, which made
# the final C arrive non-inverted. Wrong. v2: the G pedal stays G under the
# Ger6 (maximum dissonance), under the C(6/4), and under the plain G triad —
# the bass moves to C ONLY on the final chord. And NO G7/V7 anywhere ("that
# just weakens the arrival"): the dominant before the final tonic is a plain
# G triad.
#
# Both takes walk the same 10 chords over the pedal; they differ only in the
# pre-dominant: German sixth (bVI7 = Ab-C-Eb-Gb) vs Neapolitan (bVI major =
# Ab-C-Eb). Chords are authored absolutely — the quality carries the
# chromatics (A7's C#, Bm's F#, Bdim7's Ab) — so the whole progression lives
# in C major with no keyContexts: nothing can drift, and the pedal needs no
# scaleOverride to stay put. The two cadence chords pin their voicing (the
# run-15 smooth caveat): root-position bVI, then bass-G tonic with the G
# doubled on top.
BRUCKNER2_CADENCE = {
    "ger6": {"degree": 5, "alteration": -1, "quality": "7"},
    "neap": {"degree": 5, "alteration": -1, "quality": "Major"},
}


def bruckner2(model, rng, bpm=92.0, seed=1, cadence="ger6", per_chord=4.0):
    """G > Em > A7 > D > Bm > Bdim7 > {Ger6|Neapolitan} > C(6/4) > G > C,
    all over a G pedal that releases to C only on the final chord. Fully
    authored — model and rng are unused, kept for registry parity."""
    pre = dict(BRUCKNER2_CADENCE[cadence], pin={"inversion": 0})
    stations = [
        ({"degree": 4, "quality": "Major"}, "V"),
        ({"degree": 2, "quality": "m"}, "iii"),
        ({"degree": 5, "quality": "7"}, "VI7"),
        ({"degree": 1, "quality": "Major"}, "II"),
        ({"degree": 6, "quality": "m"}, "vii-m"),
        ({"degree": 6, "quality": "dim7"}, "viio7"),
        (pre, "Ger6" if cadence == "ger6" else "bVI(N)"),
        ({"degree": 0, "quality": "Major",
          "pin": {"inversion": 2, "topTone": 2}}, "I(6/4)"),
        ({"degree": 4, "quality": "Major"}, "V"),      # plain triad, NO 7th
        ({"degree": 0, "quality": "Major"}, "I"),
    ]
    final_beats = 2.0 * per_chord                      # the arrival broadens
    prog = [dict(c, beats=final_beats if i == len(stations) - 1 else per_chord)
            for i, (c, _lab) in enumerate(stations)]
    total = per_chord * (len(stations) - 1) + final_beats

    # The pedal holds G3 under every chord including the G triad; connector
    # -4 drops it a fifth to C3 exactly at the final chord's downbeat.
    reps = len(stations) - 1
    motifs = {"PED": {"units": [{"duration": per_chord, "step": 0}]},
              "PEDRES": {"units": [{"duration": final_beats, "step": 0}]}}
    t = {"keyName": "C", "scaleName": "Major", "bpm": bpm, "masterSeed": seed,
         "motifs": [{"name": n, "figure": f, "userProvided": True}
                    for n, f in motifs.items()],
         "sections": [{"name": "Main", "beats": total}],
         "parts": [
             {"name": "pedal", "role": "bass",
              "passages": {"Main": {
                  "startingPitch": {"octave": 3, "pitch": "G"},
                  "phrases": [phrase("PED", ["PED"] * reps + ["PEDRES"],
                                     [None] + [0] * (reps - 1) + [-4],
                                     octave=3, pitch="G")]}}},
             {"name": "chords", "role": "harmony",
              "passages": {"Main": {"chordConfig": {"octave": 4},
                                    "voicingSelector": "smooth",
                                    "chordProgression": prog}}},
         ]}
    return t, {"strategy": f"bruckner2_{cadence}",
               "n_entries": len(stations),
               "progression": "-".join(lab for _c, lab in stations),
               "per_chord": per_chord, "beats": total}


# --------------------------------------------------------------------------- #
# strategy 8 — modulating wandering (Matt's run-12 cliche, unblocked by stage 3)
# --------------------------------------------------------------------------- #
# Matt: "sweet major theme -> sudden diminished/minor turn -> wander minor and
# major keys -> return". The v1 `wandering` wandered in REGISTER only; this one
# wanders in KEY as well, via section keyContexts. The minor stops use
# HARMONIC minor (KeyContext.scaleOverride) — natural minor of the relative
# key is pc-identical to the major it left, so the turn would be inaudible
# and unverifiable; the raised 7th is both the cliche sound and the
# mechanical evidence that the key actually moved.
# Exact Python replica of the engine's key-aware walk (pitch_walker.h
# snap_to_scale/step_note + composer.h's realize loop: snap on scale change at
# note start, then step_note(currentNN, leadStep+step)). Needed so the
# generator can GUARANTEE the altered-key spans actually sound an accidental —
# a wander line that never touches the one altered degree renders a
# "modulation" that is mechanically indistinguishable from staying home
# (caught on suite_v2's first render, S0 A-minor span: 6 notes, 0 accidentals).
ASC_STEPS = {"Major": [2, 2, 1, 2, 2, 2, 1],
             "Minor": [2, 1, 2, 2, 1, 2, 2],
             "Harmonic Minor": [2, 1, 2, 2, 1, 3, 1]}
PC_OF = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def _scale_of(kc):
    ovr = kc.get("scaleOverride")
    if ovr:
        return PC_OF[ovr["pitch"]], ASC_STEPS[ovr["scale"]]
    words = kc["key"].split()
    pc = PC_OF[words[0]]
    for w in words[1:-1]:
        pc += 1 if w == "Sharp" else -1
    return pc % 12, ASC_STEPS[words[-1]]


def _deg_pos(nn, root, asc):
    rel = nn - root
    while rel < 0:
        rel += 12
    pos = rel % 12
    accum, deg = 0.0, 0
    for d in range(len(asc)):
        if abs(accum - pos) < 0.5:
            deg = d
            break
        accum += asc[d]
    return deg


def _step_note(nn, steps, root, asc):
    if steps > 0:
        for _ in range(steps):
            nn += asc[_deg_pos(nn, root, asc) % len(asc)]
    else:
        for _ in range(-steps):
            deg = _deg_pos(nn, root, asc)
            nn -= asc[(deg - 1 + len(asc)) % len(asc)]
    return nn


def _snap(nn, root, asc):
    rel = nn - root
    octaves = rel // 12
    pos = rel - octaves * 12
    best, bestd, accum = 0, 999.0, 0.0
    for _d in range(len(asc) + 1):
        dist = abs(accum - pos)
        if dist < bestd:
            bestd, best = dist, root + octaves * 12 + accum
        if _d < len(asc):
            accum += asc[_d]
    return best


def predict_modulating_notes(figs, conns, kctx, start_nn=48):
    """(beat, nn) per note, exactly as the engine will render them."""
    nn, beat, out = start_nn, 0.0, []
    root, asc = _scale_of(kctx[0])
    for fi, fig in enumerate(figs):
        for ui, u in enumerate(fig["units"]):
            r2, a2 = root, asc
            for kc in kctx:
                if kc["beat"] <= beat + 1e-6:
                    r2, a2 = _scale_of(kc)
            if (r2, a2) != (root, asc):
                nn = _snap(nn, r2, a2)
                root, asc = r2, a2
            lead = conns[fi] if (ui == 0 and conns[fi] is not None) else 0
            nn = _step_note(nn, lead + u["step"], root, asc)
            out.append((beat, int(nn)))
            beat += u["duration"]
    return out


def _altered_span_coverage(figs, conns, kctx, beats_total):
    """(covered, total) over keyContext spans whose scale differs from the
    home major scale: covered = spans with >=1 predicted note outside it."""
    home = set()
    root, asc = _scale_of(kctx[0])
    acc = 0
    for s in asc:
        home.add((root + acc) % 12)
        acc += s
    notes = predict_modulating_notes(figs, conns, kctx)
    covered = total = 0
    for i, kc in enumerate(kctx):
        r, a = _scale_of(kc)
        pcs, accum = set(), 0
        for s in a:
            pcs.add((r + accum) % 12)
            accum += s
        if pcs == home:
            continue
        total += 1
        end = kctx[i + 1]["beat"] if i + 1 < len(kctx) else beats_total
        span = [n for b, n in notes if kc["beat"] - 1e-6 <= b < end - 1e-6]
        if any(n % 12 not in home for n in span):
            covered += 1
    return covered, total


KEY_PLAN_MILD = [                                  # 2 key moves
    ("C Major", None),
    ("A Minor", {"pitch": "A", "scale": "Harmonic Minor"}),
    ("C Major", None),
]
KEY_PLAN_ADV = [                                   # 5 key moves, minor turns
    ("C Major", None),
    ("A Minor", {"pitch": "A", "scale": "Harmonic Minor"}),
    ("F Major", None),
    ("D Minor", {"pitch": "D", "scale": "Harmonic Minor"}),
    ("G Major", None),
    ("C Major", None),
]


def wandering_modulating(model, rng, bpm=92.0, seed=1, adventurous=False):
    """Theme (one figure, stated twice) in the home key, then wandering figures
    with the KEY moving under them, then the theme again at home — the return
    is a restatement, not just a key signature."""
    plan = KEY_PLAN_ADV if adventurous else KEY_PLAN_MILD
    # Figures per key stop: theme x2 in the first stop, wander figures in the
    # middle stops, theme restatement in the last.
    figs_per_stop = [2] + [2 if not adventurous else 1] * (len(plan) - 2) + [1]

    # Accidental guard: resample until every altered span provably sounds a
    # note outside C major (predicted with the engine's own snap/walk math) —
    # otherwise the "modulation" can render pc-identical to staying home.
    # Best-coverage kept: with 4 short altered spans (adv) full coverage is a
    # low-probability joint event, so exhausting the tries must still return
    # the candidate with the MOST audible key changes, not the last draw.
    best, best_cov, tot = None, -1, 0
    for _guard in range(60):
        built = _build_wandering_modulating(model, rng, plan, figs_per_stop)
        figs, conns, kctx, _m, _r, _a = built
        cov, tot = _altered_span_coverage(figs, conns, kctx,
                                          sum(total_beats(f) for f in figs))
        if cov > best_cov:
            best, best_cov = built, cov
        if cov == tot:
            break
    figs, conns, kctx, motifs, refs, anchors = best

    beats = sum(total_beats(f) for f in figs)
    t = template(motifs, [phrase("P", refs, conns)], beats=beats, bpm=bpm,
                 seed=seed, key_contexts=kctx)
    return t, {"strategy": ("wandering_mod_adv" if adventurous
                            else "wandering_mod_mild"),
               "n_entries": len(refs), "key_moves": len(plan) - 1,
               "keys": "-".join(k for k, _o in plan),
               "guard_tries": _guard + 1,
               "altered_span_coverage": f"{best_cov}/{tot}",
               "anchor_span": max(anchors) - min(anchors), "beats": beats}


def _build_wandering_modulating(model, rng, plan, figs_per_stop):
    theme = _sample_chain_cell(model, rng, None, 2.0, 3.5)
    motifs, refs, anchors = {"T": theme}, [], []
    cursor, w = 0, 0
    stop_starts_figidx = []
    for si, n_figs in enumerate(figs_per_stop):
        stop_starts_figidx.append(len(refs))
        for j in range(n_figs):
            first_stop, last_stop = si == 0, si == len(figs_per_stop) - 1
            if first_stop or last_stop:
                name, fig = "T", theme            # the theme frames the walk
            else:
                fig = _sample_figure_sized(model, rng, beats_lo=1.5,
                                           beats_hi=3.0)
                name = f"W{w}"
                motifs[name] = fig
                w += 1
            if not refs:
                anchors.append(0)
            elif last_stop:
                anchors.append(0)                 # the return comes HOME
            else:
                pull = (-2 if cursor > 5 else
                        (2 if cursor < -5 else rng.choice([-1, 0, 1])))
                anchors.append(cursor + pull)
            cursor = anchors[-1] + net_step(fig)
            refs.append(name)

    figs = [motifs[r] for r in refs]
    conns = anchor_connectors(figs, anchors)
    # Key contexts at the stop boundaries, in beats.
    fig_starts, b = [], 0.0
    for f in figs:
        fig_starts.append(b)
        b += total_beats(f)
    kctx = []
    for si, (key, ovr) in enumerate(plan):
        kc = {"beat": fig_starts[stop_starts_figidx[si]], "key": key}
        if ovr:
            kc["scaleOverride"] = ovr
        kctx.append(kc)
    return figs, conns, kctx, motifs, refs, anchors


STRATEGIES = {
    "pedal_buildup": pedal_buildup,
    # Matt's verdict on the run-15 A/B (hand vs voiced vs smooth): "Smooth is
    # best" — so pedal_chords now IS the engine-voiced smooth path, with the
    # Ger6 -> I(6/4) seam pinned (see _pedal_progression). The hand-voiced
    # path stays reachable as pedal_chords_hand for archaeology.
    "pedal_chords":
        lambda m, r, **kw: pedal_chords(m, r, voiced=True, selector="smooth",
                                        **kw),
    "pedal_chords_hand": pedal_chords,
    "pedal_chords_only":
        lambda m, r, **kw: pedal_chords(m, r, melody=False, **kw),
    "modulating_fifths": modulating_fifths,
    "pedal_modulating": pedal_modulating,
    "pedal_mod_minor3rds":
        lambda m, r, **kw: pedal_modulating(m, r, ring="minor3rds", **kw),
    "bruckner2_ger6":
        lambda m, r, **kw: bruckner2(m, r, cadence="ger6", **kw),
    "bruckner2_neap":
        lambda m, r, **kw: bruckner2(m, r, cadence="neap", **kw),
    "modulating_fifths_up":
        lambda m, r, **kw: modulating_fifths(m, r, direction=True, **kw),
    "modulating_fifths_down":
        lambda m, r, **kw: modulating_fifths(m, r, direction=False, **kw),
    "wandering": wandering,
    "wandering_mod_mild": wandering_modulating,
    "wandering_mod_adv":
        lambda m, r, **kw: wandering_modulating(m, r, adventurous=True, **kw),
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
# suite v2 — the run-13 shapes chained: modulating wander -> chromatic-peak
# chain -> pedal chords with melody (which ends in the full cadence, so it is
# the closer). Unlike suite(), this lifts EVERYTHING a sub-template built —
# every part (chord voices, pedal) and the section's keyContexts (their beats
# are section-relative, verified in composer.h: active_scale_at is called with
# currentBeat - passageBeatOffset) — so strategies with texture and key motion
# survive the chaining intact.
# --------------------------------------------------------------------------- #
SUITE_V2_ORDER = [
    ("wandering_mod_mild", "wandering_modulating", {}),
    ("chain_chromatic_peak", "chain_chromatic_peak", {}),
    ("pedal_chords", "pedal_chords", {"bars": 4}),   # 4 bars keeps the total
]                                                    # inside the 45-70 aim


def suite_v2(model, rng, bpm=92.0, seed=1):
    fns = {"wandering_modulating": wandering_modulating,
           "chain_chromatic_peak": chain_chromatic_peak,
           "pedal_chords": pedal_chords}
    motifs, sections, parts, metas = {}, [], {}, []
    for idx, (label, fname, kw) in enumerate(SUITE_V2_ORDER):
        t, meta = fns[fname](model, rng, bpm=bpm, seed=seed + idx, **kw)
        metas.append(meta)
        pre = f"s{idx}_"
        sec_src = t["sections"][0]
        sec_name = f"S{idx}_{label}"
        sec = {"name": sec_name, "beats": sec_src["beats"]}
        if "keyContexts" in sec_src:
            sec["keyContexts"] = sec_src["keyContexts"]
        sections.append(sec)
        for m in t["motifs"]:
            motifs[pre + m["name"]] = m["figure"]
        for part in t["parts"]:
            pd = parts.setdefault(part["name"],
                                  {"name": part["name"], "role": part["role"],
                                   "passages": {}})
            src = part["passages"]["Main"]
            pd["passages"][sec_name] = {
                "startingPitch": src["startingPitch"],
                "phrases": [dict(ph, figures=[{"source": "reference",
                                               "motifName": pre + f["motifName"]}
                                              for f in ph["figures"]])
                            for ph in src["phrases"]]}

    ordered = ([parts["melody"]]
               + [p for n, p in parts.items() if n != "melody"])
    t = {"keyName": "C", "scaleName": "Major", "bpm": bpm, "masterSeed": seed,
         "motifs": [{"name": n, "figure": f} for n, f in motifs.items()],
         "sections": sections, "parts": ordered}
    for m in t["motifs"]:
        m["userProvided"] = True
    return t, {"strategy": "suite_v2", "n_entries": len(SUITE_V2_ORDER),
               "order": "-".join(l for l, _f, _k in SUITE_V2_ORDER),
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
    ap.add_argument("--set", dest="which_set", default=None,
                    help="'v2' = the run-13 Matt-spec set with per-strategy "
                         "take counts (~15 renders)")
    args = ap.parse_args()
    outroot = args.outroot

    import markov_model as mm
    tok = (mm.TOKENS if args.corpus == "mtd"
           else mm.ROOT / f"{args.corpus.split('_')[0]}_tokens.json")
    model = MarkovModel.load(tok)
    cs = sg.corpus_stats(args.corpus)
    all_names = list(STRATEGIES) + ["suite", "chain_suite", "suite_v2"]
    fns = dict(STRATEGIES, suite=suite,
               chain_suite=lambda m, r, **kw: suite(
                   m, r, order=CHAIN_SUITE_ORDER, **kw),
               suite_v2=suite_v2)

    # The v2 set: every shape Matt spec'd after auditioning run 12, with take
    # counts weighted toward the three chain->goal transformations.
    V2_SET = [("chain_chromatic_peak", 2), ("chain_tonic_fanfare", 2),
              ("chain_arpeggio_fallback", 2), ("pedal_buildup", 1),
              ("pedal_chords", 1), ("pedal_chords_only", 1),
              ("wandering_mod_mild", 1), ("wandering_mod_adv", 2),
              ("modulating_fifths_up", 1), ("modulating_fifths_down", 1),
              ("suite_v2", 1)]
    if args.which_set == "v2":
        names = [n for n, _t in V2_SET]
        jobs = [(n, k) for n, takes in V2_SET for k in range(takes)]
    else:
        names = [args.only] if args.only else all_names
        jobs = [(n, k) for n in names for k in range(args.takes)]

    outdir = REPO / outroot
    outdir.mkdir(parents=True, exist_ok=True)

    rows = []
    for name, k in jobs:
        fn = fns[name]
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
        meta["strategy"] = name          # registered name, so up/down and
        meta["pred_range"] = span        # only-variants group separately
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
