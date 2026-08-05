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


# --------------------------------------------------------------------------- #
# Backlog #10: the transform library drives A-family variants.
# --------------------------------------------------------------------------- #
# figure_transforms.py landed in run 8 as a standalone library with nine
# operations; until now the phrase builder could only reach two of them
# (invert / retrograde, hard-coded) plus vary_tail. Everything below routes
# A-family primes through the library instead, so a repeat can be transposed
# (contour) AND transform-varied at full generality.
import figure_transforms as ftl                                   # noqa: E402

# Ops that produce a DIFFERENT result on each successive occurrence — either
# because they are stochastic, or because the occurrence index feeds the
# parameter. Each prime therefore gets its own variant motif (V1, V2, ...).
# Value = f(figA, occurrence, model, rng) -> fig.
#
# augment/diminish take the occurrence into the factor so A'' is twice as
# augmented as A' (a real intensification, not the same op twice); rotate
# advances the rotation; expand/compress_intervals widen progressively.
INDEPENDENT_OPS = {
    "vary_tail":         lambda f, occ, m, r: vary_tail(f, m, r)[0],
    "rotate":            lambda f, occ, m, r: ftl.rotate(f, occ),
    "ornament":          lambda f, occ, m, r: ftl.ornament(f, r),
    "augment":           lambda f, occ, m, r: ftl.augment(f, 1.5 ** occ),
    "diminish":          lambda f, occ, m, r: ftl.diminish(f, 1.5 ** occ),
    "expand_intervals":  lambda f, occ, m, r: ftl.expand_intervals(f, 1.0 + occ),
    "compress_intervals":lambda f, occ, m, r: ftl.compress_intervals(f, 1.0 + occ),
}

# Ops that are involutions (applying twice returns the original), so every
# prime shares ONE variant motif "P" — the historical behaviour.
INVOLUTION_OPS = {"invert", "retrograde"}

# "mixed" draws a different independent op per occurrence, which is what a
# composer varying a motif across a phrase actually does — not the same
# operation applied N times.
MIXED_MENU = ["rotate", "ornament", "vary_tail", "expand_intervals",
              "augment", "diminish"]


def make_variant(figA, transform, occ, model, rng, seen=()):
    """The A-family variant for occurrence `occ` (1-based: A' is occ 1).
    Returns (fig, op_label) so the caller can report what was actually used.

    `seen` is the figures already emitted for this phrase (A itself plus any
    earlier variants). Occurrence-parameterized ops are not guaranteed to
    differ: compress_intervals saturates once every step is at the
    minimum magnitude of 1, so factors 2 and 3 return the SAME figure on a
    stepwise cell. When that happens the variant is rotated (and, failing
    that, ornamented) so "each repeat varies differently" is a property the
    builder enforces rather than one it hopes for. The label records the
    fallback, e.g. "compress_intervals+rot"."""
    op = transform
    if transform == "mixed":
        op = rng.choice(MIXED_MENU)
    fn = INDEPENDENT_OPS.get(op)
    if fn is None:
        raise KeyError(f"make_variant: no independent op named {op!r}")
    fig = fn(figA, occ, model, rng)
    if any(fig == s for s in seen):
        fig, op = ftl.rotate(fig, occ), op + "+rot"
    if any(fig == s for s in seen):
        fig, op = ftl.ornament(fig, rng), op.split("+")[0] + "+orn"
    return fig, op


def _tokens_v2(pattern, transform):
    """Like _tokens, but transforms whose repeats differ per occurrence give
    each prime an INDEPENDENT variant: "AA'A''B" -> ["A","V1","V2","B"].
    Involutions (invert/retrograde) keep the single shared "P"."""
    independent = (transform in INDEPENDENT_OPS or transform == "mixed")
    out = []
    for ch in pattern:
        if ch == "'":
            prev = out[-1]
            if independent:
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

    Returns (motifs, refs, connectors, anchors, variant_ops) where anchors is
    the per-figure anchor degree (for verification) and variant_ops maps
    variant name -> the library op that produced it (backlog #10; previously
    this reported vary_tail's tail-step diff count, which only existed for
    the one op the builder could reach)."""
    refs = _tokens_v2(pattern, transform)
    motifs = {"A": figA, "B": figB}
    variant_ops = {}
    for r in refs:
        if r == "P" and r not in motifs:
            motifs["P"] = invert(figA) if transform == "invert" else retrograde(figA)
        elif r.startswith("V") and r not in motifs:
            occ = int(r[1:])
            motifs[r], variant_ops[r] = make_variant(
                figA, transform, occ, model, rng,
                seen=[figA] + [motifs[k] for k in motifs if k.startswith("V")])
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
    return motifs, refs, connectors, anchors, variant_ops


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
        # An independent-variant op needs enough units to be worth varying
        # (vary_tail wants a tail of >=2) -> kmin=4
        kmin = 4 if (transform in INDEPENDENT_OPS or transform == "mixed") else 3
        # Accept-first-under-cap; over-cap candidates are rerolled (fresh
        # figures — spec is fixed per slot, so no contour fallback that would
        # make the filename lie); on exhaustion keep the LAST draw, flagged.
        tries = 0
        while True:
            tries += 1
            figA = _sample_figure(model, rng, kmin=kmin)
            figB = _sample_figure(model, rng, kmin=kmin)
            motifs, refs, conns, anchors, vops = build_contour_combination(
                figA, figB, pattern, transform, contour, model, rng)
            span = predicted_range(motifs, refs, conns)
            if args.range_cap <= 0 or span <= args.range_cap \
                    or tries >= args.max_tries:
                break
        seed_i = args.seed * 1000 + i
        t = make_template(motifs, refs, conns, bpm=args.bpm, seed=seed_i)
        name = (f"p{i:02d}_{pattern.replace(chr(39), 'x')}_"
                f"{contour.replace('_', '')}_"
                f"{transform.replace('_', '') if transform else 'lit'}")
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
        ops = ("  ops=" + ",".join(f"{k}:{v}" for k, v
               in sorted(vops.items()))) if vops else ""
        print(f"{name}: {len(notes)} notes range={span:2d} tries={tries} "
              f"seq_verify={'OK' if seq_ok else 'FAIL'}{ops}{flag}")
        meta.append((name, family, pattern, contour,
                     transform or "none", span, seq_ok, prefix))

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
                 "diminish", "vary_tail", "mixed"]
# widen upward: base patterns + two longer ones (short end preserved).
# The two MULTI-prime patterns exist so the per-occurrence variant path
# (backlog #10) is actually exercised — with only single-prime patterns in the
# pool, V2 never appears and "each repeat varies differently" is untested.
V3_PATTERNS = PATTERNS + ["AA'BAB", "AABA'B", "AA'A''B", "AA'BA''B"]


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


# --------------------------------------------------------------------------- #
# Backlog #10, full generality (Matt's repeat-variety spec): LITERAL repeats.
# --------------------------------------------------------------------------- #
# Prime tokens (A') already route through figure_transforms, but a literal
# repeat — the second A in AAB / AABB / ABAB, a repeated B — could only be
# varied by transposition (contour anchoring). Now a later occurrence of an
# already-heard family may OCCASIONALLY be transform-varied instead of
# repeating verbatim. Kin-recognizability rules: exactly ONE transform per
# occurrence (never stacked), the first occurrence of a family is never
# touched, and the mix ratio is conservative (most repeats stay literal).
# vary_tail is deliberately absent — it is already reachable via primes; this
# menu is the variation literal repeats never had. augment/diminish are also
# out: reshaping an occurrence's total length mid-pattern breaks the repeat
# scheme's rhythmic footprint, the property that makes a repeat read as one.
REPEAT_XFORM_MENU = ["invert", "retrograde", "rotate", "ornament",
                     "expand_intervals", "compress_intervals"]


def _repeat_xform(fig, rng):
    """Draw ONE library transform for a literal-repeat occurrence.
    Returns (varied_fig, op_name), or (None, None) when no eligible op exists
    or the drawn op is an identity on this figure (compress_intervals on a
    stepwise cell, retrograde on a palindrome) — an unrecognizable 'variation'
    is not emitted, the occurrence stays a verbatim repeat."""
    n = len(fig["units"])
    moves = any(u["step"] != 0 for u in fig["units"])
    menu = []
    for op in REPEAT_XFORM_MENU:
        if not moves and op in ("invert", "retrograde",
                                "expand_intervals", "compress_intervals"):
            continue                          # identity on a static figure
        if op == "rotate" and n < 3:
            continue                          # 2-note rotation is a swap
        if op in ("ornament", "retrograde") and n < 2:
            continue
        menu.append(op)
    if not menu:
        return None, None
    op = rng.choice(menu)
    if op == "ornament":
        out = ftl.ornament(fig, rng)
    elif op == "rotate":
        out = ftl.rotate(fig, 1)
    elif op in ("expand_intervals", "compress_intervals"):
        out = getattr(ftl, op)(fig, 2.0)
    else:                                     # invert / retrograde
        out = getattr(ftl, op)(fig)
    if out == fig:
        return None, None
    return out, op


# Probability that the v4 "calib" final-note rule makes the last note EQUAL
# the phrase's longest (backlog #16). Matt's standing verdict wants the final
# note to be the longest much more often than the corpus does (MTD 0.354), so
# this is deliberately above the corpus rate; the MAGNITUDE, unlike v3, is
# bounded by the phrase's own longest note. Measured effect at n=200
# (final_rule_sweep.py): 0.6 -> ratio p50 2.50 / longest 0.630.
CALIB_LONGEST_P = 0.6

# Backlog #17. True = revert the repeat-transform whose removal recovers the
# most span; False = the v1 behaviour (revert last-applied first), kept only
# so the two can be measured against each other (range_guard_ab.py).
RANGE_GUARD_OFFENDER_FIRST = True

_FINAL_PROFILE = None


def _hist_pct(hist, q):
    """Percentile of a {ratio: count} histogram (final_note_profile.json)."""
    if not hist:
        return float("inf")
    vals = []
    for k, w in hist.items():
        vals.extend([float(k)] * int(w))
    vals.sort()
    return vals[min(len(vals) - 1, int(q * len(vals)))]


def _final_profile(corpus="mtd"):
    """Empirical final-note ratio profile from final_note_stats.py. Falls back
    to the MTD profile for corpora with no measurement of their own (Essen),
    and to None (rule disabled) if the profile was never generated."""
    global _FINAL_PROFILE
    if _FINAL_PROFILE is None:
        p = pathlib.Path(__file__).resolve().parent / "final_note_profile.json"
        try:
            _FINAL_PROFILE = json.loads(p.read_text(encoding="utf-8"))
        except OSError:
            _FINAL_PROFILE = {}
    return _FINAL_PROFILE.get(corpus) or _FINAL_PROFILE.get("mtd")


def _snap_dur(beats):
    """Snap to the tokenizer's duration grid so extensions stay notatable."""
    from markov_tokenize import snap_pulse
    return snap_pulse(beats)


def build_phrase_v3(figA, figB, pattern, transform, contour, model, rng,
                    elide_prob=0.2, final_ext_prob=0.85, grid=1.0,
                    corpus="mtd", final_rng=None, final_rule="longest",
                    repeat_xform_prob=0.0, xform_rng=None, range_cap=None):
    """Materialize per-occurrence figures with contour leadSteps, then apply
    join quantization (verdict 4) and final-note treatment (verdict 3).

    repeat_xform_prob (backlog #10 full generality): probability that a
    LITERAL repeat of an already-heard family (the 2nd A in AAB/AABB/ABAB, a
    repeated B) is transform-varied via _repeat_xform instead of repeating
    verbatim. 0.0 = off (historical behavior). Decisions draw ONLY from
    xform_rng — a dedicated stream, so enabling/disabling the mechanism never
    shifts the joins/variants stream and a same-seed A/B stays like-for-like.
    range_cap: if set, a transform that pushes the phrase's predicted span
    over the cap is reverted (last-applied first) — a transform never causes
    an out-of-range phrase; the untransformed path is the fallback.

    Returns (motif_list, refs, connectors, info): motif_list is an ordered
    [(name, fig)] with unique per-occurrence names (padding differs per
    occurrence, so motifs can't be shared by reference); info carries join
    kinds + final-extension flag + applied repeat transforms for reporting."""
    refs = _tokens_v2(pattern, transform)
    motifs = {"A": figA, "B": figB}
    ops = {}
    for r in refs:
        if r not in motifs:
            if r.startswith("V"):
                # Per-occurrence independent variant (backlog #10). Before
                # this, EVERY V token was vary_tail regardless of the chosen
                # transform — so "AA'A''B" with transform=rotate rendered two
                # resampled tails and no rotation at all.
                motifs[r], ops[r] = make_variant(
                    figA, transform, int(r[1:]), model, rng,
                    seen=[figA] + [motifs[k] for k in motifs if k.startswith("V")])
            else:                                    # "P" — involution
                motifs[r] = _apply_v3_transform(transform, figA, model, rng)
                ops[r] = transform

    # contour leadSteps (same math as build_contour_combination) — computed
    # from the PER-OCCURRENCE figures, so a varied occurrence still lands on
    # its contour anchor (leadStep places the START; only the cursor after it
    # sees the variant's net step).
    offsets = CONTOURS[contour]

    def _materialize(seq):
        occ = [_copy_fig(seq[0])]
        conns = [None]
        cursor = net_step(seq[0])
        a_seen = 1
        for i in range(1, len(refs)):
            r = refs[i]
            if r != "B":
                off = offsets[min(a_seen, len(offsets) - 1)]
                a_seen += 1
                lead = off - cursor
            else:
                lead = 0
            conns.append(lead)
            cursor = cursor + lead + net_step(seq[i])
            occ.append(_copy_fig(seq[i]))
        return occ, conns

    def _span(occ, conns):
        md = {f"F{i}": f for i, f in enumerate(occ)}
        return predicted_range(md, list(md), conns)

    base_seq = [_copy_fig(motifs[r]) for r in refs]
    occ_figs, connectors = _materialize(base_seq)

    # Literal-repeat transform pass (backlog #10 full generality). Per
    # occurrence: only tokens that ARE literal repeats ("A"/"B" of a family
    # already heard; primes P/V* were varied above), one op, dedicated rng.
    # Gated on the UNTRANSFORMED phrase being under the range cap: an over-cap
    # draw is left untouched so the caller's reject-loop sees the identical
    # span (and consumes the identical number of tries) whether the mechanism
    # is on or off — a transform must never rescue a draw the baseline arm
    # would have rerolled, or same-seed pairs stop being like-for-like.
    repeat_ops = []
    reverted_ops = []
    if xform_rng is not None and repeat_xform_prob > 0 \
            and not (range_cap and _span(occ_figs, connectors) > range_cap):
        seen_fams = set()
        for idx, r in enumerate(refs):
            fam = "B" if r == "B" else "A"
            if r in ("A", "B") and fam in seen_fams \
                    and xform_rng.random() < repeat_xform_prob:
                varied, op = _repeat_xform(motifs[r], xform_rng)
                if varied is not None:
                    base_seq[idx] = varied
                    repeat_ops.append({"idx": idx, "ref": r, "op": op})
            seen_fams.add(fam)
        if repeat_ops:
            occ_figs, connectors = _materialize(base_seq)
            # Range guard: never emit out-of-range BECAUSE of a transform.
            # Revert until back under cap (fully reverted == the untransformed
            # phrase, already verified under cap).
            #
            # OFFENDER-FIRST (backlog #17): revert the transform whose removal
            # buys the most span, not the last one applied. Reverting
            # last-first punishes an innocent late transform for an earlier
            # one's overshoot — run 16's p18 lost its ornament so that an
            # invert three occurrences earlier could stay. Ties go to the
            # later op, which is the old behaviour, so nothing moves unless
            # the offender really is a different one. No rng is consumed
            # either way, so the A/B stays like-for-like.
            while repeat_ops and range_cap \
                    and _span(occ_figs, connectors) > range_cap:
                if RANGE_GUARD_OFFENDER_FIRST:
                    best = None
                    for j, ro in enumerate(repeat_ops):
                        trial = list(base_seq)
                        trial[ro["idx"]] = _copy_fig(motifs[refs[ro["idx"]]])
                        s = _span(*_materialize(trial))
                        if best is None or s <= best[0]:
                            best = (s, j)
                    pick = best[1]
                else:
                    pick = len(repeat_ops) - 1        # v1: last-applied first
                dropped = repeat_ops.pop(pick)
                reverted_ops.append(dropped)
                base_seq[dropped["idx"]] = \
                    _copy_fig(motifs[refs[dropped["idx"]]])
                occ_figs, connectors = _materialize(base_seq)

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

    # verdict 3 v2 — Matt (run 12): "still need more weighting of final note
    # being longer/longest duration". v1 set the final note to the phrase
    # MEDIAN pulse, i.e. a ratio of exactly 1.0. final_note_stats.py measured
    # what real themes do (n=1632 MTD / 1024 Nottingham, same pulse convention
    # as the tokenizer): median ratio 2.0 in BOTH corpora, and the final note
    # is >= every other note in 35%/53% of themes. A ratio of 1.0 is MTD's p25
    # and Nottingham's p10 — the rule was landing at the short end of real
    # practice. So the ratio is now DRAWN from the corpus distribution rather
    # than fixed, snapped to the duration grid, and only ever EXTENDS.
    # Then the phrase end is grid-completed to an integer beat (same reasoning
    # as verdict 4's joins: a phrase stops at a barline, not mid-beat).
    durs = [u["duration"] for f in occ_figs for u in f["units"]]
    rest = durs[:-1] or durs                 # corpus convention: exclude final
    med = _stats.median(rest)
    last = occ_figs[-1]["units"][-1]
    orig_final = last["duration"]
    final_ext = False
    # Own rng stream: the ratio draw must not shift the main stream, or the
    # A/B against markov_phrases3 (same seed, old rule) stops being
    # like-for-like — every phrase after the first would differ in structure
    # as well as in its ending.
    frng = final_rng if final_rng is not None else rng
    if final_rule == "old":
        # v1 arm, kept renderable so the change can be A/B'd against a batch
        # that is otherwise bit-identical (same figures, joins, transforms).
        if last["duration"] < med and frng.random() < final_ext_prob:
            last["duration"] = float(round(med, 6))
            final_ext = True
    else:
        if final_rule == "corpus":
            prof = _final_profile(corpus)
            if prof and prof.get("hist") and frng.random() < final_ext_prob:
                keys, wts = zip(*prof["hist"].items())
                ratio = float(frng.choices(keys, weights=wts, k=1)[0])
                target = _snap_dur(med * ratio)
                if target > last["duration"] + 1e-9:
                    last["duration"] = float(round(target, 6))
                    final_ext = True
        elif final_rule == "longest":
            # verdict 3 v3 — Matt (markov_phrases3/run 15): "These are good.
            # Still need more weighting of final note being longer/longEST
            # duration." The corpus draw put the final at the corpus median
            # (~2x pulse) but rarely made it the phrase MAXIMUM. Weighting:
            #   p=0.6  final becomes the LONGEST note of the phrase
            #          (max of all other durations * U[1.1, 1.6])
            #   p=0.3  final at least 1.5x the median pulse
            #   p=0.1  left as drawn (variety)
            # Superseded as the default by "calib" (backlog #16) — the
            # max*U[1.1,1.6] anchor compounds: a generated phrase's own
            # longest note is already median 2.1x / p95 8x the median pulse
            # (final_rule_sweep.py, n=200), so multiplying it again lands the
            # batch at ratio p50 3.0 / p95 11.0 / max 29 against a corpus of
            # 2.0 / 6.0. Kept renderable as the A/B arm.
            roll = frng.random()
            target = None
            if roll < 0.6:
                target = max(rest) * frng.uniform(1.1, 1.6)
            elif roll < 0.9:
                target = 1.5 * med
            if target is not None:
                snapped = _math.ceil(target / 0.25 - 1e-9) * 0.25
                if snapped > last["duration"] + 1e-9:
                    last["duration"] = float(round(snapped, 6))
                    final_ext = True
        else:
            # v4 "calib" — backlog #16. v3 kept Matt's verdict (the final note
            # should usually be the phrase's longest) but paid for it with a
            # magnitude the corpus never shows. The fix separates the two:
            #   FREQUENCY of final-is-longest is preserved (same 0.6 branch),
            #   MAGNITUDE is bounded by the phrase's OWN longest note instead
            #   of a multiplier on top of it. The rule can no longer invent a
            #   duration longer than something already heard in the phrase.
            #   p=0.6  final EQUALS the longest note of the phrase
            #   p=0.3  ratio drawn from the corpus histogram, restricted to
            #          >= 1.5x median (corpus-shaped, not a fixed 1.5)
            #   p=0.1  left as drawn (variety)
            # Extend-only + 0.25 snap + grid completion as in v3.
            roll = frng.random()
            target = None
            if roll < CALIB_LONGEST_P:
                target = max(rest)
            elif roll < CALIB_LONGEST_P + 0.3:
                # Restricted to [1.5, corpus p95]: the rule may never INVENT an
                # ending past the corpus 95th percentile. (Branch A can still
                # exceed it — but only by matching a long note the phrase
                # already contains, which is inherited, not invented. Measured:
                # the unrestricted draw put p22 of the 24-batch at 7.6x from a
                # single rare 7.385 bin.)
                prof = _final_profile(corpus)
                hist = (prof or {}).get("hist") or {}
                hi = _hist_pct(hist, 0.95)
                qual = [(float(k), w) for k, w in hist.items()
                        if 1.5 - 1e-9 <= float(k) <= hi + 1e-9]
                if qual:
                    ks, ws = zip(*qual)
                    target = med * float(frng.choices(ks, weights=ws, k=1)[0])
                else:
                    target = 1.5 * med
            if target is not None:
                snapped = _math.ceil(target / 0.25 - 1e-9) * 0.25
                if snapped > last["duration"] + 1e-9:
                    last["duration"] = float(round(snapped, 6))
                    final_ext = True
        pre_grid = last["duration"]
        total = round(sum(total_beats(f) for f in occ_figs), 6)
        if abs(total / grid - round(total / grid)) > 1e-3:
            if final_rule == "calib":
                # Backlog #16, the real overshoot mechanism. v3 always CEILED
                # the phrase total onto the next barline, so grid completion
                # added a uniform [0, 1) beats ON TOP of whatever the ratio
                # draw asked for — measured at p50 +0.25 beats, which moved the
                # batch ratio p50 from 2.00 (the corpus median, i.e. the draw
                # was already right) to 3.00. Rounding to the NEAREST landing
                # instead lets the grid absorb part of the drawn extension as
                # often as it adds to it. Floored so the final note is never
                # shortened below what it had before the rule ran.
                floor_total = round(total - last["duration"] + orig_final, 6)
                cand = round(total / grid) * grid
                if cand < floor_total - 1e-9:
                    cand = _math.ceil(floor_total / grid - 1e-6) * grid
                pad = cand - total
            else:
                pad = _math.ceil(total / grid - 1e-6) * grid - total
            last["duration"] = float(round(last["duration"] + pad, 6))
            final_ext = final_ext or abs(pad) > 1e-9
        grid_pad = round(last["duration"] - pre_grid, 6)

    motif_list = [(f"F{i}", occ_figs[i]) for i in range(len(occ_figs))]
    info = {"joins": joins, "final_ext": final_ext, "median_pulse": med,
            "grid_pad": locals().get("grid_pad", 0.0),
            "final_ratio": last["duration"] / med if med > 0 else 0.0,
            "ops": ops, "repeat_ops": repeat_ops,
            "reverted_ops": reverted_ops}
    return motif_list, refs, connectors, info


def _phrase_stats(piece_json_path, fig_note_counts):
    """Mechanical verification stats parsed from a rendered piece JSON."""
    ev = json.loads(pathlib.Path(piece_json_path).read_text(encoding="utf-8"))
    notes = [e for e in ev["parts"][0]["events"] if e.get("type") == "note"]
    beats = [float(e["beat"]) for e in notes]
    durs = [float(e["data"]["duration"]) for e in notes]
    tot = beats[-1] + durs[-1]
    # Corpus convention (final_note_stats.py): median/max exclude the final
    # note, so the generated ratios are directly comparable to the corpus ones.
    rest = durs[:-1] or durs
    med = _stats.median(rest)
    starts, s = [], 0
    for c in fig_note_counts:
        starts.append(s)
        s += c
    join_beats = [beats[i] for i in starts[1:]]
    on_grid = sum(1 for b in join_beats if abs(b - round(b)) < 1e-3)
    return {"total_beats": tot, "n_notes": len(notes),
            "mean_pulse": sum(durs) / len(durs),
            "final_ratio": durs[-1] / med if med > 0 else 0.0,
            "final_ge_max": 1 if durs[-1] >= max(rest) - 1e-9 else 0,
            "end_on_beat": 1 if abs(tot - round(tot)) < 1e-3 else 0,
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
    fs = sorted(fr)
    print(f"  final/median ratio     min={fs[0]:.2f} "
          f"median={fs[len(fs)//2]:.2f} max={fs[-1]:.2f}")
    print(f"  final-note ratio >=1   {sum(1 for x in fr if x >= 1-1e-9)}"
          f"/{len(fr)}")
    print(f"  final-note ratio >=1.5 {sum(1 for x in fr if x >= 1.5-1e-9)}"
          f"/{len(fr)} ({sum(1 for x in fr if x >= 1.5-1e-9)/len(fr):.2f})")
    # Corpus targets from final_note_stats.py (MTD n=1632 / Nottingham n=1024):
    # median ratio 2.00 / 2.00, ratio>=2 in 0.51 / 0.85, final>=max in
    # 0.35 / 0.53. Printed alongside so the comparison is on-screen.
    print(f"  final-note ratio >=2   {sum(1 for x in fr if x >= 2-1e-9)}"
          f"/{len(fr)} ({sum(1 for x in fr if x >= 2-1e-9)/len(fr):.2f}) "
          f"[corpus mtd 0.51 / nott 0.85]")
    print(f"  final note is longest  "
          f"{sum(r['final_ge_max'] for r in rows)}/{len(rows)} "
          f"({sum(r['final_ge_max'] for r in rows)/len(rows):.2f}) "
          f"[corpus mtd 0.35 / nott 0.53]")
    print(f"  phrase ends on a beat  "
          f"{sum(r['end_on_beat'] for r in rows)}/{len(rows)}")
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
    ap.add_argument("--corpus", default="mtd",
                    help="corpus flavor: which token file trains the model AND "
                         "which baseline scores it (mtd | nottingham | essen | "
                         "essen_europa | essen_asia). Non-mtd writes to "
                         "renders/corpus_flavors/<corpus>/.")
    ap.add_argument("--outdir", default=None,
                    help="override the render directory (repo-relative)")
    ap.add_argument("--final-rule", default="calib",
                    choices=["calib", "longest", "corpus", "old"],
                    dest="final_rule",
                    help="final-note treatment: 'calib' (v4, default) makes "
                         "the final note EQUAL the phrase's longest note with "
                         "p=0.6, a corpus-histogram ratio >=1.5x median with "
                         "p=0.3, as-drawn p=0.1 — same longest-frequency as "
                         "v3 with the magnitude bounded by the phrase itself; "
                         "'longest' (v3) makes "
                         "the final note the phrase maximum with p=0.6, "
                         ">=1.5x median with p=0.3, as-drawn p=0.1; 'corpus' "
                         "draws the ratio from final_note_profile.json; 'old' "
                         "is the v1 rule (extend to the median pulse). All "
                         "grid-complete except 'old'. The draws use their own "
                         "rng stream, so arms differ ONLY in the final note.")
    ap.add_argument("--transform", default=None, choices=V3_TRANSFORMS,
                    help="pin the A-family transform for every prime-bearing "
                         "pattern instead of rolling it — the controlled A/B "
                         "for backlog #10. 'mixed' draws a DIFFERENT op per "
                         "occurrence.")
    ap.add_argument("--repeat-xform-prob", type=float, default=0.3,
                    dest="repeat_xform_prob",
                    help="probability a LITERAL repeat of an already-heard "
                         "family (2nd A in AAB/AABB/ABAB, repeated B) is "
                         "transform-varied (one op from REPEAT_XFORM_MENU) "
                         "instead of repeating verbatim. Conservative default "
                         "0.3 — an occasional alternative to transposition, "
                         "not a replacement. 0 disables (the A/B baseline).")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    # Corpus flavor: subsets (essen_europa/_asia) share one token file — the
    # SUBSET only re-anchors the scorer, the model still trains on all of Essen.
    import markov_model as mm
    tok_name = args.corpus.split("_")[0]
    tokens = (mm.TOKENS if tok_name == "mtd"
              else mm.ROOT / f"{tok_name}_tokens.json")
    model = MarkovModel.load(tokens)
    cs = sg.corpus_stats(args.corpus)
    if args.outdir:
        rel = args.outdir
    elif args.corpus == "mtd":
        rel = "renders/markov_phrases3"
    else:
        rel = f"renders/corpus_flavors/{args.corpus}"
    outdir = REPO / rel
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
            prefix = f"{rel}/before/b{i:02d}"
            render_template(t, prefix)
            counts = [len(motifs[r]["units"]) for r in refs]
            before_rows.append(_phrase_stats(
                REPO / (prefix + "_1.json"), counts))

    # ---------------- AFTER batch: verdicts 1-5 -----------------------------
    # Structure rolls come from their OWN stream so they don't depend on how
    # many draws figure sampling consumed. Same --seed across --corpus values
    # therefore yields the SAME pattern/contour/transform sequence with
    # different corpus content — a controlled corpus-flavor A/B.
    srng = random.Random(args.seed ^ 0x5EED)
    after_rows, meta, manifest = [], [], []
    for i in range(args.n):
        pattern = srng.choice(V3_PATTERNS)
        transform = (srng.choice(V3_TRANSFORMS) if "'" in pattern else "none")
        if args.transform and "'" in pattern:
            transform = args.transform            # pin for a controlled A/B
        contour = srng.choice(list(CONTOURS))
        use_ctr = (i % 2 == 1)                       # half contrast-B
        # Figure CONTENT gets its own per-index stream so two runs that differ
        # only in --transform draw the same figA/figB at every index. Without
        # this the arms diverge after the first phrase whose variant ops
        # consume a different number of rng calls, and the A/B stops being
        # like-for-like (measured: the no-prime p02 came out 20 notes in one
        # arm and 16 in the other, from an identical spec).
        frng = random.Random((args.seed ^ 0xF16E) + i * 7919)
        # Build draws (joins + prime variants) also get a per-index stream.
        # They used to come from the shared `rng`: a repeat transform that
        # changes an occurrence's durations changes how many elide rolls that
        # phrase consumes, so with a shared stream EVERY later phrase would
        # diverge between the transforms-on and transforms-off arms — the
        # same defect the frng comment above documents for figure content.
        brng = random.Random((args.seed ^ 0xB01D) + i * 15485863)
        # Repeat-transform decisions draw ONLY from this dedicated stream
        # (backlog #10): prob=0 consumes nothing, and either way the joins /
        # variants / final-note streams are untouched.
        xrng = random.Random((args.seed ^ 0x0F0F) + i * 60013)
        tries = 0
        while True:
            tries += 1
            kmin = 4 if (transform in INDEPENDENT_OPS
                         or transform == "mixed") else 3
            figA = _sample_figure_sized(model, frng, kmin=kmin)
            if use_ctr:
                figB, _fit = sample_contrast_figB(figA, model, frng)
            else:
                figB = _sample_figure_sized(model, frng)
            motif_list, refs, conns, info = build_phrase_v3(
                figA, figB, pattern, transform, contour, model, brng,
                corpus=args.corpus, final_rule=args.final_rule,
                final_rng=random.Random((args.seed ^ 0xF1A1) + i * 104729),
                repeat_xform_prob=args.repeat_xform_prob, xform_rng=xrng,
                range_cap=args.range_cap)
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
        prefix = f"{rel}/{name}"
        render_template(t, prefix)
        counts = [len(f["units"]) for _, f in motif_list]
        st = _phrase_stats(REPO / (prefix + "_1.json"), counts)
        after_rows.append(st)
        jsum = ",".join(info["joins"])
        osum = ("  ops=" + ",".join(f"{k}:{v}" for k, v
                in sorted(info["ops"].items()))) if info["ops"] else ""
        rxs = ";".join(f"{ro['ref']}@{ro['idx']}:{ro['op']}"
                       for ro in info["repeat_ops"])
        rsum = f"  repeat_xform=[{rxs}]" if rxs else ""
        print(f"{name}: {st['n_notes']} notes {st['total_beats']:.2f} beats "
              f"range={span} tries={tries} joins=[{jsum}] "
              f"final_ext={info['final_ext']}{osum}{rsum}")
        meta.append((name, pattern, contour, transform,
                     "ctrB" if use_ctr else "indB", span, prefix, rxs))
        manifest.append({"file": name + "_1.json", "pattern": pattern,
                         "contour": contour, "prime_transform": transform,
                         "prime_ops": info["ops"],
                         "repeat_xforms": info["repeat_ops"]})

    # ---------------- verification table ------------------------------------
    print("\n=== before/after verification ===")
    if before_rows:
        _print_stats_table("BEFORE (kmax=5, no sizing/final/join rules)",
                           before_rows)
    _print_stats_table("AFTER (verdicts 1-5)", after_rows)

    # ---------------- transform manifest ------------------------------------
    # Sidecar so an audition/report can see WHICH occurrence got WHICH
    # transform without replaying the rng (backlog #10 A/B bookkeeping).
    n_rx = sum(1 for m in manifest if m["repeat_xforms"])
    mpath = outdir / "transforms_manifest.json"
    mpath.write_text(json.dumps(
        {"repeat_xform_prob": args.repeat_xform_prob, "seed": args.seed,
         "n_phrases": len(manifest), "n_with_repeat_xform": n_rx,
         "phrases": manifest}, indent=2), encoding="utf-8")
    print(f"\nrepeat transforms applied in {n_rx}/{len(manifest)} phrases "
          f"(prob={args.repeat_xform_prob}) -> {mpath}")

    # ---------------- scoring -> scores.csv ---------------------------------
    cols = ["file", "pattern", "contour", "transform", "b_arm",
            "repeat_xforms", "n_notes", "int_jsd", "ctr_jsd", "rep_LxCount",
            "big_leap", "range", "zero_rate", "max_run_frac", "selfsim",
            "final_ratio", "closure", "composite"]
    rows = []
    for name, pattern, contour, transform, b_arm, span, prefix, rxs in meta:
        r = sg.score(sg.load_melody(REPO / (prefix + "_1.json")), cs)
        r.update(file=name + "_1.json", pattern=pattern, contour=contour,
                 transform=transform, b_arm=b_arm, repeat_xforms=rxs)
        rows.append(r)
    csv_path = outdir / "scores.csv"
    with open(csv_path, "w", newline="") as f:
        # score() grew closure fields (#12); ignore any future extras rather
        # than crashing the whole batch at the very last step.
        w = _csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
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
