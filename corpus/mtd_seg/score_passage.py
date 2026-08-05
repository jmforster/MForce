"""Passage-mode scoring (comp backlog #15).

The phrase screens in `score_generated.py` mis-score passages BY DESIGN. They
ask "does this look like an MTD theme": first-order interval plausibility,
enough-but-not-too-much repetition, a corpus-shaped ending. A passage is a
different object — a pedal buildup, a sequence, a wandering modulation, a
connective chain. Run 15 measured the mismatch: a Bruckner take with 16 notes
over 50 beats scores 0.719 for reasons that have nothing to do with whether it
works, and the repetition screen punishes a sequence for BEING a sequence.

Passage mode scores SHAPE instead:

  tension    does the passage build? A windowed tension curve (vertical
             dissonance + note density + register) and where its peak sits.
  arrival    does the ending LAND? final sonority consonant, tension released
             into it, final event on a downbeat, final event long.
  coherence  is it made of recurring material? Passages are SUPPOSED to
             repeat — here recurrence is rewarded inside a band instead of
             penalized past a corpus p95.

Nothing here is corpus-fitted: there is no passage corpus. These are stated
musical criteria, weighted explicitly, and the file's own --degrade harness is
the evidence that they measure what they claim (see verify()).

  python score_passage.py renders/passage_bruckner2/*_1.json
  python score_passage.py --degrade renders/passage_bruckner2/bruckner2_ger6_0_1.json
"""
import json
import statistics
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

WINDOWS = 8

# Interval-class dissonance weights, folded to ic 0..6 (unison, m2, M2, m3,
# M3, P4/P5, tritone). Ordinary consonance/dissonance ranking — deliberately
# coarse: this measures whether tension MOVES, not what a chord is called.
IC_WEIGHT = [0.0, 1.0, 0.6, 0.2, 0.2, 0.1, 0.8]


PC_NAMES = {"C": 0, "C#": 1, "DB": 1, "D": 2, "D#": 3, "EB": 3, "E": 4,
            "F": 5, "F#": 6, "GB": 6, "G": 7, "G#": 8, "AB": 8, "A": 9,
            "A#": 10, "BB": 10, "B": 11}


CHORD_INTERVALS = {"M": (0, 4, 7), "m": (0, 3, 7), "dim": (0, 3, 6),
                   "aug": (0, 4, 8), "7": (0, 4, 7, 10),
                   "M7": (0, 4, 7, 11), "m7": (0, 3, 7, 10),
                   "dim7": (0, 3, 6, 9), "m7b5": (0, 3, 6, 10)}


def chord_pitches(data):
    """Explicit voicing when the event has one, else the plain stack built
    from root + type. Both forms exist in the repo's renders: the run-15
    Bruckner takes emit root/type only (no voicing selector on that part),
    while engine-voiced parts emit `pitches`."""
    if "pitches" in data:
        return [int(x) for x in data["pitches"]]
    root = data.get("root") or {}
    pcv = PC_NAMES.get(str(root.get("pitch", "C")).upper().replace("-", ""), 0)
    base = 12 * int(root.get("octave", 4)) + pcv
    return [base + i for i in CHORD_INTERVALS.get(data.get("type", "M"),
                                                  (0, 4, 7))]


# ------------------------------------------------------------------ loading
def load_piece(path):
    """Piece JSON -> {'melody': [(beat,pitch,dur)], 'verticals': [...],
    'pedal': [...], 'chords': [(beat,[pitches],dur)], 'total': beats}."""
    p = json.loads(Path(path).read_text())
    melody, pedal, chords, others = [], [], [], []
    for part in p.get("parts", []):
        name = (part.get("name") or "").lower()
        evs = part.get("events") or []
        notes = [(float(e["beat"]), int(round(e["data"]["noteNumber"])),
                  float(e["data"]["duration"]))
                 for e in evs if e.get("type") == "note"]
        chs = [(float(e["beat"]), chord_pitches(e["data"]),
                float(e["data"]["duration"]))
               for e in evs if e.get("type") == "chord"]
        chords.extend(chs)
        if name == "pedal":
            pedal.extend(notes)
        elif name == "melody" or (not melody and notes and not chs):
            melody.extend(notes)
        else:
            others.extend(notes)
    melody.sort()
    ends = [b + d for b, _, d in melody + pedal + others] \
        + [b + d for b, _, d in chords]
    return {"melody": melody, "pedal": pedal, "chords": chords,
            "others": others, "key": p.get("key"),
            "total": max(ends) if ends else 0.0}


def sounding(pc, t):
    """Every pitch sounding at beat t, across all parts."""
    out = []
    for b, n, d in pc["melody"] + pc["pedal"] + pc["others"]:
        if b - 1e-6 <= t < b + d - 1e-6:
            out.append(n)
    for b, ps, d in pc["chords"]:
        if b - 1e-6 <= t < b + d - 1e-6:
            out.extend(ps)
    return out


def dissonance(pitches):
    """Mean pairwise interval-class dissonance of a vertical sonority, 0..1."""
    ps = sorted(set(pitches))
    if len(ps) < 2:
        return 0.0
    tot = cnt = 0
    for i in range(len(ps)):
        for j in range(i + 1, len(ps)):
            ic = abs(ps[j] - ps[i]) % 12
            tot += IC_WEIGHT[min(ic, 12 - ic)]
            cnt += 1
    return tot / cnt


def onsets(pc):
    """Every event onset in the piece, sorted (the sampling grid)."""
    return sorted(set([b for b, _, _ in pc["melody"] + pc["pedal"]
                       + pc["others"]] + [b for b, _, _ in pc["chords"]]))


# ----------------------------------------------------------------- features
def curves(pc, w=WINDOWS):
    """Windowed tension components over the passage span.

    density   event onsets per beat in the window (any part)
    register  mean melodic pitch (falls back to the top sounding voice)
    disson    mean vertical dissonance sampled at every onset in the window
    """
    total = pc["total"]
    if total <= 0:
        return None
    ons = onsets(pc)
    edges = [total * i / w for i in range(w + 1)]
    dens, reg, dis = [], [], []
    for i in range(w):
        lo, hi = edges[i], edges[i + 1]
        span = max(1e-6, hi - lo)
        win_on = [t for t in ons if lo - 1e-9 <= t < hi]
        dens.append(len(win_on) / span)
        mp = [n for b, n, _ in pc["melody"] if lo - 1e-9 <= b < hi]
        if not mp:
            tops = [max(sounding(pc, t)) for t in win_on if sounding(pc, t)]
            mp = tops
        reg.append(statistics.mean(mp) if mp else None)
        ds = [dissonance(sounding(pc, t)) for t in win_on]
        # No onset in this window: the sonority is held, so sample its middle.
        dis.append(statistics.mean(ds) if ds
                   else dissonance(sounding(pc, (lo + hi) / 2)))
    # Register holes (a window with nothing sounding) carry the previous value
    # rather than breaking the curve.
    last = next((x for x in reg if x is not None), 0.0)
    reg = [(last := (x if x is not None else last)) for x in reg]
    return {"density": dens, "register": reg, "disson": dis}


def _z(xs):
    """Z-normalize; an all-equal curve becomes all zeros (no motion)."""
    if len(xs) < 2:
        return [0.0] * len(xs)
    m = statistics.mean(xs)
    sd = statistics.pstdev(xs)
    return [0.0] * len(xs) if sd < 1e-9 else [(x - m) / sd for x in xs]


def _slope(xs):
    """Least-squares slope over index, normalized to roughly -1..1."""
    n = len(xs)
    if n < 2:
        return 0.0
    mx = (n - 1) / 2
    my = statistics.mean(xs)
    num = sum((i - mx) * (x - my) for i, x in enumerate(xs))
    den = sum((i - mx) ** 2 for i in range(n))
    return num / den if den else 0.0


def tension_curve(pc, w=WINDOWS):
    c = curves(pc, w)
    if not c:
        return None, None
    zt = [sum(v) / 3.0 for v in zip(_z(c["disson"]), _z(c["density"]),
                                    _z(c["register"]))]
    return zt, c


def recurrence(pc):
    """Fraction of the melodic interval sequence covered by its most common
    recurring n-gram, max over n=2..6 — the same measure score_generated calls
    `selfsim`, read here as a VIRTUE (a sequence is built from one cell)."""
    line = [n for _, n, _ in pc["melody"]] or \
           [ps[0] for _, ps, _ in pc["chords"]]
    ivs = [b - a for a, b in zip(line, line[1:])]
    best = 0.0
    for L in range(2, 7):
        if len(ivs) - L + 1 < 2:
            break
        g = Counter(tuple(ivs[i:i + L]) for i in range(len(ivs) - L + 1))
        top = g.most_common(1)[0][1]
        if top >= 2:
            best = max(best, min(1.0, top * L / len(ivs)))
    return best


def tonic_pc(key):
    """Pitch class of a piece key string like 'C Major' / 'Eb minor'."""
    if not key:
        return None
    return PC_NAMES.get(str(key).split()[0].upper().replace("-", ""))


def arrival_features(pc):
    """How the passage ENDS, as a passage rather than as a phrase."""
    ons = onsets(pc)
    if not ons:
        return None
    last = ons[-1]
    final_son = sounding(pc, last + 1e-3) or sounding(pc, last)
    prev = [t for t in ons if t < last]
    pre_dis = statistics.mean([dissonance(sounding(pc, t))
                               for t in prev[-3:]]) if prev else 0.0
    # duration of whatever starts on the last onset, vs the median gap
    durs = [d for b, _, d in pc["melody"] + pc["pedal"] + pc["others"]
            if abs(b - last) < 1e-6] + \
           [d for b, _, d in pc["chords"] if abs(b - last) < 1e-6]
    gaps = [b - a for a, b in zip(ons, ons[1:])]
    med_gap = statistics.median(gaps) if gaps else 1.0
    # Tonal arrival: the bottom of the final sonority should BE the tonic, and
    # the tonic should be in the chord at all. Added after the --degrade
    # harness showed retrograde slipping through everything else: a
    # rise-and-resolve tension arc is close to symmetric, so playing a passage
    # BACKWARDS leaves the curve shape (and often the grid alignment) intact —
    # what it destroys is that the thing you land on is the tonic.
    tpc = tonic_pc(pc.get("key"))
    if tpc is None or not final_son:
        tonic = 0.5                                  # unknown key: no opinion
    else:
        bass_ok = (min(final_son) % 12) == tpc
        has_tonic = any(p % 12 == tpc for p in final_son)
        tonic = 1.0 if bass_ok else (0.5 if has_tonic else 0.0)
    return {"final_disson": dissonance(final_son),
            "pre_disson": pre_dis,
            "on_downbeat": 1 if abs(last - round(last)) < 1e-3 else 0,
            "final_len_ratio": (max(durs) / med_gap) if durs and med_gap else 0,
            "tonic_arrival": tonic,
            "n_final_voices": len(set(final_son))}


# ------------------------------------------------------------------ scoring
def passage_score(pc, w=WINDOWS):
    """0..1 shape score with three named components. Weights are stated, not
    fitted — there is no passage corpus to fit to."""
    zt, c = tension_curve(pc, w)
    if zt is None:
        return None
    af = arrival_features(pc)
    slope = _slope(zt)
    peak = zt.index(max(zt)) / max(1, len(zt) - 1)
    # Motion of the COMBINED curve, in z units. Raw dissonance+density spread
    # was the first try and it is an artifact generator: a monophonic passage
    # has dissonance identically 0 (no verticals exist to be dissonant), so
    # every melody-only passage scored motion 0 no matter how much its
    # register moved. pstdev of zt asks the intended question — does the
    # tension curve go anywhere — in units that exist for every passage.
    motion = statistics.pstdev(zt)

    # -- tension: it should BUILD to a late peak, and actually move at all.
    #
    # NOT the slope across the whole passage. Every passage in the repo that
    # works — pedal_chords, bruckner2 — builds to a dissonance peak and then
    # RESOLVES into the cadence, so a whole-passage least-squares slope reads
    # NEGATIVE on exactly the passages that do the right thing (bruckner2
    # -0.02, pedal_chords -0.24). The build is measured up to the peak; what
    # happens after it is what `arrival` is for.
    ipk = zt.index(max(zt))
    climb = (zt[ipk] - zt[0]) / 2.0                       # in z units
    pre = zt[:ipk + 1]
    steady = (sum(1 for a, b in zip(pre, pre[1:]) if b >= a - 1e-9)
              / max(1, len(pre) - 1)) if len(pre) > 1 else 0.0
    rise_term = max(0.0, min(1.0, 0.6 * max(0.0, min(1.0, climb))
                             + 0.4 * steady))
    # A classical build peaks in the last third but not dead last (the last
    # window is the arrival, not the climax). Full credit 0.55..0.90.
    if 0.55 <= peak <= 0.90:
        peak_term = 1.0
    else:
        peak_term = max(0.0, 1.0 - min(abs(peak - 0.55), abs(peak - 0.90)) * 2)
    motion_term = min(1.0, motion / 0.5)     # a static passage builds nothing
    tension = 0.45 * rise_term + 0.30 * peak_term + 0.25 * motion_term

    # -- arrival: land consonant, release the tension into it, on a downbeat,
    #    and hold it.
    cons_term = max(0.0, 1.0 - af["final_disson"] * 2.5)
    release_term = max(0.0, min(1.0, 0.5 + (af["pre_disson"]
                                            - af["final_disson"]) * 3))
    beat_term = 1.0 if af["on_downbeat"] else 0.3
    hold_term = min(1.0, af["final_len_ratio"] / 2.0)
    arrival = (0.25 * cons_term + 0.20 * release_term
               + 0.15 * beat_term + 0.20 * hold_term
               + 0.20 * af["tonic_arrival"])

    # -- coherence: recurrence is a virtue inside a band. Below 0.15 the
    #    passage is a wander with no material; 1.0 exactly is one cell looped
    #    with nothing else, which is the only failure at the top.
    rec = recurrence(pc)
    if rec < 0.15:
        coh = rec / 0.15
    elif rec > 0.95:
        coh = max(0.0, 1.0 - (rec - 0.95) * 10)
    else:
        coh = 1.0
    coherence = coh

    total = 0.35 * tension + 0.40 * arrival + 0.25 * coherence
    return {"passage": round(total, 3),
            "tension": round(tension, 3), "arrival": round(arrival, 3),
            "coherence": round(coherence, 3),
            "slope": round(slope, 3), "climb": round(climb, 2),
            "steady": round(steady, 2), "peak_pos": round(peak, 2),
            "motion": round(motion, 3), "recurrence": round(rec, 3),
            "final_disson": round(af["final_disson"], 3),
            "pre_disson": round(af["pre_disson"], 3),
            "on_downbeat": af["on_downbeat"],
            "tonic_arrival": af["tonic_arrival"],
            "final_len_ratio": round(af["final_len_ratio"], 2),
            "n_notes": len(pc["melody"]), "n_chords": len(pc["chords"]),
            "beats": round(pc["total"], 2)}


# --------------------------------------------------------------- degradation
def degrade(pc, kind):
    """Mechanical degradations used to VERIFY the screen discriminates.

    Each one breaks exactly one stated criterion, so a screen that measures
    what it claims must drop on it. No taste judgement is involved: a passage
    played backwards is not a matter of opinion.
    """
    import copy
    import random
    d = copy.deepcopy(pc)
    rng = random.Random(7)
    if kind == "retrograde":                  # tension curve + arrival invert
        T = d["total"]
        for key in ("melody", "pedal", "others"):
            d[key] = sorted((round(T - b - dur, 6), n, dur)
                            for b, n, dur in d[key])
        d["chords"] = sorted((round(T - b - dur, 6), ps, dur)
                             for b, ps, dur in d["chords"])
    elif kind == "shuffle_pitch":             # coherence dies, rhythm intact
        ns = [n for _, n, _ in d["melody"]]
        rng.shuffle(ns)
        d["melody"] = [(b, ns[i], dur)
                       for i, (b, _, dur) in enumerate(d["melody"])]
        for i, (b, ps, dur) in enumerate(d["chords"]):
            sh = [p + rng.choice((-1, 1, 2, -2)) for p in ps]
            d["chords"][i] = (b, sh, dur)
    elif kind == "offbeat_end":               # arrival lands off the grid,
        def bump(seq):                        # and short
            if not seq:
                return seq
            *rest, lastev = sorted(seq)
            b = lastev[0] + 0.375
            return sorted(rest + [(b, lastev[1], min(lastev[2], 0.5))])
        d["melody"], d["chords"] = bump(d["melody"]), bump(d["chords"])
        d["pedal"] = bump(d["pedal"])
    elif kind == "flat":                       # nothing builds: one sonority
        if d["chords"]:
            first = d["chords"][0][1]
            d["chords"] = [(b, first, dur) for b, _ps, dur in d["chords"]]
        if d["melody"]:
            p0 = d["melody"][0][1]
            d["melody"] = [(b, p0, dur) for b, _n, dur in d["melody"]]
    else:
        raise SystemExit(f"unknown degradation {kind}")
    ends = [b + dur for b, _, dur in d["melody"] + d["pedal"] + d["others"]] \
        + [b + dur for b, _, dur in d["chords"]]
    d["total"] = max(ends) if ends else 0.0
    return d


DEGRADATIONS = ("retrograde", "shuffle_pitch", "offbeat_end", "flat")


def verify(paths):
    """Original vs degraded, passage-mode next to the phrase composite."""
    import score_generated as sg
    cs = sg.corpus_stats("mtd")
    print(f"{'piece / arm':<44}{'passage':>8}{'tens':>7}{'arriv':>7}"
          f"{'coher':>7}   {'phrase':>7}")
    deltas = {k: [] for k in DEGRADATIONS}
    ph_deltas = {k: [] for k in DEGRADATIONS}
    for path in paths:
        pc = load_piece(path)
        base = passage_score(pc)
        if base is None:
            continue
        pbase = _phrase_composite(pc, sg, cs)
        print(f"{Path(path).stem[:43]:<44}{base['passage']:>8.3f}"
              f"{base['tension']:>7.3f}{base['arrival']:>7.3f}"
              f"{base['coherence']:>7.3f}   "
              f"{('n/a' if pbase is None else f'{pbase:.3f}'):>7}")
        for k in DEGRADATIONS:
            dsc = passage_score(degrade(pc, k))
            pdg = _phrase_composite(degrade(pc, k), sg, cs)
            deltas[k].append(dsc["passage"] - base["passage"])
            if pbase is not None and pdg is not None:
                ph_deltas[k].append(pdg - pbase)
            print(f"   -{k:<40}{dsc['passage']:>8.3f}{dsc['tension']:>7.3f}"
                  f"{dsc['arrival']:>7.3f}{dsc['coherence']:>7.3f}   "
                  f"{('n/a' if pdg is None else f'{pdg:.3f}'):>7}")
    print("\nmean delta vs original (negative = the screen caught it):")
    for k in DEGRADATIONS:
        if deltas[k]:
            ph = (f"{statistics.mean(ph_deltas[k]):+.3f}" if ph_deltas[k]
                  else "n/a")
            print(f"  {k:<16} passage {statistics.mean(deltas[k]):+.3f}"
                  f"   phrase {ph}")


def _phrase_composite(pc, sg, cs):
    """The phrase scorer's composite on the same material, for comparison."""
    mel = pc["melody"] or [(b, ps[0], d) for b, ps, d in pc["chords"]]
    if len(mel) < 4:
        return None
    try:
        return sg.score(mel, cs)["composite"]
    except (KeyError, IndexError, ZeroDivisionError, statistics.StatisticsError):
        return None


def main():
    args = [a for a in sys.argv[1:]]
    if not args:
        print(__doc__)
        sys.exit(1)
    if args[0] == "--degrade":
        verify(args[1:])
        return
    csv_out = None
    if "--csv" in args:
        k = args.index("--csv")
        csv_out = args[k + 1]
        args = args[:k] + args[k + 2:]
    if csv_out:
        import csv as _csv
        import score_generated as sg
        cs = sg.corpus_stats("mtd")
        rows = []
        for p in args:
            pc = load_piece(p)
            r = passage_score(pc)
            if r is None:
                continue
            r["file"] = Path(p).stem
            r["dir"] = Path(p).parent.name
            r["phrase_composite"] = _phrase_composite(pc, sg, cs)
            rows.append(r)
        with open(csv_out, "w", newline="") as f:
            w = _csv.DictWriter(f, fieldnames=["dir", "file"]
                                + [k for k in rows[0] if k not in
                                   ("dir", "file")])
            w.writeheader()
            w.writerows(rows)
        print(f"{len(rows)} pieces -> {csv_out}")
        return
    cols = ["passage", "tension", "arrival", "coherence", "climb", "steady",
            "peak_pos", "motion", "recurrence", "final_disson", "tonic_arrival",
            "final_len_ratio", "n_notes", "n_chords", "beats"]
    print(f"{'file':<44}" + "".join(f"{c[:9]:>11}" for c in cols))
    for p in args:
        pc = load_piece(p)
        r = passage_score(pc)
        if r is None:
            print(f"{Path(p).stem[:43]:<44} (no events)")
            continue
        print(f"{Path(p).stem[:43]:<44}"
              + "".join(f"{str(r[c]):>11}" for c in cols))


if __name__ == "__main__":
    main()
