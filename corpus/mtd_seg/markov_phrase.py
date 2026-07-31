"""Combine 2 Markov figures into a phrase template + render to WAV. Phase 2 v1.

Samples 2 independent figures from the Phase-1 Markov model, randomly rolls a
combination (pattern x transform x placement), materializes the concrete figures,
emits an engine template JSON (verified motif/connector format), and renders to WAV
via mforce_cli --compose. No C++ changes; the chooser lives here.
"""
import argparse, json, pathlib, random, subprocess, sys

REPO  = pathlib.Path(__file__).resolve().parent.parent.parent
CLI   = REPO / "build/tools/mforce_cli/Release/mforce_cli.exe"
PATCH = REPO / "patches/Additive1.json"
MAJOR = [0, 2, 4, 5, 7, 9, 11]


# --------------------------------------------------------------------------- #
# Task 1: figure helpers
# --------------------------------------------------------------------------- #
def net_step(fig):
    return sum(u["step"] for u in fig["units"])


def invert(fig):
    return {"units": [{"duration": u["duration"], "step": -u["step"]}
                      for u in fig["units"]]}


def retrograde(fig):
    units = fig["units"]; n = len(units)
    durs = [u["duration"] for u in units]
    deg, c = [], 0
    for u in units:
        c += u["step"]; deg.append(c)            # cumulative degrees, deg[0]=0
    rdurs = durs[::-1]
    rdeg = deg[::-1]
    base = rdeg[0]
    rdeg = [d - base for d in rdeg]              # re-anchor so rdeg[0]=0
    rsteps = [0] + [rdeg[i] - rdeg[i-1] for i in range(1, n)]
    return {"units": [{"duration": rdurs[i], "step": rsteps[i]} for i in range(n)]}


# --------------------------------------------------------------------------- #
# Task 2: combination chooser
# --------------------------------------------------------------------------- #
PATTERNS = ["AB", "AAB", "AAAB", "ABAB", "AABB", "AAA'B", "ABA'B"]

# Deliberate norm-breakers (WORKFLOW outlier directive): over-long repeats and
# lopsided structures. Used only with --pattern-set outlier (guard off by default),
# to hear where "too much repetition" / runaway range crosses into non-musical.
OUTLIER_PATTERNS = ["AAAAAB", "AAAAAAB", "ABABABAB", "AAAAABB", "AAA'A'B"]


def _tokens(pattern):
    # "AAA'B" -> ["A","A","P","B"]  (apostrophe turns the PRECEDING A into prime P)
    out = []
    for ch in pattern:
        if ch == "'":
            out[-1] = "P"
        else:
            out.append(ch)
    return out


def build_combination(figA, figB, pattern, transform, placement):
    refs = _tokens(pattern)
    motifs = {"A": figA, "B": figB}
    if "P" in refs:
        motifs["P"] = invert(figA) if transform == "invert" else retrograde(figA)
    connectors = [None]
    for i in range(1, len(refs)):
        prev = motifs[refs[i-1]]
        if refs[i] == refs[i-1] and refs[i] == "A":      # repeated-A seam: apply placement
            if placement == "same":
                connectors.append(-net_step(prev))
            elif placement == "sequence":
                connectors.append(-net_step(prev) + 2)
            else:                                        # climb
                connectors.append(0)
        else:
            connectors.append(0)                         # into a different motif: continue
    return motifs, refs, connectors


# --------------------------------------------------------------------------- #
# Phase 2 v2: repeat contours + vary_tail (Matt's spec 2026-07-29)
# --------------------------------------------------------------------------- #
# Named repeat-contour patterns: scale-step offset of each successive
# A-family occurrence relative to the FIRST A (anchor 0). Applied via the
# leadStep connector: lead = offset - cursor gives "first-A pitch + offset".
# "literal" ([0,0,0]) stays available — identical A within a varied pattern
# is explicitly allowed. Occurrences past the list clamp to the last entry.
CONTOURS = {
    "literal":      [0, 0, 0, 0],
    "step_up":      [0, 1, 2, 3],
    "step_down":    [0, -1, -2, -3],
    "up_then_down": [0, 1, -1, 0],
    "leap_up":      [0, 3, 3, 3],
    "down_step":    [0, -2, -2, -2],
    "zigzag":       [0, 2, -1, 1],
}


def _tokens_v2(pattern, transform):
    """Like _tokens, but with vary_tail each prime is an INDEPENDENT variant:
    "AA'A''B" -> ["A","V1","V2","B"]. Other transforms keep the single P."""
    out = []
    for ch in pattern:
        if ch == "'":
            prev = out[-1]
            if transform == "vary_tail":
                out[-1] = "V1" if prev == "A" else "V" + str(int(prev[1:]) + 1)
            else:
                out[-1] = "P"
        else:
            out.append(ch)
    return out


def vary_tail(fig, model, rng, max_tries=20):
    """A' that KEEPS the head (first ceil(n/2) units, durations + steps) and
    resamples the tail from the Markov chain, conditioned on the head.

    Conditioning reconstructs the exact token history gen_model would have:
    BOS token (-999 step sentinel, note-0 duration) then the head's
    (step, duration) tokens — so order-2 backoff sees real context.
    Retries until the tail STEP sequence differs from the original (up to
    max_tries); returns (variant, n_diff_tail_steps)."""
    units = fig["units"]
    n = len(units)
    h = -(-n // 2)                                   # ceil(n/2)
    bos_step = model.bos_tokens[0][0]                # -999 sentinel (uniform)
    history = [(bos_step, float(units[0]["duration"]))]
    history += [(int(units[i]["step"]), float(units[i]["duration"]))
                for i in range(1, h)]
    old_tail = [int(units[i]["step"]) for i in range(h, n)]
    tail = []
    for _ in range(max_tries):
        hist = list(history)
        tail = []
        for _ in range(n - h):
            t = model.next_token(hist, rng)
            tail.append(t)
            hist.append(t)
        if [t[0] for t in tail] != old_tail:
            break
    n_diff = sum(1 for t, o in zip(tail, old_tail) if t[0] != o)
    variant = {"units": [dict(units[i]) for i in range(h)] +
                        [{"duration": float(t[1]), "step": int(t[0])}
                         for t in tail]}
    return variant, n_diff


def build_contour_combination(figA, figB, pattern, transform, contour,
                              model=None, rng=None):
    """Generalized build_combination: every A-family occurrence (A, P, V*)
    is anchored at first-A-pitch + CONTOURS[contour][j] scale steps (j =
    occurrence index); B continues from the cursor (lead 0).

    Returns (motifs, refs, connectors, anchors, tail_diffs) where anchors is
    the per-figure anchor degree (for verification) and tail_diffs maps
    variant name -> count of tail steps that differ from A."""
    refs = _tokens_v2(pattern, transform)
    motifs = {"A": figA, "B": figB}
    tail_diffs = {}
    for r in refs:
        if r == "P" and r not in motifs:
            motifs["P"] = invert(figA) if transform == "invert" else retrograde(figA)
        elif r.startswith("V") and r not in motifs:
            motifs[r], tail_diffs[r] = vary_tail(figA, model, rng)
    offsets = CONTOURS[contour]
    connectors, anchors = [None], [0]
    cursor = net_step(motifs[refs[0]])               # first figure anchors at 0
    a_seen = 1
    for i in range(1, len(refs)):
        r = refs[i]
        if r != "B":                                 # A-family: place on contour
            off = offsets[min(a_seen, len(offsets) - 1)]
            a_seen += 1
            lead = off - cursor
        else:                                        # B: continue from cursor
            lead = 0
        connectors.append(lead)
        anchor = cursor + lead
        anchors.append(anchor)
        cursor = anchor + net_step(motifs[r])
    return motifs, refs, connectors, anchors, tail_diffs


# --------------------------------------------------------------------------- #
# Phase 2 v3: contrast-aware fig B (backlog #2, Matt's Phase-2 verdict)
# --------------------------------------------------------------------------- #
# fig B was previously sampled independent of fig A (main(): "contrast = TODO").
# Real antecedent/consequent writing relates B to A: B balances A's contour
# (a rising A answered by a falling B), shares A's rhythmic character (sibling
# rhythm), and avoids collapsing to a monotone. We keep the joint Markov model
# faithful — B is still a genuine chain draw — and add SELECTION pressure: draw
# N candidate B's (same note-count as A for rhythmic kinship), keep the best on
# a contrast objective. This isolates the B-sampling change for a clean A/B vs
# the independent-B baseline (same figA, same pattern/transform/placement).
from collections import Counter as _Counter


def _pulse_profile(fig):
    return [round(u["duration"], 3) for u in fig["units"]]


def _zero_rate_fig(fig):
    """Repeated-note fraction within a figure (skip the anchor step0=0)."""
    steps = [u["step"] for u in fig["units"][1:]]
    return (sum(1 for s in steps if s == 0) / len(steps)) if steps else 0.0


def _rhythm_kinship(figA, cand):
    """0..1 duration-profile similarity (multiset overlap), full credit only at
    equal length — a rhythmic sibling shares A's note-count and durations."""
    pa, pc = _pulse_profile(figA), _pulse_profile(cand)
    base = 1.0 if len(pa) == len(pc) else 0.4
    inter = sum((_Counter(pa) & _Counter(pc)).values())
    return base * (inter / max(len(pa), len(pc), 1))


def _closure(figA, cand, span_steps=6):
    """Reward a consequent that brings the line back home: small combined net
    scale-step. This subsumes contour-complement (a rising A, net +3, is best
    answered by a falling B, net -3, summing to ~0) AND controls the two-figure
    range — the first contrast draft complemented direction without magnitude
    control and let climb-placement runaways reach 31 semitones."""
    return 1.0 - min(1.0, abs(net_step(figA) + net_step(cand)) / span_steps)


def contrast_fit(figA, cand):
    """Weighted contrast objective: antecedent/consequent closure (0.45),
    rhythmic kinship (0.35), non-monotone B (0.20 with a hard veto). The veto
    zeroes the monotony term for a B whose repeated-note rate >= 0.5 — closure
    alone will otherwise reward a near-static B (netA+netB ~ 0 achieved by a B
    that barely moves), which the first pass produced (p02 range collapsed to 1,
    p13 zero_rate 0.6)."""
    zero = _zero_rate_fig(cand)
    mono = 0.0 if zero >= 0.5 else (1.0 - zero)
    return (0.45 * _closure(figA, cand)
            + 0.35 * _rhythm_kinship(figA, cand)
            + 0.20 * mono)


def sample_contrast_figB(figA, model, rng, n_cand=32):
    """Best-of-N chain draw for B, biased to relate to A. B inherits A's
    note-count (rhythmic-sibling bias); selection picks complementary contour +
    kindred rhythm + motion. Returns (figB, fit)."""
    k = len(figA["units"])
    best, best_fit = None, -1.0
    for _ in range(n_cand):
        cand = _sample_figure(model, rng, kmin=k, kmax=k)
        f = contrast_fit(figA, cand)
        if f > best_fit:
            best, best_fit = cand, f
    return best, best_fit


# --------------------------------------------------------------------------- #
# Task 3: template emitter
# --------------------------------------------------------------------------- #
def make_template(motifs, refs, connectors, *, key="C", scale="Major",
                  bpm=84.0, seed=1, start=("C", 4)):
    total = sum(u["duration"] for r in refs for u in motifs[r]["units"])
    figures = [{"source": "reference", "motifName": r} for r in refs]
    conns = [None if c is None else int(c) for c in connectors]
    return {
        "keyName": key, "scaleName": scale, "bpm": bpm, "masterSeed": seed,
        "motifs": [{"name": name, "figure": fig, "userProvided": True}
                   for name, fig in motifs.items()],
        "sections": [{"name": "Main", "beats": total}],
        "parts": [{
            "name": "melody", "role": "melody",
            "passages": {"Main": {
                "startingPitch": {"octave": start[1], "pitch": start[0]},
                "phrases": [{
                    "name": "P1",
                    "startingPitch": {"octave": start[1], "pitch": start[0]},
                    "figures": figures,
                    "connectors": conns,
                }],
            }},
        }],
    }


# --------------------------------------------------------------------------- #
# Task 4: render + pitch-verification harness
# --------------------------------------------------------------------------- #
def degree_to_semitone(d):
    octs, deg = divmod(d, 7)
    return 12 * octs + MAJOR[deg]


def predict_relative_semitones(motifs, refs, connectors):
    """Replay the engine's cursor math in scale-degrees; return semitones rel to first note."""
    degs, cursor = [], 0          # cursor = degree of previous note
    first = True
    for i, name in enumerate(refs):
        fig = motifs[name]
        lead = 0 if connectors[i] is None else connectors[i]
        anchor = 0 if first else cursor + lead   # first figure anchors at 0
        first = False
        d = 0
        for u in fig["units"]:
            d += u["step"]                       # degree within figure (unit0 step==0)
            degs.append(anchor + d)
        cursor = anchor + d                       # land on last note's degree
    semis = [degree_to_semitone(x) for x in degs]
    return [s - semis[0] for s in semis]


def predicted_range(motifs, refs, connectors):
    """Semitone span of the phrase's predicted contour (max-min), engine-free."""
    semis = predict_relative_semitones(motifs, refs, connectors)
    return max(semis) - min(semis)


def render_template(template, out_prefix):
    tpath = REPO / (out_prefix + "_tmpl.json")    # the input template we author
    tpath.parent.mkdir(parents=True, exist_ok=True)
    tpath.write_text(json.dumps(template), encoding="utf-8")
    subprocess.run([str(CLI), "--compose", str(PATCH), str(REPO / out_prefix), "1",
                    "--template", str(tpath)], check=True, capture_output=True)
    ev = json.loads((REPO / (out_prefix + "_1.json")).read_text(encoding="utf-8"))
    return [int(e["data"]["noteNumber"]) for e in ev["parts"][0]["events"]]


# --------------------------------------------------------------------------- #
# Task 5: CLI driver
# --------------------------------------------------------------------------- #
def _sample_figure(model, rng, kmin=3, kmax=5):
    from markov_generate import gen_model
    k = rng.randint(kmin, kmax)
    step, pulse = gen_model(model, k, rng)
    return {"units": [{"duration": float(pulse[i]), "step": int(step[i])}
                      for i in range(len(step))]}


def main():
    from markov_model import MarkovModel
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=8)
    ap.add_argument("--seed", type=int, default=1234)
    ap.add_argument("--bpm", type=float, default=84.0)
    ap.add_argument("--range-cap", type=int, default=19,
                    help="semitone span cap (corpus range_p90); reject-sample past it. "
                         "0 disables the guard (raw combinations, incl. runaways).")
    ap.add_argument("--max-tries", type=int, default=24,
                    help="rejection-sampling attempts per phrase before keeping the "
                         "narrowest-range candidate.")
    ap.add_argument("--pattern-set", choices=["normal", "outlier"], default="normal",
                    help="'outlier' uses over-long/lopsided patterns and defaults the "
                         "range guard off (norm-breaking audition set).")
    args = ap.parse_args()

    patterns = OUTLIER_PATTERNS if args.pattern_set == "outlier" else PATTERNS
    if args.pattern_set == "outlier" and "--range-cap" not in sys.argv:
        args.range_cap = 0                            # let outliers run wild by default
    subdir = "outliers" if args.pattern_set == "outlier" else "."

    rng = random.Random(args.seed)
    model = MarkovModel.load()
    outdir = (REPO / "renders/markov_phrases" / subdir).resolve()
    outdir.mkdir(parents=True, exist_ok=True)

    def roll():
        figA = _sample_figure(model, rng)
        figB = _sample_figure(model, rng)            # independent (contrast = TODO)
        pattern   = rng.choice(patterns)
        transform = rng.choice(["invert", "retrograde"])
        placement = rng.choice(["same", "climb", "sequence"])
        motifs, refs, conns = build_combination(figA, figB, pattern, transform, placement)
        return motifs, refs, conns, pattern, transform, placement

    for i in range(args.n):
        # Accept the FIRST candidate under the cap. The previous
        # keep-the-narrowest fallback was a bias machine: minimizing span
        # concentrated selection pressure on the final figure, pushing its
        # zero-step rate from the raw 0.16 to 0.38 (4x corpus) — Matt heard
        # it as "second figure is all one note". If a candidate is over cap,
        # retry it once with placement="same" (kills the climb-induced span)
        # before rolling fresh; on exhaustion keep the LAST candidate
        # (unbiased draw), flagged OVER-CAP.
        cand, span, tries = None, 0, 0
        while True:
            tries += 1
            cand = roll()
            span = predicted_range(cand[0], cand[1], cand[2])
            if args.range_cap > 0 and span > args.range_cap and cand[5] != "same":
                motifs2, refs2, conns2 = build_combination(
                    cand[0]["A"], cand[0]["B"], cand[3], cand[4], "same")
                span2 = predicted_range(motifs2, refs2, conns2)
                if span2 <= args.range_cap:
                    cand = (motifs2, refs2, conns2, cand[3], cand[4], "same")
                    span = span2
            if args.range_cap <= 0 or span <= args.range_cap or tries >= args.max_tries:
                break
        motifs, refs, conns, pattern, transform, placement = cand
        seed_i = args.seed * 1000 + i
        t = make_template(motifs, refs, conns, bpm=args.bpm, seed=seed_i)
        sub = f"{subdir}/" if subdir != "." else ""
        prefix = f"renders/markov_phrases/{sub}p{i:02d}_{pattern.replace(chr(39),'x')}"
        notes = render_template(t, prefix)
        flag = "" if span <= args.range_cap or args.range_cap <= 0 else "  OVER-CAP"
        print(f"p{i:02d}: {pattern:6} t={transform:9} place={placement:8} "
              f"{len(refs)} figs, {len(notes)} notes, range={span:2d} "
              f"(tries={tries}){flag} -> {prefix}_1.wav")


# --------------------------------------------------------------------------- #
# Phase 2 v2 batch driver (--v2): 3 families x 8 phrases -> renders/markov_phrases2
# --------------------------------------------------------------------------- #
V2_SPECS = [
    # family 1: transposed LITERAL repeats (A repeats verbatim, start pitch moves)
    ("transposed", "AAB",     "step_up",      None),
    ("transposed", "AAB",     "step_down",    None),
    ("transposed", "AAAB",    "step_up",      None),
    ("transposed", "AAAB",    "up_then_down", None),
    ("transposed", "AAB",     "leap_up",      None),
    ("transposed", "AAB",     "down_step",    None),
    ("transposed", "AAAB",    "zigzag",       None),
    ("transposed", "ABAB",    "step_up",      None),
    # family 2: SAME-pitch modified repeats (vary_tail, literal contour) —
    # Matt's G-F#-G-... shape: head kept, tail resampled per repeat
    ("varytail",   "AA'B",    "literal",      "vary_tail"),
    ("varytail",   "AA'B",    "literal",      "vary_tail"),
    ("varytail",   "AA'B",    "literal",      "vary_tail"),
    ("varytail",   "AA'A''B", "literal",      "vary_tail"),
    ("varytail",   "AA'A''B", "literal",      "vary_tail"),
    ("varytail",   "AA'A''B", "literal",      "vary_tail"),
    ("varytail",   "ABA'B",   "literal",      "vary_tail"),
    ("varytail",   "ABA'B",   "literal",      "vary_tail"),
    # family 3: COMBINED (moved start pitch AND modified tail)
    ("combined",   "AA'B",    "step_up",      "vary_tail"),
    ("combined",   "AA'B",    "step_down",    "vary_tail"),
    ("combined",   "AA'A''B", "step_up",      "vary_tail"),
    ("combined",   "AA'A''B", "up_then_down", "vary_tail"),
    ("combined",   "ABA'B",   "step_up",      "vary_tail"),
    ("combined",   "AA'B",    "leap_up",      "vary_tail"),
    ("combined",   "AA'A''B", "zigzag",       "vary_tail"),
    ("combined",   "AA'B",    "down_step",    "vary_tail"),
]


def main_v2():
    from markov_model import MarkovModel
    import score_generated as sg
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=1234)
    ap.add_argument("--bpm", type=float, default=84.0)
    ap.add_argument("--range-cap", type=int, default=19)
    ap.add_argument("--max-tries", type=int, default=24)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    model = MarkovModel.load()
    (REPO / "renders/markov_phrases2").mkdir(parents=True, exist_ok=True)

    ver_rows, meta = [], []
    for i, (family, pattern, contour, transform) in enumerate(V2_SPECS):
        # vary_tail needs a tail of >=2 units to be worth resampling -> kmin=4
        kmin = 4 if transform == "vary_tail" else 3
        # Accept-first-under-cap; over-cap candidates are rerolled (fresh
        # figures — spec is fixed per slot, so no contour fallback that would
        # make the filename lie); on exhaustion keep the LAST draw, flagged.
        tries = 0
        while True:
            tries += 1
            figA = _sample_figure(model, rng, kmin=kmin)
            figB = _sample_figure(model, rng, kmin=kmin)
            motifs, refs, conns, anchors, tdiffs = build_contour_combination(
                figA, figB, pattern, transform, contour, model, rng)
            span = predicted_range(motifs, refs, conns)
            if args.range_cap <= 0 or span <= args.range_cap \
                    or tries >= args.max_tries:
                break
        seed_i = args.seed * 1000 + i
        t = make_template(motifs, refs, conns, bpm=args.bpm, seed=seed_i)
        name = (f"p{i:02d}_{pattern.replace(chr(39), 'x')}_"
                f"{contour.replace('_', '')}_"
                f"{'varytail' if transform == 'vary_tail' else 'lit'}")
        prefix = f"renders/markov_phrases2/{name}"
        notes = render_template(t, prefix)

        # verification: engine-free prediction vs rendered piece JSON
        pred = predict_relative_semitones(motifs, refs, conns)
        rel = [n - notes[0] for n in notes]
        seq_ok = (pred == rel)
        starts, s = [], 0
        for r in refs:
            starts.append(s)
            s += len(motifs[r]["units"])
        occ = 0
        for j, r in enumerate(refs):
            if r == "B":
                continue
            offs = CONTOURS[contour]
            intended = offs[min(occ, len(offs) - 1)]
            exp_semi = degree_to_semitone(anchors[j])
            got_semi = notes[starts[j]] - notes[0]
            ver_rows.append([name, r, occ, intended, anchors[j],
                             exp_semi, got_semi, exp_semi == got_semi])
            occ += 1
        flag = "" if (args.range_cap <= 0 or span <= args.range_cap) \
            else "  OVER-CAP"
        diffs = ("  tail_diffs=" + ",".join(f"{k}:{v}" for k, v
                 in sorted(tdiffs.items()))) if tdiffs else ""
        print(f"{name}: {len(notes)} notes range={span:2d} tries={tries} "
              f"seq_verify={'OK' if seq_ok else 'FAIL'}{diffs}{flag}")
        meta.append((name, family, pattern, contour,
                     "vary_tail" if transform else "none", span, seq_ok, prefix))

    print("\n=== repeat-onset verification (A-family figures) ===")
    print(f"{'phrase':34} {'ref':3} {'occ':3} {'offset':6} {'anchor':6} "
          f"{'exp_st':6} {'got_st':6} ok")
    for r in ver_rows:
        print(f"{r[0]:34} {r[1]:3} {r[2]:3d} {r[3]:+6d} {r[4]:+6d} "
              f"{r[5]:+6d} {r[6]:+6d} {'OK' if r[7] else 'FAIL'}")
    n_bad = sum(1 for r in ver_rows if not r[7])
    print(f"onset checks: {len(ver_rows) - n_bad}/{len(ver_rows)} OK")

    # scoring -> scores.csv
    import csv as _csv
    cs = sg.corpus_stats()
    cols = ["file", "family", "pattern", "contour", "transform", "n_notes",
            "int_jsd", "ctr_jsd", "rep_LxCount", "big_leap", "range",
            "zero_rate", "max_run_frac", "selfsim", "composite"]
    rows, by_family = [], {}
    for name, family, pattern, contour, transform, span, seq_ok, prefix in meta:
        r = sg.score(sg.load_melody(REPO / (prefix + "_1.json")), cs)
        r.update(file=name + "_1.json", family=family, pattern=pattern,
                 contour=contour, transform=transform)
        rows.append(r)
        by_family.setdefault(family, []).append(r["composite"])
    csv_path = REPO / "renders/markov_phrases2/scores.csv"
    with open(csv_path, "w", newline="") as f:
        w = _csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        w.writerows(rows)
    print(f"\n=== composite by family (n={len(rows)}) -> {csv_path} ===")
    for fam, vals in by_family.items():
        print(f"{fam:11} mean={sum(vals)/len(vals):.3f} "
              f"min={min(vals):.3f} max={max(vals):.3f}")
    low = [(r['file'], r['composite']) for r in rows if r['composite'] < 0.6]
    print("flagged <0.6: " + (", ".join(f"{f} ({c})" for f, c in low)
                              if low else "none"))


# --------------------------------------------------------------------------- #
# Phase 2 v3 driver (--contrast): paired A/B, independent-B vs contrast-B.
# Same figA + pattern/transform/placement per pair; only B-sampling differs.
# -> renders/markov_contrast/{independent,contrast}/ + scores.csv (arm column)
# --------------------------------------------------------------------------- #
# Patterns that actually USE B (contrast only matters where B appears).
CONTRAST_PATTERNS = ["AB", "AAB", "ABAB", "AABB", "ABA'B", "AAB", "ABAB"]


def main_contrast():
    from markov_model import MarkovModel
    import score_generated as sg
    import csv as _csv
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=len(CONTRAST_PATTERNS))
    ap.add_argument("--seed", type=int, default=1234)
    ap.add_argument("--bpm", type=float, default=84.0)
    ap.add_argument("--n-cand", type=int, default=32,
                    help="candidate B draws per contrast selection")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    model = MarkovModel.load()
    cs = sg.corpus_stats()
    for arm in ("independent", "contrast"):
        (REPO / "renders/markov_contrast" / arm).mkdir(parents=True, exist_ok=True)

    cols = ["pair", "arm", "pattern", "fit", "n_notes", "int_jsd", "ctr_jsd",
            "rep_LxCount", "big_leap", "range", "zero_rate", "max_run_frac",
            "selfsim", "composite"]
    rows = []
    print(f"{'pair':5} {'pattern':7} {'arm':11} {'fit':5} {'range':5} "
          f"{'zero':5} {'comp':5}")
    for i in range(args.n):
        pattern   = CONTRAST_PATTERNS[i % len(CONTRAST_PATTERNS)]
        # Roll the shared parts ONCE so the arms differ only in B.
        figA      = _sample_figure(model, rng)
        transform = rng.choice(["invert", "retrograde"])
        placement = rng.choice(["same", "climb", "sequence"])
        figB_indep = _sample_figure(model, rng)
        figB_ctr, fit = sample_contrast_figB(figA, model, rng, n_cand=args.n_cand)

        for arm, figB, fitv in (("independent", figB_indep, float("nan")),
                                ("contrast",    figB_ctr,   fit)):
            motifs, refs, conns = build_combination(
                figA, figB, pattern, transform, placement)
            seed_i = args.seed * 1000 + i
            t = make_template(motifs, refs, conns, bpm=args.bpm, seed=seed_i)
            prefix = (f"renders/markov_contrast/{arm}/"
                      f"p{i:02d}_{pattern.replace(chr(39), 'x')}")
            notes = render_template(t, prefix)
            span = predicted_range(motifs, refs, conns)
            r = sg.score(sg.load_melody(REPO / (prefix + "_1.json")), cs)
            r.update(pair=f"p{i:02d}", arm=arm, pattern=pattern,
                     fit=("" if fitv != fitv else round(fitv, 3)))
            rows.append(r)
            print(f"p{i:02d}   {pattern:7} {arm:11} "
                  f"{('' if fitv != fitv else f'{fitv:.2f}'):>5} {span:5d} "
                  f"{r['zero_rate']:5} {r['composite']:5}")

    csv_path = REPO / "renders/markov_contrast/scores.csv"
    with open(csv_path, "w", newline="") as f:
        w = _csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        w.writerows(rows)

    def _agg(arm, key):
        vals = [r[key] for r in rows if r["arm"] == arm]
        return sum(vals) / len(vals) if vals else 0.0
    print(f"\n=== arm comparison (n={args.n} pairs) -> {csv_path} ===")
    for key in ("composite", "zero_rate", "range", "big_leap"):
        ind, ctr = _agg("independent", key), _agg("contrast", key)
        print(f"{key:12} independent={ind:.3f}  contrast={ctr:.3f}  "
              f"delta={ctr - ind:+.3f}")


# --------------------------------------------------------------------------- #
# Phase 2 v4 (--v3 flag): Matt's 2026-07-30 verdicts -> renders/markov_phrases3
#   1. wider length variation upward (kmax 5->9, figure beats band, +2 longer
#      patterns) while keeping the short end
#   2. duration-aware sizing: short mean pulse => more notes (target-beats band
#      per figure drives k)
#   3. final-note treatment: last note >= phrase median pulse with p=0.85
#      (extend, never delete); confirmed NO such rule existed in this path
#   4. beat-grid joins: figure starts quantized to integer beats by padding
#      (EXTENDING) the previous figure's final note; elision (~20%) keeps the
#      raw concatenation. Confirmed joins previously landed wherever durations
#      summed. Done in the Python layer via per-occurrence materialized
#      figures (engine connector stays bare leadStep).
#   5. figure_transforms.py wired in: primes draw from the full transform
#      library, labeled in filenames.
# --------------------------------------------------------------------------- #
import math as _math
import statistics as _stats

V3_TRANSFORMS = ["invert", "retrograde", "expand_intervals",
                 "compress_intervals", "rotate", "ornament", "augment",
                 "vary_tail"]
# widen upward: base patterns + two longer ones (short end preserved)
V3_PATTERNS = PATTERNS + ["AA'BAB", "AABA'B"]


def total_beats(fig):
    return sum(u["duration"] for u in fig["units"])


def _copy_fig(fig):
    return {"units": [dict(u) for u in fig["units"]]}


def _sample_figure_sized(model, rng, kmin=3, kmax=9,
                         beats_lo=1.5, beats_hi=4.0, tries=6):
    """Duration-aware figure sampling (verdicts 1+2). Draw with a wide k range;
    if total beats falls outside [beats_lo, beats_hi], resample with k chosen
    from the figure's own mean pulse to hit a target inside the band — so a
    short-pulse draw gets MORE notes instead of yielding an ultra-short figure.
    Keeps the closest candidate if the band is never hit."""
    best, best_err = None, None
    fig = _sample_figure(model, rng, kmin, kmax)
    for _ in range(tries):
        tb = total_beats(fig)
        if beats_lo <= tb <= beats_hi:
            return fig
        err = (beats_lo - tb) if tb < beats_lo else (tb - beats_hi)
        if best is None or err < best_err:
            best, best_err = fig, err
        mean_pulse = tb / max(1, len(fig["units"]))
        target = rng.uniform(beats_lo, beats_hi)
        k = max(3, min(12, int(round(target / max(mean_pulse, 1e-6)))))
        fig = _sample_figure(model, rng, kmin=k, kmax=k)
    tb = total_beats(fig)
    if beats_lo <= tb <= beats_hi:
        return fig
    err = (beats_lo - tb) if tb < beats_lo else (tb - beats_hi)
    return fig if err < best_err else best


def _apply_v3_transform(name, fig, model, rng):
    """Dispatch into figure_transforms.py (verdict 5)."""
    import figure_transforms as ftl
    if name == "vary_tail":
        variant, _ = vary_tail(fig, model, rng)
        return variant
    if name == "ornament":
        return ftl.ornament(fig, rng)
    if name in ("expand_intervals", "compress_intervals"):
        return getattr(ftl, name)(fig, 2.0)
    if name == "rotate":
        return ftl.rotate(fig, 1)
    if name == "augment":
        return ftl.augment(fig, 2.0)
    return getattr(ftl, name)(fig)     # invert / retrograde


def build_phrase_v3(figA, figB, pattern, transform, contour, model, rng,
                    elide_prob=0.2, final_ext_prob=0.85, grid=1.0):
    """Materialize per-occurrence figures with contour leadSteps, then apply
    join quantization (verdict 4) and final-note treatment (verdict 3).

    Returns (motif_list, refs, connectors, info): motif_list is an ordered
    [(name, fig)] with unique per-occurrence names (padding differs per
    occurrence, so motifs can't be shared by reference); info carries join
    kinds + final-extension flag for reporting."""
    refs = _tokens_v2(pattern, transform)
    motifs = {"A": figA, "B": figB}
    for r in refs:
        if r not in motifs:
            if r.startswith("V"):
                motifs[r], _ = vary_tail(figA, model, rng)
            else:                                    # "P"
                motifs[r] = _apply_v3_transform(transform, figA, model, rng)

    # contour leadSteps (same math as build_contour_combination)
    offsets = CONTOURS[contour]
    occ_figs = [_copy_fig(motifs[refs[0]])]
    connectors = [None]
    cursor = net_step(motifs[refs[0]])
    a_seen = 1
    for i in range(1, len(refs)):
        r = refs[i]
        if r != "B":
            off = offsets[min(a_seen, len(offsets) - 1)]
            a_seen += 1
            lead = off - cursor
        else:
            lead = 0
        connectors.append(lead)
        cursor = cursor + lead + net_step(motifs[r])
        occ_figs.append(_copy_fig(motifs[r]))

    # verdict 4: quantize figure STARTS to the beat grid by extending the
    # previous figure's final note; elide_prob keeps raw concatenation.
    joins = []
    t = total_beats(occ_figs[0])
    for i in range(1, len(occ_figs)):
        off_grid = abs(t / grid - round(t / grid)) > 1e-3
        if off_grid and rng.random() >= elide_prob:
            pad = round(_math.ceil(t / grid - 1e-6) * grid - t, 6)
            last = occ_figs[i - 1]["units"][-1]
            last["duration"] = round(last["duration"] + pad, 6)
            t = round(t + pad, 6)
            joins.append("pad")
        else:
            joins.append("elide" if off_grid else "grid")
        t = round(t + total_beats(occ_figs[i]), 6)

    # verdict 3: final note >= phrase median pulse, p=final_ext_prob
    durs = [u["duration"] for f in occ_figs for u in f["units"]]
    med = _stats.median(durs)
    last = occ_figs[-1]["units"][-1]
    final_ext = False
    if last["duration"] < med and rng.random() < final_ext_prob:
        last["duration"] = float(round(med, 6))
        final_ext = True

    motif_list = [(f"F{i}", occ_figs[i]) for i in range(len(occ_figs))]
    info = {"joins": joins, "final_ext": final_ext, "median_pulse": med}
    return motif_list, refs, connectors, info


def _phrase_stats(piece_json_path, fig_note_counts):
    """Mechanical verification stats parsed from a rendered piece JSON."""
    ev = json.loads(pathlib.Path(piece_json_path).read_text(encoding="utf-8"))
    notes = [e for e in ev["parts"][0]["events"] if e.get("type") == "note"]
    beats = [float(e["beat"]) for e in notes]
    durs = [float(e["data"]["duration"]) for e in notes]
    tot = beats[-1] + durs[-1]
    med = _stats.median(durs)
    starts, s = [], 0
    for c in fig_note_counts:
        starts.append(s)
        s += c
    join_beats = [beats[i] for i in starts[1:]]
    on_grid = sum(1 for b in join_beats if abs(b - round(b)) < 1e-3)
    return {"total_beats": tot, "n_notes": len(notes),
            "mean_pulse": sum(durs) / len(durs),
            "final_ratio": durs[-1] / med if med > 0 else 0.0,
            "n_joins": len(join_beats), "joins_on_grid": on_grid}


def _print_stats_table(label, rows):
    tots = sorted(r["total_beats"] for r in rows)
    fr = [r["final_ratio"] for r in rows]
    nj = sum(r["n_joins"] for r in rows)
    ng = sum(r["joins_on_grid"] for r in rows)
    short = [r for r in rows if r["mean_pulse"] <= 0.375]
    lng = [r for r in rows if r["mean_pulse"] > 0.375]

    def mean(xs, k):
        return sum(x[k] for x in xs) / len(xs) if xs else float("nan")
    print(f"--- {label} (n={len(rows)}) ---")
    print(f"  phrase total beats     min={tots[0]:.2f} "
          f"median={tots[len(tots)//2]:.2f} max={tots[-1]:.2f}")
    print(f"  short-pulse (<=0.375)  n={len(short)} "
          f"mean_notes={mean(short,'n_notes'):.1f} "
          f"mean_beats={mean(short,'total_beats'):.2f}")
    print(f"  long-pulse  (>0.375)   n={len(lng)} "
          f"mean_notes={mean(lng,'n_notes'):.1f} "
          f"mean_beats={mean(lng,'total_beats'):.2f}")
    print(f"  final-note ratio >=1   {sum(1 for x in fr if x >= 1-1e-9)}"
          f"/{len(fr)}  (median ratio {sorted(fr)[len(fr)//2]:.2f})")
    print(f"  joins on integer beat  {ng}/{nj} ({ng/max(1,nj):.2f})")


def main_v3():
    from markov_model import MarkovModel
    import score_generated as sg
    import csv as _csv
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=24)
    ap.add_argument("--seed", type=int, default=1234)
    ap.add_argument("--bpm", type=float, default=84.0)
    ap.add_argument("--range-cap", type=int, default=19)
    ap.add_argument("--max-tries", type=int, default=24)
    ap.add_argument("--skip-before", action="store_true",
                    help="skip the baseline (before) batch")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    model = MarkovModel.load()
    cs = sg.corpus_stats()
    outdir = REPO / "renders/markov_phrases3"
    (outdir / "before").mkdir(parents=True, exist_ok=True)

    # ---------------- BEFORE batch: pre-verdict behavior --------------------
    before_rows = []
    if not args.skip_before:
        brng = random.Random(args.seed + 77)
        for i in range(args.n):
            pattern = brng.choice(PATTERNS)
            transform = brng.choice(["invert", "retrograde", "vary_tail"])
            contour = brng.choice(list(CONTOURS))
            kmin = 4 if transform == "vary_tail" else 3
            tries = 0
            while True:
                tries += 1
                figA = _sample_figure(model, brng, kmin=kmin)   # kmax=5 (old)
                figB = _sample_figure(model, brng, kmin=kmin)
                motifs, refs, conns, _anch, _td = build_contour_combination(
                    figA, figB, pattern, transform, contour, model, brng)
                span = predicted_range(motifs, refs, conns)
                if span <= args.range_cap or tries >= args.max_tries:
                    break
            t = make_template(motifs, refs, conns, bpm=args.bpm,
                              seed=args.seed * 1000 + i)
            prefix = f"renders/markov_phrases3/before/b{i:02d}"
            render_template(t, prefix)
            counts = [len(motifs[r]["units"]) for r in refs]
            before_rows.append(_phrase_stats(
                REPO / (prefix + "_1.json"), counts))

    # ---------------- AFTER batch: verdicts 1-5 -----------------------------
    after_rows, meta = [], []
    for i in range(args.n):
        pattern = rng.choice(V3_PATTERNS)
        transform = (rng.choice(V3_TRANSFORMS) if "'" in pattern else "none")
        contour = rng.choice(list(CONTOURS))
        use_ctr = (i % 2 == 1)                       # half contrast-B
        tries = 0
        while True:
            tries += 1
            kmin = 4 if transform == "vary_tail" else 3
            figA = _sample_figure_sized(model, rng, kmin=kmin)
            if use_ctr:
                figB, _fit = sample_contrast_figB(figA, model, rng)
            else:
                figB = _sample_figure_sized(model, rng)
            motif_list, refs, conns, info = build_phrase_v3(
                figA, figB, pattern, transform, contour, model, rng)
            mdict = dict(motif_list)
            names = [n for n, _ in motif_list]
            span = predicted_range(mdict, names, conns)
            if span <= args.range_cap or tries >= args.max_tries:
                break
        t = make_template(mdict, names, conns, bpm=args.bpm,
                          seed=args.seed * 1000 + 500 + i)
        tlabel = transform.replace("_", "") if transform != "none" else "plain"
        name = (f"p{i:02d}_{pattern.replace(chr(39), 'x')}_"
                f"{contour.replace('_', '')}_{tlabel}_"
                f"{'ctrB' if use_ctr else 'indB'}")
        prefix = f"renders/markov_phrases3/{name}"
        render_template(t, prefix)
        counts = [len(f["units"]) for _, f in motif_list]
        st = _phrase_stats(REPO / (prefix + "_1.json"), counts)
        after_rows.append(st)
        jsum = ",".join(info["joins"])
        print(f"{name}: {st['n_notes']} notes {st['total_beats']:.2f} beats "
              f"range={span} tries={tries} joins=[{jsum}] "
              f"final_ext={info['final_ext']}")
        meta.append((name, pattern, contour, transform,
                     "ctrB" if use_ctr else "indB", span, prefix))

    # ---------------- verification table ------------------------------------
    print("\n=== before/after verification ===")
    if before_rows:
        _print_stats_table("BEFORE (kmax=5, no sizing/final/join rules)",
                           before_rows)
    _print_stats_table("AFTER (verdicts 1-5)", after_rows)

    # ---------------- scoring -> scores.csv ---------------------------------
    cols = ["file", "pattern", "contour", "transform", "b_arm", "n_notes",
            "int_jsd", "ctr_jsd", "rep_LxCount", "big_leap", "range",
            "zero_rate", "max_run_frac", "selfsim", "composite"]
    rows = []
    for name, pattern, contour, transform, b_arm, span, prefix in meta:
        r = sg.score(sg.load_melody(REPO / (prefix + "_1.json")), cs)
        r.update(file=name + "_1.json", pattern=pattern, contour=contour,
                 transform=transform, b_arm=b_arm)
        rows.append(r)
    csv_path = outdir / "scores.csv"
    with open(csv_path, "w", newline="") as f:
        w = _csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        w.writerows(rows)
    comps = [r["composite"] for r in rows]
    print(f"\n=== scores (n={len(rows)}) -> {csv_path} ===")
    print(f"composite mean={sum(comps)/len(comps):.3f} "
          f"min={min(comps):.3f} max={max(comps):.3f}")
    low = [(r['file'], r['composite']) for r in rows if r['composite'] < 0.6]
    print("flagged <0.6: " + (", ".join(f"{f} ({c})" for f, c in low)
                              if low else "none"))


if __name__ == "__main__":
    if "--v3" in sys.argv:
        sys.argv.remove("--v3")
        main_v3()
    elif "--v2" in sys.argv:
        sys.argv.remove("--v2")
        main_v2()
    elif "--contrast" in sys.argv:
        sys.argv.remove("--contrast")
        main_contrast()
    else:
        main()
